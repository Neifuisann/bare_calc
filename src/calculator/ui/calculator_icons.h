#pragma once

#include "calculator/math/calculator_engine.h"
#include "display/panel_icons.h"

// Pure calculator -> etched-icon mapping (docs/DISPLAY.md).
// Arduino-independent: no CasioLcd, no PanelLink, so
// host tests can exercise every combination without device headers.
//
// Profiles:
//   None        menus, Home, VARIABLE grid, notices: every UI icon off.
//   Calculation Entry: MATHIO/angle/FIX/SCI/E/Verify/history as applicable.
//   Field       Store/Define/CALC: calculation settings plus the field SHIFT
//               latch. CALC's linear field does not clear the parent MATHIO.
namespace Calculator {

struct IconView {
  bool calculation = false;  // Entry
  bool field = false;        // Store/Define/Calc
  Angle angle = Angle::Degree;
  IO io = IO::MathMath;
  NumberFormat number = NumberFormat::Norm1;
  bool engineer = false;
  bool verify = false;
  bool shifted = false;
  // Replay history: previous available lights ARR_UP, later available lights
  // ARR_DOWN. Meaningful on Entry only; Field clears both. The app gates both
  // on the Up/Down browse precondition (a shown result plus history), so a lit
  // arrow always means the key will recall rather than move the cursor.
  bool historyPrev = false;
  bool historyNext = false;
  // Complex app views: the global Complex Result lights i (a+bi) or the angle
  // mark (polar). A local FORMAT choice never changes it.
  bool complexApp = false;
  ComplexResult complexResult = ComplexResult::Rectangular;
  // Base-N: only SHIFT and the replay arrows, whatever the stored settings.
  bool baseN = false;
  // Matrix/Vector: angle, FIX/SCI and E only (no MATHIO, CHECK or replay).
  bool aggregate = false;
};

PanelIcons::State desiredIcons(const IconView& view);

}  // namespace Calculator
