#pragma once
#include "calculator/ui/calculator_document.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Calculator {
// P13 L-09: evidence-gated Linear input presenter.
//
// Canonical editor source -> EditDocument -> LinearPresenter -> display
// tokens + source mapping -> paint. Display-only: it never rewrites canonical
// source, and evaluation still reads the canonical text.
//
// Only document kinds whose Linear spelling is already evidenced stay
// CONFIRMED (their canonical spelling already is the calculator-facing
// token: plain glyph runs, atomic names, visible groups, ordinary calls
// including open E-16 calls, and postfix ^() scripts from P2). Every hidden
// structural kind (legacy pow, frac, mixed, sqrt, root, abs, log, diff,
// integral, sum, product, constant, convert, recurring) keeps the safe
// verbatim fallback and is flagged PENDING until a kit (Linear I/O) or
// physical probe confirms the exact fx-880 token form. Do not invent forms.
//
// For every emitted display byte the presenter records the source offset it
// came from, so the editor caret and the horizontal viewport keep working on
// EditDocument stops (P3-P5 traversal is untouched).
namespace LinearPresenter {

// Per-kind evidence status. CONFIRMED kinds render their calculator token;
// PENDING kinds render the verbatim canonical slice behind hasPending.
enum class TokenStatus : uint8_t { Confirmed, Pending };

struct Output {
  std::string display;          // Linear display bytes (ASCII canonical subset)
  std::vector<size_t> map;      // map[i] = source offset of display[i]
  bool hasPending = false;      // true when any PENDING node was emitted
  uint16_t pendingCount = 0;    // how many PENDING nodes were emitted
};

TokenStatus statusForKind(EditDocument::Kind kind);

// Present a rebuilt document. The caller owns the document lifetime; only
// the document's current source() is read.
Output present(const EditDocument& document);

// Display column for a normalized source cursor: the number of display bytes
// coming from strictly earlier source offsets. Identity for verbatim runs.
size_t displayColumnForSource(const Output& output, size_t sourceCursor);

// Source offset for a display column (clamped). Inverse of the above.
size_t sourceForDisplayColumn(const Output& output, size_t column);

}  // namespace LinearPresenter
}  // namespace Calculator
