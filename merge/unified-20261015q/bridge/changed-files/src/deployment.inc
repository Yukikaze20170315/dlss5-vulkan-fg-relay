// Endfield deployment state. Initialization is called by AddonInit, never by
// a DLL notification. GPU work and game files are not changed by this module.
#include "../../common/drs.hpp"
#include "../../common/payload.hpp"
#include "../../common/deployment-state.hpp"
#include <mutex>

namespace deployment {
namespace fs = std::filesystem;
using namespace dlss5;
using State=dlss5::deployment_policy::State;
static State state;
static fs::path game,dir;
static std::mutex mutex;
static std::atomic_bool ready{false},safe{true};
static bool writable=false,sl_valid=false,nr_valid=false,redirected=false,load_failed=false;
static uint32_t architecture=0,current=0,current_early=0,current_nr=0;
static bool any_supported_gpu=false,early_cuda_needed=false;
static std::atomic_bool driver_sync_requested{false};
static void SynchronizeStartupDriver();
static void LoadEventLogMode();
static std::string status,drs_snapshot,sl_reject,interposer_hash;
static std::wstring nr_path;
static std::string nr_label;
static std::atomic_uint64_t last_present{0},first_present{0};
static HANDLE crash_file=INVALID_HANDLE_VALUE;
static bool Save(State next) {
    next.generation=state.generation+1;
    if(!writable || !durable::Save(dir/L"bridge.state",next)) {
        status="状态不可写，本次暂停部署干预 / Cannot save deployment state.";
        safe=true; return false;
    }
    state=next; return true;
}
static uint64_t Fingerprint() {
    std::string s=durable::Sha256(game/L"sl.interposer.dll");
    for(const wchar_t* name : {L"Endfield.exe",L"GameAssembly.dll"}) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(GetFileAttributesExW((game/name).c_str(),GetFileExInfoStandard,&data))
        {
            s.append(reinterpret_cast<const char*>(&data.ftLastWriteTime),sizeof(data.ftLastWriteTime));
            s+=std::to_string(data.nFileSizeHigh)+":"+std::to_string(data.nFileSizeLow);
        }
    }
    NvU32 driver=0; NvAPI_ShortString branch{};
    if(NvAPI_SYS_GetDriverAndBranchVersion(&driver,branch)==NVAPI_OK) s+=std::to_string(driver);
    s+=std::to_string(architecture);
    wchar_t build[64]{}; DWORD bytes=sizeof(build),ubr=0,ubr_bytes=sizeof(ubr);
    RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuildNumber",RRF_RT_REG_SZ,nullptr,build,&bytes);
    RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"UBR",RRF_RT_REG_DWORD,nullptr,&ubr,&ubr_bytes);
    s.append(reinterpret_cast<const char*>(build),wcslen(build)*sizeof(wchar_t)); s+=std::to_string(ubr);
    return durable::Checksum(s.data(),s.size());
}
static bool SupportedArchitecture(uint32_t arch) {
    return arch==0x160 || arch==0x170 || arch==0x190 || arch==0x1b0;
}
static void DetectGpu() {
    if(NvAPI_Initialize()!=NVAPI_OK) return;
    NvPhysicalGpuHandle gpu[NVAPI_MAX_PHYSICAL_GPUS]{}; NvU32 count=0;
    if(NvAPI_EnumPhysicalGPUs(gpu,&count)!=NVAPI_OK) return;
    bool mixed=false;
    for(NvU32 i=0;i<count;++i) {
        NV_GPU_ARCH_INFO info{}; info.version=NV_GPU_ARCH_INFO_VER;
        if(NvAPI_GPU_GetArchInfo(gpu[i],&info)!=NVAPI_OK) { mixed=true; continue; }
        if(i && architecture!=info.architecture) mixed=true;
        architecture=info.architecture;
        any_supported_gpu=any_supported_gpu || SupportedArchitecture(info.architecture);
        early_cuda_needed=early_cuda_needed || info.architecture==0x160 || info.architecture==0x170;
    }
    if(mixed) architecture=0;
}
static uint32_t ArchitectureForLuid(const LUID& wanted) {
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS]{}; NvU32 count=0;
    if(NvAPI_EnumPhysicalGPUs(gpus,&count)!=NVAPI_OK) return 0;
    for(NvU32 i=0;i<count;++i) {
        NvLogicalGpuHandle logical=nullptr;
        if(NvAPI_GetLogicalGPUFromPhysicalGPU(gpus[i],&logical)!=NVAPI_OK) continue;
        LUID actual{}; NV_LOGICAL_GPU_DATA data{}; data.version=NV_LOGICAL_GPU_DATA_VER; data.pOSAdapterId=&actual;
        if(NvAPI_GPU_GetLogicalGpuInfo(logical,&data)!=NVAPI_OK ||
           actual.HighPart!=wanted.HighPart || actual.LowPart!=wanted.LowPart) continue;
        // Linked nodes must share a supported mapping. Never pick GPU zero.
        uint32_t matched=0;
        if(!data.physicalGpuCount || data.physicalGpuCount>NVAPI_MAX_PHYSICAL_GPUS) return 0;
        for(NvU32 n=0;n<data.physicalGpuCount;++n) {
            NV_GPU_ARCH_INFO info{}; info.version=NV_GPU_ARCH_INFO_VER;
            if(NvAPI_GPU_GetArchInfo(data.physicalGpuHandles[n],&info)!=NVAPI_OK || !SupportedArchitecture(info.architecture)) return 0;
            if(matched && matched!=info.architecture) return 0;
            matched=info.architecture;
        }
        return matched;
    }
    return 0;
}
// Verify one table entry's file, once per process. The hash of a 166 MB file
// is not repeated per device or per query: Initialize already verifies this
// machine's selection, and the first device query that names a different
// architecture pays for that file once and then reads a cached answer.
struct RuntimeSlot {
    const payload::NrRuntime* runtime=nullptr;
    bool verified=false;
    bool usable=false;
    std::wstring path;
};
static RuntimeSlot runtime_slots[2];
static std::wstring& VerifiedRuntimePath(const payload::NrRuntime& runtime) {
    static std::wstring empty;
    RuntimeSlot* slot=nullptr;
    for(RuntimeSlot& candidate : runtime_slots) {
        if(candidate.runtime==&runtime) { slot=&candidate; break; }
        if(!candidate.runtime) { candidate.runtime=&runtime; slot=&candidate; break; }
    }
    if(!slot) return empty;
    if(!slot->verified) {
        slot->verified=true;
        slot->usable=durable::Sha256(game/runtime.path)==runtime.hash;
        if(slot->usable) slot->path=(game/runtime.path).wstring();
        Log("[deployment] NR runtime %s for arch table entry: %s",runtime.label,
            slot->usable ? "verified":"hash mismatch or missing");
    }
    return slot->usable ? slot->path:empty;
}
static void Initialize() {
    std::lock_guard<std::mutex> lock(mutex);
    if(ready.load()) return;
    game=durable::GameDirectory(); dir=durable::StateDirectory(game); writable=!dir.empty();
    DetectGpu();
    const auto result=writable ? durable::Read(dir/L"bridge.state",state,state.magic) : durable::ReadResult::Invalid;
    if(result==durable::ReadResult::Invalid || !deployment_policy::Valid(state)) {
        if(writable && fs::exists(dir/L"bridge.state")) {
            std::error_code ec;
            fs::copy_file(dir/L"bridge.state",dir/(L"bridge.state.bad-"+std::to_wstring(GetTickCount64())),fs::copy_options::none,ec);
            if(ec) writable=false;
        }
        state={}; state.safe=1;
        status="状态损坏或目录不可写，本次安全模式 / Invalid state: safe mode.";
    }
    if(result==durable::ReadResult::Missing) {
        // First install. The 2.14.1 stack is required on Turing and Ampere
        // because MFG Unlock's provider path needs DLSS-G 310.9.1 there, and it
        // is the recommended combination on Ada for multi-frame generation.
        // Preselecting it per architecture removes the one setting a fresh
        // 20/30/40-series installation would otherwise have to get right before
        // the game can be played; Blackwell keeps the game's own files by
        // default. An explicit cfg choice or an existing subdirectory still
        // wins, and a selection whose files fail verification is dropped below.
        state.selected=g_sld_dir[0] ? 1u : (architecture==0x190 || architecture==0x170 || architecture==0x160 ? 1u : 0u);
    }
    const uint64_t fingerprint=Fingerprint();
    WIN32_FILE_ATTRIBUTE_DATA mark{};
    const bool crashed=writable && GetFileAttributesExW((dir/L"startup-crash.marker").c_str(),GetFileExInfoStandard,&mark) && mark.nFileSizeLow!=0;
    if(deployment_policy::Begin(state,fingerprint,crashed))
        status="启动未通过健康点，已回退 / Startup did not reach health confirmation; restored.";
    if(durable::Plain(game/L"dlss5/safe-mode.flag") && GetFileAttributesW((game/L"dlss5/safe-mode.flag").c_str())!=INVALID_FILE_ATTRIBUTES) state.safe=1;
    safe=state.safe!=0;
    Save(state);
    if(writable) crash_file=CreateFileW((dir/L"startup-crash.marker").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_FLAG_WRITE_THROUGH,nullptr);
    current=safe.load() ? 0u:state.selected;
    current_early=state.early; current_nr=state.nr;
    if(safe.load()) { g_cfg.stage=0; g_vk_mirror=0; }
    // Two independent conditions, reported separately so a game update and a
    // damaged package never read as the same message.
    interposer_hash=durable::Sha256(game/L"sl.interposer.dll");
    const bool interposer_known=payload::KnownInterposer(interposer_hash.c_str());
    std::string package_bad;
    for(const auto& entry:payload::sl) {
        if(durable::Sha256(game/L"dlss5/sl-2.14.1"/entry.leaf)!=entry.hash) {
            if(package_bad.empty()) for(const wchar_t* c=entry.leaf; *c; ++c) package_bad.push_back(static_cast<char>(*c & 0x7f));
        }
    }
    sl_valid=interposer_known && package_bad.empty();
    if(!interposer_known) {
        sl_reject="游戏的 sl.interposer.dll 不在已验证名单（SHA256 "+interposer_hash.substr(0,12)+
            "…），2.14.1 组合暂不可用；请使用适配新版游戏的插件包 / The game's sl.interposer.dll is not a verified build (SHA256 "+
            interposer_hash.substr(0,12)+"...); the 2.14.1 stack stays unavailable until a package for this game version.";
    } else if(!package_bad.empty()) {
        sl_reject="套件文件 dlss5/sl-2.14.1/"+package_bad+" 缺失或已改变，请重新解压覆盖完整套件 / Package file dlss5/sl-2.14.1/"+
            package_bad+" is missing or changed; extract the complete package again.";
    }
    Log("[deployment] SL check: interposer=%s known=%u package=%s",interposer_hash.empty()?"(unreadable)":interposer_hash.c_str(),
        interposer_known?1u:0u,package_bad.empty()?"verified":package_bad.c_str());
    if(current && !sl_valid) {
        current=0; State next=state; next.selected=next.confirmed; Save(next);
        status=sl_reject.empty() ? "SL 组合或文件校验失败，使用游戏目录 / SL package rejected; game directory retained." : sl_reject;
    }
    g_sld_dir[0]=0; g_sld_complete=false;
    if(current) {
        const auto path=(game/L"dlss5/sl-2.14.1").wstring();
        wcscpy_s(g_sld_dir,path.c_str()); g_sld_paths[0]=g_sld_dir; g_sld_complete=true;
    }
    // Pick this machine's runtime from the payload table and verify that exact
    // file. A runtime the table names for a different architecture is never
    // validated here, so a missing or altered file for another generation
    // cannot disable the one this machine needs.
    {
        const payload::NrRuntime* selected_runtime=payload::SelectionForArchitecture(architecture);
        if(selected_runtime) {
            nr_label=selected_runtime->label;
            nr_valid=any_supported_gpu &&
                durable::Sha256(game/selected_runtime->path)==selected_runtime->hash;
            if(nr_valid) nr_path=(game/selected_runtime->path).wstring();
        }
    }
    if(!nr_valid && !safe.load()) status="NR 运行库或 GPU 未通过校验，本次暂停 NR / NR runtime/GPU validation failed.";
    // 0=auto, 1=on, 2=off. Only auto on Turing/Ampere executes by default.
    if(!safe.load() && (state.early==1 || (state.early==0 && (early_cuda_needed || state.reserved)))) {
        const HMODULE cuda=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Init=int(__stdcall*)(unsigned);
        auto init=cuda ? reinterpret_cast<Init>(GetProcAddress(cuda,"cuInit")):nullptr;
        const int result_cuda=init ? init(0):-1;
        Log("[deployment] early cuInit=%d arch=0x%x",result_cuda,architecture);
    }
    drs_snapshot=drs::Snapshot();
    LoadEventLogMode();
    if(!status.empty() && writable) {
        const std::string note=status+"\n游戏异常时运行 DLSS5-Recovery 故障恢复.cmd。\nRun DLSS5-Recovery 故障恢复.cmd if the game cannot start.\n";
        durable::Write(game/L"DLSS5-上次启动异常-说明.txt",note.data(),static_cast<DWORD>(note.size()));
    }
    ready=true;
    if(!safe.load()) driver_sync_requested=true;
    Log("[deployment] ready arch=0x%x safe=%u selected=%u current=%u sl_valid=%u nr_valid=%u state=%ls",
        architecture,safe.load()?1u:0u,state.selected,current,sl_valid?1u:0u,nr_valid?1u:0u,dir.c_str());
}
static void RecordCrash() {
    if(crash_file!=INVALID_HANDLE_VALUE) { DWORD written=0; const char mark='1'; WriteFile(crash_file,&mark,1,&written,nullptr); FlushFileBuffers(crash_file); }
}
static void DeviceCreateFailed(uint32_t result) {
    if(result!=0xfffffffdu || !ready.load()) return;
    std::lock_guard<std::mutex> lock(mutex);
    State next=state; next.reserved=1; Save(next);
    safe=true;
    InterlockedExchange(&g_vk_mirror,0);
    status="设备初始化 -3；本次暂停 NR，下一次自动提前初始化 CUDA / Device init failed; NR paused, early CUDA enabled next launch.";
    Log("[deployment] vkCreateDevice=-3: no internal layer-chain retry; current-session NR suspended.");
}
static void SlResult(int result,bool used) {
    std::lock_guard<std::mutex> lock(mutex);
    redirected=used && result==0;
    if(current && (!used || result!=0)) {
        load_failed=true; State next=state; next.selected=0; next.confirmed=0; Save(next);
        status="SL 试用失败，已计划下次启动使用游戏目录 / SL failed; game-directory stack on next launch.";
    }
}
// --- anomaly event log ------------------------------------------------------
// Present intervals of 100 ms or more are hitches; no present for 2 s while the
// game window is in the foreground and not minimised is a stall.
static constexpr ULONGLONG kHitchMs=100, kStallMs=2000;
static bool ForegroundIsGame() {
    const HWND window=GetForegroundWindow();
    DWORD pid=0;
    return window!=nullptr && GetWindowThreadProcessId(window,&pid) && pid==GetCurrentProcessId() && !IsIconic(window);
}
// dlss5/state/event-log-mode.txt holds "full=1" while the full anomaly log is
// on. Absent or anything else is the bounded default.
static void LoadEventLogMode() {
    char text[16]{};
    if(writable) {
        HANDLE h=CreateFileW((dir/L"event-log-mode.txt").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(h!=INVALID_HANDLE_VALUE) {
            DWORD got=0;
            ReadFile(h,text,sizeof(text)-1,&got,nullptr);
            CloseHandle(h);
        }
    }
    const bool full=strncmp(text,"full=1",6)==0;
    g_events.SetFull(full);
    Log("[event-log] mode at start: %s",full?"full (every occurrence)":"bounded");
}
static bool StoreEventLogMode(bool full) {
    if(!writable) return false;
    const char* text=full ? "full=1\n":"full=0\n";
    if(!durable::Write(dir/L"event-log-mode.txt",text,7)) return false;
    g_events.SetFull(full);
    Log("[event-log] mode changed from the panel: %s",full?"full (every occurrence)":"bounded");
    return true;
}
static void EventsTick() {
    const ULONGLONG now=GetTickCount64();
    static bool stalled=false;
    static ULONGLONG stall_from=0, rescan_at=0;
    const ULONGLONG last=last_present.load();
    if(last && now-last>=kStallMs) {
        if(!stalled && ForegroundIsGame()) {
            stalled=true; stall_from=last;
            char text[160];
            _snprintf_s(text,_TRUNCATE,"[stall] no frame presented for %llu ms while the game window is in the foreground",
                static_cast<unsigned long long>(now-last));
            g_events.Add(dlss5::eventlog::kSystem,dlss5::eventlog::kWarning,text);
        }
    } else if(stalled && last>stall_from) {
        stalled=false;
        char text[160];
        _snprintf_s(text,_TRUNCATE,"[stall] presenting resumed after %llu ms",static_cast<unsigned long long>(last-stall_from));
        g_events.Add(dlss5::eventlog::kSystem,dlss5::eventlog::kWarning,text);
    }
    g_events.Flush(now);
    if(now-rescan_at>=5000) { rescan_at=now; g_events.Rescan(); }
}
static void Present() {
    const auto now=GetTickCount64();
    const auto previous=last_present.exchange(now);
    if(!previous || now-previous>5000) first_present=now;
    if(previous && now-previous>=kHitchMs) {
        char text[160];
        _snprintf_s(text,_TRUNCATE,"[hitch] present interval %llu ms (foreground=%u)",
            static_cast<unsigned long long>(now-previous),ForegroundIsGame()?1u:0u);
        g_events.Add(dlss5::eventlog::kSystem,dlss5::eventlog::kWarning,text);
    }
}
static void Tick() {
    if(driver_sync_requested.exchange(false)) SynchronizeStartupDriver();
    if(!ready.load() || safe.load()) return;
    std::lock_guard<std::mutex> lock(mutex);
    if(state.healthy || load_failed) return;
    const auto now=GetTickCount64(), first=first_present.load(), last=last_present.load();
    if(!first || now-last>5000 || now-first<60000) return;
    if(current) {
        if(!redirected) return;
        // Loaded plugins may be optional. Every loaded member must come from
        // the selected directory; common, DLSS and Reflex must be present.
        for(const auto& entry:payload::sl) {
            if(wcsncmp(entry.leaf,L"sl.",3)!=0) continue;
            HMODULE mod=GetModuleHandleW(entry.leaf);
            const bool required=wcscmp(entry.leaf,L"sl.common.dll")==0 || wcscmp(entry.leaf,L"sl.dlss.dll")==0 || wcscmp(entry.leaf,L"sl.reflex.dll")==0;
            if(!mod) { if(required) return; else continue; }
            wchar_t path[32768]{}; GetModuleFileNameW(mod,path,32768);
            if(!durable::SamePath(path,game/L"dlss5/sl-2.14.1"/entry.leaf)) {
                load_failed=true; State next=state; next.selected=0; Save(next);
                status="SL 路径混合，下次启动回退 / Mixed SL paths; restore next launch."; return;
            }
        }
        // Successful game DLSS evaluation is required: directory load alone
        // cannot establish that the game's super-resolution feature exists.
        if(InterlockedCompareExchange(&g_deployment_sr_success,0,0)==0) return;
    }
    State next=state; deployment_policy::Healthy(next,current);
    next.confirmed_early=current_early; next.confirmed_nr=current_nr;
    if(Save(next)) {
        status="本次启动已通过健康点 / Startup health confirmed.";
        Log("[deployment] startup health confirmed: sl=%u successful_game_sr=%ld",current,
            InterlockedCompareExchange(&g_deployment_sr_success,0,0));
    }
}
static DWORD WINAPI MaintenanceWorker(LPVOID owner) {
    while(!g_shutting_down) { Tick(); EventsTick(); Sleep(250); }
    g_events.Flush(GetTickCount64());
    FreeLibraryAndExitThread(static_cast<HMODULE>(owner),0);
}
static void StartMaintenance() {
    static std::atomic_bool started{false};
    if(started.exchange(true)) return;
    if(HANDLE thread=StartModuleWorker(MaintenanceWorker)) CloseHandle(thread);
}
} // namespace deployment
static bool DeploymentSuspended() { return deployment::ready.load() && deployment::safe.load(); }

