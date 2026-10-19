/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: MIT
 */

// Per-frame normalization trace ([RenoDX.DLSS5] NRNormTrace=1).
//
// Brightness pumping on relative-HDR sources has two origins that look the
// same on screen: the normalization divisor moving (the governor committing a
// changing autoscale candidate), and the NR model's own response changing
// while the divisor holds.  The contract log cannot tell them apart and the
// governor keeps no CPU-visible state.  Per traced evaluate this module copies
// two things the v6 chain already computes on the GPU into a readback ring:
//   - the 1x1 norm_scale texel: .r committed divisor, .g candidate,
//     .b |log2(candidate / committed)|, .a shoulder-gate fraction; under
//     feed v2 (v6_exposure_scale) .g is this frame's exposure reading, .b
//     the raw exposure texel and .a the feed state, re-mapped per row below;
//   - the block_mean surface right after the pedestal reduce: per 32x32 block,
//     .x mean luminance of the resolved NR output (pre-pedestal) and .y mean
//     luminance of the untouched linear source (work0).
// Once a completion proof lands it writes one CSV row per frame
// (RenoDX-DLSS5-normtrace.csv beside the game's ReShade.ini) and a 5 s summary line per
// workset: divisor travel, candidate flips, and the frame-to-frame change of
// the NR output/input luminance ratio - the model-side pumping measure.
//
// Diagnostic only and off by default (the feed guard probe below, one texel
// every 8 presents, is always on): the copies ride the game's command
// list, and a slot publishes on a completion proof polled from the lifecycle
// tick: the exact one - the recording that carried the copies was submitted
// and that submission completed (submission_tracker.hpp) - wherever the queue
// hooks are live, else the every-queue lease.  The lease alone starved in Alan
// Wake 2: it waits for the next signal on EVERY tracked queue, one of its four
// queues idles for seconds, and the 16-slot ring lapped every pending slot
// (rc3, 2026-09-23: 27 frames published, 18686 dropped).  Neither proof says
// the copy ran - a recording reset unsubmitted is "released" too - so Record
// stamps the slot's texel with kUnwritten and a slot still carrying it is
// counted as unexecuted, not published.  A slot whose proof never lands is
// dropped unread.  Nothing read back here feeds the image path.  Every
// function requires runtime_mutex.

#pragma once

#include <d3d12.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "gpu_lease.hpp"
#include "submission_tracker.hpp"
#include "../../../../common/event-log.hpp"

