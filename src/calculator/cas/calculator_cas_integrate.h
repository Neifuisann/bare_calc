#pragma once
#include "calculator/cas/calculator_cas_canonical.h"

// Symbolic indefinite integration (the constant of integration is omitted).
//
// Strategies, in order: constants and linearity; a table of elementary
// antiderivatives written over a linear inner argument X = a*x+b (so one rule
// covers sin(x), sin(3x) and sin(2x-1)); power reduction for sin^n, cos^n and
// tan^n; rational functions by partial fractions over exact linear and
// irreducible quadratic factors; substitution where the integrand is
// g(u(x))*u'(x); integration by parts ordered by LIATE; and expansion.
//
// Every result is differentiated and compared with the integrand. A result
// the zero test disproves is discarded, never returned; `verified` reports
// whether the check proved it. What cannot be integrated stays an
// unevaluated Integral(f, x) -- for a sum, only the terms that failed.
namespace Calculator {
namespace Cas {

Expr integrate(const Expr& integrand, const Expr& x, bool* verified = nullptr);

// The definite integral of f over [a, b] (exact Number bounds) as an exact
// Number, or null. |g| is resolved piecewise: [a, b] splits at the zeros of
// every g in (a, b) -- all of them for sin/cos of a linear argument, solve()'s
// otherwise -- and on each piece |g| is +-g by its sign at the midpoint. Each
// piece is F(right) - F(left) of its antiderivative, so a result is only as
// good as the antiderivative's continuity: the caller must confirm it against
// a numeric value before trusting it.
Expr definiteIntegral(const Expr& f, const Expr& x, const Expr& a, const Expr& b);

}  // namespace Cas
}  // namespace Calculator
