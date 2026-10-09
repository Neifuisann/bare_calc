#include "calculator/math/calculator_exact.h"
#include <algorithm>
#include <cctype>

namespace Calculator {
// ---- Representation ---------------------------------------------------------
// Atoms are positive reals, so a single term's sign is its coefficient's:
// pi, e^q, ln(n) with n >= 2, ln(A) with A > 1, and root(k, A) with A > 0.
enum class AtomKind : uint8_t { Pi, E, Log, LogOf, RootOf };
struct Atom {
  AtomKind kind = AtomKind::Pi;
  uint32_t index = 0;  // RootOf: the root's index
  BigInt n;            // Log: ln(n), n a trial-prime or an unfactored cofactor
  ExactNumber arg;     // LogOf: ln(arg), arg > 1; RootOf: root(index, arg), arg primitive > 0
};
// Exponents: rational on pi and e; nonzero integers on Log/LogOf; on RootOf an
// integer in [1, index), or in (-index, 0) when arg has no exact inverse.
struct Factor {
  Atom atom;
  Rational exponent;
};
using Factors = std::vector<Factor, ExactAllocator<Factor>>;
// root(index, radicand): radicand free of index-th powers of trial primes and
// not a perfect d-th power for any d | index. index == 1 <=> radicand == 1.
struct Surd {
  uint32_t index = 1;
  BigInt radicand{1};
};
struct Monomial {
  Surd surd;
  Factors factors;  // sorted by atom, one entry per atom
};
struct Term {
  Rational coefficient;
  Monomial monomial;
};
using Terms = std::vector<Term, ExactAllocator<Term>>;
// Sorted by monomial, one term per monomial, no zero coefficients, and never
// a lone rational (that is stored inline).
struct ExactRep {
  explicit ExactRep(Terms&& t) : terms(std::move(t)) {}
  Terms terms;
};

struct ExactAccess {
  static ExactNumber status(ExactStatus s) {
    ExactNumber x;
    x.status_ = s;
    return x;
  }
  static ExactNumber rational(const Rational& r) {
    if (r.bits() > ExactNumber::kMaxBits) return status(ExactStatus::Inexact);
    ExactNumber x;
    x.status_ = ExactStatus::Exact;
    x.rational_ = r;
    return x;
  }
  static ExactNumber rep(Terms&& terms) {
    ExactNumber x;
    x.status_ = ExactStatus::Exact;
    x.rep_ = std::allocate_shared<ExactRep>(ExactAllocator<ExactRep>(), std::move(terms));
    return x;
  }
  static const ExactRep* get(const ExactNumber& x) { return x.rep_.get(); }
};

namespace {
using S = ExactStatus;
constexpr uint32_t kTrialLimit = 1000;  // radicand/log factoring by trial division
constexpr size_t kMaxApproxBits = 20000;

ExactNumber inexact() { return ExactAccess::status(S::Inexact); }
ExactNumber fail(S s) { return ExactAccess::status(s); }
ExactNumber number(const Rational& r) { return ExactAccess::rational(r); }
bool trivial(const Monomial& m) { return m.surd.index == 1 && m.factors.empty(); }

// The first failure wins; Inexact only when nothing failed for certain.
bool propagate(const ExactNumber& a, const ExactNumber& b, ExactNumber& out) {
  if (a.failed()) { out = a; return true; }
  if (b.failed()) { out = b; return true; }
  if (!a.exact() || !b.exact()) { out = inexact(); return true; }
  return false;
}

// Calls f(p) for 2, 3 and every 6k+-1 below the trial limit. A composite
// candidate never divides once its prime factors are gone, so no sieve.
template <typename F> void forTrialDivisors(F&& f) {
  if (!f(2u) || !f(3u)) return;
  for (uint32_t p = 5; p < kTrialLimit; p += 6)
    if (!f(p) || !f(p + 2)) return;
}
uint32_t gcd32(uint32_t a, uint32_t b) {
  while (b) { const uint32_t t = a % b; a = b; b = t; }
  return a;
}
int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// ---- Structural order -------------------------------------------------------
int compareStructural(const ExactNumber& a, const ExactNumber& b);
int compareAtom(const Atom& a, const Atom& b) {
  if (a.kind != b.kind) return a.kind < b.kind ? -1 : 1;
  if (a.index != b.index) return a.index < b.index ? -1 : 1;
  if (const int c = BigInt::compare(a.n, b.n)) return c;
  return compareStructural(a.arg, b.arg);
}
int compareFactors(const Factors& a, const Factors& b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  for (size_t i = 0; i < a.size(); ++i) {
    if (const int c = compareAtom(a[i].atom, b[i].atom)) return c;
    if (const int c = Rational::compare(a[i].exponent, b[i].exponent)) return c;
  }
  return 0;
}
// Rationals first, then radicals, then transcendental monomials: the natural
// reading order "1+sqrt(2)+pi".
int compareMonomial(const Monomial& a, const Monomial& b) {
  if (const int c = compareFactors(a.factors, b.factors)) return c;
  if (a.surd.index != b.surd.index) return a.surd.index < b.surd.index ? -1 : 1;
  return BigInt::compare(a.surd.radicand, b.surd.radicand);
}
int compareStructural(const ExactNumber& a, const ExactNumber& b) {
  if (a.status() != b.status()) return a.status() < b.status() ? -1 : 1;
  if (!a.exact()) return 0;
  const ExactRep* x = ExactAccess::get(a);
  const ExactRep* y = ExactAccess::get(b);
  if (!x || !y) {
    if (x || y) return x ? 1 : -1;
    return Rational::compare(a.rationalValue(), b.rationalValue());
  }
  if (x->terms.size() != y->terms.size()) return x->terms.size() < y->terms.size() ? -1 : 1;
  for (size_t i = 0; i < x->terms.size(); ++i) {
    if (const int c = compareMonomial(x->terms[i].monomial, y->terms[i].monomial)) return c;
    if (const int c = Rational::compare(x->terms[i].coefficient, y->terms[i].coefficient)) return c;
  }
  return 0;
}

// ---- Terms <-> numbers ------------------------------------------------------
Terms termsOf(const ExactNumber& x) {
  if (const ExactRep* r = ExactAccess::get(x)) return r->terms;
  Terms t;
  if (!x.rationalValue().zero()) t.push_back(Term{x.rationalValue(), Monomial{}});
  return t;
}
bool withinBounds(const Term& t) {
  const size_t limit = ExactNumber::kMaxBits;
  if (t.coefficient.bits() > limit || t.monomial.surd.radicand.bits() > limit) return false;
  for (const auto& f : t.monomial.factors)
    if (f.atom.n.bits() > limit || f.exponent.bits() > 64) return false;
  return true;
}
// root(k, n) == c * root(k, m) for a rational c exactly when n*m^(k-1) is a
// perfect k-th power. Canonical radicands are equal whenever this holds,
// except for an unextracted power of a prime above the trial limit; merging
// catches that case, so zero detection never depends on complete factoring.
bool commensurable(const Surd& a, const Surd& b, Rational& ratio) {
  if (a.index != b.index || a.index == 1) return false;
  const uint32_t k = a.index;
  const BigInt& n = a.radicand;
  const BigInt& m = b.radicand;
  // Only a leftover factor p^k with p above the trial limit can hide here.
  if (std::max(n.bits(), m.bits()) <= 10 * static_cast<size_t>(k)) return false;
  if (n.bits() + (k - 1) * m.bits() > ExactNumber::kMaxBits) return false;
  BigInt r;
  if (!(m * n.pow(k - 1)).exactRoot(k, r)) return false;
  ratio = Rational::of(r, n);  // root(k, m) = r/n * root(k, n)
  return true;
}
ExactNumber fromTerm(Term&& t);
ExactNumber atomPower(Atom atom, const Rational& exponent);
bool hasNestedRoot(const ExactNumber& x) {
  for (const auto& t : termsOf(x))
    for (const auto& f : t.monomial.factors)
      if (f.atom.kind == AtomKind::RootOf) return true;
  return false;
}
// Distinct radicals of integers are linearly independent, nested ones need
// not be: sqrt(5-sqrt(5)) = (sqrt(10)+sqrt(2))/2 * sqrt(5-2sqrt(5)). Two
// square-root atoms root(2, A) and root(2, B) in one sum are commensurable
// when sqrt(A*B) needs no new atom; then root(2, B) = sqrt(A*B)/A * root(2, A)
// and the sum is rewritten onto the structurally smaller atom, so one value
// keeps one representation and a zero sum is structurally zero. Each rewrite
// removes an atom, so the recursion through + and * ends.
bool mergeNestedRoots(const Terms& terms, ExactNumber& out) {
  std::vector<Atom> roots;
  for (const auto& t : terms)
    for (const auto& f : t.monomial.factors)
      if (f.atom.kind == AtomKind::RootOf && f.atom.index == 2 && f.exponent.isOne() &&
          std::none_of(roots.begin(), roots.end(), [&](const Atom& a) { return compareAtom(a, f.atom) == 0; }))
        roots.push_back(f.atom);
  if (roots.size() < 2) return false;
  std::sort(roots.begin(), roots.end(), [](const Atom& a, const Atom& b) { return compareAtom(a, b) < 0; });
  for (size_t i = 0; i < roots.size(); ++i)
    for (size_t j = i + 1; j < roots.size(); ++j) {
      const ExactNumber s = (roots[i].arg * roots[j].arg).pow(number(Rational::of(BigInt(1), BigInt(2))));
      if (!s.exact() || hasNestedRoot(s)) continue;
      const ExactNumber ratio = s / roots[i].arg;
      if (!ratio.exact()) continue;
      ExactNumber sum = number(Rational(0));
      for (const auto& t : terms) {
        Term rest = t;
        auto& f = rest.monomial.factors;
        const auto drop = std::find_if(f.begin(), f.end(), [&](const Factor& x) { return compareAtom(x.atom, roots[j]) == 0; });
        const bool had = drop != f.end();
        if (had) f.erase(drop);
        ExactNumber v = fromTerm(std::move(rest));
        if (had) v = v * ratio * atomPower(roots[i], Rational(1));
        sum = sum + v;
        if (!sum.exact()) return false;
      }
      out = sum;
      return true;
    }
  return false;
}
ExactNumber normalize(Terms&& terms) {
  terms.erase(std::remove_if(terms.begin(), terms.end(), [](const Term& t) { return t.coefficient.zero(); }), terms.end());
  std::sort(terms.begin(), terms.end(),
            [](const Term& a, const Term& b) { return compareMonomial(a.monomial, b.monomial) < 0; });
  Terms merged;
  for (auto& t : terms) {
    if (!merged.empty() && compareMonomial(merged.back().monomial, t.monomial) == 0)
      merged.back().coefficient = merged.back().coefficient + t.coefficient;
    else merged.push_back(std::move(t));
  }
  for (size_t i = 0; i < merged.size(); ++i)
    for (size_t j = i + 1; j < merged.size(); ++j) {
      Rational ratio;
      if (merged[i].coefficient.zero() || merged[j].coefficient.zero()) continue;
      if (compareFactors(merged[i].monomial.factors, merged[j].monomial.factors) != 0) continue;
      if (!commensurable(merged[i].monomial.surd, merged[j].monomial.surd, ratio)) continue;
      // Keep the smaller radicand; the ratio is relative to term i's surd.
      if (merged[j].monomial.surd.radicand < merged[i].monomial.surd.radicand) {
        merged[j].coefficient = merged[j].coefficient + merged[i].coefficient / ratio;
        merged[i].coefficient = Rational(0);
      } else {
        merged[i].coefficient = merged[i].coefficient + merged[j].coefficient * ratio;
        merged[j].coefficient = Rational(0);
      }
    }
  merged.erase(std::remove_if(merged.begin(), merged.end(), [](const Term& t) { return t.coefficient.zero(); }), merged.end());
  if (merged.size() > ExactNumber::kMaxTerms) return inexact();
  for (const auto& t : merged)
    if (!withinBounds(t)) return inexact();
  ExactNumber rewritten;
  if (mergeNestedRoots(merged, rewritten)) return rewritten;
  if (merged.empty()) return number(Rational(0));
  if (merged.size() == 1 && trivial(merged[0].monomial)) return number(merged[0].coefficient);
  return ExactAccess::rep(std::move(merged));
}
ExactNumber fromTerm(Term&& t) {
  Terms terms;
  terms.push_back(std::move(t));
  return normalize(std::move(terms));
}

// ---- Radicals ---------------------------------------------------------------
struct Radical {
  Rational outside{1};
  Surd surd;
};
// root(k, n) for n > 0 as outside * canonical surd.
Radical canonicalRadical(uint32_t k, BigInt n) {
  Radical r;
  if (k == 1 || n == BigInt(1)) { r.outside = Rational(k == 1 ? n : BigInt(1)); return r; }
  BigInt out(1), in(1);
  forTrialDivisors([&](uint32_t p) {
    if (n == BigInt(1)) return false;
    uint32_t v = 0;
    while (n.modSmall(p) == 0) { n = n / BigInt(p); ++v; }
    if (v) {
      out = out * BigInt(p).pow(v / k);
      in = in * BigInt(p).pow(v % k);
    }
    return true;
  });
  BigInt root;
  if (n != BigInt(1) && n.exactRoot(k, root)) { out = out * root; n = BigInt(1); }
  BigInt m = in * n;
  for (bool changed = true; changed && k > 1;) {
    changed = false;
    for (uint32_t d = 2; d <= k; ++d)
      if (k % d == 0 && m.exactRoot(d, root)) { m = root; k /= d; changed = true; break; }
  }
  if (m == BigInt(1)) k = 1;
  r.outside = Rational(out);
  r.surd = Surd{k, m};
  return r;
}
ExactNumber rootOfRational(const Rational& r, uint32_t k) {
  if (r.zero()) return number(Rational(0));
  if (k == 1) return number(r);
  if (k > ExactNumber::kMaxIndex) return inexact();
  if (r.sign() < 0) return k % 2 ? -rootOfRational(-r, k) : fail(S::NotReal);
  // (a/b)^(1/k) = root(k, a*b^(k-1)) / b
  if (r.num().bits() + (k - 1) * r.den().bits() > ExactNumber::kMaxBits) return inexact();
  const Radical rad = canonicalRadical(k, r.num() * r.den().pow(k - 1));
  return fromTerm(Term{rad.outside / Rational(r.den()), Monomial{rad.surd, {}}});
}

// ---- Products ---------------------------------------------------------------
Factors mergeFactors(const Factors& a, const Factors& b) {
  Factors r;
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    const int c = i == a.size() ? 1 : j == b.size() ? -1 : compareAtom(a[i].atom, b[j].atom);
    if (c < 0) r.push_back(a[i++]);
    else if (c > 0) r.push_back(b[j++]);
    else {
      Factor f = a[i++];
      f.exponent = f.exponent + b[j++].exponent;
      if (!f.exponent.zero()) r.push_back(std::move(f));
    }
  }
  return r;
}
void sortFactors(Factors& f) {
  std::sort(f.begin(), f.end(), [](const Factor& a, const Factor& b) { return compareAtom(a.atom, b.atom) < 0; });
  Factors merged;
  for (auto& x : f) {
    if (!merged.empty() && compareAtom(merged.back().atom, x.atom) == 0)
      merged.back().exponent = merged.back().exponent + x.exponent;
    else merged.push_back(std::move(x));
  }
  merged.erase(std::remove_if(merged.begin(), merged.end(), [](const Factor& x) { return x.exponent.zero(); }), merged.end());
  f.swap(merged);
}
// A term whose root(k, A)^e factors may sit outside [1, k): reduce each to
// its canonical index/exponent, multiplying out whole powers of A.
ExactNumber finishTerm(Rational coefficient, Surd surd, Factors factors) {
  ExactNumber pending = number(Rational(1));
  for (int pass = 0; pass < 4; ++pass) {
    bool changed = false;
    Factors next;
    for (auto& f : factors) {
      if (f.atom.kind != AtomKind::RootOf) { next.push_back(std::move(f)); continue; }
      int64_t e = f.exponent.num().toInt64();
      int64_t k = f.atom.index;
      const int64_t g = static_cast<int64_t>(gcd32(static_cast<uint32_t>(e < 0 ? -e : e), static_cast<uint32_t>(k)));
      if (g > 1) { e /= g; k /= g; changed = true; }
      const int64_t q = floorDiv(e, k);
      if (k == 1 || q != 0) {
        const ExactNumber whole = f.atom.arg.pow(number(Rational(k == 1 ? e : q)));
        if (whole.exact()) {
          pending = pending * whole;
          if (k == 1) continue;
          e -= q * k;
        } else if (e > 0 || e <= -k) {
          return inexact();
        }
      }
      if (!e) continue;
      f.atom.index = static_cast<uint32_t>(k);
      f.exponent = Rational(e);
      next.push_back(std::move(f));
    }
    factors.swap(next);
    if (!changed) break;
    sortFactors(factors);
  }
  if (!pending.exact()) return pending;
  const ExactNumber term = fromTerm(Term{std::move(coefficient), Monomial{std::move(surd), std::move(factors)}});
  return pending.isRational() && pending.rationalValue().isOne() ? term : term * pending;
}
// atom^exponent as a number; a nested radical goes through finishTerm so its
// exponent lands in canonical range.
ExactNumber atomPower(Atom atom, const Rational& exponent) {
  if (exponent.zero()) return number(Rational(1));
  Factors f;
  f.push_back(Factor{std::move(atom), exponent});
  if (f[0].atom.kind == AtomKind::RootOf) return finishTerm(Rational(1), Surd{}, std::move(f));
  return fromTerm(Term{Rational(1), Monomial{Surd{}, std::move(f)}});
}
bool productTerms(const Term& a, const Term& b, Terms& out) {
  Rational c = a.coefficient * b.coefficient;
  Surd s;
  const Surd &x = a.monomial.surd, &y = b.monomial.surd;
  if (x.index == 1) s = y;
  else if (y.index == 1) s = x;
  else {
    const uint32_t l = x.index / gcd32(x.index, y.index) * y.index;
    if (l > ExactNumber::kMaxIndex ||
        x.radicand.bits() * (l / x.index) + y.radicand.bits() * (l / y.index) > ExactNumber::kMaxBits)
      return false;
    const Radical r = canonicalRadical(l, x.radicand.pow(l / x.index) * y.radicand.pow(l / y.index));
    c = c * r.outside;
    s = r.surd;
  }
  const ExactNumber p = finishTerm(c, s, mergeFactors(a.monomial.factors, b.monomial.factors));
  if (!p.exact()) return false;
  const Terms t = termsOf(p);
  out.insert(out.end(), t.begin(), t.end());
  return true;
}

// ---- Content, sign helpers ----------------------------------------------------
// x = content * primitive, content > 0, primitive's coefficients coprime integers.
Rational content(const Terms& terms) {
  BigInt g(0), l(1);
  for (const auto& t : terms) {
    g = BigInt::gcd(g, t.coefficient.num());
    l = l / BigInt::gcd(l, t.coefficient.den()) * t.coefficient.den();
  }
  return Rational::of(g, l);
}
ExactNumber scaled(const Terms& terms, const Rational& factor) {
  Terms t = terms;
  for (auto& x : t) x.coefficient = x.coefficient * factor;
  return normalize(std::move(t));
}

// ---- Inverses -----------------------------------------------------------------
// v_p(n) is odd, for p > 1.
bool oddValuation(BigInt n, const BigInt& p) {
  bool odd = false;
  for (;;) {
    BigInt q, r;
    BigInt::divMod(n, p, q, r);
    if (!r.zero()) return odd;
    n = q;
    odd = !odd;
  }
}
// 1/D for a sum of rationals and square roots: multiply by the conjugate
// under each automorphism sqrt(p) -> -sqrt(p) of a coprime basis of the
// radicands until the denominator is rational. Any unexpected state (a
// conjugate that is zero, a denominator that stays irrational) is Inexact,
// never a guessed answer.
ExactNumber inverseSquareRoots(const ExactNumber& d) {
  std::vector<BigInt> basis;
  for (const auto& t : termsOf(d))
    if (t.monomial.surd.index == 2) basis.push_back(t.monomial.surd.radicand);
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < basis.size() && !changed; ++i)
      for (size_t j = i + 1; j < basis.size() && !changed; ++j) {
        const BigInt g = BigInt::gcd(basis[i], basis[j]);
        if (g == BigInt(1)) continue;
        const BigInt a = basis[i] / g, b = basis[j] / g;
        basis.erase(basis.begin() + static_cast<long>(j));
        basis.erase(basis.begin() + static_cast<long>(i));
        for (const BigInt& x : {a, b, g})
          if (x != BigInt(1) && std::find(basis.begin(), basis.end(), x) == basis.end()) basis.push_back(x);
        changed = true;
      }
    if (basis.size() > 12) return inexact();
  }
  ExactNumber num = number(Rational(1)), den = d;
  for (const BigInt& p : basis) {
    BigInt root;
    if (p.exactRoot(2, root)) continue;  // a square contributes no sign
    Terms flipped = termsOf(den);
    bool any = false;
    for (auto& t : flipped)
      if (t.monomial.surd.index == 2 && oddValuation(t.monomial.surd.radicand, p)) {
        t.coefficient = -t.coefficient;
        any = true;
      }
    if (!any) continue;
    const ExactNumber conjugate = normalize(std::move(flipped));
    if (!conjugate.exact() || conjugate.zero()) return inexact();
    num = num * conjugate;
    den = den * conjugate;
    if (!num.exact() || !den.exact()) return inexact();
  }
  if (!den.isRational()) return inexact();
  if (den.zero()) return fail(S::DivisionByZero);
  return num * number(den.rationalValue().inverse());
}
// 1/(u+v) = sum_{i<L} u^(L-1-i) (-v)^i / (u^L - (-v)^L), where u^L and v^L
// are rational (L = lcm of the two radical indices).
ExactNumber inverseTwoRadicals(const Terms& t) {
  const uint32_t a = t[0].monomial.surd.index, b = t[1].monomial.surd.index;
  const uint32_t l = a / gcd32(a, b) * b;
  if (l > 12) return inexact();
  const ExactNumber u = fromTerm(Term(t[0])), v = -fromTerm(Term(t[1]));
  const ExactNumber bottom = u.pow(number(Rational(static_cast<int64_t>(l)))) -
                             v.pow(number(Rational(static_cast<int64_t>(l))));
  if (!bottom.isRational()) return inexact();
  if (bottom.zero()) return fail(S::DivisionByZero);
  ExactNumber top = number(Rational(0)), up = number(Rational(1));
  std::vector<ExactNumber> vPowers{number(Rational(1))};
  for (uint32_t i = 1; i < l; ++i) vPowers.push_back(vPowers.back() * v);
  for (uint32_t i = 0; i < l; ++i) {
    top = top + up * vPowers[l - 1 - i];
    up = up * u;
  }
  return top * number(bottom.rationalValue().inverse());
}
ExactNumber inverseMonomial(const Term& t) {
  ExactNumber r = number(t.coefficient.inverse());
  const Surd& s = t.monomial.surd;
  if (s.index > 1) r = r * rootOfRational(Rational::of(BigInt(1), s.radicand), s.index);
  for (const auto& f : t.monomial.factors) r = r * atomPower(f.atom, -f.exponent);
  return r;
}
}  // namespace

