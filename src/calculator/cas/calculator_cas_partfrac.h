#pragma once
#include "calculator/cas/calculator_cas_poly.h"

// Partial fraction decomposition of a rational function of x with exact
// coefficients, shared by partfrac(f[,v]) and the integrator.
//
// f = P/Q is first made proper by long division (P = S*Q + R). The monic
// denominator splits by square-free decomposition into factors with their
// multiplicities; each part gives up its rational roots, then quadratics
// split over exact radicals (x^2-2) or stay irreducible over the reals
// (x^2+1), and biquadratics (x^4+1) split into two quadratics. The
// numerators -- A for a linear factor, Bx+C for a quadratic, one per power up
// to the multiplicity -- come from one exact linear system (the coefficients
// of R = sum N_ij * Q/f_i^j).
//
// Everything stays exact; a denominator that does not split exactly (an
// irreducible cubic, a symbolic coefficient) is a failure, never a numeric
// root.
namespace Calculator {
namespace Cas {

struct PartialFraction {
  Poly factor;     // monic, degree 1 or 2 (irreducible over the reals)
  int power;       // 1..multiplicity of the factor
  Poly numerator;  // degree below factor's; never zero
};
struct Decomposition {
  Poly quotient;  // the polynomial part S (zero for a proper fraction)
  std::vector<PartialFraction, ExactAllocator<PartialFraction>> terms;
};

// f = quotient + sum numerator / factor^power. False when f is not a rational
// function of x with exact coefficients (denominator of degree >= 1), or its
// denominator does not split exactly.
bool partialFractions(const Expr& f, const Expr& x, Decomposition& out);
// The decomposition as a sum, e.g. (x+3)/((x-1)(x+2)) = 4/(3(x-1))-1/(3(x+2)).
// What cannot be decomposed comes back as it is.
Expr partfrac(const Expr& f, const Expr& x);

}  // namespace Cas
}  // namespace Calculator
