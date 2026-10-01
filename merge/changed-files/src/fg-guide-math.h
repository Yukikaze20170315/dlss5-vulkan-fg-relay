#pragma once
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>

// The NGX effective rectangle must fit the image, including an explicit zero
// size (which is a transition, not an implicit whole-image declaration).
inline bool FgGuideRectValid(uint32_t texture_w, uint32_t texture_h,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    return w != 0 && h != 0 && texture_w <= INT_MAX && texture_h <= INT_MAX &&
        x <= texture_w && y <= texture_h && w <= texture_w - x && h <= texture_h - y;
}

// Streamline supplies MV * scale in pixels of the ORIGINAL effective MV grid.
// Nearest changes the grid but not sampled vector values. Preserve normalized
// displacement; Generic applies its own NR work-resolution mapping afterwards.
inline bool FgGuideScale(float input, uint32_t original_grid, uint32_t output_grid, float *result)
{
    if (!std::isfinite(input) || original_grid == 0 || output_grid == 0) return false;
    const double value = static_cast<double>(input) * output_grid / original_grid;
    if (value > FLT_MAX || value < -FLT_MAX) return false;
    *result = static_cast<float>(value);
    return std::isfinite(*result);
}

// Integer nearest uses (2*x+1)*source_width. Bound each axis so every product
// fits uint32, and stay within Vulkan's guaranteed storage-buffer range.
inline bool FgGuideBufferBytes(uint32_t w, uint32_t h, uint64_t *bytes)
{
    if (w == 0 || h == 0 || w > 32768 || h > 32768) return false;
    *bytes = static_cast<uint64_t>(w) * h * 4;
    return *bytes <= 128ull * 1024 * 1024;
}
