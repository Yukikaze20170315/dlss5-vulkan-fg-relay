// Real Vulkan readback of the production guide-recording helper. No game/NR.
#include "dlss5-bridge.cpp"
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

namespace guide_gpu {
static unsigned checks, failures, validation_errors;
static VkDeviceSize fixture_bytes, fixture_peak, compute_bytes, total_peak;
static unsigned compute_allocations;
static std::unordered_map<SVkHandle, uint64_t> compute_sizes;
static void Expect(bool ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", why); }
}
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        ++validation_errors;
        std::printf("VALIDATION ERROR %s\n", data != nullptr && data->pMessage != nullptr ? data->pMessage : "unknown");
    }
    return VK_FALSE;
}
static void Check(VkResult result, const char *where)
{
    if (result != VK_SUCCESS) { std::printf("FAIL %s VkResult=%d\n", where, result); std::exit(2); }
}
static void CheckHr(HRESULT result, const char *where)
{
    if (FAILED(result)) { std::printf("FAIL %s HRESULT=0x%08lx\n", where, result); std::exit(2); }
}
struct Buffer { VkBuffer buffer; VkDeviceMemory memory; VkDeviceSize bytes; };
struct Image { VkImage image; VkDeviceMemory memory; VkDeviceSize bytes; };
struct Context {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{};
    VkQueue queue{}; uint32_t family{}; VkCommandPool pool{}; VkCommandBuffer command{};
    VkPhysicalDeviceMemoryProperties memory{};
    uint32_t subtexel_bits{};
    uint32_t families[2]{}; VkQueue queues[2]{}; VkCommandPool pools[2]{}; VkCommandBuffer commands[2]{};
    VkDebugUtilsMessengerEXT debug{};
};
static void SelectQueue(Context &c, unsigned index)
{
    c.family = c.families[index]; c.queue = c.queues[index]; c.pool = c.pools[index]; c.command = c.commands[index];
}
static void Account(VkDeviceSize bytes)
{
    fixture_bytes += bytes;
    if (fixture_bytes > fixture_peak) fixture_peak = fixture_bytes;
    // The two production scratch allocations have independent 128 MiB caps;
    // the fixture stays below 128 MiB, bounding explicit allocations at 384 MiB.
    if (fixture_bytes > 256ull * 1024 * 1024 || fixture_bytes + compute_bytes > 384ull * 1024 * 1024) { std::printf("FAIL fixture exceeds memory budget\n"); std::exit(2); }
}
static uint32_t ComputeAllocate(SVkDevice device, const SVkMemoryAllocateInfo *info, const void *callbacks, SVkHandle *memory)
{
    if (fixture_bytes + compute_bytes + info->allocationSize > 384ull * 1024 * 1024) {
        std::printf("FAIL combined fixture/production allocations exceed 384 MiB\n"); std::exit(2);
    }
    const VkResult result = vkAllocateMemory(reinterpret_cast<VkDevice>(device),
        reinterpret_cast<const VkMemoryAllocateInfo *>(info), reinterpret_cast<const VkAllocationCallbacks *>(callbacks),
        reinterpret_cast<VkDeviceMemory *>(memory));
    if (result == VK_SUCCESS) {
        compute_sizes[*memory] = info->allocationSize; compute_bytes += info->allocationSize; ++compute_allocations;
        if (fixture_bytes + compute_bytes > total_peak) total_peak = fixture_bytes + compute_bytes;
    }
    return static_cast<uint32_t>(result);
}
static void ComputeFree(SVkDevice device, SVkHandle memory, const void *callbacks)
{
    const auto found = compute_sizes.find(memory);
    if (found != compute_sizes.end()) { compute_bytes -= found->second; compute_sizes.erase(found); }
    vkFreeMemory(reinterpret_cast<VkDevice>(device), reinterpret_cast<VkDeviceMemory>(memory),
                 reinterpret_cast<const VkAllocationCallbacks *>(callbacks));
}
static uint32_t MemoryType(const Context &c, uint32_t bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < c.memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) != 0 && (c.memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    std::printf("FAIL memory type\n"); std::exit(2);
}
static Buffer MakeBuffer(const Context &c, VkDeviceSize size)
{
    Buffer b{}; VkBufferCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size; ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode = VK_SHARING_MODE_CONCURRENT; ci.queueFamilyIndexCount = 2; ci.pQueueFamilyIndices = c.families;
    Check(vkCreateBuffer(c.device, &ci, nullptr, &b.buffer), "create buffer");
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(c.device, b.buffer, &req);
    Account(req.size); b.bytes = req.size;
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size; ai.memoryTypeIndex = MemoryType(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(c.device, &ai, nullptr, &b.memory), "allocate staging");
    Check(vkBindBufferMemory(c.device, b.buffer, b.memory, 0), "bind staging"); return b;
}
static Image MakeImage(const Context &c, unsigned w, unsigned h, VkFormat format)
{
    Image image{}; VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {w, h, 1};
    ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode = VK_SHARING_MODE_CONCURRENT; ci.queueFamilyIndexCount = 2; ci.pQueueFamilyIndices = c.families;
    Check(vkCreateImage(c.device, &ci, nullptr, &image.image), "create image");
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(c.device, image.image, &req);
    Account(req.size); image.bytes = req.size;
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size; ai.memoryTypeIndex = MemoryType(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(c.device, &ai, nullptr, &image.memory), "allocate image");
    Check(vkBindImageMemory(c.device, image.image, image.memory, 0), "bind image"); return image;
}
static void ImageBarrier(VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from; barrier.newLayout = to;
    barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}
