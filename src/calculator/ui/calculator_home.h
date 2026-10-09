#pragma once
#include "input/expression_editor.h"
#include <array>
#include <cstdint>
namespace Calculator {
// Home menu slots: the 13 Casio apps (0..12, Math Box last, alone on the
// last row). The last row's other two slots stay empty and are never
// selectable.
constexpr unsigned kCalculateIndex = 0;
constexpr unsigned kMathBoxIndex = 12;
constexpr unsigned kHomeItemCount = 13;
struct HomeMenu {
  unsigned selected=0,top=0;
  void press(ExpressionInput::Key key);
  std::array<uint8_t,3024> pixels() const;
};
}
