// TEST ONLY. A ReShade add-on that recreates, on demand, the start-up race the
// SR carrier retry handles: when the first D3D12 device is created (in a Vulkan
// game that is dlss5-bridge's private device, about 1 s before its carrier
// search), it loads the game folder's nvngx_dlss.dll and holds that reference
// for 3 s, so the first carrier search sees two SR modules (the driver's DLSS
// model and nvngx_dlss.dll).
// Expected: a bridge without the retry refuses NR for the session; a bridge
// with it logs "SR carrier hook not installed (attempt 1 of 30)", later
// "SR carrier hook installed on attempt N", and NR runs.
// It never opens dlss5-bridge.log: the bridge appends with exclusive access and
// drops a line whenever another handle is open. Remove it after the test.
#include <windows.h>
#include <cstdio>
#include "reshade_api.hpp"
#include "reshade_events.hpp"

extern "C" __declspec(dllexport) const char *NAME = "carrier-race-helper (test only)";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Holds nvngx_dlss.dll during dlss5-bridge's SR carrier search to reproduce the start-up race. Remove after testing.";

typedef bool (*PFN_Register)(HMODULE, uint32_t);
typedef void (*PFN_Unregister)(HMODULE);
typedef void (*PFN_RegisterEvent)(reshade::addon_event, void *);
static const DWORD kHoldMs = 3000;
static HMODULE g_self;
static wchar_t g_dir[MAX_PATH];
static PFN_Unregister g_unregister;
static volatile LONG g_fired;

static void Note(const char *text)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%ls\\carrier-race-helper.log", g_dir);
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"ab") != 0 || file == nullptr) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(file, "%02u:%02u:%02u.%03u  %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
    fclose(file);
}

static DWORD WINAPI Release(void *held)
{
    Sleep(kHoldMs);
    FreeLibrary(static_cast<HMODULE>(held));
    Note("released nvngx_dlss.dll; compare with the bridge log (multiple SR modules found / attempt N)");
    return 0;
}

static void OnInitDevice(reshade::api::device *device)
{
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d12) return;
    if (InterlockedExchange(&g_fired, 1) != 0) return;
    // From here a thread runs in this module, so it must outlive ReShade's
    // add-on unload cycles.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(&OnInitDevice), &pinned);
    wchar_t dll[MAX_PATH];
    swprintf_s(dll, L"%ls\\nvngx_dlss.dll", g_dir);
    const HMODULE held = LoadLibraryW(dll);
    if (held == nullptr) { Note("a D3D12 device was created, but nvngx_dlss.dll could not be loaded"); return; }
    Note("a D3D12 device was created; holding the game folder's nvngx_dlss.dll for 3000 ms");
    HANDLE thread = CreateThread(nullptr, 0, Release, held, 0, nullptr);
    if (thread != nullptr) CloseHandle(thread);
    else { FreeLibrary(held); Note("no release thread; released at once"); }
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr && g_unregister != nullptr) g_unregister(self);
        return TRUE;
    }
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    g_self = self;
    GetModuleFileNameW(self, g_dir, MAX_PATH);
    if (wchar_t *slash = wcsrchr(g_dir, L'\\')) *slash = L'\0';
    HMODULE reshade = nullptr;
    static const wchar_t *const hosts[] = {L"ReShade64.dll", L"dxgi.dll", L"d3d12.dll", L"d3d11.dll", L"opengl32.dll"};
    for (const wchar_t *name : hosts) {
        reshade = GetModuleHandleW(name);
        if (reshade != nullptr && GetProcAddress(reshade, "ReShadeRegisterAddon") != nullptr) break;
        reshade = nullptr;
    }
    if (reshade == nullptr) return TRUE;
    auto reg = reinterpret_cast<PFN_Register>(GetProcAddress(reshade, "ReShadeRegisterAddon"));
    auto reg_event = reinterpret_cast<PFN_RegisterEvent>(GetProcAddress(reshade, "ReShadeRegisterEvent"));
    if (reg == nullptr || reg_event == nullptr) return TRUE;
    bool registered = false;
    for (uint32_t version = 18; version >= 5 && !registered; --version) registered = reg(self, version);
    if (!registered) return TRUE;
    g_unregister = reinterpret_cast<PFN_Unregister>(GetProcAddress(reshade, "ReShadeUnregisterAddon"));
    reg_event(reshade::addon_event::init_device, reinterpret_cast<void *>(&OnInitDevice));
    Note("registered; waiting for the first D3D12 device");
    return TRUE;
}
