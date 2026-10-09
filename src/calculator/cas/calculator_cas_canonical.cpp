#include "calculator/cas/calculator_cas_canonical.h"
#include <algorithm>
#include <array>

namespace Calculator {
namespace Cas {
namespace {
template <class T> using Vec = std::vector<T, ExactAllocator<T>>;

Expr leaf(Kind kind, const std::string& name) {
  Node n;
  n.kind = kind;
  n.name = name;
  return finish(std::move(n));
}
Expr rawNode(Kind kind, Args args, Fn f = Fn::Sin) {
  Node n;
  n.kind = kind;
  n.fn = f;
  n.args = std::move(args);
  return finish(std::move(n));
}
Expr rawPow(const Expr& b, const Expr& e) { return rawNode(Kind::Pow, Args{b, e}); }
// A failed exact operation: Math for a certain error, Stack for a value past
// ExactNumber's size bounds (the only way exact + - * turn Inexact).
Expr failed(const ExactNumber& v) {
  context().fail(v.failed() ? Error::Math : Error::Stack);
  return integer(0);
}
// coefficient * rest for a canonical, coefficient-free rest that is not a sum.
Expr scaled(const ExactNumber& c, const Expr& rest) {
  if (c == ExactNumber::integer(1)) return rest;
  Args a;
  a.reserve(1 + rest->args.size() + 1);
  a.push_back(number(c));
  if (rest->kind == Kind::Mul) a.insert(a.end(), rest->args.begin(), rest->args.end());
  else a.push_back(rest);
  return rawNode(Kind::Mul, std::move(a));
}

// The acute angles whose sine and tangent have a short radical form: the
// multiples of 15, 18 and 22.5 degrees (NumWorks Poincare's ExactFormula
// table). Angles are in half-degrees so that 22.5 is whole. The tangents are
// listed, not computed as sin/cos: a quotient of nested radicals such as
// sqrt(2-sqrt(2))/sqrt(2+sqrt(2)) need not reduce to sqrt(2)-1. Built once,
// like the rule tables; tan at 90 degrees is left invalid (a pole).
struct AcuteTrig {
  int halfDegrees;
  ExactNumber sin, tan;
};
using AcuteTable = std::array<AcuteTrig, 13>;
const AcuteTable& acuteTable() {
  static const AcuteTable table = [] {
    const auto n = [](int64_t v) { return ExactNumber::integer(v); };
    const auto f = [](int64_t a, int64_t b) { return ExactNumber::fraction(a, b); };
    const ExactNumber r2 = n(2).sqrt(), r3 = n(3).sqrt(), r5 = n(5).sqrt(), r6 = n(6).sqrt();
    return AcuteTable{{
        {0, n(0), n(0)},
        {30, f(1, 4) * (r6 - r2), n(2) - r3},                                               // 15
        {36, f(1, 4) * (r5 - n(1)), f(1, 5) * (n(25) - n(10) * r5).sqrt()},                 // 18
        {45, f(1, 2) * (n(2) - r2).sqrt(), r2 - n(1)},                                      // 22.5
        {60, f(1, 2), f(1, 3) * r3},                                                        // 30
        {72, f(1, 4) * (n(10) - n(2) * r5).sqrt(), (n(5) - n(2) * r5).sqrt()},              // 36
        {90, f(1, 2) * r2, n(1)},                                                           // 45
        {108, f(1, 4) * (r5 + n(1)), f(1, 5) * (n(25) + n(10) * r5).sqrt()},                // 54
        {120, f(1, 2) * r3, r3},                                                            // 60
        {135, f(1, 2) * (n(2) + r2).sqrt(), r2 + n(1)},                                     // 67.5
        {144, f(1, 4) * (n(10) + n(2) * r5).sqrt(), (n(5) + n(2) * r5).sqrt()},             // 72
        {150, f(1, 4) * (r6 + r2), n(2) + r3},                                              // 75
        {180, n(1), ExactNumber()},                                                         // 90
    }};
  }();
  return table;
}
const AcuteTrig* acuteEntry(int halfDegrees) {
  for (const auto& a : acuteTable())
    if (a.halfDegrees == halfDegrees) return &a;
  return nullptr;
}
// sin, cos or tan of q*pi radians at a table angle; false for any other
// angle. tan at an odd multiple of 90 degrees sets `pole` instead.
bool standardTrig(Fn f, const Rational& q, ExactNumber& out, bool& pole) {
  const Rational half = q * Rational(360);
  if (!half.integer()) return false;
  int k = static_cast<int>(((half.num() % BigInt(720)).toInt64() + 720) % 720);
  pole = false;
  if (f == Fn::Tan) {
    k %= 360;  // period 180 degrees, odd
    const AcuteTrig* a = acuteEntry(k > 180 ? 360 - k : k);
    if (!a) return false;
    if (!a->tan.exact()) { pole = true; return true; }
    out = k > 180 ? -a->tan : a->tan;
    return true;
  }
  if (f == Fn::Cos) k = (k + 180) % 720;  // cos t = sin(t + 90)
  int acute = k > 360 ? 720 - k : k;
  if (acute > 180) acute = 360 - acute;
  const AcuteTrig* a = acuteEntry(acute);
  if (!a) return false;
  out = k > 360 ? -a->sin : a->sin;
  return true;
}
// asin/acos/atan of a table value, as q*pi (the principal angle). Matched by
// value (a zero difference), so any spelling of the radical is found.
bool standardInverse(Fn f, const ExactNumber& x, ExactNumber& out) {
  for (const auto& a : acuteTable()) {
    const ExactNumber& value = f == Fn::Atan ? a.tan : a.sin;
    if (!value.exact()) continue;
    int s = 0;
    if ((x - value).zero()) s = 1;
    else if (!value.zero() && (x + value).zero()) s = -1;
    else continue;
    Rational angle = Rational::of(BigInt(s * a.halfDegrees), BigInt(360));
    if (f == Fn::Acos) angle = Rational::of(BigInt(1), BigInt(2)) - angle;
    out = ExactNumber::rational(angle) * ExactNumber::pi();
    return true;
  }
  return false;
}
bool singleTerm(const ExactNumber& v) {
  const std::string s = v.str();
  return s.find('+', 1) == std::string::npos && s.find('-', 1) == std::string::npos;
}
// f(v) exactly, when v has a short exact image. False keeps f(v) symbolic;
// a certain domain error sets Math.
bool foldFunction(Fn f, const ExactNumber& v, Expr& out) {
  bool certain = false;
  switch (f) {
    case Fn::Sin:
    case Fn::Cos:
    case Fn::Tan: {
      Rational q;
      if (!v.piMultiple(q)) return false;
      ExactNumber r;
      bool pole = false;
      if (!standardTrig(f, q, r, pole)) return false;
      if (pole) { context().fail(Error::Math); out = integer(0); return true; }
      if (!r.exact()) return false;
      out = number(r);
      return true;
    }
    case Fn::Asin:
    case Fn::Acos:
    case Fn::Atan: {
      if (f != Fn::Atan && v.isRational() && Rational::compare(v.rationalValue().abs(), Rational(1)) > 0) {
        context().fail(Error::Math);
        out = integer(0);
        return true;
      }
      ExactNumber angle;
      if (!standardInverse(f, v, angle)) return false;
      out = number(angle);
      return true;
    }
    case Fn::Ln: {
      const int s = v.sign(&certain);
      if (!certain) return false;
      if (s <= 0) { context().fail(Error::Math); out = integer(0); return true; }
      const ExactNumber r = v.ln();
      // ln(8) = 3*ln(2) folds; ln(10) would print as ln(2)+ln(5), so it stays,
      // but a perfect power comes out: ln(10^2022) = 2022*ln(10), so
      // log(10^2022) is 2022.
      if (!r.exact()) return false;
      if (r.isRational() || singleTerm(r)) {
        out = number(r);
        return true;
      }
      if (!v.isRational()) return false;
      // Take prime roots until none is exact: 10^2022 -> 10^1011 -> 10^337 -> 10.
      Rational base = v.rationalValue();
      int64_t power = 1;
      const auto prime = [](uint32_t p) {
        for (uint32_t d = 2; d * d <= p; ++d)
          if (p % d == 0) return false;
        return true;
      };
      for (bool again = true; again && context().step();) {
        again = false;
        const size_t bits = std::max(base.num().bits(), base.den().bits());
        for (uint32_t p = 2; p <= bits; ++p) {
          BigInt n, d;
          if (!prime(p) || !base.num().exactRoot(p, n) || !base.den().exactRoot(p, d)) continue;
          base = Rational::of(n, d);
          power *= p;
          again = true;
          break;
        }
      }
      if (power == 1) return false;
      out = mul(integer(power), fn(Fn::Ln, number(ExactNumber::rational(base))));
      return true;
    }
    case Fn::Abs: {
      const ExactNumber r = v.abs();
      if (!r.exact()) return false;
      out = number(r);
      return true;
    }
    case Fn::Sinh: case Fn::Tanh: case Fn::Asinh: case Fn::Atanh:
      if (!v.zero()) {
        if (f == Fn::Atanh && v.isRational() && Rational::compare(v.rationalValue().abs(), Rational(1)) >= 0) {
          context().fail(Error::Math); out = integer(0); return true;
        }
        return false;
      }
      out = integer(0);
      return true;
    case Fn::Cosh:
      if (!v.zero()) return false;
      out = integer(1);
      return true;
    case Fn::Acosh:
      if (v.isRational() && Rational::compare(v.rationalValue(), Rational(1)) < 0) { context().fail(Error::Math); out = integer(0); return true; }
      if (v != ExactNumber::integer(1)) return false;
      out = integer(0);
      return true;
  }
  return false;
}
// sin/cos/tan of q*pi reduced to 0 <= q <= 1/2, as NumWorks Poincare
// reduces its trig arguments, so one value has one form: cos(4pi/7) =
// -cos(3pi/7), sin(241pi/120) = sin(pi/120), tan(62pi/21) = -tan(pi/21).
// False when q is already reduced.
bool reduceAngle(Fn f, const Rational& q, Expr& out) {
  const Rational half = Rational::of(BigInt(1), BigInt(2));
  const Rational period(f == Fn::Tan ? 1 : 2);
  Rational t = q - Rational((q / period).floor()) * period;  // [0, period)
  int sign = 1;
  if (f == Fn::Tan) {
    if (Rational::compare(t, half) > 0) { t = Rational(1) - t; sign = -1; }
  } else if (f == Fn::Sin) {
    if (Rational::compare(t, Rational(1)) >= 0) { t = t - Rational(1); sign = -1; }
    if (Rational::compare(t, half) > 0) t = Rational(1) - t;
  } else {
    if (Rational::compare(t, Rational(1)) > 0) t = Rational(2) - t;
    if (Rational::compare(t, half) > 0) { t = Rational(1) - t; sign = -1; }
  }
  if (sign > 0 && t == q) return false;
  const Expr reduced = fn(f, number(ExactNumber::rational(t) * ExactNumber::pi()));
  out = sign > 0 ? reduced : neg(reduced);
  return true;
}
// The angle of a reduced sin/cos/tan(q*pi), 0 <= q <= 1/2 (q < 1/2 for tan).
bool reducedAngle(const Expr& u, Fn f, Rational& q) {
  if (u->kind != Kind::Func || u->fn != f || !isNumber(u->args[0]) || !u->args[0]->number.piMultiple(q)) return false;
  const int top = Rational::compare(q, Rational::of(BigInt(1), BigInt(2)));
  return q.sign() >= 0 && (top < 0 || (top == 0 && f != Fn::Tan));
}
bool oddFunction(Fn f) {
  return f == Fn::Sin || f == Fn::Tan || f == Fn::Asin || f == Fn::Atan || f == Fn::Sinh || f == Fn::Tanh ||
         f == Fn::Asinh || f == Fn::Atanh;
}
bool inversePair(Fn outer, Fn inner) {
  return (outer == Fn::Sin && inner == Fn::Asin) || (outer == Fn::Cos && inner == Fn::Acos) ||
         (outer == Fn::Tan && inner == Fn::Atan) || (outer == Fn::Sinh && inner == Fn::Asinh) ||
         (outer == Fn::Cosh && inner == Fn::Acosh) || (outer == Fn::Tanh && inner == Fn::Atanh);
}
// u^a is never negative: an even-numerator rational power, or e^v.
bool nonNegativePower(const Expr& e) {
  if (e->kind != Kind::Pow) return false;
  if (isEuler(e->args[0])) return true;
  Rational a;
  return rationalValue(e->args[1], a) && !a.num().odd();
}
}  // namespace

// ---- Leaves ----------------------------------------------------------------
Expr number(const ExactNumber& v) {
  if (!v.exact()) return failed(v);
  Node n;
  n.kind = Kind::Number;
  n.number = v;
  return finish(std::move(n));
}
Expr integer(int64_t v) {
  Node n;
  n.kind = Kind::Number;
  n.number = ExactNumber::integer(v);
  return finish(std::move(n));
}
Expr fraction(int64_t a, int64_t b) { return number(ExactNumber::fraction(a, b)); }
Expr symbol(const std::string& name) { return leaf(Kind::Symbol, name); }
Expr text(const std::string& message) { return leaf(Kind::Text, message); }
Expr pi() { return number(ExactNumber::pi()); }
Expr euler() { return number(ExactNumber::e()); }

// ---- Parts -----------------------------------------------------------------
bool zero(const Expr& e) { return isNumber(e) && e->number.zero(); }
bool one(const Expr& e) { return isNumber(e) && e->number == ExactNumber::integer(1); }
bool integerValue(const Expr& e, int64_t& v) {
  if (!isNumber(e) || !e->number.isInteger() || !e->number.rationalValue().num().fitsInt64()) return false;
  v = e->number.rationalValue().num().toInt64();
  return true;
}
bool rationalValue(const Expr& e, Rational& v) {
  if (!isNumber(e) || !e->number.isRational()) return false;
  v = e->number.rationalValue();
  return true;
}
bool isEuler(const Expr& e) { return isNumber(e) && e->number == ExactNumber::e(); }
bool negative(const Expr& e) {
  const Expr* c = &e;
  if (e->kind == Kind::Mul && isNumber(e->args[0])) c = &e->args[0];
  if (!isNumber(*c)) return false;
  bool certain = false;
  return (*c)->number.sign(&certain) < 0 && certain;
}
void splitTerm(const Expr& t, ExactNumber& c, Expr& rest) {
  if (isNumber(t)) { c = t->number; rest = integer(1); return; }
  if (t->kind == Kind::Mul && isNumber(t->args[0])) {
    c = t->args[0]->number;
    if (t->args.size() == 2) rest = t->args[1];
    else rest = rawNode(Kind::Mul, Args(t->args.begin() + 1, t->args.end()));
    return;
  }
  c = ExactNumber::integer(1);
  rest = t;
}
void splitPower(const Expr& f, Expr& base, Expr& exponent) {
  if (f->kind == Kind::Pow) { base = f->args[0]; exponent = f->args[1]; return; }
  base = f;
  exponent = integer(1);
}

// ---- Sums ------------------------------------------------------------------
Expr add(Args terms) {
  if (!context().step()) return integer(0);
  Args flat;
  flat.reserve(terms.size());
  for (auto& t : terms) {
    if (t->kind == Kind::Add) flat.insert(flat.end(), t->args.begin(), t->args.end());
    else flat.push_back(std::move(t));
  }
  ExactNumber constant = ExactNumber::integer(0);
  struct Group { Expr rest; ExactNumber coefficient; };
  Vec<Group> groups;
  groups.reserve(flat.size());
  for (const auto& t : flat) {
    if (isNumber(t)) {
      constant = constant + t->number;
      if (!constant.exact()) return failed(constant);
      continue;
    }
    Group g;
    splitTerm(t, g.coefficient, g.rest);
    groups.push_back(std::move(g));
  }
  std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return compare(a.rest, b.rest) < 0; });
  Args out;
  if (!constant.zero()) out.push_back(number(constant));
  for (size_t i = 0; i < groups.size();) {
    ExactNumber c = groups[i].coefficient;
    size_t j = i + 1;
    for (; j < groups.size() && equal(groups[j].rest, groups[i].rest); ++j) c = c + groups[j].coefficient;
    if (!c.exact()) return failed(c);
    if (!c.zero()) out.push_back(scaled(c, groups[i].rest));
    i = j;
  }
  if (out.empty()) return integer(0);
  if (out.size() == 1) return out[0];
  return rawNode(Kind::Add, std::move(out));
}
Expr add(const Expr& a, const Expr& b) { return add(Args{a, b}); }
Expr sub(const Expr& a, const Expr& b) { return add(a, neg(b)); }
Expr neg(const Expr& a) { return mul(integer(-1), a); }

