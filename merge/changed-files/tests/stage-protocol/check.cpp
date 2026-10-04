#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>
#include <initializer_list>
static HMODULE g_self = nullptr;
static UINT g_pipeline = 0, g_follow = 0;
static int g_hook_point = 2, g_hooks = 1, g_encoding = 0, g_primaries = 0;
static const wchar_t* g_linear_unit = L"0";
static UINT TestProfileInt(LPCWSTR, LPCWSTR key, INT fallback, LPCWSTR) {
    if (wcscmp(key, L"Enabled") == 0 || wcscmp(key, L"Import") == 0) return 1u;
    if (wcscmp(key, L"Pipeline") == 0) return g_pipeline;
    if (wcscmp(key, L"Follow") == 0) return g_follow;
    if (wcscmp(key, L"NRHookPoint") == 0) return static_cast<UINT>(g_hook_point);
    if (wcscmp(key, L"EnableHooks") == 0) return static_cast<UINT>(g_hooks);
    if (wcscmp(key, L"NRSourceEncoding") == 0) return static_cast<UINT>(g_encoding);
    if (wcscmp(key, L"NRSourcePrimaries") == 0) return static_cast<UINT>(g_primaries);
    return static_cast<UINT>(fallback);
}
static DWORD TestProfileString(LPCWSTR, LPCWSTR key, LPCWSTR, LPWSTR out, DWORD count, LPCWSTR) {
    const wchar_t* value = wcscmp(key, L"Source") == 0 ? L"fg-input" : g_linear_unit;
    wcscpy_s(out, count, value);
    return static_cast<DWORD>(wcslen(value));
}
#define GetPrivateProfileIntW TestProfileInt
#define GetPrivateProfileStringW TestProfileString
#include "present-adapter-config.h"
#include "present-compatibility.h"
#include "scope-under-test.inc"
static unsigned g_config_code;
static void PresentCompatibilitySet(unsigned, unsigned code, const char*) {
    g_config_code = code;
}
#include "configuration-under-test.inc"
#undef GetPrivateProfileIntW
#undef GetPrivateProfileStringW
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv) {
    g_pipeline = argc > 1 ? static_cast<UINT>(std::strtoul(argv[1], nullptr, 10)) : 2u;
    g_follow = argc > 2 ? static_cast<UINT>(std::strtoul(argv[2], nullptr, 10)) : 0u;
    CHECK((g_pipeline == 2 || g_pipeline == 3) && g_follow <= 1);
    CHECK(!PresentAdapterStageProtocol());
    CHECK(!PresentAdapterStagePlan(nullptr));
    CHECK(FgInputActive());
    CHECK(PresentAdapterRelay());
    CHECK(PresentAdapterRelayAtEvaluate() == (g_pipeline == 2));
    CHECK(PresentAdapterRelayAtPresent() == (g_pipeline == 3));
    CHECK(PresentAdapterRelayImport() && PresentAdapterRelayThrottle() == 1);
    CHECK(!PresentAdapterConfiguration() && g_config_code == PC_GENERIC_MISSING);
    CHECK(DLSS5BridgeStageProtocolVersion() == 1);
    CHECK(DLSS5BridgeActiveStagePlan() == 0);
    {
        PresentAdapterNrScope scope;
        CHECK(DLSS5BridgeActiveStagePlan() == 0);
    }
    HMODULE consumer = LoadLibraryW(L"renodx-dlss5.addon64");
    CHECK(consumer != nullptr);
    using Set = void (__cdecl*)(uint32_t);
    auto set_plan = reinterpret_cast<Set>(GetProcAddress(consumer, "SetPlan"));
    auto set_version = reinterpret_cast<Set>(GetProcAddress(consumer, "SetVersion"));
    CHECK(set_plan != nullptr && set_version != nullptr);
    CHECK(!PresentAdapterStageProtocol());
    CHECK(PresentAdapterRelay());
    CHECK(PresentAdapterConfiguration());
    g_hook_point = 1;
    CHECK(!PresentAdapterConfiguration() && g_config_code == PC_HOOK_POINT_UNAVAILABLE);
    set_version(1);
    CHECK(PresentAdapterStageProtocol());
    // The relay settings follow Pipeline/Import alone; the plan decides whether frames reach it.
    CHECK(PresentAdapterRelay() && PresentAdapterRelayAtEvaluate() == (g_pipeline == 2) &&
          PresentAdapterRelayAtPresent() == (g_pipeline == 3));
    CHECK(PresentAdapterRelayImport() && PresentAdapterRelayThrottle() == 1);
    uint32_t result = 0;
    unsigned valid_count = 0;
    for (uint32_t r = 0; r <= 4; ++r) {
        for (uint32_t u = 0; u <= 4; ++u) {
            for (uint32_t p = 0; p <= 4; ++p) {
                const uint32_t packed = r | (u << 3) | (p << 6);
                set_plan(packed);
                CHECK(PresentAdapterStagePlan(&result) && result == packed);
                for (int point : {0, 1, 2, -1}) {
                    g_hook_point = point;
                    CHECK(PresentAdapterConfiguration() && g_config_code == PC_OK);
                    CHECK(FgInputActive() == (p != 0));
                }
                {
                    PresentAdapterNrScope outer;
                    CHECK(DLSS5BridgeActiveStagePlan() == (packed | 0x80000000u));
                    set_plan(0);
                    CHECK(DLSS5BridgeActiveStagePlan() == (packed | 0x80000000u));
                    bool isolated = false;
                    std::thread other([&] { isolated = DLSS5BridgeActiveStagePlan() == 0; });
                    other.join();
                    CHECK(isolated);
                    {
                        PresentAdapterNrScope inner;
                        CHECK(DLSS5BridgeActiveStagePlan() == 0x80000000u);
                    }
                    CHECK(DLSS5BridgeActiveStagePlan() == (packed | 0x80000000u));
                }
                CHECK(DLSS5BridgeActiveStagePlan() == 0 && !g_present_nr_scope);
                ++valid_count;
            }
        }
    }
    CHECK(valid_count == 125);
    g_hook_point = 2;
    auto invalid_plan = [&](uint32_t invalid) {
        set_plan(invalid);
        result = 0x12345678u;
        CHECK(PresentAdapterStageProtocol());
        CHECK(!PresentAdapterStagePlan(&result) && result == 0x12345678u);
        CHECK(!FgInputActive());
        CHECK(!PresentAdapterConfiguration() && g_config_code == PC_HOOK_POINT_UNAVAILABLE);
        CHECK(PresentAdapterRelay() && PresentAdapterRelayImport() && PresentAdapterRelayThrottle() == 1);
        {
            PresentAdapterNrScope outer;
            CHECK(DLSS5BridgeActiveStagePlan() == 0x80000000u);
            set_plan(64);
            try {
                PresentAdapterNrScope inner;
                CHECK(DLSS5BridgeActiveStagePlan() == 0x80000040u);
                throw 1;
            } catch (int) {}
            CHECK(DLSS5BridgeActiveStagePlan() == 0x80000000u);
        }
        CHECK(DLSS5BridgeActiveStagePlan() == 0 && !g_present_nr_scope);
        return 0;
    };
    unsigned invalid_count = 0;
    for (uint32_t packed = 0; packed < 512; ++packed) {
        if ((packed & 7u) <= 4 && ((packed >> 3) & 7u) <= 4 && ((packed >> 6) & 7u) <= 4) continue;
        CHECK(invalid_plan(packed) == 0);
        ++invalid_count;
    }
    for (uint32_t invalid : {0x200u, 0x240u, 0x80000000u, 0xffffffffu}) {
        CHECK(invalid_plan(invalid) == 0);
        ++invalid_count;
    }
    CHECK(invalid_count == 391);
    set_plan(64);
    CHECK(FgInputActive() && PresentAdapterConfiguration());
    for (int hooks : {0, 2}) {
        g_hooks = hooks;
        CHECK(!PresentAdapterConfiguration() && g_config_code == PC_HOOKS_DISABLED);
    }
    g_hooks = 1;
    g_encoding = 1;
    CHECK(!PresentAdapterConfiguration() && g_config_code == PC_SOURCE_OVERRIDE);
    g_encoding = 0;
    g_primaries = 1;
    CHECK(!PresentAdapterConfiguration() && g_config_code == PC_SOURCE_OVERRIDE);
    g_primaries = 0;
    g_linear_unit = L"0.5";
    CHECK(!PresentAdapterConfiguration() && g_config_code == PC_SOURCE_OVERRIDE);
    g_linear_unit = L"0";
    CHECK(PresentAdapterConfiguration());
    set_plan(0);
    CHECK(PresentAdapterStagePlan(nullptr) && !FgInputActive());
    CHECK(PresentAdapterRelay());
    std::printf("PASS: P%u Follow=%u; late discovery, version rejection, %u plans, %u invalid plans fail closed, relay gates follow Pipeline/Import, zero-pass bypass, nested/thread-local snapshots, hook/source validation\n",
                g_pipeline, g_follow, valid_count, invalid_count);
    return 0;
}
