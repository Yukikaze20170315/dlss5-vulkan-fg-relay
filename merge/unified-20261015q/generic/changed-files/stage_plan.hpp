#pragma once
#include <atomic>
#include <cstdint>

namespace renodx::addons::dlss5::stages {
enum class Point : uint32_t { Render, Upscaled, Present };
constexpr uint32_t kPerStage = 4;
constexpr uint32_t kChannels = 12;
struct Plan {
  uint32_t packed = 1u << 3;
  constexpr uint32_t Count(Point stage) const {
    return (packed >> (3u * static_cast<uint32_t>(stage))) & 7u;
  }
  constexpr uint32_t Offset(Point stage) const {
    return stage == Point::Render ? 0u : Count(Point::Render)
        + (stage == Point::Present ? Count(Point::Upscaled) : 0u);
  }
  constexpr uint32_t Total() const {
    return Count(Point::Render) + Count(Point::Upscaled) + Count(Point::Present);
  }
  constexpr bool Valid() const {
    return (packed & ~511u) == 0 && Count(Point::Render) <= kPerStage
        && Count(Point::Upscaled) <= kPerStage && Count(Point::Present) <= kPerStage;
  }
  constexpr Plan With(Point stage, uint32_t count) const {
    const uint32_t shift = 3u * static_cast<uint32_t>(stage);
    return {(packed & ~(7u << shift)) | (count << shift)};
  }
};
inline std::atomic_uint32_t configuration{Plan{}.packed};
}
#include "layer_trial.hpp"
namespace renodx::addons::dlss5::stages {
inline Plan Snapshot() { return trial::Snapshot(); }
inline thread_local Plan active_plan;
inline thread_local Point active_point = Point::Upscaled;
inline thread_local uint32_t scope_depth = 0;
struct Scope {
  Plan previous_plan = active_plan;
  Point previous_point = active_point;
  explicit Scope(Point point) {
    if (scope_depth++ == 0) active_plan = Snapshot();
    active_point = point;
  }
  Scope(Point point, Plan plan) : Scope(point) { active_plan = plan; }
  ~Scope() { active_plan = previous_plan; active_point = previous_point; --scope_depth; }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
inline Plan Current() { return scope_depth != 0 ? active_plan : Snapshot(); }
inline uint32_t Count() { return Current().Count(active_point); }
inline uint32_t Channel(uint32_t local) { return Current().Offset(active_point) + local; }
}
