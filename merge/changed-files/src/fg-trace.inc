// Observations are CPU-side and unpaired: stable handles do not establish frame identity or GPU readiness.
#include "fg-submit-observation.h"
namespace fso = fg_submit_observation;

static const unsigned kFgtDistinct = 12;
struct FgtSet { uint64_t v[kFgtDistinct]; unsigned n, overflow, hits; };

static int FgtNote(FgtSet &s, uint64_t x)
{
    for (unsigned i = 0; i < s.n; ++i)
        if (s.v[i] == x) { ++s.hits; return static_cast<int>(i); }
    if (s.n < kFgtDistinct) { s.v[s.n] = x; ++s.hits; return static_cast<int>(s.n++); }
    ++s.overflow;
    return -1;
}

static void FgtSetText(const FgtSet &s, char *buf, size_t cap)
{
    size_t len = 0;
    FgCensusAppend(buf, cap, &len, "n=%u hits=%u", s.n, s.hits);
    if (s.overflow != 0) FgCensusAppend(buf, cap, &len, " overflow=%u", s.overflow);
    FgCensusAppend(buf, cap, &len, " [");
    for (unsigned i = 0; i < s.n; ++i) FgCensusAppend(buf, cap, &len, "%s%llx", i ? " " : "", s.v[i]);
    FgCensusAppend(buf, cap, &len, "]");
}

struct FgtStat { double sum, min, max; unsigned n; };

static void FgtStatAdd(FgtStat &s, double x)
{
    if (s.n == 0 || x < s.min) s.min = x;
    if (s.n == 0 || x > s.max) s.max = x;
    s.sum += x;
    ++s.n;
}

static void FgtStatText(const FgtStat &s, char *buf, size_t cap)
{
    if (s.n == 0) { strcpy_s(buf, cap, "n=0"); return; }
    _snprintf_s(buf, cap, _TRUNCATE, "n=%u avg=%.2f min=%.2f max=%.2f ms", s.n, s.sum / s.n, s.min, s.max);
}

static double FgtMs(LONGLONG from, LONGLONG to)
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    return static_cast<double>(to - from) * 1000.0 / static_cast<double>(freq.QuadPart);
}

static LONGLONG FgtNow()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

// Vulkan 1.0/1.3 structures the observers read. Field offsets per the specification.
struct FgtPresentInfo {
    uint32_t sType; const void *pNext;
    uint32_t waitSemaphoreCount; const uint64_t *pWaitSemaphores;
    uint32_t swapchainCount; const uint64_t *pSwapchains; const uint32_t *pImageIndices; void *pResults;
};
struct FgtSubmitInfo {
    uint32_t sType; const void *pNext;
    uint32_t waitSemaphoreCount; const uint64_t *pWaitSemaphores; const uint32_t *pWaitDstStageMask;
    uint32_t commandBufferCount; void *const *pCommandBuffers;
    uint32_t signalSemaphoreCount; const uint64_t *pSignalSemaphores;
};
struct FgtSemaphoreSubmitInfo { uint32_t sType; const void *pNext; uint64_t semaphore; uint64_t value; uint64_t stageMask; uint32_t deviceIndex; };
struct FgtCommandBufferSubmitInfo { uint32_t sType; const void *pNext; void *commandBuffer; uint32_t deviceMask; };
struct FgtSubmitInfo2 {
    uint32_t sType; const void *pNext; uint32_t flags;
    uint32_t waitSemaphoreInfoCount; const FgtSemaphoreSubmitInfo *pWaitSemaphoreInfos;
    uint32_t commandBufferInfoCount; const FgtCommandBufferSubmitInfo *pCommandBufferInfos;
    uint32_t signalSemaphoreInfoCount; const FgtSemaphoreSubmitInfo *pSignalSemaphoreInfos;
};
struct FgtTimelineSubmitInfo { uint32_t sType; const void *pNext; uint32_t waitValueCount; const uint64_t *pWaitValues; uint32_t signalValueCount; const uint64_t *pSignalValues; };
static const uint32_t kFgtStTimelineSubmitInfo = 1000207003;   // VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO

static_assert(sizeof(FgtPresentInfo) == 64, "VkPresentInfoKHR");
static_assert(sizeof(FgtSubmitInfo) == 72, "VkSubmitInfo");
static_assert(sizeof(FgtSubmitInfo2) == 64, "VkSubmitInfo2");
static_assert(sizeof(FgtSemaphoreSubmitInfo) == 48, "VkSemaphoreSubmitInfo");
static_assert(sizeof(FgtCommandBufferSubmitInfo) == 32, "VkCommandBufferSubmitInfo");

// sl::Resource and sl::ResourceTag (Streamline 2.10.3 sl_core_types.h).
struct FgtSlResource {
    uint8_t base[32];
    uint8_t type; uint8_t pad[7];
    void *native; void *memory; void *view;
    uint32_t state, width, height, nativeFormat, mipLevels, arrayLayers;
    uint64_t gpuVirtualAddress;
    uint32_t flags, usage, reserved;
};
struct FgtSlResourceTag {
    uint8_t base[32];
    const FgtSlResource *resource;
    uint32_t type, lifecycle;
    uint32_t top, left, width, height;
};
static_assert(offsetof(FgtSlResource, native) == 40 && offsetof(FgtSlResource, state) == 64 &&
              offsetof(FgtSlResource, flags) == 96, "sl::Resource");
static_assert(sizeof(FgtSlResourceTag) == 64 && offsetof(FgtSlResourceTag, type) == 40, "sl::ResourceTag");

// The buffer types the FG input feed cares about, in sl::kBufferType order.
static const uint32_t kFgtTagTypes[] = { 0, 1, 2, 4, 23, 53 };
static const char *const kFgtTagNames[] = { "Depth", "MVec", "HUDLess", "ScalingOut", "UIColorAlpha", "Backbuffer" };

struct FgtTagType {
    unsigned count, lifecycle[3], lifecycle_other, cmd_nonnull, extent_nonzero;
    uint32_t state, w, h, fmt, usage;
    FgtSet natives, views;
};

struct FgtQueue {
    uint64_t queue;
    unsigned submits, submits2, fences, timeline;
    uint64_t cbs, waits, signals;
    FgtSet threads;
};

struct FgtTagView {
    unsigned type, lifecycle; uint64_t native, view; uint32_t state, w, h, fmt, usage, top, left, ew, eh; bool ok;
};

struct FgTrace {
    unsigned present_calls;
    LONGLONG last_present_qpc, prev_present_qpc;
    uint64_t present_queue;
    FgtSet present_swapchains, present_threads, present_indices, present_wait_counts, present_pnext_types;
    FgtStat present_interval, present_duration;
    unsigned tag_calls, tag_frame_calls, tag_bad_reads;
    FgtSet tag_threads, tag_other_types;
    FgtTagType types[_countof(kFgtTagTypes)];
    unsigned eval_calls, eval_groups;
    uint64_t cmd_queue;
    DWORD eval_thread;
    void *last_fg_cmd;
    LONGLONG last_eval1_qpc;
    FgtSet hud, back, mv, depth, out_real, out_interp, eval_threads, eval_indices;
    FgtStat present_to_eval, eval_interval, eval_group_span;
    LONGLONG group_first_qpc;
    FgtQueue queues[8];
    unsigned nqueues, queue_overflow;
    unsigned cmdqueue_detail_left, present_queue_detail_left, eval_thread_detail_left;
    FgtSet reshade_queues;
    unsigned reshade_queue_types[kFgtDistinct];
};
struct FgtControl {
    CRITICAL_SECTION cs;
    bool cs_ready, early_tried, late_tried, queue_hooks_tried, layer_hook_tried, sleep_tried;
    Hook present_hook, tag_hook, tag_frame_hook, submit_hook, submit2_hook, sleep_hook;
    Hook get_queue_hook, get_queue2_hook, get_queue_layer_hook;
};
static FgTrace g_fgt;
static FgtControl g_fgt_control;
static fso::Observer g_fgt_observer;
static bool g_fgt_command_events_registered, g_fgt_submit_observed, g_fgt_submit2_observed;
static void *volatile g_fgt_submit_target;
static void *volatile g_fgt_submit2_target;
static void *volatile g_fgt_queue_target;

static void FgtLock()   { EnterCriticalSection(&g_fgt_control.cs); }
static void FgtUnlock() { LeaveCriticalSection(&g_fgt_control.cs); }

static bool FgtEnabled() { return PresentAdapterTrace(); }

// The present and queue-discovery hooks serve the GPU relay too: Pipeline=3 arms
// each frame's copy from inside the interposer's vkQueuePresentKHR, and the relay
// needs the family of the queues it submits on. Everything else this file
// observes stays trace-only.
static bool FgtHooksWanted() { return PresentAdapterTrace() || PresentAdapterRelay() || PresentAdapterFocusKeepFG(); }

static SRWLOCK g_fgt_log_lock = SRWLOCK_INIT;
static char g_fgt_logs[256][1536];
static unsigned g_fgt_log_read, g_fgt_log_count;
static volatile LONG g_fgt_log_dropped;

