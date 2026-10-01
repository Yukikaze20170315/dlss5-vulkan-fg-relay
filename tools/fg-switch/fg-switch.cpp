// Endfield FG Switch: one window that switches Arknights: Endfield between its own Streamline 2.10.3
// and Streamline 2.14.1 + DLSS-G 310.9.1, and sets the matching NVIDIA driver multiplier.
#include "core.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>

enum { ID_FOLDER = 100, ID_BROWSE, ID_INFO, ID_MULT, ID_UPGRADE, ID_STOCK, ID_STATUS };
static HWND g_wnd, g_folder, g_info, g_mult, g_upgrade, g_stock, g_status;
static std::wstring g_dir;
static GpuInfo g_gpu;
static HFONT g_font;

static const wchar_t* kDefaultDir = L"C:\\Program Files\\Hypergryph Launcher\\games\\Endfield Game";

static std::wstring SavedDir() {
    std::vector<unsigned char> b; std::wstring d;
    if (ReadAll(AppDataDir() + L"\\folder.txt", b) && b.size() % 2 == 0) d.assign(reinterpret_cast<const wchar_t*>(b.data()), b.size() / 2);
    return IsGameFolder(d) ? d : IsGameFolder(kDefaultDir) ? kDefaultDir : std::wstring();
}
static void SaveDir(const std::wstring& d) {
    HANDLE f = CreateFileW((AppDataDir() + L"\\folder.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return; DWORD w = 0; WriteFile(f, d.data(), static_cast<DWORD>(d.size() * 2), &w, nullptr); CloseHandle(f);
}
// Highest multiplier the card and driver allow: RTX 50 needs driver 595.97 or newer for 5x/6x.
static int MaxMultiplier() { return g_gpu.tier == 50 ? (g_gpu.driver >= 59597 ? 6 : 4) : g_gpu.tier == 40 ? 2 : 0; }

static void Refresh() {
    SetWindowTextW(g_folder, g_dir.empty() ? L"（未找到，请点“浏览”选择 Endfield.exe 所在的文件夹）/ (not found; click Browse)" : g_dir.c_str());
    std::wstring info;
    if (!g_gpu.nvapi) info += L"显卡：未检测到 NVIDIA 显卡或驱动 / GPU: no NVIDIA GPU or driver found\r\n";
    else {
        wchar_t drv[32]; swprintf_s(drv, L"%u.%02u", g_gpu.driver / 100, g_gpu.driver % 100);
        info += L"显卡 / GPU: " + g_gpu.name + L"    驱动 / Driver: " + drv + L"\r\n";
        info += g_gpu.tier == 50 ? (MaxMultiplier() == 6 ? L"可用倍率 / Available: 2×–6×\r\n" : L"可用倍率 / Available: 2×–4×（5×、6× 需要 595.97 或更新的驱动 / 5x and 6x need driver 595.97 or newer）\r\n")
              : g_gpu.tier == 40 ? L"RTX 40 系列只支持 2× 帧生成，由游戏菜单控制 / RTX 40 supports 2x only, set in the game menu\r\n"
              : L"此显卡不支持 DLSS 帧生成 / This GPU has no DLSS Frame Generation\r\n";
    }
    int whole = -3;
    if (!g_dir.empty()) {
        whole = ReadFolder(g_dir).whole;
        info += std::wstring(L"游戏中的 Streamline / Game's Streamline: ") +
            (whole == kStock ? L"原装 2.10.3（最高 4×）/ original 2.10.3 (up to 4x)" : whole == kUpgraded ? L"2.14.1 + 帧生成 310.9.1 / 2.14.1 + FG 310.9.1"
             : whole == -1 ? L"两种版本混合（上次切换未完成，再切换一次即可）/ mixed (finish by switching again)" : L"未知版本（游戏可能已更新）/ unknown version (the game may have been updated)") + L"\r\n";
    }
    if (g_gpu.nvapi) {
        const int m = CurrentMultiplier();
        info += std::wstring(L"驱动倍率 / Driver multiplier: ") + (m < 0 ? L"未找到游戏配置 / no game profile" : m == 0 ? L"由游戏决定 / set by the game" : (std::to_wstring(m) + L"× 固定 / fixed").c_str());
    }
    SetWindowTextW(g_info, info.c_str());
    const bool folder = !g_dir.empty() && whole >= -1;
    EnableWindow(g_upgrade, folder && g_gpu.tier >= 40);
    EnableWindow(g_mult, folder && g_gpu.tier == 50);
    EnableWindow(g_stock, folder);
}
static void Status(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }

static void DoUpgrade() {
    std::wstring err; int mult = 0;
    if (g_gpu.tier == 50) mult = 2 + static_cast<int>(SendMessageW(g_mult, CB_GETCURSEL, 0, 0));
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = SwitchFiles(g_dir, kUpgraded, err) && SetMultiplier(mult, err);
    Status(ok ? (mult ? L"完成：已切换到 2.14.1，驱动固定 " + std::to_wstring(mult) + L"×。启动游戏后在游戏设置里打开 DLSS 帧生成即可。\r\nDone: switched to 2.14.1, driver fixed at " + std::to_wstring(mult) + L"x. Start the game and turn on DLSS Frame Generation in its settings."
                     : std::wstring(L"完成：已切换到 2.14.1，倍率由游戏菜单决定。\r\nDone: switched to 2.14.1; the game menu sets the multiplier."))
              : L"失败 / Failed: " + err);
    Refresh();
}
static void DoStock() {
    std::wstring err; SetCursor(LoadCursor(nullptr, IDC_WAIT));
    bool ok = SwitchFiles(g_dir, kStock, err);
    if (ok && g_gpu.nvapi) ok = SetMultiplier(0, err);
    Status(ok ? std::wstring(L"完成：已恢复原装 2.10.3，倍率交还游戏菜单。\r\nDone: original 2.10.3 restored; the game menu sets the multiplier again.") : L"失败 / Failed: " + err);
    Refresh();
}
static void DoBrowse() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opt = 0; dlg->GetOptions(&opt); dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"选择 Endfield.exe 所在的文件夹 / Choose the folder that contains Endfield.exe");
    IShellItem* item = nullptr; PWSTR path = nullptr;
    if (SUCCEEDED(dlg->Show(g_wnd)) && SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        if (IsGameFolder(path)) { g_dir = path; SaveDir(g_dir); Status(L""); }
        else Status(L"这个文件夹里没有 Endfield.exe 和 sl.interposer.dll。/ This folder does not contain Endfield.exe and sl.interposer.dll.");
    }
    CoTaskMemFree(path); if (item) item->Release(); dlg->Release();
    Refresh();
}

static HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, int dpi) {
    auto s = [dpi](int v) { return MulDiv(v, dpi, 96); };
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, s(x), s(y), s(w), s(h), g_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE); return c;
}
static LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = w; const int dpi = GetDpiForWindow(w);
        NONCLIENTMETRICSW m{}; m.cbSize = sizeof(m); SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(m), &m, 0);
        m.lfMessageFont.lfHeight = -MulDiv(10, dpi, 72); g_font = CreateFontIndirectW(&m.lfMessageFont);
        Make(L"STATIC", L"游戏文件夹 / Game folder:", 0, 12, 12, 600, 20, 0, dpi);
        g_folder = Make(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_BORDER, 12, 34, 500, 24, ID_FOLDER, dpi);
        Make(L"BUTTON", L"浏览… / Browse…", BS_PUSHBUTTON, 520, 33, 120, 26, ID_BROWSE, dpi);
        g_info = Make(L"STATIC", L"", 0, 12, 70, 628, 92, ID_INFO, dpi);
        Make(L"STATIC", L"帧生成倍率 / Multiplier:", 0, 12, 176, 170, 20, 0, dpi);
        g_mult = Make(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 186, 172, 260, 200, ID_MULT, dpi);
        g_upgrade = Make(L"BUTTON", L"切换到 2.14.1 并应用倍率 / Switch to 2.14.1", BS_DEFPUSHBUTTON, 12, 206, 628, 34, ID_UPGRADE, dpi);
        g_stock = Make(L"BUTTON", L"恢复原装 2.10.3，倍率交还游戏 / Restore original 2.10.3", BS_PUSHBUTTON, 12, 248, 628, 34, ID_STOCK, dpi);
        g_status = Make(L"EDIT", L"", ES_READONLY | ES_MULTILINE | WS_BORDER, 12, 294, 628, 74, ID_STATUS, dpi);
        Make(L"STATIC", L"使用前请完全退出游戏。/ Close the game before using this tool.\r\n2.14.1 用于 5×、6× 帧生成，以及切到其他窗口时保持帧生成和神经渲染。/ 2.14.1 enables 5x and 6x, and keeps FG and NR running behind other windows.", 0, 12, 376, 628, 66, 0, dpi);
        const int max = MaxMultiplier();
        for (int i = 2; i <= std::max(max, 2); ++i) SendMessageW(g_mult, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((std::to_wstring(i) + (i == max && max == 6 ? L"×（推荐 / recommended）" : L"×")).c_str()));
        SendMessageW(g_mult, CB_SETCURSEL, std::max(max, 2) - 2, 0);
        Refresh();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BROWSE) DoBrowse();
        else if (LOWORD(wp) == ID_UPGRADE) DoUpgrade();
        else if (LOWORD(wp) == ID_STOCK) DoStock();
        return 0;
    case WM_CTLCOLORSTATIC: SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT); return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

