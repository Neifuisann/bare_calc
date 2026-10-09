#pragma once
#include "calculator/ui/calculator_editor.h"
#include "input/expression_editor.h"
#include <cstdint>

// Selection, scroll window and one transient cell entry for the bounded data
// grids of the nine-apps plan (Statistics, Distribution and Table lists,
// Matrix/Vector editors, coefficients; section 4). The owning app keeps the
// values, capacities, append/default rules and commit effects -- no formulas,
// no cell references.
namespace Calculator {

struct GridPolicy {
  int rows = 0, cols = 0;
  int visibleRows = 1, visibleCols = 1;
};

struct GridCursor {
  int row = 0, col = 0;
  int firstRow = 0, firstCol = 0;
};

// Pull the cursor back inside the policy and scroll it into view: after a row
// delete, a type change or a smaller capacity. An empty grid parks at 0,0.
void clampGrid(GridCursor& cursor, const GridPolicy& policy);
// Arrows step one cell and clamp at the edges (native edge wrap is plan O-8);
// Page keys move one visible page of rows, keeping the column. Returns false
// for any other key, leaving the cursor unchanged.
bool moveGrid(GridCursor& cursor, const GridPolicy& policy, ExpressionInput::Key key);

struct CellAddress {
  int row = -1, col = -1;
};

// A draft for one cell, begun against a dataset revision. The app bumps its
// revision on every committed change; a draft whose revision no longer
// matches is stale and must be discarded, never written into a moved row.
struct CellEdit {
  MathEditor editor;
  CellAddress target;
  uint32_t revision = 0;
  bool active = false;

  void begin(CellAddress cell, uint32_t datasetRevision, const std::string& text = "");
  void abort();
  bool stale(uint32_t datasetRevision) const { return active && revision != datasetRevision; }
};

}  // namespace Calculator
