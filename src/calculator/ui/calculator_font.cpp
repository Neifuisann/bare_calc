#include "calculator/ui/calculator_font.h"

#include <Adafruit_GFX.h>

#include <algorithm>

// The one translation unit that includes the generated tables, so they are
// never duplicated in flash.
#include "calculator/assets/font_calculator_math.h"

namespace Calculator {
namespace MathFont {
namespace {
constexpr size_t kCount = sizeof(CalculatorFontData::kGlyphs) / sizeof(CalculatorFontData::kGlyphs[0]);
}
const Glyph* begin() { return CalculatorFontData::kGlyphs; }
const Glyph* end() { return CalculatorFontData::kGlyphs + kCount; }
const Glyph* find(uint32_t codepoint, Style style, uint8_t variant) {
  const uint32_t wanted = key(codepoint, style, variant);
  const Glyph* it = std::lower_bound(begin(), end(), wanted,
                                     [](const Glyph& g, uint32_t k) { return g.key < k; });
  return it != end() && it->key == wanted ? it : nullptr;
}
uint8_t variants(uint32_t codepoint) {
  uint8_t count = 0;
  while (find(codepoint, Style::Normal, static_cast<uint8_t>(count + 1))) ++count;
  return count;
}
bool pixel(const Glyph& glyph, int x, int y) {
  if (x < 0 || y < 0 || x >= glyph.width || y >= glyph.height) return false;
  const size_t bit = static_cast<size_t>(y) * glyph.width + x;
  return CalculatorFontData::kBitmaps[glyph.offset + (bit >> 3)] & (0x80 >> (bit & 7));
}
void draw(GFXcanvas1& canvas, const Glyph& glyph, int16_t penX, int16_t baseline, uint16_t ink) {
  for (int y = 0; y < glyph.height; ++y)
    for (int x = 0; x < glyph.width; ++x)
      if (pixel(glyph, x, y)) canvas.drawPixel(penX + glyph.xOffset + x, baseline + glyph.yOffset + y, ink);
}
}  // namespace MathFont
}  // namespace Calculator
