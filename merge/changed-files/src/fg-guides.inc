// Frame-generation guides are defined on their own grids. A DLSS quality
// change can resize those grids without changing the final colour image.
// Keep the private carrier fixed-size and fill every guide pixel on every
// accepted frame; never reuse a previous frame's guide after a shape change.
#include "fg-guide-math.h"
#include "fg-history-policy.h"
static volatile LONG64 g_fg_source_tick, g_fg_source_reset_tick;
struct FgGuideImage {
    VkmRes resource;
    unsigned x, y, w, h;
};
struct FgGuideContract {
    FgGuideImage depth, mv;
    unsigned out_w, out_h, inverted, jittered, reset;
    float scale_x, scale_y, carrier_scale_x, carrier_scale_y, jitter_x, jitter_y;
};
struct FgGuideFormatProperties { uint32_t linear, optimal, buffer; };
struct FgGuideQueueProperties { uint32_t flags, count, timestamp; SVkExtent3D granularity; };
struct FgGuideMemoryProperties {
    uint32_t type_count;
    struct { uint32_t flags, heap; } types[32];
    uint32_t heap_count;
    struct { uint64_t size; uint32_t flags; } heaps[16];
};
static_assert(sizeof(FgGuideMemoryProperties) == 520, "VkPhysicalDeviceMemoryProperties ABI");
struct FgGuideDevice {
    void *device;
    bool blit_formats;
    uint64_t graphics_families;
    uint64_t compute_families;
    uint32_t local_memory_types;
};
static std::unordered_map<void *, FgGuideDevice> g_fg_guide_devices;
static std::mutex g_fg_guide_device_lock;
static bool FgGuideComputePrepare(uint64_t bytes, uint32_t local_types, uint64_t queue_families);
static void FgGuideComputeRecord(SVkCommandBuffer cb, const FgGuideImage &g, SVkHandle dst, unsigned w, unsigned h);
static uint32_t FgGuideSharingFamilies(void *device, uint32_t *families, uint32_t capacity)
{
    std::lock_guard<std::mutex> lock(g_fg_guide_device_lock);
    const auto found = g_fg_guide_devices.find(device);
    if (found == g_fg_guide_devices.end()) return 0;
    const uint64_t mask = found->second.graphics_families | found->second.compute_families;
    uint32_t count = 0;
    for (uint32_t i = 0; i < 64; ++i)
        if ((mask & (uint64_t(1) << i)) != 0) {
            if (count >= capacity) return 0;
            families[count++] = i;
        }
    return count;
}
static void FgGuideForgetDevice(void *device)
{
    std::lock_guard<std::mutex> lock(g_fg_guide_device_lock);
    g_fg_guide_devices.erase(device);
}

