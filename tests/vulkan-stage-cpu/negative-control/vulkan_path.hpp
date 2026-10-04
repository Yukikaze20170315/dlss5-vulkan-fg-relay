/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: MIT
 */

// Vulkan (v8.1): the NGX Vulkan exports, observed on the same module copies
// the D3D12 family detours.  dlssnr.hpp includes this file twice: once at
// global scope for the declarations (the Vulkan types the NGX Vulkan header
// needs, then that header), and once inside renodx::addons::dlss5::internal,
// after the D3D11 and Present paths, for the body.
//
// Facts this file depends on (dumpbin /exports, 2026-09-25):
//   - the driver's NGX core (_nvngx.dll, 616.56) and the DLSS plugins
//     (nvngx_dlss.dll, nvngx_dlssd.dll 310.x) export
//     NVSDK_NGX_VULKAN_CreateFeature, _CreateFeature1, _EvaluateFeature and
//     _ReleaseFeature.  NONE exports NVSDK_NGX_VULKAN_EvaluateFeature_C: the
//     SDK's static library implements it on top of _EvaluateFeature, so the
//     four detours below see every Vulkan evaluate;
//   - the signed NR runtime (nvngx_dlssnr.dll 310.8) exports a real Vulkan
//     path: NVSDK_NGX_VULKAN_Init_Ext/_Init_Ext2, _CreateFeature/1,
//     _EvaluateFeature, _ReleaseFeature and the Vulkan requirement queries,
//     and launches its kernels through VK_NVX_binary_import.  Only
//     NVSDK_NGX_VULKAN_Init shares the stub every *_Init export has (RVA
//     0x13F50, FAIL_FeatureNotSupported);
//   - NR's Vulkan Init and extension query need the VkInstance and
//     VkPhysicalDevice, which ReShade's add-on API does not expose. Capture
//     them at Vulkan device creation as well as from NGX Init: NGX may Init
//     before its module is loaded and before the NGX detours can be attached.

#ifndef RENODX_DLSS5_VULKAN_DECLS
#define RENODX_DLSS5_VULKAN_DECLS

// The subset of vulkan_core.h (Vulkan SDK 1.4.350) the NGX Vulkan header
// and this file use, copied verbatim; the addon has no Vulkan SDK include
// path, and nvsdk_ngx_defs_vk.h expects the types to be declared already.
#ifndef VULKAN_CORE_H_
#define VULKAN_CORE_H_ 1
#define VKAPI_CALL __stdcall
#define VKAPI_PTR VKAPI_CALL
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T* object;
#define VK_MAX_EXTENSION_NAME_SIZE 256U
VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkCommandBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkEvent)
typedef uint32_t VkFlags;
typedef VkFlags VkImageAspectFlags;
typedef VkFlags VkImageUsageFlags;
typedef VkFlags VkBufferUsageFlags;
typedef VkFlags VkAccessFlags;
typedef VkFlags VkPipelineStageFlags;
typedef VkFlags VkDependencyFlags;
typedef int32_t VkStructureType;
typedef int32_t VkImageLayout;
typedef struct VkMemoryBarrier {
  VkStructureType sType;
  const void* pNext;
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
} VkMemoryBarrier;
typedef struct VkBufferMemoryBarrier {
  VkStructureType sType;
  const void* pNext;
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
  uint32_t srcQueueFamilyIndex;
  uint32_t dstQueueFamilyIndex;
  VkBuffer buffer;
  uint64_t offset;
  uint64_t size;
} VkBufferMemoryBarrier;
typedef enum VkFormat {
  VK_FORMAT_UNDEFINED = 0,
  VK_FORMAT_R16_SFLOAT = 76,
  VK_FORMAT_R16G16_SFLOAT = 83,
  VK_FORMAT_R16G16B16A16_SFLOAT = 97,
  VK_FORMAT_R32_SFLOAT = 100,
  VK_FORMAT_R32G32_SFLOAT = 103,
  VK_FORMAT_R32G32B32A32_SFLOAT = 109,
  VK_FORMAT_B10G11R11_UFLOAT_PACK32 = 122,
  VK_FORMAT_MAX_ENUM = 0x7FFFFFFF
} VkFormat;
typedef enum VkImageAspectFlagBits {
  VK_IMAGE_ASPECT_COLOR_BIT = 0x00000001,
  VK_IMAGE_ASPECT_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkImageAspectFlagBits;
typedef struct VkImageSubresourceRange {
  VkImageAspectFlags aspectMask;
  uint32_t baseMipLevel;
  uint32_t levelCount;
  uint32_t baseArrayLayer;
  uint32_t layerCount;
} VkImageSubresourceRange;
typedef struct VkImageMemoryBarrier {
  VkStructureType sType;
  const void* pNext;
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
  VkImageLayout oldLayout;
  VkImageLayout newLayout;
  uint32_t srcQueueFamilyIndex;
  uint32_t dstQueueFamilyIndex;
  VkImage image;
  VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;
typedef struct VkImageMemoryBarrier2 {
  VkStructureType sType;
  const void* pNext;
  uint64_t srcStageMask;
  uint64_t srcAccessMask;
  uint64_t dstStageMask;
  uint64_t dstAccessMask;
  VkImageLayout oldLayout;
  VkImageLayout newLayout;
  uint32_t srcQueueFamilyIndex;
  uint32_t dstQueueFamilyIndex;
  VkImage image;
  VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier2;
struct VkMemoryBarrier2;
struct VkBufferMemoryBarrier2;
typedef struct VkDependencyInfo {
  VkStructureType sType;
  const void* pNext;
  VkDependencyFlags dependencyFlags;
  uint32_t memoryBarrierCount;
  const VkMemoryBarrier2* pMemoryBarriers;
  uint32_t bufferMemoryBarrierCount;
  const VkBufferMemoryBarrier2* pBufferMemoryBarriers;
  uint32_t imageMemoryBarrierCount;
  const VkImageMemoryBarrier2* pImageMemoryBarriers;
} VkDependencyInfo;
typedef void(VKAPI_PTR* PFN_vkCmdPipelineBarrier)(
    VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
    uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
    uint32_t, const VkImageMemoryBarrier*);
typedef void(VKAPI_PTR* PFN_vkCmdPipelineBarrier2)(VkCommandBuffer, const VkDependencyInfo*);
typedef void(VKAPI_PTR* PFN_vkCmdWaitEvents)(
    VkCommandBuffer, uint32_t, const VkEvent*, VkPipelineStageFlags, VkPipelineStageFlags,
    uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
    uint32_t, const VkImageMemoryBarrier*);
typedef void(VKAPI_PTR* PFN_vkCmdWaitEvents2)(
    VkCommandBuffer, uint32_t, const VkEvent*, const VkDependencyInfo*);
typedef struct VkExtensionProperties {
  char extensionName[VK_MAX_EXTENSION_NAME_SIZE];
  uint32_t specVersion;
} VkExtensionProperties;
typedef void(VKAPI_PTR* PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction(VKAPI_PTR* PFN_vkGetInstanceProcAddr)(VkInstance instance,
                                                                 const char* pName);
typedef PFN_vkVoidFunction(VKAPI_PTR* PFN_vkGetDeviceProcAddr)(VkDevice device,
                                                               const char* pName);
#endif  // VULKAN_CORE_H_

#include <nvsdk_ngx_vk.h>

#elif !defined(RENODX_DLSS5_VULKAN_BODY)
#define RENODX_DLSS5_VULKAN_BODY

namespace vk {

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

// The device whose DLSS evaluate first arms NR. Auxiliary Vulkan device
// initialization must not replace the owner of live runtime/codec objects.
inline std::atomic<VkDevice> game_device{nullptr};
inline std::atomic<reshade::api::device*> game_api_device{nullptr};
// The game's NGX Vulkan Init or requirement query, where NR's own Init and
// the extension check get the instance and physical device from.
inline std::atomic<VkInstance> game_instance{nullptr};
inline std::atomic<VkPhysicalDevice> game_physical_device{nullptr};
inline std::atomic<VkDevice> game_init_device{nullptr};
inline std::mutex context_mutex;
inline std::unordered_map<void*, VkInstance> instances_by_dispatch;
inline std::unordered_map<VkDevice, std::pair<VkInstance, VkPhysicalDevice>> device_contexts;
inline std::unordered_map<VkDevice, reshade::api::device*> api_devices;
struct CommandList {
  reshade::api::command_list* list;
  reshade::api::device* device;
};
inline std::shared_mutex command_lists_mutex;
inline std::unordered_map<VkCommandBuffer, CommandList> command_lists;
// A command buffer may be submitted again until it is begun anew or freed.
// vkBeginCommandBuffer and destruction are the only proofs available here
// that an old recording cannot use an add-on object again.
inline std::mutex recorded_uses_mutex;
inline std::unordered_map<reshade::api::command_list*, std::vector<uint64_t>> recorded_uses;
inline std::unordered_map<uint64_t, uint32_t> live_recorded_uses;
inline uint64_t next_use_id = 1;  // assigned under runtime_mutex

inline void TrackRecordedUse(reshade::api::command_list* list, uint64_t id) {
  if (id == 0) return;
  std::lock_guard lock(recorded_uses_mutex);
  auto& uses = recorded_uses[list];
  if (std::find(uses.begin(), uses.end(), id) != uses.end()) return;
  uses.push_back(id);
  ++live_recorded_uses[id];
}

inline void ForgetRecordedUses(reshade::api::command_list* list) {
  std::lock_guard lock(recorded_uses_mutex);
  const auto found = recorded_uses.find(list);
  if (found == recorded_uses.end()) return;
  for (const uint64_t id : found->second) {
    const auto use = live_recorded_uses.find(id);
    if (use != live_recorded_uses.end() && --use->second == 0) {
      live_recorded_uses.erase(use);
    }
  }
  recorded_uses.erase(found);
}

inline bool RecordedUseComplete(uint64_t id) {
  if (id == 0) return true;
  std::lock_guard lock(recorded_uses_mutex);
  return live_recorded_uses.find(id) == live_recorded_uses.end();
}

inline constexpr VkImageLayout kLayoutUndefined = static_cast<VkImageLayout>(0);
inline constexpr VkImageLayout kLayoutGeneral = static_cast<VkImageLayout>(1);
inline constexpr VkImageLayout kLayoutReadOnly = static_cast<VkImageLayout>(5);
inline constexpr VkImageLayout kLayoutReadOnlyOptimal = static_cast<VkImageLayout>(1000314000);
// Loader dispatch, never a first-device vkGetDeviceProcAddr function:
// injection must keep dispatching to the command buffer's current device.
inline std::atomic<PFN_vkCmdPipelineBarrier> native_barrier{nullptr};
inline PFN_vkCmdPipelineBarrier real_barrier = nullptr;
inline PFN_vkCmdPipelineBarrier2 real_barrier2 = nullptr;
inline PFN_vkCmdPipelineBarrier2 real_barrier2_khr = nullptr;
inline PFN_vkCmdWaitEvents real_wait_events = nullptr;
inline PFN_vkCmdWaitEvents2 real_wait_events2 = nullptr;
inline PFN_vkCmdWaitEvents2 real_wait_events2_khr = nullptr;
inline detour_guard::Record barrier_records[6];
inline std::mutex barrier_install_mutex;
inline std::atomic_bool barrier_install_attempted{false};
inline bool barrier_install_warned = false;  // barrier_install_mutex
inline std::atomic_uint32_t barrier_observers_resolved{0};
inline std::atomic_uint32_t barrier_observers_attached{0};
inline std::atomic_bool layout_observation_complete{false};

// One slot per detoured module copy, as the D3D12 and D3D11 families have.
// The four entries share NgxSlotGuard's four records in this order.
struct Real {
  void* create = nullptr;
  void* create1 = nullptr;
  void* evaluate = nullptr;
  void* release = nullptr;
};
inline Real slot_real[kMaxNgxSlots];
inline bool slot_used[kMaxNgxSlots] = {};
inline HMODULE slot_module[kMaxNgxSlots] = {};
inline std::atomic_bool slot_noncore[kMaxNgxSlots] = {};
inline NgxSlotGuard slot_guard[kMaxNgxSlots];
inline std::unordered_set<HMODULE> refused_modules;
inline std::unordered_map<HMODULE, NgxHookRetry> failed_modules;
// Copies without a Vulkan evaluate export, checked once per image.
inline std::unordered_map<HMODULE, uint64_t> absent_modules;
inline std::atomic_bool ngx_any_hooked{false};
// Latched for the funnel (ngx_hooked, list_hooks_live) and the telemetry gate.
inline std::atomic_bool ngx_ever_hooked{false};
inline thread_local uint32_t call_depth = 0;

// Entry tallies, the first statement of every wrapper: installed is not
// entered.  The one-shot "entered" lines are the log's witness of the same.
enum Entry : uint32_t { kCreate, kCreate1, kEvaluate, kRelease, kInit, kShutdown, kEntryCount };
inline constexpr const char* kEntryNames[kEntryCount] = {
    "CreateFeature", "CreateFeature1", "EvaluateFeature", "ReleaseFeature", "Init", "Shutdown"};
inline std::atomic_uint64_t entered[kEntryCount] = {};
inline std::atomic_uint32_t logged_entries{0};
inline void Enter(Entry entry) {
  entered[entry].fetch_add(1, std::memory_order_relaxed);
  if ((logged_entries.fetch_or(1u << entry, std::memory_order_relaxed) & (1u << entry)) == 0) {
    Log(reshade::log::level::info,
        std::string("Vulkan NGX hook entered: ") + kEntryNames[entry]);
  }
}

// NR's scratch images for one output of one game DLSS feature, at the
// output's size (NR runs 1:1 on Vulkan).  Created on first use
// (EnsureSurface) and tracked per surface (`state`, ReShade usages), so a
// surface a setting adds mid-stream starts from VK_IMAGE_LAYOUT_UNDEFINED
// while the others keep their layouts.  As D3D12's FinalResources:
//   - original / proxy / neural: the copied output, NR's input and output;
//   - work0 / work_a / work_b (v6 HDR): the linear source and the resolve
//     ping-pong of a pass stack; scale the 1x1 same-frame divisor texel,
//     block_mean the 32x32 pedestal means;
//   - stack0 / stack1 (legacy SDR): the display-encoded ping-pong of a pass
//     stack (the last pass writes the output itself);
//   - look_output and the four half-resolution bands: the look stage's N'
//     and its band surfaces (look_stage.hpp); look_scratch the 1x1 history
//     texel a compose without the temporal filter binds (never read).
enum Surface : uint32_t {
  kOriginal,
  kProxy,
  kNeural,
  kWork0,
  kWorkA,
  kWorkB,
  kScale,
  kBlockMean,
  kStack0,
  kStack1,
  kLookOutput,
  kBandA,
  kBandB,
  kBandMaxA,
  kBandMaxB,
  kLookScratch,
  kRenderTarget,
  kSurfaceCount,
};
struct Workset {
  VkImage output = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  reshade::api::format output_format = reshade::api::format::unknown;
  reshade::api::resource images[kSurfaceCount] = {};
  reshade::api::resource_view views[kSurfaceCount] = {};
  reshade::api::resource_usage state[kSurfaceCount] = {};
  // What the shared v6 policy reads (FrameDisplayCodec, FrameCodecDivisor,
  // MakeV6Constants, ...): the same two fields D3D12's FinalResources has.
  uint8_t hdr_mode = 0;
  codec::SourceUnits units;
  uint64_t snaps = 0;
  // The feed guard's per-workset key (norm_trace::guard_probe).
  uint32_t id = 0;
  uint64_t use_id = 0;
  uint64_t last_used = 0;
  uint32_t source_width = 0;
  uint32_t source_height = 0;
};
inline uint32_t next_workset_id = 0;
inline uint64_t next_workset_use = 1;

// One half of a pass's look history: an RGBA32_UINT storage image at the NR
// size, in GENERAL for its whole life (look_common.hlsli says why an image).
struct LookHistoryImage {
  reshade::api::resource image = {};
  reshade::api::resource_view view = {};
};
struct RetiredLookHistory {
  LookHistoryImage buffers[2];
  uint64_t use_id = 0;
};
struct RetiredNrHandle {
  NVSDK_NGX_Handle* handle = nullptr;
  uint64_t use_id = 0;
  NVSDK_NGX_Parameter* parameters = nullptr;
};
using LookHistoryVk = BasicLookHistory<LookHistoryImage>;

// Each game handle owns independent Render and Upscaled streams, including
// the normalization governor, NR handles, worksets and temporal look history.
struct StageFeature {
  uint32_t create_flags = 0;
  uint32_t channel_offset = UINT32_MAX;
  uint64_t commit_use = 0;
  reshade::api::device* device = nullptr;
  std::vector<Workset> worksets;
  std::vector<Workset> retired_worksets;
  // NR feature 18 per stack pass (NRPasses), created at the output size
  // below; a size change retires the old handles into `retired_nr`.
  NrFeatureSlot slots[kMaxNrPasses];
  uint32_t nr_width = 0;
  uint32_t nr_height = 0;
  uint8_t nr_hdr_mode = 0;
  uint64_t configuration = 0;
  std::vector<RetiredNrHandle> retired_nr;
  uint64_t nr_use[kMaxNrPasses] = {};
  // Each pass's look history (the D3D12 slot's look_history). Replaced pairs
  // wait until every command buffer that recorded them is reset or freed.
  LookHistoryVk look_history[kMaxNrPasses];
  std::vector<RetiredLookHistory> retired_history;
  uint64_t look_history_use[kMaxNrPasses] = {};
  // The stream's v6 state, as D3D12 keeps it per game feature: `ngx` carries
  // the feed decision (SelectFeed) and the governor's NormStream (ngx.norm);
  // `commit` is the stream's committed divisor texel, shared by all of its
  // worksets like D3D12's NormStream::commit, cleared once when created.
  FeatureState ngx;
  reshade::api::resource commit = {};
  reshade::api::resource_view commit_view = {};
  bool commit_cleared = false;
};
struct Feature {
  NVSDK_NGX_Feature id = NVSDK_NGX_Feature_Reserved_Unknown;
  uint32_t create_flags = 0;
  reshade::api::device* device = nullptr;
  FeatureState ngx;
  uint32_t render_width = 0;
  uint32_t render_height = 0;
  StageFeature stages[2];
};
inline std::unordered_map<const NVSDK_NGX_Handle*, Feature> features;

// A Vulkan command buffer may be begun again or freed only when it is not
// pending. That lifecycle event is the completion/discard proof for its
// readback copies; elapsed frames and a changed sentinel are not GPU fences.
// Pending slots are never read or overwritten. The sentinel distinguishes
// a completed copy from an abandoned recording only AFTER that proof.
struct ScaleSample {
  bool recorded = false;
  bool guarded = false;
  uint32_t workset = 0;
  reshade::api::command_list* recording = nullptr;
  bool retired = false;
};
inline std::mutex scale_samples_mutex;
inline reshade::api::resource scale_readback = {};
inline float* scale_mapped = nullptr;
inline ScaleSample scale_samples[norm_trace::guard_probe::kSlots];
inline uint32_t scale_head = 0;
inline uint64_t scale_calls = 0;
inline std::atomic<float> divisor_last{0.f};
inline std::atomic_uint64_t divisor_reads{0};
inline std::atomic_uint64_t divisor_lost{0};

inline void RetireScaleSamples(reshade::api::command_list* cmd_list) {
  std::lock_guard lock(scale_samples_mutex);
  for (ScaleSample& sample : scale_samples) {
    if (sample.recorded && sample.recording == cmd_list) sample.retired = true;
  }
}

inline std::atomic_uint64_t dlss_evaluates{0};
inline std::atomic_uint64_t owner_mismatches{0};
// Present is served only by the bridge's D3D12 carrier; Vulkan never folds
// its count into the Upscaled stage.
// The evaluates a DX11 bridge add-on re-issues on Direct3D 12 pass through
// (HookedEvaluate) while that path runs NR; the first 4 changes are logged.
// nr_off_seen_ns: SteadyNowNs of the last evaluate here with NR switched
// off, which restarts the grace window a re-enabled D3D12 path warms up in.
inline std::atomic_bool bridge_yielding{false};
inline std::atomic_uint32_t bridge_yield_changes{0};
inline std::atomic_int64_t nr_off_seen_ns{0};
// The NR runtime's device-extension requirement for feature 18 against the
// game's device: 0 unchecked, 1 satisfied or not checkable, 2 missing.
inline std::atomic_uint8_t extension_state{0};

// ---------------------------------------------------------------------------
// The game's NGX Vulkan Init and requirement queries (instance capture)
// ---------------------------------------------------------------------------

using InitFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice,
    NVSDK_NGX_Version);
using InitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice,
    NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
using InitExt2Fn = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice,
    PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, NVSDK_NGX_Version,
    const NVSDK_NGX_Parameter*);
// The exported ABI is nvsdk_ngx_vk.h's NGX_SNIPPET_BUILD block (the C
// declarations, copied above): what NGX modules export, which differs from
// the application-side Init the SDK's static library implements.  That block
// is compiled out of an application build, hence the local types.
//
// The core's NVSDK_NGX_VULKAN_Init_ProjectID and _Init_ProjectID_Ext exports
// have no header declaration.  The SDK's static library
// (nvsdk_ngx_s.lib, NVSDK_NGX_VULKAN_Init_with_ProjectID, dumpbin /disasm
// 2026-09-25) calls _Init_ProjectID_Ext when the core exports it, else
// _Init_ProjectID when the application passed no vkGet*ProcAddr, with these
// arguments in this order (the header's Init_with_ProjectID arguments, with
// the SDK version moved before the feature info):
//   _Ext: ProjectId, EngineType, EngineVersion, DataPath, Instance, PD,
//         Device, GIPA, GDPA, SDKVersion, FeatureInfo
//   base: ProjectId, EngineType, EngineVersion, DataPath, Instance, PD,
//         Device, SDKVersion, FeatureInfo
// Every application that initializes NGX by project id (Streamline titles
// among them) reaches the core through one of the two.
using InitProjectFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    const char*, NVSDK_NGX_EngineType, const char*, const wchar_t*, VkInstance,
    VkPhysicalDevice, VkDevice, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo*);
using InitProjectExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    const char*, NVSDK_NGX_EngineType, const char*, const wchar_t*, VkInstance,
    VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
    NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo*);
using RequirementsFn = decltype(&NVSDK_NGX_VULKAN_GetFeatureRequirements);

inline InitFn real_init = nullptr;
inline InitExtFn real_init_ext = nullptr;
inline InitExt2Fn real_init_ext2 = nullptr;
inline InitProjectFn real_init_project = nullptr;
inline InitProjectExtFn real_init_project_ext = nullptr;
inline RequirementsFn real_requirements = nullptr;
// nvsdk_ngx_vk.h declares _Shutdown(void) only under
// NGX_ENABLE_DEPRECATED_SHUTDOWN; the core still exports it.
using ShutdownVoidFn = NVSDK_NGX_Result(NVSDK_CONV*)();
using ShutdownDeviceFn = decltype(&NVSDK_NGX_VULKAN_Shutdown1);
inline ShutdownVoidFn real_shutdown = nullptr;
inline ShutdownDeviceFn real_shutdown1 = nullptr;
inline detour_guard::Record init_records[8];
inline HMODULE init_hooked_core = nullptr;
inline uint64_t init_hooked_image = 0;

inline void NoteInstance(VkInstance instance, VkPhysicalDevice physical, VkDevice device) {
  if (instance == nullptr || physical == nullptr) return;
  const bool first = game_instance.exchange(instance, std::memory_order_relaxed) == nullptr;
  game_physical_device.store(physical, std::memory_order_relaxed);
  if (device != nullptr) {
    game_init_device.store(device, std::memory_order_relaxed);
    std::lock_guard lock(context_mutex);
    device_contexts[device] = {instance, physical};
  }
  if (first) {
    Log(reshade::log::level::info,
        "Vulkan instance and physical device captured for NR's own Vulkan Init");
  }
}

// ReShade loads add-ons inside vkCreateInstance, before returning the first
// instance. Its public device API exposes VkDevice but not its instance or
// physical device. The layer's proc lookup sees the instance after creation;
// its vkCreateDevice sees the matching physical device. ReShade itself pairs
// them by the loader dispatch key, so the same association works even when a
// game creates more than one Vulkan instance/device (RDR2 does).
inline PFN_vkGetInstanceProcAddr real_layer_gipa = nullptr;
// The add-on intentionally declares only the Vulkan ABI it needs, to avoid
// pulling a second Vulkan header in beside the NGX SDK's declarations.
using LayerCreateDeviceFn = int32_t(VKAPI_PTR*)(
    VkPhysicalDevice, const void*, const void*, VkDevice*);
inline LayerCreateDeviceFn real_layer_create_device = nullptr;
inline detour_guard::Record context_records[2];
inline bool context_hooks_installed = false;
inline PFN_vkVoidFunction VKAPI_CALL HookedLayerGipa(VkInstance instance, const char* name) {
  if (instance != nullptr) {
    std::lock_guard lock(context_mutex);
    instances_by_dispatch[*reinterpret_cast<void**>(instance)] = instance;
  }
  return real_layer_gipa(instance, name);
}
inline int32_t VKAPI_CALL HookedLayerCreateDevice(
    VkPhysicalDevice physical, const void* info, const void* allocator, VkDevice* out) {
  VkInstance instance = nullptr;
  if (physical != nullptr) {
    std::lock_guard lock(context_mutex);
    if (const auto found = instances_by_dispatch.find(*reinterpret_cast<void**>(physical));
        found != instances_by_dispatch.end()) {
      instance = found->second;
    }
  }
  const int32_t result = real_layer_create_device(physical, info, allocator, out);
  if (result >= 0 && out != nullptr && *out != nullptr && instance != nullptr) {
    NoteInstance(instance, physical, *out);
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      Log(reshade::log::level::info,
          "Vulkan device creation supplied NR context independently of NGX Init");
    }
  }
  return result;
}
inline const detour_guard::Hook kContextHooks[] = {
    {"vkGetInstanceProcAddr", reinterpret_cast<void**>(&real_layer_gipa),
     reinterpret_cast<void*>(&HookedLayerGipa), &context_records[0]},
    {"vkCreateDevice", reinterpret_cast<void**>(&real_layer_create_device),
     reinterpret_cast<void*>(&HookedLayerCreateDevice), &context_records[1]},
};
inline void InstallContextHooks() {
  if (context_hooks_installed || !hooks_enabled.load(std::memory_order_relaxed)) return;
  const HMODULE layer = reshade::internal::get_reshade_module_handle();
  const auto gipa = layer != nullptr
      ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(layer, "vkGetInstanceProcAddr"))
      : nullptr;
  if (gipa == nullptr) return;
  real_layer_gipa = gipa;
  real_layer_create_device = reinterpret_cast<LayerCreateDeviceFn>(
      gipa(nullptr, "vkCreateDevice"));
  if (real_layer_create_device == nullptr) return;
  size_t failed = 0;
  const size_t attached = detour_guard::AttachAll(kContextHooks, &failed);
  if (attached != std::size(kContextHooks)) {
    for (const detour_guard::Hook& hook : kContextHooks) {
      if (hook.record->target != nullptr) NoteDetourRemoval(detour_guard::Detach(hook));
    }
    Log(reshade::log::level::warning,
        "Vulkan device context hooks unavailable; NGX Init observation remains the fallback");
    return;
  }
  context_hooks_installed = true;
}