// Test entry points (output goes to %LOCALAPPDATA%\EndfieldFGSwitch\cli-output.txt):
//   --self-test           embedded files match their recorded SHA-256
//   --state DIR           print the per-file state of a folder
//   --switch DIR 0|1      replace the files only (no driver change)
//   --multiplier N        write the driver multiplier only (0 = back to the game)
static int Cli(int argc, wchar_t** argv) {
    FILE* f = nullptr; _wfreopen_s(&f, (AppDataDir() + L"\\cli-output.txt").c_str(), L"w, ccs=UTF-8", stdout);
    const std::wstring cmd = argv[1];
    if (cmd == L"--self-test") {
        int bad = 0;
        for (int s = 0; s < 2; ++s) for (auto& e : kFiles) { const void* d; DWORD n; if (!Resource(e.res[s], d, n) || Sha256(d, n) != e.sha[s]) { ++bad; wprintf(L"BAD set%d %s\n", s, e.name); } }
        wprintf(L"self-test: %d embedded files, %d mismatches\n", 2 * kFileCount, bad); return bad ? 1 : 0;
    }
    if (cmd == L"--state" && argc == 3) {
        const FolderState s = ReadFolder(argv[2]);
        for (int i = 0; i < kFileCount; ++i) wprintf(L"%s=%d\n", kFiles[i].name, s.file[i]);
        wprintf(L"whole=%d game_folder=%d\n", s.whole, IsGameFolder(argv[2]) ? 1 : 0); return 0;
    }
    if (cmd == L"--switch" && argc == 4) {
        std::wstring err; const bool ok = SwitchFiles(argv[2], argv[3][0] == L'1' ? kUpgraded : kStock, err);
        wprintf(L"switch: %s %s\n", ok ? L"ok" : L"refused", err.c_str()); return ok ? 0 : 2;
    }
    if (cmd == L"--multiplier" && argc == 3) {
        if (NvAPI_Initialize() != NVAPI_OK) { wprintf(L"no NVAPI\n"); return 3; }
        std::wstring err; const bool ok = SetMultiplier(_wtoi(argv[2]), err);
        wprintf(L"multiplier: %s now=%d %s\n", ok ? L"ok" : L"failed", CurrentMultiplier(), err.c_str()); return ok ? 0 : 2;
    }
    wprintf(L"unknown arguments\n"); return 4;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) return Cli(argc, argv);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    g_gpu = ReadGpu(); g_dir = SavedDir();
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = inst; wc.lpszClassName = L"EndfieldFGSwitch";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW); wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    const UINT dpi = GetDpiForSystem();
    RECT r{0, 0, MulDiv(652, dpi, 96), MulDiv(450, dpi, 96)}; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"终末地 帧生成切换 / Endfield FG Switch", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(w, show); UpdateWindow(w);
    MSG m; while (GetMessageW(&m, nullptr, 0, 0)) { if (!IsDialogMessageW(w, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    if (g_gpu.nvapi) NvAPI_Unload();
    return 0;
}
