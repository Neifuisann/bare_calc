#pragma once
#include "calculator/runtime/calculator_job.h"
#include "calculator/runtime/calculator_session.h"
#include "input/expression_editor.h"
#include "display/panel_icons.h"
#include <cstdint>
#include <vector>
#ifdef CALCULATOR_REFERENCE_TEST
#include "calculator/runtime/calculator_app.h"
#endif

// The Equation (Simul Equation, Polynomial, Solver) and Inequality apps: their
// menus, coefficient grid, Solver equation and initial value, and one answer
// page at a time. The mathematics is calculator_polynomial.* on the worker.
// One instance, state file-local; CalculatorApp routes keys, results and frames
// here while either app is running.
namespace Calculator {
namespace EquationApp {
// `relaunch`: Home launched this app after a different one, so coefficients,
// the Solver equation and the answers start over (plan section 18, provisional
// O-4). Reopening the paused app keeps them.
void enter(AppId app, bool relaunch);
void pause();
void interrupt();
void press(ExpressionInput::Key key);
void adopt(const Job& job, Result& result);
bool caretVisible();
std::vector<uint8_t> render(bool caretOn);
PanelIcons::State icons();
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
#endif
}  // namespace EquationApp
}  // namespace Calculator
