#include "calculator/math/calculator_polynomial.h"
#include "calculator/math/calculator_complex.h"
#include "calculator/cas/calculator_cas.h"
#include <algorithm>
#include <memory>

namespace Calculator {
namespace {
Value add(const Value& a, const Value& b) { return realCombine(a, b, '+'); }
Value sub(const Value& a, const Value& b) { return realCombine(a, b, '-'); }
Value mul(const Value& a, const Value& b) { return realCombine(a, b, '*'); }
Value div(const Value& a, const Value& b) { return realCombine(a, b, '/'); }

// Exactly zero when the exact form knows; otherwise zero against `scale`.
bool isZero(const Value& v, const Decimal& scale) {
  if (v.isExact()) return v.symbolic.zero();
  return v.number.abs() <= scale * Decimal("1E-30");
}
Value finished(Value v) {
  v.number = v.number.rounded();
  if (!v.number.zero() && v.number.abs() < Decimal("1E-99")) return Value(0);
  return v;
}
bool inRange(const Value& v) { return v.number.finite() && v.number.abs() <= Decimal("9.999999999E99"); }

struct Complex {
  Decimal re, im;
};
Complex operator+(const Complex& a, const Complex& b) { return {a.re + b.re, a.im + b.im}; }
Complex operator-(const Complex& a, const Complex& b) { return {a.re - b.re, a.im - b.im}; }
Complex operator*(const Complex& a, const Complex& b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
Complex operator/(const Complex& a, const Complex& b) {
  const Decimal d = b.re * b.re + b.im * b.im;
  return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}
Decimal magnitude(const Complex& z) { return (z.re * z.re + z.im * z.im).sqrt(); }

struct Solver {
  const Request& q;
  const std::function<bool()>& cancel;
  const std::function<void()>& cooperate;
  Error error = Error::None;
  std::vector<Solution> pages;

  bool stopped() {
    if (cancel && cancel()) { error = Error::Cancelled; return true; }
    if (cooperate) cooperate();
    return false;
  }
  void page(const std::string& label, const ScalarValue& value) {
    Solution s;
    s.label = label;
    s.value = value;
    if (!inRange(s.value.re) || (s.value.complex && !inRange(s.value.im))) error = Error::Math;
    s.value.re = finished(s.value.re);
    if (s.value.complex) s.value.im = finished(s.value.im);
    pages.push_back(s);
  }
  void text(const std::string& t) {
    Solution s;
    s.text = t;
    pages.push_back(s);
  }

  // ---- Simultaneous equations: Gauss-Jordan on exact values, rank classified.
  void simul(const EquationTask& task) {
    const int n = task.size, w = n + 1;
    std::array<Value, 20> a = task.c;
    Decimal scale(1);
    for (int i = 0; i < n * w; ++i) if (a[static_cast<size_t>(i)].number.abs() > scale) scale = a[static_cast<size_t>(i)].number.abs();
    auto A = [&](int r, int c) -> Value& { return a[static_cast<size_t>(r * w + c)]; };
    int rank = 0;
    for (int col = 0; col < n && rank < n; ++col) {
      int best = -1;
      for (int r = rank; r < n; ++r)
        if (!isZero(A(r, col), scale) && (best < 0 || A(r, col).number.abs() > A(best, col).number.abs())) best = r;
      if (best < 0) continue;
      for (int c = 0; c < w; ++c) std::swap(A(best, c), A(rank, c));
      const Value pivot = A(rank, col);
      for (int c = col; c < w; ++c) A(rank, c) = div(A(rank, c), pivot);
      for (int r = 0; r < n; ++r) {
        if (r == rank || isZero(A(r, col), scale)) continue;
        const Value f = A(r, col);
        for (int c = col; c < w; ++c) A(r, c) = sub(A(r, c), mul(f, A(rank, c)));
      }
      ++rank;
      if (stopped()) return;
    }
    if (rank < n) {
      bool inconsistent = false;
      for (int r = rank; r < n; ++r) inconsistent = inconsistent || !isZero(A(r, n), scale);
      text(inconsistent ? "No Solution" : "Infinite Solution");
      return;
    }
    static const char* const kNames[] = {"x", "y", "z", "t"};
    for (int r = 0; r < n; ++r) page(kNames[r], A(r, n));
  }

  // ---- Polynomials
  static Value horner(const std::vector<Value>& p, const Value& x) {
    Value h = p[0];
    for (size_t i = 1; i < p.size(); ++i) h = add(mul(h, x), p[i]);
    return h;
  }
  static void quadratic(const Value& a, const Value& b, const Value& c, std::vector<ScalarValue>& roots) {
    const Value D = sub(mul(b, b), mul(Value(4), mul(a, c)));
    const Value twoA = mul(Value(2), a), minusB = realNegate(b);
    if (isZero(D, Decimal(1))) { roots.push_back(div(minusB, twoA)); return; }
    if (!D.number.negative()) {
      const Value s = realSqrt(D);
      roots.push_back(div(add(minusB, s), twoA));
      roots.push_back(div(sub(minusB, s), twoA));
      return;
    }
    const Value re = div(minusB, twoA);
    Value im = div(realSqrt(realNegate(D)), twoA);
    if (im.number.negative()) im = realNegate(im);
    roots.push_back(ScalarValue::of(re, im));
    roots.push_back(ScalarValue::of(re, realNegate(im)));
  }
  // Durand-Kerner on the decimal image of p; roots land in z.
  bool durandKerner(const std::vector<Value>& p, std::vector<Complex>& z) {
    const size_t n = p.size() - 1;
    std::vector<Decimal> c(n + 1);
    Decimal bound(0);
    for (size_t i = 0; i <= n; ++i) {
      c[i] = p[i].number / p[0].number;
      if (i && c[i].abs() > bound) bound = c[i].abs();
    }
    bound = bound + 1;
    auto eval = [&](const Complex& x) {
      Complex h{c[0], Decimal(0)};
      for (size_t i = 1; i <= n; ++i) h = h * x + Complex{c[i], Decimal(0)};
      return h;
    };
    z.assign(n, Complex{});
    const Complex seed{Decimal("0.4"), Decimal("0.9")};
    Complex power{Decimal(1), Decimal(0)};
    for (size_t k = 0; k < n; ++k) {
      z[k] = Complex{power.re * bound, power.im * bound};
      power = power * seed;
    }
    for (int iteration = 0; iteration < 800; ++iteration) {
      if (iteration % 16 == 0 && stopped()) return false;
      Decimal largest(0);
      for (size_t k = 0; k < n; ++k) {
        Complex denominator{Decimal(1), Decimal(0)};
        for (size_t j = 0; j < n; ++j) if (j != k) denominator = denominator * (z[k] - z[j]);
        if (denominator.re.zero() && denominator.im.zero()) denominator.re = Decimal("1E-30");
        const Complex step = eval(z[k]) / denominator;
        z[k] = z[k] - step;
        const Decimal size = magnitude(step) / (magnitude(z[k]) + 1);
        if (size > largest) largest = size;
      }
      if (largest < Decimal("1E-32")) return true;
    }
    return true;  // repeated roots converge slowly; the classification below copes
  }
  void polynomial(const EquationTask& task, std::vector<ScalarValue>& roots, bool& ok) {
    std::vector<Value> p(task.c.begin(), task.c.begin() + task.size + 1);
    ok = !isZero(p[0], Decimal(1));
    if (!ok) { error = Error::Math; return; }
    std::vector<Value> real;
    // Rational roots, confirmed exactly and divided out while degree > 2.
    while (p.size() > 3) {
      std::vector<Complex> z;
      if (!durandKerner(p, z)) return;
      bool deflated = false;
      for (const Complex& root : z) {
        if (root.im.abs() > Decimal("1E-8") * (root.re.abs() + 1)) continue;
        for (int d = 1; d <= 60 && !deflated; ++d) {
          int64_t numerator;
          if (!(root.re * d).rounded(18).quantized(0).toInt(numerator) || numerator > 1000000000000LL || numerator < -1000000000000LL) continue;
          const Value candidate = div(Value(Decimal(std::to_string(numerator))), Value(d));
          const Value residual = horner(p, candidate);
          if (!residual.isExact() || !isZero(residual, Decimal(1))) continue;
          std::vector<Value> q(p.size() - 1);
          q[0] = p[0];
          for (size_t i = 1; i < q.size(); ++i) q[i] = add(mul(q[i - 1], candidate), p[i]);
          p = q;
          real.push_back(candidate);
          deflated = true;
        }
        if (deflated) break;
      }
      if (!deflated) break;
    }
    if (p.size() == 3) quadratic(p[0], p[1], p[2], roots);
    else if (p.size() == 2) roots.push_back(div(realNegate(p[1]), p[0]));
    else {
      std::vector<Complex> z;
      if (!durandKerner(p, z)) return;
      for (const Complex& root : z) {
        if (root.im.abs() <= Decimal("1E-18") * (root.re.abs() + 1)) roots.push_back(Value(root.re));
        else roots.push_back(ScalarValue::of(Value(root.re), Value(root.im)));
      }
    }
    for (const Value& r : real) roots.push_back(r);
    // Real roots high to low, then complex pairs with the positive part first
    // (native cubic/quartic order is plan O-4).
    std::stable_sort(roots.begin(), roots.end(), [](const ScalarValue& a, const ScalarValue& b) {
      if (a.complex != b.complex) return !a.complex;
      if (!a.complex) return a.re.number > b.re.number;
      return a.re.number == b.re.number ? a.im.number > b.im.number : a.re.number > b.re.number;
    });
    // Merge a repeated root that deflation or the decimal pass listed twice.
    roots.erase(std::unique(roots.begin(), roots.end(), [](const ScalarValue& a, const ScalarValue& b) {
      return !a.complex && !b.complex && (a.re.number - b.re.number).abs() <= Decimal("1E-20") * (a.re.number.abs() + 1);
    }), roots.end());
  }
  void polynomialPages(const EquationTask& task) {
    std::vector<ScalarValue> roots;
    bool ok = true;
    polynomial(task, roots, ok);
    if (error != Error::None) return;
    if (roots.size() == 1) page("x", roots[0]);
    else for (size_t i = 0; i < roots.size(); ++i) page("x" + std::to_string(i + 1), roots[i]);
    const std::vector<Value> p(task.c.begin(), task.c.begin() + task.size + 1);
    const bool up = !p[0].number.negative();
    if (task.size == 2) {
      const Value x = div(realNegate(p[1]), mul(Value(2), p[0]));
      page(up ? "x-Min" : "x-Max", x);
      page(up ? "y-Min" : "y-Max", horner(p, x));
    } else if (task.size == 3) {
      std::vector<ScalarValue> critical;
      quadratic(mul(Value(3), p[0]), mul(Value(2), p[1]), p[2], critical);
      if (critical.size() != 2 || critical[0].complex) { text("No Local Max/Min"); return; }
      // Left critical point: a maximum when the cubic rises at the far right.
      const bool firstLeft = critical[0].re.number < critical[1].re.number;
      const Value left = firstLeft ? critical[0].re : critical[1].re, right = firstLeft ? critical[1].re : critical[0].re;
      const Value maximum = up ? left : right, minimum = up ? right : left;
      page("x-Max", maximum);
      page("y-Max", horner(p, maximum));
      page("x-Min", minimum);
      page("y-Min", horner(p, minimum));
    }
  }

  // ---- Inequality: sign of p between its distinct real roots.
  void inequality(const EquationTask& task) {
    std::vector<ScalarValue> all;
    bool ok = true;
    polynomial(task, all, ok);
    if (error != Error::None) return;
    std::vector<Value> roots;
    for (const auto& r : all) if (!r.complex) roots.push_back(r.re);
    std::sort(roots.begin(), roots.end(), [](const Value& a, const Value& b) { return a.number < b.number; });
    const std::vector<Value> p(task.c.begin(), task.c.begin() + task.size + 1);
    auto sign = [&](const Decimal& x) {
      Decimal h = p[0].number;
      for (size_t i = 1; i < p.size(); ++i) h = h * x + p[i].number;
      return h.zero() ? 0 : h.negative() ? -1 : 1;
    };
    const Relation rel = task.relation;
    auto wants = [&](int s) {
      return rel == Relation::Greater ? s > 0 : rel == Relation::Less ? s < 0 : rel == Relation::GreaterEqual ? s >= 0 : s <= 0;
    };
    const size_t k = roots.size();
    std::vector<bool> region(k + 1);
    for (size_t i = 0; i <= k; ++i) {
      Decimal t;
      if (k == 0) t = Decimal(0);
      else if (i == 0) t = roots[0].number - 1;
      else if (i == k) t = roots[k - 1].number + 1;
      else t = (roots[i - 1].number + roots[i].number) / 2;
      region[i] = wants(sign(t));
    }
    const bool point = rel == Relation::GreaterEqual || rel == Relation::LessEqual;
    // Walk region0 root0 region1 ... regionk; element 2i is region i, 2i+1 root i.
    auto included = [&](size_t e) { return e % 2 ? point : static_cast<bool>(region[e / 2]); };
    const std::string le = "<=", lt = "<";
    auto value = [&](size_t root) { return "\x01" + formatValue(roots[root], q.settings) + "\x02"; };
    std::string out;
    size_t e = 0;
    const size_t last = 2 * k;
    while (e <= last) {
      if (!included(e)) { ++e; continue; }
      const size_t begin = e;
      while (e + 1 <= last && included(e + 1)) ++e;
      const size_t end = e;
      ++e;
      std::string piece;
      if (begin == 0 && end == last) { out = "All Real Numbers"; break; }
      if (begin == end && begin % 2) piece = "x=" + value(begin / 2);
      else {
        // A run starting on a root includes it; one starting in a region is open at the root before.
        if (begin != 0) piece = value(begin % 2 ? begin / 2 : begin / 2 - 1) + (begin % 2 ? le : lt);
        piece += "x";
        if (end != last) piece += (end % 2 ? le : lt) + value(end % 2 ? end / 2 : end / 2);
      }
      if (!out.empty()) out += ", ";
      out += piece;
    }
    text(out.empty() ? "No Solution" : out);
  }

  // ---- Solver: every real root of the equation, always as numbers. The CAS
  // finds them exactly where it can; whatever it leaves unevaluated (solve(...)
  // or a parameter in the way) is found numerically -- damped Newton from the
  // initial value, then a bounded scan for sign changes -- so a root the CAS
  // cannot write never means "no answer".
  void solve(const EquationTask& task, Result& r) {
    constexpr size_t kMaxRoots = 8;
    constexpr int kScanSamples = 160, kBisections = 14;
    const Parsed parsed = parse(q.expression);
    if (parsed.error != Error::None) { r.error = parsed.error; r.position = parsed.position; return; }
    Expression left = parsed.root, right;
    if (left && left->kind == Node::Kind::Binary && left->text == "=") { right = left->children[1]; left = left->children[0]; }
    int target = -1;
    std::function<void(const Expression&)> find = [&](const Expression& n) {
      if (!n) return;
      if (n->kind == Node::Kind::Symbol) {
        const int i = variableIndex(n->text);
        if (i == 6 || (i >= 0 && target < 0)) target = i;
      }
      for (const auto& child : n->children) find(child);
    };
    find(parsed.root);
    if (target < 0) { r.error = Error::Variable; return; }
    auto req = std::make_unique<Request>(q);
    req->verify = false;
    req->expression = task.seed;
    {
      const Result seedResult = evaluate(*req, cancel, cooperate);
      if (seedResult.error != Error::None) { r.error = seedResult.error; r.position = seedResult.position; return; }
      req->memory.variables[static_cast<size_t>(target)] = seedResult.value;
    }
    auto f = [&](const Decimal& x, bool& good) {
      // Newton iterates are approximations: evaluate them as such (decimal
      // speed), even when one happens to be an integer.
      req->memory.variables[static_cast<size_t>(target)] = Value::approximate(x);
      const Result a = evaluateTree(left, *req, cancel, cooperate);
      if (a.error != Error::None) {
        if (a.error == Error::Cancelled) error = a.error;
        good = false;
        return Decimal(0);
      }
      if (!right) return a.value.number;
      const Result b = evaluateTree(right, *req, cancel, cooperate);
      if (b.error != Error::None) {
        if (b.error == Error::Cancelled) error = b.error;
        good = false;
        return Decimal(0);
      }
      return a.value.number - b.value.number;
    };
    const Decimal seed = req->memory.variables[static_cast<size_t>(target)].re.number;

    // Damped Newton with a central-difference slope; true when it settled on a
    // root (x) with residual fx. A stop request is left in `error`.
    auto newton = [&](const Decimal& start, Decimal& x, Decimal& fx) {
      x = start;
      const Decimal seedScale = x.abs();
      bool good = true, converged = false;
      fx = f(x, good);
      for (int i = 0; i < 200 && good && !converged; ++i) {
        if (stopped()) return false;
        if (fx.zero()) { converged = true; break; }
        // With 40 significant digits, 1E-13 balances central-difference
        // truncation against rounding in the subtraction used for the slope.
        const Decimal h = (x.zero() ? Decimal(1) : x.abs()) * Decimal("1E-13");
        const Decimal stepScale = x.abs() > seedScale ? x.abs() : seedScale;
        const Decimal stepTolerance = stepScale * Decimal("1E-25");
        bool slopeGood = true;
        const Decimal slope = (f(x + h, slopeGood) - f(x - h, slopeGood)) / (h * 2);
        if (!slopeGood || slope.zero()) { x = x + (x.abs() + 1) * Decimal("0.001"); fx = f(x, good); continue; }
        Decimal step = fx / slope, next = x - step;
        bool nextGood = true;
        Decimal fn = f(next, nextGood);
        for (int halve = 0; halve < 40 && (!nextGood || fn.abs() > fx.abs()); ++halve) {
          step = step / 2;
          next = x - step;
          nextGood = true;
          fn = f(next, nextGood);
        }
        // A bounded line search can exhaust its halvings without descending.
        // At decimal precision that can mean the Newton step is already below
        // resolution. Scaling by the seed as well as x handles repeated roots
        // that approach zero with a fixed fractional step, without granting a
        // tiny seed the old order-one absolute tolerance.
        if (!nextGood) break;
        if (!fn.zero() && fn.abs() >= fx.abs()) {
          if (step.abs() <= stepTolerance) converged = true;
          break;
        }
        x = next;
        fx = fn;
        // Adding 1 to this scale accepted tiny roots with order-one relative
        // error, and even returned roots of x^2 + 1E-50. Keep convergence
        // relative to the iterate; an exactly zero residual is always done.
        const Decimal nextScale = x.abs() > seedScale ? x.abs() : seedScale;
        converged = fx.zero() || step.abs() <= nextScale * Decimal("1E-25");
      }
      return good && converged;
    };

    struct Found {
      Value value;
      bool numeric = false;  // found by Newton/scan: its residual is the decimal fx
      Decimal fx;
    };
    std::vector<Found> roots;
    auto known = [&](const Decimal& x) {
      for (const Found& k : roots) {
        const Decimal big = k.value.number.abs() > x.abs() ? k.value.number.abs() : x.abs();
        if ((k.value.number - x).abs() <= big * Decimal("1E-15") + Decimal("1E-30")) return true;
      }
      return false;
    };

    // Exact roots from the CAS. CAS treats other variables as symbols, so a
    // parameterized root stays unevaluated and the numeric search takes it.
    const std::string variable(1, "ABCDEFxyz"[target]);
    const auto exact = Cas::run("solve(" + q.expression + "," + variable + ")",
                                q.settings, q.memory, cancel, cooperate);
    if (exact.error == Error::Cancelled) { r.error = exact.error; return; }
    bool complete = false;
    if (exact.error == Error::None && exact.value && exact.value->kind == Cas::Kind::List) {
      complete = true;
      for (const auto& candidate : exact.value->args) {
        if (candidate->kind != Cas::Kind::Equation || candidate->args.size() != 2 ||
            candidate->args[1]->kind != Cas::Kind::Number) { complete = false; continue; }
        const Value v = Value::of(candidate->args[1]->number);
        if (!v.number.finite()) { complete = false; continue; }
        if (!known(v.number)) roots.push_back({finished(v), false, Decimal(0)});
      }
    }

    if (!complete) {
      auto add = [&](const Decimal& x, const Decimal& fx) {
        if (!known(x) && roots.size() < kMaxRoots) roots.push_back({finished(Value(x)), true, fx});
      };
      Decimal x, fx;
      if (newton(seed, x, fx)) add(x, fx);
      if (error != Error::None) { r.error = error; return; }
      // Sign changes on a grid around the initial value, each bracket narrowed
      // by bisection and settled by Newton.
      const Decimal span = seed.abs() * 10 > Decimal(10) ? seed.abs() * 10 : Decimal(10);
      const Decimal first = seed - span, stride = span * 2 / kScanSamples;
      bool previousGood = true;
      Decimal previousX = first, previousF = f(first, previousGood);
      for (int i = 1; i <= kScanSamples && roots.size() < kMaxRoots; ++i) {
        if (stopped()) { r.error = error; return; }
        const Decimal cx = first + stride * i;
        bool good = true;
        const Decimal cf = f(cx, good);
        if (good && previousGood && !previousF.zero() && !cf.zero() && previousF.negative() != cf.negative()) {
          Decimal a = previousX, b = cx, fa = previousF;
          for (int k = 0; k < kBisections; ++k) {
            const Decimal m = (a + b) / 2;
            bool mGood = true;
            const Decimal fm = f(m, mGood);
            if (!mGood) break;
            if (fm.negative() == fa.negative()) { a = m; fa = fm; } else b = m;
          }
          Decimal rx, rf;
          if (newton((a + b) / 2, rx, rf)) add(rx, rf);
        } else if (good && cf.zero()) {
          add(cx, cf);
        }
        if (error != Error::None) { r.error = error; return; }
        previousGood = good;
        previousX = cx;
        previousF = cf;
      }
    }
    if (roots.empty()) { r.error = Error::CannotSolve; return; }

    // At most kMaxRoots, the ones nearest the initial value, then ascending.
    auto byDistance = [&](const Found& a, const Found& b) {
      return (a.value.number - seed).abs() < (b.value.number - seed).abs();
    };
    std::stable_sort(roots.begin(), roots.end(), byDistance);
    if (roots.size() > kMaxRoots) roots.resize(kMaxRoots);
    const Value nearest = roots[0].value;  // the one assigned to the variable
    std::stable_sort(roots.begin(), roots.end(), [](const Found& a, const Found& b) { return a.value.number < b.value.number; });

    static const char* const kNames[] = {"A", "B", "C", "D", "E", "F", "x", "y", "z"};
    for (size_t i = 0; i < roots.size(); ++i) {
      const std::string name = roots.size() == 1 ? kNames[target] : kNames[target] + std::to_string(i + 1);
      page(name, roots[i].value);
    }
    r.memory.variables[static_cast<size_t>(target)] = nearest;
  }
};
}  // namespace

Result solveEquation(const EquationTask& task, const Request& request, const std::function<bool()>& cancelled,
                     const std::function<void()>& cooperate) {
  Result r;
  r.memory = request.memory;
  Solver solver{request, cancelled, cooperate};
  switch (task.kind) {
    case EquationKind::Simul: solver.simul(task); break;
    case EquationKind::Polynomial: solver.polynomialPages(task); break;
    case EquationKind::Inequality: solver.inequality(task); break;
    case EquationKind::Solver: solver.solve(task, r); break;
  }
  if (r.error == Error::None) r.error = solver.error;
  if (r.error != Error::None) {
    r.memory = request.memory;
    return r;
  }
  r.solutions = std::make_shared<std::vector<Solution>>(std::move(solver.pages));
  return r;
}

}  // namespace Calculator
