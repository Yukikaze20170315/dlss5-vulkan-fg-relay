// Composition guard: while NR runs at the FG input and the game window is shown,
// a 2x2, near-transparent, click-through, non-activating tool window sits at the
// top-left corner of the game's monitor. DWM then keeps composing that monitor
// instead of promoting the game to Independent Flip. On that promotion (and back)
// Streamline switches DLSS-G pacing and presents ~12 raw game frames without
// calling FG, and NR runs only inside FG -- the visible "NR off" flash
// (measured 2026-10-01 in Arknights: Endfield with PresentMon and the [fg-transition] log).
// The window belongs to this process: no taskbar, Alt+Tab, tray or separate
// Task Manager entry, and it is excluded from screen capture where Windows allows.

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

struct CompositionGuardConfig { bool on; BYTE alpha; bool hide_capture; };

static CompositionGuardConfig CompositionGuardReadConfig()
{
    CompositionGuardConfig c = { true, 1, true };
    wchar_t path[1024] = {};
    PresentAdapterPath(path, _countof(path), L"vk-present-adapter.ini");
    if (path[0] == 0) return c;
    c.on = GetPrivateProfileIntW(L"Adapter", L"HoldComposition", 1, path) == 1;
    const UINT alpha = GetPrivateProfileIntW(L"Adapter", L"HoldCompositionAlpha", 1, path);
    c.alpha = static_cast<BYTE>(alpha < 1 ? 1 : (alpha > 255 ? 255 : alpha));
    c.hide_capture = GetPrivateProfileIntW(L"Adapter", L"HoldCompositionHideCapture", 1, path) == 1;
    return c;
}

static LRESULT CALLBACK CompositionGuardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCHITTEST:     return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    default:               return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static DWORD WINAPI CompositionGuardThread(LPVOID owner_raw)
{
    const HMODULE owner = static_cast<HMODULE>(owner_raw);
    const wchar_t *const cls = L"dlss5-bridge-composition-guard";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CompositionGuardProc;
    wc.hInstance = owner;
    wc.lpszClassName = cls;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOWTEXT + 1));
    const DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    HWND guard = nullptr;
    if (RegisterClassExW(&wc) != 0)
        guard = CreateWindowExW(ex, cls, L"", WS_POPUP, 0, 0, 2, 2, nullptr, nullptr, owner, nullptr);
    if (guard == nullptr) {
        Log("[composition-guard] unavailable: window creation failed (error %lu); presentation mode is left to Windows", GetLastError());
        FreeLibraryAndExitThread(owner, 0);
    }
    CompositionGuardConfig applied = { false, 0, false };
    bool shown = false, affinity_known = false;
    RECT placed = {};
    LONG last_evaluates = InterlockedCompareExchange(&g_focus_fg_evaluates, 0, 0);
    ULONGLONG last_eval_tick = 0, next_config = 0;
    CompositionGuardConfig cfg = CompositionGuardReadConfig();
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        const ULONGLONG now = GetTickCount64();
        if (now >= next_config) { next_config = now + 1000; cfg = CompositionGuardReadConfig(); }
        const LONG evaluates = InterlockedCompareExchange(&g_focus_fg_evaluates, 0, 0);
        if (evaluates != last_evaluates) { last_evaluates = evaluates; last_eval_tick = now; }

        if (cfg.alpha != applied.alpha) {
            SetLayeredWindowAttributes(guard, 0, cfg.alpha, LWA_ALPHA);
            applied.alpha = cfg.alpha;
        }
        if (!affinity_known || cfg.hide_capture != applied.hide_capture) {
            const BOOL ok = SetWindowDisplayAffinity(guard, cfg.hide_capture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
            Log("[composition-guard] capture exclusion %s: %s", cfg.hide_capture ? "requested" : "off",
                ok ? "applied" : "not supported by this Windows build");
            applied.hide_capture = cfg.hide_capture;
            affinity_known = true;
        }

        const FocusFgWindow game = FocusFgWindowSnapshot();
        const bool window_ok = game.window != nullptr && IsWindow(game.window) &&
            IsWindowVisible(game.window) && !IsIconic(game.window);
        const bool nr_at_fg = InterlockedCompareExchange(&g_focus_fg_enabled, 0, 0) != 0;
        const bool fg_running = last_eval_tick != 0 && now - last_eval_tick < 1500;
        const bool want = cfg.on && nr_at_fg && fg_running && window_ok;

        RECT target = {};
        MONITORINFO mi = { sizeof(mi) };
        if (want && GetMonitorInfoW(MonitorFromWindow(game.window, MONITOR_DEFAULTTONEAREST), &mi))
            target = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.left + 2, mi.rcMonitor.top + 2 };
        if (want && !shown) {
            SetWindowPos(guard, HWND_TOPMOST, target.left, target.top, 2, 2, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            placed = target;
            shown = true;
            Log("[composition-guard] shown at %ld,%ld alpha=%u", target.left, target.top, static_cast<unsigned>(cfg.alpha));
        } else if (want && (target.left != placed.left || target.top != placed.top)) {
            SetWindowPos(guard, HWND_TOPMOST, target.left, target.top, 2, 2, SWP_NOACTIVATE);
            placed = target;
            Log("[composition-guard] moved to %ld,%ld (game window changed monitor)", target.left, target.top);
        } else if (!want && shown) {
            ShowWindow(guard, SW_HIDE);
            shown = false;
            Log("[composition-guard] hidden: config=%u nr_at_fg=%u fg_running=%u window_shown=%u",
                cfg.on ? 1u : 0u, nr_at_fg ? 1u : 0u, fg_running ? 1u : 0u, window_ok ? 1u : 0u);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
    }
}

// Caller holds g_hook_cs (FocusFgMaintenance); starts the guard thread once.
static void CompositionGuardStart()
{
    static bool started;
    if (started) return;
    started = true;
    HANDLE thread = StartModuleWorker(&CompositionGuardThread);
    if (thread != nullptr) CloseHandle(thread);
    else Log("[composition-guard] unavailable: thread start failed (error %lu)", GetLastError());
}
