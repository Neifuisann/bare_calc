#include "calculator/ui/calculator_grid.h"
#include <algorithm>

namespace Calculator {
namespace {
using K = ExpressionInput::Key;

// One axis: clamp the position, then keep it inside a window of `visible`
// that never scrolls past the last full page.
void clampAxis(int& position, int& first, int count, int visible) {
  visible = std::max(visible, 1);
  position = std::clamp(position, 0, std::max(count - 1, 0));
  if (position < first) first = position;
  if (position >= first + visible) first = position - visible + 1;
  first = std::clamp(first, 0, std::max(count - visible, 0));
}
}  // namespace

void clampGrid(GridCursor& cursor, const GridPolicy& policy) {
  clampAxis(cursor.row, cursor.firstRow, policy.rows, policy.visibleRows);
  clampAxis(cursor.col, cursor.firstCol, policy.cols, policy.visibleCols);
}

bool moveGrid(GridCursor& cursor, const GridPolicy& policy, K key) {
  const int page = std::max(policy.visibleRows, 1);
  switch (key) {
    case K::Left: --cursor.col; break;
    case K::Right: ++cursor.col; break;
    case K::Up: --cursor.row; break;
    case K::Down: ++cursor.row; break;
    case K::PageUp: cursor.row -= page; break;
    case K::PageDown: cursor.row += page; break;
    default: return false;
  }
  clampGrid(cursor, policy);
  return true;
}

void CellEdit::begin(CellAddress cell, uint32_t datasetRevision, const std::string& text) {
  editor.begin(text);
  target = cell;
  revision = datasetRevision;
  active = true;
}

void CellEdit::abort() {
  editor.begin();
  target = CellAddress{};
  active = false;
}

}  // namespace Calculator