// ---- Construction -------------------------------------------------------------
ExactNumber ExactNumber::integer(const BigInt& value) { return number(Rational(value)); }
ExactNumber ExactNumber::rational(const Rational& value) { return number(value); }
ExactNumber ExactNumber::fraction(int64_t n, int64_t d) {
  if (!d) return fail(S::DivisionByZero);
  return number(Rational::of(BigInt(n), BigInt(d)));
}
ExactNumber ExactNumber::failure(ExactStatus status) { return ExactAccess::status(status); }
ExactNumber ExactNumber::pi() { return atomPower(Atom{AtomKind::Pi, 0, BigInt(0), {}}, Rational(1)); }
ExactNumber ExactNumber::e() { return atomPower(Atom{AtomKind::E, 0, BigInt(0), {}}, Rational(1)); }

ExactNumber ExactNumber::fromLiteral(const std::string& s) {
  size_t i = 0;
  bool negative = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) negative = s[i++] == '-';
  std::string digits;
  int64_t scale = 0;
  bool point = false, any = false;
  for (; i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'); ++i) {
    if (s[i] == '.') {
      if (point) return inexact();
      point = true;
      continue;
    }
    digits += s[i];
    any = true;
    if (point) --scale;
  }
  if (!any) return inexact();
  if (i < s.size() && (s[i] == 'E' || s[i] == 'e')) {
    ++i;
    bool expNegative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) expNegative = s[i++] == '-';
    int64_t exponent = 0;
    bool expDigits = false;
    for (; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); ++i) {
      exponent = exponent * 10 + (s[i] - '0');
      expDigits = true;
      if (exponent > 100000) return inexact();
    }
    if (!expDigits) return inexact();
    scale += expNegative ? -exponent : exponent;
  }
  if (i != s.size()) return inexact();
  if (scale > 1300 || scale < -1300) return inexact();
  BigInt n;
  if (!BigInt::parse(digits, n)) return inexact();
  if (negative) n = -n;
  const BigInt p = BigInt(10).pow(static_cast<uint32_t>(scale < 0 ? -scale : scale));
  return number(scale < 0 ? Rational::of(n, p) : Rational(n * p));
}

