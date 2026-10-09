#pragma once
#include "calculator/ui/calculator_document.h"
#include "calculator/ui/calculator_editor.h"
#include "calculator/math/calculator_engine.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_layout.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/runtime/calculator_session.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
// Calculator frame builders; each returns PackBits RLE for PanelLink.
namespace Calculator {
std::vector<uint8_t> renderGetStarted();
std::vector<uint8_t> renderQuickCatalog(const std::vector<CatalogEntry>& entries,int selected);
// Per-editor input scroll offset kept across frames; moves only when the caret
// would leave the view.
struct Viewport {int16_t x=0,y=0;};
// The one ResultSummary -> presented text formatter (mirrors the Result path).
std::string summaryText(const ResultSummary& summary,const Settings& settings,Format format);
// Result/history view: `editor` on top, stored answer below. `resultScroll`
// owns the horizontal viewport of an overflowing answer (nullptr = legacy
// start-clipped, no arrows). Raster arrows only; segments stay off (I-04).
std::vector<uint8_t> renderResultSummary(const MathEditor& editor,const Settings& settings,
                                     const ResultSummary* summary,Format format,bool busy,bool verify,
                                     Viewport& view,const std::string& caption,bool caretOn,int16_t* resultScroll);
// Answer scroll range in ink pixels (0 if it fits, linear, or an error). Also
// the controller's Left/Right precedence, so keys and paint cannot disagree.
int16_t summaryMaxScroll(const ResultSummary& summary,const Settings& settings,Format format);
// `caption` labels a data-entry field ("A=", "Define f(x)") in the notice band
// when the editor has no notice. `caretOn` = blink phase (hides, never moves).
std::vector<uint8_t> renderCalculation(const MathEditor& editor,const Settings& settings,
                                     const Result* result,Format format,bool busy,bool verify,
                                     Viewport& view,const std::string& caption="",bool caretOn=true);
// What the most recent renderCalculation() drew, in screen coordinates.
struct RenderInfo {
  bool caret=false;
  MathLayout::Rect caretRect,input,result;
  MathFont::Style style=MathFont::Style::Normal;
  EditDocument::Role role=EditDocument::Role::Root;
  // Logical kind, including blink-off frames; all three draw the same bar
  // until alternate shapes are captured.
  MathEditor::CaretKind caretKind=MathEditor::CaretKind::Insert;
  std::string presented;  // result text as laid out
  int16_t resultOverflow=0;  // renderEquationResult: pixels the answer exceeds its band (0: it fits)
};
const RenderInfo& lastRender();
// formatValue() grammar -> layout template grammar: n/d and w r/d as
// fractions, k*pi / k*sqrt(r) as implicit products, E as x10^, recurring
// digits overlined, prime powers raised, d/m/s as degree marks. Presentation
// only.
std::string presentResult(const std::string& formatted);
// Bare four-row list via UiKit with caller-owned scrolling; no private list
// primitives live here.
std::vector<uint8_t> renderMenuList(const std::vector<MenuItem>& items, int selected, int first,bool shortcuts=true);
// Info-leading list: row zero is the unselectable question, then the options.
// Also the nine-apps parameter/range form (plan section 16): heading, then
// `name:value` rows and Execute.
std::vector<uint8_t> renderConfirm(const std::string& question, const std::vector<MenuItem>& options,
                                   int selected, int first);
// Data grid (Statistics, Distribution, Table) in the small script face: column
// headers, four 10 px rows behind a row-number gutter, a scrollbar past four,
// and the selected cell's full value in the bottom band. Exactly one cell (never
// a header) is inverted. `cells` is row-major with headers.size() columns;
// cursor.firstRow picks the rows shown. Geometry is provisional (O-8).
struct DataGridView {
  std::vector<std::string> headers;
  std::vector<std::string> cells;
  GridCursor cursor;
  std::string footer;
  // Distribution keeps two narrow columns beside its wrapped type name.
  std::string context;
  bool statistics = false;
  bool table = false;
  bool equation = false;  // Casio expression rows, rather than a data table
  bool polynomial = false;
  // Inequality: the relation after the last coefficient, e.g. "<0" ('\x03' is
  // <=, '\x04' is >=), also closing the formula header.
  std::string relation;
  const MathEditor* editor = nullptr;
  bool caretOn = true;
};
std::vector<uint8_t> renderDataGrid(const DataGridView& view);
// Spreadsheet raster geometry from spreadsheet.bmp: four 42-pixel columns,
// four 10-pixel rows; gray headers/rules and a linear edit box at y=50.
struct SpreadsheetView {
  GridCursor cursor;
  std::vector<std::string> cells; // visible 4x4 window, row-major
  std::string footer;
  const MathEditor* editor = nullptr;
  bool caretOn = true, paste = false;
};
std::vector<uint8_t> renderSpreadsheet(const SpreadsheetView&);
std::vector<uint8_t> renderSpreadsheetFill(bool formula, const std::string& expression,
    const std::string& range, int selected, const MathEditor* editor, bool caretOn);
std::vector<uint8_t> renderSpreadsheetMemory(size_t bytes);
std::vector<uint8_t> renderSpreadsheetChoices(const std::string& question,
    const std::vector<MenuItem>& choices,int selected);
// "ax^2+bx+c": ^d marks a superscript digit, as renderEquationResult headers use.
std::string polynomialFormula(int degree);
// Degree picker, or (relationDegree 2..4) the Inequality relation picker.
std::vector<uint8_t> renderPolynomialMenu(int selected, int relationDegree = 0);
// CW numeric form (Distribution, Table Range): fixed heading, three scrolling
// fields. Selection/first index only the parameters and the final Execute.
constexpr int kDistributionFormRows = 3;
struct DistributionField {
  std::string label, value;
};
std::vector<uint8_t> renderDistributionForm(const std::string& title,
    const std::vector<DistributionField>& fields, int selected, int first,
    const MathEditor* editor, Viewport& viewport, bool caretOn = true);
// Statistics result page: small-script `label=value` rows, six visible, no
// header or gutter. Caller scrolls; scrollbar past six; one row inverted.
std::vector<uint8_t> renderResultList(const std::vector<MenuItem>& items, int selected, int first);
// Base-N entry: expression, then the result in `base` (binary as two 16-bit
// right-aligned lines) and the base name bottom left; empty entry shows the
// FORMAT hint. Fixed width, never scrolls. Layout provisional (O-8).
std::vector<uint8_t> renderBaseN(const MathEditor& editor, uint8_t base, const ResultSummary* result, bool busy,
                                 Viewport& view, bool caretOn = true);
// Matrix/Vector editor or answer: `name=`, bracketed cells with the cursor
// cell inverted, and the selected value or the cell draft in the footer.
struct AggregateView {
  std::string name;
  int rows = 0, cols = 0;
  std::vector<std::string> cells;  // row-major, rows * cols
  GridCursor cursor;
  std::string footer;
};
std::vector<uint8_t> renderAggregate(const AggregateView& view);
// Matrix Size? / Vector Dimension? form (hardware captures): the question on
// row zero, one `label :n ►` row per field, then Execute-marked Confirm.
// `selected` indexes the fields, then Confirm.
struct SizeField {
  std::string label;
  int value = 0;
};
std::vector<uint8_t> renderSizeForm(const std::string& title, const std::vector<SizeField>& fields, int selected);
// Ratio coefficients (hardware capture): A:B=X:D or A:B=C:X on one line, the
// three editable fields underlined in grey, the selected one inverted, and
// the selected value (or the draft, `draft` adding the cursor) in the answer
// band.
struct RatioView {
  std::array<std::string, 3> cells;
  bool unknownLast = false;  // A:B=C:X
  int selected = 0;
  std::string footer;
  bool draft = false;
};
std::vector<uint8_t> renderRatio(const RatioView& view);
// Equation/Inequality answer page: `label=` then the value right-aligned below,
// or (empty label) a text page. \x01..\x02 spans are formatValue() output,
// drawn as math when the input/output setting is Math.
std::vector<uint8_t> renderSolution(const std::string& label, const std::string& value, const Settings& settings);
// Casio Equation/Inequality answer: `header` (the formula, "Min of y=...", or
// an Inequality template) on the top line, `label=` below it, the value on the
// bottom line. Both headers and labels take ^d/_d super/subscripts. An answer
// wider than the panel starts at its head; `scroll` (pixels, clamped here)
// moves it, with arrows on the scrolled sides.
std::vector<uint8_t> renderEquationResult(const std::string& header, const std::string& label,
                                          const std::string& value, const Settings& settings, int16_t& scroll,
                                          bool labelAtTop = false);
std::vector<uint8_t> renderSolverEntry(const MathEditor& editor, const Settings& settings,
                                     bool busy, Viewport& view, bool caretOn);
std::vector<uint8_t> renderSolverSeed(const std::string& label, const MathEditor& editor, int selected, Viewport* viewport = nullptr, bool caretOn = true);
// The root rises into the free rows under the equation when tall and scrolls
// by `scroll` pixels (clamped here) when wide.
std::vector<uint8_t> renderSolverResult(const MathEditor& equation, const std::string& label,
                                      const std::string& root, const std::string& residual, int16_t& scroll);
// Notice as a non-actionable dialog, not a one-row menu. Return dismisses it.
std::vector<uint8_t> renderDialog(const std::string& message);
// Modal error, independent of notices and list selection.
std::vector<uint8_t> renderErrorDialog(Error error);
// VARIABLE grid: nine "A=<value>" labels in grid order, all visible, no title
// or scrollbar, one cell inverted; long values truncate inside their cell.
std::vector<uint8_t> renderVariableGrid(const std::array<std::string, 9>& labels, int selected);
// Store/Define field: `label` ("A=", "f(x)=") then the editor's math, sharing
// one caret and viewport. Notice band as usual; never a result region.
std::vector<uint8_t> renderAssignment(const std::string& label, const MathEditor& editor,
                                      const Settings& settings, bool busy, Viewport& view, bool caretOn = true);
// CALC prompt: the parent expression above (no caret, never scrolls) in its own
// style (parentSettings), and a value field below that is always Linear
// (fieldSettings) and owns caret and viewport (A-10: one shared style must
// never linearise the parent).
std::vector<uint8_t> renderCalcPrompt(const std::string& parent, const Settings& parentSettings,
                                      const std::string& label, const MathEditor& editor,
                                      const Settings& fieldSettings,
                                      bool busy, Viewport& view, bool caretOn = true,
                                      bool executeRow = false, const std::string& storedValue = "0");
}
