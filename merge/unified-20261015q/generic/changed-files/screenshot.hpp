/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: MIT
 */

// One-shot NR screenshot pair of ONE final presentation image, copied before
// and after its NR pass. Inline DLSS textures are not display screenshots:
// the game's downstream exposure, tone map and UI cannot be reconstructed
// from them. The present path temporarily supplies the comparison for F5.
// A separate diagnostic mode copies an evaluated model input/output pair;
// those files explicitly exclude the game's later processing and display UI.
//
// Design contract (v2.7 rewrite): the feature must be inert while idle. The
// present path never touches D3D12 objects, user32 (beyond the one hotkey
// poll), std::filesystem, or the heap on behalf of this feature until the user
// arms it. Concretely:
//   - No QueryInterface and no command queue anywhere in this file.  GPU
//     completion is proven by the submission tracker (submission_tracker.hpp):
//     every copy is a tracked use of its readback on the list that records
//     it, and a capture settles once no recording holding one of its
//     readbacks can still execute (rc11, TIME-01/03).  A present proves
//     nothing: a generated frame presents between the evaluate and its
//     submit, and an unrelated list completes first.  Only a session without
//     the queue hook falls back to a fence-completed lease (gpu_lease.hpp)
//     acquired once the capture frame has presented - a poll of
//     ID3D12Fence::GetCompletedValue, never a CPU wait - and to the
//     present-generation delay (kSettlePresents) for queues without a
//     tracked fence.
//   - Every readback is stamped at creation (kUnwrittenStamp): a stamp still
//     there when the worker reads it is a copy the GPU never ran, and the
//     capture is refused (counted) instead of written as a black pair.
//   - The render/evaluate thread only creates two addon-owned READBACK heap
//     buffers (Prepare stage). No filesystem calls there either.
//   - Readback mapping, display coding, PNG/JPEG encoding, and file IO run on a
//     dedicated worker thread (created lazily on the first hand-off). The
//     present thread only moves a finished capture into the worker's job slot.
//   - runtime_mutex (dlssnr.hpp) is never held across any of that work. This
//     module has its own state_mutex; the only legal ordering is
//     runtime_mutex -> state_mutex -> job_mutex (state_mutex -> the
//     submission tracker's mutex for TrackUse and ReadbacksReleasable), and
//     the worker takes job_mutex alone.
//   - The worker is detached and never joined: Shutdown runs under the DLL
//     loader lock (DllMain PROCESS_DETACH), where joining any running thread
//     deadlocks. Shutdown drains owned state and raises shutdown_requested;
//     the worker exits on its own and the process reclaims it at exit.
//
// Compatibility notes (v4.1.5): the files carry no alpha - HDR intermediates
// routinely carry alpha 0/NaN, and an alpha-respecting viewer composites
// those to a fully black image.  The pair may be cropped to
// the engine-declared output subrect when the DLSS output resource is larger
// than what the engine actually renders (black-margin titles).

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <d3d12.h>

#include <include/reshade.hpp>

#include "../../utils/path.hpp"
#include "../../utils/png.hpp"
#include "gpu_lease.hpp"
#include "submission_tracker.hpp"
#include "../../../../common/event-log.hpp"

namespace renodx::addons::dlss5::screenshot {

enum class SourceMode : uint32_t { kAuto = 0, kFinalPresent = 1, kEvaluatedBuffer = 2 };
inline std::atomic_uint32_t source_mode = 0;
// Set only by an explicit producer contract identifying a separate host
// presentation window. An absent/unknown contract keeps the final frame.
inline std::atomic_bool separate_presentation = false;

// Output files (dlssnr.hpp persists them; frozen when a request arms).
// NRScreenshotFormat: 0 PNG, 1 JPEG, 2 both.  NRScreenshotLayout: 0 the
// pair, 1 the pair plus one side-by-side file (NR off | NR on) - one upload.
// NRScreenshotJpegQuality 80-100; NRScreenshotMaxMB caps each JPEG
// (0 = no cap; 10 is Discord's upload limit without Nitro).
enum class FileFormat : uint32_t { kPng = 0, kJpeg = 1, kPngAndJpeg = 2 };
inline std::atomic_uint32_t file_format = 0;
inline std::atomic_uint32_t layout = 0;
inline std::atomic_uint32_t jpeg_quality = 95;
inline std::atomic_uint32_t max_megabytes = 10;

inline constexpr bool HasSeparatePresentationContract(
    uint32_t version, uint32_t provider, uint32_t host, uint32_t cadence) {
  return version == 1u && provider == 0x444C3546u && host == 1u && cadence == 1u;
}

inline constexpr SourceMode ResolveSource(uint32_t mode, bool separate) {
  return mode == static_cast<uint32_t>(SourceMode::kEvaluatedBuffer)
                 || (mode == static_cast<uint32_t>(SourceMode::kAuto) && separate)
      ? SourceMode::kEvaluatedBuffer : SourceMode::kFinalPresent;
}

namespace internal {

// Fallback for queues without a tracked completion fence: presents that must
// pass after the copies were recorded before the readback buffers are
// mapped. Matches the retire-settle window the resource-release path uses;
// with fences the lease replaces this age rule.
constexpr uint64_t kSettlePresents = 4;
// Presents a capture with exact proofs waits for them before it gives up on
// delivery.  The proof needs the recording list submitted, completed AND
// reset, and an engine resets a list only when its allocator comes round
// again (2-3 frames, 4 presents each under 4x frame generation).  Giving up
// cancels the files only: the buffers stay retained until their own proof.
constexpr uint64_t kExactSettlePresents = 240;
// Written into every readback at creation; the copy overwrites it (its first
// row starts at offset 0).  Still there when the worker reads the buffer =
// the GPU never ran the copy.  As float data these are NaN payloads; as
// 8-bit data, four exact pixel values in sequence.
constexpr uint32_t kUnwrittenStamp[4] = {0x7FC0DEADu, 0x7FC0BEEFu, 0x7FC0CAFEu, 0x7FC0F00Du};
// Presents an armed capture may wait without a single DLSS evaluation before
// it disarms itself (~10 s at 60 fps).  Without this, F5 pressed during a
// loading screen or with NR off fires much later on the first evaluate -
// typically a black frame the user cannot make sense of.
constexpr uint32_t kArmTimeoutPresents = 600;
constexpr char kLogPrefix[] = "DLSS5 Generic";

// Raw float32 RGBA diagnostic planes captured alongside the PNG pair:
// the sRGB proxy the NR model actually sees, and the resolved linear work
// surface before the commit.  PNGs cannot establish exposure/units; these
// dumps (plus the sidecar meta) can.
struct DiagnosticPlane {
  std::string name;
  ID3D12Resource* readback = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 total_bytes = 0;
};

struct OutputOptions {
  FileFormat format = FileFormat::kPng;
  bool side_by_side = false;
  uint32_t jpeg_quality = 95;
  uint32_t max_megabytes = 10;
};

struct PendingCapture {
  bool model_diagnostic = false;
  OutputOptions output;
  ID3D12Resource* pre_nr_readback = nullptr;
  ID3D12Resource* nr_output_readback = nullptr;
  std::vector<DiagnosticPlane> planes;
  std::string meta;  // sidecar .meta.txt content (divisor, encoding, meter)
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 total_bytes = 0;
  UINT row_pitch = 0;
  // Captured subrect: engines can render into a subrect of a larger DLSS
  // output resource; width/height are the CROP dimensions written to the PNG,
  // while the footprint/row_pitch still describe the full resource rows.
  uint32_t crop_x = 0;
  uint32_t crop_y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  uint8_t hdr_mode = 0;  // 0 = SDR, 1 = linear HDR, 2 = PQ HDR
  float linear_unit_nits = 80.f;  // scRGB: 1.0 is 80 nits, never scene exposure
  uint64_t timestamp_ms = 0;  // filename stem components; paths are only built
  uint64_t serial = 0;        // on the worker so Prepare never touches the fs
  uint64_t creation_generation = 0;  // present_generation when recorded
  // The proof is chosen once, at the capture frame's first present
  // (`lease_taken`, stamped `lease_generation`): `exact` when the queue hook
  // is live, which settles on the tracker alone; otherwise the lease, taken
  // then and empty when no queue fence exists, and a fence that never
  // advances (present-starved session) settles on the bounded fallback.
  GpuLease lease;
  uint64_t lease_generation = 0;
  bool lease_taken = false;
  bool exact = false;
  bool ready = false;                // both copies have been recorded
  bool aborted = false;
};

// Counted degraded outcomes (telemetry screenshot[...]): captures cancelled
// on a timeout, captures refused because a copy never ran, and buffer sets
// retained for want of a proof (superseded, cancelled or refused).
inline std::atomic_uint64_t captures_cancelled = 0;
inline std::atomic_uint64_t captures_unexecuted = 0;
inline std::atomic_uint64_t retained_sets = 0;

inline std::atomic_bool capture_requested = false;
inline std::atomic<SourceMode> requested_source = SourceMode::kFinalPresent;
inline std::atomic_uint32_t capture_serial = 0;
inline std::atomic_bool shutdown_requested = false;
inline std::atomic_bool hotkey_was_down = false;
// Presents counted while armed without any evaluate having recorded a pair.
inline std::atomic_uint32_t presents_while_armed = 0;
inline std::atomic_bool requires_passthrough_frame = false;
inline std::atomic_uint64_t request_id = 0;
// The file settings as they stood when the request armed; state_mutex.
inline OutputOptions requested_output;

// pending is guarded by state_mutex; has_pending mirrors pending.has_value()
// so the idle present path stays a single atomic load.
inline std::mutex state_mutex;
inline std::optional<PendingCapture> pending;
inline std::atomic_bool has_pending = false;

// The last capture that ended, however it ended, and the files it wrote: the
// issue report (report_flow.hpp) bundles exactly these and states why a
// capture is missing.  `outcome`: "written", "write_failed", "aborted",
// "not_ready", "gpu_unconfirmed" (a settle timeout cancelled it), "exception".
// finished_serial is the serial of `finished` and rises monotonically.
// Lock order: state_mutex -> finished_mutex; readers take finished_mutex alone.
struct FinishedCapture {
  uint64_t serial = 0;
  const char* outcome = "none";
  bool model_diagnostic = false;
  // The pair's lossless files when PNG was written, else its JPEGs.
  std::filesystem::path pre_image, post_image, meta;  // empty when not written
};
inline std::mutex finished_mutex;
inline FinishedCapture finished;
inline std::atomic_uint64_t finished_serial = 0;

inline void PublishFinished(FinishedCapture record) noexcept {
  try {
    std::scoped_lock lock(finished_mutex);
    const uint64_t serial = record.serial;
    if (serial < finished.serial) return;
    finished = std::move(record);
    finished_serial.store(serial, std::memory_order_release);
  } catch (...) {
  }
}

// Buffers from evaluates that were superseded within the same capture-frame
// (the capture re-records on every DLSS-family evaluate so the LAST one wins).
// They share the frame's command list, so they are released at the same settle
// window as the final capture rather than immediately.  This owns the ENTIRE
// superseded buffer set - the PNG pair AND every diagnostic plane: replacing
// `pending` used to destroy the old planes vector without releasing its raw
// COM readback pointers, leaking every re-recorded plane (a 4K FP16 plane is
// ~63 MiB; v6 audit issue 13).
struct RetiredCaptureBuffers {
  ID3D12Resource* pre_nr_readback = nullptr;
  ID3D12Resource* nr_output_readback = nullptr;
  std::vector<DiagnosticPlane> planes;

