#pragma once
#include "durable.hpp"
#pragma warning(push, 0)
#include "../../../upstream/NVIDIA-nvapi-87dca62/nvapi.h"
#include "../../../upstream/NVIDIA-nvapi-87dca62/NvApiDriverSettings.h"
#pragma warning(pop)
#include <sstream>

namespace dlss5::drs {
struct Session {
  NvDRSSessionHandle handle = nullptr;
  Session() { if (NvAPI_Initialize() == NVAPI_OK && NvAPI_DRS_CreateSession(&handle) == NVAPI_OK &&
                  NvAPI_DRS_LoadSettings(handle) != NVAPI_OK) { NvAPI_DRS_DestroySession(handle); handle = nullptr; } }
  ~Session() { if (handle) NvAPI_DRS_DestroySession(handle); }
};
inline NvDRSProfileHandle GameProfile(Session& s) {
  NvAPI_UnicodeString name{};
  const wchar_t* exe = L"Endfield.exe";
  for (unsigned i=0; exe[i]; ++i) name[i] = static_cast<NvU16>(exe[i]);
  NVDRS_APPLICATION app{}; app.version = NVDRS_APPLICATION_VER;
  NvDRSProfileHandle p = nullptr;
  return s.handle && NvAPI_DRS_FindApplicationByName(s.handle, name, &p, &app) == NVAPI_OK ? p : nullptr;
}
enum class ValueState { Absent, Present, Error };
inline ValueState LocalValue(Session& s, NvDRSProfileHandle p, uint32_t id, uint32_t& value) {
  NVDRS_SETTING setting{}; setting.version = NVDRS_SETTING_VER;
  if (!s.handle || !p) return ValueState::Error;
  const auto code=NvAPI_DRS_GetSetting(s.handle,p,id,&setting);
  if (code==NVAPI_SETTING_NOT_FOUND) return ValueState::Absent;
  if (code!=NVAPI_OK || setting.settingType!=NVDRS_DWORD_TYPE) return ValueState::Error;
  if (setting.settingLocation!=NVDRS_CURRENT_PROFILE_LOCATION || setting.isCurrentPredefined)
    return ValueState::Absent;
  value=setting.u32CurrentValue; return ValueState::Present;
}
inline std::string Snapshot() {
  Session s; if (!s.handle) return "DRS unavailable / 无法读取驱动配置";
  auto game = GameProfile(s);
  NvDRSProfileHandle global = nullptr;
  NvAPI_DRS_GetBaseProfile(s.handle,&global);
  const uint32_t ids[] = {NGX_DLSSG_MODE_ID, NGX_DLSSG_MULTI_FRAME_COUNT_ID,
    NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE_ID, NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX_ID,
    NGX_DLSS_FG_OVERRIDE_ID, NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID,
    NGX_DLSS_FG_OVERRIDE_RESERVED_KEY1_ID, NGX_DLSS_FG_OVERRIDE_RESERVED_KEY2_ID,
    NGX_DLSS_SR_MODE_ID, NGX_DLSS_SR_OVERRIDE_ID, NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID,
    NGX_DLSS_RR_MODE_ID, NGX_DLSS_RR_OVERRIDE_ID, NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID,
    FRL_FPS_ID, VSYNCMODE_ID, OGL_CPL_PREFER_DXPRESENT_ID, VRR_MODE_ID, VRR_APP_OVERRIDE_ID,
    PRERENDERLIMIT_ID};
  std::ostringstream out;
  for (auto p : {game,global}) {
    out << (p==game ? "Endfield profile / 游戏配置\n" : "Global profile (read only) / 全局只读\n");
    if (!p) { out << "not found\n"; continue; }
    for (auto id : ids) {
      NVDRS_SETTING setting{}; setting.version=NVDRS_SETTING_VER;
      const auto result=NvAPI_DRS_GetSetting(s.handle,p,id,&setting);
      out << "0x" << std::hex << id << ": ";
      if(result==NVAPI_OK && setting.settingType==NVDRS_DWORD_TYPE)
        out << "0x" << setting.u32CurrentValue << " location=" << std::dec << setting.settingLocation
            << " predefined=" << setting.isCurrentPredefined;
      else out << "status=" << std::dec << result;
      out << '\n';
    }
  }
  return out.str();
}
struct Journal {
  uint32_t magic=0x35535244,version=1;
  uint64_t checksum=0,generation=0;
  uint32_t pending=0,valid[2]{},old[2]{};
  NvAPI_UnicodeString profile{};
};
inline constexpr uint32_t ids[2]={NGX_DLSSG_MODE_ID,NGX_DLSSG_MULTI_FRAME_COUNT_ID};
struct Result {
  std::string message;
  bool needs_elevation=false;
  Result(const char* text, bool elevate=false) : message(text), needs_elevation(elevate) {}
};
// Serialize this plugin's transactions, including a separately elevated helper.
// Other tools do not participate; fresh-session comparisons below narrow that race.
struct TransactionLock {
  HANDLE handle=CreateMutexW(nullptr,FALSE,L"Global\\DLSS5-Endfield-DRS-v1");
  bool owned=false;
  TransactionLock() {
    if(handle) { const auto code=WaitForSingleObject(handle,0); owned=code==WAIT_OBJECT_0 || code==WAIT_ABANDONED; }
  }
  ~TransactionLock() { if(owned) ReleaseMutex(handle); if(handle) CloseHandle(handle); }
};
inline bool ValidJournal(const Journal& j) {
  if(j.pending>1 || j.valid[0]>1 || j.valid[1]>1) return false;
  bool terminated=false;
  for(auto c:j.profile) if(!c) { terminated=true; break; }
  return terminated && j.profile[0] && (!j.pending || j.valid[0] || j.valid[1]);
}
inline bool SameJournal(Journal a, Journal b) {
  a.checksum=b.checksum=0;
  return std::memcmp(&a,&b,sizeof(a))==0;
}
inline durable::ReadResult ReadJournal(const std::filesystem::path& file,
                                      const std::filesystem::path& mirror, Journal& out) {
  Journal a,b;
  auto ar=durable::Read(file,a,a.magic),br=durable::Read(mirror,b,b.magic);
  if(ar==durable::ReadResult::Valid && !ValidJournal(a)) ar=durable::ReadResult::Invalid;
  if(br==durable::ReadResult::Valid && !ValidJournal(b)) br=durable::ReadResult::Invalid;
  // Keep damaged records intact. A missing copy can be recovered from its peer;
  // corrupt or contradictory records require explicit inspection.
  if(ar==durable::ReadResult::Invalid || br==durable::ReadResult::Invalid) return durable::ReadResult::Invalid;
  if(ar==durable::ReadResult::Missing && br==durable::ReadResult::Missing) return ar;
  if(ar==durable::ReadResult::Valid && br==durable::ReadResult::Valid) {
    if(a.generation==b.generation && !SameJournal(a,b)) return durable::ReadResult::Invalid;
    out=a.generation>=b.generation ? a:b;
  } else out=ar==durable::ReadResult::Valid ? a:b;
  return durable::ReadResult::Valid;
}
inline bool SaveJournal(const std::filesystem::path& file,const std::filesystem::path& mirror,Journal& j) {
  ++j.generation;
  // A monotonic generation lets recovery choose the newer copy after interruption.
  return durable::Save(mirror,j) && durable::Save(file,j);
}
inline Result WriteFailure(NvAPI_Status code) {
  return {"驱动写入失败，还原记录保留 / DRS write failed; journal retained.",code==NVAPI_INVALID_USER_PRIVILEGE};
}
inline Result SynchronizeLegacy(const std::filesystem::path& dir, bool restore,
                              const std::filesystem::path& game=durable::GameDirectory()) {
  if (dir.empty()) return "State unavailable / 无法记录变更";
  TransactionLock transaction;
  if(!transaction.owned) return "另一驱动事务正在处理或锁不可用 / Another DRS transaction is active or inaccessible.";
  Session s; auto p=GameProfile(s);
  if (!p) return "No Endfield profile; nothing written / 未找到游戏配置，未写入";
  NVDRS_PROFILE profile{}; profile.version=NVDRS_PROFILE_VER;
  if(NvAPI_DRS_GetProfileInfo(s.handle,p,&profile)!=NVAPI_OK) return "Cannot identify profile";
  Journal j;
  const auto file=dir/L"drs-journal.state";
  const auto mirror=durable::RecoveryMirror(game);
  if(mirror.empty()) return "Cannot locate DRS recovery mirror / 无法定位驱动还原副本";
  const auto mirror_file=mirror/L"drs-journal.state";
  const auto result=ReadJournal(file,mirror_file,j);
  if (result==durable::ReadResult::Invalid) return "Invalid DRS journal; retained for recovery / 驱动记录损坏，未写入";
  if (restore && (result!=durable::ReadResult::Valid || !j.pending)) return "No DRS changes to restore / 没有待还原设置";
  if (j.pending && std::memcmp(j.profile,profile.profileName,sizeof(j.profile))) return "Profile changed; journal retained / 配置对象已改变，保留记录";
  if (!restore && !j.pending) {
    const auto generation=j.generation;
    j={}; j.generation=generation; j.pending=1;
    std::memcpy(j.profile,profile.profileName,sizeof(j.profile));
    for (unsigned i=0;i<2;++i) {
      const auto value=LocalValue(s,p,ids[i],j.old[i]);
      if(value==ValueState::Error) return "Cannot read driver settings; nothing written / 无法读取驱动配置，未写入";
      j.valid[i]=value==ValueState::Present ? 1u:0u;
    }
    if(!j.valid[0] && !j.valid[1]) return "No local multiplier overrides / 没有本游戏的固定倍率覆盖";
    if(!SaveJournal(file,mirror_file,j))
      return "Cannot save both DRS rollback journals / 无法保存两份驱动还原记录，未提交";
  } else if(!durable::Save(mirror_file,j) || !durable::Save(file,j)) {
    return "Cannot reconcile both DRS journals; no commit / 无法同步两份驱动记录，未提交";
  }
  bool changed=false,conflict=false;
  ValueState observed[2]{}; uint32_t observed_value[2]{};
  bool touched[2]{};
  Journal next=j;
  for (unsigned i=0;i<2;++i) {
    if(!j.valid[i]) continue;
    observed[i]=LocalValue(s,p,ids[i],observed_value[i]);
    if(observed[i]==ValueState::Error) return "DRS read failed; no commit / 驱动读取失败，未提交";
    const bool exists=observed[i]==ValueState::Present;
    NvAPI_Status code=NVAPI_OK;
    if(restore) {
      // Retire each resolved entry, including external conflicts. A later retry
      // must not treat subsequent user changes as part of this transaction.
      if(exists) { if(observed_value[i]!=j.old[i]) conflict=true; next.valid[i]=0; continue; }
      NVDRS_SETTING setting{}; setting.version=NVDRS_SETTING_VER;
      setting.settingId=ids[i]; setting.settingType=NVDRS_DWORD_TYPE; setting.u32CurrentValue=j.old[i];
      code=NvAPI_DRS_SetSetting(s.handle,p,&setting);
    } else {
      // A permission retry or an interrupted commit reuses the original journal.
      if(!exists) continue;
      if(observed_value[i]!=j.old[i]) return "Concurrent DRS change; no commit / 检测到并发改动，未提交";
      code=NvAPI_DRS_DeleteProfileSetting(s.handle,p,ids[i]);
    }
    if(code!=NVAPI_OK) return WriteFailure(code);
    changed=true; touched[i]=true;
  }
  if(changed) {
    Session fresh; auto fp=GameProfile(fresh);
    NVDRS_PROFILE actual{}; actual.version=NVDRS_PROFILE_VER;
    if(!fp || NvAPI_DRS_GetProfileInfo(fresh.handle,fp,&actual)!=NVAPI_OK ||
       std::memcmp(actual.profileName,j.profile,sizeof(j.profile))) return "Profile changed before commit / 提交前配置对象改变，未提交";
    for(unsigned i=0;i<2;++i) if(j.valid[i]) {
      uint32_t value=0; const auto read=LocalValue(fresh,fp,ids[i],value);
      if(read==ValueState::Error || read!=observed[i] || (read==ValueState::Present && value!=observed_value[i]))
        return "Driver changed before commit; journal retained / 提交前驱动配置已改变，未提交";
    }
    const auto code=NvAPI_DRS_SaveSettings(s.handle);
    if(code!=NVAPI_OK) return WriteFailure(code);
  }
  // Verify through a new session rather than trusting the in-memory transaction.
  Session verify; auto vp=GameProfile(verify);
  if(!vp) return "Cannot verify DRS transaction; journal retained";
  NVDRS_PROFILE verified{}; verified.version=NVDRS_PROFILE_VER;
  if(NvAPI_DRS_GetProfileInfo(verify.handle,vp,&verified)!=NVAPI_OK ||
     std::memcmp(verified.profileName,j.profile,sizeof(j.profile))) return "Profile changed during verification; journal retained / 核验时配置对象改变，保留记录";
  for(unsigned i=0;i<2;++i) if(j.valid[i]) {
    uint32_t value=0; const auto read=LocalValue(verify,vp,ids[i],value);
    if(read==ValueState::Error) return "DRS verification read failed; journal retained / 驱动核验读取失败，保留记录";
    const bool exists=read==ValueState::Present;
    if(!restore && exists) return "DRS removal did not verify; journal retained";
    if(restore && touched[i]) {
      if(!exists || value!=j.old[i]) return "DRS restoration did not verify; journal retained / 驱动还原未通过核验，保留记录";
      next.valid[i]=0;
    }
  }
  if(restore) {
    next.pending=next.valid[0] || next.valid[1] ? 1u:0u;
    if(!SaveJournal(file,mirror_file,next)) return "Restored; journal save incomplete / 还原完成，记录同步未完成";
  }
  return conflict ? "External values preserved; resolved entries retired / 已保留外部改动，完成的项目不再重复还原" :
    restore ? "DRS restored. Restart the game / 驱动设置已还原，请重启游戏" :
              "Local fixed multiplier overrides removed. Restart the game / 本游戏固定倍率覆盖已移除，请重启游戏";
}
} // namespace dlss5::drs

#include "drs-fg.hpp"