namespace renodx::addons::dlss5::norm_trace {

// Snap-reason bits carried per traced frame (PrepareFrameScale decides them).
constexpr uint32_t kSnapPrime = 1u;   // workset's first governed frame
constexpr uint32_t kSnapEpoch = 2u;   // history-reset epoch advanced
constexpr uint32_t kSnapReset = 4u;   // the game's own NGX Reset flag
constexpr uint32_t kSnapFeed = 8u;    // the workset last ran the other feed

constexpr uint32_t kSlots = 16;
// Ring capacity sized for an 8K output: ceil(7680/32) x ceil(4320/32) blocks
// of one R32G32B32A32 texel each.  Larger outputs are skipped (logged once).
constexpr uint32_t kMaxBlocksX = 240;
constexpr uint32_t kMaxBlocksY = 135;
constexpr uint64_t kPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
constexpr uint64_t kPlacementAlign = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
constexpr uint64_t kBlockOffset = kPlacementAlign;
constexpr uint64_t kSlotStride =
    ((kBlockOffset
      + ((kMaxBlocksX * 16ull + kPitchAlign - 1) / kPitchAlign) * kPitchAlign
            * kMaxBlocksY
      + kPlacementAlign - 1)
     / kPlacementAlign)
    * kPlacementAlign;
// Lifecycle ticks a slot may wait for its completion proof before it is
// dropped unread (~2 s of presents).
constexpr uint64_t kStaleTicks = 120;
// A candidate change larger than this between consecutive traced frames of
// one workset counts as a flip (the 1/32 shoulder gate engaging/releasing
// moves it by most of a stop; bin jitter moves it by 0.19).
constexpr float kFlipStops = 0.25f;
constexpr int64_t kSummaryNs = 5'000'000'000;
// Written by the CPU over a slot's texel .r at Record; the copy replaces it
// with the committed divisor, which is never negative.
constexpr float kUnwritten = -1.f;

struct Meta {
  uint64_t generation = 0;
  int64_t time_ns = 0;
  uint32_t workset = 0;
  uint32_t snap = 0;
  uint32_t feed = 0;  // FeedSource: 0 = v1, else a feed v2 texel
};

struct Slot {
  bool recorded = false;
  GpuLease lease;
  uint64_t tick = 0;
  Meta meta;
  uint32_t blocks_x = 0;
  uint32_t blocks_y = 0;
  uint32_t row_pitch = 0;
};

// Per-workset accumulators for the 5 s summary line.
struct Window {
  int64_t start_ns = 0;
  uint64_t frames = 0;
  uint32_t snaps = 0;
  uint32_t flips = 0;
  uint32_t gate_frames = 0;
  // Feed v2 rows: the feed, the raw exposure texel range, frames that held
  // (3) or defaulted (4) instead of reading a fresh scale, and frames the
  // range guard gave v1's divisor (5).
  uint32_t feed = 0;
  uint32_t held_frames = 0;
  uint32_t guard_frames = 0;
  float exposure_min = 0.f;
  float exposure_max = 0.f;
  float committed_first = 0.f;
  float committed_last = 0.f;
  float committed_min = 0.f;
  float committed_max = 0.f;
  float candidate_min = 0.f;
  float candidate_max = 0.f;
  double committed_travel = 0.0;  // sum |dlog2 committed|, stops
  double input_d2 = 0.0;          // sum (dlog2 input mean)^2
  double ratio_d2 = 0.0;          // sum (dlog2 output/input)^2
  double ratio_dmax = 0.0;
  double ratio_sum = 0.0;
  uint64_t deltas = 0;
  bool has_previous = false;
  float previous_candidate = 0.f;
  float previous_committed = 0.f;
  double previous_input_log = 0.0;
  double previous_ratio_log = 0.0;
  int64_t last_ns = 0;  // the last publish, for the stale sweep in Publish
};

namespace internal {

// Native identity, compared only - the same reasoning as gpu_timers': the
// evaluate's device face can change without the device changing, and the
// readback buffer (never proxied) may still be the target of a copy.
inline IUnknown* context_device = nullptr;
inline ID3D12Resource* readback = nullptr;
inline uint8_t* mapped = nullptr;
inline Slot slots[kSlots];
inline uint32_t head = 0;
inline uint32_t tail = 0;
inline uint64_t ticks = 0;
inline uint64_t dropped = 0;
inline uint64_t published = 0;
inline uint64_t unexecuted = 0;
inline uint64_t lease_starved = 0;
inline bool warned_stale = false;
inline bool warned_capacity = false;
inline std::filesystem::path csv_path;
inline std::ofstream csv;
inline int64_t csv_origin_ns = 0;
inline int64_t last_flush_ns = 0;
inline int64_t last_sweep_ns = 0;
inline std::map<uint32_t, Window> windows;

inline void Log(reshade::log::level level, const std::string& message) {
  const std::string line = "DLSS5 Generic: " + message;
  reshade::log::message(level, line.c_str());
  ::dlss5::eventlog::Forward(::dlss5::eventlog::kGeneric, static_cast<int>(level), line.c_str());
}

inline void Release() {
  if (mapped != nullptr) readback->Unmap(0, nullptr);
  mapped = nullptr;
  if (readback != nullptr) readback->Release();
  readback = nullptr;
  context_device = nullptr;
  for (Slot& slot : slots) slot = Slot{};
  head = 0;
  tail = 0;
}

// A persistently mapped readback buffer in COPY_DEST: the trace's ring and
// the guard probe's.
inline bool CreateMappedReadback(
    ID3D12Device* device, uint64_t bytes, ID3D12Resource** resource, uint8_t** data) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr, IID_PPV_ARGS(resource)))
      || *resource == nullptr) {
    *resource = nullptr;
    return false;
  }
  if (FAILED((*resource)->Map(0, nullptr, reinterpret_cast<void**>(data)))) {
    *data = nullptr;
    (*resource)->Release();
    *resource = nullptr;
    return false;
  }
  return true;
}