  void Release() {
    if (pre_nr_readback != nullptr) pre_nr_readback->Release();
    if (nr_output_readback != nullptr) nr_output_readback->Release();
    for (DiagnosticPlane& plane : planes) {
      if (plane.readback != nullptr) plane.readback->Release();
    }
    pre_nr_readback = nullptr;
    nr_output_readback = nullptr;
    planes.clear();
  }

  // Takes ownership of a cancelled PendingCapture's raw buffers for
  // retention (unproven GPU completion: release happens on the set's own
  // tracker proof, or without the queue hook behind the next proven settle,
  // never on a timeout).
  void Take(PendingCapture&& capture) {
    pre_nr_readback = capture.pre_nr_readback;
    nr_output_readback = capture.nr_output_readback;
    planes = std::move(capture.planes);
    capture.pre_nr_readback = nullptr;
    capture.nr_output_readback = nullptr;
    capture.planes.clear();
  }
};
inline std::vector<RetiredCaptureBuffers> retired_buffers;
// Refused captures of a session without the queue hook: the GPU may still
// run their copies, so they are never released (worker-owned).
inline std::vector<RetiredCaptureBuffers> held_unexecuted;

// True when no recording that copies into one of the set's readbacks can
// still execute or replay (a PendingCapture or a RetiredCaptureBuffers).
template <typename Buffers>
inline bool ReadbacksReleasable(const Buffers& buffers) {
  if (!submission::ResourceReleasable(buffers.pre_nr_readback)
      || !submission::ResourceReleasable(buffers.nr_output_readback)) {
    return false;
  }
  for (const DiagnosticPlane& plane : buffers.planes) {
    if (!submission::ResourceReleasable(plane.readback)) return false;
  }
  return true;
}

// Worker hand-off slot. Guarded by job_mutex; the worker moves the job out
// under the lock and processes it outside.
inline std::mutex job_mutex;
inline std::condition_variable job_signal;
inline std::optional<PendingCapture> job;
inline std::atomic_bool worker_started = false;

inline void SafeLog(reshade::log::level level, const std::string& message) {
  if (shutdown_requested.load(std::memory_order_acquire)) return;
  const std::string line = std::string(kLogPrefix) + ": " + message;
  reshade::log::message(level, line.c_str());
  ::dlss5::eventlog::Forward(::dlss5::eventlog::kGeneric, static_cast<int>(level), line.c_str());
}

// std::filesystem::path::string() throws for characters the ANSI code page
// cannot represent; logging must never throw.
inline std::string PathToLogString(const std::filesystem::path& path) {
  try {
    return path.string();
  } catch (...) {
  }
  try {
    const std::wstring wide = path.wstring();
    std::string lossy;
    lossy.reserve(wide.size());
    for (wchar_t c : wide) lossy.push_back(static_cast<char>(c));
    return lossy;
  } catch (...) {
    return "<unprintable path>";
  }
}

inline void ReleaseReadbacks(PendingCapture& capture) {
  if (capture.pre_nr_readback != nullptr) capture.pre_nr_readback->Release();
  if (capture.nr_output_readback != nullptr) capture.nr_output_readback->Release();
  capture.pre_nr_readback = nullptr;
  capture.nr_output_readback = nullptr;
  for (DiagnosticPlane& plane : capture.planes) {
    if (plane.readback != nullptr) plane.readback->Release();
  }
  capture.planes.clear();
}

// Shared readback-buffer allocator for the PNG pair and the float planes.
inline bool MakeReadbackBuffer(
    ID3D12Device* device,
    UINT64 bytes,
    ID3D12Resource** result) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = bytes;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr, IID_PPV_ARGS(result)))) {
    return false;
  }
  // kUnwrittenStamp.  A READBACK buffer maps for CPU writes too; nothing has
  // been recorded against it yet.
  const D3D12_RANGE nothing_read{0, 0};
  void* mapped = nullptr;
  if (SUCCEEDED((*result)->Map(0, &nothing_read, &mapped)) && mapped != nullptr) {
    const D3D12_RANGE written{
        0, static_cast<SIZE_T>(std::min<UINT64>(sizeof(kUnwrittenStamp), bytes))};
    std::memcpy(mapped, kUnwrittenStamp, written.End);
    (*result)->Unmap(0, &written);
  }
  return true;
}

// The stamp is intact: the copy into this mapped readback never ran.
inline bool Unwritten(const void* mapped, UINT64 bytes) {
  return std::memcmp(mapped, kUnwrittenStamp,
                     static_cast<size_t>(std::min<UINT64>(sizeof(kUnwrittenStamp), bytes)))
         == 0;
}

// ---------------------------------------------------------------------------
// PNG byte plumbing: chunks and a stored-block zlib stream.  WIC compresses
// the image data (EncodeImageFiles); these write the chunks this module owns
// byte for byte, the iCCP profile, and the fallback PNG when WIC is absent.

inline void AppendPngBe32(std::vector<uint8_t>& bytes, uint32_t value) {
  bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
  bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  bytes.push_back(static_cast<uint8_t>(value & 0xff));
}

inline uint32_t ReadBe32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16)
         | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

inline uint32_t PngCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (-(crc & 1u)));
  }
  return ~crc;
}

inline uint32_t PngAdler32(const uint8_t* data, size_t size) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

inline void AppendPngChunk(
    std::vector<uint8_t>& png,
    const char type[4],
    const std::vector<uint8_t>& payload) {
  AppendPngBe32(png, static_cast<uint32_t>(payload.size()));
  const size_t type_offset = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), payload.begin(), payload.end());
  AppendPngBe32(png, PngCrc32(png.data() + type_offset, 4 + payload.size()));
}

inline std::vector<uint8_t> ZlibStored(const uint8_t* data, size_t size) {
  std::vector<uint8_t> zlib = {0x78, 0x01};
  zlib.reserve(size + size / 65535 * 5 + 16);
  size_t offset = 0;
  do {
    const size_t block_size = std::min<size_t>(65535, size - offset);
    zlib.push_back(offset + block_size == size ? 1u : 0u);
    const uint16_t length = static_cast<uint16_t>(block_size);
    const uint16_t inverse = static_cast<uint16_t>(~length);
    zlib.push_back(static_cast<uint8_t>(length & 0xff));
    zlib.push_back(static_cast<uint8_t>(length >> 8));
    zlib.push_back(static_cast<uint8_t>(inverse & 0xff));
    zlib.push_back(static_cast<uint8_t>(inverse >> 8));
    zlib.insert(zlib.end(), data + offset, data + offset + block_size);
    offset += block_size;
  } while (offset < size);
  AppendPngBe32(zlib, PngAdler32(data, size));
  return zlib;
}

// ---------------------------------------------------------------------------
// Readback decoding.

inline float HalfToFloat(uint16_t bits) {
  const uint32_t sign = (bits & 0x8000u) << 16;
  const uint32_t exponent = (bits >> 10) & 0x1fu;
  const uint32_t mantissa = bits & 0x3ffu;
  uint32_t value = 0;
  if (exponent == 0) {
    if (mantissa != 0) {
      float result = std::ldexp(static_cast<float>(mantissa), -24);
      return (sign != 0 ? -result : result);
    }
    value = sign;
  } else if (exponent == 31) {
    value = sign | 0x7f800000u | (mantissa << 13);
  } else {
    value = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
  }
  float result = 0.f;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

inline float DecodeUnsignedFloat(uint32_t bits, uint32_t mantissa_bits) {
  const uint32_t mantissa_mask = (1u << mantissa_bits) - 1u;
  const uint32_t mantissa = bits & mantissa_mask;
  const uint32_t exponent = (bits >> mantissa_bits) & 0x1fu;
  if (exponent == 0) {
    return std::ldexp(static_cast<float>(mantissa), -14 - static_cast<int>(mantissa_bits));
  }
  if (exponent == 31) return std::numeric_limits<float>::infinity();
  return std::ldexp(
      1.f + static_cast<float>(mantissa) / static_cast<float>(1u << mantissa_bits),
      static_cast<int>(exponent) - 15);
}

// ---------------------------------------------------------------------------
// Display encoding. SDR is already sRGB-coded, independent of storage type.
// HDR10 is already PQ/BT.2020 and is preserved, not decoded through an SDR
// approximation. scRGB is BT.709 linear at 80 nits per unit; convert primaries
// in linear light, then encode absolute nits to PQ. Do not normalize a capture
// by its maximum: doing so gives the two halves different exposure.
inline float LinearNitsToPq(float nits) {
  const float p = std::pow(std::clamp(nits / 10000.f, 0.f, 1.f), 2610.f / 16384.f);
  return std::pow((3424.f / 4096.f + (2413.f / 128.f) * p)
                      / (1.f + (2392.f / 128.f) * p),
                  2523.f / 32.f);
}

inline float PqToNits(float code) {
  const float e = std::pow(std::clamp(code, 0.f, 1.f), 32.f / 2523.f);
  return 10000.f
         * std::pow(std::max(e - 3424.f / 4096.f, 0.f)
                        / (2413.f / 128.f - (2392.f / 128.f) * e),
                    16384.f / 2610.f);
}

// Display-coded RGB, 16 bits a channel, in the swapchain's own signal: sRGB
// codes for SDR, PQ BT.2020 codes for HDR.  Every file derives from it, so
// PNG and JPEG carry the same exposure by construction.
struct CodedImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint16_t> rgb;  // row-major R, G, B
};

