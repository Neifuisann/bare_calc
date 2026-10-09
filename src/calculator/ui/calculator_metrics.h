#pragma once
#include <cstdint>

// One geometry profile for Calculate's natural display. Layout reads every
// spacing, shift and caret dimension from here and every glyph measurement
// from MathFont. Template decoration details live in the layout.
//
// Values marked MATH are the ClassWiz OTF's MATH table at the native 14 px
// scale (10 units per pixel) and extents measured from the baked glyphs;
// tools/bake_calculator_font.py emits the same numbers into
// font_calculator_math.h and the font test holds the constants and advances
// equal. Ordinary line minima are 12/1 normal and 9/0 Script; taller ink
// grows individual composed boxes. The rest are
// calculator-specific starting values that native captures still have to
// confirm (test/calculator_render/MANIFEST.md lists the capture cases).
// Menu row pitch is deliberately not here: UiKit owns it.
namespace Calculator {
namespace MathMetrics {
struct Style {
  int8_t ascent;       // ordinary field minimum above the font baseline
  int8_t descent;      // ordinary field minimum below the font baseline
  int8_t caretLeft;    // caret's left column relative to the slot x
  int8_t caretWidth;
  int8_t caretMinAscent;
  int8_t caretMinDescent;
  int8_t axis;         // fraction bar row above the baseline
  int8_t superscriptBottomMin;       // minimum separation of base/exponent ink bottoms
  int8_t superscriptBaselineDropMax; // ... and its baseline at most this far below the base's top
};
// The frozen glyph baselines put normal ink at -12..-1 and Script at
// -9..-1. Minima give 13/9 rows; composed fields can grow independently.
constexpr Style kNormal{12, 1, -1, 3, 12, 1, 6, 7, 5};
constexpr Style kScript{9, 0, -1, 3, 9, 0, 4, 5, 4};

constexpr int kNormalDigitAdvance = 11;          // MATH: measured
constexpr int kScriptDigitAdvance = 6;           // MATH: measured
constexpr int kAxisHeight = 6;                   // MATH: fraction bar row above baseline
constexpr int kFractionRuleThickness = 1;        // MATH
constexpr int kFractionNumeratorGapMin = 1;      // MATH
constexpr int kFractionDenominatorGapMin = 1;    // MATH
constexpr int kRadicalRuleThickness = 1;         // MATH
constexpr int kRadicalVerticalGap = 1;           // MATH
constexpr int kRadicalExtraAscender = 1;         // MATH
constexpr int kSuperscriptBottomMin = 7;         // MATH value; P7 relates actual ink bottoms
constexpr int kSuperscriptBaselineDropMax = 5;   // MATH: ... and at most this far below the base's top
constexpr int kSubscriptShiftDown = 2;           // MATH
constexpr int kSubscriptTopMax = 6;              // MATH
constexpr int kUpperLimitGapMin = 1;             // MATH
constexpr int kLowerLimitGapMin = 1;             // MATH

constexpr int kFractionBoxPad = 4;
constexpr int kFractionRuleInsetLeft = 1;
constexpr int kFractionNumeratorInkGap = 2;
constexpr int kFractionDenominatorInkGap = 2;
constexpr int kRootIndexTuck = 3;  // preserved native rule; target geometry pending
constexpr int kScriptExitPad = 2; // space between the raised field and its parent stop
constexpr int kMixedGap = 1;           // whole part to fraction
constexpr int kDelimiterPad = 0;       // content to a sized parenthesis/abs bar
constexpr int kLimitGap = 1;           // integral sign to its limit column
constexpr int kMarginTop = 0;
constexpr int kMarginLeft = 1;
constexpr int kMarginRight = 1;
constexpr int kResultGap = 2;          // input bottom to result top when both are shown
constexpr uint8_t kIntegralMinVariant = 1;  // the base 17 px sign cannot span two script limits
}  // namespace MathMetrics
}  // namespace Calculator
