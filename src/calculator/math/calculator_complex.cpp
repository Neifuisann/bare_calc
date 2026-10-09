#include "calculator/math/calculator_complex.h"
#include "calculator/math/calculator_eng_symbols.h"
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace Calculator {
namespace {
struct Complex {
  Value re{0}, im{0};
  bool real() const { return im.isZero(); }
};
Value add(const Value& a, const Value& b) { return realCombine(a, b, '+'); }
Value sub(const Value& a, const Value& b) { return realCombine(a, b, '-'); }
Value mul(const Value& a, const Value& b) { return realCombine(a, b, '*'); }
Value div(const Value& a, const Value& b) { return realCombine(a, b, '/'); }
Value absolute(const Value& a) { return a.number.negative() ? realNegate(a) : a; }
bool nonzeroNegative(const Decimal& d) { return !d.zero() && d.negative(); }
Complex times(const Complex& a, const Complex& b) {
  return {sub(mul(a.re, b.re), mul(a.im, b.im)), add(mul(a.re, b.im), mul(a.im, b.re))};
}
bool finite(const Complex& z) { return z.re.number.finite() && z.im.number.finite(); }

Value fromDegrees(int64_t degrees, Angle unit) {
  if (unit == Angle::Degree) return Value(static_cast<int>(degrees));
  if (unit == Angle::Gradian) return div(Value(static_cast<int>(degrees * 10)), Value(9));
  Value v = Value::of(ExactNumber::fraction(degrees, 180) * ExactNumber::pi());
  v.number = Decimal(static_cast<int>(degrees)) * Decimal::pi() / 180;
  return v;
}
// Exact argument in degrees when both parts are exact and the angle is a
// multiple of 30 or 45 -- recognized from the exact ratio, never by how close
// a decimal happens to land.
bool standardAngle(const Value& re, const Value& im, int64_t& degrees) {
  int64_t acute;
  if (im.number.zero()) acute = 0;
  else if (re.number.zero()) acute = 90;
  else {
    if (!re.isExact() || !im.isExact()) return false;
    const Value ratio = div(absolute(im), absolute(re));
    const Exact e = ratio.exact();
    if (!e.valid || ratio.extra().valid || e.constant() || e.numerator != 1) return false;
    if (e.denominator == 1 && e.radicand == 1) acute = 45;
    else if (e.denominator == 1 && e.radicand == 3) acute = 60;
    else if (e.denominator == 3 && e.radicand == 3) acute = 30;
    else return false;
  }
  const bool left = nonzeroNegative(re.number), down = nonzeroNegative(im.number);
  if (acute == 0) degrees = left ? 180 : 0;  // the negative real axis is +180, never -180
  else if (acute == 90) degrees = down ? -90 : 90;
  else {
    degrees = left ? 180 - acute : acute;
    if (down) degrees = -degrees;
  }
  return true;
}
Complex fromPolar(const Value& r, const Value& theta, Angle unit) {
  return {mul(r, realTrig(false, theta, unit)), mul(r, realTrig(true, theta, unit))};
}

class Evaluator {
 public:
  Evaluator(const Request& request, const std::function<bool()>& stop, const std::function<void()>& yield)
      : q(request), cancel(stop), cooperate(yield), memory(request.memory) {}
  Result run();

 private:
  const Request& q;
  const std::function<bool()>& cancel;
  const std::function<void()>& cooperate;
  Memory memory;  // local bindings: an f/g argument binds x here, never in the result
  Error error = Error::None;
  size_t position = 0;
  uint32_t steps = 0;
  unsigned depth = 0;
  uint8_t functions = 0;

  Complex fail(Error e, size_t p) {
    if (error == Error::None) { error = e; position = p; }
    return {};
  }
  static bool complexCall(const std::string& t) { return t == "conjg" || t == "arg" || t == "rep" || t == "imp"; }
  // Whether this subtree needs complex evaluation. f/g always do: a body may
  // hold i, or its argument may be complex.
  bool complexToken(const Expression& n) const {
    if (!n) return false;
    if (n->kind == Node::Kind::Symbol) {
      if (n->text == "i") return true;
      const int index = variableIndex(n->text);
      if (index >= 0) return memory.variables[index].complex;
      return n->text == "Ans" && memory.ans.complex;
    }
    if (n->kind == Node::Kind::Binary && n->text == "@") return true;
    if (n->kind == Node::Kind::Call && (complexCall(n->text) || n->text == "f" || n->text == "g")) return true;
    for (const auto& child : n->children)
      if (complexToken(child)) return true;
    return false;
  }
  Complex eval(const Expression& n) {
    if (!n || error != Error::None) return {};
    if (cancel && cancel()) return fail(Error::Cancelled, n->position);
    if (++steps > 2000000) return fail(Error::TimeOut, n->position);
    if ((steps & 31) == 0 && cooperate) cooperate();
    if (!complexToken(n)) return real(n);
    if (++depth > 64) { --depth; return fail(Error::Stack, n->position); }
    const Complex z = body(n);
    --depth;
    return checked(z, n->position);
  }
  Complex real(const Expression& n) {
    Request sub = q;
    sub.memory = memory;
    sub.verify = false;
    const Result r = evaluateTree(n, sub, cancel, cooperate);
    if (r.error != Error::None) return fail(r.error, r.position);
    return {r.value, Value(0)};
  }
  Complex checked(Complex z, size_t p) {
    for (Value* v : {&z.re, &z.im}) {
      if (!v->number.finite() || v->number.abs() > Decimal("9.999999999E99")) return fail(Error::Math, p);
      if (!v->number.zero() && v->number.abs() < Decimal("1E-99")) *v = Value(0);
    }
    return z;
  }
  Complex quotient(const Complex& a, const Complex& b, size_t p) {
    const Value denominator = add(mul(b.re, b.re), mul(b.im, b.im));
    if (denominator.number.zero()) return fail(Error::Math, p);
    return {div(add(mul(a.re, b.re), mul(a.im, b.im)), denominator),
            div(sub(mul(a.im, b.re), mul(a.re, b.im)), denominator)};
  }
  // Integer exponents only, -10^10 < n < 10^10 (manual p.132), by repeated
  // squaring: at most 34 squarings, each checked for overflow and cancel.
  Complex power(const Complex& base, const Complex& exponent, size_t p) {
    int64_t n;
    if (!exponent.real() || !exponent.re.number.toInt(n) || n <= -10000000000LL || n >= 10000000000LL)
      return fail(Error::Math, p);
    if (base.re.number.zero() && base.im.number.zero()) return n > 0 ? Complex{} : fail(Error::Math, p);
    Complex result, square = base;
    result.re = Value(1);
    for (uint64_t e = static_cast<uint64_t>(n < 0 ? -n : n); e; e >>= 1) {
      if (cancel && cancel()) return fail(Error::Cancelled, p);
      if (e & 1) result = times(result, square);
      if (e > 1) square = times(square, square);
      if (!finite(result) || !finite(square)) return fail(Error::Math, p);
    }
    if (n >= 0) return result;
    Complex one;
    one.re = Value(1);
    return quotient(one, result, p);
  }
  Complex call(const Expression& n) {
    const std::string& t = n->text;
    const size_t p = n->position;
    const auto& args = n->children;
    if (t == "f" || t == "g") {
      if (args.size() != 1) return fail(Error::Argument, p);
      const uint8_t bit = t == "f" ? 1 : 2;
      if (functions & bit) return fail(Error::Circular, p);
      const std::string& source = t == "f" ? q.memory.f : q.memory.g;
      if (source.empty()) return fail(Error::NotDefined, p);
      const Parsed parsed = parse(source);
      if (parsed.error != Error::None) return fail(parsed.error, p);
      const Complex argument = eval(args[0]);
      if (error != Error::None) return {};
      functions |= bit;
      const ScalarValue saved = memory.variables[6];
      memory.variables[6] = ScalarValue::of(argument.re, argument.im);
      const Complex value = eval(parsed.root);
      memory.variables[6] = saved;
      functions &= ~bit;
      return value;
    }
    if (args.size() == 2 && (t == "pow" || t == "frac")) {
      const Complex a = eval(args[0]), b = eval(args[1]);
      if (error != Error::None) return {};
      return t == "pow" ? power(a, b, p) : quotient(a, b, p);
    }
    if (complexCall(t) || t == "abs") {
      if (args.size() != 1) return fail(Error::Argument, p);
      const Complex z = eval(args[0]);
      if (error != Error::None) return {};
      if (t == "conjg") return {z.re, realNegate(z.im)};
      if (t == "rep") return {z.re, Value(0)};
      if (t == "imp") return {z.im, Value(0)};
      Value r, theta;
      // Arg(0) is undefined: Math ERROR until native behavior is observed (O-5).
      if (!toPolar(z.re, z.im, q.settings.angle, r, theta)) return t == "abs" ? Complex{} : fail(Error::Math, p);
      return {t == "abs" ? r : theta, Value(0)};
    }
    return fail(Error::Math, p);  // no other function takes a complex operand
  }
  Complex body(const Expression& n) {
    const std::string& t = n->text;
    const size_t p = n->position;
    switch (n->kind) {
      case Node::Kind::Symbol: {
        if (t == "i") return {Value(0), Value(1)};
        const int index = variableIndex(t);
        const ScalarValue& v = index >= 0 ? memory.variables[index] : memory.ans;
        return {v.re, v.complex ? v.im : Value(0)};
      }
      case Node::Kind::Unary: {
        const Complex z = eval(n->children[0]);
        if (t == "-") return {realNegate(z.re), realNegate(z.im)};
        if (t == "+") return z;
        if (t == "%") return {div(z.re, Value(100)), div(z.im, Value(100))};
        if (EngSymbol::is(t[0])) {
          const int e = EngSymbol::exponent(t[0]);
          const Value scale(Decimal(("1E" + std::to_string(e < 0 ? -e : e)).c_str()));
          return e < 0 ? Complex{div(z.re, scale), div(z.im, scale)} : Complex{mul(z.re, scale), mul(z.im, scale)};
        }
        return fail(Error::Math, p);  // factorial
      }
      case Node::Kind::Binary: {
        if (t == "@") {
          const Complex r = eval(n->children[0]), theta = eval(n->children[1]);
          if (error != Error::None) return {};
          if (!r.real() || !theta.real()) return fail(Error::Math, p);
          return fromPolar(r.re, theta.re, q.settings.angle);
        }
        if (t != "+" && t != "-" && t != "*" && t != "/" && t != "^") return fail(Error::Syntax, p);
        const Complex a = eval(n->children[0]), b = eval(n->children[1]);
        if (error != Error::None) return {};
        if (t == "+") return {add(a.re, b.re), add(a.im, b.im)};
        if (t == "-") return {sub(a.re, b.re), sub(a.im, b.im)};
        if (t == "*") return times(a, b);
        if (t == "/") return quotient(a, b, p);
        return power(a, b, p);
      }
      case Node::Kind::Call:
        return call(n);
      default:
        return fail(Error::Syntax, p);
    }
  }
};

Result Evaluator::run() {
  Result r;
  r.memory = q.memory;
  const Parsed parsed = parse(q.expression);
  if (parsed.error != Error::None) {
    r.error = parsed.error;
    r.position = parsed.position;
    return r;
  }
  Complex value;
  if (q.verify) {
    std::vector<Expression> operands;
    std::vector<std::string> relations;
    flattenRelations(parsed.root, operands, relations);
    r.error = verifyChainError(relations);
    if (r.error != Error::None) return r;
    bool truth = true;
    Complex left = eval(operands[0]);
    for (size_t i = 0; i < relations.size() && error == Error::None; ++i) {
      const Complex right = eval(operands[i + 1]);
      if (error != Error::None) break;
      const std::string& op = relations[i];
      if (op[0] == '<' || op[0] == '>') {
        // Ordering means nothing for a nonreal operand.
        if (!left.real() || !right.real()) { fail(Error::Math, operands[i + 1]->position); break; }
        const int c = left.re.number.compare(right.re.number);
        truth = truth && (op == "<" ? c < 0 : op == ">" ? c > 0 : op == "<=" ? c <= 0 : c >= 0);
      } else {
        const bool same = left.re.number.compare(right.re.number) == 0 && left.im.number.compare(right.im.number) == 0;
        truth = truth && (op == "!=" ? !same : same);
      }
      left = right;
    }
    value.re = Value(truth ? 1 : 0);
    r.verified = true;
    r.truth = truth;
  } else {
    value = eval(parsed.root);
  }
  if (error != Error::None) {
    r.error = error;
    r.position = position;
    r.verified = r.truth = false;
    return r;
  }
  value.re.number = value.re.number.rounded();
  value.im.number = value.im.number.rounded();
  r.value = value.re;
  r.complex = !value.im.number.zero();
  if (r.complex) r.imag = value.im;
  r.memory.ans = ScalarValue::of(value.re, value.im);
  return r;
}

// A top-level + or - past the sign: the coefficient needs parentheses before i.
bool compound(const std::string& s) {
  int depth = 0;
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i] == '(') ++depth;
    else if (s[i] == ')') --depth;
    else if (!depth && (s[i] == '+' || s[i] == '-') && s[i - 1] != 'E' && s[i - 1] != ',') return true;
  }
  return false;
}
}  // namespace