// Swapchain formats whose codes are exact at 8 bits: their 8-bit files need
// no dither.
inline bool EightBitFormat(DXGI_FORMAT format) {
  return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
         || format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

inline bool ReadbackToCoded(
    const PendingCapture& capture,
    ID3D12Resource* readback,
    CodedImage* image,
    bool* any_nonzero,
    bool* unwritten) {
  if (readback == nullptr || capture.width == 0 || capture.height == 0
      || capture.row_pitch == 0)
    return false;
  uint32_t bytes_per_pixel = 0;
  switch (capture.format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R11G11B10_FLOAT:
      bytes_per_pixel = 4;
      break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      bytes_per_pixel = 8;
      break;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
      bytes_per_pixel = 16;
      break;
    default:
      return false;
  }
  D3D12_RANGE range{0, static_cast<SIZE_T>(capture.total_bytes)};
  void* mapped = nullptr;
  if (FAILED(readback->Map(0, &range, &mapped)) || mapped == nullptr) return false;
  *unwritten = Unwritten(mapped, capture.total_bytes);
  if (*unwritten) {
    readback->Unmap(0, nullptr);
    return false;
  }

  bool saw_nonzero = false;
  image->width = capture.width;
  image->height = capture.height;
  image->rgb.assign(static_cast<size_t>(capture.width) * capture.height * 3u, 0);
  // Rows advance by the full-resource row pitch; the crop base lands once.
  const uint8_t* base = static_cast<const uint8_t*>(mapped)
      + static_cast<size_t>(capture.crop_y) * capture.row_pitch
      + static_cast<size_t>(capture.crop_x) * bytes_per_pixel;
  for (uint32_t y = 0; y < capture.height; ++y) {
    const uint8_t* row = base + static_cast<size_t>(y) * capture.row_pitch;
    for (uint32_t x = 0; x < capture.width; ++x) {
      float r = 0.f, g = 0.f, b = 0.f;
      switch (capture.format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
          r = row[x * 4u] / 255.f; g = row[x * 4u + 1] / 255.f;
          b = row[x * 4u + 2] / 255.f;
          break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
          b = row[x * 4u] / 255.f; g = row[x * 4u + 1] / 255.f;
          r = row[x * 4u + 2] / 255.f;
          break;
        case DXGI_FORMAT_R10G10B10A2_UNORM: {
          const uint32_t v = *reinterpret_cast<const uint32_t*>(row + x * 4u);
          r = (v & 0x3ffu) / 1023.f; g = ((v >> 10) & 0x3ffu) / 1023.f;
          b = ((v >> 20) & 0x3ffu) / 1023.f;
          break;
        }
        case DXGI_FORMAT_R11G11B10_FLOAT: {
          const uint32_t v = *reinterpret_cast<const uint32_t*>(row + x * 4u);
          r = DecodeUnsignedFloat(v & 0x7ffu, 6);
          g = DecodeUnsignedFloat((v >> 11) & 0x7ffu, 6);
          b = DecodeUnsignedFloat((v >> 22) & 0x3ffu, 5);
          break;
        }
        case DXGI_FORMAT_R16G16B16A16_FLOAT: {
          const auto* q = reinterpret_cast<const uint16_t*>(row + x * 8u);
          r = HalfToFloat(q[0]); g = HalfToFloat(q[1]);
          b = HalfToFloat(q[2]);
          break;
        }
        case DXGI_FORMAT_R32G32B32A32_FLOAT: {
          const auto* q = reinterpret_cast<const float*>(row + x * 16u);
          r = q[0]; g = q[1]; b = q[2];
          break;
        }
        default:
          readback->Unmap(0, nullptr);
          image->rgb.clear();
          return false;
      }

      if (!std::isfinite(r)) r = 0.f;
      if (!std::isfinite(g)) g = 0.f;
      if (!std::isfinite(b)) b = 0.f;
      if (capture.hdr_mode == 1) {
        // Preserve scRGB's negative wide-gamut channels until after the
        // BT.709 -> BT.2020 matrix. Clamp only at the PQ output boundary.
        const float r2020 = 0.627404f * r + 0.329283f * g + 0.043313f * b;
        const float g2020 = 0.069097f * r + 0.919540f * g + 0.011362f * b;
        const float b2020 = 0.016391f * r + 0.088013f * g + 0.895595f * b;
        r = LinearNitsToPq(r2020 * capture.linear_unit_nits);
        g = LinearNitsToPq(g2020 * capture.linear_unit_nits);
        b = LinearNitsToPq(b2020 * capture.linear_unit_nits);
      }
      // Game alpha does not describe screenshot transparency: no alpha.
      uint16_t* dst = image->rgb.data()
          + (static_cast<size_t>(y) * capture.width + x) * 3u;
      dst[0] = static_cast<uint16_t>(std::lround(std::clamp(r, 0.f, 1.f) * 65535.f));
      dst[1] = static_cast<uint16_t>(std::lround(std::clamp(g, 0.f, 1.f) * 65535.f));
      dst[2] = static_cast<uint16_t>(std::lround(std::clamp(b, 0.f, 1.f) * 65535.f));
      // Nonzero at 8 bits, the precision an SDR file keeps.
      if (dst[0] >= 129 || dst[1] >= 129 || dst[2] >= 129) saw_nonzero = true;
    }
  }
  readback->Unmap(0, nullptr);
  if (any_nonzero != nullptr) *any_nonzero = saw_nonzero;
  return true;
}

// ---------------------------------------------------------------------------
// HDR signaling for sharing.  Discord shows an upload through an 8-bit WebP
// preview that keeps an ICC profile but has no native CICP, and Chromium
// (Discord's client and every browser that opens the original) reads an ICC
// cicp tag as the HDR signal (Chromium issue 40239687).  So the HDR files
// carry the same PQ BT.2020 codes the game presented, plus this profile.
//
// ICC v4.4 display profile.  cicp = 9/16/0/1: BT.2020 primaries, ST 2084,
// RGB, full range - the same tuple as the PNG cICP chunk.  A viewer that
// ignores cicp uses the matrix/TRC: BT.2020 primaries (Bradford-adapted to
// the D50 PCS) and PQ read against a 203-nit reference white with a soft
// roll-off above 75 %.  That fallback only describes the pixels for an SDR
// viewer; the pixels themselves are never tone mapped.
inline const std::vector<uint8_t>& Bt2100PqIcc() {
  static const std::vector<uint8_t> kProfile = [] {
    const auto s15 = [](std::vector<uint8_t>& out, double value) {
      AppendPngBe32(out, static_cast<uint32_t>(static_cast<int32_t>(std::lround(value * 65536.0))));
    };
    const auto signature = [](const char* name) {
      return ReadBe32(reinterpret_cast<const uint8_t*>(name));
    };
    const auto element = [&](const char* type) {
      std::vector<uint8_t> out;
      AppendPngBe32(out, signature(type));
      AppendPngBe32(out, 0);
      return out;
    };
    const auto text = [&](const char* ascii) {
      std::vector<uint8_t> out = element("mluc");
      const size_t length = std::strlen(ascii);
      AppendPngBe32(out, 1);   // one record
      AppendPngBe32(out, 12);  // record size
      out.insert(out.end(), {'e', 'n', 'U', 'S'});
      AppendPngBe32(out, static_cast<uint32_t>(length * 2));
      AppendPngBe32(out, 28);  // string offset from the element start
      for (size_t i = 0; i < length; ++i) {
        out.push_back(0);
        out.push_back(static_cast<uint8_t>(ascii[i]));
      }
      return out;
    };
    const auto xyz = [&](double x, double y, double z) {
      std::vector<uint8_t> out = element("XYZ ");
      s15(out, x);
      s15(out, y);
      s15(out, z);
      return out;
    };
    // Bradford D65 -> D50, and BT.2020 RGB -> XYZ (D65).
    constexpr double kBradford[3][3] = {{1.0478112, 0.0228866, -0.0501270},
                                        {0.0295424, 0.9904844, -0.0170491},
                                        {-0.0092345, 0.0150436, 0.7521316}};
    constexpr double kBt2020ToXyz[3][3] = {{0.6369580, 0.1446169, 0.1688810},
                                           {0.2627002, 0.6779981, 0.0593017},
                                           {0.0000000, 0.0280727, 1.0609851}};
    double adapted[3][3] = {};
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        for (int k = 0; k < 3; ++k) adapted[i][j] += kBradford[i][k] * kBt2020ToXyz[k][j];
      }
    }
    std::vector<uint8_t> chad = element("sf32");
    for (const auto& row : kBradford) {
      for (const double value : row) s15(chad, value);
    }
    std::vector<uint8_t> curve = element("curv");
    constexpr uint32_t kCurvePoints = 1024;
    AppendPngBe32(curve, kCurvePoints);
    for (uint32_t i = 0; i < kCurvePoints; ++i) {
      const double relative =
          PqToNits(static_cast<float>(i) / (kCurvePoints - 1)) / 203.0;
      const double shown = relative <= 0.75
          ? relative : 0.75 + 0.25 * (1.0 - std::exp(-(relative - 0.75) / 0.25));
      const auto code = static_cast<uint16_t>(std::lround(std::clamp(shown, 0.0, 1.0) * 65535.0));
      curve.push_back(static_cast<uint8_t>(code >> 8));
      curve.push_back(static_cast<uint8_t>(code & 0xff));
    }
    std::vector<uint8_t> cicp = element("cicp");
    cicp.insert(cicp.end(), {9, 16, 0, 1});
    const std::vector<uint8_t> desc = text("BT.2100 PQ (RenoDX DLSS5 screenshot)");
    const std::vector<uint8_t> cprt = text("No copyright, use freely");
    const std::vector<uint8_t> wtpt = xyz(0.9642, 1.0, 0.8249);
    const std::vector<uint8_t> red = xyz(adapted[0][0], adapted[1][0], adapted[2][0]);
    const std::vector<uint8_t> green = xyz(adapted[0][1], adapted[1][1], adapted[2][1]);
    const std::vector<uint8_t> blue = xyz(adapted[0][2], adapted[1][2], adapted[2][2]);
    const std::pair<const char*, const std::vector<uint8_t>*> tags[] = {
        {"desc", &desc}, {"cprt", &cprt}, {"wtpt", &wtpt},   {"chad", &chad},
        {"rXYZ", &red},  {"gXYZ", &green}, {"bXYZ", &blue},  {"rTRC", &curve},
        {"gTRC", &curve}, {"bTRC", &curve}, {"cicp", &cicp}};

    std::vector<uint8_t> profile(128, 0);
    profile[8] = 0x04;  // version 4.4
    profile[9] = 0x40;
    std::memcpy(&profile[12], "mntr", 4);
    std::memcpy(&profile[16], "RGB ", 4);
    std::memcpy(&profile[20], "XYZ ", 4);
    const uint8_t created[12] = {0x07, 0xEA, 0, 9, 0, 26, 0, 0, 0, 0, 0, 0};
    std::memcpy(&profile[24], created, sizeof(created));
    std::memcpy(&profile[36], "acsp", 4);
    const uint8_t d50[12] = {0, 0, 0xF6, 0xD6, 0, 1, 0, 0, 0, 0, 0xD3, 0x2D};
    std::memcpy(&profile[68], d50, sizeof(d50));
    AppendPngBe32(profile, static_cast<uint32_t>(std::size(tags)));
    const size_t data_start = profile.size() + 12 * std::size(tags);
    std::vector<uint8_t> data;
    const std::vector<uint8_t>* previous = nullptr;
    uint32_t offset = 0;
    for (const auto& [name, element_data] : tags) {
      if (element_data != previous) {  // the three TRCs share one element
        while (data.size() % 4 != 0) data.push_back(0);
        offset = static_cast<uint32_t>(data_start + data.size());
        data.insert(data.end(), element_data->begin(), element_data->end());
        previous = element_data;
      }
      AppendPngBe32(profile, signature(name));
      AppendPngBe32(profile, offset);
      AppendPngBe32(profile, static_cast<uint32_t>(element_data->size()));
    }
    while (data.size() % 4 != 0) data.push_back(0);
    profile.insert(profile.end(), data.begin(), data.end());
    const auto size = static_cast<uint32_t>(profile.size());
    for (int i = 0; i < 4; ++i) profile[i] = static_cast<uint8_t>(size >> (24 - 8 * i));
    return profile;
  }();
  return kProfile;
}

// CTA-861.3 MaxCLL and MaxFALL of one PQ image, in nits: the brightest
// pixel's largest channel, and the frame average of each pixel's largest.
// Written as PNG cLLi, so a viewer maps the capture to its own display from
// what the frame holds instead of assuming a 10,000-nit master.
struct ContentLight {
  float max_cll = 0.f;
  float max_fall = 0.f;
};

