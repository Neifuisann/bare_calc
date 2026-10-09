#pragma once
#include "calculator/cas/calculator_cas_canonical.h"
#include <vector>

// Univariate polynomials with exact coefficients, for the parts of the CAS
// that need polynomial arithmetic: cancelling common factors, factoring,
// partial fractions and solving. Coefficients are ExactNumbers, so sqrt(2)x+1
// is a polynomial too; an operation that would need a non-exact coefficient
// (division by 1+pi) fails rather than approximating.
namespace Calculator {
namespace Cas {

struct Poly {
  std::vector<ExactNumber, ExactAllocator<ExactNumber>> c;  // c[i]: coefficient of x^i, no trailing zeros
  int degree() const { return static_cast<int>(c.size()) - 1; }  // -1 for the zero polynomial
  bool zero() const { return c.empty(); }
  const ExactNumber& lead() const { return c.back(); }
};

// The coefficients of e as a polynomial in x (after expansion), each free of
// x but otherwise any expression: a*x^2+b*x+c gives {c, b, a}. False when e
// is not a polynomial in x or its degree passes maxDegree.
bool coefficients(const Expr& e, const Expr& x, std::vector<Expr>& out, unsigned maxDegree = 64);
// As coefficients(), when every coefficient is an exact number.
bool toPoly(const Expr& e, const Expr& x, Poly& p, unsigned maxDegree = 64);
Expr fromPoly(const Poly& p, const Expr& x);
bool rationalPoly(const Poly& p);

// Arithmetic. Anything that leaves exact arithmetic sets Stack on the
// context; division and gcd report failure instead (a non-rational leading
// coefficient may have no exact inverse).
Poly polyAdd(const Poly& a, const Poly& b);
Poly polySub(const Poly& a, const Poly& b);
Poly polyMul(const Poly& a, const Poly& b);
Poly polyScale(const Poly& a, const ExactNumber& k);
Poly polyDerivative(const Poly& a);
ExactNumber polyEval(const Poly& a, const ExactNumber& x);
bool polyDivMod(const Poly& a, const Poly& b, Poly& quotient, Poly& remainder);
bool polyGcd(const Poly& a, const Poly& b, Poly& gcd);  // monic; gcd(0, 0) = 0
bool monic(const Poly& a, Poly& out);
// Yun's square-free decomposition of a monic-normalized p:
// p = lead * parts[0] * parts[1]^2 * parts[2]^3 ..., each part monic and
// square-free (a part may be the constant 1).
bool squareFree(const Poly& p, std::vector<Poly>& parts);
// The distinct rational roots of a rational polynomial, by the rational root
// theorem. Divisors come from trial division, so a huge constant term may
// hide a root (never produce a wrong one).
bool rationalRoots(const Poly& p, std::vector<Rational>& roots);
// p / (x - root), exactly.
Poly deflate(const Poly& p, const ExactNumber& root);

}  // namespace Cas
}  // namespace Calculator