namespace {
// Recursive descent over the canonical syntax (and ordinary arithmetic).
class Reader {
 public:
  explicit Reader(const std::string& s) : s_(s) {}
  ExactNumber run() {
    ExactNumber x = sum(0);
    skip();
    return ok_ && at_ == s_.size() ? x : inexact();
  }

 private:
  const std::string& s_;
  size_t at_ = 0;
  bool ok_ = true;
  void skip() { while (at_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[at_]))) ++at_; }
  bool eat(char c) {
    skip();
    if (at_ < s_.size() && s_[at_] == c) { ++at_; return true; }
    return false;
  }
  ExactNumber sum(unsigned depth) {
    if (depth > 64) { ok_ = false; return inexact(); }
    ExactNumber x = product(depth);
    for (;;) {
      if (eat('+')) x = x + product(depth);
      else if (eat('-')) x = x - product(depth);
      else return x;
    }
  }
  ExactNumber product(unsigned depth) {
    ExactNumber x = unary(depth);
    for (;;) {
      if (eat('*')) x = x * unary(depth);
      else if (eat('/')) x = x / unary(depth);
      else return x;
    }
  }
  ExactNumber unary(unsigned depth) {
    if (eat('-')) return -unary(depth + 1);
    if (eat('+')) return unary(depth + 1);
    ExactNumber base = primary(depth);
    if (eat('^')) return base.pow(unary(depth + 1));
    return base;
  }
  ExactNumber primary(unsigned depth) {
    skip();
    if (eat('(')) {
      ExactNumber x = sum(depth + 1);
      if (!eat(')')) ok_ = false;
      return x;
    }
    const size_t start = at_;
    if (at_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[at_])) || s_[at_] == '.')) {
      while (at_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[at_])) || s_[at_] == '.')) ++at_;
      if (at_ < s_.size() && s_[at_] == 'E') {
        ++at_;
        if (at_ < s_.size() && (s_[at_] == '+' || s_[at_] == '-')) ++at_;
        while (at_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[at_]))) ++at_;
      }
      return ExactNumber::fromLiteral(s_.substr(start, at_ - start));
    }
    while (at_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[at_]))) ++at_;
    const std::string name = s_.substr(start, at_ - start);
    if (name == "pi") return ExactNumber::pi();
    if (name == "e") return ExactNumber::e();
    if (!eat('(')) { ok_ = false; return inexact(); }
    ExactNumber a = sum(depth + 1), b;
    const bool two = eat(',');
    if (two) b = sum(depth + 1);
    if (!eat(')')) ok_ = false;
    if (name == "sqrt" && !two) return a.sqrt();
    if (name == "ln" && !two) return a.ln();
    if (name == "exp" && !two) return a.exp();
    if (name == "root" && two) {
      if (!a.isInteger() || a.rationalValue().sign() <= 0 || a.rationalValue().num().bits() > 16) return inexact();
      return b.root(static_cast<uint32_t>(a.rationalValue().num().toInt64()));
    }
    ok_ = false;
    return inexact();
  }
};
}  // namespace
ExactNumber ExactNumber::parse(const std::string& text) { return Reader(text).run(); }

