// SR carrier retry. Production carrier search, MinHook install, FG-input retry
// step and relay arm guard, against fixture DLLs in this directory (two SR
// modules visible at once, as when NGX briefly maps the game's nvngx_dlss.dll
// next to the driver's DLSS model). No DllMain, game, GPU or NGX.
#include "dlss5-bridge.cpp"
#include <string>

static unsigned checks, failures;
static void Check(bool value, const char *description)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", description); }
}

static int DummyDetour() { return 7; }

static bool LogContains(const char *text)
{
    FILE *file = nullptr;
    if (fopen_s(&file, g_log_path, "rb") != 0 || file == nullptr) return false;
    std::string all;
    char buffer[4096];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) all.append(buffer, n);
    fclose(file);
    return all.find(text) != std::string::npos;
}

static void CheckPolicy()
{
    PresentCarrierRetry r = {};
    Check(PresentCarrierRetryDue(r, 0), "first attempt is immediate");
    Check(PresentCarrierRetryAfterFailure(r, PCR_TRANSIENT, 100), "transient failure allows a retry");
    Check(!PresentCarrierRetryDue(r, 100 + kPresentCarrierRetryMs - 1), "retry waits the interval");
    Check(PresentCarrierRetryDue(r, 100 + kPresentCarrierRetryMs), "retry is due after the interval");
    unsigned allowed = 1;
    while (PresentCarrierRetryAfterFailure(r, PCR_TRANSIENT, 0)) ++allowed;
    Check(r.attempts == kPresentCarrierRetryLimit, "attempts stop exactly at the limit");
    Check(allowed == kPresentCarrierRetryLimit - 1, "limit - 1 retries follow the first attempt");
    PresentCarrierRetry f = {};
    Check(!PresentCarrierRetryAfterFailure(f, PCR_FATAL, 0), "fatal failure is not retried");
    Check(f.attempts == 1, "fatal failure is counted once");
    Check(PresentCarrierHookStatusResult(MH_ERROR_MEMORY_ALLOC) == PCR_TRANSIENT, "MinHook status 9 is transient");
    Check(PresentCarrierHookStatusResult(MH_ERROR_ALREADY_CREATED) == PCR_FATAL, "an existing hook is fatal");
    Check(PresentCarrierHookStatusResult(MH_ERROR_UNSUPPORTED_FUNCTION) == PCR_FATAL, "an unhookable function is fatal");
    Check(PresentCarrierHookStatusResult(MH_ERROR_NOT_EXECUTABLE) == PCR_FATAL, "a non-executable target is fatal");
}

static void ResetFgInput()
{
    g_fg_input = {};
    g_present_adapter.remaining = 1;
    g_fg_input.carrier_pending = true;
}

static void CheckRelayGuard()
{
    FgrState &f = g_fgr;
    f.ready = true; f.cs_ready = true; f.fell_back = false;
    InterlockedExchange(&f.releasing, 0);
    g_present_adapter.run_nr = true; g_present_adapter.remaining = 1; g_present_adapter.refused = false;
    g_fg_input = {}; g_vkm.refused = false; g_vkm.stopping = false;
    VkmRes hud = {}, back = {};
    FgGuideContract guides = {};
    const LONG guard = FgrCountRead(f.arm_guard), badres = FgrCountRead(f.arm_badres);
    g_fg_input.carrier_pending = true;
    Check(!FgRelayArmCopy(nullptr, "test", hud, back, guides), "no relay arm while the carrier is pending");
    Check(FgrCountRead(f.arm_guard) == guard + 1, "pending carrier counted as an arm guard");
    Check(FgrCountRead(f.arm_badres) == badres, "pending carrier stops before resource checks");
    g_fg_input.carrier_pending = false;
    Check(!FgRelayArmCopy(nullptr, "test", hud, back, guides), "fixture has no resources to arm");
    Check(FgrCountRead(f.arm_badres) == badres + 1, "installed carrier reaches the following checks");
    f.ready = false; f.cs_ready = false;
    g_fg_input = {};
}

// An unarmed evaluate ends inside the relay, so while the carrier is pending the
// feed must keep the frame and reach its retry (a game session stalled here).
static void CheckRelayYieldsWhilePending()
{
    Check(PresentAdapterRelay(), "fixture configuration selects the relay (Pipeline=2)");
    g_fg_input = {};
    g_fg_input.carrier_pending = true;
    Check(!FgInputRelayTakesFrame(), "relay does not take frames while the carrier is pending");
    g_fg_input.carrier_pending = false;
    Check(FgInputRelayTakesFrame(), "relay takes frames once the carrier is installed");
    g_fg_input = {};
}

