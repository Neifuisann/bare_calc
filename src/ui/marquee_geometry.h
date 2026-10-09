#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Pure panel-geometry helpers for the selected-row marquee. Kept free of
// Arduino/Adafruit includes so host regression tests can compile it with a
// plain mingw g++ while the firmware shares the exact same clipping math.
namespace MarqueeGeometry {

constexpr int16_t kPanelWidth = 192;
constexpr int16_t kPanelHeight = 63;
constexpr size_t kGrayscaleBytes = 192 * 63 / 4;  // 3024
constexpr size_t kMonoBytes = 192 * 63 / 8;       // 1512

// Clamp a marquee viewport to the visible panel rows. The fourth 16px chrome
// row starts at y=48, so only 15 rows remain before y=63 goes out of bounds;
// returning 15 there keeps the animation on the last visible scanline.
inline int16_t clippedViewportHeight(int16_t top, int16_t height) {
  if (height <= 0) return 0;
  int32_t t = top;
  int32_t h = height;
  if (t < 0) {
    h += t;
    t = 0;
  }
  if (t >= kPanelHeight) return 0;
  if (h > static_cast<int32_t>(kPanelHeight - t)) h = kPanelHeight - t;
  if (h <= 0) return 0;
  return static_cast<int16_t>(h);
}

inline bool destPixelInBounds(int32_t x, int32_t y) {
  return x >= 0 && x < kPanelWidth && y >= 0 && y < kPanelHeight;
}

}  // namespace MarqueeGeometry
