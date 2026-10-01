// Production recovery with a real D3D12 queue; Vulkan completion is controlled
// by stubs. No game, add-on injection, model or texture allocation is involved.
#include "dlss5-bridge.cpp"
static unsigned checks, failures;
static void Check(bool ok, const char *why) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n", why); } }
static bool vk_done, park_done;
static uint32_t FenceStatus(SVkDevice, SVkHandle) { return vk_done ? kSVkSuccess : 1; }
static uint32_t EventStatus(SVkDevice, SVkHandle) { return park_done ? kSVkEventSet : 0; }

static void HistoryTests()
{
    for (unsigned focus = 0; focus != 2; ++focus) {
        FgFocusHistoryPolicy p;
        Check(!p.Keep(10000, focus != 0, true, true, false, 10000, 0), "startup never suppresses a reset");
        Check(!p.Keep(11000, focus == 0, true, false, false, 11000, 0), "focus edge alone does not request reset");
        Check(p.Keep(12500, focus == 0, true, true, false, 12490, 0), "one delayed focus reset can preserve history");
        Check(!p.Keep(12520, focus == 0, true, true, false, 12510, 0), "second reset is not suppressed");
    }
    for (unsigned reason = 0; reason != 8; ++reason) {
        FgFocusHistoryPolicy p;
        p.Keep(10000, true, true, false, false, 10000, 0);
        p.Keep(11000, false, true, false, false, 11000, 0);
        Check(!p.Keep(reason == 0 ? 14001 : 12000, false, reason != 1, true, reason == 2,
            reason == 3 ? 0 : reason == 4 ? 11749 : reason == 5 ? 12001 : 12000,
            reason == 6 ? 11999 : reason == 7 ? 12001 : 0),
            "expired/ineligible/discontinuous/unknown/stale/future source and native reset keep reset");
    }
    FgFocusHistoryPolicy p;
    p.Keep(10000, true, true, false, false, 10000, 0);
    p.Keep(11000, false, false, false, false, 11000, 0);
    Check(!p.Keep(12000, true, true, true, false, 12000, 0), "minimized restoration keeps reset");
    InterlockedExchange(&g_fg_guides_pending_reset, 0);
    FgGuideObserveSourceReset(true);
    Check(g_fg_guides_pending_reset == 1 && g_fg_source_reset_tick > 0, "native SR reset requests NR reset independently of FG");
}

int main()
{
    strcpy_s(g_log_path, "recovery.log");
    InitializeCriticalSection(&g_log_cs);
    HistoryTests();
    FgRelayInit();
    Check(FgRelayRecoveryReady(), "normal session does not enter recovery");
    g_fgr.recovery_pending = true;
    g_fgr.arm_active = 1;
    Check(!FgRelayRecoveryReady(), "active recorder blocks recovery");
    g_fgr.arm_active = 0; g_fgr.arm_seq = 1;
    Check(!FgRelayRecoveryReady(), "unconsumed arm blocks recovery");
    g_fgr.worker_seq = g_fgr.eval_seq = 1;
    g_vkm.idle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(!FgRelayRecoveryReady(), "busy CPU worker blocks recovery");
    SetEvent(g_vkm.idle);
    Check(!FgRelayRecoveryReady(), "missing device blocks recovery");
    HMODULE d3d = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create = d3d ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d, "D3D12CreateDevice")) : nullptr;
    if (!create || FAILED(create(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&g_s12.dev)))) return 2;
    D3D12_COMMAND_QUEUE_DESC desc = {}; desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(g_s12.dev->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&g_s12.queue)))) return 2;
    ID3D12Fence *gate = nullptr;
    if (FAILED(g_s12.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&gate)))) return 2;
    g_s12.queue->Wait(gate, 1);
    Check(!FgRelayRecoveryReady() && g_fgr.recovery_marker_submitted, "marker is queued after pending GPU work");
    Check(!FgRelayRecoveryReady(), "pending marker blocks recovery without CPU wait");
    if (FAILED(g_s12.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&g_fgr.nr_fence12)))) return 2;
    g_fgr.nr_fence12->Signal(999);
    Check(!FgRelayRecoveryReady(), "host-signalled relay fence cannot counterfeit independent GPU completion");
    gate->Signal(1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_fgr.recovery_marker->SetEventOnCompletion(1, event);
    Check(WaitForSingleObject(event, 2000) == WAIT_OBJECT_0, "real GPU completes marker after gate release");
    Check(FgRelayRecoveryReady(), "retired D3D12 work permits recovery");
    g_fgr.ready = true; g_fgr.slot_used[0] = true; g_fgr.ev_mark[0] = 1;
    g_svk.EvStatus = EventStatus; g_fgr.GetFenceStatus = FenceStatus;
    Check(!FgRelayRecoveryReady(), "unpassed Vulkan park blocks recovery");
    park_done = true; g_fgr.copy_in_flight[0] = true;
    Check(!FgRelayRecoveryReady(), "in-flight Vulkan copy blocks recovery");
    g_fgr.copy_in_flight[0] = false; g_fgr.release_in_flight[0] = true;
    Check(!FgRelayRecoveryReady(), "in-flight Vulkan release blocks recovery");
    vk_done = true;
    Check(FgRelayRecoveryReady(), "all completion proofs permit resource retirement");
    g_s12.resources_retained = true;
    Check(!FgRelayRecoveryReady(), "unproven retained resources never auto-resume");
    g_s12.resources_retained = false;
    FgRelayOnSessionRelease();
    Check(!g_fgr.recovery_pending && !g_fgr.recovery_marker_submitted && g_fgr.recovery_marker == nullptr,
        "session teardown clears recovery ownership and releases marker");
    gate->Release(); CloseHandle(event); CloseHandle(g_vkm.idle); g_vkm.idle = nullptr;
    g_s12.queue->Release(); g_s12.queue = nullptr; g_s12.dev->Release(); g_s12.dev = nullptr;
    printf("recovery/history: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