// ---- Arithmetic ---------------------------------------------------------------
ExactNumber ExactNumber::operator-() const {
  if (!exact()) return *this;
  if (!rep_) return number(-rational_);
  return scaled(rep_->terms, Rational(-1));
}
ExactNumber operator+(const ExactNumber& a, const ExactNumber& b) {
  ExactNumber out;
  if (propagate(a, b, out)) return out;
  if (!a.rep_ && !b.rep_) return number(a.rational_ + b.rational_);
  Terms t = termsOf(a);
  const Terms u = termsOf(b);
  if (t.size() + u.size() > 2 * ExactNumber::kMaxTerms) return inexact();
  t.insert(t.end(), u.begin(), u.end());
  return normalize(std::move(t));
}
ExactNumber operator-(const ExactNumber& a, const ExactNumber& b) { return a + (-b); }
ExactNumber operator*(const ExactNumber& a, const ExactNumber& b) {
  ExactNumber out;
  if (propagate(a, b, out)) return out;
  if (a.zero() || b.zero()) return number(Rational(0));
  if (!a.rep_ && !b.rep_) return number(a.rational_ * b.rational_);
  if (!a.rep_) return scaled(b.rep_->terms, a.rational_);
  if (!b.rep_) return scaled(a.rep_->terms, b.rational_);
  const Terms &x = a.rep_->terms, &y = b.rep_->terms;
  if (x.size() * y.size() > 4 * ExactNumber::kMaxTerms) return inexact();
  Terms t;
  for (const auto& p : x)
    for (const auto& q : y)
      if (!productTerms(p, q, t)) return inexact();
  return normalize(std::move(t));
}
ExactNumber ExactNumber::inverse() const {
  if (!exact()) return *this;
  if (zero()) return fail(S::DivisionByZero);
  if (!rep_) return number(rational_.inverse());
  const Terms& t = rep_->terms;
  if (t.size() == 1) return inverseMonomial(t[0]);
  // Pull out the transcendental part every term shares (pi^min, ...), so
  // pi + pi*sqrt(2) inverts as the algebraic 1 + sqrt(2).
  Factors common = t[0].monomial.factors;
  for (size_t i = 1; i < t.size(); ++i) {
    Factors keep;
    for (const auto& f : common) {
      if (f.atom.kind == AtomKind::RootOf) continue;
      for (const auto& g : t[i].monomial.factors)
        if (compareAtom(f.atom, g.atom) == 0) {
          keep.push_back(Factor{f.atom, Rational::compare(f.exponent, g.exponent) < 0 ? f.exponent : g.exponent});
          break;
        }
    }
    common.swap(keep);
  }
  Terms algebraic = t;
  for (auto& x : algebraic) {
    Factors negated;
    for (const auto& f : common) negated.push_back(Factor{f.atom, -f.exponent});
    x.monomial.factors = mergeFactors(x.monomial.factors, negated);
    if (!x.monomial.factors.empty()) return inexact();  // a transcendental denominator
  }
  const ExactNumber d = normalize(std::move(algebraic));
  if (!d.exact()) return d;
  ExactNumber inv;
  if (d.isRational()) inv = d.inverse();
  else {
    const Terms dt = termsOf(d);
    bool squareRoots = true;
    for (const auto& x : dt) squareRoots = squareRoots && x.monomial.surd.index <= 2;
    if (squareRoots) inv = inverseSquareRoots(d);
    else if (dt.size() == 2) inv = inverseTwoRadicals(dt);
    else return inexact();
  }
  if (common.empty()) return inv;
  Term m{Rational(1), Monomial{Surd{}, common}};
  return inv * inverseMonomial(m);
}
ExactNumber operator/(const ExactNumber& a, const ExactNumber& b) {
  ExactNumber out;
  if (propagate(a, b, out)) return out;
  if (b.zero()) return fail(S::DivisionByZero);
  if (a.zero()) return number(Rational(0));
  if (!a.rep_ && !b.rep_) return number(a.rational_ / b.rational_);
  // Proportional sums divide to a rational: ln(100)/ln(10) = 2.
  if (a.rep_ && b.rep_ && a.rep_->terms.size() == b.rep_->terms.size()) {
    const Terms &x = a.rep_->terms, &y = b.rep_->terms;
    const Rational ratio = x[0].coefficient / y[0].coefficient;
    bool proportional = true;
    for (size_t i = 0; i < x.size() && proportional; ++i)
      proportional = compareMonomial(x[i].monomial, y[i].monomial) == 0 && x[i].coefficient == ratio * y[i].coefficient;
    if (proportional) return number(ratio);
  }
  return a * b.inverse();
}