inline std::pair<VkInstance, VkPhysicalDevice> ContextFor(VkDevice device) {
  std::lock_guard lock(context_mutex);
  if (const auto found = device_contexts.find(device); found != device_contexts.end()) {
    return found->second;
  }
  // A legacy NGX Init observation is a fallback for that exact device only;
  // a recreated device must not inherit another device's instance contract.
  if (game_init_device.load(std::memory_order_relaxed) == device) {
    return {game_instance.load(std::memory_order_relaxed),
            game_physical_device.load(std::memory_order_relaxed)};
  }
  return {nullptr, nullptr};
}

inline NVSDK_NGX_Result NVSDK_CONV HookedInit(
    unsigned long long id, const wchar_t* path, VkInstance instance,
    VkPhysicalDevice physical, VkDevice device, NVSDK_NGX_Version version) {
  Enter(kInit);
  if (!InsideDirectCall()) NoteInstance(instance, physical, device);
  return real_init(id, path, instance, physical, device, version);
}
inline NVSDK_NGX_Result NVSDK_CONV HookedInitExt(
    unsigned long long id, const wchar_t* path, VkInstance instance,
    VkPhysicalDevice physical, VkDevice device, NVSDK_NGX_Version version,
    const NVSDK_NGX_Parameter* parameters) {
  Enter(kInit);
  if (!InsideDirectCall()) NoteInstance(instance, physical, device);
  return real_init_ext(id, path, instance, physical, device, version, parameters);
}
inline NVSDK_NGX_Result NVSDK_CONV HookedInitExt2(
    unsigned long long id, const wchar_t* path, VkInstance instance,
    VkPhysicalDevice physical, VkDevice device, PFN_vkGetInstanceProcAddr gipa,
    PFN_vkGetDeviceProcAddr gdpa, NVSDK_NGX_Version version,
    const NVSDK_NGX_Parameter* parameters) {
  Enter(kInit);
  if (!InsideDirectCall()) NoteInstance(instance, physical, device);
  return real_init_ext2(id, path, instance, physical, device, gipa, gdpa, version,
                        parameters);
}
inline NVSDK_NGX_Result NVSDK_CONV HookedInitProject(
    const char* project, NVSDK_NGX_EngineType engine, const char* engine_version,
    const wchar_t* path, VkInstance instance, VkPhysicalDevice physical, VkDevice device,
    NVSDK_NGX_Version version, const NVSDK_NGX_FeatureCommonInfo* info) {
  Enter(kInit);
  if (!InsideDirectCall()) NoteInstance(instance, physical, device);
  return real_init_project(project, engine, engine_version, path, instance, physical,
                           device, version, info);
}
inline NVSDK_NGX_Result NVSDK_CONV HookedInitProjectExt(
    const char* project, NVSDK_NGX_EngineType engine, const char* engine_version,
    const wchar_t* path, VkInstance instance, VkPhysicalDevice physical, VkDevice device,
    PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr gdpa, NVSDK_NGX_Version version,
    const NVSDK_NGX_FeatureCommonInfo* info) {
  Enter(kInit);
  if (!InsideDirectCall()) NoteInstance(instance, physical, device);
  return real_init_project_ext(project, engine, engine_version, path, instance, physical,
                               device, gipa, gdpa, version, info);
}
inline NVSDK_NGX_Result NVSDK_CONV HookedRequirements(
    const VkInstance instance, const VkPhysicalDevice physical,
    const NVSDK_NGX_FeatureDiscoveryInfo* info, NVSDK_NGX_FeatureRequirement* out) {
  Enter(kInit);
  if (!InsideDirectCall()) NoteInstance(instance, physical, nullptr);
  return real_requirements(instance, physical, info, out);
}

// The game's NGX Vulkan Shutdown on the core (the SDK's static library calls
// _Shutdown1 when the core exports it, else _Shutdown; nvsdk_ngx_s.lib,
// NVSDK_NGX_VULKAN_ShutdownCommon): NR's runtime is shut down first.
inline void ShutdownNr(VkDevice device = nullptr);
inline NVSDK_NGX_Result NVSDK_CONV HookedShutdown() {
  Enter(kShutdown);
  const ngx_serial::Call ngx_turn;
  if (!InsideDirectCall()) {
    RuntimeLock lock(runtime_mutex);
    GuardHook([] { ShutdownNr(); });
  }
  return real_shutdown();
}
inline NVSDK_NGX_Result NVSDK_CONV HookedShutdown1(VkDevice device) {
  Enter(kShutdown);
  const ngx_serial::Call ngx_turn;
  if (!InsideDirectCall()) {
    RuntimeLock lock(runtime_mutex);
    GuardHook([&] { ShutdownNr(device); });
  }
  return real_shutdown1(device);
}

inline const detour_guard::Hook kInitHooks[] = {
    {"NVSDK_NGX_VULKAN_Init", reinterpret_cast<void**>(&real_init),
     reinterpret_cast<void*>(&HookedInit), &init_records[0]},
    {"NVSDK_NGX_VULKAN_Init_Ext", reinterpret_cast<void**>(&real_init_ext),
     reinterpret_cast<void*>(&HookedInitExt), &init_records[1]},
    {"NVSDK_NGX_VULKAN_Init_Ext2", reinterpret_cast<void**>(&real_init_ext2),
     reinterpret_cast<void*>(&HookedInitExt2), &init_records[2]},
    {"NVSDK_NGX_VULKAN_GetFeatureRequirements",
     reinterpret_cast<void**>(&real_requirements),
     reinterpret_cast<void*>(&HookedRequirements), &init_records[3]},
    {"NVSDK_NGX_VULKAN_Init_ProjectID", reinterpret_cast<void**>(&real_init_project),
     reinterpret_cast<void*>(&HookedInitProject), &init_records[4]},
    {"NVSDK_NGX_VULKAN_Init_ProjectID_Ext", reinterpret_cast<void**>(&real_init_project_ext),
     reinterpret_cast<void*>(&HookedInitProjectExt), &init_records[5]},
    {"NVSDK_NGX_VULKAN_Shutdown", reinterpret_cast<void**>(&real_shutdown),
     reinterpret_cast<void*>(&HookedShutdown), &init_records[6]},
    {"NVSDK_NGX_VULKAN_Shutdown1", reinterpret_cast<void**>(&real_shutdown1),
     reinterpret_cast<void*>(&HookedShutdown1), &init_records[7]},
};

// ---------------------------------------------------------------------------
// The NR runtime's device-extension requirement for feature 18
// ---------------------------------------------------------------------------

// Once per session, at the first DLSS evaluate: the NR runtime names the
// device extensions feature 18 needs, and one the game's device did not
// enable cannot be added after vkCreateDevice.  ReShade's add-on API has no
// enabled-extension query, so an extension is judged by one of its commands:
// vkGetDeviceProcAddr returns NULL for a command of a device extension that
// was not enabled (Vulkan spec, vkGetDeviceProcAddr behavior table).  Only
// extensions outside core Vulkan 1.3 are judged - a core-promoted one's
// functionality exists whether or not its name was enabled - and an unknown
// name is logged as not checkable, never declined.
inline constexpr std::pair<const char*, const char*> kExtensionCommands[] = {
    {"VK_NVX_binary_import", "vkCreateCuModuleNVX"},
    {"VK_NVX_image_view_handle", "vkGetImageViewHandleNVX"},
    {"VK_KHR_push_descriptor", "vkCmdPushDescriptorSetKHR"},
    {"VK_KHR_external_memory_win32", "vkGetMemoryWin32HandleKHR"},
    {"VK_KHR_external_semaphore_win32", "vkGetSemaphoreWin32HandleKHR"},
};
inline void CheckExtensions() {
  uint8_t expected = 0;
  if (!extension_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
    return;
  }
  const VkDevice device = game_device.load(std::memory_order_relaxed);
  const auto [instance, physical] = ContextFor(device);
  const NgxLoaderSymbols& symbols = NgxLoaderView();
  if (!symbols.nr_ready || instance == nullptr || physical == nullptr) {
    // Retried at the next evaluate: the runtime loads on its own schedule.
    extension_state.store(0, std::memory_order_relaxed);
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      Log(reshade::log::level::info,
          std::string("NR's Vulkan device-extension requirements are not queried yet: ")
              + (!symbols.nr_ready ? "the NR runtime is not loaded"
                                   : "the game's NGX Vulkan context was not observed"));
    }
    return;
  }
  NoteLoaderCall("vk::CheckExtensions/GetProcAddress");
  const auto query =
      reinterpret_cast<decltype(&NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements)>(
          GetProcAddress(symbols.nr_module,
                         "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements"));
  const HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
  const auto device_proc = loader == nullptr ? nullptr
      : reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(loader, "vkGetDeviceProcAddr"));
  if (query == nullptr || device_proc == nullptr || device == nullptr) return;
  NVSDK_NGX_FeatureDiscoveryInfo info{};
  info.SDKVersion = NVSDK_NGX_Version_API;
  info.FeatureID = kFeatureDlssNr;
  info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
  info.Identifier.v.ApplicationId = kDirectApplicationId;
  info.ApplicationDataPath = AddonDirectory().c_str();
  uint32_t count = 0;
  VkExtensionProperties* extensions = nullptr;
  NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
  {
    DirectCallScope direct;
    result = query(instance, physical, &info, &count, &extensions);
  }
  if (NVSDK_NGX_FAILED(result) || (count != 0 && extensions == nullptr)) {
    char text[160];
    snprintf(text, sizeof(text),
             "NR's Vulkan device-extension query for feature 18 returned 0x%08x;"
             " NR proceeds without the check",
             static_cast<uint32_t>(result));
    Log(reshade::log::level::warning, text);
    return;
  }
  std::string names;
  std::string missing;
  std::string unchecked;
  for (uint32_t i = 0; i < count; ++i) {
    const std::string name(extensions[i].extensionName);
    names += (names.empty() ? "" : ", ") + name;
    const auto known = std::find_if(
        std::begin(kExtensionCommands), std::end(kExtensionCommands),
        [&](const auto& entry) { return name == entry.first; });
    if (known == std::end(kExtensionCommands)) {
      unchecked += (unchecked.empty() ? "" : ", ") + name;
    } else if (device_proc(device, known->second) == nullptr) {
      missing += (missing.empty() ? "" : ", ") + name;
    }
  }
  Log(reshade::log::level::info,
      "NR (feature 18) on Vulkan names " + std::to_string(count)
          + " device extension(s): " + (names.empty() ? "(none)" : names)
          + (unchecked.empty() ? "" : "; core or not checkable: " + unchecked));
  if (!missing.empty()) {
    extension_state.store(2, std::memory_order_relaxed);
    Log(reshade::log::level::warning,
        "NR on Vulkan needs device extension " + missing
            + ", which the game's device did not enable; the game's image is"
              " kept (vk_extension_missing)");
  }
}

// ---------------------------------------------------------------------------
// The NGX Vulkan feature detours
// ---------------------------------------------------------------------------

using CreateFn = decltype(&NVSDK_NGX_VULKAN_CreateFeature);
using Create1Fn = decltype(&NVSDK_NGX_VULKAN_CreateFeature1);
using EvaluateFn = decltype(&NVSDK_NGX_VULKAN_EvaluateFeature);
using ReleaseFn = decltype(&NVSDK_NGX_VULKAN_ReleaseFeature);

// ---------------------------------------------------------------------------
// NR on Vulkan: the Upscaled hook point
// ---------------------------------------------------------------------------
//
// After the game's DLSS/DLSSD evaluate returns, NR is recorded into the same
// command buffer, as the D3D12 after path records into the game's list:
//   1. copy the DLSS output to `original` (one copy, as D3D12 makes);
//   2. legacy encode: original -> proxy, the display-range sRGB proxy NR's
//      network expects (README "Normalization and transfer");
//   3. feature 18 through the signed runtime's Vulkan path (Runtime below):
//      proxy -> neural, with the game's depth and motion vectors passed as
//      the game handed them to DLSS (its NVSDK_NGX_Resource_VK pointers);
//   4. legacy decode: original, proxy, neural -> the DLSS output.
// SDR uses the legacy (Classic) family. HDR uses the same v6 shaders and
// normalization decisions as D3D12, including exposure, metering and the
// governor; explicit source-unit overrides are refreshed every evaluate.
//
// Layouts.  DLSS writes its output as a storage image, which Vulkan allows
// only in VK_IMAGE_LAYOUT_GENERAL, and DLSS returns every buffer to the
// layout it came in (DLSS guide 3.4); ReShade maps unordered_access to
// GENERAL, so every barrier below leaves the output in GENERAL again.  The
// codec's sampled descriptors are SHADER_READ_ONLY_OPTIMAL (ReShade's
// push_descriptors), so the scratch images rest there between frames.
//
// Lifetime.  A workset and an NR feature belong to the game DLSS feature
// whose evaluates they ride: same command buffer, so same submission.  NGX's
// ReleaseFeature "releases the feature ... and frees its memory" (DLSS guide,
// NVSDK_NGX_ReleaseFeature), so a game that releases its DLSS handle has
// completed every submission that used it, ours included, and they are
// freed there (HookedRelease).  The device's teardown frees the rest:
// vkDestroyDevice requires all of the device's work to be complete.
// Runtime.  Feature 18 on Vulkan runs on the signed runtime directly, as on
// D3D12: its Vulkan Init_Ext on the game's instance, physical device and
// device, then CreateFeature1 / EvaluateFeature / ReleaseFeature.
//   - The game's NGX core does not serve feature 18: its
//     NVSDK_NGX_VULKAN_CreateFeature1(18) returns 0xbad0000b
//     (FAIL_UnableToInitializeFeature) on vk_basic, as core_create does on
//     D3D12 in every field log.
//   - The runtime's Vulkan Init_Ext/_Ext2 run the same caller check as its
//     D3D12_Init_Ext (310.8 disassembly: GetModuleFileNameW of the caller
//     must contain "nvngx.dll", else FAIL_PlatformError 0xbad00002, measured
//     on vk_basic before this used LoadDirectApi).  LoadDirectApi installs
//     the D3D12 path's caller identity (InstallCallerIdentity) for both.
using DirectInitVkFn = InitExtFn;
struct NrRuntime {
  DirectInitVkFn init = nullptr;
  Create1Fn create = nullptr;
  EvaluateFn evaluate = nullptr;
  ReleaseFn release = nullptr;
  ShutdownDeviceFn shutdown = nullptr;
  VkDevice device = nullptr;  // the device the runtime is initialized on
  bool failed = false;
};
inline NrRuntime runtime;
// The codec programs: the legacy pair and the v6 family, all from the D3D12
// shaders compiled to SPIR-V (vk_*.comp.slang), on one push-descriptor
// layout (t0..t3 = bindings 0..3, u0/u1 = 4/5) with the 28-dword v6
// constants range (the legacy cbuffer uses its first 20).
enum Program : uint32_t {
  kLegacyEncode,
  kLegacyDecode,
  kLinearize,
  kAutoscale,
  kExposureScale,
  kEncodeV6,
  kResolveV6,
  kPedestalReduce,
  kCommit,
  kCommitExposure,
  kProgramCount,
};
inline reshade::api::pipeline_layout codec_layout = {};
inline reshade::api::pipeline codec_programs[kProgramCount] = {};
inline bool codec_failed = false;
// The look stage's two programs (the D3D12 look_band and look_compose, as
// vk_look_*.comp.slang) on their own layout: t0..t4 = bindings 0..4, u0..u8
// = 5..13 (u7 a storage buffer), the history images = 14/15, 35 constants.
// Without them the look is off for the session and NR runs on.
inline reshade::api::pipeline_layout look_layout = {};
inline reshade::api::pipeline look_band = {};
inline reshade::api::pipeline look_compose = {};
inline std::atomic_uint64_t nr_evaluates{0};
// Feature-18 evaluates on Vulkan: nr_evals times the passes that ran.
inline std::atomic_uint64_t nr_passes{0};
// Live feature 18 handles: the Vulkan share of active_features, which the
// funnel and the status card read.
inline std::atomic_uint32_t live_nr_features{0};

// Compute-state restore (NRVkStateRestore, v8.1; 1 Auto = default, 0 off).
// The codec binds a compute pipeline, pushes descriptors and pushes
// constants on the game's command buffer.  The DLSS guide (5.2.5) makes the
// game re-bind after any NGX evaluate, but DLSS SR on Vulkan launches through
// vkCmdCuLaunchKernelNVX and measurably leaves the compute bind point intact
// (vk_basic, RENODX_E2E_VK_CANARY_REBIND=0), so a game may lean on that - the
// D3D12 line learned the same in the field (ComputeStateEnvelope).  The
// compute bind point is shadowed per command buffer from ReShade's bind
// events and replayed after NR's passes.  Every other stage returns on the
// first compare.
//
// Inexact by construction, counted as vk[restore_partial=] and logged once:
//   - a set whose layout has dynamic descriptors: ReShade's
//     bind_descriptor_tables event drops pDynamicOffsets
//     (vulkan_hooks_command_list.cpp), and a replay without them is invalid
//     usage, so that set is left as NR's passes left it;
//   - a layout created before the addon saw it (no dynamic-set record);
//   - a bind the shadow does not hold (set index >= 8, push constants past
//     256 B, more than 32 pushed writes per set, a push of a type ReShade
//     does not convert).
// Pushed image descriptors replay in ReShade's layouts (sampled images
// SHADER_READ_ONLY_OPTIMAL, storage images GENERAL); the event carries no
// layout.
inline std::atomic_uint32_t state_restore{1};
inline std::atomic_uint64_t restores{0};
inline std::atomic_uint64_t restores_partial{0};
inline std::atomic_bool state_events_registered{false};
inline std::atomic_bool image_events_registered{false};
inline std::atomic_uint64_t sampled_layout_restores{0};

// Every Vulkan pipeline layout the game created, with the mask of its sets
// that hold dynamic descriptors (ReShade passes VK_DESCRIPTOR_TYPE_*_DYNAMIC
// through as the raw values 8 and 9).
inline std::mutex layouts_mutex;
inline std::unordered_map<reshade::api::device*, std::unordered_map<uint64_t, uint32_t>>
    layout_dynamic_sets;

