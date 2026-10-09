#include "calculator/cas/calculator_cas_solve.h"
#include "calculator/cas/calculator_cas_poly.h"
#include "calculator/cas/calculator_cas_simplify.h"
#include <algorithm>

namespace Calculator {
namespace Cas {
namespace {
constexpr unsigned kMaxDepth = 6;

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
// -1, 0, +1 for a Number whose sign is certain; 2 otherwise (symbolic).
int knownSign(const Expr& e) {
  if (!isNumber(e)) return 2;
  bool certain = false;
  const int s = e->number.sign(&certain);
  return certain ? s : 2;
}

bool solveZero(const Expr& f, const Expr& x, unsigned depth, Args& roots, Args& residue);

// a x^2 + b x + c = 0.
void quadratic(const Expr& a, const Expr& b, const Expr& c, const Expr& x, Args& roots) {
  const Expr disc = simplify(sub(pow(b, 2), mul(Args{integer(4), a, c})), x);
  const Expr twiceA = mul(integer(2), a);
  const int s = knownSign(disc);
  if (s < 0) return;
  if (s == 0) { roots.push_back(div(neg(b), twiceA)); return; }
  const Expr root = sqrt(disc);
  roots.push_back(div(sub(neg(b), root), twiceA));
  roots.push_back(div(add(neg(b), root), twiceA));
}
bool numericPolyRoots(Poly p, const Expr& x, Args& roots, Args& residue) {
  // Distinct roots only: divide out the repeated part.
  Poly g, q, r;
  if (polyGcd(p, polyDerivative(p), g) && g.degree() > 0 && polyDivMod(p, g, q, r)) p = q;
  if (!p.c.empty() && p.c[0].zero()) {
    roots.push_back(integer(0));
    while (!p.c.empty() && p.c[0].zero()) p.c.erase(p.c.begin());
  }
  std::vector<Rational> rational;
  if (rationalPoly(p) && rationalRoots(p, rational)) {
    for (const auto& root : rational) {
      if (root.zero()) continue;
      roots.push_back(number(ExactNumber::rational(root)));
      p = deflate(p, ExactNumber::rational(root));
    }
  }
  if (!context().ok()) return false;
  switch (p.degree()) {
    case -1:
    case 0: return true;
    case 1: roots.push_back(number(-p.c[0] / p.c[1])); return true;
    case 2: quadratic(number(p.c[2]), number(p.c[1]), number(p.c[0]), x, roots); return true;
    default: break;
  }
  if (p.degree() == 4 && p.c[1].zero() && p.c[3].zero()) {
    // Biquadratic: t = x^2, then x = +-sqrt(t) for each t >= 0.
    Args ts;
    quadratic(number(p.c[4]), number(p.c[2]), number(p.c[0]), x, ts);
    for (const auto& t : ts) {
      const int s = knownSign(t);
      if (s < 0) continue;
      if (s == 0) { roots.push_back(integer(0)); continue; }
      roots.push_back(neg(sqrt(t)));
      roots.push_back(sqrt(t));
    }
    return true;
  }
  residue.push_back(fromPoly(p, x));
  return true;
}
bool polyRoots(const std::vector<Expr>& c, const Expr& x, Args& roots, Args& residue) {
  const int degree = static_cast<int>(c.size()) - 1;
  if (degree < 1) return true;
  if (degree == 1) { roots.push_back(div(neg(c[0]), c[1])); return true; }
  bool numeric = true;
  for (const auto& k : c) numeric = numeric && isNumber(k);
  if (numeric) {
    Poly p;
    for (const auto& k : c) p.c.push_back(k->number);
    return numericPolyRoots(p, x, roots, residue);
  }
  if (degree == 2) { quadratic(c[2], c[1], c[0], x, roots); return true; }
  if (zero(c[0])) {
    // x * (rest): x = 0 is a root, the rest has degree one less.
    roots.push_back(integer(0));
    return polyRoots(std::vector<Expr>(c.begin() + 1, c.end()), x, roots, residue);
  }
  return false;
}

// Isolates x in g(x) = r by inverting the one branch of g that holds x.
// Principal branches only; the root check discards what is not a solution.
bool isolate(const Expr& g, const Expr& x, const Expr& r, Args& roots, unsigned depth) {
  if (depth > 16 || !context().step()) return false;
  if (equal(g, x)) { roots.push_back(r); return true; }
  switch (g->kind) {
    case Kind::Add:
    case Kind::Mul: {
      Args constant;
      Expr variable;
      for (const auto& a : g->args) {
        if (freeOf(a, x)) { constant.push_back(a); continue; }
        if (variable) return false;  // x in two operands: not invertible here
        variable = a;
      }
      if (!variable) return false;
      const Expr c = g->kind == Kind::Add ? add(std::move(constant)) : mul(std::move(constant));
      if (g->kind == Kind::Mul && zero(c)) return false;
      return isolate(variable, x, g->kind == Kind::Add ? sub(r, c) : div(r, c), roots, depth + 1);
    }
    case Kind::Pow: {
      const Expr& u = g->args[0];
      const Expr& v = g->args[1];
      if (freeOf(v, x)) {
        Rational q;
        if (rationalValue(v, q)) {
          if (q.sign() < 0) {
            if (zero(r)) return true;  // u^-k is never 0
            return isolate(pow(u, number(ExactNumber::rational(-q))), x, pow(r, -1), roots, depth + 1);
          }
          const Expr inverse = number(ExactNumber::rational(q.inverse()));
          if (!q.num().odd()) {
            // Even power: u = +-r^(1/q) for r >= 0.
            const int s = knownSign(r);
            if (s < 0) return true;
            if (s == 0) return isolate(u, x, integer(0), roots, depth + 1);
            const Expr root = pow(r, inverse);
            return isolate(u, x, root, roots, depth + 1) && isolate(u, x, neg(root), roots, depth + 1);
          }
          return isolate(u, x, pow(r, inverse), roots, depth + 1);
        }
        return isolate(u, x, pow(r, pow(v, -1)), roots, depth + 1);
      }
      if (freeOf(u, x)) {
        if (knownSign(r) <= 0) return true;  // a^v > 0
        return isolate(v, x, div(fn(Fn::Ln, r), fn(Fn::Ln, u)), roots, depth + 1);
      }
      return false;
    }
    case Kind::Func: {
      const Expr& u = g->args[0];
      const int s = knownSign(r);
      auto one = [&](Fn inverse) { return isolate(u, x, fn(inverse, r), roots, depth + 1); };
      switch (g->fn) {
        case Fn::Sin:
        case Fn::Cos: {
          if (isNumber(r) && r->number.isRational() && Rational::compare(r->number.rationalValue().abs(), Rational(1)) > 0)
            return true;
          const Expr principal = fn(g->fn == Fn::Sin ? Fn::Asin : Fn::Acos, r);
          const Expr other = g->fn == Fn::Sin ? sub(pi(), principal) : neg(principal);
          if (equal(principal, other)) return isolate(u, x, principal, roots, depth + 1);
          return isolate(u, x, principal, roots, depth + 1) && isolate(u, x, other, roots, depth + 1);
        }
        case Fn::Tan: return one(Fn::Atan);
        case Fn::Ln: return isolate(u, x, exp(r), roots, depth + 1);
        case Fn::Abs:
          if (s < 0) return true;
          if (s == 0) return isolate(u, x, integer(0), roots, depth + 1);
          return isolate(u, x, r, roots, depth + 1) && isolate(u, x, neg(r), roots, depth + 1);
        case Fn::Asin: return one(Fn::Sin);
        case Fn::Acos: return one(Fn::Cos);
        case Fn::Atan: return one(Fn::Tan);
        case Fn::Sinh: return one(Fn::Asinh);
        case Fn::Tanh: return one(Fn::Atanh);
        case Fn::Asinh: return one(Fn::Sinh);
        case Fn::Acosh: return one(Fn::Cosh);
        case Fn::Atanh: return one(Fn::Tanh);
        case Fn::Cosh: {
          const Expr principal = fn(Fn::Acosh, r);
          return isolate(u, x, principal, roots, depth + 1) && isolate(u, x, neg(principal), roots, depth + 1);
        }
      }
      return false;
    }
    default: return false;
  }
}

// ---- Kernel substitution ---------------------------------------------------------
void kernels(const Expr& e, const Expr& x, Args& out) {
  auto push = [&](const Expr& k) {
    for (const auto& o : out)
      if (equal(o, k)) return;
    out.push_back(k);
  };
  if (freeOf(e, x)) return;
  Rational q;
  if (e->kind == Kind::Func || (e->kind == Kind::Pow && isNumber(e->args[0])) ||
      (e->kind == Kind::Pow && equal(e->args[0], x) && rationalValue(e->args[1], q) && !q.integer()))
    push(e);
  for (const auto& a : e->args) kernels(a, x, out);
}
// e with the kernel u replaced by t, including implicit occurrences:
// B^(k v) = (B^v)^k, and x^b = (x^a)^(b/a), x = (x^a)^(1/a).
Expr replaceKernel(const Expr& e, const Expr& u, const Expr& t, const Expr& x) {
  if (equal(e, u)) return t;
  if (u->kind == Kind::Pow && e->kind == Kind::Pow && equal(e->args[0], u->args[0])) {
    const Expr ratio = div(e->args[1], u->args[1]);
    if (isNumber(ratio)) return pow(t, ratio);
  }
  if (u->kind == Kind::Pow && equal(u->args[0], x) && equal(e, x)) return pow(t, pow(u->args[1], -1));
  return mapArgs(e, [&](const Expr& a) { return replaceKernel(a, u, t, x); });
}
bool byKernel(const Expr& n, const Expr& x, unsigned depth, Args& roots) {
  Args candidates;
  kernels(n, x, candidates);
  const Expr t = symbol(depth % 2 ? "#u" : "#v");
  for (const auto& u : candidates) {
    const Expr g = expand(replaceKernel(n, u, t, x));
    std::vector<Expr> c;
    if (!freeOf(g, x) || !coefficients(g, t, c, 8) || c.size() < 2) continue;
    Args inner, residue;
    if (!polyRoots(c, t, inner, residue) || !residue.empty()) continue;
    bool all = true;
    for (const auto& value : inner) all = isolate(u, x, value, roots, 0) && all;
    if (all) return true;
  }
  return false;
}

// One radical among other terms: c*u^(p/q) + rest = 0 gives u^p = (-rest/c)^q.
// Raising to the q-th power can add roots; solve() checks each candidate
// against the original equation and drops the extraneous ones.
bool eliminateRadical(const Expr& n, const Expr& x, unsigned depth, Args& roots, Args& residue) {
  if (n->kind != Kind::Add) return false;
  for (size_t i = 0; i < n->args.size(); ++i) {
    const Expr& t = n->args[i];
    Expr radical;
    Args coefficient;
    bool usable = true;
    for (const auto& f : t->kind == Kind::Mul ? t->args : Args{t}) {
      Expr b, e;
      splitPower(f, b, e);
      Rational r;
      if (!radical && !freeOf(b, x) && rationalValue(e, r) && !r.integer() && r.den() <= BigInt(8)) radical = f;
      else if (freeOf(f, x)) coefficient.push_back(f);
      else usable = false;
    }
    if (!radical || !usable) continue;
    Args others;
    for (size_t j = 0; j < n->args.size(); ++j)
      if (j != i) others.push_back(n->args[j]);
    const Expr rhs = div(neg(add(std::move(others))), mul(std::move(coefficient)));
    Rational r;
    rationalValue(radical->args[1], r);
    const Expr squared = sub(pow(radical->args[0], number(ExactNumber::integer(r.num()))),
                             pow(rhs, number(ExactNumber::integer(r.den()))));
    return solveZero(expand(squared), x, depth + 1, roots, residue);
  }
  return false;
}

bool solveZero(const Expr& f, const Expr& x, unsigned depth, Args& roots, Args& residue) {
  if (depth > kMaxDepth || !context().step()) return false;
  Expr n, d;
  numerDenom(f, n, d);
  if (freeOf(n, x)) return true;  // a nonzero constant numerator: no roots here
  if (n->kind == Kind::Mul) {
    // Zero product: each factor that holds x.
    for (const auto& factor : n->args) {
      if (freeOf(factor, x)) continue;
      if (!solveZero(factor, x, depth + 1, roots, residue)) residue.push_back(factor);
    }
    return true;
  }
  Rational k;
  if (n->kind == Kind::Pow && rationalValue(n->args[1], k) && k.sign() > 0)
    return solveZero(n->args[0], x, depth + 1, roots, residue);
  std::vector<Expr> c;
  if (coefficients(n, x, c, 32)) return polyRoots(c, x, roots, residue);
  Args found;
  if (isolate(n, x, integer(0), found, 0)) {
    roots.insert(roots.end(), found.begin(), found.end());
    return true;
  }
  found.clear();
  if (eliminateRadical(n, x, depth, found, residue)) {
    roots.insert(roots.end(), found.begin(), found.end());
    return true;
  }
  found.clear();
  if (byKernel(expand(n), x, depth, found)) {
    roots.insert(roots.end(), found.begin(), found.end());
    return true;
  }
  return false;
}

// ---- Systems -----------------------------------------------------------------
// Elimination after NeoCalculator's SystemSolver (substitution, then Sylvester
// resultants), over exact trees; unlike it, every candidate is checked
// against all the original equations, not only the one it was solved from.
constexpr unsigned kMaxEliminationDegree = 8;  // of one unknown in one equation
constexpr size_t kMaxSylvester = 10;          // resultant matrix order

bool freeOfAll(const Expr& e, const Args& vars) {
  for (const auto& v : vars)
    if (!freeOf(e, v)) return false;
  return true;
}
// f = 0 over one denominator: its expanded numerator.
Expr clearedNumerator(const Expr& f) {
  Expr n, d;
  numerDenom(f, n, d);
  return expand(n);
}

// Resultant of p and q in one unknown (coefficient vectors, lowest power
// first): the determinant of their Sylvester matrix, expanded along columns
// with every minor memoized by its unused rows (the mask fixes the column).
// Division-free, so coefficients that are polynomials in the other unknowns
// stay exact; the order is at most kMaxSylvester, so at most 1024 minors.
class Sylvester {
 public:
  Sylvester(const std::vector<Expr>& p, const std::vector<Expr>& q)
      : p_(p), q_(q), m_(p.size() - 1), n_(q.size() - 1), order_(m_ + n_), memo_(size_t(1) << order_) {}
  Expr determinant() { return minor((uint32_t(1) << order_) - 1, 0); }

