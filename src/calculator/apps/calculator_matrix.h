#pragma once
#include "calculator/runtime/calculator_session.h"
#include "input/expression_editor.h"
#include <string>
#include <vector>
#ifdef CALCULATOR_REFERENCE_TEST
#include "calculator/runtime/calculator_app.h"
#endif

// Matrix and Vector screens beyond the calculation line (manual pp.137-150):
// the named-slot list, size/dimension form, cell editor, the read-only answer
// and their TOOLS (Define, Store). The calculation entry itself is the shared
// expression controller (calculator_calculate.*), which routes keys here while
// one of these screens is up and acts on the returned Outcome. State is
// file-local; the values live in the session's Aggregates.
namespace Calculator {
namespace AggregateApp {

struct Outcome {
  enum class Kind : uint8_t {
    Stay,      // still on an aggregate screen
    Close,     // back to the calculation, expression kept
    Clear,     // back to the calculation, expression cleared
    Continue,  // back with `text` (MatAns/VctAns) followed by `key` (SHIFT when `shift`)
    Evaluate,  // evaluate `text` for the cell being edited; commitCell/cellError follow
  };
  Kind kind = Kind::Stay;
  ExpressionInput::Key key = ExpressionInput::Key::None;
  bool shift = false;
  std::string text;
};

// TOOLS on the calculation screen: MatA-D (or VctA-D) with their sizes.
void openList(AppId app);
// A new MatAns/VctAns: its read-only grid.
void openAnswer(AppId app);
Outcome press(ExpressionInput::Key key);
void commitCell(const Value& value);
void cellError(Error error);
std::vector<uint8_t> render();
// Editor or answer grid (angle/FIX icons) rather than a menu (no icons).
bool gridShown();
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
#endif

}  // namespace AggregateApp
}  // namespace Calculator