// The device-creation hook is the only place the bridge knows the physical
// device. Resolve format/queue capability here, before accepting any blit.
using PFN_FgGuideGipa = void *(*)(void *, const char *);
static void FgGuideNoteDevice(void *instance, void *physical, void *device, PFN_FgGuideGipa layer_gipa)
{
    using GetFormats = void (*)(void *, uint32_t, FgGuideFormatProperties *);
    using GetQueues = void (*)(void *, uint32_t *, FgGuideQueueProperties *);
    using GetMemory = void (*)(void *, FgGuideMemoryProperties *);
    // The layer's physical handle is not the loader's top-level wrapper. Only
    // resolve queries through that SAME layer and its captured real instance;
    // a vulkan-1.dll global trampoline rejects this domain and can fail-fast.
    auto get_formats = instance != nullptr && layer_gipa != nullptr
        ? reinterpret_cast<GetFormats>(layer_gipa(instance, "vkGetPhysicalDeviceFormatProperties")) : nullptr;
    auto get_queues = instance != nullptr && layer_gipa != nullptr
        ? reinterpret_cast<GetQueues>(layer_gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties")) : nullptr;
    auto get_memory = instance != nullptr && layer_gipa != nullptr
        ? reinterpret_cast<GetMemory>(layer_gipa(instance, "vkGetPhysicalDeviceMemoryProperties")) : nullptr;
    FgGuideDevice record = {};
    record.device = device;
    if (get_formats != nullptr && get_queues != nullptr && physical != nullptr) {
        FgGuideFormatProperties depth = {}, mv = {};
        get_formats(physical, 100u, &depth); // VK_FORMAT_R32_SFLOAT
        get_formats(physical, 83u, &mv);     // VK_FORMAT_R16G16_SFLOAT
        const uint32_t blit = 0x400u | 0x800u; // BLIT_SRC_BIT | BLIT_DST_BIT
        record.blit_formats = (depth.optimal & blit) == blit && (mv.optimal & blit) == blit;
        FgGuideQueueProperties queues[64] = {};
        uint32_t count = _countof(queues);
        get_queues(physical, &count, queues);
        for (uint32_t i = 0; i < count && i < _countof(queues); ++i) {
            if ((queues[i].flags & 1u) != 0) record.graphics_families |= uint64_t(1) << i;
            if ((queues[i].flags & 2u) != 0) record.compute_families |= uint64_t(1) << i;
        }
        if (get_memory != nullptr) {
            FgGuideMemoryProperties memory = {};
            get_memory(physical, &memory);
            for (uint32_t i = 0; i < memory.type_count && i < 32; ++i)
                if ((memory.types[i].flags & 1u) != 0) record.local_memory_types |= 1u << i;
        }
    }
    Log("[fg-guides] Vulkan capability device=%p instance=%p physical=%p queries=%u blit-formats=%u graphics-families=0x%llX. compute-families=0x%llX local-types=0x%X.",
        device, instance, physical, get_formats != nullptr && get_queues != nullptr ? 1u : 0u,
        record.blit_formats ? 1u : 0u, record.graphics_families, record.compute_families, record.local_memory_types);
    std::lock_guard<std::mutex> lock(g_fg_guide_device_lock);
    g_fg_guide_devices[device] = record;
}

static bool FgGuideReadImage(const NVSDK_NGX_Parameter *p, const char *key,
                             const char *base_x, const char *base_y,
                             const char *width, const char *height,
                             uint32_t format, FgGuideImage *out, const char **why)
{
    if (!VkmGetRes(p, key, &out->resource, why)) return false;
    const VkmRes &r = out->resource;
    if (r.fmt != format || r.aspect != kSVkAspectColor || r.mip != 0 || r.layer != 0) {
        *why = "guide format/aspect/mip/layer is unsupported"; return false;
    }
    out->x = out->y = 0;
    out->w = r.w; out->h = r.h;
    FgInputReadU(p, base_x, &out->x); FgInputReadU(p, base_y, &out->y);
    // Explicit zero sizes are not whole-image declarations. Treat them as a
    // temporarily invalid contract, so a transition cannot read stale pixels.
    FgInputReadU(p, width, &out->w); FgInputReadU(p, height, &out->h);
    if (!FgGuideRectValid(r.w, r.h, out->x, out->y, out->w, out->h)) {
        *why = "guide subrect is empty or outside its texture"; return false;
    }
    return true;
}

static bool FgGuideRead(const NVSDK_NGX_Parameter *p, const VkmRes &back,
                        FgGuideContract *out, const char **why)
{
    *out = {};
    out->out_w = back.w; out->out_h = back.h;
    if (back.w == 0 || back.h == 0 || back.w > INT_MAX || back.h > INT_MAX) {
        *why = "output dimensions are invalid"; return false;
    }
    if (!FgGuideReadImage(p, "DLSSG.Depth", "DLSSG.DepthSubrectBaseX", "DLSSG.DepthSubrectBaseY",
                         "DLSSG.DepthSubrectWidth", "DLSSG.DepthSubrectHeight", 100u, &out->depth, why) ||
        !FgGuideReadImage(p, "DLSSG.MVecs", "DLSSG.MVecsSubrectBaseX", "DLSSG.MVecsSubrectBaseY",
                         "DLSSG.MVecsSubrectWidth", "DLSSG.MVecsSubrectHeight", 83u, &out->mv, why)) return false;
    FgInputReadU(p, "DLSSG.DepthInverted", &out->inverted);
    FgInputReadU(p, "DLSSG.MvecJittered", &out->jittered);
    FgInputReadU(p, "DLSSG.Reset", &out->reset);
    out->scale_x = out->scale_y = 1.0f;
    p->Get("DLSSG.MvecScaleX", &out->scale_x); p->Get("DLSSG.MvecScaleY", &out->scale_y);
    p->Get("DLSSG.JitterOffsetX", &out->jitter_x); p->Get("DLSSG.JitterOffsetY", &out->jitter_y);
    if (!FgGuideScale(out->scale_x, out->mv.w, out->out_w, &out->carrier_scale_x) ||
        !FgGuideScale(out->scale_y, out->mv.h, out->out_h, &out->carrier_scale_y) ||
        !std::isfinite(out->jitter_x) || !std::isfinite(out->jitter_y)) {
        *why = "motion scale or jitter is not finite"; return false;
    }
    if (out->jittered != 0) {
        *why = "jittered motion vectors require an explicit de-jitter conversion"; return false;
    }
    return true;
}

static bool FgGuideNeedsBlit(const FgGuideImage &image, unsigned w, unsigned h)
{
    return image.w != w || image.h != h;
}
static bool FgGuideCanTransport(const FgGuideContract &g, uint32_t family)
{
    if (!FgGuideNeedsBlit(g.depth, g.out_w, g.out_h) && !FgGuideNeedsBlit(g.mv, g.out_w, g.out_h)) return true;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(g_fg_guide_device_lock);
        const auto found = g_fg_guide_devices.find(g_svk.dev);
        if (found != g_fg_guide_devices.end()) {
            const FgGuideDevice &d = found->second;
            uint64_t bytes = 0;
            ok = family < 64 && (d.compute_families & (uint64_t(1) << family)) != 0 && d.local_memory_types != 0 &&
                FgGuideBufferBytes(g.out_w, g.out_h, &bytes) && FgGuideBufferBytes(g.depth.w, g.depth.h, &bytes) &&
                FgGuideBufferBytes(g.mv.w, g.mv.h, &bytes);
        }
    }
    return ok;
}

