/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: MIT
 */

#if defined(RENODX_DLSS5_DEBUG)
// Debug variant: enable ReShade-side utils debug logging alongside the
// addon's own trace log + crash dumps (see debug.hpp).
#define DEBUG_LEVEL_2
#else
#define DEBUG_LEVEL_0
#endif

#include <windows.h>

#include "dlssnr.hpp"

#include "lastgasp.hpp"

extern "C" __declspec(dllexport) uint32_t __cdecl DLSS5StageProtocolVersion() {
  return 1;
}
extern "C" __declspec(dllexport) uint32_t __cdecl DLSS5GetStagePlan() {
  return renodx::addons::dlss5::stages::Snapshot().packed;
}

// Query/Select are called from the overlay thread. Current is an atomic-only
// query for the bridge worker; a pending choice must not affect a live session.
extern "C" __declspec(dllexport) bool __cdecl DLSS5HookModeControlV1(
    uint32_t operation, uint32_t value, dlss5::hook_mode::Snapshot* result) {
  namespace nr = renodx::addons::dlss5::internal;
  if (!nr::hook_mode_config_ready.load(std::memory_order_acquire)) return false;
  const uint32_t current = dlss5::hook_mode::Effective(nr::hooks_enabled.load(), nr::streamline_hooks_enabled.load());
  return dlss5::hook_mode::Control(operation, value, current, result, [result] {
    uint32_t mode = dlss5::hook_mode::kDefault;
    result->stored = reshade::get_config_value(nullptr, nr::kConfigSection, "EnableHooks", mode) ? 1u : 0u;
    return mode;
  }, [](uint32_t mode) {
    reshade::set_config_value(nullptr, nr::kConfigSection, "EnableHooks", mode);
  });
}

#if defined(RENODX_DLSS5_DEBUG)
extern "C" __declspec(dllexport) constexpr const char* NAME =
    "DLSS 5 Neural Rendering (Debug)";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION =
    "Generic experimental DLSS Neural Rendering post-pass for DX12 games using "
    "NGX or Streamline DLSS (DEBUG BUILD: trace log + crash minidumps - see "
    "RenoDX-DLSS5-debug.log next to the game exe)";
#else
extern "C" __declspec(dllexport) constexpr const char* NAME = "DLSS 5 Neural Rendering";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION =
    "Generic experimental DLSS Neural Rendering post-pass for DX12 games using NGX or Streamline DLSS";
#endif

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    // Before register_addon: the last-gasp reporter covers even a death in
    // the registration path.  The CRT and static initializers are already up.
    renodx::addons::dlss5::lastgasp::Install(
        module, renodx::addons::dlss5::internal::kAddonVersion);
    if (!reshade::register_addon(module)) {
      // No ReShade to register with: the loader unmaps this image next, and
      // the reporter's handlers must not outlive it.
      renodx::addons::dlss5::lastgasp::Exit(false);
      renodx::addons::dlss5::lastgasp::Uninstall();
      return FALSE;
    }
  }

  renodx::addons::dlss5::Use(reason, module, reserved);

  if (reason == DLL_PROCESS_DETACH) {
    reshade::unregister_addon(module);
    // `reserved` is null for FreeLibrary and non-null at process exit.
    // [exit] (lastgasp.hpp): the session ended here, not in a silent death.
    renodx::addons::dlss5::lastgasp::Exit(reserved != nullptr);
    if (reserved == nullptr) renodx::addons::dlss5::lastgasp::Uninstall();
  }
  return TRUE;
}