namespace {
ExactNumber integerPower(ExactNumber base, int64_t p) {
  if (p < 0) {
    base = base.inverse();
    if (!base.exact()) return base;
    p = -p;
  }
  if (base.isRational()) {
    const Rational& r = base.rationalValue();
    // x^p has at least p*(bits(x)-1)+1 bits: reject only what certainly
    // overflows (2^3700 fits); number() then checks the exact size.
    const size_t bits = std::max<size_t>(r.num().bits(), r.den().bits());
    if (bits > 1 && static_cast<uint64_t>(p) * (bits - 1) > ExactNumber::kMaxBits)
      return inexact();
    return number(Rational::of(r.num().pow(static_cast<uint32_t>(p)), r.den().pow(static_cast<uint32_t>(p))));
  }
  const ExactRep* rep = ExactAccess::get(base);
  if (p > (rep->terms.size() == 1 ? 4096 : 64)) return inexact();
  ExactNumber result = number(Rational(1));
  while (p) {
    if (p & 1) result = result * base;
    p >>= 1;
    if (p) base = base * base;
    if (!result.exact() || !base.exact()) return inexact();
  }
  return result;
}
// sqrt(a + b*sqrt(c)) = sqrt(x) + sign(b)*sqrt(y), x,y = (a +- d)/2, when
// d^2 = a^2 - b^2*c is a rational square.
bool denestSquareRoot(const ExactNumber& x, ExactNumber& out) {
  const ExactRep* rep = ExactAccess::get(x);
  if (!rep || rep->terms.size() != 2) return false;
  const Term &r = rep->terms[0], &s = rep->terms[1];
  if (!trivial(r.monomial) || !s.monomial.factors.empty() || s.monomial.surd.index != 2) return false;
  const Rational& a = r.coefficient;
  const Rational& b = s.coefficient;
  if (a.sign() <= 0) return false;
  const Rational d2 = a * a - b * b * Rational(s.monomial.surd.radicand);
  BigInt n, d;
  if (d2.sign() < 0 || !d2.num().exactRoot(2, n) || !d2.den().exactRoot(2, d)) return false;
  const Rational root = Rational::of(n, d), half = Rational::of(BigInt(1), BigInt(2));
  const ExactNumber u = rootOfRational((a + root) * half, 2), v = rootOfRational((a - root) * half, 2);
  out = b.sign() < 0 ? u - v : u + v;
  return out.exact();
}
// Lexicographic order on the exponent vectors of two transcendental
// monomials (atoms in structural order): multiplicative, so the leading
// term of a product is the product of the leading terms.
int orderFactors(const Factors& a, const Factors& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    const int c = i == a.size() ? 1 : j == b.size() ? -1 : compareAtom(a[i].atom, b[j].atom);
    const Rational x = c <= 0 ? a[i].exponent : Rational(0), y = c >= 0 ? b[j].exponent : Rational(0);
    if (const int d = Rational::compare(x, y)) return d;
    if (c <= 0) ++i;
    if (c >= 0) ++j;
  }
  return 0;
}
const Term& leadingTerm(const Terms& t) {
  size_t best = 0;
  for (size_t i = 1; i < t.size(); ++i)
    if (orderFactors(t[i].monomial.factors, t[best].monomial.factors) > 0) best = i;
  return t[best];
}
// sqrt(x) of a positive sum of rationals and pi/e/log monomials that is a
// perfect square, e.g. pi^2+2pi+1 = (pi+1)^2: the polynomial square root by
// leading terms, y_(n+1) = lead(x - y^2) / (2 lead(y)), then verified exactly.
// Surds and nested radicals are left to denesting.
bool squareRootOfSum(const ExactNumber& x, ExactNumber& out) {
  const Terms terms = termsOf(x);
  for (const auto& t : terms) {
    if (t.monomial.surd.index != 1) return false;
    for (const auto& f : t.monomial.factors)
      if (f.atom.kind == AtomKind::RootOf) return false;
  }
  const Term& lead = leadingTerm(terms);
  BigInt n, d;
  if (lead.coefficient.sign() <= 0 || !lead.coefficient.num().exactRoot(2, n) || !lead.coefficient.den().exactRoot(2, d))
    return false;
  Term top{Rational::of(n, d), Monomial{}};
  for (const auto& f : lead.monomial.factors) {
    const Rational half = f.exponent * Rational::of(BigInt(1), BigInt(2));
    if ((f.atom.kind == AtomKind::Log || f.atom.kind == AtomKind::LogOf) && !half.integer()) return false;
    top.monomial.factors.push_back(Factor{f.atom, half});
  }
  ExactNumber y = fromTerm(Term(top));
  for (size_t step = 0; step <= terms.size(); ++step) {
    const ExactNumber rest = x - y * y;
    if (!rest.exact()) return false;
    if (rest.zero()) {
      // The positive root: (pi-4)^2 has root 4-pi.
      bool certain = false;
      const int s = y.sign(&certain);
      if (!certain || !s) return false;
      out = s > 0 ? y : -y;
      return true;
    }
    const Terms restTerms = termsOf(rest);
    const Term& r = leadingTerm(restTerms);
    Factors negated;
    for (const auto& f : top.monomial.factors) negated.push_back(Factor{f.atom, -f.exponent});
    Term next{r.coefficient / (top.coefficient * Rational(2)), Monomial{Surd{}, mergeFactors(r.monomial.factors, negated)}};
    // Each new term sits below the leading one, and a log keeps an integer power.
    if (orderFactors(next.monomial.factors, top.monomial.factors) >= 0) return false;
    for (const auto& f : next.monomial.factors)
      if ((f.atom.kind == AtomKind::Log || f.atom.kind == AtomKind::LogOf) && !f.exponent.integer()) return false;
    y = y + fromTerm(std::move(next));
    if (!y.exact()) return false;
  }
  return false;
}
// root(k, x) of a positive sum.
ExactNumber rootOfSum(const ExactNumber& x, uint32_t k) {
  ExactNumber denested;
  if (k % 2 == 0 && denestSquareRoot(x, denested)) return k == 2 ? denested : denested.root(k / 2);
  if (k % 2 == 0 && squareRootOfSum(x, denested)) return k == 2 ? denested : denested.root(k / 2);
  // sqrt(a*sqrt(p) + b*sqrt(q)) = p^(1/4) * sqrt(a + b*sqrt(pq)/p), which may
  // denest: sqrt(57sqrt(17)+68sqrt(10)) = 17^(1/4)*(2sqrt(10)+sqrt(17)).
  if (k == 2) {
    const ExactRep* rep = ExactAccess::get(x);
    if (rep && rep->terms.size() == 2 && rep->terms[0].monomial.factors.empty() &&
        rep->terms[1].monomial.factors.empty() && rep->terms[0].monomial.surd.index == 2 &&
        rep->terms[1].monomial.surd.index == 2) {
      for (const auto& t : rep->terms) {  // either radicand may be the one to pull out
        const Rational p(t.monomial.surd.radicand);
        if (denestSquareRoot(x / rootOfRational(p, 2), denested)) return rootOfRational(p, 4) * denested;
      }
    }
  }
  const Terms t = termsOf(x);
  const Rational c = content(t);
  const ExactNumber primitive = scaled(t, c.inverse());
  if (!primitive.exact() || primitive.depth() + 1 > ExactNumber::kMaxDepth) return inexact();
  return rootOfRational(c, k) * atomPower(Atom{AtomKind::RootOf, k, BigInt(0), primitive}, Rational(1));
}
// x^(p/q), q > 1, x a single positive-atom term.
ExactNumber fractionalPowerOfTerm(const Term& t, const Rational& exponent) {
  const int64_t p = exponent.num().toInt64();
  const uint32_t q = static_cast<uint32_t>(exponent.den().toInt64());
  // (-c)^(p/q) = (-1)^p c^(p/q) for odd q; atoms are positive.
  ExactNumber result = number(Rational(1));
  Rational c = t.coefficient;
  if (c.sign() < 0) {
    if (q % 2 == 0) return fail(S::NotReal);
    c = -c;
    if (p % 2) result = number(Rational(-1));
  }
  const ExactNumber cp = integerPower(number(c), p);
  if (!cp.isRational()) return inexact();
  result = result * rootOfRational(cp.rationalValue(), q);
  const Surd& s = t.monomial.surd;
  if (s.index > 1) {
    if (static_cast<uint64_t>(s.index) * q > ExactNumber::kMaxIndex) return inexact();
    const ExactNumber np = integerPower(number(Rational(s.radicand)), p);
    if (!np.isRational()) return inexact();
    result = result * rootOfRational(np.rationalValue(), s.index * q);
  }
  for (const auto& f : t.monomial.factors) {
    const Rational e = f.exponent * exponent;
    switch (f.atom.kind) {
      case AtomKind::Pi:
      case AtomKind::E:
        result = result * atomPower(f.atom, e);
        break;
      case AtomKind::Log:
      case AtomKind::LogOf:
        if (!e.integer()) return inexact();
        result = result * atomPower(f.atom, e);
        break;
      case AtomKind::RootOf: {
        // root(k, A)^l ^ (p/q) = root(k*q, A)^(l*p)
        const uint64_t index = static_cast<uint64_t>(f.atom.index) * q;
        if (index > ExactNumber::kMaxIndex) return inexact();
        Atom a = f.atom;
        a.index = static_cast<uint32_t>(index);
        result = result * finishTerm(Rational(1), Surd{}, Factors{Factor{a, f.exponent * Rational(p)}});
        break;
      }
    }
    if (!result.exact()) return result.failed() ? result : inexact();
  }
  return result;
}
}  // namespace

ExactNumber ExactNumber::pow(const ExactNumber& exponent) const {
  ExactNumber out;
  if (propagate(*this, exponent, out)) return out;
  if (!exponent.isRational()) {
    // x^y = exp(y ln x): exact only when that product folds back, e.g.
    // 2^(ln(3)/ln(2)) = 3. A non-positive base with an irrational exponent
    // is left to the decimal path.
    if (sign() <= 0) return inexact();
    const ExactNumber r = (exponent * ln()).exp();
    return r.exact() ? r : inexact();
  }
  const Rational& e = exponent.rationalValue();
  if (zero()) return e.sign() > 0 ? number(Rational(0)) : fail(S::DivisionByZero);
  if (e.num().bits() > 32 || e.den().bits() > 16) return inexact();
  if (e.integer()) return integerPower(*this, e.num().toInt64());
  const uint32_t q = static_cast<uint32_t>(e.den().toInt64());
  if (q > kMaxIndex) return inexact();
  if (!rep_) {
    const ExactNumber cp = integerPower(*this, e.num().toInt64());
    if (!cp.isRational()) return cp.failed() ? cp : inexact();
    return rootOfRational(cp.rationalValue(), q);
  }
  if (rep_->terms.size() == 1) return fractionalPowerOfTerm(rep_->terms[0], e);
  bool certain = false;
  const int s = sign(&certain);
  if (!certain) return inexact();
  if (s < 0) {
    if (q % 2 == 0) return fail(S::NotReal);
    const ExactNumber r = (-*this).pow(exponent);
    return e.num().odd() ? -r : r;
  }
  const ExactNumber r = rootOfSum(*this, q);
  if (!r.exact()) return r;
  return integerPower(r, e.num().toInt64());
}
ExactNumber ExactNumber::root(uint32_t index) const {
  if (index == 0) return inexact();
  return pow(number(Rational::of(BigInt(1), BigInt(static_cast<int64_t>(index)))));
}