static void FgtLog(_Printf_format_string_ const char *fmt, ...)
{
    char line[1536];
    const int prefix = sprintf_s(line, "qpc=%lld ", FgtNow());
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(line + prefix, sizeof(line) - prefix, _TRUNCATE, fmt, args);
    va_end(args);
    if (!TryAcquireSRWLockExclusive(&g_fgt_log_lock)) { InterlockedIncrement(&g_fgt_log_dropped); return; }
    const bool wake = g_fgt_log_count == 0;
    if (g_fgt_log_count < _countof(g_fgt_logs)) {
        strcpy_s(g_fgt_logs[(g_fgt_log_read + g_fgt_log_count++) % _countof(g_fgt_logs)], line);
    } else InterlockedIncrement(&g_fgt_log_dropped);
    ReleaseSRWLockExclusive(&g_fgt_log_lock);
    if (wake) TriggerHookScan();
}

static void FgtFlushLog()
{
    for (unsigned i = 0; i < _countof(g_fgt_logs); ++i) {
        char line[1536];
        AcquireSRWLockExclusive(&g_fgt_log_lock);
        if (g_fgt_log_count == 0) { ReleaseSRWLockExclusive(&g_fgt_log_lock); break; }
        strcpy_s(line, g_fgt_logs[g_fgt_log_read]);
        g_fgt_log_read = (g_fgt_log_read + 1) % _countof(g_fgt_logs);
        --g_fgt_log_count;
        ReleaseSRWLockExclusive(&g_fgt_log_lock);
        Log("%s", line);
    }
    AcquireSRWLockShared(&g_fgt_log_lock);
    const bool pending = g_fgt_log_count != 0;
    ReleaseSRWLockShared(&g_fgt_log_lock);
    if (pending) InterlockedExchange(&g_scan_pending, 1);
}

static const char *FgtLifecycleName(unsigned l)
{
    return l == 0 ? "OnlyValidNow" : l == 1 ? "ValidUntilPresent" : l == 2 ? "ValidUntilEvaluate" : "?";
}

// ReShade SDK authority: reshade/reshade_events.hpp addon_event_traits. In Vulkan,
// reset_command_list is emitted before vkBeginCommandBuffer. A generation is an
// observed recording boundary, never proof that native/bypassed commands are covered.
static bool FgtCommandIdentity(reshade::api::command_list *cmd, uint64_t *cb, uint64_t *device)
{
    if (!FgtEnabled() || !g_fgt_control.cs_ready || cmd == nullptr) return false;
    auto *dev = cmd->get_device();
    if (dev == nullptr || dev->get_api() != reshade::api::device_api::vulkan) return false;
    *cb = cmd->get_native(); *device = dev->get_native();
    return *cb != 0;
}
static void OnFgtInitCommandList(reshade::api::command_list *cmd)
{
    uint64_t cb, device;
    if (!FgtCommandIdentity(cmd, &cb, &device)) return;
    FgtLock(); g_fgt_observer.Init(cb, device); FgtUnlock();
}
static void OnFgtResetCommandList(reshade::api::command_list *cmd)
{
    uint64_t cb, device;
    if (!FgtCommandIdentity(cmd, &cb, &device)) return;
    FgtLock(); g_fgt_observer.Reset(cb, device); FgtUnlock();
}
static void OnFgtDestroyCommandList(reshade::api::command_list *cmd)
{
    uint64_t cb, device;
    if (!FgtCommandIdentity(cmd, &cb, &device)) return;
    FgtLock(); g_fgt_observer.Destroy(cb); FgtUnlock();
}
static void OnFgtDestroyCommandDevice(reshade::api::device *dev)
{
    if (!FgtEnabled() || !g_fgt_control.cs_ready || dev == nullptr || dev->get_api() != reshade::api::device_api::vulkan) return;
    const uint64_t device = dev->get_native();
    FgtLock(); g_fgt_observer.DestroyDevice(device); FgtUnlock();
}
static void FgtCommandActivity(reshade::api::command_list *cmd, fso::Command kind)
{
    uint64_t cb, device;
    if (!FgtCommandIdentity(cmd, &cb, &device)) return;
    FgtLock(); g_fgt_observer.Activity(cb, device, kind); FgtUnlock();
}

// Derive the complete callback ABI from the vendored trait instead of repeating
// parameter lists. Counts are callback occurrences, not workgroup/resource counts.
// Bool events always return false: observing must never consume a game command.
// Secondary events count the execute site only; their contents are not flattened.
template <fso::Command Kind, typename Signature> struct FgtCommandEvent;
template <fso::Command Kind, typename Return, typename... Args>
struct FgtCommandEvent<Kind, Return (*)(reshade::api::command_list *, Args...)> {
    static Return Callback(reshade::api::command_list *cmd, Args...)
    {
        FgtCommandActivity(cmd, Kind);
        return Return();
    }
};
template <reshade::addon_event Event>
static void FgtRegisterTyped(PFN_ReShadeRegisterEvent reg, typename reshade::addon_event_traits<Event>::decl callback)
{
    reg(Event, reinterpret_cast<void *>(callback));
}
template <reshade::addon_event Event, fso::Command Kind>
static void FgtRegisterCommand(PFN_ReShadeRegisterEvent reg)
{
    FgtRegisterTyped<Event>(reg, &FgtCommandEvent<Kind, typename reshade::addon_event_traits<Event>::decl>::Callback);
}
static void FgtRegisterCommandEvents(PFN_ReShadeRegisterEvent reg)
{
    FgtRegisterTyped<reshade::addon_event::init_command_list>(reg, &OnFgtInitCommandList);
    FgtRegisterTyped<reshade::addon_event::reset_command_list>(reg, &OnFgtResetCommandList);
    FgtRegisterTyped<reshade::addon_event::destroy_command_list>(reg, &OnFgtDestroyCommandList);
    FgtRegisterTyped<reshade::addon_event::destroy_device>(reg, &OnFgtDestroyCommandDevice);
    FgtRegisterCommand<reshade::addon_event::dispatch, fso::Command::dispatch>(reg);
    FgtRegisterCommand<reshade::addon_event::dispatch_mesh, fso::Command::dispatch>(reg);
    FgtRegisterCommand<reshade::addon_event::dispatch_rays, fso::Command::dispatch>(reg);
    FgtRegisterCommand<reshade::addon_event::draw, fso::Command::draw>(reg);
    FgtRegisterCommand<reshade::addon_event::draw_indexed, fso::Command::draw>(reg);
    FgtRegisterCommand<reshade::addon_event::draw_or_dispatch_indirect, fso::Command::indirect>(reg);
    FgtRegisterCommand<reshade::addon_event::barrier, fso::Command::barrier>(reg);
    FgtRegisterCommand<reshade::addon_event::execute_secondary_command_list, fso::Command::secondary>(reg);
    FgtRegisterCommand<reshade::addon_event::copy_resource, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::copy_buffer_region, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::copy_buffer_to_texture, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::copy_texture_region, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::copy_texture_to_buffer, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::resolve_texture_region, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::update_buffer_region_command, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::update_texture_region_command, fso::Command::copy>(reg);
    FgtRegisterCommand<reshade::addon_event::clear_depth_stencil_view, fso::Command::clear>(reg);
    FgtRegisterCommand<reshade::addon_event::clear_render_target_view, fso::Command::clear>(reg);
    FgtRegisterCommand<reshade::addon_event::clear_unordered_access_view_uint, fso::Command::clear>(reg);
    FgtRegisterCommand<reshade::addon_event::clear_unordered_access_view_float, fso::Command::clear>(reg);
    FgtRegisterCommand<reshade::addon_event::begin_render_pass, fso::Command::render_pass>(reg);
    FgtRegisterCommand<reshade::addon_event::end_render_pass, fso::Command::render_pass>(reg);
    FgtRegisterCommand<reshade::addon_event::bind_pipeline, fso::Command::bind>(reg);
    FgtRegisterCommand<reshade::addon_event::bind_descriptor_tables, fso::Command::bind>(reg);
    FgtRegisterCommand<reshade::addon_event::bind_render_targets_and_depth_stencil, fso::Command::bind>(reg);
    FgtRegisterCommand<reshade::addon_event::push_constants, fso::Command::bind>(reg);
    FgtRegisterCommand<reshade::addon_event::push_descriptors, fso::Command::bind>(reg);
    FgtRegisterCommand<reshade::addon_event::begin_query, fso::Command::query>(reg);
    FgtRegisterCommand<reshade::addon_event::end_query, fso::Command::query>(reg);
    FgtRegisterCommand<reshade::addon_event::copy_query_heap_results, fso::Command::query>(reg);
    FgtLock(); g_fgt_command_events_registered = true; FgtUnlock();
    FgtLog("[fg-submit-observer] coverage=events-registered-not-proven-on-FG-CB; first_limit=%u cb_capacity=%u locations_per_sample=%u; "
        "generations=observed-init/begin; counts=callback-occurrences secondary=execute-site-only; matches=first-observed-direct-submit-attempt/retries-summary-only; "
        "native/addon commands may bypass ReShade; late-load/missed-lifecycle/capacity are incomplete; count0 never proves empty; observed prefix does not prove input production or GPU readiness",
        fso::kSamples, fso::kCommandLists, fso::kLocations);
}
static void FgtLogFirstSample(const fso::Sample &s)
{
    if (s.id == 0) return;
    const uint64_t *c = s.prefix.kind;
    FgtLog("[fg-submit-observer] first=%llu eval_qpc=%llu idx=%u%s cb=%llx generation=%llu eval_queue=%llx coverage=%s "
        "hud=%llx back=%llx depth=%llx mv=%llx images_known=0x%x prefix=%s total=%llu "
        "dispatch=%llu copy=%llu barrier=%llu draw=%llu execute_secondary=%llu indirect=%llu clear=%llu render_pass=%llu bind=%llu query=%llu",
        s.id, s.qpc, s.index, s.indexed ? "" : "(OutputReal-fallback)", s.cb, s.generation, s.eval_queue, fso::CoverageName(s.coverage),
        s.images.hud, s.images.back, s.images.depth, s.images.mv, s.images.known,
        s.prefix.total != 0 ? "PRESENT(observed-events-before-FG;not-input-producer-proof)" : "NOT-CAPTURED(does-not-prove-absent-or-empty)",
        s.prefix.total, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9]);
}

