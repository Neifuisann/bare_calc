#pragma once

// Complete etched-icon (status indicator) state for the Casio LCD's icon row.
//
// Every calculator presentation supplies a full desired state for all
// UI-owned glyphs (CalculatorApp::desiredIcons()); the display driver sets the
// requested ones and clears every other one. PAUSE is deliberately absent:
// it belongs to the sleep indicator alone, and normal staging must never
// clear it.
//
// Arduino-independent on purpose: no calculator_engine.h, no DDRAM offsets,
// so host icon-mapping tests can include this without device headers.
namespace PanelIcons {

// One bool per UI-owned glyph in the LCD's icon order, minus PAUSE.
// Keep in sync: this struct, operator== below, and the display driver.
struct State {
  bool shift = false;      // ICON_SHIFT
  bool mathio = false;     // ICON_MATHIO
  bool deg = false;        // ICON_DEG
  bool rad = false;        // ICON_RAD
  bool gra = false;        // ICON_GRA
  bool fix = false;        // ICON_FIX
  bool sci = false;        // ICON_SCI
  bool e = false;          // ICON_E
  bool cplxI = false;      // ICON_CPLX_I
  bool angle = false;      // ICON_ANGLE
  bool scrollDown = false; // ICON_SCROLL_DOWN
  bool check = false;      // ICON_CHECK
  bool arrLeft = false;    // ICON_ARR_LEFT
  bool arrDown = false;    // ICON_ARR_DOWN
  bool arrUp = false;      // ICON_ARR_UP
  bool arrRight = false;   // ICON_ARR_RIGHT
  bool sun = false;        // ICON_SUN
};

inline bool operator==(const State& a, const State& b) {
  return a.shift == b.shift && a.mathio == b.mathio && a.deg == b.deg &&
         a.rad == b.rad && a.gra == b.gra && a.fix == b.fix && a.sci == b.sci &&
         a.e == b.e && a.cplxI == b.cplxI && a.angle == b.angle &&
         a.scrollDown == b.scrollDown && a.check == b.check &&
         a.arrLeft == b.arrLeft && a.arrDown == b.arrDown &&
         a.arrUp == b.arrUp && a.arrRight == b.arrRight && a.sun == b.sun;
}

inline bool operator!=(const State& a, const State& b) { return !(a == b); }

// 17 UI-owned glyphs; PAUSE is sleep-owned and SUN is staged off. If this
// fires, update the struct, operator==, and the display driver together.
static_assert(sizeof(State) == 17, "PanelIcons::State field count changed");

}  // namespace PanelIcons