// ---- Products --------------------------------------------------------------
namespace {
Expr multiply(Args factors, bool distribute) {
  if (!context().step()) return integer(0);
  Args flat;
  flat.reserve(factors.size());
  for (auto& f : factors) {
    if (f->kind == Kind::Mul) flat.insert(flat.end(), f->args.begin(), f->args.end());
    else flat.push_back(std::move(f));
  }
  ExactNumber c = ExactNumber::integer(1);
  struct Item { Expr base, exponent, factor; };
  Vec<Item> items;
  items.reserve(flat.size());
  for (const auto& f : flat) {
    if (isNumber(f)) {
      c = c * f->number;
      if (!c.exact()) return failed(c);
      if (c.zero()) return integer(0);
      continue;
    }
    Item it;
    splitPower(f, it.base, it.exponent);
    it.factor = f;
    items.push_back(std::move(it));
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return compare(a.base, b.base) < 0; });
  Args out;
  bool again = false;  // a combined power left a number, a product or a new base
  for (size_t i = 0; i < items.size();) {
    size_t j = i + 1;
    while (j < items.size() && equal(items[j].base, items[i].base)) ++j;
    if (j == i + 1) {
      out.push_back(items[i].factor);
    } else {
      Args exponents;
      for (size_t k = i; k < j; ++k) exponents.push_back(items[k].exponent);
      const Expr p = pow(items[i].base, add(std::move(exponents)));
      if (!context().ok()) return integer(0);
      Expr b, e;
      splitPower(p, b, e);
      if (isNumber(p) || p->kind == Kind::Mul || !equal(b, items[i].base)) again = true;
      if (!one(p)) out.push_back(p);
    }
    i = j;
  }
  if (again) {
    out.push_back(number(c));
    return multiply(std::move(out), distribute);
  }
  if (out.empty()) return number(c);
  if (c == ExactNumber::integer(1) && out.size() == 1) return out[0];
  // A numeric coefficient distributes over a lone sum: 2(x+1) = 2x+2, so
  // like terms meet (2(x+1) - 2x = 2).
  if (distribute && out.size() == 1 && out[0]->kind == Kind::Add) {
    Args terms;
    const Expr k = number(c);
    for (const auto& t : out[0]->args) terms.push_back(mul(k, t));
    return add(std::move(terms));
  }
  if (c != ExactNumber::integer(1)) out.insert(out.begin(), number(c));
  return rawNode(Kind::Mul, std::move(out));
}
}  // namespace
Expr mul(Args factors) { return multiply(std::move(factors), true); }
Expr factored(Args factors) { return multiply(std::move(factors), false); }
Expr mul(const Expr& a, const Expr& b) { return mul(Args{a, b}); }
Expr div(const Expr& a, const Expr& b) { return mul(a, pow(b, integer(-1))); }

