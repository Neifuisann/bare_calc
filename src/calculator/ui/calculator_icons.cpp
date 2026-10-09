#include "calculator/ui/calculator_icons.h"

namespace Calculator {

PanelIcons::State desiredIcons(const IconView& view) {
  PanelIcons::State state;
  if (!view.calculation && !view.field) return state;
  // SHIFT is the active editor latch: main editor on Entry, field editor on
  // Store/Define/CALC. The caller picks which latch to report.
  state.shift = view.shifted;
  if (view.baseN) {
    if (view.calculation) {
      state.arrUp = view.historyPrev;
      state.arrDown = view.historyNext;
    }
    return state;
  }
  // Natural-input mode on applicable calculation views. CALC reports the
  // parent expression's IO, not its forced-linear field IO, so a math parent
  // keeps MATHIO lit under a linear prompt.
  state.mathio =
      view.io == IO::MathMath || view.io == IO::MathDecimal;
  // Exactly one angle glyph on every calculation/field view.
  state.deg = view.angle == Angle::Degree;
  state.rad = view.angle == Angle::Radian;
  state.gra = view.angle == Angle::Gradian;
  state.fix = view.number == NumberFormat::Fix;
  state.sci = view.number == NumberFormat::Sci;
  state.e = view.engineer;
  state.check = view.verify;
  // History arrows are an Entry affordance; field entry never browses replay.
  if (view.calculation) {
    state.arrUp = view.historyPrev;
    state.arrDown = view.historyNext;
  }
  state.cplxI = view.complexApp && view.complexResult == ComplexResult::Rectangular;
  state.angle = view.complexApp && view.complexResult == ComplexResult::Polar;
  if (view.aggregate) state.mathio = state.check = state.arrUp = state.arrDown = false;
  // Deliberately off: hollow capture arrow, solid left/right navigation and
  // sun. Menus clear everything by returning the empty state above.
  return state;
}

}  // namespace Calculator
