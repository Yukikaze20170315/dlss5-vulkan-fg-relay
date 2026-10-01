// Runs the production composition guard against a 1x1 non-activating stand-in
// for the game window. No game, ReShade, Vulkan, NGX or hook is involved.
#include "dlss5-bridge.cpp"

namespace guard_test {
static unsigned checks, failures;
static void Check(bool ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", why); }
    else std::printf("ok: %s\n", why);
}

static volatile LONG pump_evaluates = 1;
static DWORD WINAPI EvaluatePump(LPVOID)
{
    for (;;) {
        if (InterlockedCompareExchange(&pump_evaluates, 0, 0)) InterlockedIncrement(&g_focus_fg_evaluates);
        Sleep(50);
    }
}

static void WriteIni(int on, int alpha)
{
    wchar_t path[1024];
    PresentAdapterPath(path, _countof(path), L"vk-present-adapter.ini");
    wchar_t v[16];
    swprintf_s(v, L"%d", on);    WritePrivateProfileStringW(L"Adapter", L"HoldComposition", v, path);
    swprintf_s(v, L"%d", alpha); WritePrivateProfileStringW(L"Adapter", L"HoldCompositionAlpha", v, path);
}

static HWND FindGuard()
{
    HWND h = FindWindowExW(nullptr, nullptr, L"dlss5-bridge-composition-guard", nullptr);
    DWORD pid = 0;
    if (h != nullptr) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId() ? h : nullptr;
}

static bool WaitFor(bool visible, DWORD ms)
{
    const ULONGLONG end = GetTickCount64() + ms;
    do {
        HWND g = FindGuard();
        if (g != nullptr && (IsWindowVisible(g) != FALSE) == visible) return true;
        Sleep(50);
    } while (GetTickCount64() < end);
    return false;
}

struct Monitors { RECT r[8]; unsigned n; };
static BOOL CALLBACK AddMonitor(HMONITOR, HDC, LPRECT rect, LPARAM p)
{
    auto *m = reinterpret_cast<Monitors *>(p);
    if (m->n < 8) m->r[m->n++] = *rect;
    return TRUE;
}

static void CheckPlacement(HWND guard, HWND game, const char *where)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi);
    RECT r = {};
    GetWindowRect(guard, &r);
    char what[160];
    sprintf_s(what, "%s: guard is 2x2 at the game monitor's top-left (%ld,%ld)", where, mi.rcMonitor.left, mi.rcMonitor.top);
    Check(r.left == mi.rcMonitor.left && r.top == mi.rcMonitor.top && r.right - r.left == 2 && r.bottom - r.top == 2, what);
}

static int Run()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    InitializeCriticalSection(&g_log_cs);
    strcpy_s(g_log_path, "guard.log");
    WriteIni(1, 1);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"guard-test-game";
    RegisterClassW(&wc);
    Monitors mons = {};
    EnumDisplayMonitors(nullptr, nullptr, &AddMonitor, reinterpret_cast<LPARAM>(&mons));
    const RECT m0 = mons.r[0];
    HWND game = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
        m0.left + 10, m0.top + 10, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    Check(game != nullptr, "stand-in game window created");
    const HWND foreground_before = GetForegroundWindow();

    AcquireSRWLockExclusive(&g_focus_fg_lock);
    g_focus_fg_window.window = game;
    ReleaseSRWLockExclusive(&g_focus_fg_lock);

    InterlockedExchange(&g_focus_fg_enabled, 1);
    CreateThread(nullptr, 0, &EvaluatePump, nullptr, 0, nullptr);
    CompositionGuardStart();
    CompositionGuardStart();
    Sleep(600);
    HWND guard = FindGuard();
    Check(guard != nullptr, "guard window exists in this process");
    if (guard == nullptr) return 1;
    Check(!IsWindowVisible(guard), "hidden while the game window is not shown");

    ShowWindow(game, SW_SHOWNOACTIVATE);
    Check(WaitFor(true, 1500), "shown once the game window is visible, NR is at the FG input and FG evaluates");
    CheckPlacement(guard, game, "first monitor");

    const LONG_PTR ex = GetWindowLongPtrW(guard, GWL_EXSTYLE);
    Check((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW), "tool window without APPWINDOW: no taskbar or Alt+Tab entry");
    Check((ex & WS_EX_NOACTIVATE) != 0, "non-activating style");
    Check((ex & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) == (WS_EX_LAYERED | WS_EX_TRANSPARENT), "layered and mouse-transparent");
    Check((ex & WS_EX_TOPMOST) != 0, "topmost");
    Check(GetWindow(guard, GW_OWNER) == nullptr, "no owner window");
    wchar_t title[8] = L"x";
    Check(GetWindowTextW(guard, title, _countof(title)) == 0, "empty title");
    BYTE alpha = 0; DWORD flags = 0;
    Check(GetLayeredWindowAttributes(guard, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 1, "opacity 1/255");
    DWORD affinity = 0;
    Check(GetWindowDisplayAffinity(guard, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE, "excluded from screen capture");
    Check(GetForegroundWindow() == foreground_before, "foreground window unchanged");
    RECT gr; GetWindowRect(guard, &gr);
    Check(WindowFromPoint(POINT{ gr.left, gr.top }) != guard, "hit testing passes through the guard pixel");
    Check(GetWindowThreadProcessId(guard, nullptr) != GetCurrentThreadId(), "runs on its own thread");

    WriteIni(1, 5);
    Sleep(1600);
    Check(GetLayeredWindowAttributes(guard, nullptr, &alpha, &flags) && alpha == 5, "opacity follows HoldCompositionAlpha live");
    WriteIni(1, 1);

    if (mons.n >= 2) {
        const RECT m1 = mons.r[1];
        SetWindowPos(game, nullptr, m1.left + 10, m1.top + 10, 1, 1, SWP_NOACTIVATE | SWP_NOZORDER);
        Sleep(600);
        CheckPlacement(guard, game, "after the game window moves to the second monitor");
    } else std::printf("note: one monitor only, move scenario skipped\n");

    InterlockedExchange(&pump_evaluates, 0);
    Check(WaitFor(false, 2500), "hidden after FG evaluates stop");
    InterlockedExchange(&pump_evaluates, 1);
    Check(WaitFor(true, 1500), "shown again when FG evaluates resume");

    ShowWindow(game, SW_SHOWMINNOACTIVE);
    Check(WaitFor(false, 1000), "hidden while the game window is minimized");
    ShowWindow(game, SW_SHOWNOACTIVATE);
    Check(WaitFor(true, 1500), "shown after restore");

    InterlockedExchange(&g_focus_fg_enabled, 0);
    Check(WaitFor(false, 1000), "hidden when NR is not at the FG input");
    InterlockedExchange(&g_focus_fg_enabled, 1);
    Check(WaitFor(true, 1500), "shown when NR returns to the FG input");

    WriteIni(0, 1);
    Check(WaitFor(false, 2000), "HoldComposition=0 removes it live");
    WriteIni(1, 1);
    Check(WaitFor(true, 2000), "HoldComposition=1 restores it live");

    Check(GetForegroundWindow() == foreground_before, "foreground window still unchanged at the end");
    DestroyWindow(game);
    Check(WaitFor(false, 1000), "hidden once the game window is gone");
    std::printf("guard: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
}

int main() { return guard_test::Run(); }
