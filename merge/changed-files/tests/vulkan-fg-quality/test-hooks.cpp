// Executes the production NGX wrappers against CPU stubs. No DllMain, game,
// Vulkan/D3D device, NR library, or installed hook is used by this executable.
#include "dlss5-bridge.cpp"

namespace lifecycle_test {
static unsigned checks, failures;
static unsigned creates, releases, evaluates, legacy_creates;
static NVSDK_NGX_Handle *next_handle;
static NVSDK_NGX_Result next_result = NGX_SUCCESS;
static HANDLE release_entered, ngx_held;
static volatile LONG shutdown_worker_stage;
static unsigned gpu_event_destroys;
static void Check(bool ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", why); }
}
static void *Ptr(std::uintptr_t value) { return reinterpret_cast<void *>(value); }
class TransitionParams final : public SynthParams {
public:
    using SynthParams::Get;
    unsigned reset = 1, writes = 0;
    void *queue = Ptr(0x770000);
    NVSDK_NGX_Result Get(const char *key, unsigned *out) const override {
        if (strcmp(key, "DLSSG.Reset") == 0) *out = reset;
        else if (strcmp(key, "DLSSG.MultiFrameIndex") == 0) *out = 1;
        else if (strcmp(key, "DLSSG.MultiFrameCount") == 0) *out = 3;
        else return kSynthNoSuchKey;
        return NGX_SUCCESS;
    }
    NVSDK_NGX_Result Get(const char *key, void **out) const override {
        if (strcmp(key, "DLSSG.CmdQueue") != 0) return kSynthNoSuchKey;
        *out = queue; return NGX_SUCCESS;
    }
    void Set(const char *, unsigned) override { ++writes; }
};
static void CheckTransitionObservation()
{
    TransitionParams p;
    Check(!FgtTransitionBegin(&p).active, "disabled processing does not collect transition metadata");
    g_cfg.stage = 3; // Only call CPU observation helpers, never the processing entry.
    const auto first = FgtTransitionBegin(&p);
    Check(first.active && first.index == 1 && first.count == 3 && first.reset == 1 && first.queue == p.queue,
        "transition probe snapshots input scalars without changing them");
    Check(first.known == 7 && first.real == 0 && first.interp == 0,
        "absent scalars remain distinguishable from zero and absent images are safe");
    for (unsigned i = 1; i < 12; ++i) Check(FgtTransitionBegin(&p).active, "transition record window is bounded to 12 calls");
    Check(!FgtTransitionBegin(&p).active, "unchanged state stops logging after window");
    p.queue = Ptr(0x880000);
    Check(FgtTransitionBegin(&p).active, "queue transition starts another bounded window");
    FgtTransitionEnd(first, &p, NGX_SUCCESS, false, 10, 10);
    Check(p.reset == 1 && p.writes == 0, "post-evaluate observation leaves parameter block untouched");
    g_cfg.stage = 0;
}
static NVSDK_NGX_Result CreateLeaf(void *, void *, int, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **out)
{
    ++creates;
    if (next_result == NGX_SUCCESS && out) *out = next_handle;
    return next_result;
}
static NVSDK_NGX_Result CreateLayer(void *cmd, int feature, NVSDK_NGX_Parameter *p, NVSDK_NGX_Handle **out)
{
    ++legacy_creates;
    return Detour_VkCreate1_1(Ptr(0x220000), cmd, feature, p, out);
}
static NVSDK_NGX_Result ReleaseLeaf(NVSDK_NGX_Handle *)
{
    ++releases;
    if (release_entered != nullptr) SetEvent(release_entered);
    return next_result;
}
static NVSDK_NGX_Result ReleaseLayer(NVSDK_NGX_Handle *handle) { return Detour_VkRelease_1(handle); }
static NVSDK_NGX_Result EvaluateLeaf(void *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, void *)
{
    ++evaluates;
    return next_result;
}
static NVSDK_NGX_Result EvaluateLayer(void *cmd, const NVSDK_NGX_Handle *handle,
                                     const NVSDK_NGX_Parameter *p, void *cb)
{
    return Detour_VkEvaluate_C_1(cmd, handle, p, cb);
}
static DWORD WINAPI HoldNgxUntilRelease(LPVOID)
{
    EnterCriticalSection(&g_ngx_cs);
    SetEvent(ngx_held);
    const DWORD result = WaitForSingleObject(release_entered, 1000);
    LeaveCriticalSection(&g_ngx_cs);
    return result;
}
static DWORD WINAPI FinishArmBeforeStop(LPVOID)
{
    Sleep(30);
    if (g_vkm.stop != 0) { InterlockedExchange(&shutdown_worker_stage, -1); return 0; }
    InterlockedExchange(&g_fgr.worker_seq, g_fgr.arm_seq);
    InterlockedExchange(&shutdown_worker_stage, 1);
    SetEvent(g_vkm.idle);
    if (WaitForSingleObject(g_vkm.wake, 2000) == WAIT_OBJECT_0 && g_vkm.stop != 0)
        InterlockedExchange(&shutdown_worker_stage, 2);
    return 0;
}
static uint32_t EventSetStub(SVkDevice, SVkHandle) { return 0; }
static void EventDestroyStub(SVkDevice, SVkHandle, const void *) { ++gpu_event_destroys; }
static void CheckEmptyRuntimeRestart(const char *scenario)
{
    std::printf("scenario: %s\n", scenario);
    Check(g_svk.dev == nullptr && g_vkm.worker == nullptr, "empty runtime fixture has no private device or worker");
    for (unsigned cycle = 0; cycle != 3; ++cycle) {
        Check(VkmShutdown(), "empty runtime shutdown succeeds before NR ever starts");
        SynthVkShutdownForDevice(); // Production no-private-device teardown path.
        // OnVkmInitRuntime lifts this mirror guard; it does not modify relay state.
        g_vkm.stopping = false;
    }
    // Model the successful first FgRelayEnsure: no GPU API is called by the
    // following probe because run_nr=false makes the ordinary request gate stop
    // it. Reaching that counted gate proves shutdown did not latch releasing.
    g_fgr.ready = true;
    g_fgr.tried = true;
    g_present_adapter.run_nr = false;
    const LONG before = g_fgr.arm_noreq;
    const VkmRes image = {};
    const FgGuideContract guides = {};
    Check(!FgRelayArmCopy(nullptr, "empty-runtime-regression", image, image, guides),
          "request gate safely prevents GPU work in the CPU fixture");
    Check(g_fgr.arm_noreq == before + 1,
          "after empty runtime recreation, a new relay arm passes the releasing gate");
    g_fgr.ready = false;
    g_fgr.tried = false;
}
}

