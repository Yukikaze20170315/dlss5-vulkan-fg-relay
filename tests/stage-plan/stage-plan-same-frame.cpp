#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

// Built with /I <src/addons/dlss5>; see run.cmd.
#include "stage_plan.hpp"

namespace stages = renodx::addons::dlss5::stages;

void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void CheckPlan(stages::Plan plan) {
  for (uint32_t stage = 0; stage < 3; ++stage) {
    const auto point = static_cast<stages::Point>(stage);
    Check(plan.Count(point) <= stages::kPerStage, "count out of range");
  }
  Check(plan.Total() <= stages::kChannels, "total exceeds channels");
  Check(plan.Offset(stages::Point::Render) == 0, "Render offset mismatch");
  Check(plan.Offset(stages::Point::Upscaled) == plan.Count(stages::Point::Render),
        "Upscaled offset mismatch");
  Check(plan.Offset(stages::Point::Present)
            == plan.Count(stages::Point::Render) + plan.Count(stages::Point::Upscaled),
        "Present offset mismatch");
}

int main() {
  uint32_t combinations = 0;
  for (uint32_t render = 0; render <= 4; ++render) {
    for (uint32_t upscaled = 0; upscaled <= 4; ++upscaled) {
      for (uint32_t present = 0; present <= 4; ++present) {
        const stages::Plan frame_plan = stages::Plan{}
            .With(stages::Point::Render, render)
            .With(stages::Point::Upscaled, upscaled)
            .With(stages::Point::Present, present);
        Check(frame_plan.Valid(), "enumerated plan rejected by production Valid()");
        CheckPlan(frame_plan);
        stages::configuration.store(frame_plan.packed, std::memory_order_release);

        {
          const stages::Scope frame(stages::Point::Render);
          Check(stages::Current().packed == frame_plan.packed, "frame snapshot mismatch");
          Check(stages::Count() == render, "Render count mismatch");
          Check(stages::Channel(0) == 0, "Render first channel mismatch");

          stages::configuration.store(stages::Plan{}.packed, std::memory_order_release);
          {
            const stages::Scope nested_upscaled(stages::Point::Upscaled);
            Check(stages::Current().packed == frame_plan.packed,
                  "nested Upscaled scope observed mid-frame reconfiguration");
            Check(stages::Count() == upscaled, "Upscaled count mismatch");
            Check(stages::Channel(0) == render, "Upscaled channel offset mismatch");
            {
              const stages::Scope nested_present(stages::Point::Present);
              Check(stages::Current().packed == frame_plan.packed,
                    "nested Present scope observed mid-frame reconfiguration");
              Check(stages::Count() == present, "Present count mismatch");
              Check(stages::Channel(0) == render + upscaled,
                    "Present channel offset mismatch");
            }
            Check(stages::Current().packed == frame_plan.packed,
                  "nested Present scope did not restore Upscaled frame plan");
          }
          Check(stages::Current().packed == frame_plan.packed,
                "nested Upscaled scope did not restore frame plan");

          const stages::Plan carrier_plan{stages::Plan{}
              .With(stages::Point::Render, 4)
              .With(stages::Point::Upscaled, 1)
              .With(stages::Point::Present, 3).packed};
          {
            const stages::Scope carrier(stages::Point::Present, carrier_plan);
            Check(stages::Current().packed == carrier_plan.packed,
                  "bridge carrier did not bind its supplied plan snapshot");
            Check(stages::Count() == 3 && stages::Channel(0) == 5,
                  "bridge carrier stage mapping mismatch");
          }
          Check(stages::Current().packed == frame_plan.packed,
                "carrier scope did not restore enclosing frame plan");
        }
        Check(stages::Current().packed == stages::Snapshot().packed,
              "outer frame scope did not return to latest configuration");
        ++combinations;
      }
    }
  }

  const stages::Plan pinned{stages::Plan{}
      .With(stages::Point::Render, 2)
      .With(stages::Point::Upscaled, 4)
      .With(stages::Point::Present, 1).packed};
  stages::configuration.store(pinned.packed, std::memory_order_release);
  std::atomic_bool ready{false};
  std::atomic_bool done{false};
  std::thread writer([&] {
    ready.store(true, std::memory_order_release);
    while (!done.load(std::memory_order_acquire)) {
      stages::configuration.store(stages::Plan{}.packed, std::memory_order_release);
      stages::configuration.store(pinned.packed, std::memory_order_release);
    }
  });
  while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
  {
    const stages::Scope frame(stages::Point::Render);
    for (uint32_t i = 0; i < 10000; ++i) {
      const auto point = static_cast<stages::Point>(i % 3);
      const stages::Scope nested(point);
      Check(stages::Current().packed == pinned.packed,
            "concurrent configuration writer split one frame's plan");
    }
  }
  done.store(true, std::memory_order_release);
  writer.join();
  std::cout << "STAGE PLAN SAME-FRAME TEST: " << combinations
            << " legal plans, nested scopes, explicit carrier plan and concurrent writer passed\n";
}
