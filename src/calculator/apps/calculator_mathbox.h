#pragma once
#include "calculator/runtime/calculator_clock.h"
#include "input/expression_editor.h"
#include <cstdint>
#include <string>
#include <vector>

// Math Box (Home, last row): a clock and the device countdown (calculator_clock.h
// holds the logic). Unlike the Casio apps it is an accessory screen of the
// CalculatorApp shell, not a session app: opening it never applies the
// OtherAppLaunch transition, so checking the time mid-exam keeps PreAns, Verify
// and the paused app's work.
//
// Screens: a two-row menu (Clock, Countdown), the clock (UTC+7 date and time),
// and the countdown (set with digits H:MM, run, pause, reset). The countdown's
// alerts are a shell modal (renderAlert) so they appear over any screen.
namespace Calculator {
namespace MathBoxApp {
// From Home: `fresh` shows the menu, otherwise the screen it was left on.
void enter(bool fresh);
// Keys while Math Box is on screen, except Home and Settings (the shell's).
void press(ExpressionInput::Key key);
// True when the visible content changed since the last render (a second
// ticked, the countdown moved or finished): the shell repaints.
bool service();
std::vector<uint8_t> render();
// The countdown alert: `minutesLeft` > 0 is a milestone, 0 the finish.
std::vector<uint8_t> renderAlert(int minutesLeft);
#ifdef CALCULATOR_REFERENCE_TEST
std::string screenName();
#endif
}  // namespace MathBoxApp
}  // namespace Calculator
