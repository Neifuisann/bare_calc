#pragma once
#include "calculator/cas/calculator_cas_canonical.h"

// Symbolic solving of one equation in one variable over the reals.
//
// Polynomials: exact roots up to degree 2 with radicals, rational roots by
// deflation at any degree, biquadratics through x^2. Products split into
// their factors. Otherwise the variable is isolated through inverse
// functions (principal branches: sin(x) = 1/2 gives pi/6 and 5pi/6), or a
// kernel such as e^x is replaced by a new unknown that leaves a polynomial.
//
// Every candidate is substituted back: a root outside the domain or one the
// zero test disproves (an extraneous root of sqrt(x) = -1) is dropped. The
// result is a List of `x = root` equations, "No Solution", "Infinite
// Solution", or an unevaluated Solve for what could not be solved.
namespace Calculator {
namespace Cas {

Expr solve(const Expr& equation, const Expr& x);

// A system of equations in several unknowns (Symbols), by elimination: an
// equation linear in an unknown with a coefficient free of the unknowns is
// solved for it and substituted into the rest; otherwise an unknown is
// eliminated through the Sylvester resultant of two equations polynomial in
// it. The last unknown left is solved by solve()'s machinery and the values
// are substituted back. Elimination only ever adds candidates, so every
// candidate is checked against every original equation, as solve() does.
//
// One solution is a List of `x = value` equations; several are a List of
// such Lists (printed as tuples). Otherwise "No Solution", "Infinite
// Solution", or an unevaluated Solve of List(equations), List(unknowns).
constexpr size_t kMaxSystem = 4;  // equations, and unknowns
Expr solveSystem(const Args& equations, const Args& unknowns);

}  // namespace Cas
}  // namespace Calculator
