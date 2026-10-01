// GPU-only nearest transport for compute-capable FG queues. Source images and
// imported D3D12 images remain transfer-only; raw 32-bit texels pass through two
// device-local storage buffers on the SAME queue and within the same copy batch.
#include "fg-guide-nearest-spv.h"

struct FgcShaderInfo { uint32_t type; const void *next; uint32_t flags; size_t bytes; const uint32_t *code; };
struct FgcBinding { uint32_t binding, type, count, stages; const SVkHandle *samplers; };
struct FgcSetLayoutInfo { uint32_t type; const void *next; uint32_t flags, count; const FgcBinding *bindings; };
struct FgcPushRange { uint32_t stages, offset, size; };
struct FgcLayoutInfo { uint32_t type; const void *next; uint32_t flags, count; const SVkHandle *sets; uint32_t push_count; const FgcPushRange *push; };
struct FgcStageInfo { uint32_t type; const void *next; uint32_t flags, stage; SVkHandle module; const char *name; const void *specialization; };
struct FgcPipelineInfo { uint32_t type; const void *next; uint32_t flags; FgcStageInfo stage; SVkHandle layout, base; int32_t base_index; };
struct FgcPoolSize { uint32_t type, count; };
struct FgcPoolInfo { uint32_t type; const void *next; uint32_t flags, max_sets, count; const FgcPoolSize *sizes; };
struct FgcSetInfo { uint32_t type; const void *next; SVkHandle pool; uint32_t count; const SVkHandle *layouts; };
struct FgcBufferInfo { SVkHandle buffer; uint64_t offset, range; };
struct FgcWrite { uint32_t type; const void *next; SVkHandle set; uint32_t binding, element, count, descriptor_type; const void *images; const FgcBufferInfo *buffers; const SVkHandle *views; };
static_assert(sizeof(FgcShaderInfo) == 40 && sizeof(FgcBinding) == 24 && sizeof(FgcSetLayoutInfo) == 32, "Vulkan shader/set ABI");
static_assert(sizeof(FgcLayoutInfo) == 48 && sizeof(FgcStageInfo) == 48 && sizeof(FgcPipelineInfo) == 96, "Vulkan compute pipeline ABI");
static_assert(sizeof(FgcPoolInfo) == 40 && sizeof(FgcSetInfo) == 40 && sizeof(FgcWrite) == 64, "Vulkan descriptor ABI");

using FgcDestroy = void (*)(SVkDevice, SVkHandle, const void *);
using FgcCreateShader = uint32_t (*)(SVkDevice, const FgcShaderInfo *, const void *, SVkHandle *);
using FgcCreateSetLayout = uint32_t (*)(SVkDevice, const FgcSetLayoutInfo *, const void *, SVkHandle *);
using FgcCreateLayout = uint32_t (*)(SVkDevice, const FgcLayoutInfo *, const void *, SVkHandle *);
using FgcCreatePipeline = uint32_t (*)(SVkDevice, SVkHandle, uint32_t, const FgcPipelineInfo *, const void *, SVkHandle *);
using FgcCreatePool = uint32_t (*)(SVkDevice, const FgcPoolInfo *, const void *, SVkHandle *);
using FgcAllocateSets = uint32_t (*)(SVkDevice, const FgcSetInfo *, SVkHandle *);
using FgcUpdateSets = void (*)(SVkDevice, uint32_t, const FgcWrite *, uint32_t, const void *);
using FgcBindPipeline = void (*)(SVkCommandBuffer, uint32_t, SVkHandle);
using FgcBindSets = void (*)(SVkCommandBuffer, uint32_t, SVkHandle, uint32_t, uint32_t, const SVkHandle *, uint32_t, const uint32_t *);
using FgcPush = void (*)(SVkCommandBuffer, SVkHandle, uint32_t, uint32_t, uint32_t, const void *);
using FgcDispatch = void (*)(SVkCommandBuffer, uint32_t, uint32_t, uint32_t);