inline ContentLight MeasureContentLight(const CodedImage& image) {
  static const std::vector<float> kNits = [] {
    std::vector<float> nits(65536);
    for (uint32_t code = 0; code < nits.size(); ++code) nits[code] = PqToNits(code / 65535.f);
    return nits;
  }();
  ContentLight light;
  double sum = 0.0;
  for (size_t i = 0; i + 2 < image.rgb.size(); i += 3) {
    const float nits = kNits[std::max({image.rgb[i], image.rgb[i + 1], image.rgb[i + 2]})];
    light.max_cll = std::max(light.max_cll, nits);
    sum += nits;
  }
  if (!image.rgb.empty()) light.max_fall = static_cast<float>(sum / (image.rgb.size() / 3));
  return light;
}

// Rebuilds a PNG with this capture's colour chunks right after IHDR, and
// without any colour chunk the encoder wrote.  Empty when the stream is
// malformed.  SDR: sRGB.  HDR: cICP (which conforming decoders prefer),
// iCCP with the same signal (for Discord's preview), cLLi.
inline std::vector<uint8_t> WithColorChunks(
    const std::vector<uint8_t>& png, bool hdr, const ContentLight& light) {
  static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (png.size() < 8 || std::memcmp(png.data(), kSignature, 8) != 0) return {};
  std::vector<uint8_t> out(png.begin(), png.begin() + 8);
  out.reserve(png.size() + Bt2100PqIcc().size() + 256);
  bool placed = false;
  size_t at = 8;
  while (at + 12 <= png.size()) {
    const uint32_t length = ReadBe32(png.data() + at);
    if (length > png.size() - at - 12) return {};
    const char* type = reinterpret_cast<const char*>(png.data() + at + 4);
    const size_t end = at + 12 + length;
    bool colour = false;
    for (const char* owned : {"sRGB", "gAMA", "cHRM", "iCCP", "cICP", "cLLi", "mDCv"}) {
      colour = colour || std::memcmp(type, owned, 4) == 0;
    }
    if (!colour) out.insert(out.end(), png.begin() + at, png.begin() + end);
    at = end;
    if (std::memcmp(type, "IHDR", 4) == 0 && !placed) {
      placed = true;
      if (!hdr) {
        AppendPngChunk(out, "sRGB", {0});
        continue;
      }
      AppendPngChunk(out, "cICP", {9, 16, 0, 1});
      static constexpr char kName[] = "BT.2100 PQ";
      std::vector<uint8_t> iccp(kName, kName + sizeof(kName));  // name + NUL
      iccp.push_back(0);  // deflate
      const std::vector<uint8_t> zlib = ZlibStored(Bt2100PqIcc().data(), Bt2100PqIcc().size());
      iccp.insert(iccp.end(), zlib.begin(), zlib.end());
      AppendPngChunk(out, "iCCP", iccp);
      std::vector<uint8_t> clli;  // units of 0.0001 cd/m2
      AppendPngBe32(clli, static_cast<uint32_t>(std::lround(light.max_cll * 10000.0)));
      AppendPngBe32(clli, static_cast<uint32_t>(std::lround(light.max_fall * 10000.0)));
      AppendPngChunk(out, "cLLi", clli);
    }
    if (std::memcmp(type, "IEND", 4) == 0) return placed ? out : std::vector<uint8_t>{};
  }
  return {};
}

// Adds the profile as an APP2 ICC_PROFILE segment after SOI and APP0 (JFIF).
// Empty when the stream is not a JPEG.
inline std::vector<uint8_t> WithIccSegment(const std::vector<uint8_t>& jpeg) {
  const std::vector<uint8_t>& icc = Bt2100PqIcc();
  if (jpeg.size() < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) return {};
  size_t at = 2;
  while (at + 4 <= jpeg.size() && jpeg[at] == 0xFF && jpeg[at + 1] == 0xE0) {
    at += 2 + ((static_cast<size_t>(jpeg[at + 2]) << 8) | jpeg[at + 3]);
  }
  static constexpr char kMarker[] = "ICC_PROFILE";  // with its NUL
  const size_t length = 2 + sizeof(kMarker) + 2 + icc.size();
  if (at > jpeg.size() || length > 0xFFFF) return {};
  std::vector<uint8_t> out(jpeg.begin(), jpeg.begin() + at);
  out.reserve(jpeg.size() + length + 2);
  out.insert(out.end(), {0xFF, 0xE2, static_cast<uint8_t>(length >> 8),
                         static_cast<uint8_t>(length & 0xff)});
  out.insert(out.end(), kMarker, kMarker + sizeof(kMarker));
  out.insert(out.end(), {1, 1});  // chunk 1 of 1
  out.insert(out.end(), icc.begin(), icc.end());
  out.insert(out.end(), jpeg.begin() + at, jpeg.end());
  return out;
}

// Triangular dither in (-1, 1) LSB from a hash of the position.
inline float DitherOffset(uint32_t x, uint32_t y, uint32_t channel) {
  uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ (channel + 1u) * 0xcb1ab31fu;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return static_cast<float>((h & 0xffffu) + (h >> 16)) / 65535.f - 1.f;
}

// 8-bit BGR for WIC (every JPEG, the SDR PNG).  `dither` is for sources
// finer than 8 bits: an 8-bit PQ step is visible in dark gradients and sky,
// and the dither trades it for noise at the level JPEG's quantizer already
// adds.  The position hash repeats every `period` columns, so both halves of
// a side-by-side file carry identical noise.  8-bit sources are exact.
inline std::vector<uint8_t> ToBgr8(const CodedImage& image, bool dither, uint32_t period) {
  std::vector<uint8_t> bgr(image.rgb.size());
  for (uint32_t y = 0; y < image.height; ++y) {
    for (uint32_t x = 0; x < image.width; ++x) {
      const size_t at = (static_cast<size_t>(y) * image.width + x) * 3u;
      for (uint32_t channel = 0; channel < 3; ++channel) {
        float value = image.rgb[at + channel] / 257.f;
        if (dither) value += DitherOffset(x % period, y, channel);
        bgr[at + 2u - channel] = static_cast<uint8_t>(std::clamp(std::lround(value), 0l, 255l));
      }
    }
  }
  return bgr;
}

inline CodedImage SideBySide(const CodedImage& left, const CodedImage& right) {
  CodedImage both;
  both.width = left.width + right.width;
  both.height = std::min(left.height, right.height);
  both.rgb.reserve(static_cast<size_t>(both.width) * both.height * 3u);
  for (uint32_t y = 0; y < both.height; ++y) {
    for (const CodedImage* half : {&left, &right}) {
      const auto row = half->rgb.begin() + static_cast<ptrdiff_t>(y) * half->width * 3;
      both.rgb.insert(both.rgb.end(), row, row + static_cast<ptrdiff_t>(half->width) * 3);
    }
  }
  return both;
}

// One WIC encode into memory: JPEG from 24bpp BGR at `quality` (0-1, 4:4:4
// chroma where the OS offers it - *chroma_444 says whether it did), or PNG
// from 24bpp BGR or 48bpp RGB with adaptive filtering.  False on any
// failure; the caller falls back.  4:2:0 would smear UI text and red/blue
// edges, exactly what a comparison is looked at for.
inline bool WicEncode(
    IWICImagingFactory* factory, bool jpeg, uint32_t width, uint32_t height,
    const void* pixels, bool sixteen, float quality, std::vector<uint8_t>* out,
    bool* chroma_444 = nullptr) {
  using Microsoft::WRL::ComPtr;
  using CreateStreamFn = HRESULT(WINAPI*)(HGLOBAL, BOOL, LPSTREAM*);
  static const auto create_stream =
      renodx::utils::png::internal::LoadModuleProc<CreateStreamFn>(L"ole32.dll", "CreateStreamOnHGlobal");
  // wincodec.h's GUIDs, spelled out: their definitions live in a library
  // this addon does not link (as utils/png.hpp does).
  static constexpr GUID kJpegContainer = {0x19e4a5aa, 0x5662, 0x4fc5, {0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57}};
  static constexpr GUID kPngContainer = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
  static constexpr GUID kBgr24 = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c}};
  static constexpr GUID kRgb48 = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x15}};
  if (factory == nullptr || create_stream == nullptr || (jpeg && sixteen)) return false;
  ComPtr<IStream> stream;
  if (FAILED(create_stream(nullptr, TRUE, &stream))) return false;
  ComPtr<IWICBitmapEncoder> encoder;
  if (FAILED(factory->CreateEncoder(jpeg ? kJpegContainer : kPngContainer, nullptr, &encoder))
      || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
    return false;
  }
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> options;
  if (FAILED(encoder->CreateNewFrame(&frame, &options))) return false;
  PROPBAG2 option{};
  VARIANT value{};
  if (jpeg) {
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    value.vt = VT_R4;
    value.fltVal = quality;
    if (FAILED(options->Write(1, &option, &value))) return false;
    option.pstrName = const_cast<LPOLESTR>(L"JpegYCrCbSubsampling");
    value.vt = VT_UI1;
    value.bVal = 3;  // WICJpegYCrCbSubsampling444 (Windows 8.1+)
    const bool full_chroma = SUCCEEDED(options->Write(1, &option, &value));
    if (chroma_444 != nullptr) *chroma_444 = full_chroma;
  } else {
    option.pstrName = const_cast<LPOLESTR>(L"FilterOption");
    value.vt = VT_UI1;
    value.bVal = 6;  // WICPngFilterAdaptive; the encoder's default otherwise
    options->Write(1, &option, &value);
  }
  const GUID wanted = sixteen ? kRgb48 : kBgr24;
  WICPixelFormatGUID format = wanted;
  const UINT stride = width * (sixteen ? 6u : 3u);
  if (FAILED(frame->Initialize(options.Get())) || FAILED(frame->SetSize(width, height))
      || FAILED(frame->SetPixelFormat(&format)) || !InlineIsEqualGUID(format, wanted)
      || FAILED(frame->WritePixels(height, stride, stride * height,
                                   static_cast<BYTE*>(const_cast<void*>(pixels))))
      || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
    return false;
  }
  STATSTG stat{};
  const LARGE_INTEGER start{};
  if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0
      || stat.cbSize.QuadPart > 0x7fffffffull
      || FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
    return false;
  }
  out->resize(static_cast<size_t>(stat.cbSize.QuadPart));
  ULONG read = 0;
  return SUCCEEDED(stream->Read(out->data(), static_cast<ULONG>(out->size()), &read))
         && read == out->size();
}

// The PNG without WIC: stored deflate blocks, larger but never lost.
inline std::vector<uint8_t> StoredPng(const CodedImage& image, const std::vector<uint8_t>* bgr8) {
  const bool sixteen = bgr8 == nullptr;
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(image.height) * (1u + image.width * (sixteen ? 6u : 3u)));
  for (uint32_t y = 0; y < image.height; ++y) {
    raw.push_back(0);  // PNG filter: none
    for (uint32_t x = 0; x < image.width; ++x) {
      const size_t at = (static_cast<size_t>(y) * image.width + x) * 3u;
      for (uint32_t channel = 0; channel < 3; ++channel) {
        if (sixteen) {
          raw.push_back(static_cast<uint8_t>(image.rgb[at + channel] >> 8));
          raw.push_back(static_cast<uint8_t>(image.rgb[at + channel] & 0xff));
        } else {
          raw.push_back((*bgr8)[at + 2u - channel]);
        }
      }
    }
  }
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<uint8_t> ihdr;
  AppendPngBe32(ihdr, image.width);
  AppendPngBe32(ihdr, image.height);
  ihdr.insert(ihdr.end(), {static_cast<uint8_t>(sixteen ? 16 : 8), 2, 0, 0, 0});
  AppendPngChunk(png, "IHDR", ihdr);
  AppendPngChunk(png, "IDAT", ZlibStored(raw.data(), raw.size()));
  AppendPngChunk(png, "IEND", {});
  return png;
}