struct __declspec(uuid("6a3f1e52-8c1d-4b7e-9f2a-5d0c3b4e7a91")) ComputeShadow {
  static constexpr uint32_t kSets = 8;
  static constexpr uint32_t kPushDwords = 64;
  static constexpr size_t kPushWrites = 32;
  struct Push {
    uint32_t binding;
    uint32_t array_offset;
    uint32_t count;
    reshade::api::descriptor_type type;
    std::vector<uint64_t> data;
  };
  struct Set {
    reshade::api::pipeline_layout layout = {};
    reshade::api::descriptor_table table = {};  // 0 = pushed
    uint32_t order = 0;                          // 0 = nothing bound
    std::vector<Push> pushes;
  };
  struct Constants {
    reshade::api::shader_stage stages;
    uint32_t first;
    uint32_t count;
  };
  reshade::api::pipeline pipeline = {};
  Set sets[kSets];
  uint32_t next_order = 0;
  reshade::api::pipeline_layout constants_layout = {};
  std::vector<Constants> constants;
  uint32_t constant_data[kPushDwords] = {};
  bool overflow = false;
  std::unordered_map<uint64_t, VkImageLayout> image_states;
};

// Observe every Vulkan device before the first evaluate selects NR's owner,
// including bindings the game made before it created its NGX feature.
inline ComputeShadow* Shadow(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::vulkan) return nullptr;
  ComputeShadow* const shadow = cmd_list->get_private_data<ComputeShadow>();
  return shadow != nullptr ? shadow : cmd_list->create_private_data<ComputeShadow>();
}

// ReShade collapses unknown native layouts (including READ_ONLY_OPTIMAL)
// to `general`. Observe native barriers instead so GENERAL is evidence,
// not a lossy enum conversion. Single-subresource images keep exact state;
// broader subresource tracking is deliberately outside this adapter.
template <typename ImageBarrier>
inline void ObserveImageBarriers(VkCommandBuffer command_buffer, uint32_t count,
                                  const ImageBarrier* barriers) {
  std::shared_lock lock(command_lists_mutex);
  const auto found = command_lists.find(command_buffer);
  if (found == command_lists.end()) return;
  ComputeShadow* const shadow = Shadow(found->second.list);
  if (shadow == nullptr) return;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t image = reinterpret_cast<uint64_t>(barriers[i].image);
    const auto desc = found->second.device->get_resource_desc({image});
    if (desc.type == reshade::api::resource_type::texture_2d
        && desc.texture.levels == 1 && desc.texture.depth_or_layers == 1) {
      shadow->image_states[image] = barriers[i].newLayout;
    }
  }
}

inline void VKAPI_CALL HookedBarrier(
    VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
    VkDependencyFlags flags, uint32_t memories, const VkMemoryBarrier* memory,
    uint32_t buffers, const VkBufferMemoryBarrier* buffer, uint32_t images,
    const VkImageMemoryBarrier* image) {
  CallbackScope callback_scope;
  real_barrier(cmd, src, dst, flags, memories, memory, buffers, buffer, images, image);
  if (callback_scope) ObserveImageBarriers(cmd, images, image);
}
inline void VKAPI_CALL HookedBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* info) {
  CallbackScope callback_scope;
  real_barrier2(cmd, info);
  if (callback_scope) ObserveImageBarriers(cmd, info->imageMemoryBarrierCount, info->pImageMemoryBarriers);
}
inline void VKAPI_CALL HookedBarrier2KHR(VkCommandBuffer cmd, const VkDependencyInfo* info) {
  CallbackScope callback_scope;
  real_barrier2_khr(cmd, info);
  if (callback_scope) ObserveImageBarriers(cmd, info->imageMemoryBarrierCount, info->pImageMemoryBarriers);
}
inline void VKAPI_CALL HookedWaitEvents(
    VkCommandBuffer cmd, uint32_t count, const VkEvent* events,
    VkPipelineStageFlags src, VkPipelineStageFlags dst,
    uint32_t memories, const VkMemoryBarrier* memory,
    uint32_t buffers, const VkBufferMemoryBarrier* buffer,
    uint32_t images, const VkImageMemoryBarrier* image) {
  CallbackScope callback_scope;
  real_wait_events(cmd, count, events, src, dst, memories, memory, buffers, buffer, images, image);
  if (callback_scope) ObserveImageBarriers(cmd, images, image);
}
inline void VKAPI_CALL HookedWaitEvents2(
    VkCommandBuffer cmd, uint32_t count, const VkEvent* events, const VkDependencyInfo* info) {
  CallbackScope callback_scope;
  real_wait_events2(cmd, count, events, info);
  if (callback_scope) {
    for (uint32_t i = 0; i < count; ++i) {
      ObserveImageBarriers(cmd, info[i].imageMemoryBarrierCount, info[i].pImageMemoryBarriers);
    }
  }
}
inline void VKAPI_CALL HookedWaitEvents2KHR(
    VkCommandBuffer cmd, uint32_t count, const VkEvent* events, const VkDependencyInfo* info) {
  CallbackScope callback_scope;
  real_wait_events2_khr(cmd, count, events, info);
  if (callback_scope) {
    for (uint32_t i = 0; i < count; ++i) {
      ObserveImageBarriers(cmd, info[i].imageMemoryBarrierCount, info[i].pImageMemoryBarriers);
    }
  }
}
inline const detour_guard::Hook kBarrierHooks[] = {
    {"vkCmdPipelineBarrier", reinterpret_cast<void**>(&real_barrier),
     reinterpret_cast<void*>(HookedBarrier), &barrier_records[0]},
    {"vkCmdPipelineBarrier2", reinterpret_cast<void**>(&real_barrier2),
     reinterpret_cast<void*>(HookedBarrier2), &barrier_records[1]},
    {"vkCmdPipelineBarrier2KHR", reinterpret_cast<void**>(&real_barrier2_khr),
     reinterpret_cast<void*>(HookedBarrier2KHR), &barrier_records[2]},
    {"vkCmdWaitEvents", reinterpret_cast<void**>(&real_wait_events),
     reinterpret_cast<void*>(HookedWaitEvents), &barrier_records[3]},
    {"vkCmdWaitEvents2", reinterpret_cast<void**>(&real_wait_events2),
     reinterpret_cast<void*>(HookedWaitEvents2), &barrier_records[4]},
    {"vkCmdWaitEvents2KHR", reinterpret_cast<void**>(&real_wait_events2_khr),
     reinterpret_cast<void*>(HookedWaitEvents2KHR), &barrier_records[5]},
};

// The stage mask must be valid on a compute-only queue. ReShade's generic
// shader/UAV usage expands to graphics stages even there. ALL_COMMANDS
// covers precisely the commands supported by this command buffer's queue;
// HOST is additionally needed for the coherent diagnostic readback.
inline void Barrier(reshade::api::command_list* cmd, uint32_t count,
                     const reshade::api::resource* resources,
                     const reshade::api::resource_usage* before,
                     const reshade::api::resource_usage* after,
                     const VkImageSubresourceRange* image_range = nullptr) {
  using namespace reshade::api;
  const auto dispatch_barrier = native_barrier.load(std::memory_order_acquire);
  if (dispatch_barrier == nullptr) {
    cmd->barrier(count, resources, before, after);
    return;
  }
  const auto access = [](resource_usage usage) -> VkAccessFlags {
    if (usage == resource_usage::undefined) return 0;
    if (usage == resource_usage::cpu_access) return 0x2000u | 0x4000u;
    if (usage == resource_usage::general) return 0x8000u | 0x10000u;
    VkAccessFlags flags = 0;
    if ((usage & resource_usage::shader_resource) != 0) flags |= 0x20u;
    if ((usage & resource_usage::unordered_access) != 0) flags |= 0x20u | 0x40u;
    if ((usage & resource_usage::copy_source) != 0) flags |= 0x800u;
    if ((usage & resource_usage::copy_dest) != 0) flags |= 0x1000u;
    return flags;
  };
  const auto layout = [](resource_usage usage) -> VkImageLayout {
    if (usage == resource_usage::undefined) return kLayoutUndefined;
    if (usage == resource_usage::shader_resource) return kLayoutReadOnly;
    if (usage == resource_usage::copy_source) return static_cast<VkImageLayout>(6);
    if (usage == resource_usage::copy_dest) return static_cast<VkImageLayout>(7);
    return kLayoutGeneral;
  };
  if (count > 4) {
    for (uint32_t first = 0; first < count; first += 4) {
      Barrier(cmd, std::min(4u, count - first), resources + first,
               before + first, after + first, image_range);
    }
    return;
  }
  VkMemoryBarrier memory[4] = {};
  VkBufferMemoryBarrier buffers[4] = {};
  VkImageMemoryBarrier images[4] = {};
  uint32_t memory_count = 0, buffer_count = 0, image_count = 0;
  VkPipelineStageFlags src = 0x10000u;
  VkPipelineStageFlags dst = 0x10000u;
  for (uint32_t i = 0; i < count; ++i) {
    if (before[i] == resource_usage::cpu_access) src |= 0x4000u;
    if (after[i] == resource_usage::cpu_access) dst |= 0x4000u;
    if (resources[i].handle == 0) {
      memory[memory_count++] = {static_cast<VkStructureType>(46), nullptr, access(before[i]), access(after[i])};
    } else if (cmd->get_device()->get_resource_desc(resources[i]).type == resource_type::buffer) {
      buffers[buffer_count++] = {static_cast<VkStructureType>(44), nullptr, access(before[i]), access(after[i]),
                         UINT32_MAX, UINT32_MAX, reinterpret_cast<VkBuffer>(resources[i].handle),
                         0, UINT64_MAX};
    } else {
      images[image_count++] = {static_cast<VkStructureType>(45), nullptr, access(before[i]), access(after[i]),
                        layout(before[i]), layout(after[i]), UINT32_MAX, UINT32_MAX,
                        reinterpret_cast<VkImage>(resources[i].handle),
                        image_range != nullptr ? *image_range
                                               : VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, UINT32_MAX, 0, UINT32_MAX}};
    }
  }
  dispatch_barrier(reinterpret_cast<VkCommandBuffer>(cmd->get_native()), src, dst, 0,
                 memory_count, memory, buffer_count, buffers, image_count, images);
}

inline void OnExecuteSecondaryImageStates(reshade::api::command_list* cmd_list,
                                           reshade::api::command_list* secondary) {
  ComputeShadow* const primary = Shadow(cmd_list);
  if (primary == nullptr) return;
  const ComputeShadow* const recorded = secondary->get_private_data<ComputeShadow>();
  if (recorded == nullptr) return;
  for (const auto& [image, state] : recorded->image_states) primary->image_states[image] = state;
}

inline void OnEndTrackedRenderPass(reshade::api::command_list* cmd_list) {
  ComputeShadow* const shadow = Shadow(cmd_list);
  if (shadow == nullptr) return;
  // ReShade exposes color/depth attachments, but omits input/resolve
  // attachments and their implicit transitions. Discard the recording's
  // earlier evidence; only a later explicit barrier proves a layout again.
  shadow->image_states.clear();
}
inline void OnBeginTrackedRenderPass(
    reshade::api::command_list* cmd_list, uint32_t,
    const reshade::api::render_pass_render_target_desc*,
    const reshade::api::render_pass_depth_stencil_desc*) {
  OnEndTrackedRenderPass(cmd_list);
}