struct FgGuideComputeState {
    SVkDevice device;
    SVkHandle buffers[2], memory[2], module, set_layout, layout, pipeline, pool, set;
    uint64_t capacity, retry_after;
    bool ready, growth_pending;
    PFN_SVkDestroyBuffer DestroyBuffer;
    PFN_SVkFreeMemory FreeMemory;
    FgcDestroy DestroyShader, DestroySetLayout, DestroyLayout, DestroyPipeline, DestroyPool;
    FgcBindPipeline BindPipeline;
    FgcBindSets BindSets;
    FgcPush Push;
    FgcDispatch Dispatch;
};
static FgGuideComputeState g_fgc;

// Either no command has used a failed partial build, or the caller has proved
// both queues drained. Never call this just because the next frame wants more.
static void FgGuideComputeRelease()
{
    FgGuideComputeState &c = g_fgc;
    if (c.device != nullptr) {
        if (c.pool && c.DestroyPool) c.DestroyPool(c.device, c.pool, nullptr);
        if (c.pipeline && c.DestroyPipeline) c.DestroyPipeline(c.device, c.pipeline, nullptr);
        if (c.layout && c.DestroyLayout) c.DestroyLayout(c.device, c.layout, nullptr);
        if (c.set_layout && c.DestroySetLayout) c.DestroySetLayout(c.device, c.set_layout, nullptr);
        if (c.module && c.DestroyShader) c.DestroyShader(c.device, c.module, nullptr);
        for (unsigned i = 0; i < 2; ++i) {
            if (c.buffers[i] && c.DestroyBuffer) c.DestroyBuffer(c.device, c.buffers[i], nullptr);
            if (c.memory[i] && c.FreeMemory) c.FreeMemory(c.device, c.memory[i], nullptr);
        }
    }
    c = {};
}

static bool FgGuideComputeFailure(const char *step, uint32_t result)
{
    Log("[fg-guides] GPU nearest setup failed at %s (VkResult=0x%08X); no CPU image conversion is used.", step, result);
    FgGuideComputeRelease();
    g_fgc.retry_after = GetTickCount64() + 2000;
    return false;
}