// Where the 1x1 R32G32B32A32 norm_scale texel lands in a readback buffer.
inline D3D12_TEXTURE_COPY_LOCATION TexelFootprint(ID3D12Resource* buffer, uint64_t offset) {
  D3D12_TEXTURE_COPY_LOCATION location{};
  location.pResource = buffer;
  location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  location.PlacedFootprint.Offset = offset;
  location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  location.PlacedFootprint.Footprint.Width = 1;
  location.PlacedFootprint.Footprint.Height = 1;
  location.PlacedFootprint.Footprint.Depth = 1;
  location.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(kPitchAlign);
  return location;
}

// A slot's completion proof: the submission tracker's exact one while the
// queue hooks are live, else the every-queue lease, which one idle queue
// starves (see the header).  `*lease` receives the lease's own state, so a
// caller can count the frames only the exact proof could publish.
inline GpuLeaseState SlotProof(const Slot& slot, bool exact_proofs, GpuLeaseState* lease) {
  *lease = QueryGpuLease(slot.lease);
  return exact_proofs && submission::ResourceReleasable(&slot) ? GpuLeaseState::kCompleted
                                                               : *lease;
}

inline bool Ensure(ID3D12Device* device) {
  IUnknown* const identity = renodx::utils::directx::NativeIdentity(device);
  if (context_device == identity) return readback != nullptr;
  Release();
  if (!CreateMappedReadback(device, kSlotStride * kSlots, &readback, &mapped)) return false;
  context_device = identity;
  return true;
}

inline void EmitSummary(uint32_t workset, const Window& w) {
  if (w.frames == 0) return;
  const double deltas = static_cast<double>(std::max<uint64_t>(w.deltas, 1));
  char line[640];
  const int written = std::snprintf(
      line, sizeof(line),
      "norm trace ws%u: frames=%llu divisor[last=%.3f min=%.3f max=%.3f"
      " travel=%.3fst] candidate[min=%.3f max=%.3f flips=%u gate_on=%.0f%%]"
      " snaps=%u input_d_rms=%.4fst nr_ratio[mean=%.3f d_rms=%.4fst"
      " d_max=%.4fst] dropped=%llu",
      workset, static_cast<unsigned long long>(w.frames), w.committed_last,
      w.committed_min, w.committed_max, w.committed_travel, w.candidate_min,
      w.candidate_max, w.flips,
      100.0 * w.gate_frames / static_cast<double>(w.frames), w.snaps,
      std::sqrt(w.input_d2 / deltas),
      w.ratio_sum / static_cast<double>(w.frames),
      std::sqrt(w.ratio_d2 / deltas), w.ratio_dmax,
      static_cast<unsigned long long>(dropped));
  if (w.feed != 0 && written > 0 && static_cast<size_t>(written) < sizeof(line)) {
    // Appended, so readers of the v1 grammar parse a v2 line unchanged.
    std::snprintf(
        line + written, sizeof(line) - written,
        " feed=%s exposure[min=%.6g max=%.6g] held=%u guard_v1=%u",
        w.feed == 2 ? "v2-fixed" : "v2-exposure", w.exposure_min,
        w.exposure_max, w.held_frames, w.guard_frames);
  } else if (w.held_frames != 0 && written > 0
             && static_cast<size_t>(written) < sizeof(line)) {
    // v1 frames the cut-aware governor held (unreadable; gate fraction < 0).
    std::snprintf(line + written, sizeof(line) - written, " held=%u", w.held_frames);
  }
  Log(reshade::log::level::info, line);
}

