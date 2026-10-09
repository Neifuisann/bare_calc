#include "calculator/cas/calculator_cas_partfrac.h"
#include "calculator/cas/calculator_cas_simplify.h"

namespace Calculator {
namespace Cas {
namespace {
struct PFactor {
  Poly p;  // monic, degree 1 or 2 (irreducible over the reals)
  int multiplicity;
};
Poly monomialPoly(const ExactNumber& c0, const ExactNumber& c1) {
  Poly p;
  p.c = {c0, c1};
  return p;
}
// Splits a monic square-free piece into real linear factors (exact radicals
// allowed) and irreducible quadratics. Degree 4 is handled when biquadratic.
bool splitMonic(const Poly& q, int m, std::vector<PFactor>& out) {
  if (q.degree() == 1) { out.push_back({q, m}); return true; }
  if (q.degree() == 2) {
    const ExactNumber& p = q.c[1];
    const ExactNumber& r = q.c[0];
    const ExactNumber disc = p * p - ExactNumber::integer(4) * r;
    bool certain = false;
    const int s = disc.sign(&certain);
    if (!disc.exact() || !certain) return false;
    if (s < 0) { out.push_back({q, m}); return true; }
    const ExactNumber root = disc.sqrt(), half = ExactNumber::fraction(1, 2);
    if (!root.exact()) return false;
    out.push_back({monomialPoly((p + root) * half, ExactNumber::integer(1)), m});  // x - (-p - root)/2
    out.push_back({monomialPoly((p - root) * half, ExactNumber::integer(1)), m});
    return true;
  }
  if (q.degree() == 4 && q.c[1].zero() && q.c[3].zero()) {
    const ExactNumber& b = q.c[2];
    const ExactNumber& c = q.c[0];
    const ExactNumber disc = b * b - ExactNumber::integer(4) * c;
    bool certain = false;
    const int s = disc.sign(&certain);
    if (!disc.exact() || !certain) return false;
    if (s >= 0) {
      // x^4 + b x^2 + c = (x^2 - r1)(x^2 - r2)
      const ExactNumber root = disc.sqrt(), half = ExactNumber::fraction(1, 2);
      for (int sign = -1; sign <= 1; sign += 2) {
        const ExactNumber r = (-b + ExactNumber::integer(sign) * root) * half;
        Poly quad;
        quad.c = {-r, ExactNumber::integer(0), ExactNumber::integer(1)};
        if (!splitMonic(quad, m, out)) return false;
      }
      return true;
    }
    // c > 0: x^4 + b x^2 + c = (x^2 + s)^2 - (2s - b) x^2 with s = sqrt(c).
    const ExactNumber sc = c.sqrt(), t2 = ExactNumber::integer(2) * sc - b;
    if (!sc.exact() || t2.sign(&certain) <= 0 || !certain) return false;
    const ExactNumber t = t2.sqrt();
    if (!t.exact()) return false;
    for (int sign = -1; sign <= 1; sign += 2) {
      Poly quad;
      quad.c = {sc, ExactNumber::integer(sign) * t, ExactNumber::integer(1)};
      if (!splitMonic(quad, m, out)) return false;
    }
    return true;
  }
  return false;
}
bool denominatorFactors(const Poly& d, std::vector<PFactor>& out) {
  std::vector<Poly> parts;
  if (!squareFree(d, parts)) return false;
  for (size_t k = 0; k < parts.size(); ++k) {
    Poly rest = parts[k];
    if (rest.degree() < 1) continue;
    const int m = static_cast<int>(k + 1);
    std::vector<Rational> roots;
    if (rationalPoly(rest) && rationalRoots(rest, roots)) {
      for (const auto& r : roots) {
        out.push_back({monomialPoly(ExactNumber::rational(-r), ExactNumber::integer(1)), m});
        rest = deflate(rest, ExactNumber::rational(r));
      }
    }
    if (rest.degree() >= 1 && !splitMonic(rest, m, out)) return false;
  }
  return context().ok();
}
Poly polyPower(const Poly& p, int k) {
  Poly r;
  r.c = {ExactNumber::integer(1)};
  for (int i = 0; i < k; ++i) r = polyMul(r, p);
  return r;
}
// Solves M a = rhs exactly (M square). False if singular or inexact.
bool solveLinear(std::vector<std::vector<ExactNumber>>& m, std::vector<ExactNumber>& rhs) {
  const size_t n = rhs.size();
  for (size_t col = 0; col < n; ++col) {
    size_t pivot = col;
    while (pivot < n && m[pivot][col].zero()) ++pivot;
    if (pivot == n) return false;
    std::swap(m[pivot], m[col]);
    std::swap(rhs[pivot], rhs[col]);
    const ExactNumber inverse = ExactNumber::integer(1) / m[col][col];
    if (!inverse.exact()) return false;
    for (size_t row = 0; row < n; ++row) {
      if (row == col || m[row][col].zero()) continue;
      if (!context().step()) return false;
      const ExactNumber k = m[row][col] * inverse;
      for (size_t j = col; j < n; ++j) m[row][j] = m[row][j] - k * m[col][j];
      rhs[row] = rhs[row] - k * rhs[col];
      if (!rhs[row].exact()) return false;
    }
    for (size_t j = col; j < n; ++j) m[col][j] = m[col][j] * inverse;
    rhs[col] = rhs[col] * inverse;
  }
  return true;
}
}  // namespace

bool partialFractions(const Expr& f, const Expr& x, Decomposition& out) {
  Expr n, d;
  numerDenom(f, n, d);
  Poly pn, pd;
  if (!toPoly(n, x, pn, 32) || !toPoly(d, x, pd, 32) || pd.degree() < 1) return false;
  Poly r;
  if (!polyDivMod(pn, pd, out.quotient, r)) return false;
  out.terms.clear();
  if (r.zero()) return context().ok();
  Poly D;
  if (!monic(pd, D)) return false;
  const Poly R = polyScale(r, ExactNumber::integer(1) / pd.lead());
  std::vector<PFactor> factors;
  if (!denominatorFactors(D, factors)) return false;
  // One unknown per numerator coefficient: A/f^j for linear f, (Bx+C)/f^j
  // for quadratic f. Column = the polynomial that unknown multiplies.
  struct Unknown { size_t factor; int power; bool xTerm; };
  std::vector<Unknown> unknowns;
  std::vector<Poly> columns;
  for (size_t i = 0; i < factors.size(); ++i)
    for (int j = 1; j <= factors[i].multiplicity; ++j) {
      Poly quotient, remainder;
      if (!polyDivMod(D, polyPower(factors[i].p, j), quotient, remainder) || !remainder.zero()) return false;
      if (factors[i].p.degree() == 2) {
        unknowns.push_back({i, j, true});
        columns.push_back(polyMul(quotient, monomialPoly(ExactNumber::integer(0), ExactNumber::integer(1))));
      }
      unknowns.push_back({i, j, false});
      columns.push_back(quotient);
    }
  const size_t size = static_cast<size_t>(D.degree());
  if (columns.size() != size) return false;
  std::vector<std::vector<ExactNumber>> m(size, std::vector<ExactNumber>(size, ExactNumber::integer(0)));
  std::vector<ExactNumber> rhs(size, ExactNumber::integer(0));
  for (size_t row = 0; row < size; ++row) {
    for (size_t col = 0; col < size; ++col)
      if (row < columns[col].c.size()) m[row][col] = columns[col].c[row];
    if (row < R.c.size()) rhs[row] = R.c[row];
  }
  if (!solveLinear(m, rhs)) return false;
  for (size_t u = 0; u < unknowns.size(); ++u) {
    const Unknown& k = unknowns[u];
    if (k.xTerm) continue;  // consumed with its constant partner below
    PartialFraction term{factors[k.factor].p, k.power, {}};
    if (factors[k.factor].p.degree() == 2) term.numerator.c = {rhs[u], rhs[u - 1]};
    else term.numerator.c = {rhs[u]};
    while (!term.numerator.c.empty() && term.numerator.c.back().zero()) term.numerator.c.pop_back();
    if (!term.numerator.zero()) out.terms.push_back(std::move(term));  // (x-1)/((x-1)(x+2)) has no 1/(x-1)
  }
  return context().ok();
}

namespace {
// A rational Bx+C numerator reads with its content outside, the rest primitive
// with a positive lead: -(x+1)/(2(x^2+1)), not (-x/2-1/2)/(x^2+1).
Expr fractionTerm(const PartialFraction& t, const Expr& x) {
  const Expr denominator = pow(fromPoly(t.factor, x), t.power);
  if (t.numerator.degree() < 1 || !rationalPoly(t.numerator)) return div(fromPoly(t.numerator, x), denominator);
  BigInt l(1), g(0);
  for (const auto& k : t.numerator.c) {
    const BigInt& d = k.rationalValue().den();
    l = l / BigInt::gcd(l, d) * d;
  }
  for (const auto& k : t.numerator.c) g = BigInt::gcd(g, k.rationalValue().num() * (l / k.rationalValue().den()));
  ExactNumber content = ExactNumber::rational(Rational::of(g, l));
  if (t.numerator.lead().sign() < 0) content = -content;
  const Poly primitive = polyScale(t.numerator, ExactNumber::integer(1) / content);
  return mul(Args{number(content), fromPoly(primitive, x), pow(denominator, -1)});
}
}  // namespace

Expr partfrac(const Expr& f, const Expr& x) {
  Decomposition d;
  if (!partialFractions(f, x, d)) return f;
  Args terms{fromPoly(d.quotient, x)};
  for (const auto& t : d.terms) terms.push_back(fractionTerm(t, x));
  return add(std::move(terms));
}

}  // namespace Cas
}  // namespace Calculator