// ReShade's sampled descriptors declare SHADER_READ_ONLY_OPTIMAL. A
// GENERAL barrier in this recording is positive evidence to transition
// the game's image temporarily, then restore it exactly. Without evidence,
// preserve the original read-only assumption; never decline a frame for it.
// A submitted buffer's final layout is not such proof: another queue may
// change it before this buffer executes through a future semaphore wait.
struct SampledInputScope {
  reshade::api::command_list* cmd;
  reshade::api::resource image;
  VkImageSubresourceRange range;
  PFN_vkCmdPipelineBarrier dispatch_barrier;
  VkImageLayout original = kLayoutUndefined;
  SampledInputScope(reshade::api::command_list* command_list,
                    const NVSDK_NGX_Resource_VK* input)
      : cmd(command_list),
        image{reinterpret_cast<uint64_t>(input->Resource.ImageViewInfo.Image)},
        range(input->Resource.ImageViewInfo.SubresourceRange),
        dispatch_barrier(native_barrier.load(std::memory_order_acquire)) {
    VkImageLayout state = kLayoutUndefined;
    if (const ComputeShadow* const shadow = cmd->get_private_data<ComputeShadow>()) {
      const auto found = shadow->image_states.find(image.handle);
      if (found != shadow->image_states.end()) state = found->second;
    }
    if (dispatch_barrier != nullptr && layout_observation_complete.load(std::memory_order_acquire)
        && (state == kLayoutGeneral || state == kLayoutReadOnlyOptimal)) {
      original = state;
      const VkImageMemoryBarrier barrier = {
          static_cast<VkStructureType>(45), nullptr, 0x8000u | 0x10000u, 0x20u,
          original, kLayoutReadOnly, UINT32_MAX, UINT32_MAX,
          reinterpret_cast<VkImage>(image.handle), range};
      dispatch_barrier(reinterpret_cast<VkCommandBuffer>(cmd->get_native()),
                     0x10000u, 0x10000u, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
  }
  ~SampledInputScope() {
    if (original == kLayoutUndefined) return;
    const VkImageMemoryBarrier barrier = {
        static_cast<VkStructureType>(45), nullptr, 0x20u, 0x8000u | 0x10000u,
        kLayoutReadOnly, original, UINT32_MAX, UINT32_MAX,
        reinterpret_cast<VkImage>(image.handle), range};
    dispatch_barrier(reinterpret_cast<VkCommandBuffer>(cmd->get_native()),
                   0x10000u, 0x10000u, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    sampled_layout_restores.fetch_add(1, std::memory_order_relaxed);
  }
};

// Exact native-layout scope for game-owned color/output images. Unlike the
// sampled-input compatibility path, injection declines without local proof.
struct ImageInputScope {
  reshade::api::command_list* cmd;
  NVSDK_NGX_ImageViewInfo_VK info;
  PFN_vkCmdPipelineBarrier dispatch;
  VkImageLayout original = kLayoutUndefined;
  VkImageLayout current = kLayoutUndefined;
  ImageInputScope(reshade::api::command_list* list, const NVSDK_NGX_ImageViewInfo_VK& image)
      : cmd(list), info(image), dispatch(native_barrier.load(std::memory_order_acquire)) {
    if (!dispatch || !layout_observation_complete.load(std::memory_order_acquire)) return;
    if (const auto* shadow = cmd->get_private_data<ComputeShadow>()) {
      const auto found = shadow->image_states.find(reinterpret_cast<uint64_t>(info.Image));
      if (found != shadow->image_states.end()) original = current = found->second;
    }
  }
  bool Valid() const {
    return original == kLayoutGeneral || original == kLayoutReadOnly
        || original == kLayoutReadOnlyOptimal || original == static_cast<VkImageLayout>(6)
        || original == static_cast<VkImageLayout>(7);
  }
  void Transition(VkImageLayout next) {
    const VkImageMemoryBarrier barrier = {
        static_cast<VkStructureType>(45), nullptr, 0x8000u | 0x10000u, 0x8000u | 0x10000u,
        current, next, UINT32_MAX, UINT32_MAX, info.Image, info.SubresourceRange};
    dispatch(reinterpret_cast<VkCommandBuffer>(cmd->get_native()),
             0x10000u, 0x10000u, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    current = next;
  }
  ~ImageInputScope() { if (Valid() && current != original) Transition(original); }
};

// The descriptor stays alive through the real SR call; the parameter block
// is restored even when either the injection or the game evaluate throws.
struct RenderColorScope {
  NVSDK_NGX_Parameter* parameters;
  void* original = nullptr;
  NVSDK_NGX_Resource_VK resource = {};
  bool redirected = false;
  reshade::api::command_list* cmd = nullptr;
  VkImageLayout layout = kLayoutUndefined;
  explicit RenderColorScope(const NVSDK_NGX_Parameter* block)
      : parameters(const_cast<NVSDK_NGX_Parameter*>(block)) {}
  void Redirect(const NVSDK_NGX_Resource_VK& color) {
    parameters->Get(NVSDK_NGX_Parameter_Color, &original);
    resource = color;
    redirected = true;
    parameters->Set(NVSDK_NGX_Parameter_Color, static_cast<void*>(&resource));
  }
  ~RenderColorScope() {
    if (!redirected) return;
    parameters->Set(NVSDK_NGX_Parameter_Color, original);
    // NGX preserves the input layout by contract. Restore our resting GENERAL
    // layout after SR; workset state never retains a game-specific layout.
    const VkImageMemoryBarrier barrier = {
        static_cast<VkStructureType>(45), nullptr, 0x8000u | 0x10000u, 0x8000u | 0x10000u,
        layout, kLayoutGeneral, UINT32_MAX, UINT32_MAX,
        resource.Resource.ImageViewInfo.Image, resource.Resource.ImageViewInfo.SubresourceRange};
    native_barrier.load(std::memory_order_acquire)(
        reinterpret_cast<VkCommandBuffer>(cmd->get_native()), 0x10000u, 0x10000u, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
  }
};

inline void OnBindPipeline(reshade::api::command_list* cmd_list,
                           reshade::api::pipeline_stage stages,
                           reshade::api::pipeline pipeline) {
  if (stages != reshade::api::pipeline_stage::all_compute) return;
  if (ComputeShadow* const shadow = Shadow(cmd_list)) shadow->pipeline = pipeline;
}

inline void OnBindDescriptorTables(reshade::api::command_list* cmd_list,
                                   reshade::api::shader_stage stages,
                                   reshade::api::pipeline_layout layout, uint32_t first,
                                   uint32_t count,
                                   const reshade::api::descriptor_table* tables) {
  if (stages != reshade::api::shader_stage::all_compute) return;
  ComputeShadow* const shadow = Shadow(cmd_list);
  if (shadow == nullptr) return;
  for (uint32_t i = 0; i < count; ++i) {
    if (first + i >= ComputeShadow::kSets) {
      shadow->overflow = true;
      continue;
    }
    ComputeShadow::Set& set = shadow->sets[first + i];
    set.layout = layout;
    set.table = tables[i];
    set.order = ++shadow->next_order;
    set.pushes.clear();
  }
}

inline void OnPushDescriptors(reshade::api::command_list* cmd_list,
                              reshade::api::shader_stage stages,
                              reshade::api::pipeline_layout layout, uint32_t layout_param,
                              const reshade::api::descriptor_table_update& update) {
  if (stages != reshade::api::shader_stage::all_compute) return;
  ComputeShadow* const shadow = Shadow(cmd_list);
  if (shadow == nullptr) return;
  if (layout_param >= ComputeShadow::kSets || update.count == 0
      || update.descriptors == nullptr) {
    shadow->overflow = true;
    return;
  }
  ComputeShadow::Set& set = shadow->sets[layout_param];
  if (set.table.handle != 0 || set.layout != layout) set.pushes.clear();
  set.layout = layout;
  set.table = {};
  set.order = ++shadow->next_order;
  // Handles per descriptor: a buffer_range is three, a combined image
  // sampler two, every other type one.
  const size_t words =
      update.count
      * (update.type == reshade::api::descriptor_type::constant_buffer
                 || update.type == reshade::api::descriptor_type::shader_storage_buffer
             ? 3
         : update.type == reshade::api::descriptor_type::sampler_with_resource_view ? 2
                                                                                     : 1);
  const auto* const data = static_cast<const uint64_t*>(update.descriptors);
  // Preserve write order for overlapping updates. Replacing an older write
  // in place replays it before a newer overlapping write, and a shorter
  // replacement also loses still-live descriptors from the original tail.
  std::erase_if(set.pushes, [&](const auto& p) {
    return p.binding == update.binding && p.array_offset >= update.array_offset
        && static_cast<uint64_t>(p.array_offset) + p.count
            <= static_cast<uint64_t>(update.array_offset) + update.count;
  });
  if (set.pushes.size() >= ComputeShadow::kPushWrites) {
    shadow->overflow = true;
    return;
  }
  auto push = set.pushes.insert(set.pushes.end(), {update.binding, update.array_offset});
  push->count = update.count;
  push->type = update.type;
  push->data.assign(data, data + words);
}

inline void OnPushConstants(reshade::api::command_list* cmd_list,
                            reshade::api::shader_stage stages,
                            reshade::api::pipeline_layout layout, uint32_t, uint32_t first,
                            uint32_t count, const void* values) {
  if ((stages & reshade::api::shader_stage::compute) == 0) return;
  ComputeShadow* const shadow = Shadow(cmd_list);
  if (shadow == nullptr) return;
  if (first + count > ComputeShadow::kPushDwords) {
    shadow->overflow = true;
    return;
  }
  if (shadow->constants_layout != layout) shadow->constants.clear();
  shadow->constants_layout = layout;
  std::memcpy(shadow->constant_data + first, values, count * sizeof(uint32_t));
  const bool known =
      std::any_of(shadow->constants.begin(), shadow->constants.end(), [&](const auto& c) {
        return c.stages == stages && c.first == first && c.count == count;
      });
  if (!known) shadow->constants.push_back({stages, first, count});
}

inline void OnInitPipelineLayout(reshade::api::device* device, uint32_t count,
                                 const reshade::api::pipeline_layout_param* params,
                                 reshade::api::pipeline_layout layout) {
  if (device->get_api() != reshade::api::device_api::vulkan) return;
  uint32_t dynamic_sets = 0;
  for (uint32_t i = 0; i < count && i < 32; ++i) {
    const auto& param = params[i];
    const bool with_samplers =
        param.type == reshade::api::pipeline_layout_param_type::descriptor_table_with_static_samplers;
    if (param.type != reshade::api::pipeline_layout_param_type::descriptor_table
        && !with_samplers) {
      continue;
    }
    const uint32_t ranges = with_samplers ? param.descriptor_table_with_static_samplers.count
                                          : param.descriptor_table.count;
    for (uint32_t r = 0; r < ranges; ++r) {
      const uint32_t type = static_cast<uint32_t>(
          with_samplers ? param.descriptor_table_with_static_samplers.ranges[r].type
                        : param.descriptor_table.ranges[r].type);
      if (type == 8 || type == 9) dynamic_sets |= 1u << i;
    }
  }
  std::lock_guard lock(layouts_mutex);
  layout_dynamic_sets[device][layout.handle] = dynamic_sets;
}

inline void OnDestroyPipelineLayout(reshade::api::device* device,
                                    reshade::api::pipeline_layout layout) {
  if (device->get_api() != reshade::api::device_api::vulkan) return;
  std::lock_guard lock(layouts_mutex);
  if (const auto found = layout_dynamic_sets.find(device); found != layout_dynamic_sets.end()) {
    found->second.erase(layout.handle);
  }
}

// After NR's passes on `cmd_list`: the game's compute bind point as it was
// before the evaluate, in the order the game bound it (a bind disturbs the
// sets an incompatible layout invalidates, so the replay disturbs the same).
inline void RestoreComputeState(reshade::api::command_list* cmd_list) {
  using namespace reshade::api;
  if (state_restore.load(std::memory_order_relaxed) == 0) return;
  const ComputeShadow* const shadow = cmd_list->get_private_data<ComputeShadow>();
  if (shadow == nullptr) return;
  bool partial = shadow->overflow;
  if (shadow->pipeline.handle != 0) cmd_list->bind_pipeline(pipeline_stage::all_compute, shadow->pipeline);
  uint32_t order[ComputeShadow::kSets];
  uint32_t bound = 0;
  for (uint32_t i = 0; i < ComputeShadow::kSets; ++i) {
    if (shadow->sets[i].order != 0) order[bound++] = i;
  }
  std::sort(order, order + bound, [&](uint32_t a, uint32_t b) {
    return shadow->sets[a].order < shadow->sets[b].order;
  });
  for (uint32_t k = 0; k < bound; ++k) {
    const uint32_t index = order[k];
    const ComputeShadow::Set& set = shadow->sets[index];
    if (set.table.handle == 0) {
      for (const auto& push : set.pushes) {
        cmd_list->push_descriptors(shader_stage::all_compute, set.layout, index,
                                   {{}, push.binding, push.array_offset, push.count,
                                    push.type, push.data.data()});
      }
      continue;
    }
    bool exact = false;
    {
      std::lock_guard lock(layouts_mutex);
      const auto device = layout_dynamic_sets.find(cmd_list->get_device());
      if (device != layout_dynamic_sets.end()) {
        const auto found = device->second.find(set.layout.handle);
        exact = found != device->second.end() && (found->second & (1u << index)) == 0;
      }
    }
    if (exact) {
      cmd_list->bind_descriptor_tables(shader_stage::all_compute, set.layout, index, 1, &set.table);
    } else {
      partial = true;
    }
  }
  for (const auto& constants : shadow->constants) {
    cmd_list->push_constants(constants.stages, shadow->constants_layout, 0, constants.first,
                             constants.count, shadow->constant_data + constants.first);
  }
  restores.fetch_add(1, std::memory_order_relaxed);
  if (partial && restores_partial.fetch_add(1, std::memory_order_relaxed) == 0) {
    Log(reshade::log::level::warning,
        "Vulkan compute-state restore was partial (vk_restore_partial): a set with"
        " dynamic descriptors, a layout created before the addon, or a bind the shadow"
        " does not hold stays as NR's passes left it; a game that re-binds after DLSS,"
        " as the DLSS guide asks, is unaffected");
  }
}

// ReShade calls init_device from inside its vkCreateDevice. At that point
// the loader has installed a zeroed dispatch table on VkDevice, but fills
// its entries only after the layer chain returns. Resolving observers there
// returns null and must not latch installation complete. The game's first
// vkBeginCommandBuffer is past that boundary and precedes its first barrier.
inline void EnsureBarrierObservers(reshade::api::device* device) {
  if (barrier_install_attempted.load(std::memory_order_acquire)) return;
  std::lock_guard install(barrier_install_mutex);
  if (barrier_install_attempted.load(std::memory_order_relaxed)
      || !hooks_enabled.load(std::memory_order_relaxed)
      || shutting_down.load(std::memory_order_acquire)) {
    return;
  }
  const HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
  const auto device_proc = loader != nullptr
      ? reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(loader, "vkGetDeviceProcAddr"))
      : nullptr;
  if (device_proc != nullptr) {
    native_barrier.store(
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(GetProcAddress(loader, "vkCmdPipelineBarrier")),
        std::memory_order_release);
    real_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
        device_proc(reinterpret_cast<VkDevice>(device->get_native()), "vkCmdPipelineBarrier"));
  }
  if (real_barrier == nullptr) {
    if (!barrier_install_warned) {
      barrier_install_warned = true;
      Log(reshade::log::level::warning,
          "Vulkan native barrier observers: resolved=0 attached=0; the loader's"
          " core barrier is not ready, retrying on the next command recording");
    }
    return;
  }
  const auto native_device = reinterpret_cast<VkDevice>(device->get_native());
  real_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(device_proc(native_device, "vkCmdPipelineBarrier2"));
  real_barrier2_khr = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(device_proc(native_device, "vkCmdPipelineBarrier2KHR"));
  if (real_barrier2_khr == real_barrier2) real_barrier2_khr = nullptr;
  real_wait_events = reinterpret_cast<PFN_vkCmdWaitEvents>(device_proc(native_device, "vkCmdWaitEvents"));
  real_wait_events2 = reinterpret_cast<PFN_vkCmdWaitEvents2>(device_proc(native_device, "vkCmdWaitEvents2"));
  real_wait_events2_khr = reinterpret_cast<PFN_vkCmdWaitEvents2>(device_proc(native_device, "vkCmdWaitEvents2KHR"));
  if (real_wait_events2_khr == real_wait_events2) real_wait_events2_khr = nullptr;
  const size_t resolved = std::count_if(std::begin(kBarrierHooks), std::end(kBarrierHooks),
      [](const detour_guard::Hook& hook) { return *hook.real != nullptr; });
  size_t failed = 0;
  const size_t attached = detour_guard::AttachAll(kBarrierHooks, &failed);
  barrier_observers_resolved.store(static_cast<uint32_t>(resolved), std::memory_order_relaxed);
  barrier_observers_attached.store(static_cast<uint32_t>(attached), std::memory_order_relaxed);
  // Partial coverage can leave stale evidence after an unobserved wait or
  // barrier. Keep the read-only fallback unless every resolved entry that
  // can transition an image is observed.
  const bool complete = attached == resolved && real_barrier != nullptr;
  layout_observation_complete.store(complete, std::memory_order_release);
  // AttachAll rewrites the real pointers. Never send an already attached
  // partial batch through it again. A transaction that attached nothing and
  // failed no entry is transient; it can retry without losing a trampoline.
  barrier_install_attempted.store(attached != 0 || failed != 0, std::memory_order_release);
  if (attached != 0 || failed != 0 || !barrier_install_warned) {
    Log(complete ? reshade::log::level::info : reshade::log::level::warning,
        "Vulkan native barrier observers: resolved=" + std::to_string(resolved)
            + " attached=" + std::to_string(attached)
            + " failed=" + std::to_string(failed)
            + (complete ? "; exact input layouts are observed"
                        : "; unobserved input layouts keep the read-only fallback"));
  }
  barrier_install_warned = !complete;
}

// ReShade's command list for a VkCommandBuffer, learned when the game begins
// recording it (reset_command_list fires in vkBeginCommandBuffer): the
// add-on API has no lookup from a native command buffer.  Registered only
// once a Vulkan device is seen.
inline std::atomic_bool command_lists_registered{false};
inline void OnResetCommandList(reshade::api::command_list* cmd_list) {
  CallbackScope callback_scope;
  if (!callback_scope || cmd_list == nullptr
      || cmd_list->get_device()->get_api() != reshade::api::device_api::vulkan) {
    return;
  }
  EnsureBarrierObservers(cmd_list->get_device());
  // vkBeginCommandBuffer: a new recording starts with no state bound.
  RetireScaleSamples(cmd_list);
  ForgetRecordedUses(cmd_list);
  if (ComputeShadow* const shadow = cmd_list->get_private_data<ComputeShadow>()) {
    shadow->pipeline = {};
    for (auto& set : shadow->sets) {
      set.order = 0;
      set.pushes.clear();
    }
    shadow->next_order = 0;
    shadow->constants.clear();
    shadow->overflow = false;
    shadow->image_states.clear();
  }
  std::unique_lock lock(command_lists_mutex);
  command_lists[reinterpret_cast<VkCommandBuffer>(cmd_list->get_native())] = {
      cmd_list, cmd_list->get_device()};
}
inline void ForgetCommandList(reshade::api::command_list* cmd_list) {
  RetireScaleSamples(cmd_list);
  ForgetRecordedUses(cmd_list);
  const uint64_t native = cmd_list->get_native();
  if (cmd_list->get_private_data<ComputeShadow>() != nullptr) {
    cmd_list->destroy_private_data<ComputeShadow>();
  }
  std::unique_lock lock(command_lists_mutex);
  command_lists.erase(reinterpret_cast<VkCommandBuffer>(native));
}

// Caller holds runtime_mutex.  Returns kCount when NR's runtime is live on
// `device`, else the decline for this evaluate (a runtime not loaded yet or a
// context not observed yet retries on the next one; a failed Init is latched
// and logged once).
inline NrDeclineReason EnsureRuntime(VkDevice device) {
  if (runtime.device == device) return NrDeclineReason::kCount;
  if (runtime.failed || !LoadDirectApi()) return NrDeclineReason::kNrRuntimeUnavailable;
  const auto [instance, physical] = ContextFor(device);
  if (instance == nullptr || physical == nullptr) return NrDeclineReason::kVkNrPending;
  const auto resolve = [](const char* name) { return GetProcAddress(direct_api.module, name); };
  runtime.init = reinterpret_cast<DirectInitVkFn>(resolve("NVSDK_NGX_VULKAN_Init_Ext"));
  runtime.create = reinterpret_cast<Create1Fn>(resolve("NVSDK_NGX_VULKAN_CreateFeature1"));
  runtime.evaluate = reinterpret_cast<EvaluateFn>(resolve("NVSDK_NGX_VULKAN_EvaluateFeature"));
  runtime.release = reinterpret_cast<ReleaseFn>(resolve("NVSDK_NGX_VULKAN_ReleaseFeature"));
  runtime.shutdown = reinterpret_cast<ShutdownDeviceFn>(resolve("NVSDK_NGX_VULKAN_Shutdown1"));
  NVSDK_NGX_Result result = NVSDK_NGX_Result_FAIL_FeatureNotSupported;
  if (runtime.init != nullptr && runtime.create != nullptr && runtime.evaluate != nullptr
      && runtime.release != nullptr && runtime.shutdown != nullptr) {
    DirectCallScope direct;
    try {
      result = runtime.init(kDirectApplicationId, AddonDirectory().c_str(), instance,
                            physical, device, NVSDK_NGX_Version_API, nullptr);
    } catch (...) {
      result = NVSDK_NGX_Result_FAIL_UnableToInitializeFeature;
    }
  }
  last_result = static_cast<uint32_t>(result);
  if (NVSDK_NGX_FAILED(result)) {
    runtime.failed = true;
    char text[200];
    snprintf(text, sizeof(text),
             "NR's Vulkan Init on the game's device failed with 0x%08x; Vulkan"
             " frames keep the game's image (nr_runtime_unavailable)",
             static_cast<uint32_t>(result));
    Log(reshade::log::level::error, text);
    return NrDeclineReason::kNrRuntimeUnavailable;
  }
  runtime.device = device;
  Log(reshade::log::level::info,
      "NR runtime initialized on the game's Vulkan device (Init_Ext with the"
      " matching instance and physical device)");
  return NrDeclineReason::kCount;
}

// Caller holds runtime_mutex.  Set 0 bindings 0..3 sampled (Original, Proxy,
// Neural, OutputOriginal) and 4 storage (Output), the legacy cbuffer's 20
// dwords as push constants (legacy_common.hlsli under RENODX_VULKAN).
inline bool EnsureCodec(reshade::api::device* device) {
  if (codec_programs[kProgramCount - 1].handle != 0) return true;
  if (codec_failed) return false;
  using namespace reshade::api;
  const descriptor_range ranges[] = {
      {0, 0, 0, 4, shader_stage::all_compute, 1, descriptor_type::texture_shader_resource_view},
      {4, 0, 0, 2, shader_stage::all_compute, 1, descriptor_type::texture_unordered_access_view},
  };
  pipeline_layout_param params[2];
  params[0].type = pipeline_layout_param_type::push_descriptors_with_ranges;
  params[0].descriptor_table = {2, ranges};
  params[1] = constant_range{0, 0, 0, 28, shader_stage::all_compute};
  const std::span<const std::uint8_t> code[kProgramCount] = {
      __vk_legacy_encode, __vk_legacy_decode, __vk_v6_linearize, __vk_v6_autoscale,
      __vk_v6_exposure_scale, __vk_v6_encode, __vk_v6_resolve, __vk_v6_pedestal_reduce,
      __vk_v6_commit, __vk_v6_commit_exposure};
  bool made = device->create_pipeline_layout(2, params, &codec_layout);
  for (uint32_t i = 0; made && i < kProgramCount; ++i) {
    shader_desc shader = {code[i].data(), code[i].size(), "main"};
    const pipeline_subobject subobject = {pipeline_subobject_type::compute_shader, 1, &shader};
    made = device->create_pipeline(codec_layout, 1, &subobject, &codec_programs[i]);
  }
  if (!made) {
    codec_failed = true;
    Log(reshade::log::level::error,
        "the Vulkan codec pipelines could not be created; Vulkan frames keep"
        " the game's image (workset_setup_failed)");
    return false;
  }
  const descriptor_range look_ranges[] = {
      {0, 0, 0, 5, shader_stage::all_compute, 1, descriptor_type::texture_shader_resource_view},
      {5, 0, 0, 7, shader_stage::all_compute, 1, descriptor_type::texture_unordered_access_view},
      {12, 0, 0, 1, shader_stage::all_compute, 1, descriptor_type::shader_storage_buffer},
      {13, 0, 0, 1, shader_stage::all_compute, 1, descriptor_type::texture_unordered_access_view},
      {14, 0, 0, 2, shader_stage::all_compute, 1, descriptor_type::texture_unordered_access_view},
  };
  pipeline_layout_param look_params[2];
  look_params[0].type = pipeline_layout_param_type::push_descriptors_with_ranges;
  look_params[0].descriptor_table = {5, look_ranges};
  look_params[1] = constant_range{0, 0, 0, sizeof(look::Constants) / 4, shader_stage::all_compute};
  shader_desc band_shader = {__vk_look_band.data(), __vk_look_band.size(), "main"};
  shader_desc compose_shader = {__vk_look_compose.data(), __vk_look_compose.size(), "main"};
  const pipeline_subobject band = {pipeline_subobject_type::compute_shader, 1, &band_shader};
  const pipeline_subobject compose = {pipeline_subobject_type::compute_shader, 1,
                                      &compose_shader};
  if (!device->create_pipeline_layout(2, look_params, &look_layout)
      || !device->create_pipeline(look_layout, 1, &band, &look_band)
      || !device->create_pipeline(look_layout, 1, &compose, &look_compose)) {
    look_compose = {};
    look_unavailable = true;
    Log(reshade::log::level::error,
        "NR look stage off for this session on Vulkan: its programs could not be"
        " created; Neural Rendering continues without it");
  }
  return true;
}

inline void DestroyWorkset(reshade::api::device* device, const Workset& workset) {
  for (const auto view : workset.views) {
    if (view.handle != 0) device->destroy_resource_view(view);
  }
  for (const auto image : workset.images) {
    if (image.handle != 0) device->destroy_resource(image);
  }
}

inline void DestroyLookHistoryImage(reshade::api::device* device,
                                   const LookHistoryImage& image) {
  if (image.view.handle != 0) device->destroy_resource_view(image.view);
  if (image.image.handle != 0) device->destroy_resource(image.image);
}

// Caller holds runtime_mutex. A reset/freed command buffer cannot replay its
// old recording; until then even a completed submission is still a live use.
inline void DrainRetiredFeature(StageFeature* feature) {
  // Keep is the same retained-resource comparison on Vulkan as on D3D12:
  // completed recordings permit reuse, but frees wait for feature teardown.
  if (workset_free_policy.load(std::memory_order_relaxed) != 0) return;
  reshade::api::device* const device = feature->device;
  for (auto it = feature->retired_worksets.begin(); it != feature->retired_worksets.end();) {
    if (!RecordedUseComplete(it->use_id)) {
      ++it;
      continue;
    }
    DestroyWorkset(device, *it);
    it = feature->retired_worksets.erase(it);
  }
  for (auto it = feature->retired_history.begin(); it != feature->retired_history.end();) {
    if (!RecordedUseComplete(it->use_id)) {
      ++it;
      continue;
    }
    for (const LookHistoryImage& half : it->buffers) {
      DestroyLookHistoryImage(device, half);
    }
    it = feature->retired_history.erase(it);
  }
  for (auto it = feature->retired_nr.begin(); it != feature->retired_nr.end();) {
    if (!RecordedUseComplete(it->use_id)) {
      ++it;
      continue;
    }
    {
      DirectCallScope direct;
      runtime.release(it->handle);
    }
    if (it->parameters != nullptr) bridge::DestroyOwnedParameters(it->parameters);
    it = feature->retired_nr.erase(it);
  }
}

// Caller holds runtime_mutex.  The frees a DLSS handle's release (or the
// device's teardown) has proven safe; see "Lifetime" above.  `release_nr`
// false drops the NR handles unreleased: inside destroy_device the runtime's
// own Vulkan calls go through a ReShade layer that has already let go of the
// device (vk_basic, 2026-09-25: an access violation in ReShade64.dll on the
// runtime's Shutdown1 there), so they are released at the game's NGX
// Shutdown instead (HookedShutdown) and only a game that skips it leaves
// them to die with the device.
inline void FreeStageNr(StageFeature* feature, bool release_nr = true) {
  reshade::api::device* const device = feature->device;
  for (const Workset& workset : feature->worksets) {
    DestroyWorkset(device, workset);
  }
  feature->worksets.clear();
  for (const Workset& workset : feature->retired_worksets) {
    DestroyWorkset(device, workset);
  }
  feature->retired_worksets.clear();
  for (LookHistoryVk& history : feature->look_history) {
    for (const LookHistoryImage& half : history.buffers) {
      DestroyLookHistoryImage(device, half);
    }
    history = {};
  }
  for (const RetiredLookHistory& retired : feature->retired_history) {
    for (const LookHistoryImage& half : retired.buffers) {
      DestroyLookHistoryImage(device, half);
    }
  }
  feature->retired_history.clear();
  for (uint64_t& id : feature->look_history_use) id = 0;
  if (feature->commit_view.handle != 0) device->destroy_resource_view(feature->commit_view);
  if (feature->commit.handle != 0) device->destroy_resource(feature->commit);
  feature->commit = {};
  feature->commit_view = {};
  feature->commit_cleared = false;
  for (uint32_t pass = 0; pass < kMaxNrPasses; ++pass) {
    NrFeatureSlot& slot = feature->slots[pass];
    if (slot.handle != nullptr) {
      feature->retired_nr.push_back({slot.handle, feature->nr_use[pass]});
      live_nr_features.fetch_sub(1, std::memory_order_relaxed);
    }
    slot.handle = nullptr;
    feature->nr_use[pass] = 0;
  }
  if (release_nr) {
    DirectCallScope direct;
    for (const RetiredNrHandle& retired : feature->retired_nr) {
      runtime.release(retired.handle);
    }
  }
  for (const auto& retired : feature->retired_nr) {
    if (retired.parameters != nullptr) bridge::DestroyOwnedParameters(retired.parameters);
  }
  feature->retired_nr.clear();
  for (NrFeatureSlot& slot : feature->slots) {
    if (slot.parameters != nullptr) {
      bridge::DestroyOwnedParameters(slot.parameters);
      slot.parameters = nullptr;
    }
    slot = {};
  }
  feature->commit_use = 0;
  feature->ngx.norm = {};
  feature->nr_width = feature->nr_height = 0;
}

inline void FreeFeatureNr(Feature* feature, bool release_nr = true) {
  for (auto& stage : feature->stages) FreeStageNr(&stage, release_nr);
}

// Changing count/parameter-channel ownership must not free objects that a
// recorded command buffer can replay. Retire them with the same use IDs as
// resize; the committed divisor is packaged as a one-image retired workset.
inline void RetireStagePasses(StageFeature* feature, uint32_t first) {
  for (uint32_t pass = first; pass < kMaxNrPasses; ++pass) {
    auto& slot = feature->slots[pass];
    if (slot.handle != nullptr) {
      feature->retired_nr.push_back({slot.handle, feature->nr_use[pass], slot.parameters});
      live_nr_features.fetch_sub(1, std::memory_order_relaxed);
    } else if (slot.parameters != nullptr) {
      bridge::DestroyOwnedParameters(slot.parameters);
    }
    slot = {};
    feature->nr_use[pass] = 0;
    auto& history = feature->look_history[pass];
    if (history.buffers[0].image.handle != 0 || history.buffers[1].image.handle != 0) {
      feature->retired_history.push_back(
          {{history.buffers[0], history.buffers[1]}, feature->look_history_use[pass]});
    }
    history = {};
    feature->look_history_use[pass] = 0;
  }
  if (first != 0) return;
  for (auto& workset : feature->worksets) feature->retired_worksets.push_back(std::move(workset));
  feature->worksets.clear();
  if (feature->commit.handle != 0) {
    Workset retired;
    retired.images[0] = feature->commit;
    retired.views[0] = feature->commit_view;
    retired.use_id = feature->commit_use;
    feature->retired_worksets.push_back(retired);
  }
  feature->commit = {};
  feature->commit_view = {};
  feature->commit_cleared = false;
  feature->commit_use = 0;
  feature->ngx.norm = {};
  feature->nr_width = feature->nr_height = 0;
}

inline NVSDK_NGX_Resource_VK ImageResource(reshade::api::resource_view view,
                                           reshade::api::resource image, uint32_t width,
                                           uint32_t height, bool read_write) {
  NVSDK_NGX_Resource_VK resource = {};
  resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
  resource.ReadWrite = read_write;
  resource.Resource.ImageViewInfo = {
      reinterpret_cast<VkImageView>(view.handle), reinterpret_cast<VkImage>(image.handle),
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, VK_FORMAT_R16G16B16A16_SFLOAT, width, height};
  return resource;
}

// After the game's DLSS evaluate succeeded on `command_buffer`.  Returns the
// decline for this evaluate, kCount when NR ran.
// The divisor readout's record half (see ScaleSample): `scale` rests in
// SHADER_READ_ONLY and is written for this frame.
inline void SampleScale(reshade::api::command_list* cmd, reshade::api::device* api_device,
                        reshade::api::resource scale, uint32_t workset, bool guarded) {
  using namespace reshade::api;
  namespace probe = norm_trace::guard_probe;
  if (scale_calls++ % probe::kEvery != 0) return;
  std::lock_guard lock(scale_samples_mutex);
  if (scale_readback.handle == 0) {
    // ReShade does not invalidate noncoherent GPU-to-CPU allocations on
    // map. CPU-to-GPU requests HOST_COHERENT, and is equally legal as a
    // transfer destination; this tiny readout needs that visibility guarantee.
    if (!api_device->create_resource(
            resource_desc(probe::kSlots * 4 * sizeof(float), memory_heap::cpu_to_gpu,
                          resource_usage::copy_dest),
            nullptr, resource_usage::cpu_access, &scale_readback)) {
      divisor_lost.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    void* mapped = nullptr;
    if (!api_device->map_buffer_region(scale_readback, 0, UINT64_MAX, map_access::read_write,
                                       &mapped)) {
      api_device->destroy_resource(scale_readback);
      scale_readback = {};
      divisor_lost.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    scale_mapped = static_cast<float*>(mapped);
  }
  for (uint32_t i = 0; i < probe::kSlots; ++i) {
    ScaleSample& sample = scale_samples[i];
    if (!sample.recorded || !sample.retired) continue;
    const float* const texel = scale_mapped + i * 4;
    if (texel[0] == norm_trace::kUnwritten) {
      divisor_lost.fetch_add(1, std::memory_order_relaxed);
    } else {
      divisor_last.store(texel[0], std::memory_order_relaxed);
      divisor_reads.fetch_add(1, std::memory_order_relaxed);
    }
    if (sample.guarded) probe::Publish(texel, sample.workset);
    sample = {};
  }
  uint32_t scanned = 0;
  while (scanned < probe::kSlots && scale_samples[scale_head].recorded) {
    scale_head = (scale_head + 1) % probe::kSlots;
    ++scanned;
  }
  if (scanned == probe::kSlots) {
    divisor_lost.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  ScaleSample& sample = scale_samples[scale_head];
  float* const texel = scale_mapped + scale_head * 4;
  scale_head = (scale_head + 1) % probe::kSlots;
  texel[0] = norm_trace::kUnwritten;
  const resource list[] = {scale, scale_readback};
  const resource_usage before[] = {resource_usage::shader_resource, resource_usage::cpu_access};
  const resource_usage after[] = {resource_usage::copy_source, resource_usage::copy_dest};
  Barrier(cmd, 2, list, before, after);
  cmd->copy_texture_to_buffer(scale, 0, nullptr, scale_readback,
                              (&sample - scale_samples) * 4 * sizeof(float), 0, 0);
  Barrier(cmd, 2, list, after, before);
  sample = {true, guarded, workset, cmd, false};
}

inline NrDeclineReason ProcessNr(VkCommandBuffer command_buffer,
                                 const NVSDK_NGX_Handle* handle,
                                 const NVSDK_NGX_Parameter* parameters,
                                 stages::Point stage,
                                 RenderColorScope* render_color = nullptr) {
  using namespace reshade::api;
  const stages::Scope stage_scope(stage);
  const bool pre_sr = stage == stages::Point::Render;
  if (stage == stages::Point::Present) return NrDeclineReason::kSourceOther;
  const uint32_t stack = stages::Count();
  if (!stages::Current().Valid()) return NrDeclineReason::kMalformedEvaluate;
  // Reconcile even zero-count stages before inspecting any image. No caller
  // may hold runtime_mutex across ProcessNr (CheckExtensions uses the loader).
  {
    RuntimeLock lock(runtime_mutex);
    const auto found = features.find(handle);
    if (found == features.end()) return NrDeclineReason::kNgxNotDlssEvaluation;
    auto& feature = found->second.stages[pre_sr ? 0 : 1];
    feature.device = found->second.device;
    feature.create_flags = found->second.create_flags;
    const uint32_t offset = stages::Channel(0);
    if (feature.channel_offset != offset) RetireStagePasses(&feature, 0);
    feature.channel_offset = offset;
    RetireStagePasses(&feature, stack);
    DrainRetiredFeature(&feature);
    if (stack == 0) return NrDeclineReason::kZeroStrengthPassthrough;
    if (stack > kMaxNrPasses) return NrDeclineReason::kMalformedEvaluate;
    if (pre_sr && found->second.id != kFeatureDlss) {
      static std::atomic_bool logged{false};
      if (!logged.exchange(true)) Log(reshade::log::level::warning,
          "Vulkan Render NR declined: pre-SR requires an SR Color contract; RR is not supported");
      return NrDeclineReason::kPreSrGeometry;
    }
  }
  command_list* cmd = nullptr;
  {
    std::shared_lock lock(command_lists_mutex);
    const auto found = command_lists.find(command_buffer);
    if (found != command_lists.end()) cmd = found->second.list;
  }
  if (cmd == nullptr) return NrDeclineReason::kMalformedEvaluate;
  struct ComputeRestoreScope {
    command_list* cmd;
    bool active = false;
    ~ComputeRestoreScope() { if (active) RestoreComputeState(cmd); }
  } restore{cmd};
  reshade::api::device* const api_device = cmd->get_device();
  const VkDevice device = reinterpret_cast<VkDevice>(api_device->get_native());
  auto* const parameter_block = const_cast<NVSDK_NGX_Parameter*>(parameters);
  const auto resource_vk = [&](const char* name) {
    void* pointer = nullptr;
    parameter_block->Get(name, &pointer);
    auto* const resource = static_cast<NVSDK_NGX_Resource_VK*>(pointer);
    return resource != nullptr && resource->Type == NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW
                   && resource->Resource.ImageViewInfo.Image != nullptr
                   && resource->Resource.ImageViewInfo.ImageView != nullptr
               ? resource
               : nullptr;
  };
  NVSDK_NGX_Resource_VK* const output = resource_vk(
      pre_sr ? NVSDK_NGX_Parameter_Color : NVSDK_NGX_Parameter_Output);
  NVSDK_NGX_Resource_VK* const depth = resource_vk(NVSDK_NGX_Parameter_Depth);
  NVSDK_NGX_Resource_VK* const motion = resource_vk(NVSDK_NGX_Parameter_MotionVectors);
  if (depth == nullptr || motion == nullptr) return NrDeclineReason::kNgxMissingGuides;
  if (output == nullptr || (!pre_sr && !output->ReadWrite)) return NrDeclineReason::kNgxOutputGeometry;
  NVSDK_NGX_ImageViewInfo_VK out = output->Resource.ImageViewInfo;
  const resource source_image = {reinterpret_cast<uint64_t>(out.Image)};
  resource output_image = source_image;
  ImageInputScope source_layout(cmd, out);
  if (!source_layout.Valid()
      || (pre_sr && source_layout.original != kLayoutGeneral
          && source_layout.original != kLayoutReadOnly
          && source_layout.original != kLayoutReadOnlyOptimal)) {
    static std::atomic_bool logged{false};
    if (pre_sr && !logged.exchange(true)) Log(reshade::log::level::warning,
        "Vulkan Render NR declined: Color has no proven shader-readable native layout");
    return pre_sr ? NrDeclineReason::kPreSrNoStateTarget : NrDeclineReason::kStateShadowUnavailable;
  }
  const ngx_serial::Call ngx_turn;
  {
    RuntimeLock owner_lock(runtime_mutex);
    if (game_api_device.load(std::memory_order_relaxed) == nullptr) {
      game_device.store(device, std::memory_order_relaxed);
      game_api_device.store(api_device, std::memory_order_relaxed);
    }
    if (api_device != game_api_device.load(std::memory_order_relaxed)) {
      // This session has one runtime/codec owner. Serving a second device
      // would bind the owner's pipelines and readback in this device's
      // command buffer, so the frame keeps the game's image; the owner is
      // released when its device is destroyed (OnDestroyGameDevice).
      if (owner_mismatches.fetch_add(1, std::memory_order_relaxed) == 0) {
        Log(reshade::log::level::warning,
            "DLSS evaluates use a second Vulkan device; NR's runtime and codec"
            " belong to the first device, so those frames keep the game's image"
            " (vk_owner_mismatch)");
      }
      return NrDeclineReason::kVkOwnerMismatch;
    }
  }
  // Outside runtime_mutex: the query reaches the Windows loader
  // (runtime_lock.hpp). The owner it reads cannot change meanwhile; only
  // the owner's destruction clears it, and this evaluate records on it.
  CheckExtensions();
  if (extension_state.load(std::memory_order_relaxed) == 2) {
    return NrDeclineReason::kVkExtensionMissing;
  }
  RuntimeLock lock(runtime_mutex);
  const auto found = features.find(handle);
  if (found == features.end()) return NrDeclineReason::kNgxNotDlssEvaluation;
  Feature& game = found->second;
  StageFeature& feature = game.stages[pre_sr ? 0 : 1];
  if (game.device == nullptr) game.device = api_device;
  feature.device = api_device;
  resource_desc output_desc = api_device->get_resource_desc(source_image);
  const auto output_view_desc = api_device->get_resource_view_desc(
      {reinterpret_cast<uint64_t>(out.ImageView)});
  static std::atomic_uint32_t contract_logs[2];
  if (contract_logs[pre_sr ? 0 : 1].fetch_add(1, std::memory_order_relaxed) < 4) {
    std::ostringstream text;
    text << "Vulkan NR image contract stage=" << (pre_sr ? "render" : "upscaled")
         << " type=" << static_cast<uint32_t>(output_desc.type)
         << " extent=" << output_desc.texture.width << 'x' << output_desc.texture.height
         << " samples=" << output_desc.texture.samples
         << " levels=" << output_desc.texture.levels
         << " layers=" << output_desc.texture.depth_or_layers
         << " usage=" << static_cast<uint32_t>(output_desc.usage)
         << " format=" << static_cast<uint32_t>(output_desc.texture.format)
         << " view_format=" << static_cast<uint32_t>(output_view_desc.format)
         << " ngx_format=" << static_cast<uint32_t>(out.Format)
         << " aspect=" << out.SubresourceRange.aspectMask
         << " mip=" << out.SubresourceRange.baseMipLevel << '/' << out.SubresourceRange.levelCount
         << " layer=" << out.SubresourceRange.baseArrayLayer << '/' << out.SubresourceRange.layerCount
         << " layout=" << static_cast<uint32_t>(source_layout.original)
         << " flags=" << feature.create_flags;
    Log(reshade::log::level::info, text.str());
  }
  if (out.SubresourceRange.levelCount == UINT32_MAX
      && out.SubresourceRange.baseMipLevel < output_desc.texture.levels) {
    out.SubresourceRange.levelCount = output_desc.texture.levels - out.SubresourceRange.baseMipLevel;
  }
  if (out.SubresourceRange.layerCount == UINT32_MAX
      && out.SubresourceRange.baseArrayLayer < output_desc.texture.depth_or_layers) {
    out.SubresourceRange.layerCount = output_desc.texture.depth_or_layers - out.SubresourceRange.baseArrayLayer;
  }
  // Layout observation only proves single-subresource images. Copy requires
  // TRANSFER_SRC on the game image; Render never requires STORAGE or DST.
  if (output_desc.type != resource_type::texture_2d || output_desc.texture.samples != 1
      || output_desc.texture.levels != 1 || output_desc.texture.depth_or_layers != 1
      || out.SubresourceRange.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT
      || out.SubresourceRange.levelCount != 1 || out.SubresourceRange.layerCount != 1
      || (output_desc.usage & resource_usage::copy_source) == 0
      || output_view_desc.format == format::unknown
      || (output_view_desc.format != output_desc.texture.format
          && format_to_typeless(output_view_desc.format) != output_desc.texture.format)
      || (!pre_sr && (output_desc.usage & resource_usage::unordered_access) == 0)
      || out.SubresourceRange.baseMipLevel >= output_desc.texture.levels
      || out.SubresourceRange.baseArrayLayer >= output_desc.texture.depth_or_layers) {
    static std::atomic_bool logged{false};
    if (pre_sr && !logged.exchange(true)) Log(reshade::log::level::warning,
        "Vulkan Render NR declined: Color must be a single-sample/subresource TRANSFER_SRC image with a matching view format");
    return pre_sr ? NrDeclineReason::kPreSrGeometry : NrDeclineReason::kNgxOutputGeometry;
  }
  // ReShade reports mutable Vulkan images as typeless; the view fixes their interpretation.
  output_desc.texture.format = output_view_desc.format;
  const uint32_t resource_width = std::max(1u, output_desc.texture.width >> out.SubresourceRange.baseMipLevel);
  const uint32_t resource_height = std::max(1u, output_desc.texture.height >> out.SubresourceRange.baseMipLevel);
  const uint32_t output_subresource = out.SubresourceRange.baseMipLevel
      + out.SubresourceRange.baseArrayLayer * output_desc.texture.levels;
  const bool subrects = GetInt(parameters, NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,
                              game.ngx.output_subrects_enabled ? 1 : 0) != 0;
  uint32_t output_x = subrects ? GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X) : 0;
  uint32_t output_y = subrects ? GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y) : 0;
  const uint32_t evaluate_width = GetUInt(parameters, NVSDK_NGX_Parameter_OutWidth);
  const uint32_t evaluate_height = GetUInt(parameters, NVSDK_NGX_Parameter_OutHeight);
  uint32_t width = resource_width;
  uint32_t height = resource_height;
  if (nr_output_rect.load() != 2u) {
    // As on D3D12, the create contract wins: NGX's evaluate helper does not
    // write OutWidth/OutHeight, so a shared parameter block can contain a
    // stale render size. The Declared override preserves the older reading.
    width = nr_output_rect.load() != 1u && game.ngx.declared_output_width != 0
                ? game.ngx.declared_output_width : evaluate_width != 0 ? evaluate_width : width;
    height = nr_output_rect.load() != 1u && game.ngx.declared_output_height != 0
                 ? game.ngx.declared_output_height : evaluate_height != 0 ? evaluate_height : height;
  }
  const uint32_t render_width = GetUInt(parameters,
      NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, game.render_width);
  const uint32_t render_height = GetUInt(parameters,
      NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, game.render_height);
  if (pre_sr) {
    output_x = GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X);
    output_y = GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y);
    width = render_width;
    height = render_height;
    // Preserve Color bases for real SR by keeping a full-size private image.
    // NR operates only on the declared crop. Guides retain their own bases.
    const auto fits = [&](const NVSDK_NGX_Resource_VK* guide, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h) {
      const auto& image = guide->Resource.ImageViewInfo;
      const auto desc = api_device->get_resource_desc({reinterpret_cast<uint64_t>(image.Image)});
      return desc.type == resource_type::texture_2d && desc.texture.samples == 1
          && image.SubresourceRange.baseMipLevel < desc.texture.levels
          && image.SubresourceRange.baseArrayLayer < desc.texture.depth_or_layers
          && (image.SubresourceRange.levelCount == 1
              || (image.SubresourceRange.levelCount == UINT32_MAX
                  && image.SubresourceRange.baseMipLevel + 1 == desc.texture.levels))
          && (image.SubresourceRange.layerCount == 1
              || (image.SubresourceRange.layerCount == UINT32_MAX
                  && image.SubresourceRange.baseArrayLayer + 1 == desc.texture.depth_or_layers))
          && image.Width <= std::max(1u, desc.texture.width >> image.SubresourceRange.baseMipLevel)
          && image.Height <= std::max(1u, desc.texture.height >> image.SubresourceRange.baseMipLevel)
          && w != 0 && h != 0 && x < guide->Resource.ImageViewInfo.Width
          && y < guide->Resource.ImageViewInfo.Height
          && w <= guide->Resource.ImageViewInfo.Width - x
          && h <= guide->Resource.ImageViewInfo.Height - y;
    };
    // High-resolution MV requires the original SR output grid, not the
    // pre-SR target size. Until that guide transport exists, explicitly decline.
    if (render_color == nullptr || width == 0 || height == 0
        || out.Width != resource_width || out.Height != resource_height
        || output_x >= resource_width || output_y >= resource_height
        || width > resource_width - output_x || height > resource_height - output_y
        || (feature.create_flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) == 0
        || !fits(depth, GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X),
                 GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y), width, height)
        || !fits(motion, GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X),
                 GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y), width, height)) {
      static std::atomic_bool logged{false};
      if (!logged.exchange(true)) Log(reshade::log::level::warning,
          "Vulkan Render NR declined: Color crop and depth/MV guides must fit the render grid; high-resolution MV transport is unsupported");
      return NrDeclineReason::kPreSrGeometry;
    }
  } else {
    if (output_x >= resource_width || output_y >= resource_height) output_x = output_y = 0;
    width = std::min(width, resource_width - output_x);
    height = std::min(height, resource_height - output_y);
  }
  // HdrMode 1 (scene-linear) for float outputs, 0 (display-encoded) for
  // UNORM; an sRGB or other format keeps the game's image, named once.
  uint32_t hdr_mode = 0;
  switch (output_desc.texture.format) {
    case format::r16g16b16a16_float:
    case format::r11g11b10_float:
    case format::r32g32b32a32_float:
      hdr_mode = 1;
      break;
    case format::r8g8b8a8_unorm:
    case format::b8g8r8a8_unorm:
    case format::r10g10b10a2_unorm:
    case format::b10g10r10a2_unorm:
      break;
    default: {
      static std::atomic_bool logged{false};
      if (!logged.exchange(true)) {
        Log(reshade::log::level::warning,
            "the game's Vulkan DLSS output has format "
                + std::to_string(static_cast<uint32_t>(output_desc.texture.format))
                + ", which the Vulkan codec does not read; those frames keep the"
                  " game's image (workset_setup_failed)");
      }
      return NrDeclineReason::kWorksetSetupFailed;
    }
  }
  if (width == 0 || height == 0) {
    return NrDeclineReason::kNgxOutputGeometry;
  }

  // Share D3D12's content inference: 10-bit UNORM is PQ only when the
  // game's create contract says HDR; SDR 10-bit surfaces stay SDR.
  if ((feature.create_flags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0) {
    hdr_mode = output_desc.texture.format == format::r10g10b10a2_unorm
                    || output_desc.texture.format == format::b10g10r10a2_unorm
                   ? 2u
                   : std::max(hdr_mode, 1u);
  }
  if (const NrDeclineReason decline = EnsureRuntime(game_device.load(std::memory_order_relaxed));
      decline != NrDeclineReason::kCount) {
    return decline;
  }
  if (!EnsureCodec(api_device)) return NrDeclineReason::kWorksetSetupFailed;
  DrainRetiredFeature(&feature);

  const codec::SourceUnits current_units = InterpretSourceUnits(static_cast<uint8_t>(hdr_mode));
  auto workset = std::find_if(feature.worksets.begin(), feature.worksets.end(),
                              [&](const Workset& w) {
                                return w.output == out.Image && w.width == width
                                    && w.height == height && w.output_format == output_desc.texture.format
                                    && w.hdr_mode == hdr_mode
                                    && w.source_width == resource_width && w.source_height == resource_height;
                              });
  if (workset == feature.worksets.end()) {
    if (feature.worksets.size() >= std::max(1u, max_worksets.load())) {
      // All views in this set name its own scratch, never the game's output.
      // A completed recording therefore permits a same-contract re-key
      // without allocating another set of full-resolution images.
      workset = std::find_if(feature.worksets.begin(), feature.worksets.end(),
                             [&](const Workset& candidate) {
                               return candidate.width == width
                                   && candidate.height == height
                                   && candidate.output_format == output_desc.texture.format
                                   && candidate.hdr_mode == hdr_mode
                                   && candidate.source_width == resource_width
                                   && candidate.source_height == resource_height
                                   && RecordedUseComplete(candidate.use_id);
                             });
      if (workset != feature.worksets.end()) {
        workset->output = out.Image;
      } else {
        auto oldest = std::min_element(feature.worksets.begin(), feature.worksets.end(),
                                       [](const Workset& a, const Workset& b) {
                                         return a.last_used < b.last_used;
                                       });
        if (workset_free_policy.load(std::memory_order_relaxed) == 0
            && RecordedUseComplete(oldest->use_id)) {
          DestroyWorkset(api_device, *oldest);
        } else {
          feature.retired_worksets.push_back(std::move(*oldest));
        }
        feature.worksets.erase(oldest);
        workset = feature.worksets.end();
      }
    }
    if (workset == feature.worksets.end()) {
      Workset created = {out.Image, width, height, output_desc.texture.format};
      created.hdr_mode = static_cast<uint8_t>(hdr_mode);
      created.source_width = resource_width;
      created.source_height = resource_height;
      created.units = current_units;
      created.id = next_workset_id++;
      created.use_id = next_use_id++;
      feature.worksets.push_back(created);
      std::ostringstream text;
      text << "Vulkan NR workset " << width << 'x' << height << " (output format "
           << static_cast<uint32_t>(output_desc.texture.format) << ", codec ";
      if (hdr_mode == 0) {
        text << "legacy";
      } else {
        text << "v6: encoding " << V6EncodingConstant(created.units)
             << (created.units.absolute ? ", absolute units" : ", relative units");
      }
      text << ") on the game's DLSS feature";
      Log(reshade::log::level::info, text.str());
      workset = feature.worksets.end() - 1;
    }
  }
  if (workset->units.encoding != current_units.encoding
      || workset->units.absolute != current_units.absolute
      || workset->units.unit_nits != current_units.unit_nits
      || workset->units.bt2020 != current_units.bt2020) {
    // These are recorded constants, not an allocation contract. Reuse the
    // scratch images instead of retaining a full image set for every tick
    // of the source-units slider until the game's DLSS feature is released.
    workset->units = current_units;
    feature.ngx.norm.primed = false;
    for (NrFeatureSlot& slot : feature.slots) slot.pending_reset = true;
  }
  // Explicit zero strength on an HDR frame: nothing is recorded and the
  // game's output passes through bit-identical (ChainActive).
  if (hdr_mode != 0 && !ChainActive(*workset, stack)) {
    return NrDeclineReason::kZeroStrengthPassthrough;
  }

  // Feature 18 per stack pass at the output size.  A pass is created on this
  // command buffer and first evaluated on the next frame's: same-frame create
  // and evaluate is the shape that produced garbage frames and hangs on D3D12
  // (EnsureNrFeature, frame-ahead maturation).  Pass 0 missing keeps the
  // game's image this frame; pass p missing runs the p matured passes.
  if (feature.nr_width != width || feature.nr_height != height
      || feature.nr_hdr_mode != hdr_mode
      || feature.configuration != configuration_generation.load()) {
    for (uint32_t pass = 0; pass < kMaxNrPasses; ++pass) {
      NrFeatureSlot& slot = feature.slots[pass];
      if (slot.handle != nullptr) {
        feature.retired_nr.push_back({slot.handle, feature.nr_use[pass]});
        slot.handle = nullptr;
        live_nr_features.fetch_sub(1, std::memory_order_relaxed);
      }
      feature.nr_use[pass] = 0;
      slot.failed = false;
      slot.fail_count = 0;
    }
    feature.nr_width = width;
    feature.nr_height = height;
    feature.nr_hdr_mode = static_cast<uint8_t>(hdr_mode);
    feature.configuration = configuration_generation.load();
  }
  for (NrFeatureSlot& slot : feature.slots) {
    if (slot.failed && FeatureFailureExpired(slot)) {
      slot.failed = false;
      ++slot.fail_count;
    }
  }
  const auto create_slot = [&](uint32_t pass) {
    NrFeatureSlot& slot = feature.slots[pass];
    // The runtime exports no Vulkan parameter allocator; the block is the
    // addon's own, as on the D3D12 path without a public core (OwnedParameters).
    if (slot.parameters == nullptr) bridge::AllocateOwnedParameters(&slot.parameters);
    SetNrCreateParameters(slot.parameters, width, height, width, height, feature.create_flags,
                          hdr_mode != 0);
    NVSDK_NGX_Result result = NVSDK_NGX_Result_FAIL_UnableToInitializeFeature;
    {
      restore.active = true;
      DirectCallScope direct;
      const lastgasp::InsideCall inside("nr-create-vk");
      try {
        result = runtime.create(device, command_buffer, kFeatureDlssNr, slot.parameters,
                                &slot.handle);
      } catch (...) {
        slot.handle = nullptr;
      }
    }
    last_result = static_cast<uint32_t>(result);
    char text[160];
    snprintf(text, sizeof(text), "NR feature 18 create on Vulkan (%ux%u, stack pass %u): 0x%08x",
             width, height, pass + 1, static_cast<uint32_t>(result));
    if (NVSDK_NGX_FAILED(result) || slot.handle == nullptr) {
      slot.handle = nullptr;
      slot.failed = true;
      slot.failed_ns = SteadyNowNs();
      Log(reshade::log::level::error,
          std::string(text)
              + (pass == 0 ? "; this stream keeps the game's image (failure_backoff)"
                           : "; the stack runs the passes before it"));
      return false;
    }
    Log(reshade::log::level::info, text);
    nr_feature_ever_ready.store(true, std::memory_order_relaxed);
    live_nr_features.fetch_add(1, std::memory_order_relaxed);
    feature.nr_use[pass] = next_use_id++;
    TrackRecordedUse(cmd, feature.nr_use[pass]);
    feature.nr_width = width;
    feature.nr_height = height;
    slot.pending_reset = true;
    slot.nr_hdr_mode = static_cast<uint8_t>(hdr_mode);
    slot.configuration_generation = feature.configuration;
    return true;
  };
  if (feature.slots[0].handle == nullptr) {
    if (feature.slots[0].failed || !create_slot(0)) return NrDeclineReason::kFailureBackoff;
    return NrDeclineReason::kSlotWarmupMaturation;
  }
  uint32_t passes = 1;
  while (passes < stack && feature.slots[passes].handle != nullptr) ++passes;
  if (passes < stack && !feature.slots[passes].failed) create_slot(passes);

  // The scratch images this frame needs, created on first use (Workset).
  // Views carry no usage restriction: ReShade turns shader_resource into
  // VkImageViewUsageCreateInfo(SAMPLED), and a storage binding of such a
  // view writes nothing.
  resource* const images = workset->images;
  const resource_view* const views = workset->views;
  // The v6 work surfaces keep FP32 for an FP32 output, else FP16 (as
  // D3D12's EnsureWorkset).
  const format work_format = output_desc.texture.format == format::r32g32b32a32_float
                                 ? format::r32g32b32a32_float
                                 : format::r16g16b16a16_float;
  const auto ensure = [&](Surface surface) {
    if (views[surface].handle != 0) return true;
    uint32_t image_width = width;
    uint32_t image_height = height;
    format image_format = format::r16g16b16a16_float;
    resource_usage usage = resource_usage::unordered_access;
    switch (surface) {
      case kRenderTarget:
        image_width = resource_width;
        image_height = resource_height;
        image_format = output_desc.texture.format;
        usage |= resource_usage::copy_source | resource_usage::copy_dest;
        break;
      case kOriginal:
        image_format = output_desc.texture.format;
        usage = resource_usage::copy_dest;
        break;
      case kWork0:
      case kWorkA:
      case kWorkB:
        // copy_*: an explicit zero-strength stack pass carries the running
        // image across with a copy (as D3D12's CopyMip0).
        image_format = work_format;
        usage |= resource_usage::copy_source | resource_usage::copy_dest;
        break;
      case kScale:
        // copy_source: the divisor readout (SampleScale) copies it out.
        image_width = image_height = 1;
        image_format = format::r32g32b32a32_float;
        usage |= resource_usage::copy_source;
        break;
      case kBlockMean:
        image_width = (width + 31) / 32;
        image_height = (height + 31) / 32;
        image_format = format::r32g32b32a32_float;
        break;
      case kStack0:
      case kStack1:
        image_format = output_desc.texture.format;
        break;
      case kLookScratch:
        image_width = image_height = 1;
        image_format = format::r32g32b32a32_uint;
        break;
      case kBandA:
      case kBandB:
      case kBandMaxA:
      case kBandMaxB:
        image_width = (width + 1) / 2;
        image_height = (height + 1) / 2;
        image_format = surface == kBandA || surface == kBandB ? format::r32g32b32a32_float
                                                              : format::r32g32_float;
        break;
      default:
        break;
    }
    if (!api_device->create_resource(
            resource_desc(image_width, image_height, 1, 1, image_format, 1,
                          memory_heap::gpu_only, usage | resource_usage::shader_resource),
            nullptr, resource_usage::undefined, &images[surface])) {
      images[surface] = {};
      return false;
    }
    if (!api_device->create_resource_view(images[surface], resource_usage::undefined,
                                          resource_view_desc(image_format),
                                          &workset->views[surface])) {
      api_device->destroy_resource(images[surface]);
      images[surface] = {};
      workset->views[surface] = {};
      return false;
    }
    workset->state[surface] = resource_usage::undefined;
    return true;
  };
  bool made = ensure(kOriginal) && ensure(kProxy) && ensure(kNeural)
      && (!pre_sr || ensure(kRenderTarget));
  if (hdr_mode != 0) {
    made = made && ensure(kWork0) && ensure(kWorkA) && (passes < 2 || ensure(kWorkB))
        && (workset->units.absolute || ensure(kScale)) && ensure(kBlockMean);
    // The stream's committed divisor (relative units only), one per game
    // DLSS stream like D3D12's NormStream::commit; cleared on first use,
    // which the autoscale reads as "snap" (copy_dest: the clear is a
    // vkCmdClearColorImage, which needs TRANSFER_DST).
    if (made && !workset->units.absolute && feature.commit.handle == 0) {
      made = api_device->create_resource(
                  resource_desc(1, 1, 1, 1, format::r32g32b32a32_float, 1, memory_heap::gpu_only,
                                resource_usage::unordered_access | resource_usage::copy_dest),
                  nullptr, resource_usage::undefined, &feature.commit);
      if (made) feature.commit_use = next_use_id++;
    }
    // A view failure can be transient. Keep the valid resource and retry the
    // missing view on the next evaluate instead of wedging this stream.
    if (made && !workset->units.absolute && feature.commit_view.handle == 0) {
      made = api_device->create_resource_view(
          feature.commit, resource_usage::undefined,
          resource_view_desc(format::r32g32b32a32_float), &feature.commit_view);
    }
    made = made && (workset->units.absolute || feature.commit_view.handle != 0);
  } else {
    // Legacy: pass p < last writes stack[p & 1], the last pass the output.
    made = made && (passes < 2 || ensure(kStack0)) && (passes < 3 || ensure(kStack1));
  }
  if (!made) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      Log(reshade::log::level::error,
          "a Vulkan NR workset could not be created; its frames keep the game's"
          " image (workset_setup_failed)");
    }
    return NrDeclineReason::kWorksetSetupFailed;
  }
  workset->last_used = next_workset_use++;
  TrackRecordedUse(cmd, workset->use_id);
  if (hdr_mode != 0 && !workset->units.absolute) TrackRecordedUse(cmd, feature.commit_use);
  // The look stage (look_stage.hpp), as the D3D12 after path runs it: the
  // same snapshot (Detail stability Auto engages here), and N' shaped only
  // when a gain is off identity or a temporal filter runs.  NR runs 1:1 on
  // Vulkan, so the transport never does.  Surfaces the look cannot have
  // leave it off for the frame; NR runs on.
  const look::Settings look_settings = LookSettingsSnapshot(true);
  if (edit_trace_enabled.load()) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      Log(reshade::log::level::warning,
          "NREditTrace is on, but the edit trace is Direct3D 12 only: Vulkan frames"
          " record none (the look stage and the pass stack run)");
    }
  }
  const bool look_bands = look::BandsWanted(look_settings);
  const bool look_shape =
      look_compose.handle != 0 && (look_mode.load() || look_settings.stabilize != 0)
      && look::Dispatched(look_settings, false) && ensure(kLookOutput)
      && (!look_bands
          || (ensure(kBandA) && ensure(kBandB) && ensure(kBandMaxA) && ensure(kBandMaxB)))
      && ensure(kLookScratch);

  // 1. The DLSS output into `original`.  Every scratch image carries its
  // tracked layout (state) across frames; `transition` moves it to what the
  // next access needs (a storage-to-storage move is the write-to-read
  // barrier between two dispatches).
  const auto barrier = [&](resource image, resource_usage before, resource_usage after) {
    Barrier(cmd, 1, &image, &before, &after,
             image == output_image ? &out.SubresourceRange : nullptr);
  };
  const auto transition = [&](Surface surface, resource_usage usage) {
    resource_usage& state = workset->state[surface];
    if (state == usage && usage != resource_usage::unordered_access) return;
    Barrier(cmd, 1, &images[surface], &state, &usage);
    state = usage;
  };
  // A global memory barrier (ReShade's barrier on a null resource): the look
  // stage's dispatch-to-dispatch order on the bands and the history buffers,
  // as D3D12's UavBarrierAll.
  const auto global_barrier = [&] {
    const resource none = {};
    const resource_usage storage = resource_usage::unordered_access;
    Barrier(cmd, 1, &none, &storage, &storage);
  };
  restore.active = true;
  source_layout.Transition(static_cast<VkImageLayout>(6));
  if (pre_sr) {
    transition(kRenderTarget, resource_usage::copy_dest);
    cmd->copy_texture_region(source_image, 0, nullptr, images[kRenderTarget], 0, nullptr);
    transition(kRenderTarget, resource_usage::copy_source);
    output_image = images[kRenderTarget];
    out.Image = reinterpret_cast<VkImage>(output_image.handle);
    out.ImageView = reinterpret_cast<VkImageView>(views[kRenderTarget].handle);
    source_layout.Transition(source_layout.original);
  }
  transition(kOriginal, resource_usage::copy_dest);
  if (hdr_mode != 0 && !workset->units.absolute && !feature.commit_cleared) {
    barrier(feature.commit, resource_usage::undefined, resource_usage::general);
    const float zero[4] = {};
    cmd->clear_unordered_access_view_float(feature.commit_view, zero, 0, nullptr);
    // ReShade implements this clear with vkCmdClearColorImage in GENERAL:
    // it is a transfer write, not a shader write. Make that visible before
    // the first meter/exposure dispatch reads the committed divisor.
    barrier(feature.commit, resource_usage::general, resource_usage::unordered_access);
    feature.commit_cleared = true;
  }
  const subresource_box output_box = {output_x, output_y, 0,
                                      output_x + width, output_y + height, 1};
  // ReShade selects vkCmdCopyImage only when both rectangles are explicit
  // and equal-sized (or both are absent). A source rectangle alone selects
  // vkCmdBlitImage, which is illegal on a compute-only queue. kOriginal has
  // the source format, so this crop is an exact copy with no resampling.
  const subresource_box original_box = {0, 0, 0, width, height, 1};
  cmd->copy_texture_region(output_image, output_subresource, &output_box,
                           images[kOriginal], 0, &original_box);
  if (pre_sr) transition(kRenderTarget, resource_usage::unordered_access);
  else source_layout.Transition(kLayoutGeneral);
  transition(kOriginal, resource_usage::shader_resource);
  const uint32_t groups_x = (width + 15) / 16;
  const uint32_t groups_y = (height + 15) / 16;
  // Push-descriptor bindings 0..3 sampled (t0..t3), 4.. storage (u0, u1),
  // the cbuffer as push constants (the SPIR-V route's layout, EnsureCodec).
  const auto dispatch = [&](Program program, const resource_view(&sampled)[4],
                            std::initializer_list<resource_view> storage, const void* constants,
                            uint32_t dwords, uint32_t x, uint32_t y) {
    cmd->bind_pipeline(pipeline_stage::all_compute, codec_programs[program]);
    cmd->push_descriptors(shader_stage::all_compute, codec_layout, 0,
                          {{}, 0, 0, 4, descriptor_type::texture_shader_resource_view, sampled});
    cmd->push_descriptors(shader_stage::all_compute, codec_layout, 0,
                          {{}, 4, 0, static_cast<uint32_t>(storage.size()),
                           descriptor_type::texture_unordered_access_view, storage.begin()});
    cmd->push_constants(shader_stage::all_compute, codec_layout, 1, 0, dwords, constants);
    cmd->dispatch(x, y, 1);
  };
  // The legacy cbuffer (BindCodec's Constants, dwords 0..19); the pass loop
  // sets each pass's strengths.
  struct Constants {
    uint32_t size[2];
    uint32_t source_size[2];
    uint32_t source_base[2];
    uint32_t proxy_size[2];
    uint32_t neural_size[2];
    float paper_white_scale;
    float transfer_strength;
    float color_strength;
    uint32_t hdr;
    float diffuse_white_nits;
    float unread;
    float padding[4];
  } constants = {{width, height},
                 {width, height},
                 {0, 0},
                 {width, height},
                 {width, height},
                 std::max(0.0001f, paper_white_scale.load()),
                 0.f,
                 0.f,
                 hdr_mode,
                 diffuse_white_nits.load()};
  static_assert(sizeof(Constants) == 80);
  // v6 frame snapshots, as ProcessInline takes them: one divisor (0 = this
  // frame's GPU texel on relative units), encoding, gate and cap for every
  // dispatch of the frame.
  const float frame_divisor = hdr_mode != 0 ? FrameCodecDivisor(*workset) : 0.f;
  const uint32_t v6_encoding = V6EncodingConstant(workset->units);
  const float v6_dark_gate = V6DarkGate(workset->units);
  const float v6_pedestal_cap = codec::BlackPedestalCap(workset->units);
  // t3 of the pass sets and t2 of the commit set: the divisor texel on
  // relative units (absolute units never read it; work0 fills the slot).
  const resource_view scale_view = workset->units.absolute ? views[kWork0] : views[kScale];
  FeedSource feed = FeedSource::kV1;
  if (hdr_mode != 0) {
    // 2a. v6 linearize: the copied output becomes work0, the linear source
    // the meter, the encode, the resolve and the pedestal read.
    const V6CodecConstants linearize = MakeV6Constants(
        *workset, width, height, width, height, 0, 0, width, height, width, height,
        frame_divisor, v6_pedestal_cap, 0.f, 0.f, v6_encoding, v6_dark_gate, 0.f);
    const resource_view original_only[4] = {views[kOriginal], views[kOriginal],
                                            views[kOriginal], views[kOriginal]};
    transition(kWork0, resource_usage::unordered_access);
    dispatch(kLinearize, original_only, {views[kWork0]}, &linearize, 28, groups_x, groups_y);
    transition(kWork0, resource_usage::shader_resource);
    if (!workset->units.absolute) {
      // 2b. This frame's scale texel, from the shared plan (PlanFrameScale,
      // SelectFeed): the v1 meter and governor, and under feed v2 the
      // game's exposure (range-guarded under Auto).  PrepareFrameScale's
      // dispatches, in its order.
      FeatureState current;
      ReadExposureScalars(parameters, &current);
      NVSDK_NGX_Resource_VK* const exposure = resource_vk(NVSDK_NGX_Parameter_ExposureTexture);
      const VkFormat exposure_format =
          exposure != nullptr ? exposure->Resource.ImageViewInfo.Format : VK_FORMAT_UNDEFINED;
      // The float formats v6_exposure_scale reads (.r). SampledInputScope
      // temporarily transitions an observed GENERAL image for ReShade's
      // sampled descriptor, then restores the game's original layout.
      bool readable = false;
      switch (exposure_format) {
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
          readable = true;
          break;
        default:
          break;
      }
      feature.ngx.create_flags = feature.create_flags;
      EvaluationContract frame;
      frame.frame_reset = GetInt(parameters, NVSDK_NGX_Parameter_Reset, 0);
      frame.pre_exposure = current.pre_exposure > 0.f ? current.pre_exposure : 1.f;
      frame.exposure_scale = current.exposure.scale > 0.f ? current.exposure.scale : 1.f;
      frame.feed = SelectFeed(&feature.ngx, current, workset->hdr_mode, workset->units, true,
                              exposure != nullptr, readable,
                              static_cast<uint32_t>(exposure_format));
      feed = frame.feed;
      const FrameScalePlan plan = PlanFrameScale(&feature.ngx.norm, &workset->snaps, frame);
      const resource_view work0_only[4] = {views[kWork0], views[kWork0], views[kWork0],
                                           views[kWork0]};
      transition(kScale, resource_usage::unordered_access);
      if (frame.feed == FeedSource::kV1 || plan.guarded) {
        V6CodecConstants meter = MakeV6Constants(
            *workset, 1, 1, width, height, 0, 0, 1, 1, 1, 1, 0.f, 0.f, 0.f, 0.f, 1u, 0.f, 0.f);
        std::memcpy(&meter.slew_stops, plan.governor, sizeof(plan.governor));
        dispatch(kAutoscale, work0_only, {views[kScale], feature.commit_view}, &meter, 28, 1, 1);
        if (plan.guarded) {
          transition(kScale, resource_usage::unordered_access);
          barrier(feature.commit, resource_usage::unordered_access,
                  resource_usage::unordered_access);
        }
      }
      if (frame.feed != FeedSource::kV1) {
        const bool texture = frame.feed == FeedSource::kTexture && readable;
        const resource_view source =
            texture ? resource_view{reinterpret_cast<uint64_t>(
                          exposure->Resource.ImageViewInfo.ImageView)}
                    : views[kWork0];
        const resource_view feed_views[4] = {source, views[kWork0], source, source};
        (texture ? feed_texture_frames : feed_fixed_frames)
            .fetch_add(1, std::memory_order_relaxed);
        // v6_exposure_scale's own cbuffer view: dwords 0..6.
        const uint32_t feed_constants[7] = {
            static_cast<uint32_t>(texture ? FeedSource::kTexture : FeedSource::kFixed),
            std::bit_cast<uint32_t>(frame.pre_exposure),
            std::bit_cast<uint32_t>(frame.exposure_scale),
            std::bit_cast<uint32_t>(plan.v2_snap ? 1.f : 0.f),
            plan.guarded ? 1u : 0u,
            width,
            height};
        if (texture) {
          const SampledInputScope sampled_exposure(cmd, exposure);
          dispatch(kExposureScale, feed_views, {views[kScale], feature.commit_view},
                   feed_constants, 7, 1, 1);
        } else {
          dispatch(kExposureScale, feed_views, {views[kScale], feature.commit_view},
                   feed_constants, 7, 1, 1);
        }
      }
      // The next frame's read of the commit texel after this frame's write.
      barrier(feature.commit, resource_usage::unordered_access,
              resource_usage::unordered_access);
      transition(kScale, resource_usage::shader_resource);
      SampleScale(cmd, api_device, images[kScale], workset->id, plan.guarded);
    }
  }

  // 2. The pass stack (NRPasses, as ProcessInline): pass p encodes the
  // running image - the copied output (legacy) or work0 (v6) for pass 0,
  // the previous pass's result after - evaluates its own feature 18 with
  // its own history (NRChainedHistory, SetModelParameters), runs the look
  // stage and resolves.  v6 passes write work_a / work_b in turn and one
  // commit follows; legacy passes write stack0 / stack1 in turn and the
  // last one writes the output.
  NVSDK_NGX_Resource_VK proxy_vk =
      ImageResource(views[kProxy], images[kProxy], width, height, false);
  NVSDK_NGX_Resource_VK neural_vk =
      ImageResource(views[kNeural], images[kNeural], width, height, true);
  const uint32_t motion_x = GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X);
  const uint32_t motion_y = GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y);
  // The shared contract helper uses only Width/Height from this description.
  D3D12_RESOURCE_DESC motion_desc = {};
  motion_desc.Width = motion->Resource.ImageViewInfo.Width;
  motion_desc.Height = motion->Resource.ImageViewInfo.Height;
  const MotionWindow motion_window = ResolveMotionWindow(
      feature.create_flags, motion_x, motion_y, render_width, render_height,
      width, height, motion_desc);
  const uint32_t motion_width = motion_window.width;
  const uint32_t motion_height = motion_window.height;
  FeatureState motion_contract;
  motion_contract.has_motion_scale_x = GetFloatIfPresent(
      parameters, NVSDK_NGX_Parameter_MV_Scale_X, motion_contract.motion_scale_x);
  motion_contract.has_motion_scale_y = GetFloatIfPresent(
      parameters, NVSDK_NGX_Parameter_MV_Scale_Y, motion_contract.motion_scale_y);
  ResolveMotionScale(&motion_contract);
  const float motion_scale_x = motion_contract.motion_scale_x;
  const float motion_scale_y = motion_contract.motion_scale_y;
  const int32_t frame_reset = GetInt(parameters, NVSDK_NGX_Parameter_Reset, 0);
  // Every pass of this frame resets against the same epoch (ProcessInline).
  const uint64_t chain_reset_epoch = history_reset_epoch.load(std::memory_order_relaxed);
  const resource_view output_view = {reinterpret_cast<uint64_t>(out.ImageView)};
  float final_transfer = 0.f;
  float final_color = 0.f;
  uint32_t model_passes = 0;
  uint32_t bypassed_passes = 0;
  for (uint32_t pass = 0; pass < passes; ++pass) {
    const Surface ref = hdr_mode != 0
                            ? (pass == 0 ? kWork0 : ((pass & 1) != 0 ? kWorkA : kWorkB))
                            : (pass == 0 ? kOriginal : ((pass & 1) != 0 ? kStack0 : kStack1));
    const Surface write =
        hdr_mode != 0 ? ((pass & 1) != 0 ? kWorkB : kWorkA) : ((pass & 1) != 0 ? kStack1 : kStack0);
    const bool to_output = hdr_mode == 0 && pass + 1 == passes;
    final_transfer = PassTransferStrength(pass);
    final_color = PassColorStrength(pass);
    if (hdr_mode != 0 && final_transfer <= 0.f && final_color <= 0.f) {
      // Explicit zero-strength pass: the running image carried across
      // exactly, keeping the ping-pong parity of later passes.
      transition(ref, resource_usage::copy_source);
      transition(write, resource_usage::copy_dest);
      cmd->copy_texture_region(images[ref], 0, nullptr, images[write], 0, nullptr);
      transition(write, resource_usage::shader_resource);
      ++bypassed_passes;
      continue;
    }
    // 2c. Encode the running image into the proxy (v6: the divisor texel at
    // t3; the encode reads neither the proxy it writes nor the neural image).
    transition(ref, resource_usage::shader_resource);
    transition(kProxy, resource_usage::unordered_access);
    const resource_view ref_only[4] = {views[ref], views[ref], views[ref], views[ref]};
    if (hdr_mode == 0) {
      constants.transfer_strength = final_transfer;
      constants.color_strength = final_color;
      dispatch(kLegacyEncode, ref_only, {views[kProxy]}, &constants, 20, groups_x, groups_y);
    } else {
      const V6CodecConstants encode = MakeV6Constants(
          *workset, width, height, width, height, 0, 0, width, height, width, height,
          frame_divisor, v6_pedestal_cap, final_transfer, final_color, v6_encoding, v6_dark_gate,
          0.f);
      const resource_view encode_views[4] = {views[ref], views[ref], views[ref], scale_view};
      dispatch(kEncodeV6, encode_views, {views[kProxy]}, &encode, 28, groups_x, groups_y);
    }
    transition(kProxy, resource_usage::shader_resource);
    transition(kNeural, resource_usage::unordered_access);

    // 3. Feature 18: proxy -> neural, with the game's own guides.
    NrFeatureSlot& slot = feature.slots[pass];
    NVSDK_NGX_Parameter* const nr = slot.parameters;
    nr->Set("DLSSNR.Color", static_cast<void*>(&proxy_vk));
    nr->Set("DLSSNR.Output", static_cast<void*>(&neural_vk));
    nr->Set("DLSSNR.MVec", static_cast<void*>(motion));
    nr->Set("DLSSNR.Depth", static_cast<void*>(depth));
    for (const auto& [key, value] : std::initializer_list<std::pair<const char*, uint32_t>>{
             {NVSDK_NGX_Parameter_Width, width},
             {NVSDK_NGX_Parameter_Height, height},
             {NVSDK_NGX_Parameter_OutWidth, width},
             {NVSDK_NGX_Parameter_OutHeight, height},
             {"DLSSNR.ColorSubrectBaseX", 0},
             {"DLSSNR.ColorSubrectBaseY", 0},
             {"DLSSNR.ColorSubrectWidth", width},
             {"DLSSNR.ColorSubrectHeight", height},
             {"DLSSNR.DepthSubrectBaseX",
              GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X)},
             {"DLSSNR.DepthSubrectBaseY",
              GetUInt(parameters, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y)},
             {"DLSSNR.DepthSubrectWidth", render_width},
             {"DLSSNR.DepthSubrectHeight", render_height},
             {"DLSSNR.MVecSubrectBaseX", motion_x},
             {"DLSSNR.MVecSubrectBaseY", motion_y},
             {"DLSSNR.MVecSubrectWidth", motion_width},
             {"DLSSNR.MVecSubrectHeight", motion_height},
             {"DLSSNR.OutputSubrectBaseX", 0},
             {"DLSSNR.OutputSubrectBaseY", 0},
             {"DLSSNR.OutputSubrectWidth", width},
             {"DLSSNR.OutputSubrectHeight", height},
             {"DLSSNR.InputWidth", width},
             {"DLSSNR.InputHeight", height},
             {"DLSSNR.OutputWidth", width},
             {"DLSSNR.OutputHeight", height},
         }) {
      NVSDK_NGX_Parameter_SetUI(nr, key, value);
    }
    NVSDK_NGX_Parameter_SetF(nr, "DLSSNR.ScalingRatio", 1.f);
    NVSDK_NGX_Parameter_SetF(nr, "DLSSNR.Scale", 1.f);
    NVSDK_NGX_Parameter_SetF(nr, "DLSSNR.MVecScaleX",
                             motion_scale_x * motion_scale_x_multiplier.load());
    NVSDK_NGX_Parameter_SetF(nr, "DLSSNR.MVecScaleY",
                             motion_scale_y * motion_scale_y_multiplier.load());
    SetModelParameters(slot, pass, chain_reset_epoch, feature.create_flags, frame_reset, false);
    NVSDK_NGX_Result result = NVSDK_NGX_Result_FAIL_UnableToInitializeFeature;
    TrackRecordedUse(cmd, feature.nr_use[pass]);
    {
      DirectCallScope direct;
      try {
        result = runtime.evaluate(command_buffer, slot.handle, nr, nullptr);
      } catch (...) {
        result = NVSDK_NGX_Result_FAIL_UnableToInitializeFeature;
      }
    }
    last_result = static_cast<uint32_t>(result);
    transition(kNeural, resource_usage::shader_resource);
    if (NVSDK_NGX_FAILED(result)) {
      // The output is untouched (the passes before this one wrote only
      // scratch images), so the frame keeps the game's image.
      static std::atomic_bool logged{false};
      if (!logged.exchange(true)) {
        char text[160];
        snprintf(text, sizeof(text),
                 "NR feature 18 evaluate on Vulkan failed with 0x%08x (stack pass %u); the"
                 " frame keeps the game's image (failure_backoff)",
                 static_cast<uint32_t>(result), pass + 1);
        Log(reshade::log::level::error, text);
      }
      return NrDeclineReason::kFailureBackoff;
    }
    nr_passes.fetch_add(1, std::memory_order_relaxed);
    ++model_passes;
    slot.fail_count = 0;

    // 4. The look stage on this pass's N (RunLookStage's shaping half; the
    // NREditTrace is D3D12-only): the bands, the temporal filter on the
    // pass's history, every gain into look_output (N'), which the resolve
    // then reads in N's place.
    resource_view neural_view = views[kNeural];
    LookHistoryVk& history = feature.look_history[pass];
    if (history.buffers[0].image.handle != 0
        && (history.width != width || history.height != height)) {
      // Preserve a pair until every command buffer that recorded it is reset.
      feature.retired_history.push_back(
          {{history.buffers[0], history.buffers[1]},
           feature.look_history_use[pass]});
      history = {};
      feature.look_history_use[pass] = 0;
    }
    bool temporal = look_shape && look_settings.stabilize != 0;
    if (!temporal) history.valid = false;
    if (look_shape) {
      const int64_t now_ns = SteadyNowNs();
      look::Frame look_frame{
          .nr_width = width,
          .nr_height = height,
          .full_width = width,
          .full_height = height,
          .reference_linear = hdr_mode != 0,
      };
      if (temporal && history.buffers[0].image.handle == 0) {
        bool allocated = true;
        for (LookHistoryImage& half : history.buffers) {
          allocated = allocated
                   && api_device->create_resource(
                          resource_desc(width, height, 1, 1, format::r32g32b32a32_uint, 1,
                                        memory_heap::gpu_only, resource_usage::unordered_access),
                          nullptr, resource_usage::undefined, &half.image)
                   && api_device->create_resource_view(
                          half.image, resource_usage::undefined,
                          resource_view_desc(format::r32g32b32a32_uint), &half.view);
        }
        if (allocated) {
          history.width = width;
          history.height = height;
          feature.look_history_use[pass] = next_use_id++;
          TrackRecordedUse(cmd, feature.look_history_use[pass]);
          for (const LookHistoryImage& half : history.buffers) {
            barrier(half.image, resource_usage::undefined, resource_usage::unordered_access);
          }
        } else {
          // No history this frame: shape without the temporal filter and
          // try again next frame.
          for (LookHistoryImage& half : history.buffers) {
            DestroyLookHistoryImage(api_device, half);
            half = {};
          }
          temporal = false;
        }
      }
      const bool whole_edit = look_settings.stabilize_alpha > 0.f;
      // Unused history and motion bind something valid of the declared
      // kind, never read: the scratch texel and the copied output.
      resource_view history_views[2] = {views[kLookScratch], views[kLookScratch]};
      resource_view motion_view = views[kOriginal];
      transition(kLookScratch, resource_usage::unordered_access);
      if (temporal) {
        TrackRecordedUse(cmd, feature.look_history_use[pass]);
        PlanLookHistory(history, slot.model_reset, whole_edit, now_ns, &look_frame);
        history_views[0] = history.buffers[history.written ^ 1u].view;  // u9 HistoryOut
        history_views[1] = history.buffers[history.written].view;       // u10 HistoryIn
        if (look_settings.stabilize == 2u) {
          // DLSS motion vectors (Programming Guide 3.6), scaled to the NR
          // grid as RunLookStage does. SampledInputScope makes a known
          // GENERAL input readable and restores it around the dispatch.
          look_frame.motion = motion_width != 0 && motion_height != 0;
          if (look_frame.motion) {
            look_frame.motion_scale[0] = motion_scale_x * motion_scale_x_multiplier.load()
                * static_cast<float>(width) / static_cast<float>(motion_width);
            look_frame.motion_scale[1] = motion_scale_y * motion_scale_y_multiplier.load()
                * static_cast<float>(height) / static_cast<float>(motion_height);
            look_frame.motion_base[0] = motion_x;
            look_frame.motion_base[1] = motion_y;
            look_frame.motion_grid[0] = motion_width;
            look_frame.motion_grid[1] = motion_height;
            motion_view = {reinterpret_cast<uint64_t>(motion->Resource.ImageViewInfo.ImageView)};
          }
        }
        // Orders this frame's history read after the previous frame's write.
        global_barrier();
      }
      CountLookStabilize(look_settings, look_frame.motion);
      // The filter flag follows `temporal`, not the setting (v7.0.0-rc9).
      look::Settings shaping = look_settings;
      if (!temporal) shaping.stabilize = 0;
      look::Constants look_constants = look::MakeConstants(shaping, look_frame);
      transition(kLookOutput, resource_usage::unordered_access);
      if (look_bands) {
        for (const Surface band : {kBandA, kBandB, kBandMaxA, kBandMaxB}) {
          transition(band, resource_usage::unordered_access);
        }
      }
      // Without the bands the compose's never-read band bindings name N'.
      const resource_view look_sampled[5] = {views[ref], views[kProxy], views[kNeural],
                                             views[ref], motion_view};
      const resource_view look_storage[5] = {
          views[kLookOutput], views[look_bands ? kBandA : kLookOutput],
          views[look_bands ? kBandB : kLookOutput], views[look_bands ? kBandMaxA : kLookOutput],
          views[look_bands ? kBandMaxB : kLookOutput]};
      const auto look_dispatch = [&](reshade::api::pipeline program, uint32_t x, uint32_t y) {
        cmd->bind_pipeline(pipeline_stage::all_compute, program);
        cmd->push_descriptors(
            shader_stage::all_compute, look_layout, 0,
            {{}, 0, 0, 5, descriptor_type::texture_shader_resource_view, look_sampled});
        cmd->push_descriptors(
            shader_stage::all_compute, look_layout, 0,
            {{}, 5, 0, 5, descriptor_type::texture_unordered_access_view, look_storage});
        cmd->push_descriptors(
            shader_stage::all_compute, look_layout, 0,
            {{}, 14, 0, 2, descriptor_type::texture_unordered_access_view, history_views});
        cmd->push_constants(shader_stage::all_compute, look_layout, 1, 0,
                            sizeof(look::Constants) / 4, &look_constants);
        if (look_frame.motion) {
          const SampledInputScope sampled_motion(cmd, motion);
          cmd->dispatch(x, y, 1);
        } else {
          cmd->dispatch(x, y, 1);
        }
        global_barrier();
      };
      // RecordCompose's order: five band modes at half size, then the compose.
      if ((look_constants.flags & look::kFlagBands) != 0u) {
        look::Constants compose_constants = look_constants;
        look_constants.size[0] = (width + 1) / 2;
        look_constants.size[1] = (height + 1) / 2;
        for (uint32_t mode = 0; mode < 5; ++mode) {
          look_constants.mode = mode;
          look_dispatch(look_band, (look_constants.size[0] + 7) / 8,
                        (look_constants.size[1] + 7) / 8);
        }
        look_constants = compose_constants;
      }
      look_constants.mode = 0;
      look_dispatch(look_compose, groups_x, groups_y);
      transition(kLookOutput, resource_usage::shader_resource);
      FinishLookPass(&history, temporal, whole_edit, now_ns, look_constants, workset->id, pass,
                     width, height);
      neural_view = views[kLookOutput];
    }

    // 5. The pass's resolve: legacy into the next stack surface or, on the
    // last pass, straight into the DLSS output (GENERAL, bound as storage);
    // v6 into work_a / work_b.
    const resource_view resolve_views[4] = {views[ref], views[kProxy], neural_view,
                                            hdr_mode != 0 ? scale_view : views[ref]};
    if (!to_output) transition(write, resource_usage::unordered_access);
    if (hdr_mode == 0) {
      constants.source_base[0] = to_output ? output_x : 0;
      constants.source_base[1] = to_output ? output_y : 0;
      dispatch(kLegacyDecode, resolve_views, {to_output ? output_view : views[write]},
               &constants, 20, groups_x, groups_y);
    } else {
      const V6CodecConstants resolve = MakeV6Constants(
          *workset, width, height, width, height, 0, 0, width, height, width, height,
          frame_divisor, v6_pedestal_cap, final_transfer, final_color, v6_encoding,
          v6_dark_gate, chroma_clamp_stops.load(std::memory_order_relaxed));
      dispatch(kResolveV6, resolve_views, {views[write]}, &resolve, 28, groups_x, groups_y);
    }
    if (!to_output) transition(write, resource_usage::shader_resource);
  }

  // Vulkan has no restore-target gate: NR records inside the game's own
  // evaluate and puts the compute bind point back (RestoreComputeState).
  gate_ever_opened.store(true, std::memory_order_relaxed);
  if (hdr_mode != 0) {
    // 6. v6: the pedestal block means, then the one commit (dark-gated
    // pedestal removal and the encode back into the output's own transfer)
    // from the last pass's work surface straight into the output: D3D12
    // commits into `decoded` and copies it over the output, here the output
    // is a storage image already.  The strengths are the last pass's.
    const Surface final_surface = ((passes - 1) & 1) != 0 ? kWorkB : kWorkA;
    transition(final_surface, resource_usage::shader_resource);
    const V6CodecConstants commit = MakeV6Constants(
        *workset, width, height, width, height, output_x, output_y, width, height, width, height,
        frame_divisor, v6_pedestal_cap, final_transfer, final_color, v6_encoding, v6_dark_gate,
        0.f);
    const resource_view commit_views[4] = {views[final_surface], views[kWork0], scale_view,
                                           views[kWork0]};
    transition(kBlockMean, resource_usage::unordered_access);
    // A frame without the pedestal removal skips the reduce: the commit's
    // PedestalGate 0 multiplies the block means by zero.
    // The classic reduce on every frame: D3D12's MEASURE_UNSHAPED variant
    // for a single shaped pass is not ported (its own documented fallback).
    if (FramePedestalRemoval(*workset)) {
      const V6CodecConstants reduce = MakeV6Constants(
          *workset, width, height, width, height, 0, 0, width, height, width, height,
          frame_divisor, v6_pedestal_cap, final_transfer, final_color, v6_encoding,
          v6_dark_gate, chroma_clamp_stops.load(std::memory_order_relaxed));
      dispatch(kPedestalReduce, commit_views, {views[kBlockMean]}, &reduce, 28,
               (width + 31) / 32, (height + 31) / 32);
      transition(kBlockMean, resource_usage::unordered_access);
    }
    dispatch(feed != FeedSource::kV1 ? kCommitExposure : kCommit, commit_views,
             {output_view, views[kBlockMean]}, &commit, 28, groups_x, groups_y);
  }
  // Make the output's writes visible to what the game records next.
  barrier(output_image, resource_usage::unordered_access, resource_usage::unordered_access);
  if (pre_sr) {
    // The game sees the same Color format, extent, bases and native layout.
    // Only the backing image/view changes until its SR evaluate returns.
    NVSDK_NGX_Resource_VK redirected = *output;
    redirected.Resource.ImageViewInfo = out;
    const VkImageMemoryBarrier ready = {
        static_cast<VkStructureType>(45), nullptr, 0x20u | 0x40u, 0x8000u | 0x10000u,
        kLayoutGeneral, source_layout.original, UINT32_MAX, UINT32_MAX,
        out.Image, out.SubresourceRange};
    source_layout.dispatch(command_buffer, 0x10000u, 0x10000u, 0,
                           0, nullptr, 0, nullptr, 1, &ready);
    render_color->cmd = cmd;
    render_color->layout = source_layout.original;
    render_color->Redirect(redirected);
  }
  nr_evaluates.fetch_add(1, std::memory_order_relaxed);
  static std::atomic_uint64_t stage_successes[2];
  const uint64_t count =
      stage_successes[pre_sr ? 0 : 1].fetch_add(1, std::memory_order_relaxed) + 1;
  if (count <= 4 || count == 60 || count % 600 == 0) {
    std::ostringstream text;
    text << "Vulkan feature 18 evaluation succeeded (count=" << count << ", " << width
         << 'x' << height << ", codec " << (hdr_mode != 0 ? "v6" : "legacy")
         << ", stage=" << (pre_sr ? "render" : "upscaled")
         << " requested=" << stack << " model_passes=" << model_passes
         << " bypassed=" << bypassed_passes << " pending=" << (stack - passes)
         << " plan=" << stages::Current().packed
         << (look_shape ? ", look shaped" : "") << ')';
    Log(reshade::log::level::info, text.str());
  }
  return NrDeclineReason::kCount;
}

