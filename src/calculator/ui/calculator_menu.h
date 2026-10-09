#pragma once

#include <string>
#include <vector>

#include "calculator/math/calculator_engine.h"
#include "calculator/runtime/calculator_session.h"
#include "ui/ui_kit.h"

// Shared menu model (docs/DISPLAY.md). Lists are
// semantic rows with a stable action ID that survives filtering and reordering;
// dispatch switches on the action, never the row index. Navigation state is
// explicit and bounded. No pixels here: calculator_render.* adapts to UiKit.
namespace Calculator {

// Bare calculator lists show four large rows (plan section 2 photographs);
// page keys step by this count.
constexpr int kMenuVisibleRows = 4;
// Deepest current hierarchy (Catalog, Settings, Constants/Conversion, Reset)
// with room to spare.
constexpr size_t kMenuStackMax = 8;

// Which editor opened the menu. Data-entry origins (Store/Define/CALC, or a
// Cell of an app's data grid) make insertion land in that field and closing
// restore it instead of the main entry view.
enum class MenuOrigin : uint8_t { Main, Store, Define, Calc, Cell };

// Where a menu's insertion lands, captured at open. A Cell target carries the
// dataset revision; if the app's revision moved on, the menu closes without
// inserting anywhere (never into the main expression as a fallback).
struct InputTarget {
  MenuOrigin origin = MenuOrigin::Main;
  int16_t row = -1, col = -1;
  uint32_t revision = 0;
};

// One selectable row; filtering removes rows without renumbering actions.
struct MenuItem {
  int action = -1;
  std::string label;
  bool submenu = false;
  UiKit::Mark mark = UiKit::Mark::kNone;
};

// ---- Stable action IDs (never reorder values) ---------------------------

namespace ToolAction {
constexpr int Calc = 0;
constexpr int Undo = 1;
constexpr int VerifyToggle = 2;
}  // namespace ToolAction

namespace FunctionAction {
constexpr int F = 0;
constexpr int G = 1;
constexpr int DefineF = 2;
constexpr int DefineG = 3;
}  // namespace FunctionAction

namespace VariableAction {
constexpr int Recall = 0;
constexpr int Store = 1;
constexpr int Edit = 2;
}  // namespace VariableAction

namespace ResetAction {
constexpr int Variables = 0;
constexpr int SettingsData = 1;
constexpr int All = 2;
constexpr int Cancel = 3;
}  // namespace ResetAction

namespace ConfirmAction {
constexpr int No = 0;
constexpr int Yes = 1;
}  // namespace ConfirmAction

// Groups are appended, never renumbered; Digits is the Number Format child.
namespace SettingAction {
constexpr int InputOutput = 0;
constexpr int Angle = 1;
constexpr int Number = 2;
constexpr int Digits = 3;
constexpr int Engineer = 4;
constexpr int Fraction = 5;
constexpr int DecimalMark = 6;
constexpr int Separators = 7;
constexpr int FormatMenu = 8;
constexpr int ScientificKey = 9;
constexpr int Reset = 10;
constexpr int Complex = 11;
constexpr int CalcSettingsGroup = 12;
constexpr int SystemSettingsGroup = 13;
constexpr int GetStarted = 14;  // Destination pending; existing IDs stay fixed.
}  // namespace SettingAction

// Catalog root: category indices occupy [0, catalogSize), then the two tables.
namespace CatalogRootAction {
constexpr int SciConstants = 100;
constexpr int UnitConversions = 101;
}  // namespace CatalogRootAction

// ---- VARIABLE grid ----------------------------------------------------------
// Two columns x five rows, all nine variables, no scrolling; order is
// left-to-right, top-to-bottom, and the bottom-right cell does not exist.
constexpr int kVariableCount = 9;
constexpr int kVariableColumns = 2;
constexpr int kVariableRows = 5;

// A..F, x, y, z in grid-index order; '?' when out of range.
char variableName(int index);
int variableRow(int index);
int variableColumn(int index);
// Cell at (row, col), or -1 for the missing cell or out of range. Never wraps.
int variableIndexAt(int row, int col);

enum class GridMove : uint8_t { Left, Right, Up, Down };
// One grid step. Edges clamp (native wrapping is uncaptured, and wrapping past
// the missing cell would be ambiguous); a step into the missing cell stays put.
int moveVariableSelection(int current, GridMove move);

// "A=<value>", always in Norm 1 (manual p.44) whatever the number format;
// digit count and decimal mark still apply.
std::string variableLabel(int index, const ScalarValue& value, const Settings& settings);
// The same "<name>=<value>" for any name: the solve() root picker's rows.
std::string valueLabel(const std::string& name, const ScalarValue& value, const Settings& settings);

// ---- Catalog data (single owner; app and menu builders share it) ---------
struct CatalogEntry {
  const char* label;
  const char* expression;
};
struct CatalogGroup {
  std::string name;
  std::vector<CatalogEntry> entries;
};
const std::vector<CatalogGroup>& catalogData();
// Index of the Verify group inside catalogData(), or -1.
int verifyGroupIndex();
std::vector<CatalogEntry> quickCatalogEntries(bool verifyMode,AppId app);
std::string menuDisplayLabel(const std::string& label);
std::string quickCatalogLabel(const CatalogEntry& entry);

// ---- Row builders --------------------------------------------------------

// Complex adds its own category and drops Func Analysis (plan O-5); Calculate
// never shows Complex.
std::vector<MenuItem> catalogRootRows(bool verifyMode, AppId app = AppId::Calculate);
std::vector<MenuItem> catalogCategoryRows(int group);
// Constants/conversion tables: group rows open a child; item rows insert.
std::vector<MenuItem> constantsGroupRows();
std::vector<MenuItem> constantsItemRows(int group, std::vector<size_t>& indices);
std::vector<MenuItem> conversionGroupRows();
std::vector<MenuItem> conversionItemRows(int group, std::vector<size_t>& indices);
// TOOLS in reference order: CALC, Undo, Verify ON/OFF. CALC is Calculate's alone.
std::vector<MenuItem> toolRows(bool verifyMode, bool calc = true);
std::vector<MenuItem> functionRows();
// FORMAT rows the retained result can show; always has Standard, historical
// order. Rectangular/Polar Coord follow Decimal, complex results only.
std::vector<MenuItem> formatRows(const Result* result, bool hasResult, const Settings& settings);
// The same rows for a stored history answer (FORMAT on a HistoryView entry).
std::vector<MenuItem> formatRows(const ResultSummary& summary, const Settings& settings);
// Variable actions: Store appears only when a value is available.
std::vector<MenuItem> variableActionRows(bool canStore);
std::vector<MenuItem> resetRows();
// Root: Calc Settings, System Settings, Reset, Get Started. Calc Settings is
// the eight calculator rows plus Format Menu and Scientific Key. Option pages
// carry radio state.
std::vector<MenuItem> settingsRootRows(const Settings& settings);
std::vector<MenuItem> calcSettingsRows(const Settings& settings);
std::vector<MenuItem> settingsOptionRows(int setting, const Settings& settings);
std::string settingsQuestion(int setting);
// Reset confirmation question for a pending ResetAction.
std::string resetQuestion(int resetAction);

// ---- Capabilities --------------------------------------------------------

bool formatAvailable(Format format, const Result* result, bool hasResult, const Settings& settings);
// A CAS formula is storable only through the numeric roots of a solve().
bool canStoreResult(const Result* result, bool hasResult);

// Applies an option-page choice; true when valid. clearHistory tells the caller
// to reset history/editor (Input/Output). Never touches NVS.
bool applySettingOption(int setting, int choice, Settings& settings, bool& clearHistory);

// ---- Parent stack --------------------------------------------------------

// One menu level. `selected` is a row index; dispatch resolves it to
// MenuItem::action first. `first` is the window the renderer paints.
struct MenuContext {
  int screen = -1;
  int subpage = -1;
  int selected = 0;
  int first = 0;
  // The app that pushed this level; a context never restores into another app.
  AppId app = AppId::Calculate;
  InputTarget target;
  bool openedByUser = false;
};

class MenuStack {
 public:
  bool empty() const { return contexts_.empty(); }
  size_t size() const { return contexts_.size(); }
  const MenuContext& top() const { return contexts_.back(); }
  MenuContext& top() { return contexts_.back(); }
  // False when full; the caller keeps the current screen.
  bool push(const MenuContext& context);
  void pop();
  void clear();

 private:
  std::vector<MenuContext> contexts_;
};

}  // namespace Calculator