// ---- Powers ----------------------------------------------------------------
namespace {
// c*ln(u) as (c, u), c rational.
bool logTerm(const Expr& k, Rational& c, Expr& u) {
  if (k->kind == Kind::Func && k->fn == Fn::Ln) { c = Rational(1); u = k->args[0]; return true; }
  if (k->kind == Kind::Mul && k->args.size() == 2 && rationalValue(k->args[0], c) && k->args[1]->kind == Kind::Func &&
      k->args[1]->fn == Fn::Ln) {
    u = k->args[1]->args[0];
    return true;
  }
  return false;
}
// b^y for a positive number b != e whose exponent holds a logarithm in base b:
// b^(c*ln(u)/ln(b)) = u^c, so 10^log(x) = x and 10^(3+2log(pi)) =
// 1000*pi^2 (log(x) is ln(x)/ln(10)). Each term of y is tried separately;
// nothing changes unless one of them cancels.
bool logExponent(const Expr& b, const Expr& e, Expr& out) {
  bool certain = false;
  if (b->number.sign(&certain) <= 0 || !certain) return false;
  const Expr lnb = fn(Fn::Ln, b);
  const Args terms = e->kind == Kind::Add ? e->args : Args{e};
  Args keep, factors;
  bool cancelled = false;
  for (const auto& t : terms) {
    const Expr k = mul(t, lnb);
    Rational c;
    Expr u;
    if (!isNumber(k) && logTerm(k, c, u)) {
      cancelled = cancelled || !equal(u, b);
      factors.push_back(pow(u, number(ExactNumber::rational(c))));
    } else if (isNumber(k) && !isNumber(t) && k->number.exp().exact()) {
      // A number: 2*log(pi)*ln(10) = 2*ln(pi) folds to pi^2.
      cancelled = true;
      factors.push_back(number(k->number.exp()));
    } else {
      keep.push_back(t);
    }
  }
  if (!cancelled || !context().ok()) return false;
  if (!keep.empty()) factors.push_back(pow(b, add(std::move(keep))));
  out = mul(std::move(factors));
  return true;
}
}  // namespace
Expr pow(const Expr& b, int64_t e) { return pow(b, integer(e)); }
Expr pow(const Expr& b, const Expr& e) {
  if (!context().step()) return integer(0);
  if (isNumber(e)) {
    if (e->number.zero()) {
      if (zero(b)) { context().fail(Error::Math); return integer(0); }
      return integer(1);
    }
    if (one(e)) return b;
  }
  if (isNumber(b)) {
    const ExactNumber& bv = b->number;
    if (bv == ExactNumber::integer(1)) return integer(1);
    if (bv.zero()) {
      if (!isNumber(e)) return rawPow(b, e);
      bool certain = false;
      if (e->number.sign(&certain) > 0 && certain) return integer(0);
      context().fail(Error::Math);
      return integer(0);
    }
    if (isNumber(e)) {
      const ExactNumber r = bv.pow(e->number);
      if (r.exact()) return number(r);
      if (r.failed()) { context().fail(Error::Math); return integer(0); }
      return rawPow(b, e);
    }
    if (isEuler(b)) {
      // e^(ln u) = u and e^(c*ln u) = u^c: exp and ln are inverse on the reals.
      if (e->kind == Kind::Func && e->fn == Fn::Ln) return e->args[0];
      if (e->kind == Kind::Mul && e->args.size() == 2 && isNumber(e->args[0]) && e->args[1]->kind == Kind::Func &&
          e->args[1]->fn == Fn::Ln)
        return pow(e->args[1]->args[0], e->args[0]);
    } else {
      Expr folded;
      if (logExponent(b, e, folded)) return folded;
    }
    return rawPow(b, e);
  }
  Rational q;
  const bool rational = rationalValue(e, q);
  if (b->kind == Kind::Pow) {
    const Expr& u = b->args[0];
    const Expr& a = b->args[1];
    Rational ar;
    if (rational && rationalValue(a, ar)) {
      // (u^a)^q = u^(aq) holds when q is an integer or u^a keeps u's sign (a
      // has an odd numerator). Otherwise u^a = |u|^a: sqrt(x^2) = |x|.
      const Rational product = ar * q;
      if (q.integer() || ar.num().odd() || !product.num().odd()) return pow(u, number(ExactNumber::rational(product)));
      return pow(fn(Fn::Abs, u), number(ExactNumber::rational(product)));
    }
    if (rational && q.integer()) return pow(u, mul(a, e));
  }
  if (b->kind == Kind::Mul) {
    if (rational && q.integer()) {
      Args f;
      for (const auto& x : b->args) f.push_back(pow(x, e));
      return mul(std::move(f));
    }
    // A positive coefficient separates under any power: sqrt(4x) = 2sqrt(x);
    // a negative one only under an odd root: cbrt(-8x) = -2cbrt(x).
    if (rational && isNumber(b->args[0])) {
      bool certain = false;
      const int s = b->args[0]->number.sign(&certain);
      if (certain && (s > 0 || q.den().odd())) {
        const Expr rest = b->args.size() == 2 ? b->args[1] : rawNode(Kind::Mul, Args(b->args.begin() + 1, b->args.end()));
        const Expr coefficient = s > 0 ? b->args[0] : number(-b->args[0]->number);
        const Expr sign = s > 0 || !q.num().odd() ? integer(1) : integer(-1);
        return mul(Args{sign, pow(coefficient, e), pow(rest, e)});
      }
    }
  }
  if (b->kind == Kind::Func && b->fn == Fn::Abs && rational && q.integer() && !q.num().odd()) return pow(b->args[0], e);
  return rawPow(b, e);
}
Expr sqrt(const Expr& a) { return pow(a, fraction(1, 2)); }
Expr exp(const Expr& a) { return pow(euler(), a); }

