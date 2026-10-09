#pragma once
#include <Adafruit_GFX.h>
#include <array>
#include <algorithm>

// Home-compatible row-major pixels: x0 occupies bits 7:6, x3 bits 1:0.
// Own this in static/member storage, never as a task-local framebuffer.
class GrayCanvas : public Adafruit_GFX {
 public:
  static constexpr int16_t kW = 192, kH = 63;
  static constexpr size_t kBytes = 3024;
  GrayCanvas() : Adafruit_GFX(kW, kH) {}
  void clear(uint8_t level = 0) { if (level <= 3) pixels_.fill(level * 0x55); }
  void set(int16_t x, int16_t y, uint8_t level) {
    if (x < 0 || y < 0 || x >= kW || y >= kH || level > 3) return;
    const size_t p = size_t(y) * kW + x;
    const uint8_t shift = 6 - 2 * (p & 3);
    pixels_[p >> 2] = (pixels_[p >> 2] & ~(3 << shift)) | (level << shift);
  }
  void drawPixel(int16_t x, int16_t y, uint16_t level) override {
    if (level <= 3) set(x, y, static_cast<uint8_t>(level));
  }
  uint8_t pixel(int16_t x, int16_t y) const {
    if (x < 0 || y < 0 || x >= kW || y >= kH) return 0;
    const size_t p = size_t(y) * kW + x;
    return (pixels_[p >> 2] >> (6 - 2 * (p & 3))) & 3;
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t level) override {
    if (w <= 0 || h <= 0 || level > 3) return;
    const int right = std::min<int>(kW, int(x) + w), bottom = std::min<int>(kH, int(y) + h);
    for (int yy = std::max<int>(0, y); yy < bottom; ++yy)
      for (int xx = std::max<int>(0, x); xx < right; ++xx) set(xx, yy, level);
  }
  // MSB-first rows, padded to a byte; zero bits are transparent.
  void bits(const uint8_t* rows, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
    if (!rows || w <= 0 || h <= 0 || level > 3) return;
    const size_t stride = (size_t(w) + 7) / 8;
    for (int yy = std::max<int>(0, -int(y)); yy < std::min<int>(h, kH-int(y)); ++yy)
      for (int xx = std::max<int>(0, -int(x)); xx < std::min<int>(w, kW-int(x)); ++xx)
        if (rows[size_t(yy)*stride+xx/8] & (0x80 >> (xx%8))) set(x+xx, y+yy, level);
  }
  void blit1bpp(const GFXcanvas1& source, uint8_t level = 3) {
    for (int y = 0; y < std::min<int>(kH, source.height()); ++y)
      for (int x = 0; x < std::min<int>(kW, source.width()); ++x)
        if (source.getPixel(x,y)) set(x,y,level);
  }
  const std::array<uint8_t, kBytes>& packed() const { return pixels_; }
  // The same layout as a decoded 192x63 grayscale page, so one unpacks in place.
  uint8_t* data() { return pixels_.data(); }
 private:
  std::array<uint8_t, kBytes> pixels_{};
};
