#include "calculator/apps/calculator_quick.h"
#include "calculator/apps/calculator_matrix.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/ui/calculator_render.h"
#include "ui/ui_kit.h"
#include <algorithm>

namespace Calculator {
namespace AggregateApp {
namespace {
using K = ExpressionInput::Key;
using Kind = Outcome::Kind;
enum class View : uint8_t { List, Action, Size, Editor, Answer, Tools, StoreTo, Dimension };
View view = View::List;
bool vectors = false;
int slot = 0;  // kAnswerSlot while the answer (or its TOOLS) is up
int selection = 0, first = 0;
int sizeRows = 2, sizeCols = 2;
int dimensionField = 0;  // owning row in the size form (rows/dimensions or columns)
bool shifted = false;  // Answer: SHIFT before a continuation key
GridCursor cursor;
CellEdit edit;
CalculatorSession& session() { return calculatorSession(); }

Aggregate& at(int i) { return (vectors ? session().aggregates.vectors : session().aggregates.matrices)[static_cast<size_t>(i)]; }
Aggregate& current() { return at(slot); }
GridPolicy policy() { return {current().rows, current().cols, 4, 4}; }
void changed() { ++session().revision; }
void menuView(View v, int selected = 0) {
  view = v;
  selection = selected;
  first = 0;
}
void openEditor(int s) {
  slot = s;
  view = View::Editor;
  cursor = GridCursor{};
  edit.abort();
}

std::vector<MenuItem> rows() {
  std::vector<MenuItem> list;
  switch (view) {
    case View::List:
    case View::StoreTo:
      for (int i = 0; i < 4; ++i) {
        const Aggregate& a = at(i);
        std::string label = aggregateName(vectors, i);
        if (view == View::List)
          label += std::string(": ") + (!a.defined() ? "None" : vectors ? std::to_string(a.rows)
                                                                        : std::to_string(a.rows) + "x" + std::to_string(a.cols));
        list.push_back({i, label, view == View::List});
      }
      break;
    case View::Action:
      list = {{0, "Define New", true}, {1, "Edit", true}};
      break;
    case View::Size:
      if (vectors) list = {{0, "Dimensions: " + std::to_string(sizeRows), true}, {2, "Confirm", false}};
      else list = {{0, "Rows: " + std::to_string(sizeRows), true}, {1, "Columns: " + std::to_string(sizeCols), true},
                   {2, "Confirm", false}};
      break;
    case View::Dimension:
      for (int n = vectors ? 2 : 1; n <= (vectors ? 3 : 4); ++n)
        list.push_back({n, std::to_string(n) + (vectors ? " Dimensions" : dimensionField ? (n == 1 ? " Column" : " Columns") : (n == 1 ? " Row" : " Rows")), false});
      break;
    case View::Tools:
      list.push_back({0, vectors ? "Define Vector" : "Define Matrix", true});
      list.push_back({1, "Store", true});
      break;
    default:
      break;
  }
  return list;
}

// Return from a menu: one level up, or out of the list to the calculation.
bool back() {
  switch (view) {
    case View::List: return false;
    case View::Action: menuView(View::List, slot); return true;
    case View::Size: at(slot).defined() ? menuView(View::Action) : menuView(View::List, slot); return true;
    case View::Dimension: menuView(View::Size, dimensionField); return true;
    case View::Tools: view = slot == kAnswerSlot ? View::Answer : View::Editor; return true;
    case View::StoreTo: menuView(View::Tools); return true;
    default: return true;
  }
}

void accept(int action) {
  switch (view) {
    case View::List:
      slot = action;
      sizeRows = 2;
      sizeCols = vectors ? 1 : 2;
      at(action).defined() ? menuView(View::Action) : menuView(View::Size);
      break;
    case View::Action:
      if (action == 1) openEditor(slot);
      else menuView(View::Size);
      break;
    case View::Size:
      if (action < 2) {
        dimensionField = action;
        menuView(View::Dimension, (action == 0 ? sizeRows : sizeCols) - (vectors ? 2 : 1));
      }
      else {
        // Only Confirm replaces the old value, with zeros.
        Aggregate& a = current();
        for (auto& cell : a.cells) cell = Decimal(0);
        a.rows = static_cast<uint8_t>(sizeRows);
        a.cols = static_cast<uint8_t>(vectors ? 1 : sizeCols);
        changed();
        openEditor(slot);
      }
      break;
    case View::Dimension:
      (dimensionField == 0 ? sizeRows : sizeCols) = action;
      menuView(View::Size, dimensionField);
      break;
    case View::Tools:
      if (action == 0) menuView(View::List);
      else menuView(View::StoreTo);
      break;
    case View::StoreTo:
      at(action) = current();  // whole-object Store, then the destination's editor
      changed();
      openEditor(action);
      break;
    default:
      break;
  }
}

bool startsEntry(K key) {
  return (key >= K::Digit0 && key <= K::Digit9) || key == K::Dot || key == K::Subtract || key == K::Open ||
         key == K::Ans || key == K::Root || key == K::Sin || key == K::Cos || key == K::Tan || key == K::Log ||
         key == K::X || key == K::Shift;
}
bool continues(K key) {
  return key == K::Add || key == K::Subtract || key == K::Multiply || key == K::Divide || key == K::Power ||
         key == K::Square || key == K::Fraction || key == K::Root;
}
std::string cellText(const Decimal& d) {
  Settings norm1 = session().settings;
  norm1.number = NumberFormat::Norm1;
  return formatValue(Value(d), norm1);
}
}  // namespace

void openList(AppId app) {
  vectors = app == AppId::Vector;
  menuView(View::List);
}

void openAnswer(AppId app) {
  vectors = app == AppId::Vector;
  slot = kAnswerSlot;
  view = View::Answer;
  cursor = GridCursor{};
  shifted = false;
  edit.abort();
}

Outcome press(K key) {
  Outcome out;
  if (view == View::Editor) {
    if (edit.active) {
      if(QuickOverlay::open(edit.editor,key,vectors?AppId::Vector:AppId::Matrix))return out;
      if (key == K::Return) edit.abort();
      else if (key == K::Execute || key == K::Okay) {
        if (edit.editor.text().empty()) edit.abort();
        else { out.kind = Kind::Evaluate; out.text = edit.editor.text(); }
      } else edit.editor.press(key);
      return out;
    }
    if (moveGrid(cursor, policy(), key)) return out;
    if (key == K::Return || key == K::Clear || key == K::Okay || key == K::Execute) out.kind = Kind::Close;
    else if (key == K::Tools) menuView(View::Tools);
    else if (startsEntry(key)) {
      edit.begin({cursor.row, cursor.col}, 0);
      edit.editor.press(key);
    }
    return out;
  }
  if (view == View::Answer) {
    if (key == K::Shift) { shifted = !shifted; return out; }
    const bool shift = shifted;
    shifted = false;
    if (moveGrid(cursor, policy(), key)) return out;
    if (key == K::Clear) out.kind = Kind::Clear;
    else if (key == K::Return || key == K::Okay || key == K::Execute) out.kind = Kind::Close;
    else if (key == K::Tools) menuView(View::Tools);
    else if (continues(key)) {
      out.kind = Kind::Continue;
      out.key = key;
      out.shift = shift;
      out.text = aggregateName(vectors, kAnswerSlot);
    }
    return out;
  }
  const auto list = rows();
  if (key == K::Return) {
    if (!back()) out.kind = Kind::Close;
    return out;
  }
  if (list.empty()) return out;
  if(view!=View::Size)menuShortcut(key,selection,list.size());
  const int count = static_cast<int>(list.size());
  if (selection >= count) selection = count - 1;
  const int action = list[static_cast<size_t>(selection)].action;
  if (key == K::Down) selection = (selection + 1) % count;
  else if (key == K::Up) selection = (selection + count - 1) % count;
  else if (view == View::Size && action < 2 && (key == K::Left || key == K::Right)) {
    int& value = action == 0 ? sizeRows : sizeCols;
    const int low = vectors ? 2 : 1, high = vectors ? 3 : 4;
    value = std::max(low, std::min(high, value + (key == K::Right ? 1 : -1)));
  } else if (view == View::Size && action < 2 && key >= K::Digit1 && key <= K::Digit4) {
    const int value = static_cast<int>(key) - static_cast<int>(K::Digit0);
    if (!vectors || value == 2 || value == 3) (action == 0 ? sizeRows : sizeCols) = value;
  } else if (key == K::Okay || key == K::Execute) accept(action);
  if (view != View::Editor && view != View::Answer)
    first = UiKit::ensureVisible(first, selection, static_cast<int>(rows().size()), kMenuVisibleRows);
  return out;
}

void commitCell(const Value& value) {
  if (view != View::Editor || !edit.active) return;
  Aggregate& a = current();
  a.at(cursor.row, cursor.col) = value.number.rounded();
  changed();
  edit.abort();
  // Row-major advance; the last cell stays selected.
  if (cursor.col + 1 < a.cols) ++cursor.col;
  else if (cursor.row + 1 < a.rows) { ++cursor.row; cursor.col = 0; }
  clampGrid(cursor, policy());
}

void cellError(Error error) {
  if (edit.active) edit.editor.notice = errorText(error);
}

std::vector<uint8_t> render() {
  if (view == View::Size) {
    if (vectors) return renderSizeForm("Vector Dimension?", {{"Dimensions", sizeRows}}, selection);
    return renderSizeForm("Matrix Size?", {{"Rows", sizeRows}, {"Columns", sizeCols}}, selection);
  }
  if (view != View::Editor && view != View::Answer) return renderMenuList(rows(), selection, first);
  const Aggregate& a = current();
  AggregateView v;
  v.name = aggregateName(vectors, slot);
  v.rows = a.rows;
  v.cols = a.cols;
  for (int r = 0; r < a.rows; ++r)
    for (int c = 0; c < a.cols; ++c) v.cells.push_back(cellText(a.at(r, c)));
  v.cursor = cursor;
  if (edit.active) v.footer = edit.editor.notice.empty() ? edit.editor.text() + "_" : edit.editor.notice;
  else if (a.defined()) v.footer = cellText(a.at(cursor.row, cursor.col));
  return renderAggregate(v);
}

bool gridShown() { return view == View::Editor || view == View::Answer; }

#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot() {
  CalculatorApp::Snapshot out;
  static const char* names[]={"List","Action","Size","Editor","Answer","Tools","StoreTo","Dimension"};
  out.screen=names[static_cast<unsigned>(view)];
  out.selection=selection;out.first=first;out.editing=edit.active;
  out.gridRow=cursor.row;out.gridCol=cursor.col;
  out.text=edit.active?edit.editor.text():"";
  out.notice=edit.active?edit.editor.notice:"";
  if(gridShown()){
    const auto& a=current();out.rows=a.rows;out.cols=a.cols;
    for(int r=0;r<a.rows;++r)for(int c=0;c<a.cols;++c)out.cells.push_back(cellText(a.at(r,c)));
    out.hasResult=view==View::Answer;
  }else{
    out.menuScreen=out.screen;
    for(const auto& row:rows())out.menuLabels.push_back(row.label);
  }
  return out;
}
#endif

}  // namespace AggregateApp
}  // namespace Calculator
