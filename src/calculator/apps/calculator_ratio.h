#pragma once
#include "calculator/apps/calculator_calculate.h"
namespace Calculator { namespace RatioApp {
void enter(bool relaunch);
void pause(bool home = false);
void press(ExpressionInput::Key key);
void adopt(const Job& job, Result& result);
bool caretVisible();
std::vector<uint8_t> render(bool caretOn);
PanelIcons::State icons();
size_t stateBytes();
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
#endif
} }
