// Calculator math font: the grid-exact bake of ClassWizMathCW (P6), its
// script/variant coverage, the ink-extent contract, and calculator_metrics.h
// against what the bake measured. tools/check.ps1 also pins both generated
// font headers by hash.
//
// Font profiles: the calculator math profile is font_calculator_math.h (this
// test + the bake gate); the menu chrome profile is font_classwiz15.h, a
// separate FreeType-era face with hand-curved glyphs, held byte-for-byte by
// its hash in tools/check.ps1. The two deliberately differ -- nothing here
// compares their pixels.
#include "calculator/ui/calculator_font.h"
#include "calculator/ui/calculator_metrics.h"
#include "calculator/assets/font_calculator_math.h"

#include <Adafruit_GFX.h>

#include <fstream>
#include <iostream>
#include <set>
#include <string>

using namespace Calculator;
using MathFont::Style;

namespace {
int failures = 0;
void check(const std::string& name, bool ok) {
  if (!ok) {std::cout << "FAIL  " << name << "\n"; ++failures;}
}
std::string label(uint32_t cp) { return "U+" + std::to_string(cp); }
}  // namespace

// Every baked glyph on one sheet, 16 per row in 16x44 cells with the baseline
// at row 32, as a P1 PBM for eyeballing against the reference photos.
void writeSheet(const std::string& path) {
  const int count = static_cast<int>(MathFont::end() - MathFont::begin()), rows = (count + 15) / 16;
  GFXcanvas1 sheet(256, rows * 44);
  sheet.fillScreen(0);
  int i = 0;
  for (const auto* g = MathFont::begin(); g != MathFont::end(); ++g, ++i) MathFont::draw(sheet, *g, (i % 16) * 16 + 2, (i / 16) * 44 + 32);
  std::ofstream out(path, std::ios::binary);
  out << "P1\n256 " << rows * 44 << "\n";
  for (int y = 0; y < rows * 44; ++y) {for (int x = 0; x < 256; ++x) out << (sheet.getPixel(x, y) ? '1' : '0'); out << '\n';}
}

