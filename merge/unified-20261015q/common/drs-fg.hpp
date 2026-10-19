#pragma once
// Public DLSS-G policy synchronization. Only the application profile is written;
// explicit default values shadow inherited global overrides without changing them.
namespace dlss5::drs {
inline constexpr uint32_t fg_ids[]={NGX_DLSSG_MODE_ID,NGX_DLSSG_MULTI_FRAME_COUNT_ID,
  NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE_ID,NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX_ID,
  NGX_DLSS_FG_OVERRIDE_ID,NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID,
  NGX_DLSS_FG_OVERRIDE_RESERVED_KEY1_ID,NGX_DLSS_FG_OVERRIDE_RESERVED_KEY2_ID};
inline constexpr unsigned fg_count=static_cast<unsigned>(sizeof(fg_ids)/sizeof(fg_ids[0]));
struct FgJournal {
  uint32_t magic=0x32474644,version=1;
  uint64_t checksum=0,generation=0;
  uint32_t pending=0,valid[fg_count]{},existed[fg_count]{},old[fg_count]{},applied[fg_count]{};
  NvAPI_UnicodeString profile{};
};
inline bool ValidFgJournal(const FgJournal& j) {
  if(j.pending>1 || !j.profile[0]) return false;
  bool terminated=false,any=false;
  for(auto c:j.profile) if(!c) terminated=true;
  for(unsigned i=0;i<fg_count;++i) {
    if(j.valid[i]>1 || j.existed[i]>1 || j.applied[i]!=0) return false;
    any=any || j.valid[i]!=0;
  }
  return terminated && (j.pending!=0)==any;
}
inline durable::ReadResult ReadFgJournal(const std::filesystem::path& dir,const std::filesystem::path& mirror,FgJournal& j) {
  FgJournal a,b;
  auto ar=durable::Read(dir/L"drs-fg.state",a,a.magic),br=durable::Read(mirror/L"drs-fg.state",b,b.magic);
  if(ar==durable::ReadResult::Valid && !ValidFgJournal(a)) ar=durable::ReadResult::Invalid;
  if(br==durable::ReadResult::Valid && !ValidFgJournal(b)) br=durable::ReadResult::Invalid;
  if(ar==durable::ReadResult::Invalid || br==durable::ReadResult::Invalid) return durable::ReadResult::Invalid;
  if(ar==durable::ReadResult::Missing && br==ar) return ar;
  if(ar==durable::ReadResult::Valid && br==durable::ReadResult::Valid) {
    auto ac=a,bc=b; ac.checksum=bc.checksum=0;
    if(a.generation==b.generation && std::memcmp(&ac,&bc,sizeof(ac))) return durable::ReadResult::Invalid;
    j=a.generation>=b.generation?a:b;
  } else j=ar==durable::ReadResult::Valid?a:b;
  return durable::ReadResult::Valid;
}
inline bool SaveFgJournal(const std::filesystem::path& dir,const std::filesystem::path& mirror,FgJournal& j) {
  ++j.generation;
  return durable::Save(mirror/L"drs-fg.state",j) && durable::Save(dir/L"drs-fg.state",j);
}
inline bool FgPending(const std::filesystem::path& dir,const std::filesystem::path& mirror) {
  FgJournal j; const auto r=ReadFgJournal(dir,mirror,j);
  return r==durable::ReadResult::Invalid || (r==durable::ReadResult::Valid && j.pending);
}
inline Result SynchronizeFg(const std::filesystem::path& dir,bool restore,const std::filesystem::path& game) {
  const auto mirror=durable::RecoveryMirror(game);
  if(dir.empty() || mirror.empty()) return "无法记录驱动还原数据 / Cannot record driver recovery data.";
  TransactionLock lock;
  if(!lock.owned) return "另一驱动事务正在处理 / Another driver transaction is active.";
  FgJournal j;
  const auto prior=ReadFgJournal(dir,mirror,j);
  if(prior==durable::ReadResult::Invalid) return "驱动记录损坏，未写入 / Invalid driver journal; nothing written.";
  if(restore && (prior==durable::ReadResult::Missing || !j.pending)) return "没有待还原的帧生成设置 / No FG changes to restore.";
  Session s; auto p=GameProfile(s);
  if(!p) return "无法读取本游戏驱动配置，未写入 / Cannot read the game driver profile; nothing written.";
  NVDRS_PROFILE profile{}; profile.version=NVDRS_PROFILE_VER;
  if(NvAPI_DRS_GetProfileInfo(s.handle,p,&profile)!=NVAPI_OK) return "无法识别驱动配置 / Cannot identify driver profile.";
  if(j.pending && std::memcmp(j.profile,profile.profileName,sizeof(j.profile))) return "驱动配置对象已改变，保留恢复记录 / Driver profile changed; recovery journal retained.";
  std::memcpy(j.profile,profile.profileName,sizeof(j.profile));
  ValueState before[fg_count]{}; uint32_t values[fg_count]{};
  bool change[fg_count]{},any=false;
  FgJournal next=j;
  for(unsigned i=0;i<fg_count;++i) {
    if(restore && !j.valid[i]) continue;
    before[i]=LocalValue(s,p,fg_ids[i],values[i]);
    if(before[i]==ValueState::Error) return "读取驱动配置失败，未提交 / Driver read failed; no commit.";
    const bool exists=before[i]==ValueState::Present;
    if(restore) {
      // Do not overwrite a later change made by any other tool.
      if(!exists || values[i]!=j.applied[i]) { next.valid[i]=0; continue; }
      change[i]=true;
    } else {
      NVDRS_SETTING effective{}; effective.version=NVDRS_SETTING_VER;
      const auto code=NvAPI_DRS_GetSetting(s.handle,p,fg_ids[i],&effective);
      if(code!=NVAPI_OK && code!=NVAPI_SETTING_NOT_FOUND) return "读取有效驱动设置失败，未提交 / Effective driver setting read failed; no commit.";
      if(code==NVAPI_OK && effective.settingType!=NVDRS_DWORD_TYPE) return "驱动设置类型不支持，未提交 / Unsupported driver setting type; no commit.";
      // No inherited/local override to neutralize: do not add redundant keys.
      if(code==NVAPI_SETTING_NOT_FOUND || effective.u32CurrentValue==0) continue;
      if(!j.valid[i] || !exists || values[i]!=j.applied[i]) {
        j.old[i]=values[i]; j.existed[i]=exists?1u:0u;
      }
      j.valid[i]=1; j.applied[i]=0; change[i]=true;
    }
    any=any || change[i];
  }
  if(!restore) {
    j.pending=0; for(auto valid:j.valid) j.pending|=valid;
    if(!any) return "帧生成驱动配置已兼容 / FG driver settings are already compatible.";
    if(!SaveFgJournal(dir,mirror,j)) return "无法保存两份恢复记录，未写入 / Cannot save both recovery journals; nothing written.";
    next=j;
  }
  for(unsigned i=0;i<fg_count;++i) if(change[i]) {
    NvAPI_Status code;
    if(restore && !j.existed[i]) code=NvAPI_DRS_DeleteProfileSetting(s.handle,p,fg_ids[i]);
    else {
      NVDRS_SETTING set{}; set.version=NVDRS_SETTING_VER; set.settingId=fg_ids[i];
      set.settingType=NVDRS_DWORD_TYPE; set.u32CurrentValue=restore?j.old[i]:j.applied[i];
      code=NvAPI_DRS_SetSetting(s.handle,p,&set);
    }
    if(code!=NVAPI_OK) return WriteFailure(code);
  }
  if(any) {
    Session fresh; auto fp=GameProfile(fresh);
    NVDRS_PROFILE identity{}; identity.version=NVDRS_PROFILE_VER;
    if(!fp || NvAPI_DRS_GetProfileInfo(fresh.handle,fp,&identity)!=NVAPI_OK || std::memcmp(identity.profileName,j.profile,sizeof(j.profile)))
      return "提交前配置对象已改变 / Driver profile changed before commit.";
    for(unsigned i=0;i<fg_count;++i) if(change[i]) {
      uint32_t value=0; const auto state=LocalValue(fresh,fp,fg_ids[i],value);
      if(state!=before[i] || (state==ValueState::Present && value!=values[i]))
        return "提交前检测到外部改动，未提交 / External driver change detected; no commit.";
    }
    const auto code=NvAPI_DRS_SaveSettings(s.handle);
    if(code!=NVAPI_OK) return WriteFailure(code);
    Session verify; auto vp=GameProfile(verify);
    NVDRS_PROFILE verified{}; verified.version=NVDRS_PROFILE_VER;
    if(!vp || NvAPI_DRS_GetProfileInfo(verify.handle,vp,&verified)!=NVAPI_OK || std::memcmp(verified.profileName,j.profile,sizeof(j.profile)))
      return "无法核验驱动配置，保留记录 / Cannot verify driver profile; journal retained.";
    for(unsigned i=0;i<fg_count;++i) if(change[i]) {
      uint32_t value=0; const auto state=LocalValue(verify,vp,fg_ids[i],value);
      const bool expect_present=!restore || j.existed[i]!=0;
      if(state==ValueState::Error || (state==ValueState::Present)!=expect_present ||
         (expect_present && value!=(restore?j.old[i]:j.applied[i])))
        return "驱动核验失败，保留恢复记录 / Driver verification failed; recovery journal retained.";
      if(restore) next.valid[i]=0;
    }
  }
  if(restore) {
    next.pending=0; for(auto valid:next.valid) next.pending|=valid;
    if(!SaveFgJournal(dir,mirror,next)) return "驱动已还原，记录同步未完成 / Driver restored; journal synchronization incomplete.";
  }
  return restore ? "驱动改动已处理，保留外部修改；请重启 / Driver changes resolved; external edits preserved. Restart required.":
    "帧生成驱动设置已同步，请重启游戏 / FG driver settings synchronized. Restart the game.";
}
inline Result Synchronize(const std::filesystem::path& dir,bool restore,const std::filesystem::path& game=durable::GameDirectory()) {
  // Resolve v1 ownership before v2 takes over the same IDs. The old record is
  // preserved and retired through its existing compare-and-restore path.
  const auto mirror=durable::RecoveryMirror(game);
  Journal old;
  const auto r=ReadJournal(dir/L"drs-journal.state",mirror/L"drs-journal.state",old);
  if(r==durable::ReadResult::Invalid) return "旧驱动记录损坏，未写入 / Invalid legacy driver journal; nothing written.";
  if(r==durable::ReadResult::Valid && old.pending) {
    const auto result=SynchronizeLegacy(dir,true,game);
    if(result.needs_elevation) return result;
    if(ReadJournal(dir/L"drs-journal.state",mirror/L"drs-journal.state",old)!=durable::ReadResult::Valid || old.pending) return result;
  }
  return SynchronizeFg(dir,restore,game);
}
}