extern "C" __declspec(dllexport) uint32_t __cdecl DLSS5DeploymentSafeMode() {
    return !deployment::ready.load() || deployment::safe.load() ? 1u:0u;
}

// Generic and MFG Unlock forward their log messages here (common/event-log.hpp).
// Warnings and errors are anomalies; info and debug lines are classified.
extern "C" __declspec(dllexport) void __cdecl DLSS5LogEventV1(uint32_t source,uint32_t level,const char* text) {
    if(text==nullptr || *text==0) return;
    const DWORD last_error=GetLastError();
    if(level<=dlss5::eventlog::kWarning || dlss5::eventlog::LooksAnomalous(text))
        g_events.Add(source,level==0 ? static_cast<uint32_t>(dlss5::eventlog::kError):level,text);
    SetLastError(last_error);
}
extern "C" __declspec(dllexport) uint32_t __cdecl DLSS5LogFullModeV1() {
    return g_events.Full() ? 1u:0u;
}
extern "C" __declspec(dllexport) int __cdecl DLSS5GetNrRuntimePathV1(wchar_t* out,uint32_t count) {
    if(!out || !count || !deployment::ready.load() || deployment::safe.load()) return -1;
    // No device: use the machine-wide architecture. A machine whose physical
    // GPUs disagree has architecture 0 and gets no runtime, rather than the
    // runtime of whichever card happened to be enumerated first.
    const dlss5::payload::NrRuntime* runtime=dlss5::payload::SelectionForArchitecture(deployment::architecture);
    const std::wstring path=runtime ? deployment::VerifiedRuntimePath(*runtime):std::wstring();
    if(path.size()+1>count || path.empty()) return -1;
    wcscpy_s(out,count,path.c_str()); return 1;
}

extern "C" __declspec(dllexport) int __cdecl DLSS5GetNrRuntimePathV2(uint32_t low,int32_t high,wchar_t* out,uint32_t count) {
    if(!out || !count || !deployment::ready.load() || deployment::safe.load()) return -1;
    const LUID luid{low,high};
    const uint32_t arch=deployment::ArchitectureForLuid(luid);
    if(!deployment::SupportedArchitecture(arch)) return -1;
    const dlss5::payload::NrRuntime* runtime=dlss5::payload::SelectionForArchitecture(arch);
    const std::wstring path=runtime ? deployment::VerifiedRuntimePath(*runtime):std::wstring();
    if(path.empty() || path.size()+1>count) return -1;
    wcscpy_s(out,count,path.c_str());
    Log("[deployment] NR selected for device LUID=%08x:%08x arch=0x%x runtime=%s path=%ls",
        static_cast<uint32_t>(high),low,arch,runtime->label,out);
    return 1;
}

// The panel shows which runtime this launch resolved, so a report can name it
// without reading the log.
extern "C" __declspec(dllexport) const char* __cdecl DLSS5GetNrRuntimeLabel() {
    return deployment::nr_label.empty() ? "" : deployment::nr_label.c_str();
}