int main(int argc, char** argv) {
  if (argc > 1) writeSheet(argv[1]);
  // Sorted, contiguous, in-bounds table, and every bitmap trimmed to its ink:
  // tightness is what makes inkLeft()/inkRight() the painted columns.
  const MathFont::Glyph* previous = nullptr;
  size_t expectedOffset = 0;
  for (const auto* g = MathFont::begin(); g != MathFont::end(); ++g) {
    const std::string at = std::to_string(g->key);
    if (previous) check("keys sorted at " + at, previous->key < g->key);
    check("bitmap contiguous at " + at, g->offset == expectedOffset);
    expectedOffset += (static_cast<size_t>(g->width) * g->height + 7) / 8;
    previous = g;
    if (!g->width || !g->height) {
      check("empty glyph has no ink box at " + at, g->width == 0 && g->height == 0 && g->inkLeft() == 0 && g->inkRight() == 0);
      continue;
    }
    bool top = false, bottom = false, left = false, right = false;
    for (int x = 0; x < g->width; ++x) {top |= MathFont::pixel(*g, x, 0); bottom |= MathFont::pixel(*g, x, g->height - 1);}
    for (int y = 0; y < g->height; ++y) {left |= MathFont::pixel(*g, 0, y); right |= MathFont::pixel(*g, g->width - 1, y);}
    check("bitmap trimmed to ink at " + at, top && bottom && left && right);
    check("ink extents span the bitmap at " + at, g->inkLeft() == g->xOffset && g->inkRight() == g->xOffset + g->width);
  }
  check("bitmap fully referenced", expectedOffset == sizeof(CalculatorFontData::kBitmaps));
  check("glyph count covers the calculator (>= 240)", MathFont::end() - MathFont::begin() >= 240);

  // Every ASCII symbol the layout can ask for is baked in normal style.
  for (int c = 0x21; c <= 0x7E; ++c)
    if (c != 0x60) check(std::string("normal glyph present: ") + char(c), MathFont::find(c, Style::Normal) != nullptr);  // no U+0060 in the OTF

  // L-01: digits are the outline grid, not FreeType's stretch -- normal 8x12
  // (the '1' 5x12) on an 11 advance; script is the `ssty` glyph, 5x9 (3x9) on
  // a 6 advance. Bottom row sits just above the baseline in both styles.
  for (char c = '0'; c <= '9'; ++c) {
    const auto* n = MathFont::find(c, Style::Normal);
    const auto* s = MathFont::find(c, Style::Script);
    const std::string d(1, c);
    check("normal digit " + d + " is 8x12 (1: 5x12) at x" + (c == '1' ? "2" : "1"),
          n && n->width == (c == '1' ? 5 : 8) && n->height == 12 && n->xOffset == (c == '1' ? 2 : 1) && n->yOffset == -12);
    check("normal digit advance 11: " + d, n && n->advance == 11);
    check("script digit " + d + " is the 5x9 ssty glyph (1: 3x9)",
          s && s->width == (c == '1' ? 3 : 5) && s->height == 9 && s->xOffset == 0 && s->yOffset == -9);
    check("script digit advance 6: " + d, s && s->advance == 6);
  }
  // Advances are the OTF's, independent of ink: '|' paints from one column
  // left of the pen (ink outside [0, advance)), '1' is narrower than its cell.
  {
    const auto* bar = MathFont::find('|', Style::Normal);
    check("'|' ink starts left of the pen", bar && bar->inkLeft() == -1 && bar->inkRight() == 2 && bar->advance == 11);
    const auto* one = MathFont::find('1', Style::Normal);
    check("'1' ink narrower than its advance", one && one->inkRight() - one->inkLeft() < one->advance);
    const auto* space = MathFont::find(' ', Style::Normal);
    check("space is empty with an advance", space && space->width == 0 && space->inkLeft() == 0 && space->inkRight() == 0 && space->advance > 0);
  }
  for (char c : std::string("0123456789ABCDEFxyz+-*/=().,")) {
    const auto* s = MathFont::find(static_cast<uint8_t>(c), Style::Script);
    check(std::string("script glyph present: ") + c, s != nullptr);
    if (s) check(std::string("script advance is 6: ") + c, s->advance == MathMetrics::kScriptDigitAdvance);
  }
  check("normal digit advance", MathFont::find('0', Style::Normal)->advance == MathMetrics::kNormalDigitAdvance);
  {
    const auto* open = MathFont::find('(', Style::Script);
    const auto* close = MathFont::find(')', Style::Script);
    check("script parens stay a symmetric pair",
          open && close && open->width == close->width && open->height == close->height);
  }
  for (uint32_t cp : {0x1D452u, 0x1D456u, 0x1D465u, 0x1D466u, 0x1D467u, 0x1D70Bu, 0xD7u, 0xF7u, 0x2212u, 0x25A1u, 0xB0u, 0x2192u}) {
    check("normal symbol " + label(cp), MathFont::find(cp, Style::Normal) != nullptr);
    check("script symbol " + label(cp), MathFont::find(cp, Style::Script) != nullptr);
  }
  // P6 H1: grid-exact script policy. The old 9.8 px overrides broke all three
  // (E was 7 rows, minus ran past the 6 px advance, lowercase sat 1 row low),
  // so these fail while any legacy script drawing stays applied.
  for (const auto* g = MathFont::begin(); g != MathFont::end(); ++g) {
    if (((g->key >> 21) & 7) != 1 || !g->width) continue;
    check("script ink within the advance at " + std::to_string(g->key),
          g->inkLeft() >= 0 && g->inkRight() <= g->advance);
  }
  for (char c = 'A'; c <= 'Z'; ++c) {
    const auto* s = MathFont::find(static_cast<uint8_t>(c), Style::Script);
    if (s) check(std::string("script capital 9 rows: ") + c, s->height == 9);
  }
  for (char c : std::string("abdehiklmnorstuvwxzcf")) {
    const auto* s = MathFont::find(static_cast<uint8_t>(c), Style::Script);
    if (s) check(std::string("script non-descender ends on the baseline: ") + c,
                 s->yOffset + s->height == 0);
  }
  // Symbols with no `ssty` glyph have no script entry at all (the layout falls
  // back to normal size); nothing is synthesised by scaling.
  const std::set<uint32_t> noScript = {0x24, 0x26, 0x7B, 0x7C, 0x7D, 0x7E, 0x2260, 0x2264, 0x2265, 0x222B, 0x2211, 0x220F, 0x221A, 0x2220, 0x22C5};
  check("normal angle sign", MathFont::find(0x2220u, Style::Normal) != nullptr);
  check("normal dot operator", MathFont::find(0x22C5u, Style::Normal) != nullptr);
  for (const auto* g = MathFont::begin(); g != MathFont::end(); ++g) {
    const uint32_t cp = g->key & 0x1FFFFF;
    if ((g->key >> 21) != 0 || cp == 0x20) continue;
    check("script coverage for " + label(cp) + " is as documented",
          (MathFont::find(cp, Style::Script) == nullptr) == (noScript.count(cp) == 1));
  }

  // From v1 up, vertical variants never shrink, so layout can pick the first
  // that fits. (The radical's base glyph is a wider drawing than its v1..v8
  // family; layout sizes radicals from v1.)
  for (uint32_t cp : {0x28u, 0x29u, 0x221Au, 0x222Bu}) {
    const uint8_t n = MathFont::variants(cp);
    check("variants exist for " + label(cp), n >= 4);
    for (uint8_t v = 2; v <= n; ++v)
      check("variant heights never shrink for " + label(cp),
            MathFont::find(cp, Style::Normal, v)->height >= MathFont::find(cp, Style::Normal, v - 1)->height);
    check("largest variant is taller for " + label(cp),
          MathFont::find(cp, Style::Normal, n)->height > MathFont::find(cp, Style::Normal)->height);
  }
  check("no variant past the last", MathFont::find('(', Style::Normal, MathFont::variants('(') + 1) == nullptr);

  // calculator_metrics.h restates the MATH constants and digit advances. Line
  // ascent/descent are the layout profile's (research spec 6.1: not a font
  // property; calibrated in P7), so they need only contain the baked digit,
  // capital and lowercase ink.
  using namespace CalculatorFontData;
  check("axis", MathMetrics::kAxisHeight == kAxisHeight && MathMetrics::kNormal.axis == kAxisHeight);
  check("normal superscript shifts", MathMetrics::kNormal.superscriptBottomMin == kSuperscriptBottomMin &&
                                     MathMetrics::kNormal.superscriptBaselineDropMax == kSuperscriptBaselineDropMax);
  check("fraction rule", MathMetrics::kFractionRuleThickness == kFractionRuleThickness);
  check("fraction gaps", MathMetrics::kFractionNumeratorGapMin == kFractionNumeratorGapMin &&
                         MathMetrics::kFractionDenominatorGapMin == kFractionDenominatorGapMin);
  check("radical", MathMetrics::kRadicalRuleThickness == kRadicalRuleThickness &&
                   MathMetrics::kRadicalVerticalGap == kRadicalVerticalGap &&
                   MathMetrics::kRadicalExtraAscender == kRadicalExtraAscender);
  check("scripts", MathMetrics::kSuperscriptBottomMin == kSuperscriptBottomMin &&
                   MathMetrics::kSuperscriptBaselineDropMax == kSuperscriptBaselineDropMax &&
                   MathMetrics::kSubscriptShiftDown == kSubscriptShiftDown &&
                   MathMetrics::kSubscriptTopMax == kSubscriptTopMax);
  check("limits", MathMetrics::kUpperLimitGapMin == kUpperLimitGapMin && MathMetrics::kLowerLimitGapMin == kLowerLimitGapMin);
  // P7 ordinary-line minima follow the frozen digit baselines. Taller glyphs
  // grow their item/field boxes individually; they do not set every line's height.
  check("normal caret minima 12/1", MathMetrics::kNormal.caretMinAscent == 12 && MathMetrics::kNormal.caretMinDescent == 1);
  check("script caret minima 9/0", MathMetrics::kScript.caretMinAscent == 9 && MathMetrics::kScript.caretMinDescent == 0);
  check("digit advances", MathMetrics::kNormalDigitAdvance == kNormalDigitAdvance &&
                          MathMetrics::kScriptDigitAdvance == kScriptDigitAdvance);

  if (failures) return 1;
  std::cout << "Calculator font: " << (MathFont::end() - MathFont::begin())
            << " glyphs; grid-exact digits (8x12 / ssty 5x9), ink extents, script, variant and metric checks passed.\n";
  return 0;
}