static bool FgGuidePrepareTransport(const FgGuideContract &g, uint32_t family)
{
    if (!FgGuideCanTransport(g, family)) return false;
    if (!FgGuideNeedsBlit(g.depth, g.out_w, g.out_h) && !FgGuideNeedsBlit(g.mv, g.out_w, g.out_h)) return true;
    FgGuideDevice d = {};
    {
        std::lock_guard<std::mutex> lock(g_fg_guide_device_lock);
        const auto found = g_fg_guide_devices.find(g_svk.dev);
        if (found == g_fg_guide_devices.end()) return false;
        d = found->second;
    }
    uint64_t bytes = 0, temp = 0;
    if (!FgGuideBufferBytes(g.out_w, g.out_h, &bytes) || !FgGuideBufferBytes(g.depth.w, g.depth.h, &temp)) return false;
    if (temp > bytes) bytes = temp;
    if (!FgGuideBufferBytes(g.mv.w, g.mv.h, &temp)) return false;
    if (temp > bytes) bytes = temp;
    return FgGuideComputePrepare(bytes, d.local_memory_types, d.graphics_families | d.compute_families);
}

static void FgGuideRecord(SVkCommandBuffer cb, const FgGuideImage &g,
                          SVkHandle dst, unsigned w, unsigned h)
{
    if (!FgGuideNeedsBlit(g, w, h)) {
        SVkImageCopy copy = {};
        copy.srcSubresource = {kSVkAspectColor, 0, 0, 1};
        copy.dstSubresource = {kSVkAspectColor, 0, 0, 1};
        copy.srcOffset = {static_cast<int32_t>(g.x), static_cast<int32_t>(g.y), 0};
        copy.extent = {w, h, 1};
        g_svk.CmdCopyImage(cb, g.resource.image, kSVkLayoutTransferSrc, dst, kSVkLayoutGeneral, 1, &copy);
        return;
    }
    FgGuideComputeRecord(cb, g, dst, w, h);
}