namespace {
ExactNumber lnSigned(const ExactNumber& x, bool allowInvert);
// ln(n) for an integer n >= 1: trial primes, then a perfect-power cofactor.
ExactNumber lnInteger(BigInt n) {
  ExactNumber r = number(Rational(0));
  forTrialDivisors([&](uint32_t p) {
    if (n == BigInt(1)) return false;
    int64_t v = 0;
    while (n.modSmall(p) == 0) { n = n / BigInt(p); ++v; }
    if (v) r = r + number(Rational(v)) * atomPower(Atom{AtomKind::Log, 0, BigInt(p), {}}, Rational(1));
    return true;
  });
  if (n != BigInt(1)) {
    int64_t power = 1;
    for (bool changed = true; changed;) {
      changed = false;
      BigInt root;
      // A cofactor's prime factors exceed the trial limit, so j <= bits/10.
      for (uint32_t j = 2; j <= n.bits() / 10 + 1; ++j)
        if (n.exactRoot(j, root)) { n = root; power *= j; changed = true; break; }
    }
    r = r + number(Rational(power)) * atomPower(Atom{AtomKind::Log, 0, n, {}}, Rational(1));
  }
  return r;
}
ExactNumber lnRational(const Rational& r) { return lnInteger(r.num()) - lnInteger(r.den()); }
// ln(c) for a non-rational c > 0 that ln cannot split any further.
ExactNumber logAtom(const ExactNumber& c, bool allowInvert) {
  if (c.depth() + 1 > ExactNumber::kMaxDepth) return inexact();
  bool certain = false;
  const int s = ExactNumber::compare(c, number(Rational(1)), &certain);
  if (!certain) return inexact();
  if (s > 0) return atomPower(Atom{AtomKind::LogOf, 0, BigInt(0), c}, Rational(1));
  if (!allowInvert) return inexact();
  const ExactNumber inverse = c.inverse();
  if (!inverse.exact()) return inexact();
  // A single term would split straight back into ln(c): keep 1/c whole.
  const ExactRep* rep = ExactAccess::get(inverse);
  if (rep && rep->terms.size() == 1) return -atomPower(Atom{AtomKind::LogOf, 0, BigInt(0), inverse}, Rational(1));
  return -lnSigned(inverse, false);
}
ExactNumber lnSigned(const ExactNumber& x, bool allowInvert) {
  if (!x.exact()) return x;
  if (x.zero()) return fail(S::NotReal);
  bool certain = false;
  const int s = x.sign(&certain);
  if (!certain) return inexact();
  if (s < 0) return fail(S::NotReal);
  if (x.isRational()) return lnRational(x.rationalValue());
  const Terms t = termsOf(x);
  if (t.size() > 1) {
    const Rational c = content(t);
    return lnRational(c) + logAtom(scaled(t, c.inverse()), allowInvert);
  }
  const Term& term = t[0];
  ExactNumber r = lnRational(term.coefficient);
  const Surd& surd = term.monomial.surd;
  if (surd.index > 1) r = r + lnInteger(surd.radicand) * number(Rational::of(BigInt(1), BigInt(surd.index)));
  for (const auto& f : term.monomial.factors) {
    switch (f.atom.kind) {
      case AtomKind::E: r = r + number(f.exponent); break;
      case AtomKind::RootOf:
        r = r + lnSigned(f.atom.arg, allowInvert) * number(f.exponent * Rational::of(BigInt(1), BigInt(f.atom.index)));
        break;
      default:  // pi, ln(n), ln(A): a logarithm of the atom itself
        r = r + logAtom(atomPower(f.atom, Rational(1)), allowInvert) * number(f.exponent);
        break;
    }
    if (!r.exact()) return r.failed() ? r : inexact();
  }
  return r;
}
}  // namespace

ExactNumber ExactNumber::ln() const { return lnSigned(*this, true); }
ExactNumber ExactNumber::exp() const {
  if (!exact()) return *this;
  // Only sums of a rational and logarithms fold: e^(r + c*ln(A)) = e^r * A^c.
  ExactNumber result = number(Rational(1));
  for (const auto& t : termsOf(*this)) {
    const auto& f = t.monomial.factors;
    if (trivial(t.monomial)) {
      result = result * atomPower(Atom{AtomKind::E, 0, BigInt(0), {}}, t.coefficient);
      continue;
    }
    if (t.monomial.surd.index != 1 || f.size() != 1 || !f[0].exponent.isOne()) return inexact();
    ExactNumber base;
    if (f[0].atom.kind == AtomKind::Log) base = number(Rational(f[0].atom.n));
    else if (f[0].atom.kind == AtomKind::LogOf) base = f[0].atom.arg;
    else return inexact();
    result = result * base.pow(number(t.coefficient));
    if (!result.exact()) return result.failed() ? result : inexact();
  }
  return result;
}
ExactNumber ExactNumber::log(const ExactNumber& base) const {
  ExactNumber out;
  if (propagate(*this, base, out)) return out;
  return ln() / base.ln();
}
ExactNumber ExactNumber::abs() const {
  if (!exact()) return *this;
  bool certain = false;
  const int s = sign(&certain);
  if (!certain) return inexact();
  return s < 0 ? -*this : *this;
}

bool operator==(const ExactNumber& a, const ExactNumber& b) { return compareStructural(a, b) == 0; }

bool ExactNumber::piMultiple(Rational& q) const {
  if (!exact()) return false;
  if (!rep_) {
    if (!rational_.zero()) return false;
    q = Rational(0);
    return true;
  }
  if (rep_->terms.size() != 1) return false;
  const Term& t = rep_->terms[0];
  if (t.monomial.surd.index != 1 || t.monomial.factors.size() != 1) return false;
  const Factor& f = t.monomial.factors[0];
  if (f.atom.kind != AtomKind::Pi || !f.exponent.isOne()) return false;
  q = t.coefficient;
  return true;
}
bool ExactNumber::simpleTerms(std::vector<SimpleTerm>& out) const {
  out.clear();
  if (!exact()) return false;
  for (const auto& t : termsOf(*this)) {
    const auto& m = t.monomial;
    SimpleTerm s;
    s.coefficient = t.coefficient;
    if (m.factors.empty() && m.surd.index <= 2) s.radicand = m.surd.radicand;
    else if (m.surd.index == 1 && m.factors.size() == 1 && m.factors[0].exponent.isOne() &&
             (m.factors[0].atom.kind == AtomKind::Pi || m.factors[0].atom.kind == AtomKind::E)) {
      s.pi = m.factors[0].atom.kind == AtomKind::Pi;
      s.e = !s.pi;
    } else return false;
    out.push_back(s);
  }
  return true;
}

bool ExactNumber::constantTerms(bool& pi, std::vector<SimpleTerm>& out) const {
  out.clear();
  if (!exact()) return false;
  bool seen = false;
  for (const auto& t : termsOf(*this)) {
    const auto& m = t.monomial;
    if (m.surd.index > 2 || m.factors.size() != 1 || !m.factors[0].exponent.isOne()) return false;
    const AtomKind kind = m.factors[0].atom.kind;
    if (kind != AtomKind::Pi && kind != AtomKind::E) return false;
    if (seen && pi != (kind == AtomKind::Pi)) return false;
    pi = kind == AtomKind::Pi;
    seen = true;
    SimpleTerm s;
    s.coefficient = t.coefficient;
    s.radicand = m.surd.radicand;
    out.push_back(s);
    // Insertion by radicand: a couple of terms, and std::sort's frames cost
    // the loop task more stack than the whole walk.
    for (size_t i = out.size() - 1; i > 0 && out[i].radicand < out[i - 1].radicand; --i) std::swap(out[i], out[i - 1]);
  }
  return seen;
}

unsigned ExactNumber::depth() const {
  unsigned d = 0;
  if (!rep_) return 0;
  for (const auto& t : rep_->terms)
    for (const auto& f : t.monomial.factors)
      if (f.atom.kind == AtomKind::LogOf || f.atom.kind == AtomKind::RootOf) d = std::max(d, f.atom.arg.depth() + 1);
  return d;
}

size_t ExactNumber::heapBytes() const {
  size_t bytes = rational_.heapBytes();
  if (!rep_) return bytes;
  // The shared representation is counted in full by every holder: an
  // overcount, never an undercount, for history budgeting.
  bytes += sizeof(ExactRep) + 32 + rep_->terms.capacity() * sizeof(Term);
  for (const auto& t : rep_->terms) {
    bytes += t.coefficient.heapBytes() + t.monomial.surd.radicand.heapBytes();
    bytes += t.monomial.factors.capacity() * sizeof(Factor);
    for (const auto& f : t.monomial.factors) bytes += f.atom.n.heapBytes() + f.exponent.heapBytes() + f.atom.arg.heapBytes();
  }
  return bytes;
}

// ---- Text -----------------------------------------------------------------------
namespace {
std::string rationalText(const Rational& r) {
  return r.integer() ? r.num().str() : r.num().str() + "/" + r.den().str();
}
std::string exponentText(const Rational& e) {
  if (e.isOne()) return "";
  if (e.integer() && e.sign() > 0) return "^" + e.num().str();
  return "^(" + rationalText(e) + ")";
}
std::string factorText(const Factor& f) {
  const Atom& a = f.atom;
  std::string base;
  switch (a.kind) {
    case AtomKind::Pi: base = "pi"; break;
    case AtomKind::E: base = "e"; break;
    case AtomKind::Log: base = "ln(" + a.n.str() + ")"; break;
    case AtomKind::LogOf: base = "ln(" + a.arg.str() + ")"; break;
    case AtomKind::RootOf:
      base = a.index == 2 ? "sqrt(" + a.arg.str() + ")" : "root(" + std::to_string(a.index) + "," + a.arg.str() + ")";
      break;
  }
  return base + exponentText(f.exponent);
}
std::string termText(const Term& t) {
  const auto& m = t.monomial;
  if (trivial(m)) return rationalText(t.coefficient);
  std::string body;
  auto append = [&](const std::string& piece) { body += (body.empty() ? "" : "*") + piece; };
  if (m.surd.index == 2) append("sqrt(" + m.surd.radicand.str() + ")");
  else if (m.surd.index > 2) append("root(" + std::to_string(m.surd.index) + "," + m.surd.radicand.str() + ")");
  for (const auto& f : m.factors) append(factorText(f));
  if (t.coefficient.isOne()) return body;
  if (t.coefficient == Rational(-1)) return "-" + body;
  return rationalText(t.coefficient) + "*" + body;
}
}  // namespace
std::string ExactNumber::str() const {
  if (!exact()) return "";
  if (!rep_) return rationalText(rational_);
  std::string s;
  for (const auto& t : rep_->terms) {
    const std::string text = termText(t);
    s += s.empty() || text[0] == '-' ? text : "+" + text;
  }
  return s;
}