// Caller holds runtime_mutex.  The game's NGX Vulkan Shutdown: NR's features
// and runtime go first, while the device and the layer are whole.  The game
// shuts NGX down after its last DLSS evaluate completed (its own DLSS
// features are released by then), which proves NR's work complete too.
inline void ShutdownNr(VkDevice device) {
  // NGX Shutdown1(non-null) affects only that device. An auxiliary NGX
  // instance says nothing about pending work on the device serving NR.
  if (runtime.device == nullptr || (device != nullptr && device != runtime.device)) return;
  for (auto& [handle, feature] : features) {
    if (feature.device == game_api_device.load(std::memory_order_relaxed)) {
      FreeFeatureNr(&feature);
    }
  }
  {
    DirectCallScope direct;
    runtime.shutdown(runtime.device);
  }
  runtime.device = nullptr;
  Log(reshade::log::level::info, "NR runtime shut down with the game's NGX Vulkan Shutdown");
}

// Caller holds runtime_mutex.  The game's device is going away: every Vulkan
// NR object dies with it (vkDestroyDevice requires its work complete).
inline void OnDestroyGameDevice() {
  reshade::api::device* const device = game_api_device.load(std::memory_order_relaxed);
  if (runtime.device != nullptr) {
    Log(reshade::log::level::warning,
        "the game destroyed its Vulkan device without an NGX Vulkan Shutdown; NR's"
        " runtime state is left to die with the device");
  }
  for (auto it = features.begin(); it != features.end();) {
    if (it->second.device != device) {
      ++it;
      continue;
    }
    FreeFeatureNr(&it->second, false);
    it = features.erase(it);
  }
  for (auto& program : codec_programs) {
    if (program.handle != 0) device->destroy_pipeline(program);
    program = {};
  }
  if (codec_layout.handle != 0) device->destroy_pipeline_layout(codec_layout);
  codec_layout = {};
  for (reshade::api::pipeline* program : {&look_band, &look_compose}) {
    if (program->handle != 0) device->destroy_pipeline(*program);
    *program = {};
  }
  if (look_layout.handle != 0) device->destroy_pipeline_layout(look_layout);
  look_layout = {};
  {
    std::lock_guard lock(scale_samples_mutex);
    if (scale_readback.handle != 0) {
      device->unmap_buffer_region(scale_readback);
      device->destroy_resource(scale_readback);
    }
    scale_readback = {};
    scale_mapped = nullptr;
    for (ScaleSample& sample : scale_samples) sample = {};
    scale_head = 0;
    scale_calls = 0;
  }
  runtime = {};
  codec_failed = false;
  extension_state.store(0, std::memory_order_relaxed);
  game_api_device.store(nullptr, std::memory_order_relaxed);
  game_device.store(nullptr, std::memory_order_relaxed);
}