static bool FgGuideComputePrepare(uint64_t bytes, uint32_t local_types, uint64_t queue_mask)
{
    FgGuideComputeState &c = g_fgc;
    if (c.ready) {
        c.growth_pending = c.device == g_svk.dev && bytes > c.capacity;
        return c.device == g_svk.dev && bytes <= c.capacity;
    }
    if (bytes == 0 || bytes > 128ull * 1024 * 1024 || local_types == 0 || queue_mask == 0 ||
        GetTickCount64() < c.retry_after || g_svk.lib == nullptr || g_svk.dev == nullptr) return false;
    auto get = reinterpret_cast<PFN_SVkGetDeviceProcAddr>(GetProcAddress(g_svk.lib, "vkGetDeviceProcAddr"));
    if (get == nullptr) return FgGuideComputeFailure("vkGetDeviceProcAddr", 0xFFFFFFFFu);
    c.device = g_svk.dev;
    c.DestroyBuffer = g_svk.DestroyBuffer;
    c.FreeMemory = g_svk.FreeMemory;
    auto create_shader = reinterpret_cast<FgcCreateShader>(get(c.device, "vkCreateShaderModule"));
    auto create_set_layout = reinterpret_cast<FgcCreateSetLayout>(get(c.device, "vkCreateDescriptorSetLayout"));
    auto create_layout = reinterpret_cast<FgcCreateLayout>(get(c.device, "vkCreatePipelineLayout"));
    auto create_pipeline = reinterpret_cast<FgcCreatePipeline>(get(c.device, "vkCreateComputePipelines"));
    auto create_pool = reinterpret_cast<FgcCreatePool>(get(c.device, "vkCreateDescriptorPool"));
    auto allocate_sets = reinterpret_cast<FgcAllocateSets>(get(c.device, "vkAllocateDescriptorSets"));
    auto update_sets = reinterpret_cast<FgcUpdateSets>(get(c.device, "vkUpdateDescriptorSets"));
    c.DestroyShader = reinterpret_cast<FgcDestroy>(get(c.device, "vkDestroyShaderModule"));
    c.DestroySetLayout = reinterpret_cast<FgcDestroy>(get(c.device, "vkDestroyDescriptorSetLayout"));
    c.DestroyLayout = reinterpret_cast<FgcDestroy>(get(c.device, "vkDestroyPipelineLayout"));
    c.DestroyPipeline = reinterpret_cast<FgcDestroy>(get(c.device, "vkDestroyPipeline"));
    c.DestroyPool = reinterpret_cast<FgcDestroy>(get(c.device, "vkDestroyDescriptorPool"));
    c.BindPipeline = reinterpret_cast<FgcBindPipeline>(get(c.device, "vkCmdBindPipeline"));
    c.BindSets = reinterpret_cast<FgcBindSets>(get(c.device, "vkCmdBindDescriptorSets"));
    c.Push = reinterpret_cast<FgcPush>(get(c.device, "vkCmdPushConstants"));
    c.Dispatch = reinterpret_cast<FgcDispatch>(get(c.device, "vkCmdDispatch"));
    if (!create_shader || !create_set_layout || !create_layout || !create_pipeline || !create_pool || !allocate_sets || !update_sets ||
        !c.DestroyShader || !c.DestroySetLayout || !c.DestroyLayout || !c.DestroyPipeline || !c.DestroyPool ||
        !c.BindPipeline || !c.BindSets || !c.Push || !c.Dispatch || !c.DestroyBuffer || !c.FreeMemory ||
        !g_svk.CreateBuffer || !g_svk.GetBufferMemoryRequirements || !g_svk.AllocateMemory || !g_svk.BindBufferMemory ||
        !g_svk.CmdCopyImageToBuffer || !g_svk.CmdCopyBufferToImage || !g_svk.CmdPipelineBarrier)
        return FgGuideComputeFailure("required Vulkan compute/transfer entry point", 0xFFFFFFFFu);

    uint32_t families[64] = {}, family_count = 0;
    for (uint32_t i = 0; i < 64; ++i)
        if ((queue_mask & (uint64_t(1) << i)) != 0) families[family_count++] = i;
    for (unsigned i = 0; i < 2; ++i) {
        SVkBufferCreateInfo info = {};
        info.sType = 12; info.size = bytes; info.usage = 0x1u | 0x2u | 0x20u; // transfer src/dst, storage
        if (family_count > 1) { info.sharingMode = 1; info.queueFamilyIndexCount = family_count; info.pQueueFamilyIndices = families; }
        uint32_t result = g_svk.CreateBuffer(c.device, &info, nullptr, &c.buffers[i]);
        if (result != kSVkSuccess) return FgGuideComputeFailure("vkCreateBuffer", result);
        SVkMemoryRequirements required = {};
        g_svk.GetBufferMemoryRequirements(c.device, c.buffers[i], &required);
        const uint32_t allowed = required.memoryTypeBits & local_types;
        if (allowed == 0) return FgGuideComputeFailure("device-local buffer memory type", 0xFFFFFFFFu);
        uint32_t type = 0; while ((allowed & (1u << type)) == 0) ++type;
        SVkMemoryAllocateInfo allocate = {};
        allocate.sType = 5; allocate.allocationSize = required.size; allocate.memoryTypeIndex = type;
        result = g_svk.AllocateMemory(c.device, &allocate, nullptr, &c.memory[i]);
        if (result != kSVkSuccess) return FgGuideComputeFailure("vkAllocateMemory", result);
        result = g_svk.BindBufferMemory(c.device, c.buffers[i], c.memory[i], 0);
        if (result != kSVkSuccess) return FgGuideComputeFailure("vkBindBufferMemory", result);
    }
    FgcShaderInfo shader = {16, nullptr, 0, sizeof(kFgGuideNearestSpv), kFgGuideNearestSpv};
    uint32_t result = create_shader(c.device, &shader, nullptr, &c.module);
    if (result != kSVkSuccess) return FgGuideComputeFailure("vkCreateShaderModule", result);
    FgcBinding bindings[2] = {{0, 7, 1, 0x20, nullptr}, {1, 7, 1, 0x20, nullptr}};
    FgcSetLayoutInfo set_info = {32, nullptr, 0, 2, bindings};
    result = create_set_layout(c.device, &set_info, nullptr, &c.set_layout);
    if (result != kSVkSuccess) return FgGuideComputeFailure("vkCreateDescriptorSetLayout", result);
    FgcPushRange push = {0x20, 0, 16};
    FgcLayoutInfo layout = {30, nullptr, 0, 1, &c.set_layout, 1, &push};
    result = create_layout(c.device, &layout, nullptr, &c.layout);
    if (result != kSVkSuccess) return FgGuideComputeFailure("vkCreatePipelineLayout", result);
    FgcPipelineInfo pipeline = {};
    pipeline.type = 29; pipeline.stage = {18, nullptr, 0, 0x20, c.module, "main", nullptr};
    pipeline.layout = c.layout; pipeline.base_index = -1;
    result = create_pipeline(c.device, 0, 1, &pipeline, nullptr, &c.pipeline);
    if (result != kSVkSuccess) return FgGuideComputeFailure("vkCreateComputePipelines", result);
    FgcPoolSize size = {7, 2};
    FgcPoolInfo pool = {33, nullptr, 0, 1, 1, &size};
    result = create_pool(c.device, &pool, nullptr, &c.pool);
    if (result != kSVkSuccess) return FgGuideComputeFailure("vkCreateDescriptorPool", result);
    FgcSetInfo set = {34, nullptr, c.pool, 1, &c.set_layout};
    result = allocate_sets(c.device, &set, &c.set);
    if (result != kSVkSuccess) return FgGuideComputeFailure("vkAllocateDescriptorSets", result);
    FgcBufferInfo buffers[2] = {{c.buffers[0], 0, bytes}, {c.buffers[1], 0, bytes}};
    FgcWrite writes[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].type = 35; writes[i].set = c.set; writes[i].binding = i;
        writes[i].count = 1; writes[i].descriptor_type = 7; writes[i].buffers = &buffers[i];
    }
    update_sets(c.device, 2, writes, 0, nullptr);
    c.capacity = bytes; c.ready = true;
    Log("[fg-guides] GPU nearest ready: two device-local buffers of %llu bytes, integer raw-texel shader, concurrent queue-families=0x%llX.", bytes, queue_mask);
    return true;
}