#include "focus-fg.inc"

// ---------------------------------------------------------------------------
// Game-side vkQueuePresentKHR: the interposer's export, which is also what its
// vkGetDeviceProcAddr hands out (SL_INTERCEPT). The DLSS-G before-hook runs
// inside the original, so this detour sees frame N the moment the game is done.
typedef uint32_t (*PFN_FgtQueuePresent)(void *, const FgtPresentInfo *);

static void FgtNotePresent(void *queue, const FgtPresentInfo *info, LONGLONG now)
{
    FgTrace &t = g_fgt;
    FgtLock();
    const unsigned call = ++t.present_calls;
    if (t.prev_present_qpc != 0) FgtStatAdd(t.present_interval, FgtMs(t.prev_present_qpc, now));
    t.prev_present_qpc = now;
    t.present_queue = reinterpret_cast<uint64_t>(queue);
    FgtNote(t.present_threads, GetCurrentThreadId());
    uint64_t sc = 0; uint32_t idx = 0xFFFFFFFFu; uint32_t waits = 0; uint32_t pnext_type = 0;
    if (info != nullptr) {
        waits = info->waitSemaphoreCount;
        if (info->swapchainCount != 0 && info->pSwapchains != nullptr) sc = info->pSwapchains[0];
        if (info->swapchainCount != 0 && info->pImageIndices != nullptr) idx = info->pImageIndices[0];
        if (info->pNext != nullptr) pnext_type = *static_cast<const uint32_t *>(info->pNext);
        FgtNote(t.present_swapchains, sc);
        FgtNote(t.present_indices, idx);
        FgtNote(t.present_wait_counts, waits);
        FgtNote(t.present_pnext_types, pnext_type);
    }
    t.last_present_qpc = now;
    FgtUnlock();
    if (call <= 12 || call % 1800 == 0) {
        char sems[160] = {}; size_t len = 0;
        for (uint32_t i = 0; info != nullptr && info->pWaitSemaphores != nullptr && i < waits && i < 3; ++i)
            FgCensusAppend(sems, sizeof(sems), &len, "%s%llx", i ? " " : "", info->pWaitSemaphores[i]);
        FgtLog("[fg-trace] game present #%u thread=%lu queue=%p swapchains=%u sc=%llx image=%u waits=%u [%s] pNext=%u",
            call, GetCurrentThreadId(), queue, info != nullptr ? info->swapchainCount : 0u, sc, idx, waits, sems, pnext_type);
    }
}

static uint32_t FgtPresentDetour(void *queue, const FgtPresentInfo *info)
{
    FocusFgOnPresent();
    if (PresentAdapterTrace()) FgtNotePresent(queue, info, FgtNow());
    // The GPU relay arms its copy here (Pipeline=3), before the original runs, so
    // the copy is in the present queue ahead of the present operation itself.
    if (PresentAdapterRelay()) FgRelayOnPresent(queue, info);
    const LONGLONG start = FgtNow();
    const uint32_t result = reinterpret_cast<PFN_FgtQueuePresent>(g_fgt_control.present_hook.original)(queue, info);
    const LONGLONG end = FgtNow();
    if (PresentAdapterTrace() && g_fgt_control.cs_ready) {
        FgtLock();
        FgtStatAdd(g_fgt.present_duration, FgtMs(start, end));
        FgtUnlock();
    }
    return result;
}

// slReflexSleep, the Reflex plugin's entry the game calls at the top of each frame,
// before it samples input and simulates. The relay's throttle waits here, after
// Reflex's own sleep, so the frame starts only once the NR it would queue behind
// has completed. The address is the plugin's own (slGetPluginFunction), which is
// what slGetFeatureFunction hands the game, so patching it covers the game however
// early it fetched the pointer.
typedef int (*PFN_FgtReflexSleep)(const void *);
static int FgtReflexSleepDetour(const void *frame)
{
    const int result = reinterpret_cast<PFN_FgtReflexSleep>(g_fgt_control.sleep_hook.original)(frame);
    if (PresentAdapterRelay()) FgRelayThrottle(kFgrAtSleep);
    return result;
}

// ---------------------------------------------------------------------------
// slSetTag / slSetTagForFrame: what the game hands Streamline, with which lifecycle.
typedef uint32_t (*PFN_FgtSetTag)(const void *, const FgtSlResourceTag *, uint32_t, void *);
typedef uint32_t (*PFN_FgtSetTagForFrame)(const void *, const void *, const FgtSlResourceTag *, uint32_t, void *);

