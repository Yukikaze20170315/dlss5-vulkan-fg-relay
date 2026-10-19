#pragma once
#include "../../common/hook-mode.hpp"
#include <atomic>

using GenericHookModeControl = bool (__cdecl *)(uint32_t, uint32_t, dlss5::hook_mode::Snapshot*);
static std::atomic<GenericHookModeControl> g_generic_hook_mode_control{nullptr};
static bool GenericHookModeCall(uint32_t operation, uint32_t value, dlss5::hook_mode::Snapshot* result) {
    auto control = g_generic_hook_mode_control.load();
    if (!control) {
        const HMODULE consumer = GetModuleHandleW(L"renodx-dlss5.addon64");
        if (!consumer) return false;
        control = reinterpret_cast<GenericHookModeControl>(GetProcAddress(consumer, "DLSS5HookModeControlV1"));
        HMODULE retained = nullptr;
        if (!control || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                            reinterpret_cast<LPCWSTR>(consumer), &retained)) return false;
        g_generic_hook_mode_control.store(control);
    }
    return control(operation, value, result) && result && result->version == 1 &&
        result->size == sizeof(*result) && dlss5::hook_mode::Valid(result->current);
}
static int GenericCurrentHookMode(const wchar_t* ini, int legacy_default) {
    dlss5::hook_mode::Snapshot state;
    if (GenericHookModeCall(dlss5::hook_mode::Current, 0, &state)) return static_cast<int>(state.current);
    return static_cast<int>(GetPrivateProfileIntW(L"RenoDX.DLSS5", L"EnableHooks", legacy_default, ini));
}
