#pragma once
#include "calculator/apps/calculator_calculate.h"
#include "calculator/ui/calculator_grid.h"
#include <array>

namespace Calculator {
constexpr int kSheetRows = 45, kSheetCols = 5, kSheetCells = 225;
constexpr size_t kSheetMemory = 2380, kSheetInput = 49;
struct SpreadsheetCell {
  // Empty source for a constant; formulas retain their leading '='.
  std::string formula;
  Decimal value;
  Error error = Error::None;
  bool occupied = false;
};
struct SpreadsheetData { std::array<SpreadsheetCell, kSheetCells> cells; };
struct SpreadsheetRange { int row = 0, col = 0, endRow = 0, endCol = 0; };
bool spreadsheetRange(const std::string&, SpreadsheetRange&);
size_t spreadsheetInputBytes(const std::string&);
size_t spreadsheetUsedBytes(const SpreadsheetData&);
// Compact numeric cell readout retains the exponent; footer keeps all digits.
std::string spreadsheetCellText(const Decimal&, const Settings&);
std::string spreadsheetReference(int row, int col);
std::string relocateSpreadsheetFormula(const std::string&, int rows, int cols);
enum class SpreadsheetOperation : uint8_t { Set, Erase, Clear, Recalculate, FillFormula, FillValue, Copy, Cut };
struct SpreadsheetTask {
  SpreadsheetOperation operation = SpreadsheetOperation::Set;
  std::shared_ptr<const SpreadsheetData> input;
  SpreadsheetRange range;
  std::string expression;
  int sourceRow = 0, sourceCol = 0;
  bool autoCalc = true;
};
Result evaluateSpreadsheet(const SpreadsheetTask&, const Request&,
                          const std::function<bool()>& cancelled = {},
                          const std::function<void()>& cooperate = {});
namespace SpreadsheetApp {
void enter();
void pause();
void clear(); // Power-off/native ON: sheet is volatile, never stored in NVS.
void press(ExpressionInput::Key);
void adopt(const Job&, Result&);
bool service();
bool caretVisible();
std::vector<uint8_t> render(bool caretOn);
PanelIcons::State icons();
size_t stateBytes();
size_t heapBytes();
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
#endif
} // namespace SpreadsheetApp
} // namespace Calculator
