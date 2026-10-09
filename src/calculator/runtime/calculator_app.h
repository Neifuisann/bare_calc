#pragma once
#include "input/expression_editor.h"
#include "display/panel_icons.h"
#include <cstdint>
#include <vector>
#ifdef CALCULATOR_REFERENCE_TEST
#include "calculator/ui/calculator_render.h"
#endif
namespace CalculatorApp {
#ifdef CALCULATOR_REFERENCE_TEST
// Host-only detached observation. Frame is the caller's last service output;
// no extra framebuffer, render, blink tick, job adoption or session mutation.
struct Snapshot {
  std::string screen, menuScreen, text, fieldRole, caretKind, notice, resultText;
  std::string logicalCaretKind; // P8 kind survives blink/linear visibility; host-only.
  std::string errorTitle,errorOrigin; // P10 modal observation, not a result/notice.
  std::string calcPhase, calcParent; // P13 explicit CALC state; never inferred text.
  size_t errorCursor=0;
  size_t cursor=0, selection=0, historyCursor=0;
  size_t calcIndex=0, calcCount=0;
  int first=0;
  bool hasResult=false, caret=false, busy=false, calcResult=false;
  Calculator::MathLayout::Rect caretRect;
  PanelIcons::State icons;
  std::vector<std::string> menuLabels;
  std::vector<uint8_t> frame;
  // P15 detached app observations; excluded from target firmware.
  int appId=0, base=10, gridRow=0, gridCol=0, rows=0, cols=0, page=0;
  bool editing=false;
  std::vector<std::string> cells;
};
Snapshot snapshot(const std::vector<uint8_t>& lastFrame);
#endif
// Fresh entry on the Home menu, with no paused app to resume.
void openHome();
// App (or notice) -> Home menu, preserving this session's resume target.
void showHome();
// Quick calculate, the calculator's resting face (boot and power-on):
// straight into Calculate on the shared session, skipping Home so its replay
// history and last draft/result stay on screen. Every key behaves as in
// Calculate. Home shows the menu; Home again resumes it as quick calculate,
// another app ends it.
void openQuick();
// Persists the session and stops presenting (power-off). openQuick() or
// openHome() resumes.
void suspend();
// Math Box countdown alerts (calculator_clock.h). The calculator shows one
// over any of its screens by itself and any key closes it. alertDue() lets a
// suspended host notice one (wake, then openQuick()); alertActive() is true
// while it shows.
bool alertDue();
bool alertActive();
// For the host's power policy: auto power-off waits while a run is live, and
// while a due alert is pending or visible. Idle sleep wakes for the next
// milestone (0 = nothing armed).
bool countdownRunning();
bool countdownBlocksAutoPowerOff();
uint32_t countdownWakeMs();
bool active();
// True while the overlay is showing the app menu (used for the Home toggle
// in app/input.cpp). Never true while suspended.
bool isHome();
// True once after OK on Settings > System Setup in any app (Home's Setting key
// included): the host shows its System Setup page over the Settings list, then
// calls redraw() when it hands back. The session is persisted first, since the
// page can restart the device.
bool takeSystemSetup();
// Repaint on the next service(): the host drew over the overlay.
void redraw();
void press(ExpressionInput::Key key);
// Returns a frame only when the visible state changed. Worker results are
// adopted on this thread; cancelled/background results cannot publish.
bool service(std::vector<uint8_t>& frame);
// How long the loop may light-sleep after service() before this needs it
// again, capped at `maxMs`: 0 while a job runs (the worker is on core 0, which
// the sleep stops too) or a frame is still owed, else until the next caret
// blink edge.
uint32_t sleepBudgetMs(uint32_t maxMs);
// Complete etched-icon state for the current view snapshot. Calculation views
// report MATHIO/angle/FIX/SCI/E/Verify/history; field views report the parent
// settings plus the field SHIFT latch; every menu reports none. The caller
// stages it through PanelLink before presenting.
PanelIcons::State desiredIcons();
}
