#pragma once
#include "calculator/math/calculator_decimal.h"
#include "calculator/math/calculator_exact.h"
#include <cstddef>
#include <cstdint>

namespace Calculator {
// The ClassWiz display form of one exact term: coefficient * sqrt(radicand),
// coefficient*pi, or coefficient*e (beyond ClassWiz, so exact integrals such
// as e-1 read exactly). radicand=1 is rational. Always derived from
// Value::symbolic (Value::exact()/extra()), within the calculator's display
// limits; it is a view, never an independent source of exactness.
struct Exact {
  int64_t numerator = 0, denominator = 1;
  int64_t radicand = 1;
  bool pi = false, valid = false;
  bool euler = false;  // last, so {n, d, r, pi, valid} initializers keep their meaning
  // A pi or e term: shown with its constant, under the pi magnitude rule.
  bool constant() const { return pi || euler; }
};

// Display budget for one c*sqrt(n)/d term. A fraction stacks, so the numerator
// (coefficient and radicand digits) and the denominator are measured apart:
// 25sqrt(80278)/276 reads exactly instead of 25.66... Shared by
// Presentation::permittedExact and the engine's legacy formatValue so the two agree.
constexpr int64_t kExactMaxRadicand = 1000000;   // radicand < 1E6
constexpr size_t kExactMaxNumeratorChars = 10;   // coefficient + radicand digits
constexpr size_t kExactMaxDenominatorDigits = 6;
constexpr size_t kExactMaxTwoTermVisualWidth = 20;

inline size_t exactIntDigits(int64_t v) {
  size_t n = 1;
  uint64_t u = v < 0 ? static_cast<uint64_t>(-(v + 1)) + 1u : static_cast<uint64_t>(v);
  while (u >= 10) {
    u /= 10;
    ++n;
  }
  return n;
}

inline bool radicalTermFits(const Exact& term) {
  if (term.radicand <= 0 || term.radicand >= kExactMaxRadicand) return false;
  const size_t coeffDigits = (term.numerator == 1 || term.numerator == -1) ? 0 : exactIntDigits(term.numerator);
  const size_t numChars = coeffDigits + exactIntDigits(term.radicand);
  if (numChars > kExactMaxNumeratorChars) return false;
  if (term.denominator != 1 && exactIntDigits(term.denominator) > kExactMaxDenominatorDigits) return false;
  return true;
}

inline size_t exactTermVisualWidth(const Exact& term) {
  size_t numChars = 0;
  if (term.constant()) {
    const size_t coeffDigits = (term.numerator == 1 || term.numerator == -1) ? 0 : exactIntDigits(term.numerator);
    numChars = coeffDigits + 1;
  } else if (term.radicand == 1) {
    numChars = exactIntDigits(term.numerator);
  } else {
    const size_t coeffDigits = (term.numerator == 1 || term.numerator == -1) ? 0 : exactIntDigits(term.numerator);
    numChars = coeffDigits + exactIntDigits(term.radicand);
  }
  const size_t denDigits = term.denominator == 1 ? 0 : exactIntDigits(term.denominator);
  return (numChars > denDigits) ? numChars : denDigits;
}

inline bool twoTermFits(const Exact& e, const Exact& extra) {
  if (!e.valid || !extra.valid) return false;
  return exactTermVisualWidth(e) + 1 + exactTermVisualWidth(extra) <= kExactMaxTwoTermVisualWidth;
}
// pi or e times one or two non-constant display terms -- the view of an exact
// value whose every term carries the same constant, so pi(5sqrt(5)-1)/12 reads
// exactly instead of failing the one-irrational-per-term rule above. Derived
// from Value::symbolic like Exact; valid only where exact() is not.
struct FactoredExact {
  Exact first, second;  // second.valid only for two terms
  bool pi = false, valid = false;  // the constant is e when !pi
};
// A real value. `symbolic` is authoritative whenever it is exact (see
// calculator_exact.h); `number` is its decimal approximation at work
// precision, used for display, range checks and every inexact continuation.
// Nothing decides exactness, equality or zero from `number` while `symbolic`
// is exact.
struct Value {
  Decimal number;
  ExactNumber symbolic;
  Value() = default;
  // Exact only when the decimal is an int64 integer: library routines return
  // integral results this way. Anything computed approximately uses
  // approximate() instead.
  explicit Value(const Decimal& value);
  explicit Value(int value);
  static Value of(const ExactNumber& exact);        // number from the exact form (NaN on a failure)
  static Value approximate(const Decimal& value);   // explicitly inexact
  bool isExact() const { return symbolic.exact(); }
  bool isZero() const { return symbolic.exact() ? symbolic.zero() : number.zero(); }
  // ClassWiz display terms: the first and the optional second of at most two
  // c, c*sqrt(n) or c*pi terms with |c|, n <= 1E15; both invalid otherwise.
  Exact exact() const;
  Exact extra() const;
  // pi or e factored out of a value exact() cannot show (FactoredExact).
  FactoredExact factored() const;
};
// A global scalar (Ans, A-F, x, y, z): real, or complex when the Complex app
// stored one. An exactly zero imaginary part is always stored as real, so a
// real app only ever meets a complex value that genuinely has one -- and then
// gets Math ERROR at that reference, while unused complex values elsewhere in
// memory stay harmless.
struct ScalarValue {
  Value re, im;
  bool complex = false;
  ScalarValue() = default;
  ScalarValue(const Value& real) : re(real) {}  // implicit: every real store site
  static ScalarValue of(const Value& real, const Value& imaginary) {
    ScalarValue v(real);
    if (!imaginary.isZero()) {
      v.im = imaginary;
      v.complex = true;
    }
    return v;
  }
};
// P11 history compares retained answers by value: the exact form is canonical,
// so structural equality is value equality.
inline bool operator==(const Exact& a, const Exact& b) {
  return a.numerator == b.numerator && a.denominator == b.denominator &&
         a.radicand == b.radicand && a.pi == b.pi && a.euler == b.euler && a.valid == b.valid;
}
inline bool operator==(const Value& a, const Value& b) {
  return a.number == b.number && a.symbolic == b.symbolic;
}
inline bool operator==(const ScalarValue& a, const ScalarValue& b) {
  return a.complex == b.complex && a.re == b.re && (!a.complex || a.im == b.im);
}
}  // namespace Calculator