// ---- Functions -------------------------------------------------------------
Expr fn(Fn f, const Expr& u) {
  if (!context().step()) return integer(0);
  if (isNumber(u)) {
    Expr folded;
    if (foldFunction(f, u->number, folded)) return folded;
    Rational q;
    if ((f == Fn::Sin || f == Fn::Cos || f == Fn::Tan) && u->number.piMultiple(q) && reduceAngle(f, q, folded))
      return folded;
  }
  if (negative(u)) {
    const Expr m = neg(u);
    if (f == Fn::Cos || f == Fn::Cosh || f == Fn::Abs) return fn(f, m);
    if (oddFunction(f)) return neg(fn(f, m));
    // acos(-v) = pi - acos(v), taken only when acos(v) resolves.
    if (f == Fn::Acos) {
      const Expr inner = fn(f, m);
      if (inner->kind != Kind::Func || inner->fn != Fn::Acos) return sub(pi(), inner);
    }
  }
  if (u->kind == Kind::Func) {
    if (inversePair(f, u->fn)) return u->args[0];
    if (f == Fn::Abs && (u->fn == Fn::Abs || u->fn == Fn::Cosh)) return u;
    // sinh and tanh are one-to-one on the reals; cosh is even.
    if ((f == Fn::Asinh && u->fn == Fn::Sinh) || (f == Fn::Atanh && u->fn == Fn::Tanh)) return u->args[0];
    if (f == Fn::Acosh && u->fn == Fn::Cosh) return fn(Fn::Abs, u->args[0]);
    // asin(sin(a)) = a on the principal branch: the reduced angles above.
    Rational q;
    if ((f == Fn::Asin && reducedAngle(u, Fn::Sin, q)) || (f == Fn::Acos && reducedAngle(u, Fn::Cos, q)) ||
        (f == Fn::Atan && reducedAngle(u, Fn::Tan, q)))
      return u->args[0];
  }
  if (f == Fn::Ln && u->kind == Kind::Pow && isEuler(u->args[0])) return u->args[1];
  // ln(b^c) = c*ln(b) for a positive number b: 10^2022 is past the exact
  // bounds and stays a power, but log(10^2022) is 2022.
  if (f == Fn::Ln && u->kind == Kind::Pow && isNumber(u->args[0])) {
    bool certain = false;
    if (u->args[0]->number.sign(&certain) > 0 && certain) return mul(u->args[1], fn(Fn::Ln, u->args[0]));
  }
  if (f == Fn::Abs) {
    if (nonNegativePower(u)) return u;
    if (u->kind == Kind::Mul && isNumber(u->args[0])) {
      const Expr rest = u->args.size() == 2 ? u->args[1] : rawNode(Kind::Mul, Args(u->args.begin() + 1, u->args.end()));
      return mul(number(u->args[0]->number.abs()), fn(Fn::Abs, rest));
    }
  }
  return rawNode(Kind::Func, Args{u}, f);
}