 private:
  const std::vector<Expr>& p_;
  const std::vector<Expr>& q_;
  const size_t m_, n_, order_;
  Args memo_;

  // Rows 0..n-1 hold p's coefficients shifted right by the row, highest
  // power first; rows n..n+m-1 hold q's the same way. Null for a zero.
  const Expr* entry(size_t row, size_t col) const {
    const bool first = row < n_;
    if (!first) row -= n_;
    const size_t degree = first ? m_ : n_;
    if (col < row || col - row > degree) return nullptr;
    return &(first ? p_ : q_)[degree - (col - row)];
  }
  Expr minor(uint32_t rows, size_t col) {
    if (col == order_) return integer(1);
    if (memo_[rows]) return memo_[rows];
    Args terms;
    bool positive = true;
    for (size_t r = 0; r < order_; ++r) {
      if (!(rows & (uint32_t(1) << r))) continue;
      const Expr* a = entry(r, col);
      if (a && !zero(*a)) {
        const Expr rest = minor(rows & ~(uint32_t(1) << r), col + 1);
        if (!context().ok()) return integer(0);
        if (!zero(rest)) terms.push_back(positive ? mul(*a, rest) : neg(mul(*a, rest)));
      }
      positive = !positive;
    }
    return memo_[rows] = expand(add(std::move(terms)));
  }
};

// The roots of g in v, or false when solve() would leave some of them
// unevaluated (a system answer must be complete to be an answer).
bool allRoots(const Expr& g, const Expr& v, Args& roots) {
  Args residue;
  return solveZero(g, v, 0, roots, residue) && residue.empty();
}
// e with each vars[k] replaced by values[k]; false on a domain error there.
bool substituteAll(Expr& e, const Args& vars, const Args& values) {
  return guarded([&] {
    for (size_t k = 0; k < vars.size(); ++k) e = substitute(e, vars[k], values[k]);
    return true;
  });
}

// Candidate solutions of fs = 0 (cleared numerators) in vars, each aligned
// with vars, appended to `out`: a superset of the real solutions, which
// solveSystem() then checks. False when a step has no exact way forward.
// `infinite` is set where an unknown is left unconstrained.
bool eliminate(const Args& fs, const Args& vars, unsigned depth, std::vector<Args>& out, bool& infinite) {
  if (depth > 2 * kMaxSystem || !context().step()) return false;
  // An equation free of every unknown is a verdict: 0 = 0 drops out, c = 0
  // ends this branch.
  Args live;
  for (const auto& f : fs) {
    if (!freeOfAll(f, vars)) { live.push_back(f); continue; }
    const Zero z = zeroTest(f);
    if (z == Zero::No) return true;
    if (z == Zero::Unknown) return false;
  }
  if (vars.empty()) { out.push_back(Args{}); return true; }
  if (live.empty()) { infinite = true; return true; }

  // Substitution: an equation linear in v whose coefficient is a known
  // nonzero free of the unknowns (parameters are taken as generic, as solve()
  // takes a*x+b=0). The last unknown is eliminated first, so x is solved last.
  for (size_t vi = vars.size(); vi-- > 0;) {
    const Expr& v = vars[vi];
    for (size_t fi = 0; fi < live.size(); ++fi) {
      std::vector<Expr> c;
      if (!coefficients(live[fi], v, c, 1) || c.size() != 2 || !freeOfAll(c[1], vars) || zeroTest(c[1]) != Zero::No)
        continue;
      const Expr value = div(neg(c[0]), c[1]);
      Args rest, restVars;
      for (size_t j = 0; j < live.size(); ++j)
        if (j != fi) rest.push_back(clearedNumerator(substitute(live[j], v, value)));
      for (size_t k = 0; k < vars.size(); ++k)
        if (k != vi) restVars.push_back(vars[k]);
      std::vector<Args> partial;
      if (!context().ok() || !eliminate(rest, restVars, depth + 1, partial, infinite)) return false;
      for (auto& p : partial) {
        Expr x = value;
        if (!substituteAll(x, restVars, p)) continue;  // outside the domain
        p.insert(p.begin() + static_cast<std::ptrdiff_t>(vi), x);
        out.push_back(std::move(p));
      }
      return context().ok();
    }
  }

  // One unknown left: the roots of its smallest equation; the check tests
  // them against the others.
  if (vars.size() == 1) {
    const Expr* smallest = &live[0];
    for (const auto& f : live)
      if (f->size < (*smallest)->size) smallest = &f;
    Args roots;
    if (!allRoots(*smallest, vars[0], roots)) return false;
    for (const auto& r : roots) out.push_back(Args{r});
    return true;
  }

  // Resultant: eliminate the last unknown between the equation of least
  // positive degree in it and each other equation that holds it. Common
  // roots of two polynomials are roots of their resultant, so nothing is lost.
  const Expr& v = vars.back();
  size_t pivot = live.size();
  std::vector<std::vector<Expr>> coeffs(live.size());
  for (size_t fi = 0; fi < live.size(); ++fi) {
    if (freeOf(live[fi], v)) continue;
    if (!coefficients(live[fi], v, coeffs[fi], kMaxEliminationDegree) || coeffs[fi].size() < 2) return false;
    if (pivot == live.size() || coeffs[fi].size() < coeffs[pivot].size()) pivot = fi;
  }
  Args reduced, restVars(vars.begin(), vars.end() - 1);
  for (size_t fi = 0; fi < live.size(); ++fi) {
    if (fi == pivot) continue;
    if (coeffs[fi].empty()) { reduced.push_back(live[fi]); continue; }
    if (coeffs[pivot].size() + coeffs[fi].size() - 2 > kMaxSylvester) return false;
    const Expr r = Sylvester(coeffs[pivot], coeffs[fi]).determinant();
    // Identically zero: the two share a factor in v, which elimination
    // cannot separate.
    if (!context().ok() || zeroTest(r) == Zero::Yes) return false;
    reduced.push_back(r);
  }
  std::vector<Args> partial;
  if (!eliminate(reduced, restVars, depth + 1, partial, infinite)) return false;
  for (const auto& p : partial) {
    // v from the first equation (pivot first) that still holds it there.
    bool solved = false, contradiction = false;
    for (size_t k = 0; k <= live.size() && !solved; ++k) {
      const size_t fi = k == 0 ? pivot : k - 1;
      if (pivot == live.size() || (k > 0 && fi == pivot)) continue;
      Expr g = live[fi];
      if (!substituteAll(g, restVars, p)) { contradiction = true; break; }
      g = clearedNumerator(g);
      if (freeOf(g, v)) {
        contradiction = contradiction || zeroTest(g) == Zero::No;
        continue;
      }
      Args roots;
      if (!allRoots(g, v, roots)) return false;
      for (const auto& r : roots) {
        Args full = p;
        full.push_back(r);
        out.push_back(std::move(full));
      }
      solved = true;
    }
    if (!solved && !contradiction) infinite = true;  // v is free at this point
  }
  return context().ok();
}
}  // namespace

Expr solve(const Expr& eq, const Expr& x) {
  const Expr form = eq->kind == Kind::Equation ? eq : equation(eq, integer(0));
  const Expr f = sub(form->args[0], form->args[1]);
  if (!context().ok()) return form;
  if (freeOf(f, x)) {
    const Zero z = zeroTest(f);
    if (z == Zero::Yes) return text("Infinite Solution");
    if (z == Zero::No || isNumber(f)) return text("No Solution");
    return solveOf(form, x);
  }
  Args roots, residue;
  if (!guarded([&] { return solveZero(f, x, 0, roots, residue); })) return context().ok() ? solveOf(form, x) : form;
  Args kept;
  for (const auto& candidate : roots) {
    Expr r;
    Zero z = Zero::Unknown;
    const bool inDomain = guarded([&] {
      r = simplify(candidate, x);
      z = zeroTest(simplify(substitute(f, x, r), x));
      return true;
    });
    if (!context().ok()) return form;
    if (!inDomain || z == Zero::No) continue;  // outside the domain, or extraneous
    bool duplicate = false;
    for (const auto& k : kept) duplicate = duplicate || equal(k, r) || zeroTest(sub(k, r)) == Zero::Yes;
    if (!duplicate) kept.push_back(r);
  }
  std::stable_sort(kept.begin(), kept.end(), [](const Expr& a, const Expr& b) {
    if (!isNumber(a) || !isNumber(b)) return isNumber(a) && !isNumber(b);
    bool certain = false;
    return ExactNumber::compare(a->number, b->number, &certain) < 0 && certain;
  });
  Args items;
  for (const auto& r : kept) items.push_back(equation(x, r));
  for (const auto& rest : residue) items.push_back(solveOf(equation(rest, integer(0)), x));
  if (items.empty()) return text("No Solution");
  return list(std::move(items));
}

Expr solveSystem(const Args& equations, const Args& unknowns) {
  Args forms, fs;
  for (const auto& eq : equations) {
    forms.push_back(eq->kind == Kind::Equation ? eq : equation(eq, integer(0)));
    fs.push_back(clearedNumerator(sub(forms.back()->args[0], forms.back()->args[1])));
  }
  const Expr unevaluated = solveOf(list(forms), list(unknowns));
  if (!context().ok()) return unevaluated;
  std::vector<Args> candidates;
  bool infinite = false;
  if (!guarded([&] { return eliminate(fs, unknowns, 0, candidates, infinite); })) return unevaluated;
  if (infinite) return text("Infinite Solution");

  std::vector<Args> kept;
  for (const auto& candidate : candidates) {
    Args values;
    bool holds = true;
    const bool inDomain = guarded([&] {
      for (size_t i = 0; i < unknowns.size(); ++i) values.push_back(simplify(candidate[i], unknowns[i]));
      for (const auto& form : forms) {
        Expr f = sub(form->args[0], form->args[1]);
        for (size_t i = 0; i < unknowns.size(); ++i) f = substitute(f, unknowns[i], values[i]);
        if (zeroTest(simplify(f, unknowns[0])) == Zero::No) return holds = false;
      }
      return true;
    });
    if (!context().ok()) return unevaluated;
    if (!inDomain || !holds) continue;  // outside the domain, or extraneous
    bool duplicate = false;
    for (const auto& k : kept) {
      bool same = true;
      for (size_t i = 0; i < values.size() && same; ++i)
        same = equal(k[i], values[i]) || zeroTest(sub(k[i], values[i])) == Zero::Yes;
      duplicate = duplicate || same;
    }
    if (!duplicate) kept.push_back(std::move(values));
  }
  if (!context().ok()) return unevaluated;
  // Ascending by the first unknown, then the next, where the order is certain.
  std::stable_sort(kept.begin(), kept.end(), [](const Args& a, const Args& b) {
    for (size_t i = 0; i < a.size(); ++i) {
      if (!isNumber(a[i]) || !isNumber(b[i])) return false;
      bool certain = false;
      const int order = ExactNumber::compare(a[i]->number, b[i]->number, &certain);
      if (!certain) return false;
      if (order != 0) return order < 0;
    }
    return false;
  });
  if (kept.empty()) return text("No Solution");
  Args solutions;
  for (const auto& values : kept) {
    Args items;
    for (size_t i = 0; i < unknowns.size(); ++i) items.push_back(equation(unknowns[i], values[i]));
    solutions.push_back(list(std::move(items)));
  }
  return solutions.size() == 1 ? solutions[0] : list(std::move(solutions));
}

}  // namespace Cas
}  // namespace Calculator