// False for a slot the copy never reached (counted in `unexecuted`).
inline bool Publish(const Slot& slot) {
  const uint64_t base = static_cast<uint64_t>(&slot - slots) * kSlotStride;
  const auto* texel = reinterpret_cast<const float*>(mapped + base);
  if (texel[0] == kUnwritten) {
    ++unexecuted;
    return false;
  }
  const bool v2 = slot.meta.feed != 0;
  const float committed = texel[0];
  const float candidate = texel[1];
  // v1 texel: .b error, .a gate fraction.  v2 texel: .b the raw exposure,
  // .a the state (1 texture, 2 fixed, 3 held, 4 defaulted, 5 range guard:
  // v1's divisor applied); its "error" is how far a fresh reading sits from
  // the applied scale (0 unless held or guarded).
  const float exposure = v2 ? texel[2] : 0.f;
  const uint32_t state = v2 ? static_cast<uint32_t>(texel[3]) : 0u;
  const float error_stops = !v2 ? texel[2]
      : candidate > 0.f && committed > 0.f
          ? std::fabs(std::log2(candidate / committed))
          : 0.f;
  const float gate_fraction = v2 ? 0.f : texel[3];

  double sum_output = 0.0;
  double sum_input = 0.0;
  double sum_log_ratio = 0.0;
  uint64_t blocks = 0;
  uint64_t ratio_blocks = 0;
  for (uint32_t y = 0; y < slot.blocks_y; ++y) {
    const auto* row = reinterpret_cast<const float*>(
        mapped + base + kBlockOffset
        + static_cast<uint64_t>(y) * slot.row_pitch);
    for (uint32_t x = 0; x < slot.blocks_x; ++x) {
      const float output = row[x * 4 + 0];
      const float input = row[x * 4 + 1];
      if (!std::isfinite(output) || !std::isfinite(input)) continue;
      sum_output += output;
      sum_input += input;
      ++blocks;
      // Blocks below the linear floor carry no usable ratio.
      if (input > 1e-4f && output > 1e-4f) {
        sum_log_ratio += std::log2(static_cast<double>(output) / input);
        ++ratio_blocks;
      }
    }
  }
  const double input_mean = blocks != 0 ? sum_input / blocks : 0.0;
  const double output_mean = blocks != 0 ? sum_output / blocks : 0.0;
  const double ratio = sum_input > 0.0 ? sum_output / sum_input : 0.0;
  const double log_ratio =
      ratio_blocks != 0 ? sum_log_ratio / ratio_blocks : 0.0;
  ++published;

  if (!csv.is_open() && !csv_path.empty()) {
    csv.open(csv_path, std::ios::trunc);
    if (csv.is_open()) {
      csv_origin_ns = slot.meta.time_ns;
      csv << "t_ms,generation,workset,snap,committed,candidate,error_stops,"
             "gate_fraction,input_mean,output_mean,nr_ratio,nr_log_ratio,"
             "feed,exposure,feed_state\n";
    } else {
      // A folder the game cannot write (Program Files, a redirected OneDrive
      // folder, ENV-12) is said once, and the open is not retried per frame.
      const auto path = csv_path.u8string();
      Log(reshade::log::level::warning,
          "normalization trace CSV could not be opened: "
              + std::string(path.begin(), path.end()) + " - no CSV this session");
      csv_path.clear();
    }
  }
  if (csv.is_open()) {
    char row[320];
    std::snprintf(
        row, sizeof(row),
        "%.3f,%llu,%u,%u,%.6f,%.6f,%.5f,%.5f,%.6g,%.6g,%.6f,%.6f,%u,%.6g,%u\n",
        (slot.meta.time_ns - csv_origin_ns) / 1e6,
        static_cast<unsigned long long>(slot.meta.generation),
        slot.meta.workset, slot.meta.snap, committed, candidate, error_stops,
        gate_fraction, input_mean, output_mean, ratio, log_ratio,
        slot.meta.feed, exposure, state);
    csv << row;
    if (slot.meta.time_ns - last_flush_ns > 1'000'000'000) {
      csv.flush();
      last_flush_ns = slot.meta.time_ns;
    }
  }

  Window& w = windows[slot.meta.workset];
  if (w.frames == 0) {
    w.start_ns = slot.meta.time_ns;
    w.committed_first = committed;
    w.committed_min = committed;
    w.committed_max = committed;
    w.candidate_min = candidate;
    w.candidate_max = candidate;
    w.exposure_min = exposure;
    w.exposure_max = exposure;
  }
  ++w.frames;
  if (slot.meta.snap != 0) ++w.snaps;
  if (gate_fraction * 32.f >= 1.f) ++w.gate_frames;
  w.feed = slot.meta.feed;
  if (state == 5u) {
    ++w.guard_frames;
  } else if (state >= 3u || gate_fraction < 0.f) {
    // v2's held/defaulted frames, or v1 frames the cut-aware governor held
    // (unreadable; v1 rows report the gate fraction negated).
    ++w.held_frames;
  }
  w.exposure_min = std::min(w.exposure_min, exposure);
  w.exposure_max = std::max(w.exposure_max, exposure);
  w.committed_last = committed;
  w.committed_min = std::min(w.committed_min, committed);
  w.committed_max = std::max(w.committed_max, committed);
  w.candidate_min = std::min(w.candidate_min, candidate);
  w.candidate_max = std::max(w.candidate_max, candidate);
  w.ratio_sum += ratio;
  const double input_log = std::log2(std::max(input_mean, 1e-9));
  if (w.has_previous) {
    if (candidate > 0.f && w.previous_candidate > 0.f
        && std::fabs(std::log2(candidate / w.previous_candidate)) > kFlipStops) {
      ++w.flips;
    }
    if (committed > 0.f && w.previous_committed > 0.f) {
      w.committed_travel += std::fabs(std::log2(committed / w.previous_committed));
    }
    const double input_delta = input_log - w.previous_input_log;
    const double ratio_delta = log_ratio - w.previous_ratio_log;
    w.input_d2 += input_delta * input_delta;
    w.ratio_d2 += ratio_delta * ratio_delta;
    w.ratio_dmax = std::max(w.ratio_dmax, std::fabs(ratio_delta));
    ++w.deltas;
  }
  w.has_previous = true;
  w.previous_candidate = candidate;
  w.previous_committed = committed;
  w.previous_input_log = input_log;
  w.previous_ratio_log = log_ratio;
  if (slot.meta.time_ns - w.start_ns >= kSummaryNs) {
    EmitSummary(slot.meta.workset, w);
    // Keep the frame-to-frame reference across windows; restart the rest.
    const Window carry = w;
    w = Window{};
    w.has_previous = true;
    w.previous_candidate = carry.previous_candidate;
    w.previous_committed = carry.previous_committed;
    w.previous_input_log = carry.previous_input_log;
    w.previous_ratio_log = carry.previous_ratio_log;
  }
  w.last_ns = slot.meta.time_ns;
  // Windows of worksets that stopped publishing (retired, evicted, idle) are
  // summarized and dropped: workset ids only grow, so a churning title traced
  // for hours kept one window per workset it ever built (LIFE-14).
  if (slot.meta.time_ns - last_sweep_ns >= kSummaryNs) {
    last_sweep_ns = slot.meta.time_ns;
    for (auto it = windows.begin(); it != windows.end();) {
      if (slot.meta.time_ns - it->second.last_ns < 2 * kSummaryNs) {
        ++it;
        continue;
      }
      EmitSummary(it->first, it->second);
      it = windows.erase(it);
    }
  }
  return true;
}

}  // namespace internal