static int CheckCrowdedCarrier()
{
    InitializeCriticalSection(&g_log_cs);
    strcpy_s(g_log_path, "carrier-crowded.log");
    Check(MH_Initialize() == MH_OK, "MinHook initialised before address pressure");
    const HMODULE sr = LoadLibraryW(L".\\sr-low.dll");
    Check(sr != nullptr && reinterpret_cast<uintptr_t>(sr) == 0x128AD0000ULL, "SR fixture loaded at its fixed low address");
    void *entry = sr != nullptr ? reinterpret_cast<void *>(GetProcAddress(sr, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
    if (entry == nullptr) return 1;
    const uintptr_t origin = reinterpret_cast<uintptr_t>(entry);
    const uintptr_t low = origin - 0x50000000ULL, high = origin + 0x50000000ULL;
    std::vector<void *> reserved;
    bool complete = true;
    for (uintptr_t pos = low; pos < high;) {
        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(reinterpret_cast<void *>(pos), &region, sizeof(region)) == 0) { complete = false; break; }
        const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (region.State == MEM_FREE) {
            const uintptr_t begin = (pos + 0xFFFFULL) & ~uintptr_t(0xFFFFULL);
            const uintptr_t stop = end < high ? end : high;
            if (begin + 0x1000 <= stop) {
                void *block = VirtualAlloc(reinterpret_cast<void *>(begin), stop - begin, MEM_RESERVE, PAGE_NOACCESS);
                if (block != nullptr) reserved.push_back(block);
                else complete = false;
            }
        }
        pos = end;
    }
    Check(complete && !reserved.empty(), "all aligned inner-window free regions reserved without committing memory");
    using Evaluate = int (*)(void *, void *, void *, void *);
    const auto evaluate = reinterpret_cast<Evaluate>(entry);
    Check(evaluate(sr, sr, nullptr, sr) == 3, "external SR baseline accepts all four arguments");
    void *ordinary = nullptr;
    Check(MH_CreateHook(entry, reinterpret_cast<void *>(&DummyDetour), &ordinary) == MH_ERROR_MEMORY_ALLOC && ordinary == nullptr,
          "ordinary hooks still refuse the exhausted inner window");
    const auto result = PresentAdapterCarrier();
    std::printf("crowded carrier: status=%u entry=%p trampoline=%p\n", static_cast<unsigned>(result), entry, g_present_sr_original);
    Check(result == PCR_READY, "production carrier installs with the inner window exhausted");
    if (result == PCR_READY) {
        const uintptr_t forward = reinterpret_cast<uintptr_t>(g_present_sr_original);
        const uintptr_t distance = origin > forward ? origin - forward : forward - origin;
        Check(distance > 0x40000000ULL && distance < 0x80000000ULL, "carrier trampoline uses the outer rel32 window");
        Check(evaluate(sr, nullptr, sr, sr) == 3, "external SR is unchanged through the production assembly gate");
        Check(evaluate(nullptr, nullptr, nullptr, nullptr) == 0, "external null arguments remain unchanged");
        Check(MH_DisableHook(entry) == MH_OK && evaluate(sr, sr, sr, sr) == 4, "carrier disables without changing external SR");
        Check(MH_EnableHook(entry) == MH_OK && evaluate(sr, sr, sr, sr) == 4, "scoped carrier re-enables with the same prologue");
        Check(MH_RemoveHook(entry) == MH_OK, "scoped carrier removes cleanly");
        DWORD protect = 0;
        Check(VirtualProtect(entry, 5, PAGE_EXECUTE_READWRITE, &protect) != FALSE, "fixture prologue can be changed for rejection cases");
        const unsigned char original[5] = {0x48, 0x89, 0x5C, 0x24, 0x08};
        const unsigned char jump[5] = {0xE9, 0, 0, 0, 0};
        memcpy(entry, jump, sizeof(jump));
        ordinary = nullptr;
        Check(MH_CreateHookPrologueExtended(entry, reinterpret_cast<void *>(&DummyDetour), &ordinary) == MH_ERROR_MEMORY_ALLOC && ordinary == nullptr,
              "extended create refuses an existing jump rather than hiding its original prologue");
        Check(memcmp(entry, jump, sizeof(jump)) == 0, "rejected create leaves the target unchanged");
        memcpy(entry, original, sizeof(original));
        Check(MH_CreateHookPrologueExtended(entry, reinterpret_cast<void *>(&DummyDetour), &ordinary) == MH_OK,
              "extended create accepts the verified live prologue");
        memcpy(entry, jump, sizeof(jump));
        Check(MH_EnableHook(entry) == MH_ERROR_UNSUPPORTED_FUNCTION, "enable rechecks a prologue changed after create");
        Check(memcmp(entry, jump, sizeof(jump)) == 0, "rejected enable leaves the intervening jump unchanged");
        Check(MH_RemoveHook(entry) == MH_OK, "rejected disabled hook removes without patching target");
        memcpy(entry, original, sizeof(original));
        DWORD ignored = 0;
        VirtualProtect(entry, 5, protect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), entry, 5);
    }
    for (void *block : reserved) VirtualFree(block, 0, MEM_RELEASE);
    DeleteCriticalSection(&g_log_cs);
    std::printf("%s crowded carrier: checks=%u failures=%u\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--crowded") == 0) return CheckCrowdedCarrier();
    InitializeCriticalSection(&g_log_cs);
    strcpy_s(g_log_path, "carrier.log");
    g_self = GetModuleHandleW(nullptr);
    {
        wchar_t ini[1024] = {};
        PresentAdapterPath(ini, _countof(ini), L"vk-present-adapter.ini");
        FILE *file = nullptr;
        if (_wfopen_s(&file, ini, L"wb") == 0 && file != nullptr) {
            fputs("[Adapter]\r\nEnabled=1\r\nSource=fg-input\r\nPipeline=2\r\n", file);
            fclose(file);
        }
    }
    Check(MH_Initialize() == MH_OK, "MinHook initialised");
    CheckPolicy();
    CheckRelayGuard();
    CheckRelayYieldsWhilePending();

    Check(PresentAdapterCarrier() == PCR_TRANSIENT, "no SR module yet is transient");
    Check(PresentCompatibilitySnapshot().code[1] == PC_SR_ENTRY_UNAVAILABLE, "missing SR module is reported");

    const HMODULE rr = LoadLibraryW(L".\\rr-fixture.dll");
    Check(rr != nullptr, "Ray Reconstruction fixture loaded");
    Check(PresentAdapterCarrier() == PCR_TRANSIENT, "Ray Reconstruction is not an SR carrier");

    const HMODULE a = LoadLibraryW(L".\\sr-a.dll");
    const HMODULE b = LoadLibraryW(L".\\sr-b.dll");
    Check(a != nullptr && b != nullptr && a != b, "two distinct SR modules loaded");
    void *entry = a != nullptr ? reinterpret_cast<void *>(GetProcAddress(a, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
    Check(entry != nullptr, "SR fixture export resolved");

    Check(PresentAdapterCarrier() == PCR_TRANSIENT, "two SR modules are transient, not fatal");
    Check(!g_present_adapter.sr_hook, "ambiguous carrier installs nothing");
    Check(LogContains("multiple SR modules found"), "ambiguity is logged");

    // FG-input step while ambiguous: one attempt per interval, session kept, refusal at the limit.
    ResetFgInput();
    FgGuideContract guides = {};
    Check(!FgInputCarrierStep(guides), "ambiguous step does not become ready");
    Check(g_fg_input.carrier.attempts == 1 && g_fg_input.carrier_pending && !g_fg_input.refused,
          "first ambiguous attempt keeps the session pending");
    Check(!FgInputCarrierStep(guides) && g_fg_input.carrier.attempts == 1, "no second attempt inside the interval");
    unsigned steps = 1;
    while (!g_fg_input.refused && steps < 2 * kPresentCarrierRetryLimit) {
        g_fg_input.carrier.next_at = 0;
        FgInputCarrierStep(guides);
        ++steps;
    }
    Check(g_fg_input.refused, "persistent ambiguity is refused after the limit");
    Check(steps == kPresentCarrierRetryLimit, "refusal happens on the last allowed attempt");
    Check(g_present_adapter.remaining == 0, "refusal stops the request as before");
    Check(!g_present_adapter.sr_hook, "nothing was hooked while ambiguous");
    Check(LogContains("attempt 1 of 30"), "retry attempts are logged");

    // A failure other than a transient one refuses at once.
    Check(b != nullptr && FreeLibrary(b), "second SR module unloaded");
    Check(MH_CreateHook(entry, reinterpret_cast<void *>(&DummyDetour), nullptr) == MH_OK, "foreign hook placed first");
    ResetFgInput();
    Check(!FgInputCarrierStep(guides) && g_fg_input.refused && g_fg_input.carrier.attempts == 1,
          "fatal hook status refuses on the first attempt");
    Check(MH_RemoveHook(entry) == MH_OK, "foreign hook removed");

    // The ambiguity has cleared: the next attempt installs the carrier.
    PresentCompatibilityIssue(PC_SR_ENTRY_UNAVAILABLE, "A unique DLSS SR carrier evaluate export could not be found.");
    Check(PresentAdapterCarrier() == PCR_READY, "unique SR module installs the carrier");
    Check(g_present_adapter.sr_hook && g_present_sr_original != nullptr, "carrier hook and trampoline exist");
    Check(entry != nullptr && *static_cast<const unsigned char *>(entry) == 0xE9, "SR entry now jumps to the carrier");
    Check(PresentCompatibilitySnapshot().code[1] == PC_OK, "carrier success clears the runtime diagnostic");
    Check(LogContains("private SR carrier copy ready on sr-a.dll"), "installed module is named");
    Check(PresentAdapterCarrier() == PCR_READY, "carrier install is idempotent");

    g_fg_input = {};
    FgInputOnSessionRelease();
    Check(!g_fg_input.carrier_pending && g_fg_input.carrier.attempts == 0, "session release clears the pending carrier");

    if (rr != nullptr) FreeLibrary(rr);
    DeleteCriticalSection(&g_log_cs);
    std::printf("%s carrier: checks=%u failures=%u\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