static bool FgtReadTag(const FgtSlResourceTag *tag, FgtTagView *out)
{
    __try {
        out->type = tag->type; out->lifecycle = tag->lifecycle;
        out->top = tag->top; out->left = tag->left; out->ew = tag->width; out->eh = tag->height;
        const FgtSlResource *r = tag->resource;
        if (r != nullptr) {
            out->native = reinterpret_cast<uint64_t>(r->native);
            out->view = reinterpret_cast<uint64_t>(r->view);
            out->state = r->state; out->w = r->width; out->h = r->height; out->fmt = r->nativeFormat; out->usage = r->usage;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void FgtNoteTags(const FgtSlResourceTag *tags, uint32_t num, void *cmd, bool for_frame, const void *frame)
{
    FgTrace &t = g_fgt;
    FgtTagView views[16] = {};
    const uint32_t n = num < _countof(views) ? num : static_cast<uint32_t>(_countof(views));
    for (uint32_t i = 0; tags != nullptr && i < n; ++i) views[i].ok = FgtReadTag(tags + i, &views[i]);
    FgtLock();
    const unsigned call = for_frame ? ++t.tag_frame_calls : ++t.tag_calls;
    FgtNote(t.tag_threads, GetCurrentThreadId());
    for (uint32_t i = 0; i < n; ++i) {
        if (!views[i].ok) { ++t.tag_bad_reads; continue; }
        int slot = -1;
        for (unsigned k = 0; k < _countof(kFgtTagTypes); ++k) if (kFgtTagTypes[k] == views[i].type) slot = static_cast<int>(k);
        if (slot < 0) { FgtNote(t.tag_other_types, views[i].type); continue; }
        FgtTagType &tt = t.types[slot];
        ++tt.count;
        if (views[i].lifecycle < 3) ++tt.lifecycle[views[i].lifecycle]; else ++tt.lifecycle_other;
        if (cmd != nullptr) ++tt.cmd_nonnull;
        if (views[i].ew != 0 || views[i].eh != 0) ++tt.extent_nonzero;
        tt.state = views[i].state; tt.w = views[i].w; tt.h = views[i].h; tt.fmt = views[i].fmt; tt.usage = views[i].usage;
        FgtNote(tt.natives, views[i].native);
        FgtNote(tt.views, views[i].view);
    }
    FgtUnlock();
    if (call <= 8 || call % 1800 == 0) {
        FgtLog("[fg-trace] %s #%u thread=%lu tags=%u cmd=%p frame=%p", for_frame ? "slSetTagForFrame" : "slSetTag",
            call, GetCurrentThreadId(), num, cmd, frame);
        for (uint32_t i = 0; i < n; ++i) {
            if (!views[i].ok) { FgtLog("[fg-trace]   tag[%u] unreadable", i); continue; }
            FgtLog("[fg-trace]   tag[%u] type=%u lifecycle=%s native=%llx view=%llx state=%u %ux%u fmt=%u usage=0x%x extent=%u,%u %ux%u",
                i, views[i].type, FgtLifecycleName(views[i].lifecycle), views[i].native, views[i].view, views[i].state,
                views[i].w, views[i].h, views[i].fmt, views[i].usage, views[i].left, views[i].top, views[i].ew, views[i].eh);
        }
    }
}

static uint32_t FgtSetTagDetour(const void *viewport, const FgtSlResourceTag *tags, uint32_t num, void *cmd)
{
    FgtNoteTags(tags, num, cmd, false, nullptr);
    return reinterpret_cast<PFN_FgtSetTag>(g_fgt_control.tag_hook.original)(viewport, tags, num, cmd);
}

static uint32_t FgtSetTagForFrameDetour(const void *frame, const void *viewport, const FgtSlResourceTag *tags, uint32_t num, void *cmd)
{
    FgtNoteTags(tags, num, cmd, true, frame);
    return reinterpret_cast<PFN_FgtSetTagForFrame>(g_fgt_control.tag_frame_hook.original)(frame, viewport, tags, num, cmd);
}

// ---------------------------------------------------------------------------
// vkQueueSubmit / vkQueueSubmit2 at the top of the layer chain: the function the
// loader's vkGetDeviceProcAddr returns, which every dispatch table in the process
// -- the interposer's, Streamline's plugins', the game's -- points at.
typedef uint32_t (*PFN_FgtQueueSubmit)(void *, uint32_t, const FgtSubmitInfo *, uint64_t);
typedef uint32_t (*PFN_FgtQueueSubmit2)(void *, uint32_t, const FgtSubmitInfo2 *, uint64_t);

static FgtQueue *FgtQueueSlot(uint64_t q)
{
    FgTrace &t = g_fgt;
    for (unsigned i = 0; i < t.nqueues; ++i) if (t.queues[i].queue == q) return &t.queues[i];
    if (t.nqueues < _countof(t.queues)) { t.queues[t.nqueues].queue = q; return &t.queues[t.nqueues++]; }
    ++t.queue_overflow;
    return nullptr;
}

// The observer copies only scalar metadata from borrowed arrays. SEH is confined
// to a trivial leaf, so unreadable application memory cannot strand our lock or
// suppress the original submit. Neither semaphore payloads nor pNext are needed
// for recording-identity matching; the original receives every pointer unchanged.
template <typename T>
static bool FgtReadArray(const T *source, uint32_t index, T *out)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(source);
    const uintptr_t max = ~uintptr_t(0);
    if (base == 0 || base > max - (sizeof(T) - 1) ||
        static_cast<uintptr_t>(index) > (max - base - (sizeof(T) - 1)) / sizeof(T)) return false;
    __try { memcpy(out, reinterpret_cast<const void *>(base + static_cast<uintptr_t>(index) * sizeof(T)), sizeof(T)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct FgtSubmitBatch {
    uint32_t waits, signals, cbs;
    void *const *commands;
    const FgtCommandBufferSubmitInfo *commands2;
};
static bool FgtReadSubmitBatch(const FgtSubmitInfo *subs, const FgtSubmitInfo2 *subs2, uint32_t index,
    bool submit2, FgtSubmitBatch *out)
{
    if (submit2) {
        FgtSubmitInfo2 s = {};
        if (!FgtReadArray(subs2, index, &s)) return false;
        out->waits = s.waitSemaphoreInfoCount; out->signals = s.signalSemaphoreInfoCount;
        out->cbs = s.commandBufferInfoCount; out->commands2 = s.pCommandBufferInfos;
    } else {
        FgtSubmitInfo s = {};
        if (!FgtReadArray(subs, index, &s)) return false;
        out->waits = s.waitSemaphoreCount; out->signals = s.signalSemaphoreCount;
        out->cbs = s.commandBufferCount; out->commands = s.pCommandBuffers;
    }
    return true;
}
static void FgtObserveSubmit(fso::Submit *out, const FgtSubmitInfo *subs, const FgtSubmitInfo2 *subs2)
{
    for (uint32_t batch = 0; batch < out->batches; ++batch) {
        FgtSubmitBatch s = {};
        if (!FgtReadSubmitBatch(subs, subs2, batch, out->submit2, &s)) { fso::Add(out->read_faults); break; }
        fso::Add(out->batches_read); fso::Add(out->waits, s.waits);
        fso::Add(out->signals, s.signals); fso::Add(out->declared_cbs, s.cbs);
        for (uint32_t cb = 0; cb < s.cbs; ++cb) {
            void *command = nullptr;
            if (out->submit2) {
                FgtCommandBufferSubmitInfo item = {};
                if (!FgtReadArray(s.commands2, cb, &item)) { fso::Add(out->read_faults); break; }
                command = item.commandBuffer;
            } else if (!FgtReadArray(s.commands, cb, &command)) { fso::Add(out->read_faults); break; }
            fso::Observer::MatchCommand(*out, reinterpret_cast<uint64_t>(command), { batch, cb, s.cbs, s.waits, s.signals });
        }
    }
}
static void FgtFinishObservedSubmit(fso::Submit *s, uint32_t result)
{
    FgtLock(); g_fgt_observer.Finish(*s, result); FgtUnlock();
    for (unsigned i = 0; i < s->candidates; ++i) {
        const fso::Match &m = s->matches[i];
        if (!m.claimed) continue;
        FgtLog("[fg-submit-observer] first=%llu submit=%llu api=%s queue=%llx fence=%llx cb=%llx "
            "eval_generation=%llu submit_generation=%llu match=%s coverage=%s generation_changed_during_call=%u "
            "batches=%u read=%llu cbs=%llu/%llu waits=%llu signals=%llu read_faults=%llu occurrences=%llu locations_omitted=%llu "
            "result_bits=0x%08x (submit-return-not-GPU-completion)",
            m.sample.id, s->id, s->submit2 ? "vkQueueSubmit2/KHR" : "vkQueueSubmit", s->queue, s->fence, m.sample.cb,
            m.sample.generation, m.submit_generation, m.sample.generation != 0 ? "same-observed-generation" : "HANDLE-ONLY-untracked",
            fso::CoverageName(m.sample.coverage), m.generation_changed ? 1u : 0u,
            s->batches, s->batches_read, s->cbs_read, s->declared_cbs, s->waits, s->signals, s->read_faults,
            m.occurrences, m.omitted_locations, s->result);
        for (unsigned j = 0; j < m.stored; ++j) {
            const fso::Location &p = m.locations[j];
            FgtLog("[fg-submit-observer] first=%llu submit=%llu occurrence=%u batch=%u cb_index=%u batch_cbs=%u waits=%u signals=%u",
                m.sample.id, s->id, j, p.batch, p.cb_index, p.batch_cbs, p.waits, p.signals);
        }
    }
}

struct FgtSubmitView {
    uint32_t waits, cbs, signals; bool timeline, has_fg_cmd, incomplete;
    uint64_t wait_sem[3], signal_sem[3]; uint64_t wait_val[3], signal_val[3]; void *cb[3];
};

static void FgtViewSubmit(const FgtSubmitInfo *source, void *fg_cmd, FgtSubmitView *v)
{
    FgtSubmitInfo s = {};
    if (!FgtReadArray(source, 0, &s)) { v->incomplete = true; return; }
    v->waits = s.waitSemaphoreCount; v->cbs = s.commandBufferCount; v->signals = s.signalSemaphoreCount;
    for (uint32_t i = 0; i < v->waits && i < 3; ++i)
        if (!FgtReadArray(s.pWaitSemaphores, i, &v->wait_sem[i])) { v->incomplete = true; break; }
    for (uint32_t i = 0; i < v->signals && i < 3; ++i)
        if (!FgtReadArray(s.pSignalSemaphores, i, &v->signal_sem[i])) { v->incomplete = true; break; }
    for (uint32_t i = 0; i < v->cbs; ++i) {
        void *cb = nullptr;
        if (!FgtReadArray(s.pCommandBuffers, i, &cb)) { v->incomplete = true; break; }
        if (i < 3) v->cb[i] = cb;
        if (cb == fg_cmd && fg_cmd != nullptr) v->has_fg_cmd = true;
    }
    struct Extension { uint32_t sType; const void *pNext; };
    const void *next = s.pNext;
    for (unsigned depth = 0; next != nullptr && depth < 16; ++depth) {
        Extension ext = {};
        if (!FgtReadArray(static_cast<const Extension *>(next), 0, &ext)) { v->incomplete = true; return; }
        if (ext.sType == kFgtStTimelineSubmitInfo) {
            FgtTimelineSubmitInfo tl = {};
            if (!FgtReadArray(static_cast<const FgtTimelineSubmitInfo *>(next), 0, &tl)) { v->incomplete = true; return; }
            v->timeline = true;
            for (uint32_t i = 0; i < tl.waitValueCount && i < 3; ++i)
                if (!FgtReadArray(tl.pWaitValues, i, &v->wait_val[i])) { v->incomplete = true; break; }
            for (uint32_t i = 0; i < tl.signalValueCount && i < 3; ++i)
                if (!FgtReadArray(tl.pSignalValues, i, &v->signal_val[i])) { v->incomplete = true; break; }
            return;
        }
        next = ext.pNext;
    }
    if (next != nullptr) v->incomplete = true; // Bounded even for a cyclic legacy-detail pNext chain.
}

static void FgtViewSubmit2(const FgtSubmitInfo2 *source, void *fg_cmd, FgtSubmitView *v)
{
    FgtSubmitInfo2 s = {};
    if (!FgtReadArray(source, 0, &s)) { v->incomplete = true; return; }
    v->waits = s.waitSemaphoreInfoCount; v->cbs = s.commandBufferInfoCount; v->signals = s.signalSemaphoreInfoCount;
    v->timeline = false;
    for (uint32_t i = 0; i < v->waits && i < 3; ++i) {
        FgtSemaphoreSubmitInfo sem = {};
        if (!FgtReadArray(s.pWaitSemaphoreInfos, i, &sem)) { v->incomplete = true; break; }
        v->wait_sem[i] = sem.semaphore; v->wait_val[i] = sem.value;
    }
    for (uint32_t i = 0; i < v->signals && i < 3; ++i) {
        FgtSemaphoreSubmitInfo sem = {};
        if (!FgtReadArray(s.pSignalSemaphoreInfos, i, &sem)) { v->incomplete = true; break; }
        v->signal_sem[i] = sem.semaphore; v->signal_val[i] = sem.value;
    }
    for (uint32_t i = 0; i < v->cbs; ++i) {
        FgtCommandBufferSubmitInfo cb = {};
        if (!FgtReadArray(s.pCommandBufferInfos, i, &cb)) { v->incomplete = true; break; }
        if (i < 3) v->cb[i] = cb.commandBuffer;
        if (cb.commandBuffer == fg_cmd && fg_cmd != nullptr) v->has_fg_cmd = true;
    }
}

static void FgtNoteSubmit(void *queue, uint32_t count, const FgtSubmitInfo *subs, const FgtSubmitInfo2 *subs2,
    uint64_t fence, bool is_submit2, const fso::Submit &observation)
{
    if (!FgtEnabled() || !g_fgt_control.cs_ready) return;
    FgTrace &t = g_fgt;
    const uint64_t q = reinterpret_cast<uint64_t>(queue);
    const LONGLONG now = FgtNow();
    const DWORD tid = GetCurrentThreadId();
    FgtSubmitView views[4] = {};
    const uint32_t n = count < _countof(views) ? count : static_cast<uint32_t>(_countof(views));
    FgtLock();
    void *const fg_cmd = t.last_fg_cmd;
    const bool sample_window = g_fgt_observer.Summary().firsts <= fso::kSamples;
    FgtUnlock();
    for (uint32_t i = 0; i < n; ++i) {
        if (!sample_window) break;
        if (is_submit2) {
            FgtSubmitInfo2 s = {};
            if (FgtReadArray(subs2, i, &s)) FgtViewSubmit2(&s, fg_cmd, &views[i]);
            else views[i].incomplete = true;
        } else {
            FgtSubmitInfo s = {};
            if (FgtReadArray(subs, i, &s)) FgtViewSubmit(&s, fg_cmd, &views[i]);
            else views[i].incomplete = true;
        }
    }
    bool detail = false;
    const char *why = "";
    FgtLock();
    FgtQueue *slot = FgtQueueSlot(q);
    if (slot != nullptr) {
        ++slot->submits;
        if (is_submit2) ++slot->submits2;
        if (fence != 0) ++slot->fences;
        FgtNote(slot->threads, tid);
        fso::Add(slot->cbs, observation.cbs_read); fso::Add(slot->waits, observation.waits); fso::Add(slot->signals, observation.signals);
        for (uint32_t i = 0; i < n; ++i)
            if (views[i].timeline) ++slot->timeline;
    }
    if (q == t.cmd_queue && t.cmd_queue != 0 && t.cmdqueue_detail_left != 0) { --t.cmdqueue_detail_left; detail = true; why = "FG queue"; }
    else if (q == t.present_queue && t.present_queue != 0 && t.present_queue_detail_left != 0) { --t.present_queue_detail_left; detail = true; why = "game present queue"; }
    else if (tid == t.eval_thread && t.eval_thread != 0 && t.eval_thread_detail_left != 0) { --t.eval_thread_detail_left; detail = true; why = "evaluate thread"; }
    const LONGLONG eval1 = t.last_eval1_qpc;
    const LONGLONG present = t.last_present_qpc;
    FgtUnlock();
    if (!detail || !sample_window) return;
    FgtLog("[fg-trace] %s (%s; legacy-preview-only/max4-batches/max3-printed-CBs/not-recording-identity) queue=%p thread=%lu batches=%u fence=%llx since_present=%.2fms since_eval1=%.2fms",
        is_submit2 ? "vkQueueSubmit2" : "vkQueueSubmit", why, queue, tid, count, fence,
        present != 0 ? FgtMs(present, now) : -1.0, eval1 != 0 ? FgtMs(eval1, now) : -1.0);
    for (uint32_t i = 0; i < n; ++i) {
        const FgtSubmitView &v = views[i];
        FgtLog("[fg-trace]   batch[%u] waits=%u [%llx:%llu %llx:%llu %llx:%llu] cbs=%u [%p %p %p]%s signals=%u [%llx:%llu %llx:%llu %llx:%llu]%s%s",
            i, v.waits, v.wait_sem[0], v.wait_val[0], v.wait_sem[1], v.wait_val[1], v.wait_sem[2], v.wait_val[2],
            v.cbs, v.cb[0], v.cb[1], v.cb[2], v.has_fg_cmd ? " <last-FG-handle-only>" : "",
            v.signals, v.signal_sem[0], v.signal_val[0], v.signal_sem[1], v.signal_val[1], v.signal_sem[2], v.signal_val[2],
            v.timeline ? " timeline" : "", v.incomplete ? " incomplete-read" : "");
    }
}

static thread_local bool g_fgt_in_submit_observer;
static bool FgtBeginSubmitObservation()
{
    if (!FgtEnabled() || !g_fgt_control.cs_ready) return false;
    if (g_fgt_in_submit_observer) {
        FgtLock(); g_fgt_observer.NestedSubmitSkipped(); FgtUnlock();
        return false; // Layer forwarding may re-enter another observed dispatch target.
    }
    g_fgt_in_submit_observer = true;
    return true;
}
static uint32_t FgtQueueSubmitDetour(void *queue, uint32_t count, const FgtSubmitInfo *subs, uint64_t fence)
{
    if (!FgtBeginSubmitObservation())
        return reinterpret_cast<PFN_FgtQueueSubmit>(g_fgt_control.submit_hook.original)(queue, count, subs, fence);
    fso::Submit observation;
    FgtLock(); g_fgt_observer.BeginSubmit(observation, reinterpret_cast<uint64_t>(queue), count, fence, false); FgtUnlock();
    FgtObserveSubmit(&observation, subs, nullptr);
    FgtLock(); g_fgt_observer.Claim(observation); FgtUnlock();
    FgtNoteSubmit(queue, count, subs, nullptr, fence, false, observation);
    // Borrowed pointers and scalar arguments are forwarded exactly once, unchanged.
    // No observation/log lock spans this call (including any ReShade callbacks).
    const uint32_t result = reinterpret_cast<PFN_FgtQueueSubmit>(g_fgt_control.submit_hook.original)(queue, count, subs, fence);
    FgtFinishObservedSubmit(&observation, result);
    g_fgt_in_submit_observer = false;
    return result;
}

static uint32_t FgtQueueSubmit2Detour(void *queue, uint32_t count, const FgtSubmitInfo2 *subs, uint64_t fence)
{
    if (!FgtBeginSubmitObservation())
        return reinterpret_cast<PFN_FgtQueueSubmit2>(g_fgt_control.submit2_hook.original)(queue, count, subs, fence);
    fso::Submit observation;
    FgtLock(); g_fgt_observer.BeginSubmit(observation, reinterpret_cast<uint64_t>(queue), count, fence, true); FgtUnlock();
    FgtObserveSubmit(&observation, nullptr, subs);
    FgtLock(); g_fgt_observer.Claim(observation); FgtUnlock();
    FgtNoteSubmit(queue, count, nullptr, subs, fence, true, observation);
    const uint32_t result = reinterpret_cast<PFN_FgtQueueSubmit2>(g_fgt_control.submit2_hook.original)(queue, count, subs, fence);
    FgtFinishObservedSubmit(&observation, result);
    g_fgt_in_submit_observer = false;
    return result;
}

// ---------------------------------------------------------------------------
// ReShade's queues: which VkQueue it presents on and which types it knows.
static void OnFgtInitCommandQueue(reshade::api::command_queue *queue)
{
    if (queue == nullptr || queue->get_device()->get_api() != reshade::api::device_api::vulkan) return;
    FgtLock();
    const int slot = FgtNote(g_fgt.reshade_queues, queue->get_native());
    if (slot >= 0 && slot < static_cast<int>(_countof(g_fgt.reshade_queue_types)))
        g_fgt.reshade_queue_types[slot] = static_cast<unsigned>(queue->get_type());
    FgtUnlock();
    FgtLog("[fg-trace] ReShade init_command_queue native=%llx type=0x%x thread=%lu",
        queue->get_native(), static_cast<unsigned>(queue->get_type()), GetCurrentThreadId());
}

// Asynchronous installation may miss queue retrievals; an unknown family must remain unknown.
typedef void (*PFN_FgtGetDeviceQueue)(void *, uint32_t, uint32_t, void **);
typedef void (*PFN_FgtGetDeviceQueue2)(void *, const void *, void **);
struct FgtQueueId { uint64_t queue; uint32_t family, index; };
static FgtQueueId g_fgt_queue_ids[16];
static unsigned g_fgt_queue_id_count;

static void FgtNoteQueueId(void *queue, uint32_t family, uint32_t index, const char *via)
{
    FgtLock();
    for (unsigned i = 0; i < g_fgt_queue_id_count; ++i) {
        const FgtQueueId &q = g_fgt_queue_ids[i];
        if (q.queue == reinterpret_cast<uint64_t>(queue) && q.family == family && q.index == index) { FgtUnlock(); return; }
    }
    const bool stored = g_fgt_queue_id_count < _countof(g_fgt_queue_ids);
    if (stored) {
        FgtQueueId &q = g_fgt_queue_ids[g_fgt_queue_id_count++];
        q.queue = reinterpret_cast<uint64_t>(queue); q.family = family; q.index = index;
    }
    FgtUnlock();
    if (stored) FgtLog("[fg-trace] %s family=%u index=%u -> queue=%p thread=%lu", via, family, index, queue, GetCurrentThreadId());
}

static void FgtGetDeviceQueueDetour(void *dev, uint32_t family, uint32_t index, void **queue)
{
    reinterpret_cast<PFN_FgtGetDeviceQueue>(g_fgt_control.get_queue_hook.original)(dev, family, index, queue);
    void *q = queue != nullptr ? *queue : nullptr;
    FgRelayNoteQueue(dev, q, family);
    FgtNoteQueueId(q, family, index, "vkGetDeviceQueue");
}

static void FgtGetDeviceQueue2Detour(void *dev, const void *info, void **queue)
{
    reinterpret_cast<PFN_FgtGetDeviceQueue2>(g_fgt_control.get_queue2_hook.original)(dev, info, queue);
    const uint32_t *u = static_cast<const uint32_t *>(info);
    void *q = queue != nullptr ? *queue : nullptr;
    FgRelayNoteQueue(dev, q, u != nullptr ? u[5] : 0xFFFFFFFFu);
    FgtNoteQueueId(q, u != nullptr ? u[5] : 0xFFFFFFFFu, u != nullptr ? u[6] : 0xFFFFFFFFu, "vkGetDeviceQueue2");
}

static void FgtGetDeviceQueueLayerDetour(void *dev, uint32_t family, uint32_t index, void **queue)
{
    reinterpret_cast<PFN_FgtGetDeviceQueue>(g_fgt_control.get_queue_layer_hook.original)(dev, family, index, queue);
    void *q = queue != nullptr ? *queue : nullptr;
    FgRelayNoteQueue(dev, q, family);
    FgtNoteQueueId(q, family, index, "vkGetDeviceQueue(layer)");
}

static void FgtPublishTarget(void *volatile *slot, void *target)
{
    if (target == nullptr || !PinHookModule(target)) return;
    void *const previous = InterlockedCompareExchangePointer(slot, target, nullptr);
    if (previous != nullptr && previous != target)
        FgtLog("[fg-trace] additional dispatch target %p not observed (retaining %p)", target, previous);
    if (previous == nullptr) TriggerHookScan();
}

static void OnFgtInitDevice(reshade::api::device *dev)
{
    if (dev == nullptr || dev->get_api() != reshade::api::device_api::vulkan) return;
    auto gdpa = g_reshade_dll != nullptr
        ? reinterpret_cast<PFN_SVkGetDeviceProcAddr>(GetProcAddress(g_reshade_dll, "vkGetDeviceProcAddr")) : nullptr;
    if (gdpa != nullptr)
        FgtPublishTarget(&g_fgt_queue_target, gdpa(reinterpret_cast<SVkDevice>(static_cast<uintptr_t>(dev->get_native())), "vkGetDeviceQueue"));
}

static void FgtOnRuntime(reshade::api::effect_runtime *rt)
{
    if (!FgtHooksWanted() || rt == nullptr) return;
    auto *dev = rt->get_device();
    if (dev == nullptr || dev->get_api() != reshade::api::device_api::vulkan) return;
    FocusFgSetRuntime(rt);
    // The queue-discovery target is what gives the relay the family of the queues
    // it submits on, so it is published whether or not the trace is on.
    OnFgtInitDevice(dev);
    if (!FgtEnabled()) return;
    HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
    auto gdpa = loader != nullptr
        ? reinterpret_cast<PFN_SVkGetDeviceProcAddr>(GetProcAddress(loader, "vkGetDeviceProcAddr")) : nullptr;
    if (gdpa != nullptr) {
        auto native = reinterpret_cast<SVkDevice>(static_cast<uintptr_t>(dev->get_native()));
        void *submit2 = gdpa(native, "vkQueueSubmit2");
        if (submit2 == nullptr) submit2 = gdpa(native, "vkQueueSubmit2KHR");
        FgtPublishTarget(&g_fgt_submit2_target, submit2);
        FgtPublishTarget(&g_fgt_submit_target, gdpa(native, "vkQueueSubmit"));
    }
    auto *rq = rt->get_command_queue();
    const uint32_t n = rt->get_back_buffer_count();
    char list[512] = {}; size_t len = 0;
    for (uint32_t i = 0; i < n && i < 16; ++i)
        FgCensusAppend(list, sizeof(list), &len, "%s%llx", i ? " " : "", rt->get_back_buffer(i).handle);
    static volatile LONG generation;
    FgtLog("[fg-trace] runtime generation=%ld device=%llx present queue=%llx type=0x%x back buffers=%u [%s]",
        InterlockedIncrement(&generation), dev->get_native(), rq != nullptr ? rq->get_native() : 0ull,
        rq != nullptr ? static_cast<unsigned>(rq->get_type()) : 0u, n, list);
}

static void FgtInit()
{
    FgRelayInit();
    if (g_fgt_control.cs_ready) return;
    InitializeCriticalSection(&g_fgt_control.cs);
    g_fgt_control.cs_ready = true;
    g_fgt.cmdqueue_detail_left = 24;
    g_fgt.present_queue_detail_left = 8;
    g_fgt.eval_thread_detail_left = 12;
}

static void FgtRegisterEvents(PFN_ReShadeRegisterEvent reg)
{
    if (!FgtHooksWanted() || reg == nullptr) return;
    FgtInit();
    if (!FgtEnabled()) return;   // relay-only: nothing to register, the hooks come from the worker
    FgtRegisterTyped<reshade::addon_event::init_command_queue>(reg, &OnFgtInitCommandQueue);
    FgtRegisterCommandEvents(reg);
    FgtLog("[fg-trace] Trace=1: asynchronous log; hook installation deferred to the NGX hook worker. Early calls may be missed.");
}

// Only HookWorkerProc calls this; rendering callbacks publish pinned code addresses, never device objects.
static void FgtMaintenance()
{
    PresentCompatibilityPoll();
    if (!FgtHooksWanted() || !g_fgt_control.cs_ready) return;
    const bool trace = FgtEnabled();
    FgtControl &t = g_fgt_control;
    if (TryEnterCriticalSection(&g_hook_cs)) {
        HMODULE sl = nullptr;
        if ((!t.early_tried || !t.queue_hooks_tried) &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"sl.interposer.dll", &sl)) {
            if (!t.early_tried) {
                t.early_tried = true;
                const bool a = HookInstall(t.present_hook, reinterpret_cast<void *>(GetProcAddress(sl, "vkQueuePresentKHR")), reinterpret_cast<void *>(&FgtPresentDetour));
                bool b = false, c = false;
                if (trace) {
                    b = HookInstall(t.tag_hook, reinterpret_cast<void *>(GetProcAddress(sl, "slSetTag")), reinterpret_cast<void *>(&FgtSetTagDetour));
                    c = HookInstall(t.tag_frame_hook, reinterpret_cast<void *>(GetProcAddress(sl, "slSetTagForFrame")), reinterpret_cast<void *>(&FgtSetTagForFrameDetour));
                }
                FgtLog("%s: present=%s%s", trace ? "[fg-trace] interposer observers" : "[fg-relay] interposer hook",
                    a ? "hooked" : "FAILED", trace ? (b && c ? " tag=hooked tag_frame=hooked" : " tag/tag_frame FAILED") : "");
            }
            if (!t.queue_hooks_tried) {
                t.queue_hooks_tried = true;
                const bool a = HookInstall(t.get_queue_hook, reinterpret_cast<void *>(GetProcAddress(sl, "vkGetDeviceQueue")), reinterpret_cast<void *>(&FgtGetDeviceQueueDetour));
                const bool b = HookInstall(t.get_queue2_hook, reinterpret_cast<void *>(GetProcAddress(sl, "vkGetDeviceQueue2")), reinterpret_cast<void *>(&FgtGetDeviceQueue2Detour));
                FgtLog("[fg-trace] queue discovery exports: get_queue=%s get_queue2=%s", a ? "hooked" : "FAILED", b ? "hooked" : "FAILED");
            }
        }
        HMODULE reflex = nullptr;
        if (!t.sleep_tried && PresentAdapterRelayThrottle() > 0 &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"sl.reflex.dll", &reflex)) {
            t.sleep_tried = true;
            typedef void *(*PFN_FgtSlGetPluginFunction)(const char *);
            auto get = reinterpret_cast<PFN_FgtSlGetPluginFunction>(GetProcAddress(reflex, "slGetPluginFunction"));
            void *sleep = get != nullptr ? get("slReflexSleep") : nullptr;
            const bool ok = sleep != nullptr &&
                HookInstall(t.sleep_hook, sleep, reinterpret_cast<void *>(&FgtReflexSleepDetour));
            FgtLog("[fg-relay] Reflex sleep hook: slGetPluginFunction=%p slReflexSleep=%p %s",
                reinterpret_cast<void *>(get), sleep, ok ? "hooked" : "FAILED (the present-side throttle is the only one)");
        }
        void *queue = InterlockedCompareExchangePointer(&g_fgt_queue_target, nullptr, nullptr);
        if (queue != nullptr && !t.layer_hook_tried) {
            t.layer_hook_tried = true;
            const bool ok = queue != t.get_queue_hook.target &&
                HookInstall(t.get_queue_layer_hook, queue, reinterpret_cast<void *>(&FgtGetDeviceQueueLayerDetour));
            FgtLog("[fg-trace] layer queue discovery: %p %s", queue, ok ? "hooked" : "unavailable/duplicate");
        }
        void *submit = InterlockedCompareExchangePointer(&g_fgt_submit_target, nullptr, nullptr);
        if (trace && submit != nullptr && !t.late_tried) {
            t.late_tried = true;
            void *submit2 = InterlockedCompareExchangePointer(&g_fgt_submit2_target, nullptr, nullptr);
            const bool a = HookInstall(t.submit_hook, submit, reinterpret_cast<void *>(&FgtQueueSubmitDetour));
            const bool b = submit2 != nullptr && submit2 != submit && HookInstall(t.submit2_hook, submit2, reinterpret_cast<void *>(&FgtQueueSubmit2Detour));
            FgtLock(); g_fgt_submit_observed = a; g_fgt_submit2_observed = b; FgtUnlock();
            FgtLog("[fg-trace] queue observers: submit=%p %s submit2=%p %s", submit, a ? "hooked" : "FAILED", submit2, b ? "hooked" : "unavailable");
            FgtLog("[fg-submit-observer] submit-coverage=%s submit2-coverage=%s; selected-dispatch-targets-only, early/other-target/secondary-only submissions may be missed; original waits/signals unchanged",
                a ? "hooked" : "unavailable", b ? "hooked" : "unavailable");
        }
        // The relay's own device-creation hook (Import=1 only; it logs its
        // verdict). Here rather than at the top because it is the same kind of
        // decision as the observers above -- a patch on a process-wide entry
        // point -- and belongs behind the same lock.
        FgRelayInstallDeviceHook();
        FocusFgMaintenance();
        LeaveCriticalSection(&g_hook_cs);
    } else InterlockedExchange(&g_scan_pending, 1);
    FgtFlushLog();
}

static void FgtSummary()
{
    FgTrace t;
    FgtQueueId queue_ids[16];
    unsigned queue_id_count, pending = 0, in_submit = 0;
    fso::Stats observer;
    bool events_registered, submit_hooked, submit2_hooked;
    FgtLock();
    t = g_fgt;
    queue_id_count = g_fgt_queue_id_count;
    memcpy(queue_ids, g_fgt_queue_ids, sizeof(queue_ids));
    observer = g_fgt_observer.Summary();
    events_registered = g_fgt_command_events_registered;
    submit_hooked = g_fgt_submit_observed; submit2_hooked = g_fgt_submit2_observed;
    for (unsigned i = 0; i < g_fgt_observer.SampleCount(); ++i) {
        const fso::Sample s = g_fgt_observer.GetSample(i);
        if (s.state == fso::State::pending) ++pending;
        if (s.state == fso::State::in_submit) ++in_submit;
    }
    FgtUnlock();
    FgtLog("[fg-submit-observer] summary coverage events_registered=%u submit_hooked=%u submit2_hooked=%u "
        "init=%llu begin/reset=%llu destroy=%llu device_destroy=%llu events=%llu partial_starts=%llu capacity_misses=%llu generation_exhausted=%llu "
        "firsts=%llu observed_prefix=%llu untracked=%llu partial=%llu; no-prefix-is-not-empty/native-coverage-incomplete",
        events_registered ? 1u : 0u, submit_hooked ? 1u : 0u, submit2_hooked ? 1u : 0u,
        observer.inits, observer.resets, observer.destroys, observer.device_destroys, observer.events,
        observer.partial_starts, observer.capacity_misses, observer.generation_exhausted,
        observer.firsts, observer.prefixes, observer.untracked_firsts, observer.partial_firsts);
    FgtLog("[fg-submit-observer] summary submits=%llu submit2=%llu batches_read=%llu cbs_read=%llu read_faults=%llu nested_suppressed=%llu "
        "sample_returns=%llu handle_only=%llu pending=%u in_submit=%u invalidated=%llu duplicates=%llu locations_omitted=%llu nonzero_results=%llu log_dropped_all_trace=%ld; "
        "detail_limit=8-firsts/8-locations-each; unobserved-submit-is-not-no-submit; success-is-not-GPU-readiness",
        observer.submits, observer.submits2, observer.batches, observer.cbs, observer.read_faults, observer.nested_suppressed,
        observer.returned, observer.handle_only, pending, in_submit, observer.invalidated,
        observer.duplicate_occurrences, observer.omitted_locations, observer.failed_results, InterlockedCompareExchange(&g_fgt_log_dropped, 0, 0));
    char a[512], b[256];
    FgtLog("[fg-trace] ---- summary at %u FG groups / %u evaluates / %u game presents; log_dropped=%ld ----",
        t.eval_groups, t.eval_calls, t.present_calls, InterlockedCompareExchange(&g_fgt_log_dropped, 0, 0));
    FgtStatText(t.present_duration, a, sizeof(a));
    FgtLog("[fg-trace] game present CPU call duration %s (not input-to-display latency)", a);
    FgtStatText(t.present_interval, b, sizeof(b));
    FgtSetText(t.present_threads, a, sizeof(a));
    FgtLog("[fg-trace] game present: queue=%llx interval %s threads %s", t.present_queue, b, a);
    FgtSetText(t.present_swapchains, a, sizeof(a)); FgtLog("[fg-trace] game present swapchains %s", a);
    FgtSetText(t.present_indices, a, sizeof(a));    FgtLog("[fg-trace] game present image indices %s", a);
    FgtSetText(t.present_wait_counts, a, sizeof(a)); FgtSetText(t.present_pnext_types, b, sizeof(b));
    FgtLog("[fg-trace] game present wait counts %s pNext sTypes %s", a, b);
    FgtStatText(t.present_to_eval, a, sizeof(a)); FgtStatText(t.eval_interval, b, sizeof(b));
    FgtLog("[fg-trace] evaluate idx1: thread=%lu cmd_queue=%llx latest-present age (unpaired) %s | evaluate interval %s", t.eval_thread, t.cmd_queue, a, b);
    FgtStatText(t.eval_group_span, a, sizeof(a)); FgtSetText(t.eval_indices, b, sizeof(b));
    FgtLog("[fg-trace] evaluate group span (idx1->last) %s indices %s", a, b);
    FgtSetText(t.eval_threads, a, sizeof(a)); FgtLog("[fg-trace] evaluate threads %s", a);
    FgtSetText(t.hud, a, sizeof(a));        FgtLog("[fg-trace] handles HUDLess    %s", a);
    FgtSetText(t.back, a, sizeof(a));       FgtLog("[fg-trace] handles Backbuffer %s", a);
    FgtSetText(t.depth, a, sizeof(a));      FgtLog("[fg-trace] handles Depth      %s", a);
    FgtSetText(t.mv, a, sizeof(a));         FgtLog("[fg-trace] handles MVecs      %s", a);
    FgtSetText(t.out_real, a, sizeof(a));   FgtLog("[fg-trace] handles OutputReal %s", a);
    FgtSetText(t.out_interp, a, sizeof(a)); FgtLog("[fg-trace] handles OutputInterpolated %s", a);
    FgtSetText(t.tag_threads, a, sizeof(a));
    FgtLog("[fg-trace] tags: slSetTag=%u slSetTagForFrame=%u unreadable=%u threads %s", t.tag_calls, t.tag_frame_calls, t.tag_bad_reads, a);
    for (unsigned k = 0; k < _countof(kFgtTagTypes); ++k) {
        const FgtTagType &tt = t.types[k];
        if (tt.count == 0) continue;
        FgtSetText(tt.natives, a, sizeof(a)); FgtSetText(tt.views, b, sizeof(b));
        FgtLog("[fg-trace] tag %-12s count=%u lifecycle now/present/evaluate/other=%u/%u/%u/%u cmd_nonnull=%u extent_nonzero=%u last state=%u %ux%u fmt=%u usage=0x%x natives %s views %s",
            kFgtTagNames[k], tt.count, tt.lifecycle[0], tt.lifecycle[1], tt.lifecycle[2], tt.lifecycle_other, tt.cmd_nonnull, tt.extent_nonzero,
            tt.state, tt.w, tt.h, tt.fmt, tt.usage, a, b);
    }
    if (t.tag_other_types.n != 0) { FgtSetText(t.tag_other_types, a, sizeof(a)); FgtLog("[fg-trace] tag other types %s", a); }
    for (unsigned i = 0; i < t.nqueues; ++i) {
        const FgtQueue &q = t.queues[i];
        FgtSetText(q.threads, a, sizeof(a));
        const char *label = q.queue == t.cmd_queue && t.cmd_queue != 0 ? " =DLSSG.CmdQueue" : "";
        const char *label2 = q.queue == t.present_queue && t.present_queue != 0 ? " =game present queue" : "";
        int rs = -1;
        for (unsigned r = 0; r < t.reshade_queues.n; ++r) if (t.reshade_queues.v[r] == q.queue) rs = static_cast<int>(r);
        char fam[48] = "family=? index=?";
        for (unsigned k = 0; k < queue_id_count; ++k)
            if (queue_ids[k].queue == q.queue) { _snprintf_s(fam, sizeof(fam), _TRUNCATE, "family=%u index=%u", queue_ids[k].family, queue_ids[k].index); break; }
        FgtLog("[fg-trace] queue %llx%s%s %s reshade_type=%s0x%x submits=%u (submit2 %u, timeline-preview-only %u, fenced %u) cbs=%llu waits=%llu signals=%llu threads %s",
            q.queue, label, label2, fam, rs >= 0 ? "" : "unknown ", rs >= 0 && rs < static_cast<int>(kFgtDistinct) ? t.reshade_queue_types[rs] : 0u,
            q.submits, q.submits2, q.timeline, q.fences, q.cbs, q.waits, q.signals, a);
    }
    if (t.queue_overflow != 0) FgtLog("[fg-trace] %u submits to queues beyond the table", t.queue_overflow);
}

// Bounded CPU metadata around queue/focus/reset transitions. No texture readback
// or GPU commands are added. Logs describe recorded work, not displayed pixels.
struct FgtTransitionSample {
    bool active;
    unsigned index, reset, count, enabled, recording, not_rendering, metering, fullscreen, mode, known;
    void *queue, *frame_id;
    uint64_t hud, back, real, interp;
    LONGLONG begin;
};
static FgtTransitionSample FgtTransitionBegin(const NVSDK_NGX_Parameter *p)
{
    FgtTransitionSample s = {};
    if (g_cfg.stage < 2 || !PresentAdapterFocusKeepFG()) return s;
    static thread_local void *last_queue;
    static thread_local bool have, last_focus;
    static thread_local unsigned last_reset, remaining, bursts;
    p->Get("DLSSG.CmdQueue", &s.queue);
    if (FgInputReadU(p, "DLSSG.Reset", &s.reset)) s.known |= 1u;
    const bool focus = ForegroundIsThisProcess();
    const bool changed = (have && (last_queue != s.queue || last_focus != focus)) || (s.reset && !last_reset);
    if (changed && bursts < 128) { remaining = 12; ++bursts; }
    have = true; last_queue = s.queue; last_focus = focus; last_reset = s.reset;
    if (remaining == 0) return s;
    --remaining; s.active = true; s.begin = FgtNow();
    const char *keys[] = {"DLSSG.MultiFrameIndex", "DLSSG.MultiFrameCount", "DLSSG.EnableInterp",
        "DLSSG.IsRecording", "DLSSG.NotRenderingGameFrames", "DLSSG.FlipMetering", "DLSSG.FullscreenMode", "DLSSG.StreamlineMode"};
    unsigned *values[] = {&s.index, &s.count, &s.enabled, &s.recording, &s.not_rendering, &s.metering, &s.fullscreen, &s.mode};
    for (unsigned i = 0; i < _countof(keys); ++i)
        if (FgInputReadU(p, keys[i], values[i])) s.known |= 2u << i;
    p->Get("DLSSG.BackbufferFrameID", &s.frame_id);
    const char *why = nullptr;
    VkmRes r = {};
    if (VkmGetRes(p, "DLSSG.HUDLess", &r, &why)) s.hud = r.image;
    if (VkmGetRes(p, "DLSSG.Backbuffer", &r, &why)) s.back = r.image;
    if (VkmGetRes(p, "DLSSG.OutputReal", &r, &why)) s.real = r.image;
    if (VkmGetRes(p, "DLSSG.OutputInterpolated", &r, &why)) s.interp = r.image;
    return s;
}
static void FgtTransitionEnd(const FgtTransitionSample &s, const NVSDK_NGX_Parameter *p,
    NVSDK_NGX_Result result, bool entered, LONG armed_before, LONG armed_after)
{
    if (!s.active) return;
    unsigned disable = 0;
    const bool disable_known = FgInputReadU(p, "DLSSG.OutputDisableInterpolation", &disable);
    const FocusFgWindow window = FocusFgWindowSnapshot();
    const HMONITOR game_monitor = MonitorFromWindow(window.window, MONITOR_DEFAULTTONULL);
    const HMONITOR focus_monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONULL);
    FgtLog("[fg-transition] start=%lld frame=%p idx=%u/%u queue=%p reset=%u enable=%u recording=%u not-rendering=%u "
        "metering=%u fullscreen=%u slmode=%u known=0x%x hud=%llx back=%llx real=%llx interp=%llx "
        "bridge-lock=%u arm=%ld->%ld result=%d disable-interp=%u(known=%u) foreground=%u monitors=%p/%p cpu=%.3fms",
        s.begin, s.frame_id, s.index, s.count, s.queue, s.reset, s.enabled, s.recording, s.not_rendering,
        s.metering, s.fullscreen, s.mode, s.known, s.hud, s.back, s.real, s.interp,
        entered ? 1u : 0u, armed_before, armed_after, result, disable, disable_known ? 1u : 0u,
        ForegroundIsThisProcess() ? 1u : 0u, game_monitor, focus_monitor, FgtMs(s.begin, FgtNow()));
}

// Every FG evaluate, on Streamline's thread, before the input feed looks at it.
static void FgTraceEvaluate(void *cmd, const NVSDK_NGX_Parameter *p)
{
    InterlockedIncrement(&g_focus_fg_evaluates);
    if (!FgtEnabled() || !g_fgt_control.cs_ready) return;
    FgTrace &t = g_fgt;
    const LONGLONG now = FgtNow();
    unsigned idx = 0;
    const bool indexed = FgInputReadU(p, "DLSSG.MultiFrameIndex", &idx);
    void *queue = nullptr;
    p->Get("DLSSG.CmdQueue", &queue);
    const char *why = nullptr;
    VkmRes hud = {}, back = {}, mv = {}, depth = {}, oreal = {}, ointerp = {};
    const bool have_hud = VkmGetRes(p, "DLSSG.HUDLess", &hud, &why);
    const bool have_back = VkmGetRes(p, "DLSSG.Backbuffer", &back, &why);
    const bool have_mv = VkmGetRes(p, "DLSSG.MVecs", &mv, &why);
    const bool have_depth = VkmGetRes(p, "DLSSG.Depth", &depth, &why);
    const bool have_real = VkmGetRes(p, "DLSSG.OutputReal", &oreal, &why);
    const bool have_interp = VkmGetRes(p, "DLSSG.OutputInterpolated", &ointerp, &why);
    const bool first = indexed ? idx == 1 : have_real;
    fso::Sample first_sample;
    unsigned groups = 0, calls = 0;
    double since_present = -1.0;
    FgtLock();
    const LONGLONG present = t.last_present_qpc;
    calls = ++t.eval_calls;
    t.cmd_queue = reinterpret_cast<uint64_t>(queue);
    t.eval_thread = GetCurrentThreadId();
    FgtNote(t.eval_threads, GetCurrentThreadId());
    FgtNote(t.eval_indices, idx);
    if (first) {
        ++t.eval_groups;
        t.last_fg_cmd = cmd;
        const fso::Images images = { have_hud ? hud.image : 0ull, have_back ? back.image : 0ull,
            have_depth ? depth.image : 0ull, have_mv ? mv.image : 0ull,
            (have_hud ? 1u : 0u) | (have_back ? 2u : 0u) | (have_depth ? 4u : 0u) | (have_mv ? 8u : 0u) };
        first_sample = g_fgt_observer.First(reinterpret_cast<uint64_t>(cmd), reinterpret_cast<uint64_t>(queue),
            images, static_cast<uint64_t>(now), idx, indexed);
        if (present != 0 && now >= present) { since_present = FgtMs(present, now); FgtStatAdd(t.present_to_eval, since_present); }
        if (t.last_eval1_qpc != 0) FgtStatAdd(t.eval_interval, FgtMs(t.last_eval1_qpc, now));
        t.last_eval1_qpc = now;
        t.group_first_qpc = now;
        if (have_hud) FgtNote(t.hud, hud.image);
        if (have_back) FgtNote(t.back, back.image);
        if (have_mv) FgtNote(t.mv, mv.image);
        if (have_depth) FgtNote(t.depth, depth.image);
        if (have_real) FgtNote(t.out_real, oreal.image);
    } else if (t.group_first_qpc != 0) {
        FgtStatAdd(t.eval_group_span, FgtMs(t.group_first_qpc, now));
    }
    if (have_interp) FgtNote(t.out_interp, ointerp.image);
    groups = t.eval_groups;
    FgtUnlock();
    FgtLogFirstSample(first_sample);
    if (groups <= 3) {
        FgtLog("[fg-trace] evaluate #%u idx=%u%s thread=%lu cmd=%p queue=%p since_present=%.2fms hud=%llx back=%llx depth=%llx mv=%llx real=%llx interp=%llx",
            calls, idx, indexed ? "" : "(unindexed)", GetCurrentThreadId(), cmd, queue,
            first ? since_present : (present != 0 ? FgtMs(present, now) : -1.0),
            have_hud ? hud.image : 0ull, have_back ? back.image : 0ull, have_depth ? depth.image : 0ull,
            have_mv ? mv.image : 0ull, have_real ? oreal.image : 0ull, have_interp ? ointerp.image : 0ull);
    }
    if (first && (groups == 60 || groups % 600 == 0)) FgtSummary();
}