int main()
{
    using namespace lifecycle_test;
    InitializeCriticalSection(&g_log_cs);
    InitializeCriticalSection(&g_hook_cs);
    InitializeCriticalSection(&g_bridge_cs);
    InitializeCriticalSection(&g_ngx_cs);
    g_ngx_cs_ready = true;
    g_self = GetModuleHandleW(nullptr);
    strcpy_s(g_log_path, "hooks.log");
    g_cfg.stage = 0; // Event/lifecycle policy on; no GPU processing is allowed.
    Check(PresentAdapterRelay() && g_cfg.stage < 2, "CPU fixture tests relay lifecycle without GPU processing");
    FgRelayInit();
    CheckTransitionObservation();
    CheckEmptyRuntimeRestart("cold startup with repeated runtime replacement");
    g_layer_count = 2;
    g_layer[0].mod = reinterpret_cast<HMODULE>(Ptr(0x300000));
    g_layer[1].mod = reinterpret_cast<HMODULE>(Ptr(0x400000));
    g_layer[0].vk_create.original = reinterpret_cast<void *>(&CreateLayer);
    g_layer[1].vk_create1.original = reinterpret_cast<void *>(&CreateLeaf);
    g_layer[0].vk_release.original = reinterpret_cast<void *>(&ReleaseLayer);
    g_layer[1].vk_release.original = reinterpret_cast<void *>(&ReleaseLeaf);
    g_layer[0].release.original = reinterpret_cast<void *>(&ReleaseLeaf);
    g_layer[0].vk_eval.original = reinterpret_cast<void *>(&EvaluateLayer);
    g_layer[1].vk_eval_c.original = reinterpret_cast<void *>(&EvaluateLeaf);
    SynthParams params;
    NVSDK_NGX_Handle handles[96] = {};
    std::uint64_t prior_generation = 0;
    for (unsigned i = 0; i != _countof(handles); ++i)
    {
        next_handle = &handles[i];
        NVSDK_NGX_Handle *out = nullptr;
        Check(Detour_VkCreate_0(Ptr(0x120000), 11, &params, &out) == NGX_SUCCESS,
              "production Create -> nested Create1 returns success");
        const auto feature = g_feature_registry.Find(FeatureApi::vulkan, out);
        Check(feature.id == 11 && feature.generation == prior_generation + 1,
              "only outermost Create registers a lifetime");
        prior_generation = feature.generation;
        Check(FgInputIsFgHandle(out) && IsOtherFeature(FeatureApi::vulkan, out),
              "FG beyond old 8/32 capacities routes to FG branch");
        Check(Detour_VkEvaluate_0(Ptr(0x120000), out, &params, nullptr) == NGX_SUCCESS,
              "production Evaluate -> nested Evaluate_C forwards");
    }
    Check(g_feature_registry.Size(FeatureApi::vulkan) == _countof(handles), "96 simultaneous FG features tracked");
    Check(creates == _countof(handles) && evaluates == _countof(handles), "each original create/evaluate executes once");
    next_result = -7;
    Check(Detour_VkRelease_0(&handles[0]) == -7 && FgInputIsFgHandle(&handles[0]),
          "failed nested Release preserves live FG");
    NVSDK_NGX_Handle *failed = &handles[1];
    Check(Detour_VkCreate_0(Ptr(0x120000), 11, &params, &failed) == -7 &&
          g_feature_registry.Find(FeatureApi::vulkan, failed).generation == 2,
          "failed Create does not register stale output pointer");
    next_result = NGX_SUCCESS;
    for (auto &handle : handles)
        Check(Detour_VkRelease_0(&handle) == NGX_SUCCESS && !FgInputIsFgHandle(&handle),
              "successful multi-layer Release removes each FG");
    Check(g_feature_registry.Size(FeatureApi::vulkan) == 0, "released hook lifetimes drained");
    Check(releases == _countof(handles) + 1, "nested Release original executes once");

    // Exercise CreateFeature1 directly and the device-aware Create -> Create1
    // forwarding path, not only the legacy wrapper.
    next_handle = &handles[0];
    NVSDK_NGX_Handle *out = nullptr;
    Check(Detour_VkCreate1_1(Ptr(0x220000), Ptr(0x120000), 11, &params, &out) == NGX_SUCCESS,
          "direct CreateFeature1 registers");
    Check(g_feature_registry.Find(FeatureApi::vulkan, out).owner == g_layer[1].mod,
          "lifetime belongs to outermost module");
    g_vkm_cmd_devices[0x120000] = 0x220000;
    g_layer[0].vk_create1.target = reinterpret_cast<void *>(&CreateLeaf);
    g_layer[0].vk_create1.original = reinterpret_cast<void *>(&CreateLeaf);
    const unsigned legacy_before = legacy_creates;
    Check(Detour_VkCreate_0(Ptr(0x120000), 11, &params, &out) == NGX_SUCCESS && legacy_creates == legacy_before,
          "device-aware FG Create forwards through Create1");

    FeatureNoteCreated(FeatureApi::d3d11, g_layer[0].create, 1, NGX_SUCCESS, &out);
    Check(!IsOtherFeature(FeatureApi::d3d11, out) && IsOtherFeature(FeatureApi::vulkan, out),
          "D3D11 SR at same address is not classified as Vulkan FG");
    Check(Detour_Release_0(out) == NGX_SUCCESS && FgInputIsFgHandle(out),
          "D3D11 Release does not erase Vulkan record");
    Check(Detour_VkRelease_0(out) == NGX_SUCCESS && !FgInputIsFgHandle(out), "Vulkan Release clears remaining lifetime");
    Check(g_nest == 0 && g_in_flight_trampoline_calls == 0, "nest and trampoline guards unwind");

    // A driver release may wait for the NR worker. Holding g_ngx_cs across that
    // call would deadlock: this fixture's worker releases the lock only after
    // the original release is allowed to run. Timeout bounds a regressed test.
    release_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ngx_held = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE holder = CreateThread(nullptr, 0, HoldNgxUntilRelease, nullptr, 0, nullptr);
    Check(holder != nullptr && WaitForSingleObject(ngx_held, 1000) == WAIT_OBJECT_0, "dependency fixture owns NGX lock");
    Check(Detour_VkRelease_0(out) == NGX_SUCCESS, "original release runs while another thread owns NGX lock");
    WaitForSingleObject(holder, 2000);
    DWORD holder_result = WAIT_FAILED;
    GetExitCodeThread(holder, &holder_result);
    Check(holder_result == WAIT_OBJECT_0, "release forwarding cannot wait on its NR dependency's lock");
    CloseHandle(holder); CloseHandle(ngx_held); CloseHandle(release_entered);
    ngx_held = release_entered = nullptr;

    // Teardown must consume a published arm before stopping the worker, and
    // must not destroy a VkEvent until device completion is proven separately.
    FgRelayInit();
    g_fgr.ready = true;
    g_fgr.arm_seq = 1; g_fgr.worker_seq = 0;
    g_svk.dev = reinterpret_cast<SVkDevice>(Ptr(0x880000));
    g_fgr.dev = g_svk.dev;
    g_svk.EvSet = EventSetStub; g_svk.EvDestroy = EventDestroyStub;
    g_vkm.ev_in[0] = 1; g_vkm.ev_in[1] = 2;
    g_vkm.ev_out[0] = 3; g_vkm.ev_out[1] = 4;
    g_vkm.idle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_vkm.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_vkm.worker = CreateThread(nullptr, 0, FinishArmBeforeStop, nullptr, 0, nullptr);
    Check(g_vkm.worker != nullptr && VkmShutdown(), "runtime shutdown drains and joins active worker");
    Check(shutdown_worker_stage == 2, "published arm completes before worker stop");
    Check(gpu_event_destroys == 0 && g_vkm.ev_out[0] == 3, "joined worker does not prematurely destroy GPU events");
    Check(g_vkm.worker == nullptr && g_vkm.idle == nullptr && g_vkm.wake == nullptr,
          "joined CPU worker handles are retired");
    g_vkm.drain = 2; g_vkm.fg_arm = 1; g_vkm.next = g_vkm.cur = 1;
    VkmDestroyRetiredEvents(); // Fixture now supplies the caller's idle proof.
    Check(gpu_event_destroys == 4 && g_vkm.ev_in[0] == 0 && g_vkm.ev_out[1] == 0,
          "GPU events are destroyed only in retirement helper");
    Check(g_vkm.drain == 0 && g_vkm.fg_arm == 0 && g_vkm.next == 0 && g_vkm.cur == 0,
          "retired event tokens cannot block a recreated runtime forever");
    g_fg_input.want_release = g_fg_input.refused = true;
    g_fg_input.w = 5120; g_fg_input.h = 2160; g_fg_input.vk_fmt = 37;
    FgRelayOnSessionRelease();
    FgInputOnSessionRelease();
    Check(!g_fgr.ready && !g_fgr.tried && !g_fgr.releasing && !g_fgr.fell_back,
          "completed runtime teardown allows a fresh relay session");
    Check(!g_fg_input.refused && !g_fg_input.want_release && g_fg_input.vk_fmt == 0 && g_s12.need_reset,
          "completed runtime teardown clears FG shape/refusal and resets history");
    Check(g_fgr.dev == nullptr, "completed relay retirement clears its stale device identity");
    g_svk.dev = nullptr; // SynthVkShutdownForDevice's successful final state.
    CheckEmptyRuntimeRestart("repeated empty runtime replacement after a retired real session");
    std::printf("hooks: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
