#include "calculator/runtime/calculator_storage.h"
#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/apps/calculator_equation.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/apps/calculator_calculate.h"
#include "calculator/math/calculator_complex.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/math/calculator_polynomial.h"
#include "calculator/ui/calculator_presentation.h"
#include "calculator/ui/calculator_render.h"
#include "ui/ui_kit.h"
#include <array>
#include <functional>
#include <memory>

namespace Calculator {
namespace EquationApp {
namespace {
using K = ExpressionInput::Key;
enum class View : uint8_t { Mode, Size, Relation, Coefficients, Solutions, SolverEntry, SolverSeed, Tools, ComplexRoots, Format, Catalog };
constexpr int kSolve = 0, kCell = 1;  // Job::destination
AppId app = AppId::Equation;
View view = View::Mode;
int selection = 0, first = 0;
EquationKind kind = EquationKind::Simul;
int size = 2;
Relation relation = Relation::Greater;
// Boot-lifetime PSRAM, not stack/internal .bss: twenty Values are 2 KB.
std::array<Value, 20>& coefficients() {
  static auto& value = CalculatorStorage::makeState<std::array<Value, 20>>();
  return value;
}
EquationKind gridKind = EquationKind::Simul;  // the shape the coefficients were entered for
int gridSize = 2;
GridCursor cursor;
CellEdit edit;
std::string notice;
std::shared_ptr<const std::vector<Solution>> solutions;
size_t page = 0;
int16_t resultScroll = 0;  // pixels into a long answer (Left/Right)
// FORMAT on an answer page, like Calculate: the shown page's format, reset
// when the page changes; SHIFT inverts the Format Menu setting once.
Format pageFormat = Format::Standard;
bool resultShift = false;
// CATALOG over an equation, initial value or coefficient draft: the shared
// catalog data, inserting into the draft it was opened from.
View catalogReturn = View::SolverEntry;
int catalogGroup = -1;  // -1: the category list
bool shared = false;  // the VARIABLE menu, hosted by CalculateApp, is open over a Solver answer
bool complexRoots = true;
View menuOrigin = View::Coefficients;
MathEditor solverEditor, seedEditor;
Viewport solverView, seedView;
int solverTarget = 6;
int seedSelection = 0;
bool seedEditing = false;
std::string seedSaved;
Error solverError=Error::None;
size_t solverErrorCursor=0;
bool revealErrorCaret=false;
// Every error is the modal ERROR screen, never an inline notice (native).
void showSolverError(Error code,size_t position){
  MathEditor& source=view==View::SolverSeed?seedEditor:view==View::Coefficients&&edit.active?edit.editor:solverEditor;
  solverError=code;
  solverErrorCursor=CalculateApp::errorReturnCursor(source.text(),code,position);
  source.notice.clear();
}

CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }

bool inequality() { return app == AppId::Inequality; }
bool solver() { return !inequality() && kind == EquationKind::Solver; }
bool hideComplexSolutions() {
  if (inequality() || kind != EquationKind::Polynomial || complexRoots || !solutions) return false;
  for (const auto& solution : *solutions)
    if (solution.value.complex && solution.value.im.number != 0) return true;
  return false;
}
int columns() { return size + 1; }
int rowCount() { return kind == EquationKind::Simul && !inequality() ? size : 1; }
GridPolicy policy() { return {rowCount(), columns(), 4, 5}; }
Value& cell(int r, int c) { return coefficients()[static_cast<size_t>(r * columns() + c)]; }
void clearCoefficients() {
  for (auto& v : coefficients()) v = Value(0);
}
void menuView(View v, int selected = 0) {
  view = v;
  selection = selected;
  first = 0;
}
void openCoefficients() {
  if (gridKind != kind || gridSize != size) {
    clearCoefficients();
    gridKind = kind;
    gridSize = size;
  }
  view = View::Coefficients;
  cursor = GridCursor{};
  edit.abort();
  notice.clear();
}
// Grid cells show decimals (native): the 5x7 face cannot draw 1/10, sqrt(2 or pi.
std::string cellText(const Value& v) {
  Settings norm1 = session().settings;
  norm1.number = NumberFormat::Norm1;
  return formatValue(v, norm1, Format::Decimal);
}

// Formula shown by the menus, the coefficient row and the answer headers.
std::string formula() { return polynomialFormula(size); }
std::string relationText() {
  static const char* const kRelations[] = {">0", "<0", "\x04" "0", "\x03" "0"};
  return kRelations[static_cast<int>(relation)];
}
// Inequality answer template (native): each root becomes a, b, c... in the
// order written, relation signs drawn as the single glyphs. Empty for a
// rootless text answer (All Real Numbers, No Solution).
std::string inequalityTemplate(const std::string& answer) {
  std::string out;
  char next = 'a';
  bool root = false;
  for (size_t i = 0; i < answer.size(); ++i) {
    const char ch = answer[i];
    if (ch == '\x01') { out += next++; root = true; continue; }
    if (ch == '\x02') { root = false; continue; }
    if (root) continue;
    if ((ch == '<' || ch == '>') && i + 1 < answer.size() && answer[i + 1] == '=') {
      out += ch == '<' ? '\x03' : '\x04';
      ++i;
      continue;
    }
    out += ch;
  }
  return root || next == 'a' ? std::string() : out;
}
// Native answer captions: x1..x4 as subscripts under the equation; the
// quadratic's vertex and the cubic's extrema under "Min/Max of y=...".
void answerCaption(std::string& label, std::string& header) {
  // Solver: x1..xn subscripted under no header (the answer screen is exclusive).
  if (solver()) {
    if (label.size() == 2) label = label.substr(0, 1) + "_" + label[1];
    return;
  }
  if (kind != EquationKind::Polynomial) return;
  const size_t dash = label.find('-');
  if (dash != std::string::npos) {
    header = label.substr(dash + 1) + " of y=" + formula();
    label = label.substr(0, dash);
    return;
  }
  header = formula() + "=0";
  if (label.size() == 2 && label[0] == 'x') label = std::string("x_") + label[1];
}

// The shown answer page as a Result, for the FORMAT rows and presentation.
bool valuePage() {
  return !hideComplexSolutions() && solutions && page < solutions->size() && !(*solutions)[page].label.empty();
}
// Heap, not the loop stack: a Result carries its Memory (~3 KB).
std::unique_ptr<Result> pageResult() {
  auto r = std::make_unique<Result>();
  const Solution& p = (*solutions)[page];
  r->value = p.value.re;
  if (p.value.complex && !p.value.im.number.zero()) { r->complex = true; r->imag = p.value.im; }
  return r;
}

std::vector<MenuItem> rows() {
  switch (view) {
    case View::Mode:
      return {{0, "Simul Equation", true}, {1, "Polynomial", true}, {2, "Solver", true}};
    case View::Size:
      if (kind == EquationKind::Simul && !inequality())
        return {{2, "2 Unknowns", true}, {3, "3 Unknowns", true}, {4, "4 Unknowns", true}};
      // Drawn as formulas by renderPolynomialMenu; these labels name the rows.
      return {{2, "Degree 2", true}, {3, "Degree 3", true}, {4, "Degree 4", true}};
    case View::Relation:
      // ClassWiz15 menu rows are ASCII: >= and <= stand in for the native signs (O-8).
      return {{0, "> 0", true}, {1, "< 0", true}, {2, ">= 0", true}, {3, "<= 0", true}};
    case View::Tools:
      return {{0, "Complex Roots", true}};
    case View::Format:
      return valuePage() ? formatRows(pageResult().get(), true, session().settings) : std::vector<MenuItem>{};
    case View::Catalog: {
      if (catalogGroup >= 0) return catalogCategoryRows(catalogGroup);
      // The constant and conversion tables insert into Calculate's own editor only.
      std::vector<MenuItem> out;
      for (const auto& row : catalogRootRows(false, app)) if (row.action < CatalogRootAction::SciConstants) out.push_back(row);
      return out;
    }
    case View::ComplexRoots:
      return {{1, "On", false, complexRoots ? UiKit::Mark::kRadioOn : UiKit::Mark::kRadioOff},
              {0, "Off", false, complexRoots ? UiKit::Mark::kRadioOff : UiKit::Mark::kRadioOn}};
    default:
      return {};
  }
}

void start(Job& job) {
  if (jobs().start(job) == JobService::Start::NoMemory) notice = "Not enough memory";
}
void solve() {
  auto task = std::make_shared<EquationTask>();
  task->kind = inequality() ? EquationKind::Inequality : kind;
  task->size = static_cast<uint8_t>(size);
  task->relation = relation;
  std::string expression;
  if (solver()) {
    expression = solverEditor.text();
    task->seed = seedEditor.text();
  } else {
    for (int i = 0; i < rowCount() * columns(); ++i) task->c[static_cast<size_t>(i)] = coefficients()[static_cast<size_t>(i)];
  }
  Job& job = prepareJob(app, kSolve, expression);
  job.equation = task;
  start(job);
}

bool startsEntry(K key) {
  return (key >= K::Digit0 && key <= K::Digit9) || key == K::Dot || key == K::Subtract || key == K::Open ||
         key == K::Ans || key == K::Root || key == K::Sin || key == K::Cos || key == K::Tan || key == K::Log ||
         key == K::Fraction || key == K::Shift;
}

// Solver's unknown: x when the equation has one, else its first variable.
int solverVariable(const std::string& text,size_t& errorPosition) {
  const Parsed parsed = parse(text);
  if (parsed.error != Error::None) { errorPosition=parsed.position; return -2; }
  int target = -1;
  std::function<void(const Expression&)> find = [&](const Expression& n) {
    if (!n) return;
    if (n->kind == Node::Kind::Symbol) {
      const int i = variableIndex(n->text);
      if (i == 6 || (i >= 0 && target < 0)) target = i;
    }
    for (const auto& child : n->children) find(child);
  };
  find(parsed.root);
  return target;
}

void pressMenu(K key) {
  const auto list = rows();
  const int count = static_cast<int>(list.size());
  if (view == View::Tools || view == View::ComplexRoots) {
    if (key == K::Clear) { view = menuOrigin; return; }
    if (key == K::Return) {
      if (view == View::ComplexRoots) menuView(View::Tools);
      else view = menuOrigin;
      return;
    }
  }
  if (key == K::Return) {
    if (view == View::Size && !inequality()) menuView(View::Mode, static_cast<int>(kind));
    else if (view == View::Relation) menuView(View::Size, size - 2);
    return;
  }
  if (!count) return;
  menuShortcut(key,selection,list.size());
  if (selection >= count) selection = count - 1;
  if (key == K::Down) selection = (selection + 1) % count;
  else if (key == K::Up) selection = (selection + count - 1) % count;
  else if (key == K::Okay || key == K::Execute) {
    const int action = list[static_cast<size_t>(selection)].action;
    if (view == View::Tools) {
      menuView(View::ComplexRoots, complexRoots ? 0 : 1);
    } else if (view == View::ComplexRoots) {
      complexRoots = action != 0;
      page = 0;
      menuView(View::Tools);
    } else if (view == View::Mode) {
      kind = static_cast<EquationKind>(action);
      if (solver()) view = View::SolverEntry;
      else menuView(View::Size);
    } else if (view == View::Size) {
      size = action;
      if (inequality()) menuView(View::Relation, static_cast<int>(relation));
      else openCoefficients();
    } else {
      relation = static_cast<Relation>(action);
      openCoefficients();
    }
  }
  if (view == View::Mode || view == View::Size || view == View::Relation)
    first = UiKit::ensureVisible(first, selection, static_cast<int>(rows().size()), kMenuVisibleRows);
}

MathEditor& catalogTarget() {
  return catalogReturn == View::SolverEntry ? solverEditor : catalogReturn == View::SolverSeed ? seedEditor : edit.editor;
}
void openCatalog(View from) {
  catalogReturn = from;
  catalogGroup = -1;
  menuView(View::Catalog);
}
void pressCatalog(K key) {
  const auto list = rows();
  const int count = static_cast<int>(list.size());  if (key == K::Return || key == K::Clear || !count) {
    if (catalogGroup >= 0) {
      const int group = catalogGroup;
      catalogGroup = -1;
      int at = 0;
      const auto parent = rows();
      for (size_t i = 0; i < parent.size(); ++i) if (parent[i].action == group) at = static_cast<int>(i);
      menuView(View::Catalog, at);
      first = UiKit::ensureVisible(0, at, static_cast<int>(parent.size()), kMenuVisibleRows);
    } else view = catalogReturn;
    return;
  }
  menuShortcut(key, selection, list.size());
  if (selection >= count) selection = count - 1;
  if (key == K::Down) selection = (selection + 1) % count;
  else if (key == K::Up) selection = (selection + count - 1) % count;
  else if (key == K::Okay || key == K::Execute) {
    const int action = list[static_cast<size_t>(selection)].action;
    if (catalogGroup < 0) { catalogGroup = action; menuView(View::Catalog); }
    else {
      // As Calculate inserts: a one-argument call opens like its function key,
      // everything else keeps its template form.
      const std::string text = catalogData()[static_cast<size_t>(catalogGroup)].entries[static_cast<size_t>(action)].expression;
      const size_t n = text.size();
      MathEditor& target = catalogTarget();
      if (text != "rand()" && text != "abs()" && n > 2 && text.compare(n - 2, 2, "()") == 0 &&
          text.find(',') == std::string::npos && text.find('^') == std::string::npos)
        target.insertTemplate(text.substr(0, n - 1));
      else if (text == "rand()") target.insert(text);
      else target.insertTemplate(text);
      view = catalogReturn;
      return;
    }
  }
  first = UiKit::ensureVisible(first, selection, static_cast<int>(rows().size()), kMenuVisibleRows);
}
}  // namespace

void enter(AppId next, bool relaunch) {
  if (relaunch || next != app) {
    app = next;
    kind = inequality() ? EquationKind::Polynomial : EquationKind::Simul;
    size = gridSize = 2;
    gridKind = kind;
    relation = Relation::Greater;
    clearCoefficients();
    solutions.reset();
    complexRoots = true;
    solverError=Error::None;revealErrorCaret=false;
    solverEditor.begin();
    seedEditor.begin();
    menuView(inequality() ? View::Size : View::Mode);
  }
  if (view == View::Solutions && !solutions) view = inequality() ? View::Size : View::Mode;
  edit.abort();
  shared = false;
}

void pause() {
  edit.abort();
  shared = false;
}

void interrupt() {
  solverEditor.press(K::Interrupted);
  seedEditor.press(K::Interrupted);
  edit.abort();
}

void press(K key) {
  if (shared) {
    CalculateApp::press(key);
    shared = CalculateApp::sharedActive();
    return;
  }
  // A SHIFT+letter store confirmation shows on the answer page for one key,
  // whichever key (or menu) comes next.
  if (view == View::Solutions) notice.clear();
  if(solverError!=Error::None){
    if(key==K::Okay||key==K::Clear||key==K::Return){
      const bool cellDraft=view==View::Coefficients&&edit.active;
      if(solver()||cellDraft){
        MathEditor& source=view==View::SolverSeed?seedEditor:cellDraft?edit.editor:solverEditor;
        source.setCursor(solverErrorCursor);
        if(view==View::SolverSeed){seedSaved=source.text();seedEditing=true;seedSelection=0;}
        revealErrorCaret=true;
      }
      solverError=Error::None;
    }
    return;
  }
  if(!jobs().running()){
    MathEditor* input=edit.active?&edit.editor:view==View::SolverEntry?&solverEditor:view==View::SolverSeed&&seedEditing?&seedEditor:nullptr;
    if(input&&QuickOverlay::open(*input,key,app))return;
    if(input&&key==K::Catalog){
      input->disarmIns();
      openCatalog(edit.active?View::Coefficients:view);
      return;
    }
  }
  if (view == View::Catalog) { pressCatalog(key); return; }
  if (jobs().running()) {
    if (key == K::Clear || key == K::Return) {
      jobs().cancel();
      notice = "Cancelled";
    }
    return;
  }
  if (key == K::Tools && !inequality() && kind == EquationKind::Polynomial &&
      (view == View::Coefficients || view == View::Solutions) && !edit.active) {
    menuOrigin = view;
    menuView(View::Tools);
    return;
  }
  if (view == View::Format) {
    const auto list = rows();
    if (key == K::Return || key == K::Clear || list.empty()) { view = View::Solutions; return; }
    menuShortcut(key, selection, list.size());
    if (selection >= static_cast<int>(list.size())) selection = static_cast<int>(list.size()) - 1;
    const int count = static_cast<int>(list.size());
    if (key == K::Down) selection = (selection + 1) % count;
    else if (key == K::Up) selection = (selection + count - 1) % count;
    else if (key == K::Okay || key == K::Execute) {
      pageFormat = static_cast<Format>(list[static_cast<size_t>(selection)].action);
      resultScroll = 0;
      view = View::Solutions;
      return;
    }
    first = UiKit::ensureVisible(first, selection, count, kMenuVisibleRows);
    return;
  }
  switch (view) {
    case View::Coefficients:
      if (edit.active) {
        if (key == K::Return) edit.abort();
        else if (key == K::Execute || key == K::Okay) {
          if (edit.editor.text().empty()) edit.abort();
          else start(prepareJob(app, kCell, edit.editor.text()));
        } else edit.editor.press(key);
        return;
      }
      notice.clear();
      if (moveGrid(cursor, policy(), key)) return;
      if (key == K::Execute || key == K::Okay) solve();
      else if (key == K::Clear) clearCoefficients();
      else if (key == K::Return) {
        if (inequality()) menuView(View::Relation, static_cast<int>(relation));
        else menuView(View::Size, size - 2);
      } else if (startsEntry(key)) {
        edit.begin({cursor.row, cursor.col}, 0);
        edit.editor.press(key);
      }
      return;
    case View::Solutions: {
      if (key == K::Variable) {
        // Store the shown root, vertex or residual through the shared
        // VARIABLE menu. Inequality answers are intervals: nothing to store.
        if (inequality() || !valuePage()) return;
        const ScalarValue& value = (*solutions)[page].value;
        ResultSummary summary;
        summary.value = value.re;
        if (value.complex && !value.im.number.zero()) { summary.complex = true; summary.imag = value.im; }
        CalculateApp::openShared(app, key, &summary);
        shared = true;
        return;
      }
      // SHIFT then a letter key stores the shown value straight into that
      // variable (SHIFT 4 = A, as the editor types it). The page stays, so
      // Down then SHIFT 5 stores the next root into B. Ans is untouched.
      const int target = resultShift ? shiftedVariableIndex(key) : -1;
      if (target >= 0) {
        resultShift = false;
        if (inequality() || !valuePage()) { notice = "Cannot store"; return; }
        session().memory.variables[static_cast<size_t>(target)] = (*solutions)[page].value;
        ++session().revision;
        notice = std::string("Stored in ") + variableName(target);
        return;
      }
      // A Solver answer that fits the screen has nothing to scroll, so Left and
      // Right page like Up and Down; only a wide one scrolls with them.
      if (solver() && lastRender().resultOverflow == 0 && (key == K::Left || key == K::Right))
        key = key == K::Right ? K::Down : K::Up;
      if (key == K::Left || key == K::Right) {
        resultScroll = static_cast<int16_t>(resultScroll + (key == K::Right ? 24 : -24));
        if (resultScroll < 0) resultScroll = 0;  // the renderer clamps the far end
        return;
      }
      if (key == K::Shift) { resultShift = !resultShift; return; }
      if (key == K::Format) {
        const bool wantMenu = session().settings.formatMenu != resultShift;
        resultShift = false;
        if (!valuePage()) return;
        resultScroll = 0;
        if (!wantMenu) { pageFormat = pageFormat == Format::Decimal ? Format::Standard : Format::Decimal; return; }
        const Format current = pageFormat;
        view = View::Format;
        const auto list = rows();
        if (list.size() <= 1) { view = View::Solutions; return; }
        selection = 0;
        for (size_t i = 0; i < list.size(); ++i) if (list[i].action == static_cast<int>(current)) selection = static_cast<int>(i);
        first = UiKit::ensureVisible(0, selection, static_cast<int>(list.size()), kMenuVisibleRows);
        return;
      }
      resultShift = false;
      const size_t count = hideComplexSolutions() ? 1 : solutions ? solutions->size() : 0;
      const View parent = solver() ? View::SolverEntry : View::Coefficients;
      resultScroll = 0;
      pageFormat = Format::Standard;
      // OK and EXE step to the next answer like the down arrow (native).
      if (key == K::Down || key == K::Execute || key == K::Okay) {
        if (page + 1 < count) ++page;
        else view = parent;
      } else if (key == K::Up) {
        if (page) --page;
      } else if (key == K::Return && !inequality() && page > 0) {
        // fx-880 manual, polynomial result pages: BACK follows the previous
        // result indicator. Inequality BACK still returns to coefficients.
        --page;
      } else if (key == K::Return || key == K::Clear) view = parent;
      return;
    }
    case View::SolverEntry:
      if (key == K::Return) { menuView(View::Mode, 2); return; }
      if (key == K::Execute || key == K::Okay) {
        if (solverEditor.text().empty()) return;
        size_t errorPosition=0;
        const int target = solverVariable(solverEditor.text(),errorPosition);
        if (target == -2) { showSolverError(Error::Syntax,errorPosition); return; }
        if (target < 0) { showSolverError(Error::Variable,solverEditor.text().size()); return; }
        solverTarget = target;
        // The initial value starts from the variable's current value.
        const ScalarValue& value = session().memory.variables[static_cast<size_t>(target)];
        // Decimal, whatever the stored form: a seed is a starting point, and
        // a fraction or radical will not fit the one-line field. The
        // selection lands on Execute; the field is reached with Up.
        seedEditor.begin(value.complex ? "0" : formatValue(value.re, session().settings, Format::Decimal));
        seedView = Viewport{};
        seedSelection=1;seedEditing=false;
        view = View::SolverSeed;
        return;
      }
      solverEditor.press(key);
      return;
    case View::SolverSeed:
      if(seedEditing){
        if(key==K::Return){seedEditor.begin(seedSaved);seedEditing=false;return;}
        if(key==K::Execute||key==K::Okay){
          if(seedEditor.text().empty())seedEditor.begin("0");
          seedEditing=false;seedSelection=1;return;
        }
        seedEditor.press(key);return;
      }
      if (key == K::Return) { view = View::SolverEntry; return; }
      if(key==K::Up||key==K::Down){seedSelection=1-seedSelection;return;}
      if(key==K::Execute||key==K::Okay){
        if(seedSelection==1)solve();
        else {seedSaved=seedEditor.text();seedEditing=true;seedView={};}
        return;
      }
      if(seedSelection==0&&(startsEntry(key)||key==K::Clear)){
        seedSaved=seedEditor.text();seedEditor.begin();seedView={};seedEditing=true;
        seedEditor.press(key);
      }
      return;
    default:
      pressMenu(key);
      return;
  }
}

void adopt(const Job& job, Result& output) {
  if (shared) { CalculateApp::adopt(job, output); return; }
  if (job.destination == kCell) {
    if (view != View::Coefficients || !edit.active) return;
    if (output.error != Error::None) { showSolverError(output.error, output.position); return; }
    Value v = output.value;
    v.number = v.number.rounded();
    cell(cursor.row, cursor.col) = v;
    edit.abort();
    if (cursor.col + 1 < columns()) ++cursor.col;
    else if (cursor.row + 1 < rowCount()) { ++cursor.row; cursor.col = 0; }
    clampGrid(cursor, policy());
    return;
  }
  if (output.error != Error::None) {
    if (solver()) {
      if(output.error!=Error::Cancelled){showSolverError(output.error,output.position);return;}
      (view == View::SolverSeed ? seedEditor : solverEditor).notice = errorText(output.error);
      if(view==View::SolverSeed)seedEditing=true;
    }
    else if (output.error == Error::Cancelled) notice = errorText(output.error);
    else showSolverError(output.error, 0);
    return;
  }
  solutions = output.solutions;
  page = 0;
  resultScroll = 0;
  pageFormat = Format::Standard;
  resultShift = false;
  view = View::Solutions;
  if (solver()) {
    session().memory = output.memory;  // the root is assigned to its variable
    ++session().revision;
  }
}

bool caretVisible() {
  if (shared) return CalculateApp::caretVisible();
  return solverError==Error::None && (view == View::SolverEntry || (view == View::SolverSeed && seedEditing) || (view == View::Coefficients && edit.active)) && !jobs().running();
}

std::vector<uint8_t> render(bool caretOn) {
  if (shared) return CalculateApp::render(caretOn);
  if(solverError!=Error::None)return renderErrorDialog(solverError);
  if(revealErrorCaret){caretOn=true;revealErrorCaret=false;}
  const bool busy = jobs().running();
  const Settings& s = session().settings;
  switch (view) {
    case View::Coefficients: {
      // Modal CW spinner while the solver runs (see renderAssignment in
      // calculator_render.cpp); explicit notices keep the footer instead.
      if (busy && notice.empty() && !edit.active) return Busy::renderBusy();
      DataGridView v;
      v.equation=true;
      v.polynomial=inequality()||kind==EquationKind::Polynomial;
      if(inequality())v.relation=relationText();
      for (int c = 0; c < columns(); ++c) v.headers.push_back(std::string(1, "abcde"[c]));
      for (int r = 0; r < rowCount(); ++r)
        for (int c = 0; c < columns(); ++c) v.cells.push_back(cellText(cell(r, c)));
      v.cursor = cursor;
      if (edit.active) {v.editor=&edit.editor;v.caretOn=caretOn;v.footer=edit.editor.notice;}
      else if (!notice.empty()) v.footer = notice;
      else v.footer = cellText(cell(cursor.row, cursor.col));
      return renderDataGrid(v);
    }
    case View::Solutions: {
      // A SHIFT+letter store confirms (or refuses) in place of the header,
      // for one key.
      auto head = [](const std::string& header) { return notice.empty() ? header : notice; };
      if (hideComplexSolutions()) return renderEquationResult(head(formula() + "=0"), "", "No Real Roots", s, resultScroll);
      if (!solutions || page >= solutions->size()) return renderEquationResult(head(""), "", "No Solution", s, resultScroll);
      const Solution& p = (*solutions)[page];
      if (p.label.empty()) {
        if (inequality()) {
          const std::string header = inequalityTemplate(p.text);
          return renderEquationResult(head(header.empty() ? formula() + relationText() : header), "", p.text, s, resultScroll);
        }
        return renderEquationResult(head(kind == EquationKind::Polynomial ? "y=" + formula() : ""), "", p.text, s, resultScroll);
      }
      const bool math = s.io == IO::MathMath || s.io == IO::MathDecimal;
      // A FORMAT choice goes through Calculate's typed presentation.
      const std::string text = pageFormat != Format::Standard
          ? Presentation::toTemplateSource(Presentation::presentSummary(toSummary(*pageResult(), pageFormat), s, pageFormat), s)
          : p.value.complex ? formatComplex(p.value.re, p.value.im, s, Format::Standard, math ? presentResult : nullptr)
                            : "\x01" + formatValue(p.value.re, s) + "\x02";
      std::string header, label = p.label;
      answerCaption(label, header);
      return renderEquationResult(head(header), label, text, s, resultScroll, solver());
    }
    case View::SolverEntry:
      return renderSolverEntry(solverEditor,s,busy,solverView,caretOn);
    case View::SolverSeed:
      if(busy)return Busy::renderBusy();
      return renderSolverSeed(std::string(1,variableName(solverTarget)),seedEditor,seedSelection,
                              seedEditing ? &seedView : nullptr,caretOn);
    case View::Size:
      if(inequality()||kind==EquationKind::Polynomial)return renderPolynomialMenu(selection);
      return renderMenuList(rows(), selection, first);
    case View::Relation:
      return renderPolynomialMenu(selection,size);
    case View::Format:
      return renderMenuList(rows(), selection, first);
    default:
      return renderMenuList(rows(), selection, first);
  }
}

PanelIcons::State icons() {
  if (shared) return CalculateApp::icons();
  if(solverError!=Error::None)return {};
  IconView v;
  if (view == View::Mode || view == View::Size || view == View::Relation || view == View::Tools || view == View::ComplexRoots || view == View::Format || view == View::Catalog) return desiredIcons(v);
  const Settings& s = session().settings;
  v.calculation = view != View::SolverSeed;
  v.field = view == View::SolverSeed;
  v.angle = s.angle;
  v.io = s.io;
  v.number = s.number;
  v.engineer = s.engineer;
  v.shifted = view == View::SolverEntry ? solverEditor.shifted : view == View::SolverSeed ? seedEditor.shifted
            : view == View::Solutions ? resultShift : edit.editor.shifted;
  if (view == View::Solutions && solutions && !hideComplexSolutions()) {
    v.historyPrev = page > 0;
    v.historyNext = page + 1 < solutions->size();
  }
  // Polynomial answers can be complex, so they show the i/angle mark.
  v.complexApp = !inequality() && kind == EquationKind::Polynomial;
  v.complexResult = s.complexResult;
  return desiredIcons(v);
}

#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot() {
  if(shared)return CalculateApp::snapshot();
  CalculatorApp::Snapshot out;
  static const char* names[]={"Mode","Size","Relation","Coefficients","Solutions","SolverEntry","SolverSeed","Tools","ComplexRoots","Format","Catalog"};
  out.screen=names[static_cast<unsigned>(view)];out.selection=selection;out.first=first;
  out.gridRow=cursor.row;out.gridCol=cursor.col;out.page=static_cast<int>(page);
  out.editing=edit.active;out.notice=edit.active?edit.editor.notice:notice;
  out.caret=lastRender().caret;out.caretRect=lastRender().caretRect;
  if(view==View::SolverSeed){out.selection=seedSelection;out.editing=seedEditing;out.text=seedEditor.text();}
  if(view==View::SolverEntry)out.text=solverEditor.text();
  if(view==View::Coefficients){
    out.rows=rowCount();out.cols=columns();out.text=edit.active?edit.editor.text():"";
    for(int r=0;r<rowCount();++r)for(int c=0;c<columns();++c)out.cells.push_back(cellText(cell(r,c)));
  }
  if(view==View::Solutions){
    out.hasResult=true;
    // Observe the renderer's delivered text, never build a second presenter.
    for(char ch:lastRender().presented)if(ch!='\x01'&&ch!='\x02')out.resultText+=ch;
  }
  if(solverError!=Error::None){
    out.errorOrigin=out.screen;out.screen="ErrorDialog";
    out.errorTitle=errorText(solverError);out.errorCursor=solverErrorCursor;
    out.editing=false;
  }
  for(const auto& row:rows())out.menuLabels.push_back(row.label);
  if(!out.menuLabels.empty())out.menuScreen=out.screen;
  return out;
}
#endif

}  // namespace EquationApp
}  // namespace Calculator
