#pragma once
#include <cstdint>

// At most one FG-only reset may be ignored for a visible-window focus edge.
// Native SR resets, discontinuities and unknown/stale SR observations keep
// their reset semantics. This policy does not modify the game's FG parameters.
struct FgFocusHistoryPolicy {
    bool observed = false, previous_focus = false, previous_eligible = false, available = false;
    std::uint64_t edge_tick = 0;
    bool Keep(std::uint64_t now, bool focus, bool eligible, bool fg_reset,
              bool discontinuity, std::uint64_t source_tick, std::uint64_t source_reset_tick) {
        if (observed && eligible && previous_eligible && focus != previous_focus) {
            edge_tick = now;
            available = true;
        }
        observed = true; previous_focus = focus; previous_eligible = eligible;
        if (!eligible || discontinuity || (available && now - edge_tick > 3000)) available = false;
        if (!fg_reset || !available) return false;
        available = false;
        return source_tick != 0 && now >= source_tick && now - source_tick <= 250 &&
            (source_reset_tick == 0 || (now >= source_reset_tick && now - source_reset_tick > 3000));
    }
};