// A finished PNG: RGB16 PQ for HDR, RGB8 for SDR (from `bgr8`), WIC deflate
// when WIC is available (*compressed), stored blocks otherwise, and this
// capture's colour chunks either way.
inline std::vector<uint8_t> EncodePng(
    IWICImagingFactory* factory, const CodedImage& image, bool hdr,
    const std::vector<uint8_t>& bgr8, const ContentLight& light, bool* compressed) {
  std::vector<uint8_t> png;
  *compressed = WicEncode(factory, false, image.width, image.height,
                          hdr ? static_cast<const void*>(image.rgb.data()) : bgr8.data(),
                          hdr, 0.f, &png);
  if (*compressed) {
    std::vector<uint8_t> tagged = WithColorChunks(png, hdr, light);
    if (!tagged.empty()) return tagged;
    *compressed = false;
  }
  return WithColorChunks(StoredPng(image, hdr ? nullptr : &bgr8), hdr, light);
}

inline bool WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return file.good();
}

// The panel's own UI captures (dlssnr.hpp): RGBA8 in, an sRGB RGB8 PNG out.
inline bool WritePngRgba8(const std::filesystem::path& path,
                          uint32_t width, uint32_t height,
                          const std::vector<uint8_t>& rgba) {
  if (width == 0 || height == 0 || rgba.size() != static_cast<size_t>(width) * height * 4u) {
    return false;
  }
  CodedImage image;
  image.width = width;
  image.height = height;
  std::vector<uint8_t> bgr(static_cast<size_t>(width) * height * 3u);
  for (size_t pixel = 0; pixel < bgr.size() / 3u; ++pixel) {
    bgr[pixel * 3u] = rgba[pixel * 4u + 2u];
    bgr[pixel * 3u + 1u] = rgba[pixel * 4u + 1u];
    bgr[pixel * 3u + 2u] = rgba[pixel * 4u];
  }
  renodx::utils::png::internal::ScopedComInitialization com;
  bool compressed = false;
  return WriteBytes(path, EncodePng(com.IsUsable()
                                        ? renodx::utils::png::internal::CreateWicFactory().Get()
                                        : nullptr,
                                    image, false, bgr, {}, &compressed));
}


// Every file of one capture, encoded and written; true when the pair was.
// The pair is all or nothing: a lone half is removed.  The side-by-side
// file is extra, and its failure is logged, never a lost pair.  JPEGs share
// one quality - the requested one, lowered in steps of 3 (to 85, or the
// request when lower) until each file fits the size cap; the image is never
// downscaled.  A pair at two qualities would bias the comparison.  Appends
// what it did to *meta.
inline bool WriteCaptureFiles(
    const PendingCapture& work, const CodedImage& pre, const CodedImage& post,
    const std::filesystem::path& base, FinishedCapture* record, std::string* meta) {
  const bool hdr = work.hdr_mode != 0;
  const bool dither = hdr || !EightBitFormat(work.format);
  renodx::utils::png::internal::ScopedComInitialization com;
  const Microsoft::WRL::ComPtr<IWICImagingFactory> factory =
      com.IsUsable() ? renodx::utils::png::internal::CreateWicFactory() : nullptr;
  // JPEG needs WIC; without it the capture is still kept, as PNG.
  const bool want_jpeg = work.output.format != FileFormat::kPng && factory != nullptr;
  const bool want_png = work.output.format != FileFormat::kJpeg || !want_jpeg;
  if (work.output.format != FileFormat::kPng && !want_jpeg) {
    SafeLog(reshade::log::level::warning,
            "NR screenshot: JPEG needs the Windows Imaging Component, which did not load;"
            " writing PNG instead");
  }

  struct Picture {
    const wchar_t* suffix;
    const CodedImage* image;
    bool pair;
    std::vector<uint8_t> bgr8;
    ContentLight light;
  };
  CodedImage both;
  std::vector<Picture> pictures;
  pictures.push_back({work.model_diagnostic ? L"_MODEL_INPUT" : L"_NR_OFF", &pre, true, {}, {}});
  pictures.push_back({work.model_diagnostic ? L"_MODEL_OUTPUT" : L"", &post, true, {}, {}});
  if (work.output.side_by_side) {
    both = SideBySide(pre, post);
    pictures.push_back({work.model_diagnostic ? L"_MODEL_COMPARE" : L"_COMPARE", &both, false, {}, {}});
  }
  for (Picture& picture : pictures) {
    if (want_jpeg || !hdr) picture.bgr8 = ToBgr8(*picture.image, dither, pre.width);
    if (hdr) picture.light = MeasureContentLight(*picture.image);
  }

  struct File {
    std::filesystem::path path;
    std::vector<uint8_t> bytes;
    bool pair;
  };
  std::vector<File> files;
  bool compressed = true;
  if (want_png) {
    for (const Picture& picture : pictures) {
      bool wic = false;
      files.push_back({base.wstring() + picture.suffix + L".png",
                       EncodePng(factory.Get(), *picture.image, hdr, picture.bgr8, picture.light, &wic),
                       picture.pair});
      compressed = compressed && wic;
    }
  }
  const uint32_t requested = std::clamp(work.output.jpeg_quality, 80u, 100u);
  const uint32_t lowest = std::min(85u, requested);
  const uint64_t budget = static_cast<uint64_t>(work.output.max_megabytes) * 1000000ull;
  uint32_t pair_quality = 0;
  uint32_t compare_quality = 0;
  bool chroma_444 = false;
  bool over_budget = false;
  if (want_jpeg) {
    for (const bool pair : {true, false}) {
      std::vector<File> encoded;
      for (uint32_t quality = requested;; quality = std::max(lowest, quality - 3u)) {
        encoded.clear();
        bool fits = true;
        for (const Picture& picture : pictures) {
          if (picture.pair != pair) continue;
          std::vector<uint8_t> jpeg;
          if (WicEncode(factory.Get(), true, picture.image->width, picture.image->height,
                        picture.bgr8.data(), false, quality / 100.f, &jpeg, &chroma_444)
              && hdr) {
            jpeg = WithIccSegment(jpeg);
          }
          if (jpeg.empty()) {
            SafeLog(reshade::log::level::error, "NR screenshot JPEG encode failed");
            encoded.clear();
            break;
          }
          fits = fits && (budget == 0 || jpeg.size() <= budget);
          encoded.push_back({base.wstring() + picture.suffix + L".jpg", std::move(jpeg), pair});
        }
        if (encoded.empty() || fits || quality == lowest) {
          uint32_t& used = pair ? pair_quality : compare_quality;
          used = encoded.empty() ? 0 : quality;
          over_budget = over_budget || (!encoded.empty() && !fits);
          break;
        }
      }
      if (pair && encoded.empty() && !want_png) return false;  // nothing to write
      for (File& file : encoded) files.push_back(std::move(file));
    }
  }

  bool pair_ok = true;
  bool extra_ok = true;
  std::string names;
  std::string sizes;
  for (const File& file : files) {
    bool& ok = file.pair ? pair_ok : extra_ok;
    ok = WriteBytes(file.path, file.bytes) && ok;
    const std::string name = PathToLogString(file.path.filename());
    names += (names.empty() ? "" : ",") + name;
    sizes += (sizes.empty() ? "" : ",") + name + ":" + std::to_string(file.bytes.size());
  }
  if (!pair_ok) {
    // Never leave a lone A image without its B counterpart.
    for (const File& file : files) {
      std::error_code ignored;
      if (file.pair) std::filesystem::remove(file.path, ignored);
    }
    return false;
  }
  if (!extra_ok) {
    SafeLog(reshade::log::level::warning, "NR screenshot side-by-side file could not be written");
  }
  // The report bundles the lossless pair when there is one.
  const std::wstring primary = want_png ? L".png" : L".jpg";
  record->pre_image = base.wstring() + pictures[0].suffix + primary;
  record->post_image = base.wstring() + pictures[1].suffix + primary;
  if (over_budget) {
    SafeLog(reshade::log::level::warning,
            "NR screenshot JPEG is larger than NRScreenshotMaxMB="
                + std::to_string(work.output.max_megabytes) + " even at quality "
                + std::to_string(lowest) + "; written anyway (the image is never downscaled)");
  }
  SafeLog(reshade::log::level::info,
          "NR screenshot files: " + sizes
              + (want_jpeg ? ", JPEG quality " + std::to_string(pair_quality)
                                 + (chroma_444 ? " 4:4:4" : " (encoder-default chroma)")
                           : std::string())
              + (hdr ? ", HDR PQ BT.2020 signalled by cICP and ICC cicp 9/16/0/1" : ", SDR sRGB"));

  std::string lines = std::string("file_format=")
      + (want_png && want_jpeg ? "png+jpeg" : want_png ? "png" : "jpeg")
      + "\nlayout=" + (work.output.side_by_side ? "pair+side-by-side" : "pair")
      + "\nfiles=" + names + "\nbytes=" + sizes
      + "\npng_bits=" + (hdr ? "16" : "8")
      + "\npng_encoder=" + (!want_png ? "none" : compressed ? "wic-deflate" : "stored")
      + "\ndither_8bit=" + (dither ? "1" : "0");
  if (want_jpeg) {
    lines += "\njpeg_quality=" + std::to_string(pair_quality)
             + "\njpeg_requested_quality=" + std::to_string(requested)
             + "\njpeg_budget_bytes=" + std::to_string(budget)
             + "\njpeg_over_budget=" + (over_budget ? "1" : "0")
             + "\njpeg_chroma=" + (chroma_444 ? "444" : "encoder-default")
             + "\njpeg_icc=" + (hdr ? "bt2100-pq-cicp-9-16-0-1" : "none-srgb");
    if (work.output.side_by_side) lines += "\njpeg_compare_quality=" + std::to_string(compare_quality);
  }
  if (hdr) {
    char light[160];
    std::snprintf(light, sizeof(light),
                  "\nmax_cll_nits=%.1f,%.1f\nmax_fall_nits=%.1f,%.1f",
                  pictures[0].light.max_cll, pictures[1].light.max_cll,
                  pictures[0].light.max_fall, pictures[1].light.max_fall);
    lines += light;
  }
  *meta += lines + "\n";
  return true;
}

// ---------------------------------------------------------------------------
// Worker thread. Owns everything after the hand-off; must never throw across
// its boundary.

