#include "calculator/cas/calculator_cas_integrate.h"
#include "calculator/cas/calculator_cas_diff.h"
#include "calculator/cas/calculator_cas_partfrac.h"
#include "calculator/cas/calculator_cas_pattern.h"
#include "calculator/cas/calculator_cas_poly.h"
#include "calculator/cas/calculator_cas_simplify.h"
#include "calculator/cas/calculator_cas_solve.h"
#include <algorithm>

namespace Calculator {
namespace Cas {
namespace {
constexpr unsigned kMaxDepth = 8;  // nested strategies (parts of x^n e^x uses n)
constexpr size_t kMaxKernels = 12;
enum : uint8_t { kX = 0, kN = 1, kA = 2, kB = 3, kY = 4 };

Expr antiderivative(const Expr& f, const Expr& x, unsigned depth);

// Runs one strategy. A certain math error inside a speculative attempt (a
// substitution that divides by zero somewhere) abandons only that attempt.
template <class F>
bool guarded(F attempt) {
  Context& c = context();
  if (!c.ok()) return false;
  const bool found = attempt();
  if (c.error() == Error::Math) {
    c.recover();
    return false;
  }
  return found && c.ok();
}
bool positiveNumber(const Expr& e) {
  bool certain = false;
  return isNumber(e) && e->number.sign(&certain) > 0 && certain;
}
bool negativeNumber(const Expr& e) {
  bool certain = false;
  return isNumber(e) && e->number.sign(&certain) < 0 && certain;
}

// ---- The table -----------------------------------------------------------------
// Each rule's pattern is over a linear X (see calculator_cas_pattern.h) and
// its result is the antiderivative in X; `linear` rules are then divided by
// dX/dx. Builders cover results a template cannot express.
struct TableRule {
  Rule rule;
  bool linear;
};
bool notMinusOne(const Bindings& b) { return !(isNumber(b.slot[kN]) && b.slot[kN]->number == ExactNumber::integer(-1)); }
bool notNegativeBase(const Bindings& b) { return !negativeNumber(b.slot[kA]); }
// 1/sqrt(A + B X^2): asin for B < 0 < A, a logarithm for B > 0.
Expr inverseRoot(const Expr& A, const Expr& B, const Expr& X) {
  if (!isNumber(A) || !isNumber(B) || zero(A)) return nullptr;
  if (positiveNumber(B)) {
    const Expr rb = sqrt(B);
    return div(fn(Fn::Ln, fn(Fn::Abs, add(mul(rb, X), sqrt(add(A, mul(B, pow(X, 2))))))), rb);
  }
  if (negativeNumber(B) && positiveNumber(A)) return div(fn(Fn::Asin, mul(sqrt(div(neg(B), A)), X)), sqrt(neg(B)));
  return nullptr;
}
Expr buildInverseRoot(const Bindings& b) { return inverseRoot(b.slot[kA], b.slot[kB], b.slot[kX]); }
// sqrt(A + B X^2) = X/2 sqrt(A + B X^2) + A/2 * integral of 1/sqrt(A + B X^2).
Expr buildRoot(const Bindings& b) {
  const Expr inner = inverseRoot(b.slot[kA], b.slot[kB], b.slot[kX]);
  if (!inner) return nullptr;
  const Expr& X = b.slot[kX];
  return add(mul(Args{fraction(1, 2), X, sqrt(add(b.slot[kA], mul(b.slot[kB], pow(X, 2))))}),
             mul(Args{fraction(1, 2), b.slot[kA], inner}));
}
// A^U * sin(V) and A^U * cos(V) with U, V linear: with p = U' ln A and q = V',
// the integral is A^U (p sin V - q cos V)/(p^2+q^2), resp. A^U (p cos V + q sin V)/(p^2+q^2).
Expr expTrig(const Bindings& b, bool sine) {
  Expr au, bu, av, bv;
  if (!linear(b.slot[kX], b.var, au, bu) || !linear(b.slot[kY], b.var, av, bv)) return nullptr;
  const Expr p = mul(au, fn(Fn::Ln, b.slot[kA])), q = av;
  const Expr denominator = add(pow(p, 2), pow(q, 2));
  if (zero(denominator)) return nullptr;
  const Expr s = fn(Fn::Sin, b.slot[kY]), c = fn(Fn::Cos, b.slot[kY]);
  const Expr body = sine ? sub(mul(p, s), mul(q, c)) : add(mul(p, c), mul(q, s));
  return mul(Args{pow(b.slot[kA], b.slot[kX]), body, pow(denominator, -1)});
}
Expr buildExpSin(const Bindings& b) { return expTrig(b, true); }
Expr buildExpCos(const Bindings& b) { return expTrig(b, false); }

const std::vector<TableRule>& table() {
  static const std::vector<TableRule> rules = buildTable([] {
    const Expr X = wild(kX, Constraint::Linear), N = wild(kN, Constraint::Const), A = wild(kA, Constraint::Const),
               B = wild(kB, Constraint::Const), Y = wild(kY, Constraint::Linear);
    const Expr half = fraction(1, 2), quarter = fraction(1, 4);
    std::vector<TableRule> t;
    auto rule = [&](const Expr& p, const Expr& r, bool (*when)(const Bindings&) = nullptr) {
      t.push_back({Rule{p, r, nullptr, when}, true});
    };
    auto built = [&](const Expr& p, Expr (*build)(const Bindings&), bool linearRule) {
      t.push_back({Rule{p, nullptr, build, nullptr}, linearRule});
    };
    auto f = [](Fn g, const Expr& u) { return fn(g, u); };
    const Expr sqrtOneMinus = sqrt(sub(integer(1), pow(X, 2)));
    // Powers and exponentials.
    rule(pow(X, -1), f(Fn::Ln, f(Fn::Abs, X)));
    rule(pow(X, N), div(pow(X, add(N, integer(1))), add(N, integer(1))), notMinusOne);
    rule(pow(A, X), div(pow(A, X), f(Fn::Ln, A)), notNegativeBase);
    // Functions of X.
    rule(f(Fn::Sin, X), neg(f(Fn::Cos, X)));
    rule(f(Fn::Cos, X), f(Fn::Sin, X));
    rule(f(Fn::Tan, X), neg(f(Fn::Ln, f(Fn::Abs, f(Fn::Cos, X)))));
    rule(f(Fn::Sinh, X), f(Fn::Cosh, X));
    rule(f(Fn::Cosh, X), f(Fn::Sinh, X));
    rule(f(Fn::Tanh, X), f(Fn::Ln, f(Fn::Cosh, X)));
    rule(f(Fn::Ln, X), sub(mul(X, f(Fn::Ln, X)), X));
    rule(f(Fn::Asin, X), add(mul(X, f(Fn::Asin, X)), sqrtOneMinus));
    rule(f(Fn::Acos, X), sub(mul(X, f(Fn::Acos, X)), sqrtOneMinus));
    rule(f(Fn::Atan, X), sub(mul(X, f(Fn::Atan, X)), mul(half, f(Fn::Ln, add(integer(1), pow(X, 2))))));
    rule(f(Fn::Asinh, X), sub(mul(X, f(Fn::Asinh, X)), sqrt(add(pow(X, 2), integer(1)))));
    rule(f(Fn::Acosh, X), sub(mul(X, f(Fn::Acosh, X)), sqrt(sub(pow(X, 2), integer(1)))));
    rule(f(Fn::Atanh, X), add(mul(X, f(Fn::Atanh, X)), mul(half, f(Fn::Ln, sub(integer(1), pow(X, 2))))));
    rule(f(Fn::Abs, X), mul(Args{half, X, f(Fn::Abs, X)}));
    // Squares and reciprocals of trigonometric and hyperbolic functions.
    rule(pow(f(Fn::Cos, X), -2), f(Fn::Tan, X));
    rule(pow(f(Fn::Sin, X), -2), neg(div(f(Fn::Cos, X), f(Fn::Sin, X))));
    rule(pow(f(Fn::Cosh, X), -2), f(Fn::Tanh, X));
    rule(pow(f(Fn::Sinh, X), -2), neg(div(f(Fn::Cosh, X), f(Fn::Sinh, X))));
    rule(pow(f(Fn::Cos, X), -1), f(Fn::Ln, f(Fn::Abs, div(add(integer(1), f(Fn::Sin, X)), f(Fn::Cos, X)))));
    rule(pow(f(Fn::Sin, X), -1), f(Fn::Ln, f(Fn::Abs, div(sub(integer(1), f(Fn::Cos, X)), f(Fn::Sin, X)))));
    rule(pow(f(Fn::Sin, X), 2), sub(mul(half, X), mul(quarter, f(Fn::Sin, mul(integer(2), X)))));
    rule(pow(f(Fn::Cos, X), 2), add(mul(half, X), mul(quarter, f(Fn::Sin, mul(integer(2), X)))));
    rule(pow(f(Fn::Tan, X), 2), sub(f(Fn::Tan, X), X));
    rule(pow(f(Fn::Sinh, X), 2), sub(mul(quarter, f(Fn::Sinh, mul(integer(2), X))), mul(half, X)));
    rule(pow(f(Fn::Cosh, X), 2), add(mul(quarter, f(Fn::Sinh, mul(integer(2), X))), mul(half, X)));
    // Square roots of quadratics A + B X^2.
    const Expr quadratic = add(A, mul(B, pow(X, 2)));
    built(pow(quadratic, fraction(-1, 2)), buildInverseRoot, true);
    built(pow(quadratic, half), buildRoot, true);
    // Exponential times sine/cosine (both arguments linear).
    built(mul(pow(A, X), f(Fn::Sin, Y)), buildExpSin, false);
    built(mul(pow(A, X), f(Fn::Cos, Y)), buildExpCos, false);
    return t;
  });
  return rules;
}
bool tableIntegral(const Expr& f, const Expr& x, Expr& out) {
  for (const auto& t : table()) {
    Bindings b;
    b.var = x;
    if (!match(t.rule.pattern, f, b) || (t.rule.when && !t.rule.when(b))) continue;
    Expr r = t.rule.build ? t.rule.build(b) : instantiate(t.rule.replacement, b);
    if (!r || !context().ok()) continue;
    if (t.linear) {
      Expr a, c;
      if (!linear(b.slot[kX], x, a, c)) continue;
      r = div(r, a);
    }
    out = r;
    return true;
  }
  return false;
}

// ---- Power reduction --------------------------------------------------------------
// sin^n, cos^n, tan^n (n >= 3) of a linear argument, one reduction step:
//   sin^n = -sin^(n-1) cos/(n a) + (n-1)/n * int sin^(n-2)
//   cos^n =  cos^(n-1) sin/(n a) + (n-1)/n * int cos^(n-2)
//   tan^n =  tan^(n-1)/((n-1) a) - int tan^(n-2)
bool powerReduction(const Expr& f, const Expr& x, unsigned depth, Expr& out) {
  int64_t n = 0;
  if (f->kind != Kind::Pow || !integerValue(f->args[1], n) || n < 3 || n > 24 || f->args[0]->kind != Kind::Func) return false;
  const Fn g = f->args[0]->fn;
  if (g != Fn::Sin && g != Fn::Cos && g != Fn::Tan) return false;
  const Expr& u = f->args[0]->args[0];
  Expr a, b;
  if (!linear(u, x, a, b)) return false;
  const Expr rest = antiderivative(pow(f->args[0], n - 2), x, depth + 1);
  if (!rest) return false;
  const Expr k = integer(n);
  if (g == Fn::Tan) {
    out = sub(div(pow(f->args[0], n - 1), mul(integer(n - 1), a)), rest);
    return true;
  }
  const Expr s = fn(Fn::Sin, u), c = fn(Fn::Cos, u);
  const Expr lead = g == Fn::Sin ? neg(mul(pow(s, n - 1), c)) : mul(pow(c, n - 1), s);
  out = add(div(lead, mul(k, a)), mul(fraction(n - 1, n), rest));
  return true;
}

// ---- Rational functions -----------------------------------------------------------
// Integral of E/(t^2+a^2)^k over x, t = x + p/2, a^2 = q - p^2/4 (a2), as the
// usual recursion J_k = t/(2a^2(k-1)(t^2+a^2)^(k-1)) + (2k-3)/(2a^2(k-1)) J_(k-1).
Expr quadraticPower(const Expr& quadratic, const Expr& t, const Expr& a2, int k) {
  if (k == 1) {
    const Expr a = sqrt(a2);
    return div(fn(Fn::Atan, div(t, a)), a);
  }
  const Expr scale = mul(Args{integer(2), a2, integer(k - 1)});
  return add(div(t, mul(scale, pow(quadratic, k - 1))),
             mul(div(integer(2 * k - 3), scale), quadraticPower(quadratic, t, a2, k - 1)));
}
ExactNumber coefficient(const Poly& p, size_t i) { return i < p.c.size() ? p.c[i] : ExactNumber::integer(0); }
// Term by term over the partial fractions (calculator_cas_partfrac.h).
bool rationalIntegral(const Expr& f, const Expr& x, Expr& out) {
  Decomposition pf;
  if (!partialFractions(f, x, pf)) return false;
  Args result;
  const Poly& q = pf.quotient;
  for (size_t i = 0; i < q.c.size(); ++i)
    result.push_back(mul(number(q.c[i] / ExactNumber::integer(static_cast<int64_t>(i + 1))), pow(x, static_cast<int64_t>(i + 1))));
  for (const auto& term : pf.terms) {
    const Expr fx = fromPoly(term.factor, x);
    if (term.factor.degree() == 1) {
      const Expr A = number(coefficient(term.numerator, 0));
      result.push_back(term.power == 1 ? mul(A, fn(Fn::Ln, fn(Fn::Abs, fx)))
                                       : mul(div(A, integer(1 - term.power)), pow(fx, 1 - term.power)));
      continue;
    }
    const Expr B = number(coefficient(term.numerator, 1)), C = number(coefficient(term.numerator, 0));
    const Expr p = number(term.factor.c[1]), qq = number(term.factor.c[0]);
    const Expr halfP = mul(fraction(1, 2), p);
    const Expr t = add(x, halfP), a2 = sub(qq, pow(halfP, 2));
    const Expr E = sub(C, mul(B, halfP));
    result.push_back(term.power == 1 ? mul(Args{fraction(1, 2), B, fn(Fn::Ln, fx)})
                                     : mul(div(B, integer(2 * (1 - term.power))), pow(fx, 1 - term.power)));
    if (!zero(E)) result.push_back(mul(E, quadraticPower(fx, t, a2, term.power)));
  }
  if (!context().ok()) return false;
  out = add(std::move(result));
  return true;
}

// ---- Substitution --------------------------------------------------------------------
void kernels(const Expr& e, const Expr& x, Args& out) {
  if (freeOf(e, x) || equal(e, x)) return;
  auto push = [&](const Expr& k) {
    if (freeOf(k, x) || equal(k, x)) return;
    for (const auto& o : out)
      if (equal(o, k)) return;
    out.push_back(k);
  };
  if (e->kind == Kind::Func) { push(e); push(e->args[0]); }
  if (e->kind == Kind::Pow) {
    if (isNumber(e->args[0])) push(e);
    else push(e->args[0]);
    if (!isNumber(e->args[1])) push(e->args[1]);
  }
  for (const auto& a : e->args) kernels(a, x, out);
}
// Replaces u by t, also where u appears only implicitly: B^(k*v) = (B^v)^k
// for an exponential kernel u = B^v.
Expr replaceKernel(const Expr& e, const Expr& u, const Expr& t) {
  if (equal(e, u)) return t;
  if (u->kind == Kind::Pow && isNumber(u->args[0]) && e->kind == Kind::Pow && equal(e->args[0], u->args[0])) {
    const Expr ratio = div(e->args[1], u->args[1]);
    if (isNumber(ratio)) return pow(t, ratio);
  }
  return mapArgs(e, [&](const Expr& a) { return replaceKernel(a, u, t); });
}
// Even powers of the partner function in terms of the kernel:
// u = cos v turns sin^2k v into (1-cos^2 v)^k, and u = sin v the reverse.
Expr pythagoreanTo(const Expr& e, const Expr& u) {
  if (u->kind != Kind::Func || (u->fn != Fn::Sin && u->fn != Fn::Cos)) return e;
  const Fn partner = u->fn == Fn::Sin ? Fn::Cos : Fn::Sin;
  const Expr t = mapArgs(e, [&](const Expr& a) { return pythagoreanTo(a, u); });
  int64_t n = 0;
  if (t->kind == Kind::Pow && t->args[0]->kind == Kind::Func && t->args[0]->fn == partner &&
      equal(t->args[0]->args[0], u->args[0]) && integerValue(t->args[1], n) && n % 2 == 0 && n != 0)
    return pow(sub(integer(1), pow(u, 2)), n / 2);
  return t;
}
bool substitution(const Expr& f, const Expr& x, unsigned depth, Expr& out) {
  if (depth + 1 >= kMaxDepth) return false;
  Args candidates;
  kernels(f, x, candidates);
  std::stable_sort(candidates.begin(), candidates.end(), [](const Expr& a, const Expr& b) { return a->size > b->size; });
  if (candidates.size() > kMaxKernels) candidates.resize(kMaxKernels);
  const Expr t = symbol("#t");
  for (const auto& u : candidates) {
    if (equal(u, f)) continue;
    Expr found;
    const bool ok = guarded([&] {
      const Expr du = derivative(u, x);
      if (zero(du)) return false;
      const Expr q = mul(f, pow(du, -1));
      const Expr forms[] = {q, pythagoreanTo(q, u), cancel(q, x)};
      for (const auto& form : forms) {
        const Expr g = replaceKernel(form, u, t);
        if (!freeOf(g, x)) continue;
        const Expr G = antiderivative(g, t, depth + 1);
        if (!G) continue;
        found = substitute(G, t, u);
        return true;
      }
      return false;
    });
    if (ok) { out = found; return true; }
    if (!context().ok()) return false;
  }
  return false;
}

// ---- Integration by parts ----------------------------------------------------------
// LIATE: which factor to differentiate. 5 logarithm, 4 inverse function,
// 3 algebraic (a power of x or a polynomial), 2 trigonometric/hyperbolic,
// 1 exponential, 0 other.
int liate(const Expr& f, const Expr& x) {
  Expr b, e;
  splitPower(f, b, e);
  int64_t n = 0;
  if (b->kind == Kind::Func && integerValue(e, n) && n > 0) {
    switch (b->fn) {
      case Fn::Ln: return 5;
      case Fn::Asin: case Fn::Acos: case Fn::Atan: case Fn::Asinh: case Fn::Acosh: case Fn::Atanh: return 4;
      case Fn::Abs: return 0;
      default: return 2;
    }
  }
  std::vector<Expr> c;
  if (integerValue(e, n) && n > 0 && coefficients(b, x, c, 16)) return 3;
  if (isNumber(b) && !freeOf(e, x)) return 1;
  return 0;
}
bool byParts(const Expr& f, const Expr& x, unsigned depth, Expr& out) {
  if (depth + 1 >= kMaxDepth) return false;
  const Args factors = f->kind == Kind::Mul ? f->args : Args{f};
  std::vector<std::pair<int, size_t>> order;
  bool transcendental = false;
  for (size_t i = 0; i < factors.size(); ++i) {
    const int c = liate(factors[i], x);
    if (c == 1 || c == 2) transcendental = true;
    if (c >= 3) order.push_back({c, i});
  }
  std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  for (const auto& o : order) {
    if (o.first == 3 && !transcendental) continue;  // a polynomial alone needs no parts
    if (factors.size() == 1 && o.first < 4) continue;
    Expr found;
    const bool ok = guarded([&] {
      const Expr u = factors[o.second];
      Args rest;
      for (size_t i = 0; i < factors.size(); ++i)
        if (i != o.second) rest.push_back(factors[i]);
      const Expr v = antiderivative(mul(std::move(rest)), x, depth + 1);
      if (!v) return false;
      const Expr w = antiderivative(expand(mul(v, derivative(u, x))), x, depth + 1);
      if (!w) return false;
      found = sub(mul(u, v), w);
      return true;
    });
    if (ok) { out = found; return true; }
    if (!context().ok()) return false;
  }
  return false;
}

// ---- Driver ----------------------------------------------------------------------------
Expr antiderivative(const Expr& f, const Expr& x, unsigned depth) {
  if (!context().step() || depth > kMaxDepth) return nullptr;
  if (freeOf(f, x)) return mul(f, x);
  if (f->kind == Kind::Add) {
    Args parts;
    for (const auto& t : f->args) {
      Expr F = antiderivative(t, x, depth);
      if (!F) return nullptr;
      parts.push_back(F);
    }
    return add(std::move(parts));
  }
  if (f->kind == Kind::Mul) {
    Args constant, rest;
    for (const auto& a : f->args) (freeOf(a, x) ? constant : rest).push_back(a);
    if (!constant.empty()) {
      const Expr F = antiderivative(mul(std::move(rest)), x, depth);
      return F ? mul(mul(std::move(constant)), F) : nullptr;
    }
  }
  Expr out;
  if (guarded([&] { return tableIntegral(f, x, out); })) return out;
  if (guarded([&] { return powerReduction(f, x, depth, out); })) return out;
  if (guarded([&] { return rationalIntegral(f, x, out); })) return out;
  if (substitution(f, x, depth, out)) return out;
  if (byParts(f, x, depth, out)) return out;
  if (!context().ok()) return nullptr;
  const Expr expanded = expand(f);
  if (!equal(expanded, f) && context().ok()) return antiderivative(expanded, x, depth + 1);
  return nullptr;
}

// One integrand: its antiderivative checked by differentiation, or null.
Expr attempt(const Expr& f, const Expr& x, bool& verified) {
  Expr F = antiderivative(f, x, 0);
  if (!F || !context().ok()) {
    context().recover();
    return nullptr;
  }
  F = simplify(F, x);
  Zero z = Zero::Unknown;
  guarded([&] {
    z = zeroTest(sub(derivative(F, x), f));
    return true;
  });
  if (!context().ok() || z == Zero::No) return nullptr;
  verified = z == Zero::Yes;
  return F;
}
}  // namespace

Expr integrate(const Expr& f, const Expr& x, bool* verified) {
  bool proved = false;
  Expr F = attempt(f, x, proved);
  if (!F && f->kind == Kind::Add && context().ok()) {
    // Integrate what can be; only the rest stays unevaluated.
    Args done;
    Args pending;
    proved = true;
    for (const auto& t : f->args) {
      bool termProved = false;
      const Expr G = attempt(t, x, termProved);
      if (G) {
        done.push_back(G);
        proved = proved && termProved;
      } else {
        pending.push_back(t);
      }
    }
    if (!done.empty() && context().ok()) {
      done.push_back(integral(add(std::move(pending)), x));
      F = add(std::move(done));
    }
  }
  if (!F) {
    proved = false;
    F = integral(f, x);
  }
  if (verified) *verified = proved;
  return F;
}

// ---- Definite integrals ---------------------------------------------------------------
namespace {
constexpr size_t kMaxPieces = 64;  // split points from |g| zeros

bool containsKind(const Expr& e, Kind k) {
  if (e->kind == k) return true;
  for (const auto& a : e->args)
    if (containsKind(a, k)) return true;
  return false;
}
// e at x = at in decimal, for signs only (|g| on a piece whose midpoint has no
// exact value, cos(1/2)). False for what it does not evaluate.
bool approximate(const Expr& e, const Expr& x, const Decimal& at, Decimal& out) {
  switch (e->kind) {
    case Kind::Number: out = e->number.toDecimal(); return out.finite();
    case Kind::Symbol: if (!equal(e, x)) return false; out = at; return true;
    case Kind::Add:
    case Kind::Mul: {
      out = e->kind == Kind::Add ? Decimal(0) : Decimal(1);
      for (const auto& a : e->args) {
        Decimal v;
        if (!approximate(a, x, at, v)) return false;
        out = e->kind == Kind::Add ? out + v : out * v;
      }
      return out.finite();
    }
    case Kind::Pow: {
      Decimal base, exponent;
      if (!approximate(e->args[0], x, at, base) || !approximate(e->args[1], x, at, exponent)) return false;
      int64_t n = 0;
      if (base.negative() && integerValue(e->args[1], n)) {
        out = base.abs().pow(exponent);
        if (n % 2) out = -out;
      } else {
        out = base.pow(exponent);
      }
      return out.finite();
    }
    case Kind::Func: {
      Decimal u;
      if (!approximate(e->args[0], x, at, u)) return false;
      switch (e->fn) {
        case Fn::Sin: out = u.sin(); break;
        case Fn::Cos: out = u.cos(); break;
        case Fn::Tan: out = u.sin() / u.cos(); break;
        case Fn::Atan: out = u.atan(); break;
        case Fn::Sinh: out = (u.exp() - (-u).exp()) / 2; break;
        case Fn::Cosh: out = (u.exp() + (-u).exp()) / 2; break;
        case Fn::Tanh: out = (u.exp() - (-u).exp()) / (u.exp() + (-u).exp()); break;
        case Fn::Ln: out = u.ln(); break;
        case Fn::Abs: out = u.abs(); break;
        default: return false;
      }
      return out.finite();
    }
    default: return false;
  }
}
// |g| kernels whose argument depends on x.
void absArguments(const Expr& e, const Expr& x, Args& out) {
  for (const auto& a : e->args) absArguments(a, x, out);
  if (e->kind != Kind::Func || e->fn != Fn::Abs || freeOf(e->args[0], x)) return;
  for (const auto& o : out)
    if (equal(o, e->args[0])) return;
  out.push_back(e->args[0]);
}
bool inside(const ExactNumber& r, const ExactNumber& a, const ExactNumber& b) {
  bool c1 = false, c2 = false;
  return ExactNumber::compare(r, a, &c1) > 0 && c1 && ExactNumber::compare(r, b, &c2) < 0 && c2;
}
// Zeros of g strictly inside (a, b), appended to `out`. Products split into
// factors; sin/cos of a linear argument enumerate every period (solve()
// returns principal branches only); anything else goes to solve().
bool zerosIn(const Expr& g, const Expr& x, const ExactNumber& a, const ExactNumber& b, std::vector<ExactNumber>& out) {
  if (freeOf(g, x)) return true;
  if (g->kind == Kind::Mul) {
    for (const auto& f : g->args)
      if (!zerosIn(f, x, a, b, out)) return false;
    return true;
  }
  int64_t n = 0;
  if (g->kind == Kind::Pow && integerValue(g->args[1], n) && n > 0) return zerosIn(g->args[0], x, a, b, out);
  Expr slope, offset;
  if (g->kind == Kind::Func && (g->fn == Fn::Sin || g->fn == Fn::Cos) && linear(g->args[0], x, slope, offset) &&
      isNumber(slope) && isNumber(offset)) {
    // u = slope*x + offset is a zero at u = k*pi (+ pi/2 for cos).
    const Expr shift = g->fn == Fn::Cos ? fraction(1, 2) : integer(0);
    const Decimal piValue = Decimal::pi();
    Decimal u0 = (slope->number * a + offset->number).toDecimal() / piValue - shift->number.toDecimal();
    Decimal u1 = (slope->number * b + offset->number).toDecimal() / piValue - shift->number.toDecimal();
    if (u1 < u0) std::swap(u0, u1);
    int64_t first = 0, last = 0;
    if (!(-(-u0).floor()).toInt(first) || !u1.floor().toInt(last) || last - first > static_cast<int64_t>(kMaxPieces))
      return false;
    for (int64_t k = first; k <= last; ++k) {
      const Expr root = div(sub(mul(add(integer(k), shift), pi()), offset), slope);
      if (!isNumber(root)) return false;
      if (inside(root->number, a, b)) out.push_back(root->number);
    }
    return context().ok();
  }
  const Expr s = solve(equation(g, integer(0)), x);
  if (!context().ok()) return false;
  if (s->kind == Kind::Text) return true;  // No Solution (Infinite Solution means g is 0: no split needed)
  if (s->kind != Kind::List) return false;
  for (const auto& item : s->args) {
    if (item->kind != Kind::Equation || !isNumber(item->args[1])) return false;
    if (inside(item->args[1]->number, a, b)) out.push_back(item->args[1]->number);
  }
  return true;
}
// Each |g| as +-g by g's sign at `mid`, innermost first.
Expr resolveAbs(const Expr& e, const Expr& x, const Decimal& mid, bool& ok) {
  const Expr t = mapArgs(e, [&](const Expr& a) { return resolveAbs(a, x, mid, ok); });
  if (!ok || t->kind != Kind::Func || t->fn != Fn::Abs || freeOf(t->args[0], x)) return t;
  Decimal v;
  if (!approximate(t->args[0], x, mid, v) || v.zero()) {
    ok = false;
    return t;
  }
  return v.negative() ? neg(t->args[0]) : t->args[0];
}
// F(right) - F(left), or null where F is not an exact number at either end.
Expr difference(const Expr& F, const Expr& x, const ExactNumber& left, const ExactNumber& right) {
  const Expr d = sub(substitute(F, x, number(right)), substitute(F, x, number(left)));
  return context().ok() && isNumber(d) ? d : nullptr;
}
}  // namespace

Expr definiteIntegral(const Expr& f, const Expr& x, const Expr& a, const Expr& b) {
  if (!isNumber(a) || !isNumber(b)) return nullptr;
  bool certain = false;
  const int order = ExactNumber::compare(a->number, b->number, &certain);
  if (!certain) return nullptr;
  if (order == 0) return integer(0);
  if (order > 0) {
    const Expr r = definiteIntegral(f, x, b, a);
    return r ? neg(r) : nullptr;
  }
  Args kernels;
  absArguments(f, x, kernels);
  std::vector<ExactNumber> points{a->number};
  for (const auto& g : kernels)
    if (!zerosIn(g, x, a->number, b->number, points) || points.size() > kMaxPieces) return nullptr;
  points.push_back(b->number);
  std::sort(points.begin(), points.end(),
            [](const ExactNumber& p, const ExactNumber& q) { return ExactNumber::compare(p, q) < 0; });
  Expr total = integer(0);
  for (size_t i = 0; i + 1 < points.size() && context().ok(); ++i) {
    if ((points[i + 1] - points[i]).zero()) continue;  // a zero shared by two kernels
    bool ok = true;
    const Decimal mid = ((points[i] + points[i + 1]) * ExactNumber::fraction(1, 2)).toDecimal();
    const Expr piece = resolveAbs(f, x, mid, ok);
    if (!ok || !context().ok()) return nullptr;
    const Expr F = integrate(piece, x);
    if (!context().ok() || containsKind(F, Kind::Integral)) return nullptr;
    const Expr d = difference(F, x, points[i], points[i + 1]);
    if (!d) return nullptr;
    total = add(total, d);
  }
  return context().ok() && isNumber(total) ? total : nullptr;
}

}  // namespace Cas
}  // namespace Calculator
