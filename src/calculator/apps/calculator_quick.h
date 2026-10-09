#pragma once
#include "calculator/ui/calculator_editor.h"
#include "calculator/runtime/calculator_session.h"
#include <vector>
namespace Calculator { namespace QuickOverlay {
bool open(MathEditor& editor,ExpressionInput::Key key,AppId app);
bool active();
void close();
void press(ExpressionInput::Key key);
std::vector<uint8_t> render();
} }
