#pragma once
#include "calculator/cas/calculator_cas_expr.h"

// Canonical constructors: the CAS's automatic simplification. Every tree the
// CAS builds goes through these, so every tree is in one normal form:
//
//  - sums and products are flattened and sorted; like terms combine
//    (2x+3x = 5x) and so do like factors (x*x^2 = x^3, 2^x*2^y = 2^(x+y));
//  - identities vanish: x+0, x*1, x^1, x^0 = 1, 1^u = 1, 0*u = 0;
//  - a constant subtree with an exact value becomes one Number, through
//    ExactNumber (sqrt(8) = 2*sqrt(2), sin(pi/6) = 1/2, e^(ln u) = u);
//  - powers combine only where that is valid over the reals:
//    (u^a)^n = u^(a*n) for an integer n; sqrt(u^2) = |u|; (2x)^3 = 8x^3;
//  - odd/even symmetry pulls signs out of functions (sin(-u) = -sin(u)).
//
// Nothing here expands products of sums or cancels polynomial factors; that
// is calculator_cas_simplify.h. A certain math error (1/0, sqrt(-1), ln(0))
// sets Error::Math on the context and yields 0.
namespace Calculator {
namespace Cas {

Expr number(const ExactNumber& value);
Expr integer(int64_t value);
Expr fraction(int64_t numerator, int64_t denominator);
Expr symbol(const std::string& name);
Expr pi();
Expr euler();  // e
Expr text(const std::string& message);

Expr add(Args terms);
Expr add(const Expr& a, const Expr& b);
Expr mul(Args factors);
Expr mul(const Expr& a, const Expr& b);
// A product exactly as given: canonical except that a numeric coefficient is
// not distributed over a lone sum, so factor() can return 2(x+1).
Expr factored(Args factors);
Expr neg(const Expr& a);
Expr sub(const Expr& a, const Expr& b);
Expr div(const Expr& a, const Expr& b);
Expr pow(const Expr& base, const Expr& exponent);
Expr pow(const Expr& base, int64_t exponent);
Expr sqrt(const Expr& a);
Expr exp(const Expr& a);
Expr fn(Fn f, const Expr& argument);
Expr equation(const Expr& lhs, const Expr& rhs);
Expr list(Args items);
Expr integral(const Expr& integrand, const Expr& variable);
Expr solveOf(const Expr& equation, const Expr& variable);

// The same node kind over new children, through the canonical constructors.
Expr rebuild(const Expr& e, Args args);
// Leaves (Number, Symbol, Wild, Text) returned unchanged; otherwise each
// child mapped and the node rebuilt only if a child changed.
template <class F>
Expr mapArgs(const Expr& e, F f) {
  if (e->args.empty()) return e;
  Args out;
  out.reserve(e->args.size());
  bool changed = false;
  for (const auto& a : e->args) {
    out.push_back(f(a));
    changed = changed || out.back() != a;
  }
  return changed ? rebuild(e, std::move(out)) : e;
}

// ---- Parts -----------------------------------------------------------------
inline bool isNumber(const Expr& e) { return e->kind == Kind::Number; }
bool zero(const Expr& e);
bool one(const Expr& e);
bool integerValue(const Expr& e, int64_t& value);    // Number that is an int64 integer
bool rationalValue(const Expr& e, Rational& value);  // rational Number
bool isEuler(const Expr& e);
// Syntactically negative: a negative Number, or a product whose numeric
// coefficient is negative. Sums are never "negative" here.
bool negative(const Expr& e);
// term = coefficient * rest, with a Number coefficient (1 if none).
void splitTerm(const Expr& term, ExactNumber& coefficient, Expr& rest);
// factor = base ^ exponent (exponent 1 if the factor is not a Pow).
void splitPower(const Expr& factor, Expr& base, Expr& exponent);

}  // namespace Cas
}  // namespace Calculator