// ---- Structures ------------------------------------------------------------
Expr equation(const Expr& l, const Expr& r) { return rawNode(Kind::Equation, Args{l, r}); }
Expr list(Args items) { return rawNode(Kind::List, std::move(items)); }
Expr integral(const Expr& f, const Expr& x) { return rawNode(Kind::Integral, Args{f, x}); }
Expr solveOf(const Expr& eq, const Expr& x) { return rawNode(Kind::Solve, Args{eq, x}); }

Expr rebuild(const Expr& e, Args a) {
  switch (e->kind) {
    case Kind::Add: return add(std::move(a));
    case Kind::Mul: return mul(std::move(a));
    case Kind::Pow: return pow(a[0], a[1]);
    case Kind::Func: return fn(e->fn, a[0]);
    case Kind::Equation: return equation(a[0], a[1]);
    case Kind::List: return list(std::move(a));
    case Kind::Integral: return integral(a[0], a[1]);
    case Kind::Solve: return solveOf(a[0], a[1]);
    default: return e;
  }
}

Expr substitute(const Expr& e, const Expr& target, const Expr& replacement) {
  if (e->size < target->size) return e;
  if (equal(e, target)) return replacement;
  return mapArgs(e, [&](const Expr& a) { return substitute(a, target, replacement); });
}

}  // namespace Cas
}  // namespace Calculator