// Metadata exists before an evaluate selects the NR owner. Clean up every
// Vulkan device, including auxiliary devices that never owned NR objects.
inline void OnDestroyDeviceContext(reshade::api::device* device) {
  RuntimeLock runtime_lock(runtime_mutex);
  const VkDevice destroyed = reinterpret_cast<VkDevice>(device->get_native());
  for (auto it = features.begin(); it != features.end();) {
    if (it->second.device == device) {
      FreeFeatureNr(&it->second, false);
      it = features.erase(it);
    } else {
      ++it;
    }
  }
  {
    std::lock_guard lock(context_mutex);
    device_contexts.erase(destroyed);
    api_devices.erase(destroyed);
  }
  if (game_init_device.load(std::memory_order_relaxed) == destroyed) {
    game_init_device.store(nullptr, std::memory_order_relaxed);
    game_instance.store(nullptr, std::memory_order_relaxed);
    game_physical_device.store(nullptr, std::memory_order_relaxed);
  }
  {
    std::lock_guard lock(layouts_mutex);
    layout_dynamic_sets.erase(device);
  }
  std::vector<reshade::api::command_list*> destroyed_lists;
  {
    std::unique_lock lock(command_lists_mutex);
    for (auto it = command_lists.begin(); it != command_lists.end();) {
      if (it->second.device != device) {
        ++it;
        continue;
      }
      destroyed_lists.push_back(it->second.list);
      it = command_lists.erase(it);
    }
  }
  for (auto* list : destroyed_lists) {
    RetireScaleSamples(list);
    ForgetRecordedUses(list);
  }
}