Result evaluateComplex(const Request& request, const std::function<bool()>& cancelled,
                       const std::function<void()>& cooperate) {
  return Evaluator(request, cancelled, cooperate).run();
}

bool toPolar(const Value& re, const Value& im, Angle unit, Value& r, Value& theta) {
  if (re.number.zero() && im.number.zero()) return false;
  r = realSqrt(add(mul(re, re), mul(im, im)));
  int64_t degrees;
  if (standardAngle(re, im, degrees)) {
    theta = fromDegrees(degrees, unit);
    return true;
  }
  // Both parts nonzero here: standardAngle covers the axes.
  const Decimal pi = Decimal::pi(), x = re.number, y = im.number;
  Decimal t = (y / x).atan();
  if (x.negative()) t = t + (y.negative() ? -pi : pi);
  theta = Value(unit == Angle::Degree ? t * 180 / pi : unit == Angle::Gradian ? t * 200 / pi : t);
  return true;
}

std::string formatComplex(const Value& re, const Value& im, const Settings& settings, Format format,
                          std::string (*present)(const std::string&)) {
  const Format part = format == Format::Rectangular || format == Format::Polar ? Format::Standard : format;
  auto text = [&](const Value& v) {
    const std::string out = formatValue(v, settings, part);
    return present ? present(out) : out;
  };
  const bool polar = format == Format::Polar ||
                     (format != Format::Rectangular && !im.number.zero() && settings.complexResult == ComplexResult::Polar);
  if (polar) {
    Value r, theta;
    if (!toPolar(re, im, settings.angle, r, theta)) return text(Value(0));
    return text(r) + "@" + text(theta);
  }
  const std::string realPart = re.number.zero() ? "" : text(re);
  if (im.number.zero()) return realPart.empty() ? text(Value(0)) : realPart;
  std::string coefficient = text(absolute(im));
  if (coefficient == "1") coefficient.clear();
  else if (compound(coefficient)) coefficient = "(" + coefficient + ")";
  const bool negative = im.number.negative();
  if (realPart.empty()) return (negative ? "-" : "") + coefficient + "i";
  return realPart + (negative ? "-" : "+") + coefficient + "i";
}

}  // namespace Calculator
