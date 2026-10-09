#pragma once
#include "calculator/cas/calculator_cas_canonical.h"

// Simplification beyond the canonical form: expansion, rational
// normalization, factoring, identity rewriting, and a zero test. `x` is the
// main variable (it decides what counts as a polynomial); expressions in
// other symbols are still handled, only less thoroughly.
namespace Calculator {
namespace Cas {

// Distributes products over sums and expands integer powers of sums, all the
// way down (function arguments included). With `exponentials`, e^(a+b) also
// becomes e^a*e^b (for zero testing; simplify keeps e^(2x+1) whole).
Expr expand(const Expr& e, bool exponentials = true);
// e = numerator / denominator, over the least common denominator.
void numerDenom(const Expr& e, Expr& numerator, Expr& denominator);
Expr together(const Expr& e);
// A rational function of x with its polynomial gcd cancelled and a monic
// denominator: (x^2-1)/(x-1) = x+1. Anything else comes back combined.
Expr cancel(const Expr& e, const Expr& x);
// Factors over the rationals: numeric and monomial content, square-free
// decomposition and rational roots in the main variable, and perfect-square,
// difference-of-squares and sum/difference-of-cubes forms in any variables.
// Irreducible parts stay as they are (x^2+1, x^2-2).
Expr factor(const Expr& e, const Expr& x);
// The smallest of the canonical form, the rule-rewritten form, the expanded
// form, the cancelled form and (small sums) the factored form.
Expr simplify(const Expr& e, const Expr& x);
// sin^2 u + cos^2 u = 1 and its relatives, on the terms of a sum.
Expr pythagorean(const Expr& e);
// Deep exact folding: every constant subtree with an exact value becomes a
// Number, even where the printer would rather keep it (ln(10)).
Expr fold(const Expr& e);

// Is e identically zero? Yes is proved symbolically (expansion, rational
// normalization, trigonometric/hyperbolic normal forms). No is proved by an
// exact nonzero value at a sample point. Unknown is neither.
enum class Zero : uint8_t { Yes, No, Unknown };
Zero zeroTest(const Expr& e);

}  // namespace Cas
}  // namespace Calculator
