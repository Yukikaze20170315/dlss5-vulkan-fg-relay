#include "../../common/panel-locale.hpp"
#include "../../common/maintenance-command.hpp"
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
static HANDLE g_driver_maintenance=nullptr;
static std::recursive_mutex g_driver_maintenance_mutex;
static dlss5_panel::Preference g_panel_language;
static bool g_panel_advanced=false;
static bool DeploymentPanelHeader() {
    if(deployment::ready.load()) g_panel_language.Load(deployment::dir);
    const auto* ui=g_panel_gui;
    int selected=g_panel_language.chinese ? 1:0;
    bool changed=ui->RadioButton2("English##bridge_en",&selected,0);
    ui->SameLine(0.f,-1.f);
    changed=ui->RadioButton2("简体中文##bridge_zh",&selected,1) || changed;
    if(changed) {
        if(deployment::ready.load()) g_panel_language.Store(deployment::dir,selected==1);
        else g_panel_language.chinese=selected==1;
    }
    ui->Checkbox(g_panel_language.chinese ? "高级设置###bridge_advanced":"Advanced settings###bridge_advanced",&g_panel_advanced);
    return g_panel_advanced;
}
static std::string DeploymentText(const std::string& value) {
    return dlss5_panel::SelectText(value,g_panel_language.chinese);
}