// ---- Approximation ----------------------------------------------------------------
// A binary float m*2^e with arbitrary-precision mantissa. Each primitive is
// computed with guard bits so its result is within a few units of its last
// bit; approxRelative() then checks the accumulated error bound of a sum and
// raises precision until the requested relative precision is certified.
namespace {
struct Approx {
  BigInt m;
  int64_t e = 0;
  bool ok = true;
};
int64_t magnitude(const Approx& a) { return a.m.zero() ? INT64_MIN / 4 : static_cast<int64_t>(a.m.bits()) + a.e; }
Approx trimmed(Approx a, size_t bits) {
  const size_t have = a.m.bits();
  if (have > bits) {
    a.m = a.m.shifted(-static_cast<int64_t>(have - bits));
    a.e += static_cast<int64_t>(have - bits);
  }
  return a;
}
Approx fromRational(const Rational& r, size_t bits) {
  const int64_t s = static_cast<int64_t>(bits + 8 + r.den().bits()) - static_cast<int64_t>(r.num().bits());
  Approx a;
  a.m = s >= 0 ? r.num().shifted(s) / r.den() : r.num() / r.den().shifted(-s);
  a.e = -s;
  return a;
}
Approx multiply(const Approx& a, const Approx& b, size_t bits) {
  Approx r{a.m * b.m, a.e + b.e, a.ok && b.ok};
  return trimmed(r, bits);
}
Approx divide(const Approx& a, const Approx& b, size_t bits) {
  const int64_t s = std::max<int64_t>(0, static_cast<int64_t>(bits + 8 + b.m.bits()) - static_cast<int64_t>(a.m.bits()));
  Approx r{a.m.shifted(s) / b.m, a.e - b.e - s, a.ok && b.ok && !b.m.zero()};
  return trimmed(r, bits);
}
Approx add(const Approx& a, const Approx& b, size_t bits) {
  if (a.m.zero()) return b;
  if (b.m.zero()) return a;
  // A term below the other's precision is inside the error bound.
  if (magnitude(a) < magnitude(b) - static_cast<int64_t>(bits) - 64) return b;
  if (magnitude(b) < magnitude(a) - static_cast<int64_t>(bits) - 64) return a;
  const int64_t e = std::min(a.e, b.e);
  return Approx{a.m.shifted(a.e - e) + b.m.shifted(b.e - e), e, a.ok && b.ok};
}
Approx powerInt(Approx x, int64_t n, size_t bits) {
  const bool invert = n < 0;
  uint64_t k = invert ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
  Approx r{BigInt(1), 0, true};
  while (k) {
    if (k & 1) r = multiply(r, x, bits + 16);
    k >>= 1;
    if (k) x = multiply(x, x, bits + 16);
  }
  return invert ? divide(Approx{BigInt(1), 0, true}, r, bits) : trimmed(r, bits);
}
// atan(1/x) and atanh(1/x) in fixed point with `f` fraction bits.
BigInt arcFixed(int64_t x, bool hyperbolic, size_t f) {
  const BigInt one = BigInt(1).shifted(static_cast<int64_t>(f)), xx(x * x);
  BigInt term = one / BigInt(x), sum = term;
  for (int64_t n = 1; !term.zero(); ++n) {
    term = term / xx;
    const BigInt t = term / BigInt(2 * n + 1);
    sum = hyperbolic || n % 2 == 0 ? sum + t : sum - t;
  }
  return sum;
}
// floor(pi * 2^320) and floor(ln 2 * 2^320), little-endian limbs (generated
// with Python's exact decimal arithmetic). The engine's 40-digit work needs
// ~170 bits, so series only run for approximate() at high precision.
constexpr size_t kConstantBits = 320;
constexpr uint32_t kPiLimbs[] = {0x38D01377u, 0x452821E6u, 0xEC4E6C89u, 0x082EFA98u, 0x299F31D0u, 0xA4093822u,
                                 0x03707344u, 0x13198A2Eu, 0x85A308D3u, 0x243F6A88u, 0x00000003u};
constexpr uint32_t kLn2Limbs[] = {0x6DEBAC98u, 0xE7B87620u, 0x8BAAFA2Bu, 0x8A0D175Bu, 0x7298B62Du,
                                  0x40F34326u, 0x03F2F6AFu, 0xC9E3B398u, 0xD1CF79ABu, 0xB17217F7u};
BigInt piFixed(size_t f) {  // Machin: pi = 16 atan(1/5) - 4 atan(1/239)
  if (f + 1 < kConstantBits)
    return BigInt::fromLimbs(kPiLimbs, sizeof(kPiLimbs) / 4).shifted(static_cast<int64_t>(f) - static_cast<int64_t>(kConstantBits));
  const size_t g = f + 16;
  return (arcFixed(5, false, g) * BigInt(16) - arcFixed(239, false, g) * BigInt(4)).shifted(-16);
}
BigInt ln2Fixed(size_t f) {  // ln 2 = 2 atanh(1/3)
  if (f + 1 < kConstantBits)
    return BigInt::fromLimbs(kLn2Limbs, sizeof(kLn2Limbs) / 4).shifted(static_cast<int64_t>(f) - static_cast<int64_t>(kConstantBits));
  return (arcFixed(3, true, f + 16) * BigInt(2)).shifted(-16);
}
Approx piApprox(size_t bits) { return Approx{piFixed(bits + 8), -static_cast<int64_t>(bits + 8), true}; }
// ln(x), x > 0 given exactly: x = f*2^k with f in [sqrt(1/2), sqrt(2)),
// ln x = k ln 2 + 2 atanh((f-1)/(f+1)). Fixed point widens until the result
// itself carries `bits` significant bits (x near 1 needs more fraction bits).
Approx lnApprox(const Approx& x, size_t bits) {
  if (!x.ok || x.m.sign() <= 0) return Approx{BigInt(0), 0, false};
  int64_t k = static_cast<int64_t>(x.m.bits()) + x.e;
  for (size_t extra = 32; extra < kMaxApproxBits; extra *= 4) {
    const size_t f = bits + extra + 64;
    const BigInt one = BigInt(1).shifted(static_cast<int64_t>(f));
    BigInt fx = x.m.shifted(static_cast<int64_t>(f) - static_cast<int64_t>(x.m.bits()));  // in [1/2, 1)
    int64_t kk = k;
    if ((fx * fx).shifted(1) < one * one) { fx = fx.shifted(1); --kk; }  // f < sqrt(1/2): use 2f
    const BigInt y = (fx - one).shifted(static_cast<int64_t>(f)) / (fx + one);
    const BigInt yy = (y * y).shifted(-static_cast<int64_t>(f));
    BigInt term = y, sum = y;
    for (int64_t n = 1; !term.zero(); ++n) {
      term = (term * yy).shifted(-static_cast<int64_t>(f));
      sum = sum + term / BigInt(2 * n + 1);
    }
    const BigInt fixed = BigInt(kk) * ln2Fixed(f) + sum.shifted(1);
    if (fixed.bits() >= bits + 16 || fixed.zero()) return trimmed(Approx{fixed, -static_cast<int64_t>(f), true}, bits + 8);
  }
  return Approx{BigInt(0), 0, false};
}
// exp(y) for y given exactly: y = k ln 2 + r, exp(r) by Taylor on r/2^8 then
// eight squarings.
Approx expApprox(const Approx& y, size_t bits) {
  if (!y.ok) return y;
  if (y.m.zero()) return Approx{BigInt(1), 0, true};
  if (magnitude(y) > 40) return Approx{BigInt(0), 0, false};  // far past any calculator range
  const int64_t kSquarings = 8;
  const size_t f = bits + 64 + kSquarings;
  const BigInt one = BigInt(1).shifted(static_cast<int64_t>(f));
  const BigInt yf = y.m.shifted(y.e + static_cast<int64_t>(f));
  const BigInt ln2 = ln2Fixed(f + 64).shifted(-64);
  BigInt k, rem;
  BigInt::divMod(yf + ln2.shifted(-1), ln2, k, rem);  // round(y / ln 2), near enough
  const BigInt r = yf - k * ln2;
  BigInt term = one, sum = one;
  for (int64_t n = 1; !term.zero(); ++n) {
    term = (term * r).shifted(-static_cast<int64_t>(f) - kSquarings) / BigInt(n);
    sum = sum + term;
  }
  for (int64_t i = 0; i < kSquarings; ++i) sum = (sum * sum).shifted(-static_cast<int64_t>(f));
  return trimmed(Approx{sum, k.toInt64() - static_cast<int64_t>(f), true}, bits + 8);
}
Approx rootApprox(const Approx& x, uint32_t k, size_t bits) {
  if (k == 1 || x.m.zero()) return x;
  int64_t t = std::max<int64_t>(0, static_cast<int64_t>(k * (bits + 8)) - static_cast<int64_t>(x.m.bits()));
  t += ((x.e - t) % k + k) % k;  // (e - t) divisible by k
  return trimmed(Approx{x.m.shifted(t).root(k), (x.e - t) / static_cast<int64_t>(k), x.ok}, bits + 8);
}
bool approxRelative(const ExactNumber& x, size_t bits, Approx& out);
Approx approxFactor(const Factor& f, size_t bits) {
  const Atom& a = f.atom;
  const size_t w = bits + 16;
  switch (a.kind) {
    case AtomKind::Pi:
      if (f.exponent.integer()) return powerInt(piApprox(w), f.exponent.num().toInt64(), w);
      {
        const Rational& q = f.exponent;
        const size_t extra = q.floor().abs().bits() + 8;
        return expApprox(multiply(fromRational(q, w + extra), lnApprox(piApprox(w + extra), w + extra), w + extra), w);
      }
    case AtomKind::E: {
      const size_t extra = f.exponent.floor().abs().bits() + 8;
      return expApprox(fromRational(f.exponent, w + extra), w);
    }
    case AtomKind::Log:
      return powerInt(lnApprox(fromRational(Rational(a.n), w), w), f.exponent.num().toInt64(), w);
    case AtomKind::LogOf: {
      // ln's absolute error is the argument's relative error, so an argument
      // near 1 needs extra precision to give ln(arg) `w` significant bits.
      for (size_t p = w + 16; p < kMaxApproxBits; p *= 2) {
        Approx c;
        if (!approxRelative(a.arg, p, c)) break;
        const Approx l = lnApprox(c, p);
        if (!l.ok) break;
        const int64_t lost = -magnitude(l);
        if (lost <= 0 || p >= w + 16 + static_cast<size_t>(lost))
          return powerInt(l, f.exponent.num().toInt64(), w);
      }
      return Approx{BigInt(0), 0, false};
    }
    case AtomKind::RootOf: {
      Approx c;
      if (!approxRelative(a.arg, w + 16, c)) return Approx{BigInt(0), 0, false};
      return powerInt(rootApprox(c, a.index, w), f.exponent.num().toInt64(), w);
    }
  }
  return Approx{BigInt(0), 0, false};
}
Approx approxTerm(const Term& t, size_t bits) {
  Approx r = fromRational(t.coefficient, bits + 16);
  const Surd& s = t.monomial.surd;
  if (s.index > 1) r = multiply(r, rootApprox(fromRational(Rational(s.radicand), bits + 16), s.index, bits + 16), bits + 16);
  for (const auto& f : t.monomial.factors) r = multiply(r, approxFactor(f, bits + 16), bits + 16);
  return r;
}
// Relative precision `bits`, certified by the sum's error bound; false (with
// the best estimate in `out`) when the bound cannot be met, which for a
// canonical, structurally nonzero value only happens past the precision cap.
bool approxRelative(const ExactNumber& x, size_t bits, Approx& out) {
  if (!x.exact()) { out = Approx{BigInt(0), 0, false}; return false; }
  if (x.isRational()) { out = fromRational(x.rationalValue(), bits); return true; }
  const Terms t = termsOf(x);
  size_t w = bits + 32;
  for (;;) {
    Approx sum{BigInt(0), 0, true};
    int64_t largest = INT64_MIN / 4;
    for (const auto& term : t) {
      const Approx a = approxTerm(term, w);
      if (!a.ok) { out = a; return false; }
      largest = std::max(largest, magnitude(a));
      sum = add(sum, a, w);
    }
    // Each term is within 2^-(w-12) relative; the sum's absolute error is
    // below terms * largest * 2^-(w-12).
    const int64_t error = largest + 8 - static_cast<int64_t>(w - 12);
    const int64_t have = sum.m.zero() ? INT64_MIN / 4 : magnitude(sum) - error;
    if (have >= static_cast<int64_t>(bits) + 2) { out = trimmed(sum, bits + 8); return true; }
    if (w >= kMaxApproxBits) { out = sum; return false; }
    const int64_t missing = sum.m.zero() ? static_cast<int64_t>(w) : static_cast<int64_t>(bits) + 2 - have;
    w = std::min(kMaxApproxBits, w + static_cast<size_t>(std::max<int64_t>(64, missing + 32)));
  }
}
// `digits` significant digits of r, rounded half away from zero, as digit
// string and decimal exponent of the first digit.
void decimalDigits(const Rational& r, unsigned digits, std::string& mantissa, int64_t& exponent) {
  const BigInt num = r.num().abs(), den = r.den();
  if (num.zero()) { mantissa = std::string(digits, '0'); exponent = 0; return; }
  int64_t k = (static_cast<int64_t>(num.bits()) - static_cast<int64_t>(den.bits())) * 30103 / 100000;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const int64_t shift = static_cast<int64_t>(digits) - 1 - k;
    BigInt n = num, d = den;
    if (shift >= 0) n = n * BigInt(10).pow(static_cast<uint32_t>(shift));
    else d = d * BigInt(10).pow(static_cast<uint32_t>(-shift));
    const BigInt rounded = (n.shifted(1) + d) / d.shifted(1);
    const BigInt low = BigInt(10).pow(digits - 1), high = BigInt(10).pow(digits);
    if (rounded >= high) { ++k; continue; }
    if (rounded < low) { --k; continue; }
    mantissa = rounded.str();
    exponent = k;
    return;
  }
  mantissa = std::string(digits, '0');
  exponent = 0;
}
Rational approxValue(const Approx& a) {
  return a.e >= 0 ? Rational(a.m.shifted(a.e)) : Rational::of(a.m, BigInt(1).shifted(-a.e));
}
bool approxRational(const ExactNumber& x, unsigned digits, Rational& value) {
  if (x.isRational()) { value = x.rationalValue(); return true; }
  Approx a;
  approxRelative(x, static_cast<size_t>(digits) * 3322 / 1000 + 16, a);
  if (!a.ok) return false;
  value = approxValue(a);
  return true;
}
}  // namespace

