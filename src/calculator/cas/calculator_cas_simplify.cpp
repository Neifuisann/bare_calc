#include "calculator/cas/calculator_cas_simplify.h"
#include "calculator/cas/calculator_cas_pattern.h"
#include "calculator/cas/calculator_cas_poly.h"
#include <algorithm>

namespace Calculator {
namespace Cas {
namespace {
constexpr uint32_t kExpandTerms = 2000;  // one distribution step
constexpr uint32_t kSimplifyCandidates = 400;  // node count up to which every candidate is tried

Args termsOf(const Expr& e) {
  if (e->kind == Kind::Add) return e->args;
  return Args{e};
}
Expr distribute(const Expr& a, const Expr& b) {
  if (a->kind != Kind::Add && b->kind != Kind::Add) return mul(a, b);
  const Args ta = termsOf(a), tb = termsOf(b);
  if (ta.size() * tb.size() > kExpandTerms) {
    context().fail(Error::Stack);
    return integer(0);
  }
  Args out;
  out.reserve(ta.size() * tb.size());
  for (const auto& x : ta)
    for (const auto& y : tb) out.push_back(mul(x, y));
  return add(std::move(out));
}

// ---- Content and monomial roots (factor) ------------------------------------
// The common factor of a sum's terms: the gcd of rational coefficients (made
// negative when every term is) times each base at its least power.
Expr commonFactor(const Expr& sum) {
  BigInt numerator(0), denominator(1);
  bool rational = true, allNegative = true;
  struct Power { Expr base; Rational exponent; unsigned seen; };
  std::vector<Power> powers;
  for (size_t i = 0; i < sum->args.size(); ++i) {
    ExactNumber c;
    Expr rest;
    splitTerm(sum->args[i], c, rest);
    if (c.isRational()) {
      const Rational& r = c.rationalValue();
      numerator = BigInt::gcd(numerator, r.num());
      denominator = denominator / BigInt::gcd(denominator, r.den()) * r.den();
      allNegative = allNegative && r.sign() < 0;
    } else {
      rational = false;
      allNegative = false;
    }
    Args factors = rest->kind == Kind::Mul ? rest->args : Args{rest};
    if (one(rest)) factors.clear();
    for (const auto& f : factors) {
      Expr b, e;
      splitPower(f, b, e);
      Rational q;
      if (!rationalValue(e, q)) {
        // B^(k*u): the base is B^u at power k, so e^(2x) and e^x share e^x.
        ExactNumber k;
        Expr u;
        splitTerm(e, k, u);
        if (k.isRational()) {
          b = pow(b, u);
          q = k.rationalValue();
        } else {
          b = f;
          q = Rational(1);
        }
      }
      if (q.sign() <= 0) continue;
      auto it = std::find_if(powers.begin(), powers.end(), [&](const Power& p) { return equal(p.base, b); });
      if (it == powers.end()) {
        if (i == 0) powers.push_back({b, q, 1});
      } else if (it->seen == i) {
        if (Rational::compare(q, it->exponent) < 0) it->exponent = q;
        ++it->seen;
      }
    }
    powers.erase(std::remove_if(powers.begin(), powers.end(), [&](const Power& p) { return p.seen != i + 1; }), powers.end());
  }
  Args content;
  if (rational && !numerator.zero()) {
    Rational c = Rational::of(numerator, denominator);
    if (allNegative) c = -c;
    content.push_back(number(ExactNumber::rational(c)));
  }
  for (const auto& p : powers) content.push_back(pow(p.base, number(ExactNumber::rational(p.exponent))));
  return mul(std::move(content));
}
// t = m^k for a monomial m (positive rational coefficient with an exact k-th
// root, every factor at a multiple of k).
bool monomialRoot(const Expr& t, unsigned k, Expr& m) {
  ExactNumber c;
  Expr rest;
  splitTerm(t, c, rest);
  if (!c.isRational() || c.rationalValue().sign() <= 0) return false;
  const ExactNumber rc = c.root(k);
  if (!rc.isRational()) return false;
  Args out{number(rc)};
  Args factors = rest->kind == Kind::Mul ? rest->args : Args{rest};
  if (one(rest)) factors.clear();
  for (const auto& f : factors) {
    Expr b, e;
    splitPower(f, b, e);
    Rational q;
    if (!rationalValue(e, q) || !q.integer()) return false;
    const BigInt& n = q.num();
    if (n.modSmall(k) || n.sign() <= 0) return false;
    out.push_back(pow(b, number(ExactNumber::integer(n / BigInt(static_cast<int64_t>(k))))));
  }
  m = mul(std::move(out));
  return true;
}
Expr factorPolynomial(const Expr& e, const Expr& x, unsigned depth);
// a^2 - b^2, a^3 +- b^3 and a^2 +- 2ab + b^2 over monomials a, b.
bool specialForms(const Expr& sum, const Expr& x, unsigned depth, Expr& out) {
  const Args& t = sum->args;
  if (t.size() == 2) {
    for (int i = 0; i < 2; ++i) {
      const Expr& p = t[i];
      const Expr q = neg(t[1 - i]);
      Expr a, b;
      if (!negative(p) && monomialRoot(p, 2, a) && monomialRoot(q, 2, b)) {
        out = factored(Args{factorPolynomial(sub(a, b), x, depth + 1), factorPolynomial(add(a, b), x, depth + 1)});
        return true;
      }
    }
    for (int sign = 1; sign >= -1; sign -= 2) {  // a^3 + b^3, then a^3 - b^3
      Expr a, b;
      const Expr second = sign > 0 ? t[1] : neg(t[1]);
      if (monomialRoot(t[0], 3, a) && monomialRoot(second, 3, b)) {
        const Expr bs = sign > 0 ? b : neg(b);
        out = factored(Args{add(a, bs), add(Args{pow(a, 2), neg(mul(a, bs)), pow(b, 2)})});
        return true;
      }
    }
  }
  if (t.size() == 3) {
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        if (i == j) continue;
        const int k = 3 - i - j;
        Expr a, b;
        if (!monomialRoot(t[i], 2, a) || !monomialRoot(t[j], 2, b)) continue;
        const Expr twice = mul(integer(2), mul(a, b));
        if (equal(t[k], twice)) { out = factored(Args{pow(add(a, b), 2)}); return true; }
        if (equal(t[k], neg(twice))) { out = factored(Args{pow(sub(a, b), 2)}); return true; }
      }
  }
  return false;
}
// A primitive integer-coefficient version of a rational polynomial factor,
// with the scale it was multiplied by.
Poly primitive(const Poly& p, ExactNumber& scale) {
  BigInt l(1);
  for (const auto& k : p.c) {
    const BigInt& d = k.rationalValue().den();
    l = l / BigInt::gcd(l, d) * d;
  }
  BigInt g(0);
  for (const auto& k : p.c) g = BigInt::gcd(g, k.rationalValue().num() * (l / k.rationalValue().den()));
  scale = ExactNumber::rational(Rational::of(l, g.zero() ? BigInt(1) : g));
  return polyScale(p, scale);
}
// Rational factorization of a univariate polynomial: lead * prod f_i^m_i with
// primitive integer factors.
bool factorUnivariate(const Poly& p, const Expr& x, Expr& out) {
  std::vector<Poly> parts;
  if (!squareFree(p, parts)) return false;
  ExactNumber lead = p.lead();
  Args factors;
  for (size_t k = 0; k < parts.size(); ++k) {
    Poly part = parts[k];
    if (part.degree() < 1) continue;
    const int64_t multiplicity = static_cast<int64_t>(k + 1);
    std::vector<Poly> pieces;
    std::vector<Rational> roots;
    if (rationalPoly(part) && rationalRoots(part, roots)) {
      for (const auto& r : roots) {
        Poly linear;
        linear.c = {ExactNumber::rational(-r), ExactNumber::integer(1)};
        pieces.push_back(linear);
        part = deflate(part, ExactNumber::rational(r));
      }
    }
    if (part.degree() >= 1) pieces.push_back(part);
    for (const auto& piece : pieces) {
      Poly f = piece;
      if (rationalPoly(piece)) {
        ExactNumber scale;
        f = primitive(piece, scale);
        lead = lead / scale.pow(ExactNumber::integer(multiplicity));
      }
      factors.push_back(pow(fromPoly(f, x), multiplicity));
    }
  }
  if (!context().ok() || !lead.exact()) return false;
  factors.insert(factors.begin(), number(lead));
  out = factored(std::move(factors));
  return true;
}
Expr factorPolynomial(const Expr& e, const Expr& x, unsigned depth) {
  const Expr ex = expand(e, false);
  if (ex->kind != Kind::Add || depth > 6 || !context().ok()) return ex;
  const Expr content = commonFactor(ex);
  Expr rest = expand(mul(ex, pow(content, -1)), false);
  Args factors{content};
  Expr v = x;
  if (freeOf(rest, x)) {
    std::vector<std::string> names;
    collectSymbols(rest, names);
    if (!names.empty()) v = symbol(names.front());
  }
  Poly p;
  Expr f;
  if (rest->kind == Kind::Add && toPoly(rest, v, p) && rationalPoly(p) && p.degree() >= 2 && factorUnivariate(p, v, f)) {
    factors.push_back(f);
  } else if (rest->kind == Kind::Add && specialForms(rest, x, depth, f)) {
    factors.push_back(f);
  } else {
    factors.push_back(rest);
  }
  return factored(std::move(factors));
}

// ---- Identities --------------------------------------------------------------
// Rules the simplifier applies anywhere in a tree.
bool oppositePowers(const Bindings& b) {
  return equal(neg(b.slot[2]), b.slot[3]) && isNumber(b.slot[2]) && b.slot[2]->number.sign() > 0;
}
const std::vector<Rule>& identityRules() {
  static const std::vector<Rule> rules = buildTable([] {
    const Expr X = wild(0), R = wild(1), N = wild(2, Constraint::Number), M = wild(3, Constraint::Number);
    std::vector<Rule> r;
    // sin^n/cos^n = tan^n (and the hyperbolic twin); cos/sin = 1/tan.
    r.push_back({mul(Args{R, pow(fn(Fn::Sin, X), N), pow(fn(Fn::Cos, X), M)}), mul(R, pow(fn(Fn::Tan, X), N)), nullptr,
                 oppositePowers});
    r.push_back({mul(Args{R, pow(fn(Fn::Sinh, X), N), pow(fn(Fn::Cosh, X), M)}), mul(R, pow(fn(Fn::Tanh, X), N)),
                 nullptr, oppositePowers});
    r.push_back({mul(Args{R, pow(fn(Fn::Cos, X), N), pow(fn(Fn::Sin, X), M)}), mul(R, pow(fn(Fn::Tan, X), M)), nullptr,
                 oppositePowers});
    return r;
  });
  return rules;
}
struct Square { Fn fn; Expr arg; };
bool squaredTrig(const Expr& f, Square& s) {
  int64_t n = 0;
  if (f->kind != Kind::Pow || !integerValue(f->args[1], n) || n != 2 || f->args[0]->kind != Kind::Func) return false;
  const Fn g = f->args[0]->fn;
  if (g != Fn::Sin && g != Fn::Cos && g != Fn::Sinh && g != Fn::Cosh) return false;
  s = {g, f->args[0]->args[0]};
  return true;
}
Expr squareOf(Fn f, const Expr& u) { return pow(fn(f, u), 2); }

// What simplify minimizes: node count first, then the number of fractional
// constants, so (sqrt(b^2-4c)-b)/2 beats -b/2+sqrt(b^2-4c)/2 at equal size.
uint32_t fractions(const Expr& e) {
  uint32_t n = isNumber(e) && !e->number.isInteger() ? 1 : 0;
  for (const auto& a : e->args) n += fractions(a);
  return n;
}
uint32_t complexity(const Expr& e) { return e->size * 8 + std::min<uint32_t>(fractions(e), 7); }

// ---- Zero testing --------------------------------------------------------------
// Rewrites for deciding zero, not for display: tan and multiple angles in
// terms of sin/cos of the plain argument, hyperbolics as exponentials, and
// even powers of cos as powers of sin.
Expr trigNormal(const Expr& e);
Expr angleSum(bool sine, const Expr& a, const Expr& b) {
  const Expr sa = trigNormal(fn(Fn::Sin, a)), ca = trigNormal(fn(Fn::Cos, a));
  const Expr sb = trigNormal(fn(Fn::Sin, b)), cb = trigNormal(fn(Fn::Cos, b));
  return sine ? add(mul(sa, cb), mul(ca, sb)) : sub(mul(ca, cb), mul(sa, sb));
}
Expr trigNormal(const Expr& e) {
  if (!context().ok()) return e;
  Expr t = mapArgs(e, trigNormal);
  if (t->kind == Kind::Func) {
    const Expr& u = t->args[0];
    switch (t->fn) {
      case Fn::Tan: return div(trigNormal(fn(Fn::Sin, u)), trigNormal(fn(Fn::Cos, u)));
      case Fn::Tanh: return div(sub(exp(u), exp(neg(u))), add(exp(u), exp(neg(u))));
      case Fn::Sinh: return mul(fraction(1, 2), sub(exp(u), exp(neg(u))));
      case Fn::Cosh: return mul(fraction(1, 2), add(exp(u), exp(neg(u))));
      case Fn::Sin:
      case Fn::Cos: {
        const bool sine = t->fn == Fn::Sin;
        int64_t k = 0;
        if (u->kind == Kind::Add && u->args.size() >= 2)
          return angleSum(sine, u->args[0], add(Args(u->args.begin() + 1, u->args.end())));
        if (u->kind == Kind::Mul && integerValue(u->args[0], k) && k >= 2 && k <= 8) {
          const Expr v = div(u, u->args[0]);
          return angleSum(sine, mul(integer(k - 1), v), v);
        }
        return t;
      }
      default: return t;
    }
  }
  int64_t n = 0;
  if (t->kind == Kind::Pow && t->args[0]->kind == Kind::Func && t->args[0]->fn == Fn::Cos && integerValue(t->args[1], n) &&
      n >= 2 && n <= 16) {
    const Expr& u = t->args[0]->args[0];
    return mul(pow(t->args[0], n % 2), pow(sub(integer(1), pow(fn(Fn::Sin, u), 2)), n / 2));
  }
  return t;
}
bool zeroNumerator(const Expr& e) {
  Expr n, d;
  numerDenom(e, n, d);
  return zero(pythagorean(expand(n)));
}
// Exact values at sample points, in a sandbox: a sample outside the domain
// (ln of a negative) only skips that sample.
Zero sampleTest(const Expr& e) {
  std::vector<std::string> names;
  collectSymbols(e, names);
  static const int64_t rationals[][2] = {{1, 3}, {5, 7}, {11, 5}, {-4, 7}, {3, 2}, {2, 9}};
  static const int64_t angles[][2] = {{1, 6}, {1, 4}, {1, 3}, {2, 3}};  // multiples of pi
  constexpr size_t kR = sizeof(rationals) / sizeof(rationals[0]), kA = sizeof(angles) / sizeof(angles[0]);
  for (size_t attempt = 0; attempt < kR + kA; ++attempt) {
    if (!context().step()) return Zero::Unknown;
    Context sandbox({}, {}, 60000);
    Scope scope(sandbox);
    Expr v = e;
    for (size_t i = 0; i < names.size(); ++i) {
      const size_t k = attempt + i;
      const Expr value = attempt < kR ? fraction(rationals[k % kR][0], rationals[k % kR][1])
                                      : mul(fraction(angles[k % kA][0], angles[k % kA][1]), pi());
      v = substitute(v, symbol(names[i]), value);
    }
    v = fold(v);
    if (sandbox.ok() && isNumber(v) && !v->number.zero()) return Zero::No;
  }
  return Zero::Unknown;
}
}  // namespace

// ---- Expansion ------------------------------------------------------------------
Expr expand(const Expr& e, bool exponentials) {
  if (!context().step() || e->args.empty()) return e;
  Args a;
  a.reserve(e->args.size());
  for (const auto& x : e->args) a.push_back(expand(x, exponentials));
  switch (e->kind) {
    case Kind::Mul: {
      Expr acc = integer(1);
      for (const auto& f : a) acc = distribute(acc, f);
      return acc;
    }
    case Kind::Pow: {
      int64_t n = 0;
      if (a[0]->kind == Kind::Add && integerValue(a[1], n) && n != 0 && n >= -32 && n <= 32) {
        Expr acc = a[0];
        for (int64_t i = 1; i < (n < 0 ? -n : n); ++i) acc = distribute(acc, a[0]);
        return n > 0 ? acc : pow(acc, -1);
      }
      // e^(a+b) = e^a*e^b, so exponentials in different forms meet.
      if (exponentials && isNumber(a[0]) && a[1]->kind == Kind::Add) {
        Args f;
        for (const auto& t : a[1]->args) f.push_back(pow(a[0], t));
        return mul(std::move(f));
      }
      return pow(a[0], a[1]);
    }
    default: return rebuild(e, std::move(a));
  }
}

// ---- Rational normalization -----------------------------------------------------
void numerDenom(const Expr& e, Expr& n, Expr& d) {
  if (!context().step()) { n = e; d = integer(1); return; }
  switch (e->kind) {
    case Kind::Number:
      if (e->number.isRational()) {
        n = number(ExactNumber::integer(e->number.rationalValue().num()));
        d = number(ExactNumber::integer(e->number.rationalValue().den()));
      } else {
        n = e;
        d = integer(1);
      }
      return;
    case Kind::Pow: {
      const Expr& x = e->args[1];
      int64_t k = 0;
      if (negative(x)) {
        Expr bn, bd;
        numerDenom(e->args[0], bn, bd);
        const Expr m = neg(x);
        n = pow(bd, m);
        d = pow(bn, m);
        return;
      }
      if (integerValue(x, k) && k > 0) {
        Expr bn, bd;
        numerDenom(e->args[0], bn, bd);
        n = pow(bn, x);
        d = pow(bd, x);
        return;
      }
      n = e;
      d = integer(1);
      return;
    }
    case Kind::Mul: {
      Args ns, ds;
      for (const auto& f : e->args) {
        Expr a, b;
        numerDenom(f, a, b);
        ns.push_back(a);
        ds.push_back(b);
      }
      n = mul(std::move(ns));
      d = mul(std::move(ds));
      return;
    }
    case Kind::Add: {
      std::vector<Expr> ns, ds;
      for (const auto& t : e->args) {
        Expr a, b;
        numerDenom(t, a, b);
        ns.push_back(a);
        ds.push_back(b);
      }
      // Least common denominator: lcm of the integer parts, each base at its
      // highest power.
      BigInt l(1);
      struct Power { Expr base; Rational exponent; };
      std::vector<Power> powers;
      for (const auto& den : ds) {
        ExactNumber c;
        Expr rest;
        splitTerm(den, c, rest);
        Args factors;
        if (c.isInteger()) {
          const BigInt& v = c.rationalValue().num().abs();
          l = l / BigInt::gcd(l, v) * v;
        } else {
          factors.push_back(number(c));
        }
        if (!one(rest)) {
          if (rest->kind == Kind::Mul) factors.insert(factors.end(), rest->args.begin(), rest->args.end());
          else factors.push_back(rest);
        }
        for (const auto& f : factors) {
          Expr b, x;
          splitPower(f, b, x);
          Rational q;
          if (!rationalValue(x, q)) { b = f; q = Rational(1); }
          auto it = std::find_if(powers.begin(), powers.end(), [&](const Power& p) { return equal(p.base, b); });
          if (it == powers.end()) powers.push_back({b, q});
          else if (Rational::compare(q, it->exponent) > 0) it->exponent = q;
        }
      }
      Args lcd{number(ExactNumber::integer(l))};
      for (const auto& p : powers) lcd.push_back(pow(p.base, number(ExactNumber::rational(p.exponent))));
      d = mul(std::move(lcd));
      Args terms;
      for (size_t i = 0; i < ns.size(); ++i) terms.push_back(mul(Args{ns[i], d, pow(ds[i], -1)}));
      n = add(std::move(terms));
      return;
    }
    default:
      n = e;
      d = integer(1);
  }
}
Expr together(const Expr& e) {
  Expr n, d;
  numerDenom(e, n, d);
  return one(d) ? n : mul(n, pow(d, -1));
}

Expr cancel(const Expr& e, const Expr& x) {
  Expr n, d;
  numerDenom(e, n, d);
  if (one(d)) return e;
  Poly pn, pd, g, q, r;
  if (!toPoly(n, x, pn) || !toPoly(d, x, pd) || pd.zero() || !polyGcd(pn, pd, g)) {
    if (!context().ok()) return e;
    return mul(factor(n, x), pow(factor(d, x), -1));
  }
  if (g.degree() > 0) {
    if (!polyDivMod(pn, g, q, r)) return e;
    pn = q;
    if (!polyDivMod(pd, g, q, r)) return e;
    pd = q;
  }
  const ExactNumber lead = pd.lead();
  const ExactNumber inverse = ExactNumber::integer(1) / lead;
  if (!inverse.exact()) return e;
  pn = polyScale(pn, inverse);
  pd = polyScale(pd, inverse);
  return mul(fromPoly(pn, x), pow(fromPoly(pd, x), -1));
}

Expr factor(const Expr& e, const Expr& x) {
  if (e->kind == Kind::Equation) return equation(factor(e->args[0], x), factor(e->args[1], x));
  Expr n, d;
  numerDenom(e, n, d);
  const Expr top = factorPolynomial(n, x, 0);
  if (one(d)) return top;
  return factored(Args{top, pow(factorPolynomial(d, x, 0), -1)});
}

// ---- Identities -------------------------------------------------------------------
Expr pythagorean(const Expr& e) {
  if (e->kind != Kind::Add) return e;
  Args t = e->args;
  for (unsigned round = 0; round < 8 && context().ok(); ++round) {
    bool changed = false;
    for (size_t i = 0; i < t.size() && !changed; ++i) {
      const Args factors = t[i]->kind == Kind::Mul ? t[i]->args : Args{t[i]};
      for (const auto& f : factors) {
        Square s;
        if (!squaredTrig(f, s)) continue;
        const Expr r = div(t[i], f);
        // (partner term, replacement for both)
        std::pair<Expr, Expr> forms[2];
        switch (s.fn) {
          case Fn::Sin: forms[0] = {mul(r, squareOf(Fn::Cos, s.arg)), r}; forms[1] = {neg(r), neg(mul(r, squareOf(Fn::Cos, s.arg)))}; break;
          case Fn::Cos: forms[0] = {mul(r, squareOf(Fn::Sin, s.arg)), r}; forms[1] = {neg(r), neg(mul(r, squareOf(Fn::Sin, s.arg)))}; break;
          case Fn::Sinh: forms[0] = {neg(mul(r, squareOf(Fn::Cosh, s.arg))), neg(r)}; forms[1] = {r, mul(r, squareOf(Fn::Cosh, s.arg))}; break;
          default: forms[0] = {neg(mul(r, squareOf(Fn::Sinh, s.arg))), r}; forms[1] = {neg(r), mul(r, squareOf(Fn::Sinh, s.arg))}; break;
        }
        for (const auto& form : forms) {
          for (size_t j = 0; j < t.size(); ++j) {
            if (j == i || !equal(t[j], form.first)) continue;
            const Expr replacement = form.second;
            t.erase(t.begin() + static_cast<long>(std::max(i, j)));
            t.erase(t.begin() + static_cast<long>(std::min(i, j)));
            t.push_back(replacement);
            changed = true;
            break;
          }
          if (changed) break;
        }
        if (changed) break;
      }
    }
    if (!changed) break;
  }
  return add(std::move(t));
}

Expr simplify(const Expr& e, const Expr& x) {
  if (!context().ok()) return e;
  const Expr s = mapArgs(e, [&](const Expr& a) { return simplify(a, x); });
  if (s->kind == Kind::Equation || s->kind == Kind::List || s->kind == Kind::Integral || s->kind == Kind::Solve ||
      s->args.empty())
    return s;
  Expr best = pythagorean(rewrite(s, identityRules(), x));
  uint32_t bestScore = complexity(best);
  auto consider = [&](const Expr& c) {
    if (!context().ok() || !c) return;
    const uint32_t score = complexity(c);
    if (score < bestScore) { best = c; bestScore = score; }
  };
  if (s->size <= kSimplifyCandidates) {
    const Expr expanded = expand(s, false);
    consider(pythagorean(rewrite(expanded, identityRules(), x)));
    Expr n, d;
    numerDenom(s, n, d);
    if (!one(d)) {
      consider(cancel(s, x));
      consider(cancel(expanded, x));
    }
    if (s->kind == Kind::Add && s->size <= 80) consider(factor(s, x));
  }
  return context().ok() ? best : e;
}

Expr fold(const Expr& e) {
  const Expr t = mapArgs(e, fold);
  if (t->kind == Kind::Func && t->fn == Fn::Ln && isNumber(t->args[0])) {
    bool certain = false;
    if (t->args[0]->number.sign(&certain) > 0 && certain) {
      const ExactNumber r = t->args[0]->number.ln();
      if (r.exact()) return number(r);
    }
  }
  return t;
}

Zero zeroTest(const Expr& e) {
  if (zero(e)) return Zero::Yes;
  const Expr f = fold(e);
  if (zero(f) || zero(expand(f)) || zeroNumerator(expand(f))) return Zero::Yes;
  if (!context().ok()) return Zero::Unknown;
  if (zeroNumerator(expand(trigNormal(f)))) return Zero::Yes;
  if (!context().ok()) return Zero::Unknown;
  return sampleTest(e);
}

}  // namespace Cas
}  // namespace Calculator