static uint32_t Value(unsigned x, unsigned y, unsigned width, bool half, unsigned frame)
{
    const uint32_t special[] = {0u, 0x80000000u, 0x7FC01234u, 0x7F800000u, 1u, 0xFC007C00u, 0x80000001u};
    if ((x + y * 17u) % 251u == 0) return special[(frame + x + y) % _countof(special)];
    if (half) {
        const auto a = DirectX::PackedVector::XMConvertFloatToHalf(static_cast<float>(static_cast<int>((x + frame) % 31) - 15));
        const auto b = DirectX::PackedVector::XMConvertFloatToHalf(static_cast<float>(static_cast<int>((y + frame * 3) % 29) - 14));
        return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 16);
    }
    const float f = static_cast<float>(y * width + x + frame * 1048576u);
    uint32_t bits = 0; std::memcpy(&bits, &f, sizeof(bits)); return bits;
}
struct Case { unsigned tw, th, x, y, w, h, ow, oh; };
static unsigned Choices(unsigned target, unsigned input, unsigned output, uint32_t, unsigned *a, unsigned *b)
{
    const uint64_t numerator = (uint64_t(2) * target + 1) * input;
    const uint64_t denominator = uint64_t(2) * output;
    *a = static_cast<unsigned>(numerator / denominator);
    if (*a >= input) *a = input - 1;
    // Compute uses exact integer division. No blit boundary tolerance applies.
    *b = *a;
    return 1;
}
static bool Run(Context &c, const Case &test, bool half, unsigned frames = 8)
{
    const VkFormat format = half ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R32_SFLOAT;
    const VkDeviceSize input_size = VkDeviceSize(test.tw) * test.th * 4;
    const VkDeviceSize output_size = VkDeviceSize(test.ow) * test.oh * 4;
    Buffer upload = MakeBuffer(c, input_size), readback = MakeBuffer(c, output_size);
    Image source = MakeImage(c, test.tw, test.th, format), target = MakeImage(c, test.ow, test.oh, format);
    VkSemaphore semaphores[2]{}; VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    Check(vkCreateSemaphore(c.device, &sci, nullptr, &semaphores[0]), "create family-switch semaphore 0");
    Check(vkCreateSemaphore(c.device, &sci, nullptr, &semaphores[1]), "create family-switch semaphore 1");
    unsigned steady_allocations = 0; SVkHandle steady_buffer = 0, steady_pipeline = 0;
    bool ok = true;
    for (unsigned frame = 0; frame < frames; ++frame) {
    SelectQueue(c, (frame / 2) % 2); // Same-family reuse, then family switching.
    FgGuideImage guide{}; guide.resource.image = reinterpret_cast<SVkHandle>(source.image);
    guide.resource.w = test.tw; guide.resource.h = test.th; guide.resource.fmt = static_cast<uint32_t>(format);
    guide.resource.aspect = kSVkAspectColor; guide.x = test.x; guide.y = test.y; guide.w = test.w; guide.h = test.h;
    FgGuideContract contract{}; contract.depth = contract.mv = guide;
    contract.depth.resource.fmt = 100; contract.mv.resource.fmt = 83;
    contract.out_w = test.ow; contract.out_h = test.oh;
    bool prepared = FgGuidePrepareTransport(contract, c.family);
    if (!prepared && g_fgc.growth_pending) {
        Expect(frame == 0, "capacity growth requests explicit retirement at scenario transition");
        const SVkHandle old_buffer = g_fgc.buffers[0], old_pipeline = g_fgc.pipeline;
        Expect(!FgGuidePrepareTransport(contract, c.family) && g_fgc.buffers[0] == old_buffer && g_fgc.pipeline == old_pipeline,
               "capacity growth never destroys or overwrites live scratch objects");
        Check(vkDeviceWaitIdle(c.device), "test-controlled capacity retirement");
        FgGuideComputeRelease();
        prepared = FgGuidePrepareTransport(contract, c.family);
    }
    Expect(prepared, "production Prepare accepts actual graphics/compute queue");
    if (!prepared) { ok = false; break; }
    if (frame == 0) { steady_allocations = compute_allocations; steady_buffer = g_fgc.buffers[0]; steady_pipeline = g_fgc.pipeline; }
    else Expect(compute_allocations == steady_allocations && g_fgc.buffers[0] == steady_buffer && g_fgc.pipeline == steady_pipeline,
                "same-capacity frames reuse buffers/pipeline across queue families");
    if (fixture_bytes + compute_bytes > total_peak) total_peak = fixture_bytes + compute_bytes;
    Expect(fixture_bytes + compute_bytes <= 384ull * 1024 * 1024, "explicit GPU allocation budget respected");
    void *mapped = nullptr; Check(vkMapMemory(c.device, upload.memory, 0, input_size, 0, &mapped), "map upload");
    auto data = static_cast<uint32_t *>(mapped);
    for (unsigned y = 0; y < test.th; ++y) for (unsigned x = 0; x < test.tw; ++x) data[y * test.tw + x] = Value(x, y, test.tw, half, frame);
    vkUnmapMemory(c.device, upload.memory);
    Check(vkResetCommandBuffer(c.command, 0), "reset command");
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Check(vkBeginCommandBuffer(c.command, &begin), "begin");
    ImageBarrier(c.command, source.image, frame == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {test.tw, test.th, 1};
    vkCmdCopyBufferToImage(c.command, upload.buffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    ImageBarrier(c.command, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ImageBarrier(c.command, target.image, frame == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    FgGuideRecord(reinterpret_cast<SVkCommandBuffer>(c.command), guide, reinterpret_cast<SVkHandle>(target.image), test.ow, test.oh);
    ImageBarrier(c.command, target.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    copy.imageExtent = {test.ow, test.oh, 1};
    vkCmdCopyImageToBuffer(c.command, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
    VkMemoryBarrier memory_barrier{}; memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory_barrier, 0, nullptr, 0, nullptr);
    Check(vkEndCommandBuffer(c.command), "end");
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; VkFence fence{};
    Check(vkCreateFence(c.device, &fci, nullptr, &fence), "create fence");
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &c.command;
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (frame != 0) { submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &semaphores[(frame - 1) % 2]; submit.pWaitDstStageMask = &wait_stage; }
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &semaphores[frame % 2];
    Check(vkQueueSubmit(c.queue, 1, &submit, fence), "submit");
    Check(vkWaitForFences(c.device, 1, &fence, VK_TRUE, 10000000000ull), "test-only bounded readback");
    Check(vkMapMemory(c.device, readback.memory, 0, output_size, 0, &mapped), "map readback");
    data = static_cast<uint32_t *>(mapped); unsigned mismatches = 0, rounded_samples = 0;
    for (unsigned y = 0; y < test.oh; ++y) for (unsigned x = 0; x < test.ow; ++x) {
        unsigned xs[2], ys[2];
        const unsigned nx = Choices(x, test.w, test.ow, c.subtexel_bits, &xs[0], &xs[1]);
        const unsigned ny = Choices(y, test.h, test.oh, c.subtexel_bits, &ys[0], &ys[1]);
        bool match = false;
        for (unsigned iy = 0; iy < ny; ++iy) for (unsigned ix = 0; ix < nx; ++ix)
            if (data[y * test.ow + x] == Value(test.x + xs[ix], test.y + ys[iy], test.tw, half, frame)) match = true;
        if (match && data[y * test.ow + x] != Value(test.x + xs[0], test.y + ys[0], test.tw, half, frame)) ++rounded_samples;
        if (!match) {
            if (mismatches < 8) {
                float actual = 0;
                std::memcpy(&actual, &data[y * test.ow + x], sizeof(actual));
                std::printf("mismatch x=%u y=%u expected-src=%u,%u actual-bits=%08x actual-float=%.9g\n",
                            x, y, test.x + xs[0], test.y + ys[0], data[y * test.ow + x], actual);
            }
            ++mismatches;
        }
    }
    vkUnmapMemory(c.device, readback.memory);
    Expect(mismatches == 0, "GPU output matches exact raw nearest bits");
    ok = ok && mismatches == 0;
    std::printf("%s %s frame=%u family=%u texture=%ux%u rect=%u,%u+%ux%u output=%ux%u pixels=%llu mismatch=%u nearest-boundary-rounding=%u\n",
                mismatches == 0 ? "PASS" : "FAIL", half ? "RG16F" : "R32F", frame, c.family, test.tw, test.th, test.x, test.y, test.w, test.h,
                test.ow, test.oh, static_cast<unsigned long long>(test.ow) * test.oh, mismatches, rounded_samples);
    vkDestroyFence(c.device, fence, nullptr);
    }
    vkDestroyImage(c.device, source.image, nullptr); vkFreeMemory(c.device, source.memory, nullptr);
    vkDestroyImage(c.device, target.image, nullptr); vkFreeMemory(c.device, target.memory, nullptr);
    vkDestroyBuffer(c.device, upload.buffer, nullptr); vkFreeMemory(c.device, upload.memory, nullptr);
    vkDestroyBuffer(c.device, readback.buffer, nullptr); vkFreeMemory(c.device, readback.memory, nullptr);
    vkDestroySemaphore(c.device, semaphores[0], nullptr); vkDestroySemaphore(c.device, semaphores[1], nullptr);
    fixture_bytes -= source.bytes + target.bytes + upload.bytes + readback.bytes;
    return ok;
}
static bool ColourRegion(Context &c, unsigned sw, unsigned sh, unsigned sx, unsigned sy,
                         unsigned tw, unsigned th, unsigned dx, unsigned dy, unsigned w, unsigned h, unsigned family_slot)
{
    c.queue=c.queues[family_slot]; c.command=c.commands[family_slot]; c.family=c.families[family_slot];
    const VkDeviceSize bytes=VkDeviceSize((std::max)(sw*sh,tw*th))*4;
    Buffer upload=MakeBuffer(c,bytes), readback=MakeBuffer(c,VkDeviceSize(tw)*th*4);
    Image source=MakeImage(c,sw,sh,VK_FORMAT_R8G8B8A8_UNORM), carrier=MakeImage(c,w,h,VK_FORMAT_R8G8B8A8_UNORM), target=MakeImage(c,tw,th,VK_FORMAT_R8G8B8A8_UNORM);
    void *mapped=nullptr; Check(vkMapMemory(c.device,upload.memory,0,bytes,0,&mapped),"map colour fixture");
    auto *words=static_cast<uint32_t *>(mapped);
    for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x)words[y*sw+x]=0xA0000000u^(y*sw+x);
    vkUnmapMemory(c.device,upload.memory);
    Check(vkResetCommandBuffer(c.command,0),"reset colour command");
    VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Check(vkBeginCommandBuffer(c.command,&begin),"begin colour");
    ImageBarrier(c.command,source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy upload_region{};upload_region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};upload_region.imageExtent={sw,sh,1};
    vkCmdCopyBufferToImage(c.command,upload.buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&upload_region);
    ImageBarrier(c.command,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ImageBarrier(c.command,carrier.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    ImageBarrier(c.command,target.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear{};clear.float32[0]=clear.float32[2]=1;clear.float32[3]=1;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(c.command,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
    ImageBarrier(c.command,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    FgColorCopy(reinterpret_cast<SVkCommandBuffer>(c.command),reinterpret_cast<SVkHandle>(source.image),kSVkLayoutTransferSrc,
                reinterpret_cast<SVkHandle>(carrier.image),kSVkLayoutGeneral,sx,sy,0,0,w,h);
    ImageBarrier(c.command,carrier.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    FgColorCopy(reinterpret_cast<SVkCommandBuffer>(c.command),reinterpret_cast<SVkHandle>(carrier.image),kSVkLayoutTransferSrc,
                reinterpret_cast<SVkHandle>(target.image),kSVkLayoutTransferDst,0,0,dx,dy,w,h);
    ImageBarrier(c.command,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy download{};download.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};download.imageExtent={tw,th,1};
    vkCmdCopyImageToBuffer(c.command,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&download);
    VkMemoryBarrier mb{};mb.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,nullptr,0,nullptr);
    Check(vkEndCommandBuffer(c.command),"end colour");
    VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;VkFence fence{};Check(vkCreateFence(c.device,&fi,nullptr,&fence),"colour fence");
    VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&c.command;
    Check(vkQueueSubmit(c.queue,1,&submit,fence),"colour submit");Check(vkWaitForFences(c.device,1,&fence,VK_TRUE,10000000000ull),"colour readback wait");
    Check(vkMapMemory(c.device,readback.memory,0,VkDeviceSize(tw)*th*4,0,&mapped),"map colour result");words=static_cast<uint32_t *>(mapped);
    unsigned wrong_inside=0,wrong_outside=0;
    for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x){
        const bool inside=x>=dx&&y>=dy&&x-dx<w&&y-dy<h;
        const uint32_t expected=inside?(0xA0000000u^((sy+y-dy)*sw+sx+x-dx)):0xFFFF00FFu;
        if(words[y*tw+x]!=expected){if(inside)++wrong_inside;else ++wrong_outside;}
    }
    vkUnmapMemory(c.device,readback.memory);
    Expect(wrong_inside==0&&wrong_outside==0,"production colour crop/writeback keeps active pixels and untouched borders");
    std::printf("COLOUR family=%u src=%ux%u+%u,%u dst=%ux%u+%u,%u active=%ux%u inside_errors=%u outside_errors=%u\n",c.family,sw,sh,sx,sy,tw,th,dx,dy,w,h,wrong_inside,wrong_outside);
    vkDestroyFence(c.device,fence,nullptr);
    for(auto image:{source,carrier,target}){vkDestroyImage(c.device,image.image,nullptr);vkFreeMemory(c.device,image.memory,nullptr);fixture_bytes-=image.bytes;}
    for(auto buffer:{upload,readback}){vkDestroyBuffer(c.device,buffer.buffer,nullptr);vkFreeMemory(c.device,buffer.memory,nullptr);fixture_bytes-=buffer.bytes;}
    return wrong_inside==0&&wrong_outside==0;
}

static bool SharedImports(Context &c)
{
    ID3D12Device *d3d = nullptr;
    CheckHr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&d3d)), "create shared-texture D3D12 device");
    VkPhysicalDeviceIDProperties ids{}; ids.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 props{}; props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; props.pNext = &ids;
    vkGetPhysicalDeviceProperties2(c.physical, &props);
    const LUID luid = d3d->GetAdapterLuid();
    Expect(ids.deviceLUIDValid && std::memcmp(ids.deviceLUID, &luid, sizeof(luid)) == 0, "D3D12 shared texture and Vulkan use same adapter LUID");
    auto handle_properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(vkGetDeviceProcAddr(c.device, "vkGetMemoryWin32HandlePropertiesKHR"));
    if (handle_properties == nullptr) { std::printf("FAIL external memory query unavailable\n"); return false; }
    struct SharedCase { DXGI_FORMAT dx; VkFormat vk; D3D12_RESOURCE_FLAGS flags; };
    const SharedCase cases[] = {
        {DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {DXGI_FORMAT_R32_FLOAT, VK_FORMAT_R32_SFLOAT, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {DXGI_FORMAT_R16G16_FLOAT, VK_FORMAT_R16G16_SFLOAT, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS}
    };
    bool ok = true;
    for (const auto &test : cases) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = desc.Height = 16; desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = test.dx; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = test.flags;
        ID3D12Resource *texture = nullptr; HANDLE shared = nullptr;
        CheckHr(d3d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&texture)), "create shared carrier-shaped texture");
        CheckHr(d3d->CreateSharedHandle(texture, nullptr, GENERIC_ALL, nullptr, &shared), "create texture NT handle");
        VkExternalMemoryImageCreateInfo external{}; external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkImageCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ici.pNext = &external;
        ici.imageType = VK_IMAGE_TYPE_2D; ici.format = test.vk; ici.extent = {16,16,1};
        ici.mipLevels = ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT; ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.sharingMode = VK_SHARING_MODE_CONCURRENT; ici.queueFamilyIndexCount = 2; ici.pQueueFamilyIndices = c.families;
        VkImage image{}; VkDeviceMemory memory{};
        Check(vkCreateImage(c.device, &ici, nullptr, &image), "create concurrent imported image");
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(c.device, image, &req);
        VkMemoryWin32HandlePropertiesKHR properties{}; properties.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
        Check(handle_properties(c.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, shared, &properties), "D3D12 handle memory types");
        VkMemoryDedicatedAllocateInfo dedicated{}; dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO; dedicated.image = image;
        VkImportMemoryWin32HandleInfoKHR import{}; import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        import.pNext = &dedicated; import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT; import.handle = shared;
        VkMemoryAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocate.pNext = &import;
        allocate.allocationSize = req.size; allocate.memoryTypeIndex = MemoryType(c, req.memoryTypeBits & properties.memoryTypeBits, 0);
        Account(req.size);
        Check(vkAllocateMemory(c.device, &allocate, nullptr, &memory), "import NT shared D3D12 resource");
        Check(vkBindImageMemory(c.device, image, memory, 0), "bind concurrent imported image");
        Buffer upload = MakeBuffer(c, 16 * 16 * 4), readback = MakeBuffer(c, 16 * 16 * 4);
        void *mapped = nullptr; Check(vkMapMemory(c.device, upload.memory, 0, 16 * 16 * 4, 0, &mapped), "map shared upload");
        for (unsigned i = 0; i < 256; ++i) static_cast<uint32_t *>(mapped)[i] = 0x10001000u + i * 0x00010001u;
        vkUnmapMemory(c.device, upload.memory);
        VkSemaphore semaphore{}; VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        Check(vkCreateSemaphore(c.device, &si, nullptr, &semaphore), "create shared test semaphore");
        VkFence fence{}; VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Check(vkCreateFence(c.device, &fi, nullptr, &fence), "create shared test fence");
        for (unsigned queue = 0; queue < 2; ++queue) {
            SelectQueue(c, queue); Check(vkResetCommandBuffer(c.command, 0), "reset shared command");
            VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            Check(vkBeginCommandBuffer(c.command, &begin), "begin shared command");
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {16,16,1};
            if (queue == 0) {
                ImageBarrier(c.command, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                vkCmdCopyBufferToImage(c.command, upload.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                ImageBarrier(c.command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            } else {
                ImageBarrier(c.command, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                vkCmdCopyImageToBuffer(c.command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
                VkMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(c.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            }
            Check(vkEndCommandBuffer(c.command), "end shared command");
            VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &c.command;
            const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            if (queue == 0) { submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &semaphore; }
            else { submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &semaphore; submit.pWaitDstStageMask = &wait; }
            Check(vkQueueSubmit(c.queue, 1, &submit, queue == 0 ? VK_NULL_HANDLE : fence), "submit shared image across queue family");
        }
        Check(vkWaitForFences(c.device, 1, &fence, VK_TRUE, 10000000000ull), "wait shared readback");
        Check(vkMapMemory(c.device, readback.memory, 0, 16 * 16 * 4, 0, &mapped), "map shared readback");
        unsigned mismatch = 0;
        for (unsigned i = 0; i < 256; ++i) if (static_cast<uint32_t *>(mapped)[i] != 0x10001000u + i * 0x00010001u) ++mismatch;
        vkUnmapMemory(c.device, readback.memory);
        Expect(mismatch == 0, "D3D12 NT shared image supports concurrent graphics-write/compute-read");
        ok = ok && mismatch == 0;
        std::printf("%s D3D12-shared CONCURRENT format=%u flags=0x%x families=%u,%u mismatch=%u\n",
            mismatch == 0 ? "PASS" : "FAIL", static_cast<unsigned>(test.dx), static_cast<unsigned>(test.flags), c.families[0], c.families[1], mismatch);
        vkDestroyFence(c.device, fence, nullptr); vkDestroySemaphore(c.device, semaphore, nullptr);
        vkDestroyBuffer(c.device, upload.buffer, nullptr); vkFreeMemory(c.device, upload.memory, nullptr);
        vkDestroyBuffer(c.device, readback.buffer, nullptr); vkFreeMemory(c.device, readback.memory, nullptr);
        vkDestroyImage(c.device, image, nullptr); vkFreeMemory(c.device, memory, nullptr);
        fixture_bytes -= req.size + upload.bytes + readback.bytes;
        CloseHandle(shared); texture->Release();
    }
    d3d->Release(); return ok;
}
}
int main(int argc, char **argv)
{
    using namespace guide_gpu;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    InitializeCriticalSection(&g_log_cs); strcpy_s(g_log_path, "gpu-guide.log");
    Context c{}; VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance{}; instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; instance.pApplicationInfo = &app;
    uint32_t layer_count = 0; Check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr), "count layers");
    std::vector<VkLayerProperties> layers(layer_count); Check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()), "layers");
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    const char *debug_extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
    bool validation = false;
    for (const auto &layer : layers) if (std::strcmp(layer.layerName, validation_layer) == 0) validation = true;
    VkDebugUtilsMessengerCreateInfoEXT debug{}; debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    debug.pfnUserCallback = Validation;
    const VkValidationFeatureEnableEXT enable_sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features{}; validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validation_features.pNext = &debug; validation_features.enabledValidationFeatureCount = 1;
    validation_features.pEnabledValidationFeatures = &enable_sync;
    if (validation) { instance.enabledLayerCount = 1; instance.ppEnabledLayerNames = &validation_layer;
        instance.enabledExtensionCount = _countof(debug_extensions); instance.ppEnabledExtensionNames = debug_extensions;
        instance.pNext = &validation_features; }
    Check(vkCreateInstance(&instance, nullptr, &c.instance), "create isolated instance");
    if (validation) {
        auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(c.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create_debug == nullptr) return 2;
        Check(create_debug(c.instance, &debug, nullptr, &c.debug), "create validation messenger");
    }
    uint32_t count = 0; Check(vkEnumeratePhysicalDevices(c.instance, &count, nullptr), "count GPUs");
    std::vector<VkPhysicalDevice> devices(count); Check(vkEnumeratePhysicalDevices(c.instance, &count, devices.data()), "get GPUs");
    for (auto device : devices) {
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || c.physical == nullptr) c.physical = device;
    }
    if (c.physical == nullptr) return 2;
    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(c.physical, &properties);
    c.subtexel_bits = properties.limits.subTexelPrecisionBits;
    std::printf("GPU %s validation=%u exact-integer-nearest\n", properties.deviceName, validation ? 1u : 0u);
    vkGetPhysicalDeviceMemoryProperties(c.physical, &c.memory);
    vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count); vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &count, queues.data());
    bool found_graphics = false, found_compute = false;
    for (uint32_t i = 0; i < count; ++i) {
        std::printf("queue family=%u flags=0x%x count=%u\n", i, queues[i].queueFlags, queues[i].queueCount);
        if (!found_graphics && (queues[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) { c.families[0] = i; found_graphics = true; }
        if (!found_compute && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 &&
                (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) { c.families[1] = i; found_compute = true; }
    }
    if (!found_graphics || !found_compute) return 2;
    const float priority = 1; VkDeviceQueueCreateInfo qi[2]{};
    for (unsigned i = 0; i < 2; ++i) { qi[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi[i].queueFamilyIndex = c.families[i]; qi[i].queueCount = 1; qi[i].pQueuePriorities = &priority; }
    VkDeviceCreateInfo dci{}; dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.queueCreateInfoCount = 2; dci.pQueueCreateInfos = qi;
    const char *external_memory = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = &external_memory;
    Check(vkCreateDevice(c.physical, &dci, nullptr, &c.device), "create isolated device");
    for (unsigned i = 0; i < 2; ++i) {
        vkGetDeviceQueue(c.device, c.families[i], 0, &c.queues[i]);
        VkCommandPoolCreateInfo pci{}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = c.families[i];
        Check(vkCreateCommandPool(c.device, &pci, nullptr, &c.pools[i]), "create pool");
        VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; cai.commandPool = c.pools[i]; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(c.device, &cai, &c.commands[i]), "allocate command");
    }
    g_svk.dev = reinterpret_cast<SVkDevice>(c.device); g_svk.lib = GetModuleHandleW(L"vulkan-1.dll");
    g_svk.CmdCopyImage = reinterpret_cast<PFN_SVkCmdCopyImage>(vkCmdCopyImage);
    g_svk.CmdCopyImageToBuffer = reinterpret_cast<PFN_SVkCmdCopyImageToBuffer>(vkCmdCopyImageToBuffer);
    g_svk.CmdCopyBufferToImage = reinterpret_cast<PFN_SVkCmdCopyBufferToImage>(vkCmdCopyBufferToImage);
    g_svk.CmdPipelineBarrier = reinterpret_cast<PFN_SVkCmdPipelineBarrier>(vkCmdPipelineBarrier);
    g_svk.CreateBuffer = reinterpret_cast<PFN_SVkCreateBuffer>(vkCreateBuffer);
    g_svk.DestroyBuffer = reinterpret_cast<PFN_SVkDestroyBuffer>(vkDestroyBuffer);
    g_svk.GetBufferMemoryRequirements = reinterpret_cast<PFN_SVkGetBufferMemoryRequirements>(vkGetBufferMemoryRequirements);
    g_svk.BindBufferMemory = reinterpret_cast<PFN_SVkBindBufferMemory>(vkBindBufferMemory);
    g_svk.AllocateMemory = ComputeAllocate; g_svk.FreeMemory = ComputeFree;
    FgGuideNoteDevice(c.instance, c.physical, c.device, reinterpret_cast<PFN_FgGuideGipa>(vkGetInstanceProcAddr));
    const bool colour_only=argc>1&&strcmp(argv[1],"colour-only")==0;
    bool ok = true;
    if(colour_only){
        for(unsigned q=0;q<2;++q){
            ok=ColourRegion(c,3840,2160,0,0,5120,2160,640,0,3840,2160,q)&&ok;
            ok=ColourRegion(c,1920,1080,0,0,5120,2160,1600,540,1920,1080,q)&&ok;
            ok=ColourRegion(c,5120,2160,640,0,5120,2160,640,0,3840,2160,q)&&ok;
            ok=ColourRegion(c,37,29,3,5,41,31,7,9,17,13,q)&&ok;
        }
    } else {
    const Case cases[] = {{2,2,0,0,2,2,4,4}, {9,7,2,1,4,3,11,8}, {7,5,2,1,4,3,4,3},
        {3413,1440,0,0,3413,1440,5120,2160}, {3420,1444,3,2,3414,1440,5120,2160}, {63,51,1,3,61,47,17,19}};
    for (const auto &test : cases) { ok = Run(c, test, false) && ok; ok = Run(c, test, true) && ok; }
    FgGuideContract excessive{}; excessive.depth.w = excessive.depth.h = excessive.mv.w = excessive.mv.h = 1;
    excessive.out_w = 32769; excessive.out_h = 1;
    const unsigned allocations_before_reject = compute_allocations;
    Expect(!FgGuidePrepareTransport(excessive, c.families[1]), "oversized axis returns explicit failure");
    excessive.out_w = excessive.out_h = 32768;
    Expect(!FgGuidePrepareTransport(excessive, c.families[1]), "oversized storage returns explicit failure");
    excessive.out_w = excessive.out_h = 2;
    Expect(!FgGuidePrepareTransport(excessive, 63), "unsupported queue returns explicit failure");
    Expect(compute_allocations == allocations_before_reject, "rejected guides never allocate scratch memory");
    ok = Run(c, {9,7,2,1,4,3,11,8}, false, 4) && ok;
    ok = SharedImports(c) && ok;
    }
    Check(vkDeviceWaitIdle(c.device), "final drain"); FgGuideComputeRelease(); FgGuideForgetDevice(c.device);
    for (auto pool : c.pools) vkDestroyCommandPool(c.device, pool, nullptr);
    vkDestroyDevice(c.device, nullptr);
    if (c.debug != VK_NULL_HANDLE) {
        auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(c.instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroy_debug(c.instance, c.debug, nullptr);
    }
    vkDestroyInstance(c.instance, nullptr);
    Expect(fixture_bytes == 0 && compute_bytes == 0, "all fixture and production allocations freed");
    Expect(validation_errors == 0, "no Vulkan validation errors");
    std::printf("%s GPU guide compute/readback checks=%u failures=%u validation_errors=%u fixture_peak=%llu total_peak=%llu compute_allocations=%u\n",
        ok && failures == 0 ? "PASS" : "FAIL", checks, failures, validation_errors,
        static_cast<unsigned long long>(fixture_peak), static_cast<unsigned long long>(total_peak), compute_allocations);
    return ok && failures == 0 ? 0 : 1;
}
