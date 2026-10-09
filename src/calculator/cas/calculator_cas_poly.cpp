#include "calculator/cas/calculator_cas_poly.h"
#include "calculator/cas/calculator_cas_simplify.h"
#include <algorithm>

namespace Calculator {
namespace Cas {
namespace {
const ExactNumber& zeroNumber() {
  static const ExactNumber z = ExactNumber::integer(0);
  return z;
}
void trim(Poly& p) {
  while (!p.c.empty() && p.c.back().zero()) p.c.pop_back();
}
// An exact operation that left exact arithmetic (size bounds).
ExactNumber checked(const ExactNumber& v) {
  if (!v.exact()) context().fail(v.failed() ? Error::Math : Error::Stack);
  return v.exact() ? v : zeroNumber();
}
const ExactNumber& at(const Poly& p, size_t i) { return i < p.c.size() ? p.c[i] : zeroNumber(); }

// Divisors of |n| from trial division to 10^5; a cofactor left over is taken
// as prime (so a composite one hides divisors, never adds a wrong one).
bool divisors(const BigInt& n, std::vector<BigInt>& out) {
  out.clear();
  BigInt m = n.abs();
  if (m.zero()) return false;
  std::vector<std::pair<BigInt, unsigned>> primes;
  for (uint32_t p = 2; p <= 100000; ++p) {
    if (BigInt(static_cast<int64_t>(p)) * BigInt(static_cast<int64_t>(p)) > m) break;
    if (m.modSmall(p)) continue;
    unsigned k = 0;
    while (!m.modSmall(p)) { m = m / BigInt(static_cast<int64_t>(p)); ++k; }
    primes.push_back({BigInt(static_cast<int64_t>(p)), k});
    if ((p & 1023u) == 0 && !context().step()) return false;
  }
  if (m > BigInt(1)) primes.push_back({m, 1});
  out.push_back(BigInt(1));
  for (const auto& pk : primes) {
    const size_t count = out.size();
    BigInt power(1);
    for (unsigned k = 0; k < pk.second; ++k) {
      power = power * pk.first;
      for (size_t i = 0; i < count; ++i) out.push_back(out[i] * power);
      if (out.size() > 4096) return false;
    }
  }
  return true;
}
}  // namespace

bool coefficients(const Expr& e, const Expr& x, std::vector<Expr>& out, unsigned maxDegree) {
  out.clear();
  const Expr expanded = expand(e);
  std::vector<Args> buckets;
  auto term = [&](const Expr& t) {
    unsigned k = 0;
    Args rest;
    auto factor = [&](const Expr& f) {
      if (equal(f, x)) { ++k; return true; }
      int64_t n = 0;
      if (f->kind == Kind::Pow && equal(f->args[0], x)) {
        if (!integerValue(f->args[1], n) || n < 0 || n > static_cast<int64_t>(maxDegree)) return false;
        k += static_cast<unsigned>(n);
        return true;
      }
      if (!freeOf(f, x)) return false;
      rest.push_back(f);
      return true;
    };
    if (t->kind == Kind::Mul) {
      for (const auto& f : t->args)
        if (!factor(f)) return false;
    } else if (!factor(t)) {
      return false;
    }
    if (k > maxDegree) return false;
    if (buckets.size() <= k) buckets.resize(k + 1);
    buckets[k].push_back(mul(std::move(rest)));
    return true;
  };
  if (expanded->kind == Kind::Add) {
    for (const auto& t : expanded->args)
      if (!term(t)) return false;
  } else if (!term(expanded)) {
    return false;
  }
  for (auto& b : buckets) out.push_back(add(std::move(b)));
  while (!out.empty() && zero(out.back())) out.pop_back();
  return context().ok();
}

bool toPoly(const Expr& e, const Expr& x, Poly& p, unsigned maxDegree) {
  std::vector<Expr> c;
  if (!coefficients(e, x, c, maxDegree)) return false;
  p.c.clear();
  for (const auto& k : c) {
    if (!isNumber(k)) return false;
    p.c.push_back(k->number);
  }
  return true;
}

Expr fromPoly(const Poly& p, const Expr& x) {
  Args terms;
  for (size_t i = 0; i < p.c.size(); ++i)
    if (!p.c[i].zero()) terms.push_back(mul(number(p.c[i]), pow(x, static_cast<int64_t>(i))));
  return add(std::move(terms));
}

bool rationalPoly(const Poly& p) {
  for (const auto& k : p.c)
    if (!k.isRational()) return false;
  return true;
}

Poly polyAdd(const Poly& a, const Poly& b) {
  Poly r;
  const size_t n = std::max(a.c.size(), b.c.size());
  for (size_t i = 0; i < n; ++i) r.c.push_back(checked(at(a, i) + at(b, i)));
  trim(r);
  return r;
}
Poly polySub(const Poly& a, const Poly& b) {
  Poly r;
  const size_t n = std::max(a.c.size(), b.c.size());
  for (size_t i = 0; i < n; ++i) r.c.push_back(checked(at(a, i) - at(b, i)));
  trim(r);
  return r;
}
Poly polyMul(const Poly& a, const Poly& b) {
  Poly r;
  if (a.zero() || b.zero()) return r;
  r.c.assign(a.c.size() + b.c.size() - 1, zeroNumber());
  for (size_t i = 0; i < a.c.size(); ++i)
    for (size_t j = 0; j < b.c.size(); ++j) r.c[i + j] = checked(r.c[i + j] + a.c[i] * b.c[j]);
  trim(r);
  return r;
}
Poly polyScale(const Poly& a, const ExactNumber& k) {
  Poly r;
  for (const auto& v : a.c) r.c.push_back(checked(v * k));
  trim(r);
  return r;
}
Poly polyDerivative(const Poly& a) {
  Poly r;
  for (size_t i = 1; i < a.c.size(); ++i) r.c.push_back(checked(a.c[i] * ExactNumber::integer(static_cast<int64_t>(i))));
  trim(r);
  return r;
}
ExactNumber polyEval(const Poly& a, const ExactNumber& x) {
  ExactNumber acc = ExactNumber::integer(0);
  for (size_t i = a.c.size(); i-- > 0;) acc = checked(acc * x + a.c[i]);
  return acc;
}

bool polyDivMod(const Poly& a, const Poly& b, Poly& q, Poly& r) {
  if (b.zero()) return false;
  const ExactNumber inverse = ExactNumber::integer(1) / b.lead();
  if (!inverse.exact()) return false;
  r = a;
  q.c.assign(a.degree() >= b.degree() ? a.c.size() - b.c.size() + 1 : 0, zeroNumber());
  while (!r.zero() && r.degree() >= b.degree()) {
    if (!context().step()) return false;
    const size_t shift = static_cast<size_t>(r.degree() - b.degree());
    const ExactNumber k = checked(r.lead() * inverse);
    q.c[shift] = k;
    for (size_t i = 0; i < b.c.size(); ++i) r.c[i + shift] = checked(r.c[i + shift] - k * b.c[i]);
    r.c.back() = zeroNumber();  // exactly cancelled, whatever rounding the form might suggest
    trim(r);
  }
  trim(q);
  return context().ok();
}

bool monic(const Poly& a, Poly& out) {
  if (a.zero()) { out = a; return true; }
  const ExactNumber inverse = ExactNumber::integer(1) / a.lead();
  if (!inverse.exact()) return false;
  out = polyScale(a, inverse);
  return context().ok();
}

bool polyGcd(const Poly& a, const Poly& b, Poly& g) {
  Poly x = a, y = b;
  while (!y.zero()) {
    if (!context().step()) return false;
    Poly q, r;
    if (!polyDivMod(x, y, q, r)) return false;
    x = std::move(y);
    if (!monic(r, y)) return false;
  }
  return monic(x, g);
}

bool squareFree(const Poly& p, std::vector<Poly>& parts) {
  parts.clear();
  Poly f;
  if (!monic(p, f)) return false;
  if (f.degree() <= 0) return true;
  Poly g, b, c, d, q, r;
  const Poly df = polyDerivative(f);
  if (!polyGcd(f, df, g)) return false;
  if (!polyDivMod(f, g, b, r) || !polyDivMod(df, g, c, r)) return false;
  d = polySub(c, polyDerivative(b));
  while (b.degree() > 0) {
    if (!context().step()) return false;
    Poly a;
    if (!polyGcd(b, d, a)) return false;
    parts.push_back(a);
    if (!polyDivMod(b, a, q, r)) return false;
    b = q;
    if (!polyDivMod(d, a, c, r)) return false;
    d = polySub(c, polyDerivative(b));
  }
  return context().ok();
}

bool rationalRoots(const Poly& p, std::vector<Rational>& roots) {
  roots.clear();
  if (!rationalPoly(p) || p.degree() < 1) return false;
  // Integer coefficients: multiply through by the common denominator.
  BigInt l(1);
  for (const auto& k : p.c) {
    const BigInt& d = k.rationalValue().den();
    l = l / BigInt::gcd(l, d) * d;
  }
  std::vector<BigInt> a;
  for (const auto& k : p.c) a.push_back(k.rationalValue().num() * (l / k.rationalValue().den()));
  size_t low = 0;
  while (low < a.size() && a[low].zero()) ++low;
  if (low) roots.push_back(Rational(0));
  a.erase(a.begin(), a.begin() + static_cast<long>(low));
  if (a.size() < 2) return true;
  std::vector<BigInt> ps, qs;
  if (!divisors(a.front(), ps) || !divisors(a.back(), qs)) return !roots.empty();
  size_t tried = 0;
  const size_t n = a.size() - 1;
  for (const auto& q : qs) {
    for (const auto& pp : ps) {
      if (BigInt::gcd(pp, q) != BigInt(1)) continue;
      for (int s = -1; s <= 1; s += 2) {
        if (++tried > 20000 || !context().step()) return context().ok();
        const BigInt num = s < 0 ? -pp : pp;
        // q^n * f(num/q) by Horner: exact, integer-only.
        BigInt acc = a[n], qpow(1);
        for (size_t i = n; i-- > 0;) {
          qpow = qpow * q;
          acc = acc * num + a[i] * qpow;
        }
        if (acc.zero()) roots.push_back(Rational::of(num, q));
      }
    }
  }
  return context().ok();
}

Poly deflate(const Poly& p, const ExactNumber& root) {
  // Synthetic division by (x - root).
  Poly q;
  if (p.degree() < 1) return q;
  q.c.assign(p.c.size() - 1, zeroNumber());
  ExactNumber carry = ExactNumber::integer(0);
  for (size_t i = p.c.size(); i-- > 1;) {
    carry = checked(carry * root + p.c[i]);
    q.c[i - 1] = carry;
  }
  trim(q);
  return q;
}

}  // namespace Cas
}  // namespace Calculator