// ---- The feed guard probe (always on, since v7.0.0-rc11) -------------------
// Auto's v2 range guard decides on the GPU (v6_exposure_scale), so without
// NRNormTrace a session that fed v1's divisor on every frame read exactly
// like one that followed the game's exposure: feed[texture=] counts both.
// The probe copies the norm_scale texel the trace reads (.r the divisor
// applied, .g the divisor the game's texel implied, .b the raw texel, .a the
// state, 5 = guarded) from one guarded evaluate every kEvery presents, never
// more than one per present, into its own four-slot readback, and publishes
// it on the trace's completion proof (SlotProof).  Counted for the telemetry
// line's feed[] block: samples read (guard_sampled), samples the guard fed
// v1's divisor (guard_v1), engage transitions (guard_engaged), and samples
// lost (guard_lost: lapped, stale, never executed, or no readback) - a probe
// that could not see is not a probe that saw nothing.  A workset's engage
// and clear are one log line each, "NR feed guard: engaged|cleared on ws<N>",
// at most kMaxLines per session.  Nothing read here feeds the image path.
namespace guard_probe {

constexpr uint32_t kSlots = 4;
constexpr uint64_t kEvery = 8;
constexpr uint32_t kMaxLines = 16;

struct WorksetState {
  bool guarded = false;
  uint64_t run = 0;  // consecutive samples in this state
};

inline IUnknown* context_device = nullptr;
inline ID3D12Resource* readback = nullptr;
inline uint8_t* mapped = nullptr;
inline Slot slots[kSlots];
inline uint32_t head = 0;
inline uint32_t tail = 0;
inline uint64_t ticks = 0;
inline uint64_t next_generation = 0;
inline uint64_t sampled = 0;
inline uint64_t guarded = 0;
inline uint64_t engaged = 0;
inline uint64_t lost = 0;
inline uint32_t lines = 0;
inline std::map<uint32_t, WorksetState> worksets;

inline void Release() {
  if (mapped != nullptr) readback->Unmap(0, nullptr);
  mapped = nullptr;
  if (readback != nullptr) readback->Release();
  readback = nullptr;
  context_device = nullptr;
  for (Slot& slot : slots) slot = Slot{};
  head = 0;
  tail = 0;
  worksets.clear();
}

// One read-back norm_scale texel of a guarded frame (API-agnostic: the
// Vulkan path's readout publishes its guarded samples here too).
inline void Publish(const float* texel, uint32_t workset) {
  if (texel[0] == kUnwritten) {
    ++lost;
    return;
  }
  ++sampled;
  const bool now = static_cast<uint32_t>(texel[3]) == 5u;
  if (now) ++guarded;
  WorksetState& ws = worksets[workset];
  const bool changed = now != ws.guarded;
  const uint64_t previous_run = ws.run;
  ws.guarded = now;
  ws.run = changed ? 1 : ws.run + 1;
  if (!changed) return;
  if (now) ++engaged;
  if (lines > kMaxLines) return;
  char line[400];
  if (lines++ == kMaxLines) {
    std::snprintf(line, sizeof(line),
                  "NR feed guard: further engage/clear lines are not logged;"
                  " the telemetry's feed[guard_engaged=] keeps counting");
  } else if (now && texel[1] > 0.f) {
    std::snprintf(
        line, sizeof(line),
        "NR feed guard: engaged on ws%u - the game's exposure texel %.6g sets"
        " divisor %.6g, the frame's own meter %.6g (%.1f stops apart); NR uses"
        " the meter until the texel fits the frame",
        workset, texel[2], texel[1], texel[0],
        texel[0] > 0.f ? std::fabs(std::log2(texel[1] / texel[0])) : 0.f);
  } else if (now) {
    std::snprintf(
        line, sizeof(line),
        "NR feed guard: engaged on ws%u - the game's exposure texel %.6g gives"
        " no usable divisor; NR uses the frame's own meter (%.6g)",
        workset, texel[2], texel[0]);
  } else {
    std::snprintf(
        line, sizeof(line),
        "NR feed guard: cleared on ws%u after %llu guarded sample%s - the game's"
        " exposure texel %.6g (divisor %.6g) fits the frame again; NR follows"
        " the game's exposure",
        workset, static_cast<unsigned long long>(previous_run),
        previous_run == 1 ? "" : "s", texel[2], texel[0]);
  }
  internal::Log(now ? reshade::log::level::warning : reshade::log::level::info, line);
}

// PrepareFrameScale, on a guarded frame, after v6_exposure_scale wrote the
// texel: samples it if a sample is due at this present.
inline void Record(
    ID3D12GraphicsCommandList* list,
    ID3D12Device* device,
    ID3D12Resource* norm_scale,
    D3D12_RESOURCE_STATES norm_scale_state,
    uint32_t workset,
    uint64_t generation) {
  if (generation < next_generation || norm_scale == nullptr) return;
  next_generation = generation + kEvery;
  IUnknown* const identity = renodx::utils::directx::NativeIdentity(device);
  if (context_device != identity) {
    Release();
    if (!internal::CreateMappedReadback(device, kSlots * kPlacementAlign, &readback, &mapped)) {
      ++lost;
      return;
    }
    context_device = identity;
  }
  Slot& slot = slots[head];
  if (slot.recorded) {
    // Lapping an unpublished sample: drop it rather than overwrite a region
    // the GPU may still be copying into.
    ++lost;
    if (tail == head) tail = (tail + 1) % kSlots;
  }
  const uint64_t base = static_cast<uint64_t>(head) * kPlacementAlign;
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = norm_scale;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = norm_scale_state;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);
  const D3D12_TEXTURE_COPY_LOCATION destination = internal::TexelFootprint(readback, base);
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = norm_scale;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
  list->ResourceBarrier(1, &barrier);
  // As in norm_trace::Record: the slot is its own tracked object, and the CPU
  // stamp lands before this list can be submitted.
  reinterpret_cast<float*>(mapped + base)[0] = kUnwritten;
  submission::TrackUse(list, &slot);
  slot.lease = AcquireGpuLease();
  slot.tick = ticks;
  slot.meta = Meta{};
  slot.meta.generation = generation;
  slot.meta.workset = workset;
  slot.recorded = true;
  head = (head + 1) % kSlots;
}