inline void ProcessJob(std::optional<PendingCapture> work) noexcept {
  if (!work.has_value()) return;
  FinishedCapture record;
  record.serial = work->serial;
  record.model_diagnostic = work->model_diagnostic;
  try {
    if (work->aborted || !work->ready) {
      if (work->aborted) {
        SafeLog(reshade::log::level::info, "NR screenshot aborted; no files written");
      } else {
        SafeLog(reshade::log::level::error, "NR screenshot not ready; no files written");
      }
      // Settled on the same proof as a delivered capture (this path used to
      // return without releasing them).
      ReleaseReadbacks(*work);
      record.outcome = work->aborted ? "aborted" : "not_ready";
      PublishFinished(std::move(record));
      return;
    }

    // All filesystem work happens here on the worker, never on the render or
    // present thread. Wide paths only: narrowing to ANSI can throw.
    std::filesystem::path directory;
    try {
      directory =
          renodx::utils::path::GetReShadeBasePath() / std::filesystem::path(L"DLSS5 Screenshots");
      std::error_code ec;
      std::filesystem::create_directories(directory, ec);
    } catch (...) {
      directory.clear();
    }

    std::filesystem::path base = directory.empty()
        ? std::filesystem::path(
              std::wstring(work->model_diagnostic ? L"NR_DIAGNOSTIC_" : L"NR_CAPTURE_")
              + std::to_wstring(work->timestamp_ms) + L"_"
              + std::to_wstring(work->serial))
        : directory / std::filesystem::path(
              std::wstring(work->model_diagnostic ? L"NR_DIAGNOSTIC_" : L"NR_CAPTURE_")
              + std::to_wstring(work->timestamp_ms) + L"_"
              + std::to_wstring(work->serial));
    CodedImage pre_image;
    CodedImage post_image;
    bool pre_nonzero = false;
    bool post_nonzero = false;
    bool pre_unwritten = false;
    bool post_unwritten = false;
    const bool read_ok =
        ReadbackToCoded(*work, work->pre_nr_readback, &pre_image, &pre_nonzero,
                        &pre_unwritten)
        && ReadbackToCoded(*work, work->nr_output_readback, &post_image, &post_nonzero,
                           &post_unwritten);
    if (pre_unwritten || post_unwritten) {
      captures_unexecuted.fetch_add(1, std::memory_order_relaxed);
      SafeLog(
          reshade::log::level::warning,
          std::string("NR screenshot refused: the GPU had not run its copies when"
                      " they were read")
              + (work->exact
                     ? " (the command list that recorded them was reset without"
                       " being submitted)"
                     : " (this session has no exact submission proof, so the"
                       " buffers stay allocated)")
              + "; no files written - take the screenshot again");
      if (work->exact) {
        ReleaseReadbacks(*work);
      } else {
        RetiredCaptureBuffers held;
        held.Take(std::move(*work));
        held_unexecuted.push_back(std::move(held));
        retained_sets.fetch_add(1, std::memory_order_relaxed);
      }
      record.outcome = "unexecuted";
      PublishFinished(std::move(record));
      return;
    }
    if (read_ok && (!pre_nonzero || !post_nonzero)) {
      // The copies ran (the stamp is gone), so the frame itself was black.
      // Said anyway, so a black PNG is never a mystery.
      SafeLog(
          reshade::log::level::warning,
          std::string("NR screenshot pair contains an entirely black image")
              + (!pre_nonzero ? " [pre-NR]" : "")
              + (!post_nonzero ? " [NR on]" : "")
              + "; the GPU ran the copies, so the captured frame itself was"
                " black");
    }
    std::string meta = work->meta;
    const bool wrote = read_ok
        && WriteCaptureFiles(*work, pre_image, post_image, base, &record, &meta);
    record.outcome = wrote ? "written" : "write_failed";
    if (wrote) {
      SafeLog(
          reshade::log::level::info,
          "NR screenshot pair written: " + PathToLogString(record.pre_image) + " and "
              + PathToLogString(record.post_image));
    } else {
      SafeLog(
          reshade::log::level::error,
          "NR screenshot readback or image write failed; no pair written ("
              + PathToLogString(directory)
              + "). Check the folder exists and is writable, and that disk"
                " space is available");
    }
    // Raw float planes + sidecar meta: the exposure/units evidence the PNGs
    // cannot carry.  Row-major float32 RGBA, no header; dims live in the meta.
    for (const internal::DiagnosticPlane& plane : work->planes) {
      if (plane.readback == nullptr) continue;
      std::filesystem::path plane_path = base;
      plane_path += L"_" + std::wstring(plane.name.begin(), plane.name.end())
          + L".f32";
      std::error_code plane_ec;
      std::filesystem::remove(plane_path, plane_ec);
      std::ofstream out(plane_path, std::ios::binary | std::ios::trunc);
      bool plane_ok = out.good() && plane.width > 0u && plane.height > 0u;
      if (plane_ok) {
        void* mapped = nullptr;
        if (SUCCEEDED(plane.readback->Map(0, nullptr, &mapped))
            && mapped != nullptr) {
          plane_ok = !Unwritten(mapped, plane.total_bytes);  // copy never ran
          std::vector<float> row(static_cast<size_t>(plane.width) * 4u);
          const auto* base_row = static_cast<const uint8_t*>(mapped);
          for (uint32_t y = 0; y < plane.height && plane_ok; ++y) {
            const auto* src = reinterpret_cast<const uint16_t*>(
                base_row + static_cast<size_t>(y) * plane.footprint.Footprint.RowPitch);
            for (uint32_t x = 0; x < plane.width; ++x) {
              row[x * 4u] = HalfToFloat(src[x * 4u]);
              row[x * 4u + 1] = HalfToFloat(src[x * 4u + 1]);
              row[x * 4u + 2] = HalfToFloat(src[x * 4u + 2]);
              row[x * 4u + 3] = HalfToFloat(src[x * 4u + 3]);
            }
            out.write(
                reinterpret_cast<const char*>(row.data()),
                static_cast<std::streamsize>(row.size() * sizeof(float)));
            plane_ok = out.good();
          }
          plane.readback->Unmap(0, nullptr);
        } else {
          plane_ok = false;
        }
      }
      if (plane_ok) {
        SafeLog(
            reshade::log::level::info,
            "NR diagnostic plane written: " + PathToLogString(plane_path)
                + " (" + plane.name + ", " + std::to_string(plane.width) + "x"
                + std::to_string(plane.height) + " f32)");
      } else {
        SafeLog(
            reshade::log::level::error,
            "NR diagnostic plane write failed: " + PathToLogString(plane_path));
      }
    }
    if (!work->meta.empty()) {
      std::filesystem::path meta_path = base;
      meta_path += L".meta.txt";
      std::ofstream meta_out(meta_path, std::ios::trunc);
      if (meta_out.good()) {
        meta_out << meta;
        record.meta = meta_path;
        SafeLog(
            reshade::log::level::info,
            "NR capture meta written: " + PathToLogString(meta_path));
      }
    }
  } catch (const std::exception& ex) {
    record.outcome = "exception";
    SafeLog(reshade::log::level::error, std::string("NR screenshot exception: ") + ex.what());
  } catch (...) {
    record.outcome = "exception";
    SafeLog(reshade::log::level::error, "NR screenshot unknown exception");
  }
  PublishFinished(std::move(record));
  ReleaseReadbacks(*work);
}

inline void WorkerLoop() noexcept {
  // Pin the addon module for the lifetime of this thread.  The worker is
  // detached by design (see the header contract), so a mid-session FreeLibrary
  // of the addon - a ReShade addon reload - would otherwise unmap the code
  // this loop is executing.  The pinned reference is deliberately never
  // released; the OS reclaims it at process end.
  HMODULE pinned_module = nullptr;
  GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
      reinterpret_cast<LPCWSTR>(&WorkerLoop),
      &pinned_module);
  for (;;) {
    std::optional<PendingCapture> work;
    {
      std::unique_lock lock(job_mutex);
      job_signal.wait(lock, [] {
        return shutdown_requested.load(std::memory_order_acquire) || job.has_value();
      });
      if (!job.has_value()) return;  // shutdown requested and queue drained
      work = std::move(*job);
      job.reset();
    }
    ProcessJob(std::move(work));
  }
}

// Hands a settled capture to the worker. Called on the present thread with no
// other lock held; must not throw.
inline void HandoffToWorker(std::optional<PendingCapture> work) noexcept {
  if (!work.has_value()) return;
  if (shutdown_requested.load(std::memory_order_acquire)) {
    ReleaseReadbacks(*work);
    return;
  }
  {
    std::scoped_lock lock(job_mutex);
    if (job.has_value()) {
      // A newer capture supersedes one the worker has not picked up yet.
      ReleaseReadbacks(*job);
      job.reset();
      SafeLog(reshade::log::level::info, "NR screenshot superseded before write");
    }
    job = std::move(work);
  }
  if (!worker_started.exchange(true, std::memory_order_acq_rel)) {
    bool created = false;
    try {
      std::thread(WorkerLoop).detach();
      created = true;
    } catch (...) {
    }
    if (!created) {
      // No worker available: degrade to synchronous completion rather than
      // leaving the feature wedged. One present-thread stall, never a crash.
      worker_started.store(false, std::memory_order_release);
      std::optional<PendingCapture> fallback;
      {
        std::scoped_lock lock(job_mutex);
        fallback = std::move(job);
        job.reset();
      }
      ProcessJob(std::move(fallback));
      return;
    }
  }
  job_signal.notify_one();
}

}  // namespace internal

// ---------------------------------------------------------------------------
// Public API. Every entry point is noexcept-safe; callers on the render path
// (ProcessInline) hold dlssnr's runtime_mutex, and state_mutex is always
// nested inside it, never the other way around.

inline bool IsArmed() {
  return internal::capture_requested.load(std::memory_order_acquire);
}

inline bool WantsFinalPresent() {
  return IsArmed() && internal::requested_source.load(std::memory_order_relaxed)
      == SourceMode::kFinalPresent;
}

inline bool WantsEvaluatedBuffer() {
  return IsArmed() && internal::requested_source.load(std::memory_order_relaxed)
      == SourceMode::kEvaluatedBuffer;
}

// The request must precede the OUTER game evaluation. A request that arrives
// after pre-SR NR already ran cannot claim that evaluation's later guide copy
// is an untouched source. Nested NGX/Streamline forwards keep the outer stamp.
inline thread_local uint32_t evaluation_depth = 0;
inline thread_local uint64_t evaluation_request = 0;
struct EvaluationScope {
  EvaluationScope() {
    if (evaluation_depth++ == 0) {
      evaluation_request = IsArmed()
          ? internal::request_id.load(std::memory_order_acquire) : 0;
    }
  }
  ~EvaluationScope() {
    if (--evaluation_depth == 0) evaluation_request = 0;
  }
};

inline bool HasPending() {
  return internal::has_pending.load(std::memory_order_acquire);
}

inline void Disarm() {
  internal::capture_requested.store(false, std::memory_order_release);
}