// Creates share one body; `real` forwards with the export's own arguments.
template <typename Real>
inline NVSDK_NGX_Result CreateBody(Entry entry, VkDevice device,
                                   VkCommandBuffer command_buffer, NVSDK_NGX_Feature feature,
                                   NVSDK_NGX_Parameter* parameters,
                                   NVSDK_NGX_Handle** output_handle, Real&& real) {
  Enter(entry);
  CallbackScope callback_scope;
  const NgxNesting nesting(&call_depth);
  if (!callback_scope || !nesting.outermost || InsideDirectCall()) return real();
  const ngx_serial::Call ngx_turn;
  ++intercepted_creates;
  const NVSDK_NGX_Result result = real();
  {
    std::ostringstream text;
    text << "NGX feature create (Vulkan " << kEntryNames[entry]
         << "): feature=" << static_cast<int>(feature) << " ("
         << SourceFeatureName(feature) << "), result=0x" << std::hex
         << static_cast<uint32_t>(result);
    Log(NVSDK_NGX_FAILED(result) ? reshade::log::level::warning
                                 : reshade::log::level::info,
        text.str());
  }
  if (NVSDK_NGX_FAILED(result) || output_handle == nullptr || *output_handle == nullptr) {
    return result;
  }
  int32_t flags = 0;
  if (parameters != nullptr) {
    NVSDK_NGX_Parameter_GetI(parameters, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
                             &flags);
  }
  RuntimeLock lock(runtime_mutex);
  features[*output_handle] = {feature, static_cast<uint32_t>(flags)};
  {
    std::shared_lock lists_lock(command_lists_mutex);
    if (const auto found = command_lists.find(command_buffer); found != command_lists.end()) {
      features[*output_handle].device = found->second.device;
    } else if (device != nullptr) {
      std::lock_guard context_lock(context_mutex);
      if (const auto found = api_devices.find(device); found != api_devices.end()) {
        features[*output_handle].device = found->second;
      }
    }
  }
  if ((feature == kFeatureDlss || feature == kFeatureDlssd)
      && features[*output_handle].device != nullptr
      && game_api_device.load(std::memory_order_relaxed) == nullptr) {
    game_api_device.store(features[*output_handle].device, std::memory_order_relaxed);
    game_device.store(
        reinterpret_cast<VkDevice>(features[*output_handle].device->get_native()),
        std::memory_order_relaxed);
  }
  features[*output_handle].render_width = GetUInt(parameters, NVSDK_NGX_Parameter_Width);
  features[*output_handle].render_height = GetUInt(parameters, NVSDK_NGX_Parameter_Height);
  FeatureState& contract = features[*output_handle].ngx;
  contract.declared_output_width = GetUInt(parameters, NVSDK_NGX_Parameter_OutWidth);
  contract.declared_output_height = GetUInt(parameters, NVSDK_NGX_Parameter_OutHeight);
  contract.output_subrects_enabled =
      GetInt(parameters, NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0) != 0;
  return result;
}