inline void Poll(bool exact_proofs) {
  ++ticks;
  uint32_t scanned = 0;
  while (tail != head && scanned++ < kSlots) {
    Slot& slot = slots[tail];
    if (slot.recorded) {
      GpuLeaseState lease = GpuLeaseState::kPending;
      const GpuLeaseState state = internal::SlotProof(slot, exact_proofs, &lease);
      if (state == GpuLeaseState::kPending && ticks - slot.tick < kStaleTicks) return;
      if (state == GpuLeaseState::kCompleted) {
        Publish(reinterpret_cast<const float*>(
                    mapped + static_cast<uint64_t>(&slot - slots) * kPlacementAlign),
                slot.meta.workset);
      } else {
        ++lost;
      }
      slot.recorded = false;
    }
    tail = (tail + 1) % kSlots;
  }
}

}  // namespace guard_probe

inline void Configure(const std::filesystem::path& csv_path) {
  internal::csv_path = csv_path;
}

// Records one traced frame onto `list` right after the commit dispatch: the
// governor texel and the pedestal-reduce block means.  `norm_scale_state` is
// the texel's tracked state (restored afterwards); block_mean lives in UAV.
inline void Record(
    ID3D12GraphicsCommandList* list,
    ID3D12Device* device,
    ID3D12Resource* norm_scale,
    D3D12_RESOURCE_STATES norm_scale_state,
    ID3D12Resource* block_mean,
    uint32_t width,
    uint32_t height,
    const Meta& meta) {
  using internal::slots;
  const uint32_t blocks_x = (width + 31) / 32;
  const uint32_t blocks_y = (height + 31) / 32;
  if (blocks_x > kMaxBlocksX || blocks_y > kMaxBlocksY) {
    if (!internal::warned_capacity) {
      internal::warned_capacity = true;
      internal::Log(reshade::log::level::warning,
                    "norm trace: output larger than 8K; frames not traced");
    }
    return;
  }
  if (norm_scale == nullptr || block_mean == nullptr
      || !internal::Ensure(device)) {
    return;
  }
  Slot& slot = slots[internal::head];
  if (slot.recorded) {
    // Lapping an undrained slot: drop the oldest rather than overwrite a
    // region the GPU may still be copying into.
    slot.recorded = false;
    ++internal::dropped;
    if (internal::tail == internal::head) {
      internal::tail = (internal::tail + 1) % kSlots;
    }
  }
  const uint64_t base = static_cast<uint64_t>(internal::head) * kSlotStride;
  const uint32_t row_pitch = static_cast<uint32_t>(
      ((blocks_x * 16ull + kPitchAlign - 1) / kPitchAlign) * kPitchAlign);

  D3D12_RESOURCE_BARRIER barriers[2]{};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = norm_scale;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[0].Transition.StateBefore = norm_scale_state;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[1] = barriers[0];
  barriers[1].Transition.pResource = block_mean;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  list->ResourceBarrier(2, barriers);

  D3D12_TEXTURE_COPY_LOCATION destination = internal::TexelFootprint(internal::readback, base);
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = norm_scale;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  destination.PlacedFootprint.Offset = base + kBlockOffset;
  destination.PlacedFootprint.Footprint.Width = blocks_x;
  destination.PlacedFootprint.Footprint.Height = blocks_y;
  destination.PlacedFootprint.Footprint.RowPitch = row_pitch;
  source.pResource = block_mean;
  list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
  std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
  list->ResourceBarrier(2, barriers);

  // The slot is its own tracked object: its address never moves, and a
  // lapped slot's earlier recording simply keeps it unreleased a little
  // longer.  The CPU stamp lands before this list can be submitted.
  reinterpret_cast<float*>(internal::mapped + base)[0] = kUnwritten;
  submission::TrackUse(list, &slot);
  slot.lease = AcquireGpuLease();
  slot.tick = internal::ticks;
  slot.meta = meta;
  slot.blocks_x = blocks_x;
  slot.blocks_y = blocks_y;
  slot.row_pitch = row_pitch;
  slot.recorded = true;
  internal::head = (internal::head + 1) % kSlots;
}