// Arms the one-shot capture. Ignored while a capture is already armed or
// waiting for its GPU completion window.
inline bool RequestCapture() {
  std::scoped_lock lock(internal::state_mutex);
  if (internal::capture_requested.load(std::memory_order_acquire)
      || internal::pending.has_value()) {
    return false;
  }
  internal::requires_passthrough_frame.store(false, std::memory_order_relaxed);
  internal::requested_source.store(
      ResolveSource(source_mode.load(std::memory_order_relaxed),
                    separate_presentation.load(std::memory_order_relaxed)),
      std::memory_order_relaxed);
  internal::request_id.fetch_add(1, std::memory_order_relaxed);
  const uint32_t format = file_format.load(std::memory_order_relaxed);
  internal::requested_output = {
      format <= 2u ? static_cast<FileFormat>(format) : FileFormat::kPng,
      layout.load(std::memory_order_relaxed) == 1u,
      jpeg_quality.load(std::memory_order_relaxed),
      max_megabytes.load(std::memory_order_relaxed)};
  internal::presents_while_armed.store(0, std::memory_order_relaxed);
  internal::capture_requested.store(true, std::memory_order_release);
  internal::SafeLog(
      reshade::log::level::info,
      WantsFinalPresent()
          ? "NR screenshot armed: one final presentation image, before and after NR"
            " (temporarily uses On Present; HDR stays HDR)"
          : "NR diagnostic armed: evaluated model input/output from one pass;"
            " excludes look, transfer, game tone mapping and UI");
  return true;
}

// Unsupported presentation APIs finish the request explicitly. They must not
// substitute a pre-tonemap NGX texture and call it a displayed NR off/on pair.
inline void FailRequest(const char* outcome, const char* explanation) {
  std::scoped_lock lock(internal::state_mutex);
  if (!internal::capture_requested.exchange(false, std::memory_order_acq_rel)) return;
  if (internal::pending.has_value()) internal::pending->aborted = true;
  internal::FinishedCapture record;
  record.serial = internal::capture_serial.fetch_add(1) + 1;
  record.outcome = outcome;
  internal::PublishFinished(std::move(record));
  internal::SafeLog(reshade::log::level::warning,
                    std::string("NR screenshot unavailable: ") + explanation);
}

// English; the modern panel draws it through ui::Tr (the Developer view as
// is).
// i18n: begin - display strings (tools/i18n/ui_strings.py catalog)
inline const char* StatusText() {
  if (HasPending()) return "waiting for GPU completion";
  if (WantsFinalPresent()) return "armed, waiting for one final presentation frame";
  if (IsArmed()) return "armed, waiting for an evaluated model buffer pair";
  std::scoped_lock lock(internal::finished_mutex);
  const char* outcome = internal::finished.outcome;
  if (std::strcmp(outcome, "written") == 0) {
    return internal::finished.model_diagnostic
        ? "saved: model input/output diagnostic; not the displayed game frame"
        : "saved: same frame before and after NR";
  }
  if (std::strcmp(outcome, "unsupported_present") == 0) return "unavailable on this presentation path";
  if (std::strcmp(outcome, "unsupported_diagnostic") == 0) return "evaluated-buffer capture is unavailable on this API";
  if (std::strcmp(outcome, "unproven_baseline") == 0) return "exact comparison unavailable with this frame-generation route; disable frame generation for the capture";
  if (std::strcmp(outcome, "nr_disabled") == 0) return "enable NR before taking a comparison";
  if (std::strcmp(outcome, "foreign_nr") == 0) return "unavailable: another NR implementation owns this frame";
  if (std::strcmp(outcome, "none") == 0) return "idle";
  return "capture failed; see the log for its cause";
}
// i18n: end

// Edge-detected capture-hotkey poll, called once per present with the
// configurable virtual-key code (owned by dlssnr.hpp) and whether the game
// has the foreground: GetAsyncKeyState is system-wide, so without it the key
// pressed in another application captured the game.  Only a single user32
// call plus two atomic accesses when idle.
inline void PollHotkey(uint32_t virtual_key, bool focused) {
  const bool down = focused
      && (GetAsyncKeyState(static_cast<int>(virtual_key)) & 0x8000) != 0;
  if (!down) {
    internal::hotkey_was_down.store(false, std::memory_order_relaxed);
    return;
  }
  if (internal::hotkey_was_down.exchange(true, std::memory_order_relaxed)) return;
  RequestCapture();
}

// Area (crop dimensions) of the currently pending capture, 0 when none.  The
// evaluate-side gate uses it to keep a larger main-pass capture from being
// superseded by a smaller secondary pass in the same frame.
inline uint64_t PendingCaptureArea() {
  if (!internal::has_pending.load(std::memory_order_acquire)) return 0;
  std::scoped_lock lock(internal::state_mutex);
  if (!internal::pending.has_value()) return 0;
  return static_cast<uint64_t>(internal::pending->width)
      * internal::pending->height;
}

// Allocates the two addon-owned READBACK buffers for one evaluation pair.
// Runs on the render thread with runtime_mutex held; performs no filesystem
// work and can therefore not throw.  width/height are the CAPTURE (crop)
// dimensions; crop_x/crop_y select a subrect when the engine renders into
// part of a larger DLSS output resource (clamped against the resource here).
inline bool PrepareCapture(
    ID3D12Device* device,
    ID3D12Resource* source,
    uint32_t crop_x,
    uint32_t crop_y,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    uint8_t hdr_mode,
    uint64_t generation,
    float linear_unit_nits = 80.f,
    bool model_diagnostic = false) {
  if (device == nullptr || source == nullptr || width == 0 || height == 0)
    return false;
  const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 total_bytes = 0;
  device->GetCopyableFootprints(
      &source_desc, 0, 1, 0, &footprint, &rows, nullptr, &total_bytes);
  if (total_bytes == 0 || footprint.Footprint.RowPitch == 0) return false;
  if (crop_x >= footprint.Footprint.Width
      || crop_y >= footprint.Footprint.Height) {
    return false;
  }
  if (crop_x + width > footprint.Footprint.Width) {
    width = footprint.Footprint.Width - crop_x;
  }
  if (crop_y + height > footprint.Footprint.Height) {
    height = footprint.Footprint.Height - crop_y;
  }
  if (width == 0 || height == 0) return false;

  internal::PendingCapture capture;
  capture.model_diagnostic = model_diagnostic;
  capture.width = width;
  capture.height = height;
  capture.crop_x = crop_x;
  capture.crop_y = crop_y;
  capture.footprint = footprint;
  capture.total_bytes = total_bytes;
  capture.row_pitch = footprint.Footprint.RowPitch;
  capture.format = format;
  capture.hdr_mode = hdr_mode;
  capture.linear_unit_nits = linear_unit_nits;
  capture.timestamp_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  capture.serial = internal::capture_serial.fetch_add(1) + 1;
  capture.creation_generation = generation;

  if (!internal::MakeReadbackBuffer(device, total_bytes, &capture.pre_nr_readback)
      || !internal::MakeReadbackBuffer(device, total_bytes, &capture.nr_output_readback)) {
    internal::ReleaseReadbacks(capture);
    return false;
  }
  std::scoped_lock lock(internal::state_mutex);
  if (internal::pending.has_value()) {
    // A prior evaluate in this same capture-frame already recorded a pair into
    // its own buffers. Retire that ENTIRE buffer set - the PNG pair and every
    // diagnostic plane (still GPU-pending on the frame's command list) - so it
    // is released at the same settle window as the final capture; the latest
    // evaluate's pair wins.
    internal::RetiredCaptureBuffers retired;
    retired.pre_nr_readback = internal::pending->pre_nr_readback;
    retired.nr_output_readback = internal::pending->nr_output_readback;
    retired.planes = std::move(internal::pending->planes);
    internal::retired_buffers.push_back(std::move(retired));
    internal::retained_sets.fetch_add(1, std::memory_order_relaxed);
  }
  capture.output = internal::requested_output;
  internal::pending = std::move(capture);
  internal::has_pending.store(true, std::memory_order_release);
  return true;
}

// Records the pre-NR copy: pre_nr_source must hold the untouched DLSS output
// and be in D3D12_RESOURCE_STATE_COPY_SOURCE when called.
inline void RecordPreCopy(
    ID3D12GraphicsCommandList* command_list,
    ID3D12Resource* pre_nr_source) {
  if (command_list == nullptr || pre_nr_source == nullptr) return;
  std::scoped_lock lock(internal::state_mutex);
  if (!internal::pending.has_value()) return;
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = internal::pending->pre_nr_readback;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = internal::pending->footprint;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = pre_nr_source;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  submission::TrackUse(command_list, dst.pResource);
}

// Records the post-NR copy and completes the pair: nr_source must hold the
// final decoded result and be in D3D12_RESOURCE_STATE_COPY_SOURCE.
inline void RecordPostCopy(
    ID3D12GraphicsCommandList* command_list,
    ID3D12Resource* nr_source) {
  if (command_list == nullptr || nr_source == nullptr) return;
  std::scoped_lock lock(internal::state_mutex);
  if (!internal::pending.has_value()) return;
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = internal::pending->nr_output_readback;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = internal::pending->footprint;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = nr_source;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  submission::TrackUse(command_list, dst.pResource);
  internal::pending->ready = true;
  // Freeze this exact pair immediately. Waiting for a later Present lets
  // another swapchain or generated frame replace one the user requested.
  internal::capture_requested.store(false, std::memory_order_release);
}

// The float planes are a developer's evidence, ini-only (NRScreenshotPlanes,
// set by dlssnr.hpp's LoadConfiguration; off by default since v7.0.0-rc10).
// An HDR capture records four or more of them, each ~130 MB on disk at 4K
// plus its readback heap, which a player's screenshot key should not cost.
inline std::atomic_bool diagnostic_planes_enabled = false;

// Allocates the readback buffer for one named float plane (e.g. the sRGB
// proxy, the resolved work surface).  Call while a capture is pending, before
// the chain records its copies; no-op when no capture is in flight or the
// planes are off, and every plane's copy then records nothing.
inline bool PrepareDiagnosticPlane(
    ID3D12Device* device,
    const char* name,
    ID3D12Resource* texture,
    uint32_t width,
    uint32_t height) {
  if (!diagnostic_planes_enabled.load(std::memory_order_relaxed)) return false;
  if (device == nullptr || name == nullptr || texture == nullptr) return false;
  std::scoped_lock lock(internal::state_mutex);
  if (!internal::pending.has_value()) return false;
  for (const internal::DiagnosticPlane& plane : internal::pending->planes) {
    if (plane.name == name) return true;  // already prepared this frame
  }
  const D3D12_RESOURCE_DESC desc = texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 total_bytes = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                &total_bytes);
  if (total_bytes == 0 || footprint.Footprint.RowPitch == 0) return false;
  internal::DiagnosticPlane plane;
  plane.name = name;
  plane.width = width;
  plane.height = height;
  plane.footprint = footprint;
  plane.total_bytes = total_bytes;
  if (!internal::MakeReadbackBuffer(device, total_bytes, &plane.readback)) return false;
  internal::pending->planes.push_back(std::move(plane));
  return true;
}

// Records one float-plane copy: texture must be in COPY_SOURCE for the call.
inline void RecordDiagnosticPlaneCopy(
    ID3D12GraphicsCommandList* command_list,
    const char* name,
    ID3D12Resource* texture) {
  if (command_list == nullptr || name == nullptr || texture == nullptr) return;
  std::scoped_lock lock(internal::state_mutex);
  if (!internal::pending.has_value()) return;
  for (internal::DiagnosticPlane& plane : internal::pending->planes) {
    if (plane.name != name || plane.readback == nullptr) continue;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = plane.readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = plane.footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    submission::TrackUse(command_list, plane.readback);
    return;
  }
}

