#pragma once

// Build identity is diagnostic metadata, never permission to run. Resource and
// call contracts remain checked where they are actually consumed.
enum PresentCompatibilityCode : unsigned {
    PC_OK = 0,
    PC_GENERIC_MISSING = 1,
    PC_HOOKS_DISABLED = 2,
    PC_HOOK_POINT_UNAVAILABLE = 3,
    PC_SOURCE_OVERRIDE = 4,
    PC_NR_ENTRY_UNAVAILABLE = 5,
    PC_CARRIER_MISMATCH = 6,
    PC_PROCESSING_FAILED = 7,
    PC_SR_ENTRY_UNAVAILABLE = 8,
    PC_HOOK_INSTALL_FAILED = 9
};

static PresentCompatibilityCode PresentCompatibilityConfigCode(bool loaded, int hooks, int point,
                                                               bool follow, int encoding, int primaries,
                                                               double linear_unit)
{
    if (!loaded) return PC_GENERIC_MISSING;
    if (hooks != 1) return PC_HOOKS_DISABLED;
    if ((follow && point < 0) || (point != 0 && point != 2)) return PC_HOOK_POINT_UNAVAILABLE;
    if (encoding != 0 || primaries != 0 || linear_unit != 0.0) return PC_SOURCE_OVERRIDE;
    return PC_OK;
}

static const char *PresentCompatibilityConfigReason(PresentCompatibilityCode code)
{
    switch (code) {
    case PC_GENERIC_MISSING: return "Generic add-on is not loaded (renodx-dlss5.addon64).";
    case PC_HOOKS_DISABLED: return "The carrier requires EnableHooks=1; processing resumes after the setting is restored.";
    case PC_HOOK_POINT_UNAVAILABLE: return "The configured hook point cannot serve this carrier; use Upscaled/Present, or a Generic with a supported hook-point configuration.";
    case PC_SOURCE_OVERRIDE: return "Source encoding, primaries and linear-unit overrides must be Auto for this carrier; restore Auto to resume.";
    default: return "Configuration available.";
    }
}
