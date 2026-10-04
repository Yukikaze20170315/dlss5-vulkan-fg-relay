#include <intrin.h>
#include "focus-fg-contract.h"

// Only Streamline's single verified DLSS-G focus call is overridden. Keeping
// the original method for every other caller also preserves its keyboard and
// input handling. Explicit Off, missing tags and resource release are untouched.
struct FocusFgWindow {
    reshade::api::effect_runtime *runtime;
    HWND window;
    LONG width, height;
};
struct FocusFgState {
    Hook focus, options, free_resources;
    bool discovery_done, options_tried, free_tried;
    void *caller;
    ULONGLONG next_poll;
    unsigned last_options_mode, last_options_flags, last_options_frames, last_options_result;
    bool have_options, options_observed, last_options_readable;
};
static FocusFgState g_focus_fg;
static FocusFgWindow g_focus_fg_window;
static SRWLOCK g_focus_fg_lock = SRWLOCK_INIT;
static volatile LONG g_focus_fg_enabled;
static volatile LONG g_focus_fg_calls, g_focus_fg_overrides, g_focus_fg_original_false;
static volatile LONG g_focus_fg_presents, g_focus_fg_evaluates;
static volatile LONGLONG g_focus_fg_request_tick;

static FocusFgWindow FocusFgWindowSnapshot()
{
    AcquireSRWLockShared(&g_focus_fg_lock);
    const FocusFgWindow value = g_focus_fg_window;
    ReleaseSRWLockShared(&g_focus_fg_lock);
    return value;
}

#include "composition-guard.inc"

static void FocusFgSetRuntime(reshade::api::effect_runtime *rt)
{
    FocusFgWindow value = {};
    value.runtime = rt;
    value.window = static_cast<HWND>(rt->get_hwnd());
    RECT rect = {};
    if (value.window != nullptr && GetClientRect(value.window, &rect)) {
        value.width = rect.right - rect.left;
        value.height = rect.bottom - rect.top;
    }
    AcquireSRWLockExclusive(&g_focus_fg_lock);
    g_focus_fg_window = value;
    ReleaseSRWLockExclusive(&g_focus_fg_lock);
    TriggerHookScan();
}

static void FgtOnDestroyRuntime(reshade::api::effect_runtime *rt)
{
    AcquireSRWLockExclusive(&g_focus_fg_lock);
    if (g_focus_fg_window.runtime == rt) g_focus_fg_window = {};
    ReleaseSRWLockExclusive(&g_focus_fg_lock);
}

typedef bool (*PFN_FocusFgHasFocus)(void *);
static bool FocusFgHasFocusDetour(void *keyboard)
{
    void *const caller = _ReturnAddress();
    const bool original = reinterpret_cast<PFN_FocusFgHasFocus>(g_focus_fg.focus.original)(keyboard);
    if (caller != g_focus_fg.caller) return original;
    InterlockedIncrement(&g_focus_fg_calls);
    if (original) return true;
    InterlockedIncrement(&g_focus_fg_original_false);
    if (InterlockedCompareExchange(&g_focus_fg_enabled, 0, 0) == 0) return false;
    const FocusFgWindow value = FocusFgWindowSnapshot();
    DWORD pid = 0;
    RECT rect = {};
    const bool valid = value.window != nullptr && IsWindow(value.window);
    if (valid) GetWindowThreadProcessId(value.window, &pid);
    const bool drawable = valid && GetClientRect(value.window, &rect) &&
        rect.right > rect.left && rect.bottom > rect.top;
    const bool result = focus_fg::Override(original, true, true, value.runtime != nullptr,
        pid == GetCurrentProcessId(), valid && IsWindowVisible(value.window),
        !valid || IsIconic(value.window), drawable,
        drawable && rect.right - rect.left == value.width && rect.bottom - rect.top == value.height);
    if (result) InterlockedIncrement(&g_focus_fg_overrides);
    return result;
}

