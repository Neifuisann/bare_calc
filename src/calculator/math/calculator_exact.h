#pragma once
#include "calculator/math/calculator_bigint.h"
#include "calculator/math/calculator_decimal.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Calculator {
// Why an exact number is or is not available. DivisionByZero and NotReal are
// certain mathematical errors (the caller maps them to Math ERROR); Inexact
// only means "no exact form here" and the caller continues in decimal.
enum class ExactStatus : uint8_t { Inexact, Exact, DivisionByZero, NotReal };

struct ExactRep;

// An exact real number in a canonical normal form, so that equal values have
// equal representations and zero is recognized structurally -- never by
// comparing decimals. See docs/calculator/EXACT_ARITHMETIC.md for the form and its
// limits. The value is a sum of terms
//
//   coefficient * root(k, n) * pi^a * e^b * ln(p)^i * ln(A)^j * root(m, B)^l
//
// with a rational coefficient, a canonical radical (n k-th-power free, k
// minimal), rational exponents on pi and e, and integer exponents on the
// logarithm and nested-radical atoms. Rationals (the common case) live inline
// without heap; anything else shares one immutable representation.
//
// Every operation either stays exact, reports a certain error, or returns
// Inexact; it never returns an exact-looking result that is only
// approximately right. Sizes are bounded (bits, terms, nesting), and a result
// past a bound is Inexact.
class ExactNumber {
 public:
  static constexpr size_t kMaxBits = 4096;      // any integer in the form
  static constexpr size_t kMaxTerms = 64;       // terms in one sum
  static constexpr unsigned kMaxDepth = 4;      // nested ln()/root() atoms
  static constexpr uint32_t kMaxIndex = 64;     // radical index
  static constexpr unsigned kMaxDigits = 1000;  // approximate() precision

  ExactNumber() = default;  // Inexact
  static ExactNumber integer(const BigInt& value);
  static ExactNumber rational(const Rational& value);
  static ExactNumber fraction(int64_t numerator, int64_t denominator);  // denominator 0: DivisionByZero
  static ExactNumber failure(ExactStatus status);
  static ExactNumber pi();
  static ExactNumber e();
  // A decimal literal ("12", "0.25", ".5", "1.5E-3", "-2E+4"), exactly.
  // Returns Inexact for anything else.
  static ExactNumber fromLiteral(const std::string& literal);
  // The canonical text produced by str(), or any expression over numbers,
  // pi, e, + - * / ^, parentheses, sqrt(x), root(k,x), ln(x), exp(x).
  // Returns Inexact on a syntax error.
  static ExactNumber parse(const std::string& text);

  ExactStatus status() const { return status_; }
  bool exact() const { return status_ == ExactStatus::Exact; }
  bool failed() const { return status_ == ExactStatus::DivisionByZero || status_ == ExactStatus::NotReal; }
  bool zero() const { return exact() && !rep_ && rational_.zero(); }
  bool isRational() const { return exact() && !rep_; }
  bool isInteger() const { return isRational() && rational_.integer(); }
  const Rational& rationalValue() const { return rational_; }  // isRational() only
  // -1, 0 or +1. Zero is structural; a nonzero value's sign comes from a
  // rigorous approximation. `certain` is false only when that approximation
  // runs out of precision; the result is then 0 and must not be trusted.
  int sign(bool* certain = nullptr) const;
  size_t heapBytes() const;
  // Nesting depth of ln()/root() atoms (0 for a radical/pi/e expression).
  unsigned depth() const;

  // Canonical calculator syntax: parse(str()) reproduces the same value, and
  // equal values print identically ("5*sqrt(2)", "10/21", "1+sqrt(2)").
  std::string str() const;
  // Decimal work precision (40 digits), correctly rounded except within one
  // unit of a rounding boundary. NaN when not exact.
  Decimal toDecimal() const;
  // `digits` significant digits (1..kMaxDigits), rounded half away from zero,
  // trailing zeros trimmed, E-notation outside 1E-5 <= |x| < 1E+digits.
  // Empty when not exact.
  std::string approximate(unsigned digits) const;

  ExactNumber operator-() const;
  friend ExactNumber operator+(const ExactNumber& a, const ExactNumber& b);
  friend ExactNumber operator-(const ExactNumber& a, const ExactNumber& b);
  friend ExactNumber operator*(const ExactNumber& a, const ExactNumber& b);
  friend ExactNumber operator/(const ExactNumber& a, const ExactNumber& b);
  ExactNumber pow(const ExactNumber& exponent) const;
  ExactNumber root(uint32_t index) const;  // principal real root; odd roots of negatives are negative
  ExactNumber sqrt() const { return root(2); }
  ExactNumber ln() const;
  ExactNumber exp() const;
  ExactNumber log(const ExactNumber& base) const;  // log_base(this)
  ExactNumber abs() const;
  ExactNumber inverse() const;

  // Structural identity of the canonical form (status included). For exact
  // numbers this IS value equality.
  friend bool operator==(const ExactNumber& a, const ExactNumber& b);
  friend bool operator!=(const ExactNumber& a, const ExactNumber& b) { return !(a == b); }
  // Numeric order of two exact numbers: equality is structural, only the
  // sign of a known-nonzero difference is approximated. `certain` as sign().
  static int compare(const ExactNumber& a, const ExactNumber& b, bool* certain = nullptr);

  // x == q*pi for a rational q (true for 0 with q = 0).
  bool piMultiple(Rational& q) const;
  // The terms when every one is c, c*sqrt(n), c*pi or c*e (the forms a
  // ClassWiz shows, plus e); false otherwise. Sorted: rational, radicals by
  // radicand, then pi and e.
  struct SimpleTerm {
    Rational coefficient;
    BigInt radicand{1};
    bool pi = false, e = false;
  };
  bool simpleTerms(std::vector<SimpleTerm>& out) const;
  // x == pi*(sum of out) or e*(sum of out), every term c or c*sqrt(n) (its
  // pi/e flags unset), sorted by radicand; `pi` names the constant. Read off
  // the normal form like simpleTerms -- no arithmetic, so it is cheap enough
  // for the UI loop's small stack (a division there overflowed it).
  bool constantTerms(bool& pi, std::vector<SimpleTerm>& out) const;

 private:
  friend struct ExactAccess;
  ExactStatus status_ = ExactStatus::Inexact;
  Rational rational_;
  std::shared_ptr<const ExactRep> rep_;
};
}  // namespace Calculator