static std::string DeploymentDriverResult(const std::filesystem::path& directory) {
    const auto file=directory/L"drs-result.txt";
    if(!dlss5::durable::Plain(file)) return "驱动维护结果未确认 / DRS result not confirmed.";
    HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE) return "无法读取驱动维护结果 / Cannot read DRS result.";
    char data[4096]{}; DWORD got=0;
    const bool okay=ReadFile(h,data,sizeof(data)-1,&got,nullptr)!=FALSE;
    CloseHandle(h);
    return okay ? std::string(data,got):"无法读取驱动维护结果 / Cannot read DRS result.";
}
static void DeploymentRequestDriver(dlss5::maintenance::DriverAction action) {
    using namespace deployment;
    std::lock_guard<std::recursive_mutex> maintenance_lock(g_driver_maintenance_mutex);
    if(g_driver_maintenance) { status="驱动维护进行中，请完成授权后再重启 / Driver maintenance is running; finish authorization before restarting."; return; }
    const char pending[]="驱动维护结果尚未确认；可重试，游戏启动不受此状态限制 / DRS result not confirmed; retry is available and game startup is not blocked.";
    if(!dlss5::durable::Write(dir/L"drs-result.txt",pending,sizeof(pending)-1)) {
        status="无法保存驱动维护状态，未执行 / Cannot record driver maintenance request."; return;
    }
    wchar_t module[32768]{}; GetModuleFileNameW(g_self,module,32768);
    g_driver_maintenance=dlss5::maintenance::Start(module,action);
    status=g_driver_maintenance ? "正在处理驱动配置，完成后重启 / Processing driver settings; restart after completion.":
        "无法启动驱动维护，可重试；SL 选择仍保留 / Cannot start driver maintenance; retry available and SL selection retained.";
}
namespace deployment {
static void SynchronizeStartupDriver() {
    if(safe.load()) return;
    const auto result=dlss5::drs::Synchronize(dir,false,game);
    const auto snapshot=dlss5::drs::Snapshot();
    std::lock_guard<std::mutex> lock(mutex);
    status=result.message;
    dlss5::durable::Write(dir/L"drs-result.txt",status.data(),static_cast<DWORD>(status.size()));
    Log("[deployment] startup DRS: %s",status.c_str());
    if(result.needs_elevation) DeploymentRequestDriver(dlss5::maintenance::DriverAction::Synchronize);
    drs_snapshot=snapshot;
}
}
static void DeploymentPresent(reshade::api::command_queue*,reshade::api::swapchain*,
    const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*) {
    deployment::Present();
}
static void DeploymentRegisterEvents() {
    if(!g_reshade_dll) return;
    const auto reg=reinterpret_cast<PFN_ReShadeRegisterEvent>(GetProcAddress(g_reshade_dll,"ReShadeRegisterEvent"));
    if(reg) reg(reshade::addon_event::present,reinterpret_cast<void*>(&DeploymentPresent));
}
// The diagnostics bundle is written into the log folder, beside the logs it
// copies, so everything a report needs is under dlss5\dlss5_log. The state
// folder is the fallback when the log folder cannot be created.
static std::filesystem::path DeploymentLogFolder(const std::filesystem::path& game,const std::filesystem::path& state) {
    wchar_t folder[MAX_PATH]{};
    return dlss5::eventlog::EnsureLogDirectory(game.c_str(),folder,MAX_PATH) ? std::filesystem::path(folder):state;
}
static void DeploymentExportDiagnostics(const std::filesystem::path& game,const std::filesystem::path& state) {
    namespace fs=std::filesystem;
    const auto logs=DeploymentLogFolder(game,state);
    const auto dest=logs/(L"diagnostics-"+std::to_wstring(GetTickCount64()));
    if(!dlss5::durable::Plain(dest)) return;
    std::error_code ec; fs::create_directories(dest,ec); if(ec) return;
    for(const wchar_t* leaf : {L"dlss5-bridge.log",L"dlss5-bridge.prev.log",L"dlss5-bridge.overflow.log",L"dlss5-observe.log",
                               L"dlss5-observe.prev.log",L"dlss5-sl.log",L"RenoDX-DLSS5-crash.log"}) {
        if(dlss5::durable::Plain(logs/leaf)) fs::copy_file(logs/leaf,dest/leaf,fs::copy_options::none,ec);
        ec.clear();
    }
    // The newest event log of each mode; older ones stay where they are.
    for(const wchar_t* pattern : {L"dlss5-events-2*.log",L"dlss5-events-full-*.log"}) {
        WIN32_FIND_DATAW data{}, newest{};
        HANDLE find=FindFirstFileW((logs/pattern).c_str(),&data);
        if(find==INVALID_HANDLE_VALUE) continue;
        do {
            if(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if(newest.cFileName[0]==0 || CompareFileTime(&data.ftLastWriteTime,&newest.ftLastWriteTime)>0) newest=data;
        } while(FindNextFileW(find,&data));
        FindClose(find);
        if(newest.cFileName[0]!=0) fs::copy_file(logs/newest.cFileName,dest/newest.cFileName,fs::copy_options::none,ec);
        ec.clear();
    }
    for(const wchar_t* leaf : {L"ReShade.log",L"dlss5-bridge.cfg",L"vk-present-adapter.ini",L"ReShade.ini"}) {
        if(dlss5::durable::Plain(game/leaf)) fs::copy_file(game/leaf,dest/leaf,fs::copy_options::none,ec);
        ec.clear();
    }
    for(const wchar_t* leaf : {L"bridge.state",L"generic-layers.state",L"drs-journal.state",L"drs-fg.state",L"drs-result.txt",L"startup-crash.marker"}) {
        if(dlss5::durable::Plain(state/leaf)) fs::copy_file(state/leaf,dest/leaf,fs::copy_options::none,ec);
        ec.clear();
    }
    const std::string snapshot=dlss5::drs::Snapshot();
    dlss5::durable::Write(dest/L"driver-settings.txt",snapshot.data(),static_cast<DWORD>(snapshot.size()));
    HMODULE modules[1024]{}; const DWORD count=ObsEnumModules(modules,1024);
    std::wstring inventory;
    for(DWORD i=0;i<count;++i) {
        wchar_t path[32768]{}; GetModuleFileNameW(modules[i],path,32768);
        inventory+=path; inventory+=L"\r\n";
    }
    dlss5::durable::Write(dest/L"loaded-modules-utf16.txt",inventory.data(),static_cast<DWORD>(inventory.size()*sizeof(wchar_t)));
    deployment::status="诊断已保存到 dlss5/dlss5_log（不可写时使用状态目录） / Diagnostics saved in dlss5/dlss5_log (state directory if that is not writable).";
}
static void DeploymentDrawHookMode() {
    const auto* ui = g_panel_gui;
    const auto text = [ui](const std::string& value) { ui->TextUnformatted(DeploymentText(value).c_str(), nullptr); };
    ui->SeparatorText(DeploymentText("NR 钩子模式（EnableHooks） / NR hook mode (EnableHooks)").c_str());
    dlss5::hook_mode::Snapshot mode;
    if (!GenericHookModeCall(dlss5::hook_mode::Query, 0, &mode)) {
        text("当前 Generic 不支持面板配置或尚未初始化，请使用同包版本 / Generic is not ready or does not support this control; use the matching package.");
        return;
    }
    int selected = dlss5::hook_mode::Valid(mode.selected) ? static_cast<int>(mode.selected) : -1;
    bool changed = ui->RadioButton2(DeploymentText("0：停用 NR 钩子 / 0: Disable NR hooks##hook-mode0").c_str(), &selected, 0);
    changed = ui->RadioButton2(DeploymentText("1：NGX + Streamline（本套件呈现 NR） / 1: NGX + Streamline (suite Present NR)##hook-mode1").c_str(), &selected, 1) || changed;
    changed = ui->RadioButton2(DeploymentText("2：仅 NGX（Generic 默认） / 2: NGX only (Generic default)##hook-mode2").c_str(), &selected, 2) || changed;
    static bool save_failed = false;
    if (changed && selected >= 0 && (static_cast<uint32_t>(selected) != mode.selected || !mode.stored)) {
        save_failed = !GenericHookModeCall(dlss5::hook_mode::Select, static_cast<uint32_t>(selected), &mode);
    }
    char line[256]{};
    if (dlss5::hook_mode::Valid(mode.selected)) {
        sprintf_s(line, g_panel_language.chinese ? "本次生效：%u；下次启动：%u" : "Active this session: %u; next launch: %u", mode.current, mode.selected);
        text(line);
        if (!mode.stored) text("尚未保存 EnableHooks，使用 Generic 默认值 / EnableHooks is not stored; using Generic's default.");
        if (mode.current != mode.selected) text("选择已提交 ReShade 配置；请正常退出并重启游戏生效 / Choice submitted to ReShade configuration; exit normally and restart the game to apply.");
    } else {
        text("已保存的值无法识别，请选择 0、1 或 2 / The stored value is not recognized; select 0, 1 or 2.");
    }
    if (save_failed) text("未能确认配置写入，请重试；本次运行模式未改变 / Could not confirm the configuration change; retry. The active mode is unchanged.");
    text("本候选的呈现 NR 和失焦帧生成保持使用模式 1；模式 0 停用，模式 2 保留用于兼容选择 / This candidate uses mode 1 for Present NR and background FG retention; mode 0 disables hooks and mode 2 remains a compatibility option.");
}

// Bottom of the advanced settings. Turning the full anomaly log on takes two
// steps: the checkbox opens a modal with the disk-use warning, and only its
// confirm button, after the acknowledgement box is ticked, switches the mode.
// Turning it off is immediate.
static void DeploymentDrawEventLog() {
    using namespace deployment;
    const auto* ui=g_panel_gui;
    if(ui==nullptr || !ready.load()) return;
    const bool zh=g_panel_language.chinese;
    const auto text=[ui](const std::string& value){ui->TextUnformatted(DeploymentText(value).c_str(),nullptr);};
    static bool acknowledged=false;
    static std::string result;

    ui->SeparatorText(DeploymentText("异常日志 / Anomaly log").c_str());
    const EventSink::Stats s=g_events.Snapshot();
    char line[512]{};
    sprintf_s(line,zh ? "模式：%s；本局已写入 %.1f MB，合并的重复事件 %llu 条；dlss5_log 中事件日志合计 %.1f MB" :
                        "Mode: %s; this session wrote %.1f MB and merged %llu repeated events; event logs in dlss5_log total %.1f MB",
        s.full ? (zh ? "完整（每次发生都记录）":"full (every occurrence)") : (zh ? "有界（默认）":"bounded (default)"),
        static_cast<double>(s.written_session)/1048576.0,static_cast<unsigned long long>(s.suppressed_session),
        static_cast<double>(s.folder_bytes)/1048576.0);
    ui->TextUnformatted(line,nullptr);
    text("位置：游戏目录\\dlss5\\dlss5_log\\dlss5-events-*.log / Location: game folder\\dlss5\\dlss5_log\\dlss5-events-*.log");
    if(s.paused) text("日志所在磁盘剩余空间不足，已暂停写入（仅计数） / Low disk space on the log volume: writing paused, events are counted");
    if(!s.full) text("有界模式：每种异常首次出现完整记录，重复的按 10 秒合并计数；每局最多 16 MB，文件夹内保留最多 64 MB / Bounded mode: the first occurrence of each anomaly in full, repeats merged per 10 s; at most 16 MB per session and 64 MB kept in the folder");

    bool box=s.full;
    if(ui->Checkbox(DeploymentText("完整异常日志（每次发生都完整记录一行） / Full anomaly log (one full line per occurrence)###event-log-full").c_str(),&box)) {
        if(box && !s.full) { acknowledged=false; result.clear(); ui->OpenPopup("###dlss5-event-log-confirm",0); }
        else if(!box && s.full) {
            std::lock_guard<std::mutex> lock(mutex);
            result=StoreEventLogMode(false) ? "已关闭完整异常日志，恢复有界模式；已写入的完整日志文件保留 / Full anomaly log off, bounded mode restored; written full logs are kept" :
                                              "无法保存设置，模式未改变 / Could not save the setting; mode unchanged";
        }
    }
    if(ui->BeginPopupModal(DeploymentText("开启完整异常日志？ / Turn on the full anomaly log?###dlss5-event-log-confirm").c_str(),nullptr,64 /* ImGuiWindowFlags_AlwaysAutoResize */)) {
        ui->PushTextWrapPos(600.0f);
        text("警告：完整模式会把每一次异常都写成一行，不做合并。 / Warning: full mode writes every anomaly as its own line, without merging.");
        text("正常游玩时约为每小时数 MB；如果某个异常每帧都发生（例如帧生成倍率接管失败、等待超时），按每秒约 140 帧、每行约 250 字节估算约 3 GB/天，多处同时每帧发生时可达约 30 GB/天。 / Normal play writes a few MB per hour. An anomaly that repeats every frame (for example a failed multiplier takeover or a wait timeout) is about 3 GB per day at 140 frames per second and 250 bytes per line, and about 30 GB per day when several repeat at once.");
        text("文件按 64 MB 分段保存为 dlss5\\dlss5_log\\dlss5-events-full-*.log，不会被自动删除；关闭本开关后也会保留，需要手动删除。 / Files are written as dlss5\\dlss5_log\\dlss5-events-full-*.log in 64 MB parts and are never deleted automatically, also after this switch is turned off; delete them by hand.");
        text("日志所在磁盘剩余空间低于 4 GB 时自动暂停写入（只计数），回到 6 GB 以上后继续。 / Writing pauses below 4 GB free on the log volume (events are only counted) and resumes above 6 GB.");
        text("大量写入可能影响帧时间的稳定性。建议只在复现问题时开启，复现后关闭。设置立即生效，并在下次启动时保持。 / Heavy writing may affect frame-time stability. Turn it on only while reproducing a problem and off afterwards. The setting applies immediately and persists across launches.");
        ui->Checkbox(DeploymentText("我已了解上述容量占用风险 / I understand the disk-use risk above###event-log-ack").c_str(),&acknowledged);
        if(ui->Button(DeploymentText(acknowledged ? "确认开启 / Turn on###event-log-yes" : "确认开启（请先勾选上方确认框） / Turn on (tick the box above first)###event-log-yes").c_str(),PanelVec2{0.0f,0.0f}) && acknowledged) {
            std::lock_guard<std::mutex> lock(mutex);
            result=StoreEventLogMode(true) ? "已开启完整异常日志 / Full anomaly log on" :
                                             "无法保存设置（状态目录不可写），模式未改变 / Could not save the setting (state folder not writable); mode unchanged";
            ui->CloseCurrentPopup();
        }
        ui->SameLine(0.0f,-1.0f);
        if(ui->Button(DeploymentText("取消 / Cancel###event-log-no").c_str(),PanelVec2{0.0f,0.0f})) ui->CloseCurrentPopup();
        ui->PopTextWrapPos();
        ui->EndPopup();
    }
    if(!result.empty()) text(result);
}

static void DeploymentDraw() {
    using namespace deployment;
    if(!ready.load()) return;
    std::lock_guard<std::mutex> lock(mutex);
    std::lock_guard<std::recursive_mutex> maintenance_lock(g_driver_maintenance_mutex);
    if(g_driver_maintenance && WaitForSingleObject(g_driver_maintenance,0)==WAIT_OBJECT_0) {
        CloseHandle(g_driver_maintenance); g_driver_maintenance=nullptr;
        status=DeploymentDriverResult(dir); drs_snapshot=dlss5::drs::Snapshot();
    }
    const auto* ui=g_panel_gui;
    ui->SeparatorText(DeploymentText("统一部署 / Deployment").c_str());
    const auto text=[ui](const std::string& value){ui->TextUnformatted(DeploymentText(value).c_str(),nullptr);};
    text(status);
    char line[256]{};
    if(g_panel_advanced) {
        sprintf_s(line,"GPU arch=0x%x | NR %s | %s",architecture,
            nr_label.empty() ? (g_panel_language.chinese ? "无匹配运行库":"no matching runtime"):nr_label.c_str(),
            nr_valid ? (g_panel_language.chinese ? "SHA256 已校验":"SHA256 verified") : (g_panel_language.chinese ? "不可用":"unavailable")); text(line);
    }
    text(current ? (redirected?"本次 SL：2.14.1 重定向成功 / Current SL: 2.14.1 redirected":"本次 SL：等待初始化 / Current SL: awaiting initialization"):
                   "本次 SL：游戏目录文件（可能是旧工具已替换的文件） / Current SL: game-directory files (may include replacements by older tools)");
    text("点击 SL 版本即保存并同步驱动，重启生效；与帧生成开关无关 / Selecting an SL version saves it and synchronizes driver settings; restart to apply. Independent of frame generation.");
    int selection=static_cast<int>(state.selected);
    bool sl_changed=ui->RadioButton2(DeploymentText("游戏目录 / Game directory##deploy-sl0").c_str(),&selection,0);
    sl_changed=ui->RadioButton2(DeploymentText("2.14.1 + DLSS-G 310.9.1##deploy-sl1").c_str(),&selection,1) || sl_changed;
    if(sl_changed && static_cast<uint32_t>(selection)!=state.selected) {
        if(selection && !sl_valid) status=sl_reject.empty() ? "文件或 interposer 不匹配，未保存 / Package or interposer does not match." :
            sl_reject+"\n未保存 / Not saved.";
        else { State next=state; next.selected=static_cast<uint32_t>(selection); if(Save(next)) DeploymentRequestDriver(dlss5::maintenance::DriverAction::Synchronize); }
    }
    if(!sl_valid && !sl_reject.empty() && status.compare(0,sl_reject.size(),sl_reject)!=0) text(sl_reject);
    if(state.selected!=current) {
        text("SL 版本更改已保存，请重启游戏生效 / SL version change saved; restart the game to apply.");
        bool cancel=false;
        if(ui->Checkbox(DeploymentText("撤销下次启动更改 / Cancel scheduled change").c_str(),&cancel) && cancel) {
            State next=state; next.selected=current; Save(next); selection=static_cast<int>(current);
        }
    }
    DeploymentDrawHookMode();
    if(g_panel_advanced) {
    text("MFG 建议仅供查看：Architecture=Auto；RuntimeSelectionMode=0。 / MFG recommendations only: Architecture=Auto; RuntimeSelectionMode=0.");
    text(architecture==0x190 ? "RTX 40：Legacy software flip pacing 建议开启；更改后重启。 / RTX 40: Legacy pacing recommended; restart after changing." :
         architecture==0x1b0 ? "RTX 50 默认保留原生节奏；Legacy 为可选兼容实验。 / RTX 50: native pacing by default; Legacy is optional. Restart after changing." :
         "RTX 20/30：建议使用 2.14.1，兼容选项位于 MFG 面板。 / RTX 20/30: 2.14.1 recommended; compatibility options are in MFG Unlock.");
    int early=static_cast<int>(state.early);
    text("提前初始化 CUDA（重启生效） / Early CUDA initialization (restart required)");
    ui->RadioButton2(DeploymentText("自动 / Auto##deploy-cu0").c_str(),&early,0);
    ui->RadioButton2(DeploymentText("开启 / On##deploy-cu1").c_str(),&early,1);
    ui->RadioButton2(DeploymentText("关闭 / Off##deploy-cu2").c_str(),&early,2);
    if(early!=static_cast<int>(state.early)) {
        State next=state; next.early=static_cast<uint32_t>(early);
        if(Save(next)) status="CUDA 选择已保存，重启生效；启动失败将回退 / CUDA selection saved for a restart trial.";
    }
    ui->SeparatorText(DeploymentText("驱动配置 / Driver settings").c_str());
    text(drs_snapshot);
    text("同步 8 个公开帧生成项；本游戏的默认值屏蔽全局覆盖，其他游戏不变，原值可恢复。 / Synchronize 8 public FG settings in this game profile; shadow global overrides without changing other games and retain recovery values.");
    bool synchronize=false;
    if(ui->Checkbox(DeploymentText("确认同步驱动倍率设置 / Synchronize driver multiplier settings").c_str(),&synchronize) && synchronize) {
        DeploymentRequestDriver(dlss5::maintenance::DriverAction::Synchronize);
    }
    bool restore=false;
    if(ui->Checkbox(DeploymentText("还原本插件的驱动改动 / Restore plugin DRS changes").c_str(),&restore) && restore) {
        DeploymentRequestDriver(dlss5::maintenance::DriverAction::Restore);
    }
    }
    ui->SeparatorText(DeploymentText("故障与恢复 / Recovery").c_str());
    text(safe.load()?"安全模式：本次暂停 NR/SL 干预 / Safe mode":"游戏无法启动时，双击 DLSS5-Recovery 故障恢复.cmd / Run DLSS5-Recovery 故障恢复.cmd if startup fails.");
    if(g_panel_advanced || safe.load() || state.safe) {
    bool safe_next=state.safe!=0;
    if(ui->Checkbox(DeploymentText("下次启动安全模式（手动退出） / Safe mode next launch").c_str(),&safe_next)) {
        State next=state; next.safe=safe_next?1u:0u;
        if(!safe_next) { next.active=0; next.failures=0; }
        Save(next);
        status="重启后生效 / Applies on restart.";
    }
    bool export_data=false;
    if(ui->Checkbox(DeploymentText("导出诊断 / Export diagnostics").c_str(),&export_data) && export_data) DeploymentExportDiagnostics(game,dir);
    }
}

#include <tlhelp32.h>
extern "C" __declspec(dllexport) void CALLBACK Maintenance(HWND, HINSTANCE, LPSTR command, int) {
    namespace fs=std::filesystem;
    const std::string action=command?command:"";
    const auto driver_action=dlss5::maintenance::Parse(action,dlss5::maintenance::UserKey());
    const bool driver_operation=driver_action!=dlss5::maintenance::DriverAction::None;
    if(!driver_operation && action!="safe" && action!="normal" && action!="diagnose" && action!="reset-layers") {
        MessageBoxW(nullptr,L"未知操作或授权账户改变，未执行。请使用游戏面板或故障恢复入口。\nUnknown action or different authorization account; nothing changed.",L"DLSS5 Recovery",MB_OK); return;
    }
    HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    bool running=false,checked=false;
    if(processes!=INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W process{}; process.dwSize=sizeof(process);
        if(Process32FirstW(processes,&process)) {
            checked=true;
            do { if(_wcsicmp(process.szExeFile,L"Endfield.exe")==0) running=true; } while(Process32NextW(processes,&process));
        }
        CloseHandle(processes);
    }
    if(!driver_operation && (!checked || running)) { MessageBoxW(nullptr,L"请先退出 Endfield；进程核验失败时不操作。 / Exit Endfield before recovery.",L"DLSS5 Recovery",MB_OK); return; }
    wchar_t module[32768]{}; GetModuleFileNameW(g_self,module,32768);
    const fs::path game=fs::path(module).parent_path();
    const auto state_dir=dlss5::durable::StateDirectory(game);
    if(state_dir.empty()) { MessageBoxW(nullptr,L"无法写入恢复状态 / Cannot write recovery state",L"DLSS5 Recovery",MB_OK); return; }
    if(driver_operation) {
        auto result=dlss5::drs::Synchronize(state_dir,driver_action==dlss5::maintenance::DriverAction::Restore,game);
        bool child_result=false;
        if(result.needs_elevation && !dlss5::maintenance::Elevated()) {
            const auto runner=dlss5::maintenance::Runner();
            const auto parameters=dlss5::maintenance::Parameters(module,driver_action,dlss5::maintenance::UserKey());
            SHELLEXECUTEINFOW info{}; info.cbSize=sizeof(info); info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
            info.lpVerb=L"runas"; info.lpFile=runner.c_str(); info.lpParameters=parameters.c_str();
            info.lpDirectory=game.c_str(); info.nShow=SW_HIDE;
            if(!runner.empty() && !parameters.empty() && ShellExecuteExW(&info) && info.hProcess) {
                // This wait is in rundll32, never in the game's render thread.
                WaitForSingleObject(info.hProcess,INFINITE); CloseHandle(info.hProcess); child_result=true;
            } else result={"未完成管理员授权，驱动记录保留，可重试；不阻止游戏启动 / Administrator authorization was not completed; journal retained, retry available, game startup unaffected."};
        }
        if(!child_result) dlss5::durable::Write(state_dir/L"drs-result.txt",result.message.data(),static_cast<DWORD>(result.message.size()));
        if(action.find('@')==std::string::npos) {
            const auto message=child_result ? DeploymentDriverResult(state_dir):result.message;
            const int n=MultiByteToWideChar(CP_UTF8,0,message.c_str(),-1,nullptr,0);
            std::wstring text(static_cast<size_t>(n),L'\0'); MultiByteToWideChar(CP_UTF8,0,message.c_str(),-1,text.data(),n);
            MessageBoxW(nullptr,text.c_str(),L"DLSS5 Recovery",MB_OK);
        }
        return;
    }
    if(action=="diagnose") { DeploymentExportDiagnostics(game,state_dir); MessageBoxW(nullptr,DeploymentLogFolder(game,state_dir).c_str(),L"诊断保存位置 / Diagnostic location",MB_OK); return; }
    if(action=="reset-layers") {
        const bool okay=dlss5::durable::ArchiveRecord(state_dir/L"generic-layers.state");
        MessageBoxW(nullptr,okay ? L"层数确认状态已归档。原 ReShade.ini 参数保留；下次启动会重新确认 5 层及以上设置。\nLayer confirmation record archived. ReShade.ini is preserved; 5+ layers require confirmation on the next launch." :
            L"归档失败，原状态保留。\nCould not archive the record; original retained.",L"DLSS5 Recovery",MB_OK); return;
    }
    deployment::State next;
    const auto file=state_dir/L"bridge.state";
    const auto result=dlss5::durable::Read(file,next,next.magic);
    if(result==dlss5::durable::ReadResult::Invalid) {
        std::error_code ec; fs::copy_file(file,fs::path(file.wstring()+L".bad-"+std::to_wstring(GetTickCount64())),fs::copy_options::none,ec);
        if(ec) { MessageBoxW(nullptr,L"无法备份损坏状态，未覆盖 / Cannot preserve invalid state",L"DLSS5 Recovery",MB_OK); return; }
        next={};
    }
    if(action!="safe" && action!="normal") { MessageBoxW(nullptr,L"通过 DLSS5-Recovery 故障恢复.cmd 选择操作 / Choose an action in DLSS5-Recovery 故障恢复.cmd",L"DLSS5 Recovery",MB_OK); return; }
    next.safe=action=="safe"?1u:0u; next.active=0; next.healthy=0; next.failures=0; next.selected=0; next.confirmed=0; ++next.generation;
    if(!dlss5::durable::Save(file,next)) { MessageBoxW(nullptr,L"保存失败 / Save failed",L"DLSS5 Recovery",MB_OK); return; }
    MessageBoxW(nullptr,L"已保存。下次启动使用游戏目录的 Streamline。\nSaved. The next launch uses the game-directory Streamline files.\n\n旧工具改过游戏文件时，请通过启动器校验修复。\nUse the launcher repair feature to restore files changed by older tools.",L"DLSS5 Recovery",MB_OK);
}