// Reads only version-1 fields shared by current SL DLSSGOptions versions.
// Diagnostic hooks always forward the exact original pointer and call once.
struct FocusFgOptions {
    void *next;
    uint8_t type[16];
    uint64_t version;
    uint32_t mode, frames, flags, dynamic_width, dynamic_height, backbuffers;
    uint32_t guide_width, guide_height, colour_width, colour_height;
};
static_assert(offsetof(FocusFgOptions, mode) == 32 && sizeof(FocusFgOptions) == 72, "SL DLSSGOptions prefix");
static bool FocusFgReadOptions(const void *options, FocusFgOptions *out)
{
    static const uint8_t type[16] = { 0xcb, 0xf1, 0xc5, 0xfa, 0xfd, 0x2d, 0x36, 0x4f,
        0xa1, 0xe6, 0x3a, 0x9e, 0x86, 0x52, 0x56, 0xc5 };
    __try {
        memcpy(out, options, sizeof(*out));
        return out->version >= 1 && memcmp(out->type, type, sizeof(type)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

typedef uint32_t (*PFN_FocusFgOptions)(const void *, const void *);
static uint32_t FocusFgOptionsDetour(const void *viewport, const void *options)
{
    FocusFgOptions value = {};
    const bool read = FocusFgReadOptions(options, &value);
    const uint32_t result = reinterpret_cast<PFN_FocusFgOptions>(g_focus_fg.options.original)(viewport, options);
    bool report = false;
    AcquireSRWLockExclusive(&g_focus_fg_lock);
    report = !g_focus_fg.options_observed || read != g_focus_fg.last_options_readable ||
        result != g_focus_fg.last_options_result;
    g_focus_fg.options_observed = true;
    g_focus_fg.last_options_readable = read;
    g_focus_fg.last_options_result = result;
    if (read) {
        report = report || !g_focus_fg.have_options || value.mode != g_focus_fg.last_options_mode ||
            value.flags != g_focus_fg.last_options_flags || value.frames != g_focus_fg.last_options_frames;
        g_focus_fg.last_options_mode = value.mode;
        g_focus_fg.last_options_flags = value.flags;
        g_focus_fg.last_options_frames = value.frames;
        g_focus_fg.have_options = true;
    }
    ReleaseSRWLockExclusive(&g_focus_fg_lock);
    if (report) {
        const FocusFgWindow window = FocusFgWindowSnapshot();
        FgtLog("[focus-fg] game SetOptions unchanged: readable=%u version=%llu mode=%u generated=%u flags=0x%x retain_when_off=%u guides=%ux%u colour=%ux%u result=0x%x foreground=%u minimized=%u",
            read ? 1u : 0u, value.version, value.mode, value.frames, value.flags, (value.flags & 8u) ? 1u : 0u,
            value.guide_width, value.guide_height, value.colour_width, value.colour_height, result,
            ForegroundIsThisProcess() ? 1u : 0u, window.window != nullptr && IsIconic(window.window) ? 1u : 0u);
    }
    return result;
}

typedef uint32_t (*PFN_FocusFgFree)(uint32_t, const void *);
static uint32_t FocusFgFreeDetour(uint32_t feature, const void *viewport)
{
    const uint32_t result = reinterpret_cast<PFN_FocusFgFree>(g_focus_fg.free_resources.original)(feature, viewport);
    if (feature == 1000) FgtLog("[focus-fg] game slFreeResources(DLSS_G) forwarded unchanged: result=0x%x foreground=%u",
        result, ForegroundIsThisProcess() ? 1u : 0u);
    return result;
}

static bool FocusFgFindGate(HMODULE module, focus_fg::Site *site, void **object, void **target)
{
    __try {
        const uint8_t *image = reinterpret_cast<const uint8_t *>(module);
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return false;
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
        const auto *sections = IMAGE_FIRST_SECTION(nt);
        unsigned found = 0;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (!(sections[i].Characteristics & IMAGE_SCN_CNT_CODE)) continue;
            focus_fg::Site candidate;
            if (focus_fg::Find(image, nt->OptionalHeader.SizeOfImage, sections[i].VirtualAddress,
                              sections[i].Misc.VirtualSize, &candidate)) {
                ++found;
                *site = candidate;
            }
        }
        if (found != 1) return false;
        *object = *reinterpret_cast<void *const *>(image + site->object_slot);
        // A null interface can be a normal early-load state; retry later.
        if (*object == nullptr) { *target = nullptr; return true; }
        void *const *vtable = *reinterpret_cast<void *const *const *>(*object);
        *target = vtable[3];
        return *target != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Called on game presents: no disk I/O, GPU waits or worker lock. The deferred
// worker owns configuration reads and summaries; the hook's state is atomic.
static void FocusFgOnPresent()
{
    InterlockedIncrement(&g_focus_fg_presents);
    const LONGLONG now = static_cast<LONGLONG>(GetTickCount64());
    const LONGLONG previous = InterlockedCompareExchange64(&g_focus_fg_request_tick, 0, 0);
    if (now - previous >= 1000 && InterlockedCompareExchange64(&g_focus_fg_request_tick, now, previous) == previous)
        TriggerHookScan();
}

// Caller holds g_hook_cs. Module/interface owners are pinned before detouring.
static void FocusFgMaintenance()
{
    if (!PresentAdapterFocusKeepFG()) return;
    FocusFgState &state = g_focus_fg;
    const ULONGLONG now = GetTickCount64();
    if (now >= state.next_poll) {
        state.next_poll = now + 1000;
        wchar_t path[1024] = {};
        PresentAdapterPath(path, _countof(path), L"ReShade.ini");
        // A stage-protocol Generic selects the FG-input feed by its Present layer
        // count; NRHookPoint is only its legacy single-point setting.
        uint32_t plan = 0;
        const bool present_selected = PresentAdapterStagePlan(&plan)
            ? ((plan >> 6) & 7u) != 0
            : GetPrivateProfileIntW(L"RenoDX.DLSS5", L"NRHookPoint", -1, path) == 2;
        const bool enabled = GetPrivateProfileIntW(L"RenoDX.DLSS5", L"NeuralUplift", 0, path) == 1 &&
            GetPrivateProfileIntW(L"RenoDX.DLSS5", L"EnableHooks", 0, path) == 1 && present_selected;
        InterlockedExchange(&g_focus_fg_enabled, enabled ? 1 : 0);
        if (enabled) CompositionGuardStart();
        static unsigned report_count;
        static LONG previous_focus = -1, previous_minimized = -1;
        const FocusFgWindow window = FocusFgWindowSnapshot();
        const LONG focused = ForegroundIsThisProcess() ? 1 : 0;
        const LONG minimized = window.window != nullptr && IsIconic(window.window) ? 1 : 0;
        if (focused != previous_focus || minimized != previous_minimized || ++report_count >= 15) {
            report_count = 0;
            previous_focus = focused;
            previous_minimized = minimized;
            FgtLog("[focus-fg] state: enabled=%u hooked=%u foreground=%ld minimized=%ld hwnd=%p scope_calls=%ld original_false=%ld kept=%ld presents=%ld FG_evaluates=%ld",
                enabled ? 1u : 0u, state.focus.active ? 1u : 0u, focused, minimized, window.window,
                InterlockedCompareExchange(&g_focus_fg_calls, 0, 0), InterlockedCompareExchange(&g_focus_fg_original_false, 0, 0),
                InterlockedCompareExchange(&g_focus_fg_overrides, 0, 0), InterlockedCompareExchange(&g_focus_fg_presents, 0, 0),
                InterlockedCompareExchange(&g_focus_fg_evaluates, 0, 0));
        }
    }
    HMODULE dlssg = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"sl.dlss_g.dll", &dlssg)) return;
    typedef void *(*PFN_GetPluginFunction)(const char *);
    const auto get = reinterpret_cast<PFN_GetPluginFunction>(GetProcAddress(dlssg, "slGetPluginFunction"));
    if (!state.options_tried && get != nullptr) {
        state.options_tried = true;
        void *options = get("slDLSSGSetOptions");
        const bool ok = options != nullptr && HookInstall(state.options, options, reinterpret_cast<void *>(&FocusFgOptionsDetour));
        FgtLog("[focus-fg] SetOptions diagnostic hook %s (arguments unchanged)", ok ? "ready" : "unavailable");
    }
    if (!state.free_tried) {
        HMODULE interposer = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"sl.interposer.dll", &interposer)) {
            state.free_tried = true;
            const bool ok = HookInstall(state.free_resources, reinterpret_cast<void *>(GetProcAddress(interposer, "slFreeResources")), reinterpret_cast<void *>(&FocusFgFreeDetour));
            FgtLog("[focus-fg] FreeResources diagnostic hook %s (arguments unchanged)", ok ? "ready" : "unavailable");
        }
    }
    if (!state.discovery_done) {
        focus_fg::Site site;
        void *object = nullptr, *target = nullptr;
        if (!FocusFgFindGate(dlssg, &site, &object, &target)) {
            state.discovery_done = true;
            FgtLog("[focus-fg] unavailable: unique Streamline focus-gate contract was not found; native focus behaviour retained (this is unrelated to Generic version compatibility)");
        } else if (target != nullptr) {
            state.discovery_done = true;
            state.caller = reinterpret_cast<uint8_t *>(dlssg) + site.return_address;
            const bool ok = PinHookModule(target) && HookInstall(state.focus, target, reinterpret_cast<void *>(&FocusFgHasFocusDetour));
            FgtLog("[focus-fg] scoped focus gate %s: return_rva=0x%zx interface=%p method=%p; explicit FG Off and all resource release remain native",
                ok ? "ready" : "unavailable", site.return_address, object, target);
        }
    }
}