int ExactNumber::sign(bool* certain) const {
  if (certain) *certain = exact();
  if (!exact()) return 0;
  if (!rep_) return rational_.sign();
  if (rep_->terms.size() == 1) return rep_->terms[0].coefficient.sign();
  Approx a;
  const bool ok = approxRelative(*this, 4, a);
  if (certain) *certain = ok && a.ok;
  return ok && a.ok ? a.m.sign() : 0;
}
int ExactNumber::compare(const ExactNumber& a, const ExactNumber& b, bool* certain) {
  if (a == b && a.exact()) {
    if (certain) *certain = true;
    return 0;
  }
  return (a - b).sign(certain);
}

Decimal ExactNumber::toDecimal() const {
  if (!exact()) return Decimal::nan();
  if (!rep_) {
    if (rational_.integer()) return Decimal(rational_.num().str());
    if (rational_.num().fitsInt64() && rational_.den().fitsInt64())
      return Decimal(rational_.num().str()) / Decimal(rational_.den().str());
  }
  Rational v;
  if (!approxRational(*this, 44, v)) return Decimal::nan();
  std::string mantissa;
  int64_t exponent = 0;
  decimalDigits(v, DECNUMDIGITS, mantissa, exponent);  // one rounding, straight to work precision
  if (v.sign() == 0) return Decimal(0);
  const std::string text = (v.sign() < 0 ? "-" : "") + mantissa.substr(0, 1) + "." + mantissa.substr(1) + "E" +
                           std::to_string(exponent);
  return Decimal(text);
}

std::string ExactNumber::approximate(unsigned digits) const {
  if (!exact()) return "";
  digits = std::max(1u, std::min(digits, kMaxDigits));
  if (zero()) return "0";
  Rational v;
  if (!approxRational(*this, digits + 2, v)) return "";
  std::string m;
  int64_t k = 0;
  decimalDigits(v, digits, m, k);
  while (m.size() > 1 && m.back() == '0') m.pop_back();
  std::string s = v.sign() < 0 ? "-" : "";
  if (k >= -5 && k < static_cast<int64_t>(digits)) {
    if (k < 0) return s + "0." + std::string(static_cast<size_t>(-k - 1), '0') + m;
    const size_t point = static_cast<size_t>(k) + 1;
    if (m.size() <= point) return s + m + std::string(point - m.size(), '0');
    return s + m.substr(0, point) + "." + m.substr(point);
  }
  s += m.substr(0, 1);
  if (m.size() > 1) s += "." + m.substr(1);
  return s + "E" + (k >= 0 ? "+" : "") + std::to_string(k);
}
}  // namespace Calculator
