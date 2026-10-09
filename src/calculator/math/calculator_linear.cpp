#include "calculator/math/calculator_linear.h"
#include "calculator/math/calculator_eng_symbols.h"
#include "calculator/math/calculator_loose_paren.h"

namespace Calculator {
namespace LinearPresenter {
namespace {

// Evidence ledger (P13 L-09). Status CONFIRMED means the canonical spelling
// below is already the calculator-facing Linear token by direct evidence:
// P2 postfix scripts, E-16 open calls, or plain ASCII with no hidden syntax.
// PENDING means the exact fx-880 Linear token form is UNKNOWN until a kit
// Linear-I/O probe or physical capture confirms it; the presenter emits the
// verbatim canonical slice so nothing is hidden and nothing is invented.
//
// Kind          | canonical example | target token | evidence      | status
// Glyph runs    | 12+3*(4-5)        | as-is        | SRC/P2        | CONFIRMED
// Name          | Ans, pi, sin name  | as-is        | SRC/E-16      | CONFIRMED
// Group         | (2+3)              | as-is        | SRC           | CONFIRMED
// Call          | sin(1), log(       | as-is        | SRC/E-16      | CONFIRMED
// Script        | 9^(3)              | as-is        | P2 reference  | CONFIRMED
// Power         | pow(9,3) legacy    | UNKNOWN      | none          | PENDING
// Fraction      | frac(1,2)          | UNKNOWN      | none          | PENDING
// Mixed         | mixed(1,2,3)       | UNKNOWN      | none          | PENDING
// Sqrt          | sqrt(2)            | UNKNOWN      | none          | PENDING
// Root          | root(3,8)          | UNKNOWN      | none          | PENDING
// Abs           | abs(-3)            | UNKNOWN      | none          | PENDING
// Log           | log(2,8)           | UNKNOWN      | none          | PENDING
// Diff          | diff(x,3)          | UNKNOWN      | none          | PENDING
// Integral      | integral(x,0,1)    | UNKNOWN      | none          | PENDING
// Sum/Product   | sum(x,1,5)         | UNKNOWN      | none          | PENDING
// Constant      | constant(0)        | UNKNOWN      | none          | PENDING
// Convert       | convert(5,0)       | UNKNOWN      | none          | PENDING
// Recurring     | recur(3)           | UNKNOWN      | none          | PENDING

void emitSlice(Output& out, const std::string& source, size_t begin, size_t end) {
  for (size_t i = begin; i < end && i < source.size(); ++i) {
    // Engineering symbols and loose parens are private bytes; Linear text
    // shows their letter / ASCII paren.
    out.display.push_back(LooseParen::ascii(EngSymbol::ascii(source[i])));
    out.map.push_back(i);
  }
}

}  // namespace

TokenStatus statusForKind(EditDocument::Kind kind) {
  using Kind = EditDocument::Kind;
  switch (kind) {
    case Kind::Glyph:
    case Kind::Name:
    case Kind::Group:
    case Kind::Call:
    case Kind::Script:
      return TokenStatus::Confirmed;
    default:
      return TokenStatus::Pending;
  }
}

Output present(const EditDocument& document) {
  Output out;
  const std::string& source = document.source();
  out.display.reserve(source.size());
  out.map.reserve(source.size());
  if (document.fields() == 0) return out;
  // Linear display is flat: emit the root field's items in order. Each item
  // span covers its whole construct (document.cpp finish()), so a verbatim
  // slice never splits hidden syntax. Structural children stay inside the
  // parent slice until their token form is confirmed.
  uint16_t item = document.field(document.root()).first;
  size_t covered = 0;
  while (item != EditDocument::kNone) {
    const EditDocument::Item& it = document.item(item);
    if (statusForKind(it.kind) == TokenStatus::Pending) {
      out.hasPending = true;
      ++out.pendingCount;
    }
    emitSlice(out, source, it.begin, it.end);
    covered = it.end;
    item = it.next;
  }
  // Defensive tail: the item chain covers the whole source today, but a
  // future document change must degrade to verbatim text, never drop bytes.
  emitSlice(out, source, covered, source.size());
  return out;
}

size_t displayColumnForSource(const Output& output, size_t sourceCursor) {
  size_t column = 0;
  while (column < output.map.size() && output.map[column] < sourceCursor) ++column;
  return column;
}

size_t sourceForDisplayColumn(const Output& output, size_t column) {
  if (column >= output.map.size()) return output.map.empty() ? 0 : output.map.back() + 1;
  return output.map[column];
}

}  // namespace LinearPresenter
}  // namespace Calculator