// Lifecycle-tick side, after DrainRetiredResources pruned the tracker:
// publishes slots whose completion proof landed, in order, and drops slots
// whose proof can never land.  `exact_proofs`: the queue hooks are live, so
// the tracker's release of a slot is the proof (queue_tracking_active).  The
// guard probe's ring is polled first, trace or no trace.
inline void Poll(bool exact_proofs) {
  guard_probe::Poll(exact_proofs);
  using internal::slots;
  ++internal::ticks;
  uint32_t scanned = 0;
  while (internal::tail != internal::head && scanned++ < kSlots) {
    Slot& slot = slots[internal::tail];
    if (!slot.recorded) {
      internal::tail = (internal::tail + 1) % kSlots;
      continue;
    }
    GpuLeaseState lease = GpuLeaseState::kPending;
    const GpuLeaseState state = internal::SlotProof(slot, exact_proofs, &lease);
    if (state == GpuLeaseState::kPending) {
      if (internal::ticks - slot.tick < kStaleTicks) return;
      ++internal::dropped;
      if (!internal::warned_stale) {
        internal::warned_stale = true;
        internal::Log(
            reshade::log::level::warning,
            "norm trace: a slot's completion proof never landed (its list was"
            " neither submitted nor reset, or the queue hooks are not live and"
            " a tracked queue idles); dropping it unread");
      }
    } else if (state == GpuLeaseState::kCompleted) {
      // Counted: the frames only the exact proof could publish.
      if (internal::Publish(slot) && lease == GpuLeaseState::kPending) {
        ++internal::lease_starved;
      }
    }
    slot.recorded = false;
    internal::tail = (internal::tail + 1) % kSlots;
  }
}

inline uint64_t Published() { return internal::published; }
inline uint64_t Dropped() { return internal::dropped; }
inline uint64_t Unexecuted() { return internal::unexecuted; }
inline uint64_t LeaseStarved() { return internal::lease_starved; }

// Device teardown / shutdown: pending slots are abandoned (their device is
// going away) and the CSV is flushed.  The guard probe's counters are
// session totals and survive.
inline void ReleaseAll() {
  guard_probe::Release();
  internal::Release();
  internal::windows.clear();
  if (internal::csv.is_open()) internal::csv.flush();
}

}  // namespace renodx::addons::dlss5::norm_trace