static void FgGuideComputeBarrier(SVkCommandBuffer cb, uint32_t src_stage, uint32_t src_access,
                                 uint32_t dst_stage, uint32_t dst_access)
{
    SVkMemoryBarrier barrier = {};
    barrier.sType = kSVkStMemoryBarrier;
    barrier.srcAccessMask = src_access; barrier.dstAccessMask = dst_access;
    g_svk.CmdPipelineBarrier(cb, src_stage, dst_stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

static void FgGuideComputeRecord(SVkCommandBuffer cb, const FgGuideImage &g, SVkHandle dst, unsigned w, unsigned h)
{
    FgGuideComputeState &c = g_fgc;
    // Preparation proves capacity and all core commands before recording starts.
    // Every reuse is ordered on this batch; the existing relay completion fence
    // also orders batches when the game switches between graphics/compute queues.
    FgGuideComputeBarrier(cb, kSVkStageAllCommands, kSVkAccessMemoryRead | kSVkAccessMemoryWrite, 0x1000, 0x1000);
    SVkBufferImageCopy copy = {};
    copy.imageSubresource = {kSVkAspectColor, 0, 0, 1};
    copy.imageOffset = {static_cast<int32_t>(g.x), static_cast<int32_t>(g.y), 0};
    copy.imageExtent = {g.w, g.h, 1};
    g_svk.CmdCopyImageToBuffer(cb, g.resource.image, kSVkLayoutTransferSrc, c.buffers[0], 1, &copy);
    FgGuideComputeBarrier(cb, kSVkStageAllCommands, kSVkAccessMemoryRead | kSVkAccessMemoryWrite, 0x800, 0x20 | 0x40);
    c.BindPipeline(cb, 1, c.pipeline);
    c.BindSets(cb, 1, c.layout, 0, 1, &c.set, 0, nullptr);
    const uint32_t dims[4] = {g.w, g.h, w, h};
    c.Push(cb, c.layout, 0x20, 0, sizeof(dims), dims);
    c.Dispatch(cb, (w + 15) / 16, (h + 7) / 8, 1);
    FgGuideComputeBarrier(cb, 0x800, 0x40, 0x1000, 0x800);
    copy.imageOffset = {0, 0, 0}; copy.imageExtent = {w, h, 1};
    g_svk.CmdCopyBufferToImage(cb, c.buffers[1], dst, kSVkLayoutGeneral, 1, &copy);
}