// Per-stack-pass diagnostic copy: under a multi-pass stack each pass records
// its own plane ("proxy_p2"), exposing what each stacked pass sees and
// returns; a single-pass stack keeps the canonical name ("proxy").  Prepares
// the plane on first use (name-deduped), so stack-1 captures are unchanged.
inline void RecordStackPlaneCopy(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* command_list,
    ID3D12Resource* texture,
    uint32_t width,
    uint32_t height,
    const char* base_name,
    uint32_t pass,
    uint32_t stack) {
  char name[16];
  if (stack > 1) {
    std::snprintf(name, sizeof(name), "%.6s_p%u", base_name, pass);
  } else {
    std::snprintf(name, sizeof(name), "%s", base_name);
  }
  if (!PrepareDiagnosticPlane(device, name, texture, width, height)) return;
  RecordDiagnosticPlaneCopy(command_list, name, texture);
}

// Attaches the sidecar metadata (divisor, encoding, meter state) that the
// .f32 planes need to be interpretable.  No-op when no capture is pending.
inline void SetPendingDiagnosticMeta(std::string meta) {
  std::scoped_lock lock(internal::state_mutex);
  if (internal::pending.has_value()) {
    internal::pending->meta = std::move(meta);
  }
}

// A helper's presentation window is not the evaluated game image. Copy
// only addon-owned model buffers, whose states and equal extents are known;
// the game's pre-SR Color is merely shader-readable, with no copy-state
// proof. Both PNGs are explicitly sRGB previews, not calibrated HDR or the
// game's final NR-off/on result. Call after the full chain succeeds, while
// the last evaluated pass's proxy/output still exist unmodified.
inline void CaptureModelPair(
    ID3D12Device* device, ID3D12GraphicsCommandList* command_list,
    ID3D12Resource* proxy, D3D12_RESOURCE_STATES proxy_state,
    ID3D12Resource* neural, D3D12_RESOURCE_STATES neural_state,
    uint64_t generation, const char* path, uint32_t pass, uint32_t stack) {
  if (!WantsEvaluatedBuffer() || HasPending()) return;
  const D3D12_RESOURCE_DESC desc = proxy->GetDesc();
  const D3D12_RESOURCE_DESC neural_desc = neural->GetDesc();
  if (desc.Width != neural_desc.Width || desc.Height != neural_desc.Height
      || desc.Format != neural_desc.Format) {
    FailRequest("unsupported_diagnostic", "model input and output have different extents or formats");
    return;
  }
  if (!PrepareCapture(device, proxy, 0, 0, static_cast<uint32_t>(desc.Width),
                      desc.Height, desc.Format, 0, generation, 0.f, true)) {
    FailRequest("allocation_failed", "model diagnostic readback buffers could not be allocated");
    return;
  }
  SetPendingDiagnosticMeta(
      std::string("path=") + path
      + "\ncomparison=model-pass-input-output\ndisplay_reference=0"
        "\nsource=model-proxy-and-neural-output\npng=rgb8-srgb-clipped-preview"
        "\nencoding=srgb\nunits=model-proxy-relative"
        "\nstage=before-look-transfer-and-game-tonemap"
        "\nexcluded=look,transfer,pedestal,game-upscale,game-tonemap,UI"
      + "\ninput_includes_previous_stack_passes=" + (pass != 0 ? "1" : "0")
      + "\nstack_pass=" + std::to_string(pass + 1)
      + "\nstack=" + std::to_string(stack)
      + "\npresent_generation=" + std::to_string(generation)
      + "\nwidth=" + std::to_string(desc.Width)
      + "\nheight=" + std::to_string(desc.Height)
      + "\nsource_format=" + std::to_string(static_cast<uint32_t>(desc.Format)) + "\n");
  D3D12_RESOURCE_BARRIER barriers[2]{};
  for (auto& barrier : barriers) {
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.Subresource = 0;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  barriers[0].Transition.pResource = proxy;
  barriers[0].Transition.StateBefore = proxy_state;
  barriers[1].Transition.pResource = neural;
  barriers[1].Transition.StateBefore = neural_state;
  command_list->ResourceBarrier(2, barriers);
  RecordPreCopy(command_list, proxy);
  RecordPostCopy(command_list, neural);
  for (auto& barrier : barriers) {
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
  }
  command_list->ResourceBarrier(2, barriers);
}

// Marks an armed or in-flight capture as dead (device loss, NR evaluate
// failure, new device). The buffers are released by the worker after the
// settle window.
inline void AbortPending() {
  std::scoped_lock lock(internal::state_mutex);
  if (internal::pending.has_value()) internal::pending->aborted = true;
  internal::capture_requested.store(false, std::memory_order_release);
}

// Called once per present with the new generation, and whether the queue
// hook's exact submission proofs are live (dlssnr's queue_tracking_active).
// Fast path when idle is a few atomic loads. Settled captures are moved to the
// worker; all heavy work happens off this thread.
inline void OnPresent(uint64_t generation, bool exact_proofs) noexcept {
  // A request without an eligible final frame must not fire unexpectedly
  // after a loading screen, long after the user pressed F5.
  if (internal::capture_requested.load(std::memory_order_acquire)
      && !internal::has_pending.load(std::memory_order_acquire)) {
    if (internal::presents_while_armed.fetch_add(1, std::memory_order_relaxed) + 1
        > internal::kArmTimeoutPresents) {
      internal::presents_while_armed.store(0, std::memory_order_relaxed);
      FailRequest("no_nr_frame", "no eligible capture frame arrived within 600 presents");
    }
  } else {
    internal::presents_while_armed.store(0, std::memory_order_relaxed);
  }
  // With exact proofs every retained set - superseded in its frame, or
  // cancelled - is released on its own proof, whatever the pending capture
  // does (a starved or cancelled capture used to keep them until a later
  // capture settled, or forever).
  if (exact_proofs && internal::retained_sets.load(std::memory_order_relaxed) != 0) {
    std::scoped_lock lock(internal::state_mutex);
    std::erase_if(internal::retired_buffers,
                  [](internal::RetiredCaptureBuffers& retired) {
                    if (!internal::ReadbacksReleasable(retired)) return false;
                    retired.Release();
                    internal::retained_sets.fetch_sub(1, std::memory_order_relaxed);
                    return true;
                  });
  }
  if (!internal::has_pending.load(std::memory_order_acquire)) return;
  std::optional<internal::PendingCapture> work;
  {
    std::scoped_lock lock(internal::state_mutex);
    if (!internal::pending.has_value()) {
      internal::has_pending.store(false, std::memory_order_release);
      return;
    }
    // A timeout cancels delivery and reports failure - it never establishes
    // GPU completion, so the buffers are not mapped and not released: they
    // move to the retired set.  A frame count cannot prove a fence.
    const auto cancel = [&](const std::string& why) {
      internal::captures_cancelled.fetch_add(1, std::memory_order_relaxed);
      static std::atomic_bool logged_cancel_timeout{false};
      if (!logged_cancel_timeout.exchange(true)) {
        internal::SafeLog(reshade::log::level::warning,
                          std::string("NR screenshot cancelled: ") + why
                              + " (buffers retained, no files written; counted"
                                " as screenshot[cancelled=])");
      }
      // The issue report names why a capture is missing (report_flow.hpp).
      internal::FinishedCapture cancelled;
      cancelled.serial = internal::pending->serial;
      cancelled.outcome = "gpu_unconfirmed";
      internal::PublishFinished(std::move(cancelled));
      internal::RetiredCaptureBuffers retained;
      retained.Take(std::move(*internal::pending));
      internal::pending.reset();
      internal::has_pending.store(false, std::memory_order_release);
      internal::retired_buffers.push_back(std::move(retained));
      internal::retained_sets.fetch_add(1, std::memory_order_relaxed);
    };
    // The capture-frame's present has occurred: no further evaluates for this
    // frame will run, so the pair already in `pending` is the LAST evaluate's
    // and is the one we keep. Stop arming further frames. (OnPresent bumps the
    // generation before it reaches here, so the capture-frame's present arrives
    // with generation > creation_generation.) If the frame never completed a
    // pair (the last evaluate failed), drop the capture.
    if (generation > internal::pending->creation_generation
        && internal::capture_requested.load(std::memory_order_acquire)) {
      internal::capture_requested.store(false, std::memory_order_release);
      if (!internal::pending->ready && !internal::pending->aborted) {
        internal::pending->aborted = true;
      }
    }
    // The proof is chosen at the capture frame's first present.  Covers
    // aborted captures too - their buffers may hold a recorded pre-copy.
    if (generation > internal::pending->creation_generation
        && !internal::pending->lease_taken) {
      internal::pending->lease_taken = true;
      internal::pending->lease_generation = generation;
      internal::pending->exact = exact_proofs;
      if (!exact_proofs) internal::pending->lease = AcquireGpuLease();
    }
    if (internal::pending->exact) {
      // Every copy was tracked on the list that recorded it: settled once no
      // recording holding a readback can still execute.  A present, or an
      // unrelated submission completing, proves nothing about that list.
      if (!internal::ReadbacksReleasable(*internal::pending)) {
        if (generation >= internal::pending->lease_generation
                              + internal::kExactSettlePresents) {
          cancel("the command list carrying the copies was not submitted,"
                 " completed and reset within "
                 + std::to_string(internal::kExactSettlePresents) + " presents");
        }
        return;
      }
    } else {
      const bool empty_lease_timeout =
          internal::pending->lease.empty()
          && generation
                 >= internal::pending->creation_generation
                        + internal::kSettlePresents;
      const bool stalled_lease_timeout =
          !internal::pending->lease.empty()
          && !GpuLeaseCompleted(internal::pending->lease)
          && generation
                 >= internal::pending->lease_generation
                        + kCaptureLeaseFallbackTicks;
      if (empty_lease_timeout || stalled_lease_timeout) {
        cancel(internal::pending->lease.empty()
                   ? "no tracked queue fence proved the copies executed before"
                     " the timeout"
                   : "a tracked queue's fence did not advance before the"
                     " timeout (an idle queue, or a present-starved session)");
        return;
      }
      if (internal::pending->lease.empty()) return;  // still waiting for fences
      if (!GpuLeaseCompleted(internal::pending->lease)) return;
    }
    if (!internal::pending->ready && !internal::pending->aborted) return;
    work = std::move(*internal::pending);
    internal::pending.reset();
    internal::has_pending.store(false, std::memory_order_release);
    // Without exact proofs, retired buffers from earlier evaluates in the
    // same frame share the frame's command list and settle together with the
    // final capture.
    if (!work->exact) {
      internal::retained_sets.fetch_sub(internal::retired_buffers.size(),
                                        std::memory_order_relaxed);
      for (internal::RetiredCaptureBuffers& retired : internal::retired_buffers) {
        retired.Release();
      }
      internal::retired_buffers.clear();
    }
  }
  internal::HandoffToWorker(std::move(work));
}

// Flag-only shutdown signal: no locks, no state release, no worker join.
// Safe from DLL_PROCESS_DETACH where blocking on a worker-owned mutex could
// stall the loader lock on an in-flight job.  The worker exits after its
// current job; any queued job's readbacks are reclaimed by the OS at process
// end (device rebuilds use AbortPending instead, which does drain state).
inline void SignalShutdown() noexcept {
  internal::capture_requested.store(false, std::memory_order_release);
  internal::shutdown_requested.store(true, std::memory_order_release);
  internal::job_signal.notify_all();
}

}  // namespace renodx::addons::dlss5::screenshot