static bool g_fg_guides_have_previous;
static volatile LONG g_fg_guides_pending_reset = 1;
static FgGuideContract g_fg_guides_previous;
static void FgGuideRequestReset()
{
    InterlockedExchange(&g_fg_guides_pending_reset, 1);
}
static void FgGuideObserveSourceReset(bool reset)
{
    const LONG64 now = static_cast<LONG64>(GetTickCount64());
    if (reset) {
        InterlockedExchange64(&g_fg_source_reset_tick, now);
        FgGuideRequestReset();
    }
    InterlockedExchange64(&g_fg_source_tick, now);
}
static void FgGuidePause(const char *why)
{
    static void *volatile last_reason;
    const void *previous = InterlockedExchangePointer(&last_reason, const_cast<char *>(why));
    if (previous != why || InterlockedCompareExchange(&g_fg_guides_pending_reset, 0, 0) == 0)
        Log("[fg-guides] paused for this frame: %s; valid inputs resume automatically.", why);
    FgGuideRequestReset();
}
static bool FgGuideSameImageGrid(const FgGuideImage &a, const FgGuideImage &b)
{
    return a.resource.w == b.resource.w && a.resource.h == b.resource.h && a.resource.fmt == b.resource.fmt &&
        a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
static bool FgGuideSameContract(const FgGuideContract &a, const FgGuideContract &b)
{
    return FgGuideSameImageGrid(a.depth, b.depth) && FgGuideSameImageGrid(a.mv, b.mv) &&
        a.out_w == b.out_w && a.out_h == b.out_h && a.inverted == b.inverted && a.jittered == b.jittered &&
        a.scale_x == b.scale_x && a.scale_y == b.scale_y;
}
// Called only once the worker is idle and this frame owns the shared contract.
static void FgGuideApply(SynthParams *sp, const FgGuideContract &g)
{
    const bool changed = !g_fg_guides_have_previous || !FgGuideSameContract(g, g_fg_guides_previous);
    const bool pending = InterlockedExchange(&g_fg_guides_pending_reset, 0) != 0;
    // FG resets remain intact. Display-path transitions require observations
    // of the FG outputs; suppressing the carrier's reset does not establish
    // continuity of the image actually presented by the FG runtime.
    const bool reset = changed || pending || g.reset != 0;
    static unsigned game_reset_frames;
    static ULONGLONG game_reset_started;
    if (g.reset != 0) {
        if (game_reset_frames++ == 0) {
            game_reset_started = GetTickCount64();
            DWORD foreground = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
            Log("[fg-history] FG Reset burst starts; contract_changed=%u bridge_pending=%u foreground=%u.",
                changed ? 1u : 0u, pending ? 1u : 0u, foreground == GetCurrentProcessId() ? 1u : 0u);
        }
    } else if (game_reset_frames != 0) {
        Log("[fg-history] FG Reset burst ends: %u real frames, %llu ms.", game_reset_frames, GetTickCount64() - game_reset_started);
        game_reset_frames = 0;
    }
    sp->Set("DLSS.Feature.Create.Flags", g.inverted != 0 ? 0x08u : 0u);
    sp->Set("MV.Scale.X", g.carrier_scale_x); sp->Set("MV.Scale.Y", g.carrier_scale_y);
    // HUDLess is already resolved by the game's SR. The unjittered guides and
    // the resolved carrier share a stable grid, so its synthetic SR jitter is 0.
    sp->Set("Jitter.Offset.X", 0.0f); sp->Set("Jitter.Offset.Y", 0.0f);
    sp->Set("Reset", reset ? 1 : 0);
    if (changed || pending) {
        Log("[fg-guides] output=%ux%u depth=%ux%u rect=%u,%u+%ux%u mv=%ux%u rect=%u,%u+%ux%u "
            "mv-scale=%.6g,%.6g carrier-scale=%.6g,%.6g jitter=%.6g,%.6g jittered=%u inverted=%u reset=%u gpu-nearest=%u/%u.",
            g.out_w, g.out_h, g.depth.resource.w, g.depth.resource.h, g.depth.x, g.depth.y, g.depth.w, g.depth.h,
            g.mv.resource.w, g.mv.resource.h, g.mv.x, g.mv.y, g.mv.w, g.mv.h,
            g.scale_x, g.scale_y, g.carrier_scale_x, g.carrier_scale_y,
            g.jitter_x, g.jitter_y, g.jittered, g.inverted, reset ? 1u : 0u,
            FgGuideNeedsBlit(g.depth, g.out_w, g.out_h) ? 1u : 0u, FgGuideNeedsBlit(g.mv, g.out_w, g.out_h) ? 1u : 0u);
    }
    g_fg_guides_previous = g;
    g_fg_guides_have_previous = true;
}