template <int Slot>
inline NVSDK_NGX_Result NVSDK_CONV HookedCreate(
    VkCommandBuffer command_buffer, NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** output_handle) {
  const auto real = ngx_serial::Serialized(reinterpret_cast<CreateFn>(slot_real[Slot].create));
  return CreateBody(kCreate, nullptr, command_buffer, feature, parameters, output_handle, [&] {
    return real(command_buffer, feature, parameters, output_handle);
  });
}

template <int Slot>
inline NVSDK_NGX_Result NVSDK_CONV HookedCreate1(
    VkDevice device, VkCommandBuffer command_buffer, NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** output_handle) {
  const auto real =
      ngx_serial::Serialized(reinterpret_cast<Create1Fn>(slot_real[Slot].create1));
  return CreateBody(kCreate1, device, command_buffer, feature, parameters, output_handle, [&] {
    return real(device, command_buffer, feature, parameters, output_handle);
  });
}

// Called outside runtime_mutex. Version 1 bridges retain FG presentation but
// no longer mirror Render/Upscaled NR through their D3D12 carrier. Presence
// alone is insufficient: this ABI is uint32_t __cdecl(void), returning 1.
inline bool BridgeStageProtocol() {
  if (InsideDllMain()) return false;
  NoteLoaderCall("Vulkan/BridgeStageProtocol", LoaderCallSafety::kUnsafeUnderLoaderLock);
  // The enclosing NGX turn serializes this cache. Retain a path, never a stale
  // HMODULE; a fresh reference and version check also handle bridge reloads.
  static std::wstring known_path;
  const auto supports = [](HMODULE module) {
    const auto version = reinterpret_cast<uint32_t(__cdecl*)()>(
        GetProcAddress(module, "DLSS5BridgeStageProtocolVersion"));
    return version != nullptr && version() == 1;
  };
  if (!known_path.empty()) {
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(0, known_path.c_str(), &pinned)) {
      const bool supported = supports(pinned);
      FreeLibrary(pinned);
      if (supported) return true;
    }
    known_path.clear();
  }
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                            GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  bool supported = false;
  MODULEENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
    std::wstring name(entry.szModule);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if ((name.rfind(L"dlss5-bridge", 0) != 0 && name.rfind(L"dlss5-feed", 0) != 0)
        || name.find(L".addon") == std::wstring::npos) continue;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(entry.hModule), &pinned)) continue;
    supported = supports(pinned);
    if (supported) known_path = entry.szExePath;
    FreeLibrary(pinned);
    if (supported) break;
  }
  CloseHandle(snapshot);
  return supported;
}

template <int Slot>
inline NVSDK_NGX_Result NVSDK_CONV HookedEvaluate(
    VkCommandBuffer command_buffer, const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters, PFN_NVSDK_NGX_ProgressCallback callback) {
  Enter(kEvaluate);
  const auto real =
      ngx_serial::Serialized(reinterpret_cast<EvaluateFn>(slot_real[Slot].evaluate));
  CallbackScope callback_scope;
  const NgxNesting nesting(&call_depth);
  if (!callback_scope || !nesting.outermost || InsideDirectCall()) {
    return real(command_buffer, handle, parameters, callback);
  }
  const stages::Scope stage_plan(stages::Point::Upscaled);
  const EvaluateChainScope chain_scope;
  // Counted in `seen` from here until this evaluate names its terminal.
  const EvaluateInFlightScope in_flight(true);
  ++intercepted_evaluations;
  NgxLifecycleTick();
  if (enabled.load(std::memory_order_relaxed)) PrimeNgxLoaderSymbols(nullptr);
  // Hold the NGX reservation through NR injection, with the process-wide
  // NGX turn before runtime_mutex just as Present and retirement acquire it.
  const ngx_serial::Call ngx_turn;
  const auto passthrough = [&](NrDeclineReason decline) {
    const auto result = real(command_buffer, handle, parameters, callback);
    CountNrDecline(NVSDK_NGX_FAILED(result) ? NrDeclineReason::kGameEvaluateFailed : decline);
    return result;
  };
  if (!enabled.load()) {
    nr_off_seen_ns.store(SteadyNowNs(), std::memory_order_relaxed);
    return passthrough(NrDeclineReason::kNrDisabledEvaluation);
  }
  if (command_buffer == nullptr || handle == nullptr || parameters == nullptr) {
    return passthrough(NrDeclineReason::kMalformedEvaluate);
  }
  if (NrYieldsToForeign()) return passthrough(NrDeclineReason::kForeignNr);
  // v8.5.0-rc4: a DX11 bridge add-on of another project that re-issues this
  // evaluate as a Direct3D 12 DLSS evaluate on its own device, where the
  // D3D12 path serves NR once per frame.  BG3 v8.5.0-rc1: 10170 D3D12
  // evaluates served NR while the Vulkan create failed 26 times
  // (0xBAD00002), and every Vulkan evaluate read as an eligible decline
  // (ratio 0.4995); a Vulkan create that succeeded would run NR twice.
  // Only while the D3D12 path ran NR within the last 5 s, or NR came back
  // on less than 5 s ago after it once ran there: a bridge that stops, or
  // whose evaluates NR declines, hands the frames back here rather than
  // leave NR running nowhere.  The window outlasts a cold feature-18 create
  // (2.7 s measured, Resonance v8.5.0-rc1), so a re-enable never starts a
  // Vulkan create the bridge's frames would make redundant.  Without the
  // bridge, unchanged.
  const int64_t d3d12_success = d3d12_nr_success_ns.load(std::memory_order_relaxed);
  const bool bridge_serves =
      foreign_dx11_tool_seen.load(std::memory_order_relaxed)
      && !d3d12_present_seen.load(std::memory_order_relaxed) && d3d12_success != 0
      && SteadyNowNs() - std::max(d3d12_success, nr_off_seen_ns.load(std::memory_order_relaxed))
             < 5'000'000'000
      && !BridgeStageProtocol();
  if (bridge_yielding.exchange(bridge_serves) != bridge_serves
      && bridge_yield_changes.fetch_add(1, std::memory_order_relaxed) < 4) {
    Log(reshade::log::level::info,
        bridge_serves ? "Vulkan DLSS evaluates pass through: a DX11 bridge add-on of another"
                        " project re-issues them as Direct3D 12 DLSS evaluates, where NR runs"
                        " (source_other)"
                      : "Vulkan DLSS evaluates are served again: the DX11 bridge's Direct3D 12"
                        " path ran no NR for 5 s");
  }
  if (bridge_serves) return passthrough(NrDeclineReason::kSourceOther);
  bool eligible = false;
  {
    RuntimeLock lock(runtime_mutex);
    const auto feature = features.find(handle);
    eligible = feature != features.end()
        && (feature->second.id == kFeatureDlss || feature->second.id == kFeatureDlssd);
  }
  if (!eligible) return passthrough(NrDeclineReason::kNgxNotDlssEvaluation);
  if (dlss_evaluates.fetch_add(1, std::memory_order_relaxed) == 0) {
    Log(reshade::log::level::info,
        "first Vulkan DLSS evaluate intercepted (slot=" + std::to_string(Slot) + ")");
  }
  NrDeclineReason render_decline = NrDeclineReason::kZeroStrengthPassthrough;
  NrDeclineReason upscaled_decline = NrDeclineReason::kZeroStrengthPassthrough;
  NVSDK_NGX_Result result;
  {
    RenderColorScope color(parameters);
    GuardHook([&] {
      render_decline = ProcessNr(command_buffer, handle, parameters, stages::Point::Render, &color);
    });
    result = real(command_buffer, handle, parameters, callback);
  }
  if (NVSDK_NGX_FAILED(result)) {
    CountNrDecline(NrDeclineReason::kGameEvaluateFailed);
    return result;
  }
  GuardHook([&] {
    upscaled_decline = ProcessNr(command_buffer, handle, parameters, stages::Point::Upscaled);
  });
  // Keep the evaluate funnel one terminal per game evaluate. Present belongs
  // to the bridge carrier and never supplies a fallback layer here.
  if (render_decline == NrDeclineReason::kCount || upscaled_decline == NrDeclineReason::kCount) {
    ++successful_evaluations;
  } else {
    CountNrDecline(stages::Current().Count(stages::Point::Upscaled) != 0
                       ? upscaled_decline : render_decline);
  }
  return result;
}

template <int Slot>
inline NVSDK_NGX_Result NVSDK_CONV HookedRelease(NVSDK_NGX_Handle* handle) {
  Enter(kRelease);
  const auto real = ngx_serial::Serialized(reinterpret_cast<ReleaseFn>(slot_real[Slot].release));
  CallbackScope callback_scope;
  const NgxNesting nesting(&call_depth);
  const ngx_serial::Call ngx_turn;
  if (callback_scope && nesting.outermost && !InsideDirectCall()) {
    // The game's release proves the submissions that used this handle, and
    // with them NR's work on it, complete ("Lifetime" above).
    RuntimeLock lock(runtime_mutex);
    const auto feature = features.find(handle);
    if (feature != features.end()) {
      GuardHook([&] { FreeFeatureNr(&feature->second); });
      features.erase(feature);
    }
  }
  return real(handle);
}

inline CreateFn const kCreateWrappers[kMaxNgxSlots] = {
    HookedCreate<0>, HookedCreate<1>, HookedCreate<2>, HookedCreate<3>,
    HookedCreate<4>, HookedCreate<5>, HookedCreate<6>, HookedCreate<7>,
};
inline Create1Fn const kCreate1Wrappers[kMaxNgxSlots] = {
    HookedCreate1<0>, HookedCreate1<1>, HookedCreate1<2>, HookedCreate1<3>,
    HookedCreate1<4>, HookedCreate1<5>, HookedCreate1<6>, HookedCreate1<7>,
};
inline EvaluateFn const kEvaluateWrappers[kMaxNgxSlots] = {
    HookedEvaluate<0>, HookedEvaluate<1>, HookedEvaluate<2>, HookedEvaluate<3>,
    HookedEvaluate<4>, HookedEvaluate<5>, HookedEvaluate<6>, HookedEvaluate<7>,
};
inline ReleaseFn const kReleaseWrappers[kMaxNgxSlots] = {
    HookedRelease<0>, HookedRelease<1>, HookedRelease<2>, HookedRelease<3>,
    HookedRelease<4>, HookedRelease<5>, HookedRelease<6>, HookedRelease<7>,
};

inline NgxSlotHooks MakeHookItems(int slot) {
  return {{
      {"NVSDK_NGX_VULKAN_CreateFeature", &slot_real[slot].create,
       reinterpret_cast<void*>(kCreateWrappers[slot]), &slot_guard[slot].records[0]},
      {"NVSDK_NGX_VULKAN_CreateFeature1", &slot_real[slot].create1,
       reinterpret_cast<void*>(kCreate1Wrappers[slot]), &slot_guard[slot].records[1]},
      {"NVSDK_NGX_VULKAN_EvaluateFeature", &slot_real[slot].evaluate,
       reinterpret_cast<void*>(kEvaluateWrappers[slot]), &slot_guard[slot].records[2]},
      {"NVSDK_NGX_VULKAN_ReleaseFeature", &slot_real[slot].release,
       reinterpret_cast<void*>(kReleaseWrappers[slot]), &slot_guard[slot].records[3]},
  }};
}

inline const NgxSlotFamily kSlots = {
    "VULKAN",   slot_used,        slot_module, slot_noncore,
    slot_guard, &refused_modules, MakeHookItems};

// Caller holds runtime_mutex (InstallHooks' phase 2).  Same slot logic and
// retry schedule as the D3D11 family (TryHookNgx11Module).
inline bool TryHookModule(HMODULE module, const std::wstring& module_path, bool is_core) {
  if (module == nullptr) return false;
  const auto absent = absent_modules.find(module);
  if (absent != absent_modules.end()
      && absent->second == detour_guard::ImageIdentity(module)) {
    return false;
  }
  int slot = -1;
  if (!FindNgxSlot(kSlots, module, &slot)) return false;
  if (slot < 0) {
    NoteNgxSlotsFull(kSlots, module, module_path);
    return false;
  }
  if (GetProcAddress(module, "NVSDK_NGX_VULKAN_EvaluateFeature") == nullptr) {
    absent_modules[module] = detour_guard::ImageIdentity(module);
    return false;
  }
  const auto backing_off = failed_modules.find(module);
  if (backing_off != failed_modules.end()
      && present_generation < backing_off->second.next_attempt_present) {
    return false;
  }
  if (AttachNgxSlot(kSlots, slot, module, module_path) == 0) {
    NgxHookRetry& retry = failed_modules[module];
    bool announce = false;
    const uint64_t delay = NoteHookInstallFailure(
        retry.attempts, retry.next_attempt_present, announce);
    if (announce) {
      Log(reshade::log::level::warning,
          "detouring the NGX Vulkan exports FAILED for "
              + NarrowPath(module_path.c_str()) + "; its Vulkan evaluates pass"
              " through without NR. Attempt " + std::to_string(retry.attempts)
              + ", retrying in " + std::to_string(delay) + " presents");
    }
    return false;
  }
  failed_modules.erase(module);
  slot_used[slot] = true;
  slot_module[slot] = module;
  slot_noncore[slot].store(!is_core, std::memory_order_relaxed);
  ngx_ever_hooked.store(true, std::memory_order_relaxed);
  Log(reshade::log::level::info,
      "detoured NGX Vulkan module copy [" + std::to_string(slot) + "] "
          + NarrowPath(module_path.c_str()) + (is_core ? " (core)" : ""));
  // The Init capture sits on the core alone: that is where the game's NGX
  // Vulkan Init lands (plugins receive the core's forwarded calls).
  if (is_core && init_hooked_core == nullptr) {
    for (const detour_guard::Hook& hook : kInitHooks) {
      *hook.real = reinterpret_cast<void*>(GetProcAddress(module, hook.name));
    }
    size_t init_failed = 0;
    if (detour_guard::AttachAll(kInitHooks, &init_failed) != 0) {
      init_hooked_core = module;
      init_hooked_image = detour_guard::ImageIdentity(module);
    }
  }
  return true;
}

// Called from InstallHooks' phase 2 for every NGX candidate copy.
template <typename Candidates>
inline void HookModules(const Candidates& candidates) {
  SweepNgxSlots(kSlots);
  bool hooked = false;
  for (const auto& candidate : candidates) {
    if (TryHookModule(candidate.module, candidate.log_path, candidate.is_core)) hooked = true;
  }
  if (hooked && !ngx_any_hooked.exchange(true, std::memory_order_relaxed)) {
    int modules = 0;
    for (int s = 0; s < kMaxNgxSlots; ++s) if (slot_used[s]) ++modules;
    Log(reshade::log::level::info,
        "Vulkan NGX hooks installed across " + std::to_string(modules)
            + " module(s); the game's Vulkan DLSS evaluates are observed");
  }
}

// UnhookInstalledDetours' Vulkan half.  Fully re-armable.
inline void Unhook() {
  if (context_hooks_installed) {
    for (const detour_guard::Hook& hook : kContextHooks) {
      NoteDetourRemoval(detour_guard::Detach(hook));
    }
    context_hooks_installed = false;
  }
  {
    std::lock_guard lock(context_mutex);
    instances_by_dispatch.clear();
    device_contexts.clear();
  }
  {
    std::lock_guard install(barrier_install_mutex);
    layout_observation_complete.store(false, std::memory_order_release);
    for (const detour_guard::Hook& hook : kBarrierHooks) {
      if (hook.record->target != nullptr) NoteDetourRemoval(detour_guard::Detach(hook));
    }
    native_barrier.store(nullptr, std::memory_order_release);
    barrier_observers_resolved.store(0, std::memory_order_relaxed);
    barrier_observers_attached.store(0, std::memory_order_relaxed);
    barrier_install_warned = false;
    barrier_install_attempted.store(false, std::memory_order_release);
  }
  for (int s = 0; s < kMaxNgxSlots; ++s) {
    if (slot_used[s]) ReleaseNgxSlot(kSlots, s);
  }
  if (init_hooked_core != nullptr) {
    const bool same_image = detour_guard::ImageIdentity(init_hooked_core) == init_hooked_image;
    for (const detour_guard::Hook& hook : kInitHooks) {
      if (same_image) {
        NoteDetourRemoval(detour_guard::Detach(hook));
      } else {
        detour_guard::Forget(hook);
      }
    }
    init_hooked_core = nullptr;
  }
  failed_modules.clear();
  absent_modules.clear();
  refused_modules.clear();
  ngx_any_hooked.store(false, std::memory_order_relaxed);
}

inline void OnInitDevice(reshade::api::device* device) {
  {
    std::lock_guard lock(context_mutex);
    api_devices[reinterpret_cast<VkDevice>(device->get_native())] = device;
  }
  if (!command_lists_registered.exchange(true)) {
    reshade::register_event<reshade::addon_event::reset_command_list>(OnResetCommandList);
  }
  if (!image_events_registered.exchange(true)) {
    reshade::register_event<reshade::addon_event::begin_render_pass>(OnBeginTrackedRenderPass);
    reshade::register_event<reshade::addon_event::end_render_pass>(OnEndTrackedRenderPass);
    reshade::register_event<reshade::addon_event::execute_secondary_command_list>(
        OnExecuteSecondaryImageStates);
  }
  // The compute bind-point shadow (RestoreComputeState), only while the
  // restore is on and only once a Vulkan device exists: a D3D12 game's
  // binds never reach these callbacks.
  if (state_restore.load(std::memory_order_relaxed) != 0
      && !state_events_registered.exchange(true)) {
    reshade::register_event<reshade::addon_event::init_pipeline_layout>(OnInitPipelineLayout);
    reshade::register_event<reshade::addon_event::destroy_pipeline_layout>(
        OnDestroyPipelineLayout);
    reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
    reshade::register_event<reshade::addon_event::bind_descriptor_tables>(
        OnBindDescriptorTables);
    reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
    reshade::register_event<reshade::addon_event::push_constants>(OnPushConstants);
  }
}

inline void UnregisterStateEvents() {
  if (image_events_registered.exchange(false)) {
    reshade::unregister_event<reshade::addon_event::begin_render_pass>(OnBeginTrackedRenderPass);
    reshade::unregister_event<reshade::addon_event::end_render_pass>(OnEndTrackedRenderPass);
    reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(
        OnExecuteSecondaryImageStates);
  }
  if (!state_events_registered.exchange(false)) return;
  reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(OnInitPipelineLayout);
  reshade::unregister_event<reshade::addon_event::destroy_pipeline_layout>(
      OnDestroyPipelineLayout);
  reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
  reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(
      OnBindDescriptorTables);
  reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
  reshade::unregister_event<reshade::addon_event::push_constants>(OnPushConstants);
}

// The telemetry group, stated only in a session whose Vulkan exports were
// ever detoured, so every other session's line stays byte-identical.
inline std::string Telemetry() {
  if (!ngx_ever_hooked.load(std::memory_order_relaxed)) return {};
  std::ostringstream out;
  out << " vk[creates="
      << entered[kCreate].load(std::memory_order_relaxed)
             + entered[kCreate1].load(std::memory_order_relaxed)
      << " evals=" << entered[kEvaluate].load(std::memory_order_relaxed)
      << " releases=" << entered[kRelease].load(std::memory_order_relaxed)
      << " inits=" << entered[kInit].load(std::memory_order_relaxed)
      << " dlss=" << dlss_evaluates.load(std::memory_order_relaxed)
      << " owner_mismatch=" << owner_mismatches.load(std::memory_order_relaxed)
      << " nr_evals=" << nr_evaluates.load(std::memory_order_relaxed)
      << " nr_passes=" << nr_passes.load(std::memory_order_relaxed)
      << " stage_protocol=1"
      << " restores=" << restores.load(std::memory_order_relaxed)
      << " restore_partial=" << restores_partial.load(std::memory_order_relaxed)
      << " layout_hooks=" << barrier_observers_attached.load(std::memory_order_relaxed)
      << "/" << barrier_observers_resolved.load(std::memory_order_relaxed)
      << " sampled_layout_restores=" << sampled_layout_restores.load(std::memory_order_relaxed)
      << " divisor=" << divisor_last.load(std::memory_order_relaxed)
      << " divisor_reads=" << divisor_reads.load(std::memory_order_relaxed)
      << " divisor_lost=" << divisor_lost.load(std::memory_order_relaxed)
      << " context=" << (game_instance.load(std::memory_order_relaxed) != nullptr ? 1 : 0)
      << "]";
  return out.str();
}

}  // namespace vk

#endif
