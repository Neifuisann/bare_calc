#pragma once
#include <cstdint>

class GFXcanvas1;

// Glyph provider for Calculate's natural display: the ClassWiz face at its
// native 14 px scale plus the font's own script (`ssty`) glyphs and vertical
// operator variants, baked by tools/bake_calculator_font.py into
// font_calculator_math.h. Menus keep using ClassWiz15 through UiKit; this
// provider exists because a GFXfont cannot express script styles, variants or
// codepoints above 0xFF.
//
// Every glyph records its advance separately from its tight ink box: layout
// advances by `advance` and clips/collides on the ink box. find() returns
// nullptr for anything that was not baked -- there is no silent fallback to
// another font or style; the caller decides (see calculator_layout.cpp).
namespace Calculator {
namespace MathFont {
enum class Style : uint8_t { Normal, Script };
struct Glyph {
  uint32_t key;       // codepoint | style << 21 | variant << 24
  uint16_t offset;    // into the packed bitmap (row-major, MSB first, no row padding)
  uint8_t width, height, advance;
  int8_t xOffset, yOffset;  // ink box relative to the pen and the baseline
  // Horizontal ink extents relative to the pen, as the half-open range
  // [inkLeft, inkRight). The bake trims every bitmap to its ink, so these are
  // exactly the painted columns -- derived, not stored, so they cost no flash
  // and cannot drift from the bitmap. Ink may start left of the pen or end
  // past `advance`; an empty glyph (space) is [0, 0).
  constexpr int inkLeft() const { return width ? xOffset : 0; }
  constexpr int inkRight() const { return width ? xOffset + width : 0; }
};
constexpr uint32_t key(uint32_t codepoint, Style style, uint8_t variant = 0) {
  return codepoint | static_cast<uint32_t>(style) << 21 | static_cast<uint32_t>(variant) << 24;
}
const Glyph* find(uint32_t codepoint, Style style, uint8_t variant = 0);
// Number of baked vertical variants above the base glyph (0 when none).
uint8_t variants(uint32_t codepoint);
bool pixel(const Glyph& glyph, int x, int y);  // ink-box coordinates
void draw(GFXcanvas1& canvas, const Glyph& glyph, int16_t penX, int16_t baseline, uint16_t ink = 1);
// Whole baked table, for the font test.
const Glyph* begin();
const Glyph* end();
}  // namespace MathFont
}  // namespace Calculator
