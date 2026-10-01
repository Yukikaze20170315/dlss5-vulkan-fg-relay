// Compile the production policy and integration together. No DllMain, GPU or game.
#include "dlss5-bridge.cpp"

static unsigned checks, failures;
static void Check(bool value, const char *description)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", description); }
}

int main()
{
    InitializeCriticalSection(&g_log_cs);
    strcpy_s(g_log_path, "compatibility.log");
    g_self = GetModuleHandleW(nullptr);
    wchar_t ini[1024] = {};
    PresentAdapterPath(ini, _countof(ini), L"ReShade.ini");
    auto set = [&](const wchar_t *key, const wchar_t *value) {
        Check(WritePrivateProfileStringW(L"RenoDX.DLSS5", key, value, ini) != 0, "isolated test configuration written");
    };
    set(L"EnableHooks", L"1"); set(L"NRHookPoint", L"2");
    set(L"NRSourceEncoding", L"0"); set(L"NRSourcePrimaries", L"0"); set(L"NRLinearUnitNits", L"0");
    Check(!PresentAdapterConsumer(), "missing Generic is diagnosed");
    Check(PresentCompatibilitySnapshot().code[0] == PC_GENERIC_MISSING, "missing is distinguished from unknown build");

    const HMODULE fixture = LoadLibraryW(L".\\renodx-dlss5.addon64");
    Check(fixture != nullptr, "unknown fixture loaded");
    Check(PresentAdapterConsumer(), "unknown build allowed without matching SHA/version");
    PresentCompatibilityPoll();
    Check(PresentCompatibilitySnapshot().unknown_build, "unknown identity retained only as diagnostic");
    Check(!g_present_adapter.refused, "unknown build is not permanently refused");

    set(L"NRLinearUnitNits", L"0.5");
    Check(!PresentAdapterConsumer(), "fractional unit override diagnosed");
    Check(PresentCompatibilitySnapshot().code[0] == PC_SOURCE_OVERRIDE, "specific configuration error reported");
    set(L"NRLinearUnitNits", L"0");
    Check(PresentAdapterConsumer(), "restoring configuration recovers without restart");
    Check(PresentCompatibilitySnapshot().code[0] == PC_OK, "configuration status clears");
    set(L"NRHookPoint", nullptr);
    Check(!PresentAdapterConsumer(), "missing Follow configuration is explicit, not version rejected");
    set(L"NRHookPoint", L"2");
    Check(PresentAdapterConsumer(), "hook-point configuration can recover");

    PresentCompatibilityFrame(true, 0, 1, 1);
    Check(PresentCompatibilitySnapshot().code[1] == PC_OK, "NR off/warmup does not become incompatibility");
    PresentCompatibilityFrame(true, 2, 2, 1);
    Check(PresentCompatibilitySnapshot().code[1] == PC_CARRIER_MISMATCH, "actual carrier violation reported");
    PresentCompatibilityFrame(false, 2, 1, 1);
    Check(PresentCompatibilitySnapshot().code[1] == PC_PROCESSING_FAILED, "processing failure not blamed on unknown version");
    PresentCompatibilityFrame(true, 2, 1, 1);
    Check(PresentCompatibilitySnapshot().code[1] == PC_OK, "successful transport clears runtime error");
    Check(!g_present_adapter.refused, "all recoverable paths preserve retry");
    if (fixture != nullptr) FreeLibrary(fixture);
    DeleteCriticalSection(&g_log_cs);
    std::printf("%s compatibility: checks=%u failures=%u\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
