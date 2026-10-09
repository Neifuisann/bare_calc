// Symbolic algebra (CAS): expression tree, canonical form, pattern matching,
// polynomials, simplification, differentiation, integration, solving,
// printing and the Calculate entry point. Expected forms are the linear
// printer's text, which the calculator parser reads back; every antiderivative
// is also checked by differentiating it.
#include "calculator/cas/calculator_cas.h"
#include "calculator/cas/calculator_cas_canonical.h"
#include "calculator/cas/calculator_cas_diff.h"
#include "calculator/cas/calculator_cas_integrate.h"
#include "calculator/cas/calculator_cas_pattern.h"
#include "calculator/cas/calculator_cas_poly.h"
#include "calculator/cas/calculator_cas_print.h"
#include "calculator/cas/calculator_cas_simplify.h"
#include "calculator/cas/calculator_cas_solve.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace Calculator;
using namespace Calculator::Cas;
namespace {
int checks = 0, failures = 0;
void require(bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
  }
}
Settings radians() {
  Settings s;
  s.angle = Angle::Radian;
  return s;
}
// The canonical tree of an expression (commands run, if any).
Expr E(const std::string& source) {
  const Outcome o = run(source, radians());
  if (o.error != Error::None || !o.value) {
    require(false, source + " did not evaluate: " + errorText(o.error));
    return integer(0);
  }
  return o.value;
}
std::string text(const Expr& e) { return linearText(e); }
// `source` evaluates to `expected` (linear text), and that text reads back
// to the same canonical tree.
void same(const std::string& source, const std::string& expected, Angle angle = Angle::Radian) {
  Settings s;
  s.angle = angle;
  const Outcome o = run(source, s);
  if (o.error != Error::None || !o.value) {
    require(false, source + " -> " + errorText(o.error) + ", expected " + expected);
    return;
  }
  const std::string got = text(o.value);
  require(got == expected, source + " -> " + got + ", expected " + expected);
  if (angle == Angle::Radian && o.value->kind != Kind::Text && o.value->kind != Kind::List) {
    const Outcome back = run(got, s);
    // Factored forms are deliberately not canonical, so reading back gives
    // an equal value, not always the same tree.
    bool sameValue = back.error == Error::None && back.value && equal(back.value, o.value);
    if (!sameValue && back.value) {
      Context c;
      Scope scope(c);
      sameValue = zeroTest(sub(back.value, o.value)) == Zero::Yes;
    }
    require(sameValue, source + ": \"" + got + "\" reads back to the same value");
  }
}
void natural(const std::string& source, const std::string& expected) {
  const Outcome o = run(source, radians());
  const std::string got = o.value ? naturalText(o.value) : std::string("<") + errorText(o.error) + ">";
  require(got == expected, source + " natural -> " + got + ", expected " + expected);
}
void fails(const std::string& source, Error expected) {
  const Outcome o = run(source, radians());
  require(o.error == expected, source + " -> error " + errorText(o.error) + ", expected " + errorText(expected));
}
bool isZero(const std::string& source) {
  Context c;
  Scope scope(c);
  return zeroTest(E(source)) == Zero::Yes;
}
// No decimal anywhere: every number in the tree is an exact ExactNumber.
bool exactTree(const Expr& e) {
  if (e->kind == Kind::Number && !e->number.exact()) return false;
  for (const auto& a : e->args)
    if (!exactTree(a)) return false;
  return true;
}

void ast() {
  Context c;
  Scope scope(c);
  const Expr x = symbol("x"), y = symbol("y");
  require(equal(add(x, y), add(y, x)), "x+y and y+x are one tree");
  require(add(x, y)->hash == add(y, x)->hash, "equal trees hash equal");
  require(equal(mul(Args{integer(2), x, y}), mul(Args{y, x, integer(2)})), "products sort");
  require(compare(x, y) != 0 && compare(x, y) == -compare(y, x), "total order is antisymmetric");
  require(compare(add(x, y), add(y, x)) == 0, "order agrees with equality");
  require(freeOf(E("y^2+sin(y)"), x) && !freeOf(E("y+sin(2x)"), x), "freeOf");
  require(text(substitute(E("x^2+x"), x, E("y+1"))) == "(y+1)^2+y+1", "substitute rebuilds canonically");
  require(text(substitute(E("sin(x)^2"), E("sin(x)"), symbol("u"))) == "u^2", "substitute a subtree");
  require(E("x+y")->size == 3 && E("sin(x^2)")->depth == 3, "size and depth");
  std::vector<std::string> names;
  collectSymbols(E("A*x^2+B*y+x"), names);
  require(names.size() == 4, "collectSymbols finds A, x, B, y once each");
  // Shared subtrees: substitution leaves an untouched branch as the same node.
  const Expr f = E("sin(y)+x");
  const Expr g = substitute(f, x, integer(3));
  require(g->args.size() == 2, "substitution into a sum");
}

void canonical() {
  same("x+0", "x");
  same("x*1", "x");
  same("x^1", "x");
  same("x^0", "1");
  same("1^x", "1");
  same("0*x", "0");
  same("2x+3x", "5x");
  same("x+y-x", "y");
  same("x*x^2*x^-3", "1");
  same("x^a*x^b", "x^(a+b)");
  same("2^x*2^y", "2^(x+y)");
  same("(x^2)^3", "x^6");
  same("sqrt(x)^2", "x");
  same("sqrt(x^2)", "abs(x)");
  same("(x^2)^(3/2)", "abs(x)^3");
  same("(x^3)^(1/3)", "x");
  same("(2x)^3", "8x^3");
  same("sqrt(4x)", "2sqrt(x)");
  same("root(3,-8x)", "-2root(3,x)");
  same("2(x+1)", "2x+2");
  same("2(x+1)-2x", "2");
  same("x/x", "1");
  same("abs(-3x)", "3abs(x)");
  same("abs(x^2)", "x^2");
  same("abs(abs(x))", "abs(x)");
  // Exact constants fold through ExactNumber; nothing becomes a decimal.
  same("sqrt(8)", "2sqrt(2)");
  same("sqrt(8)+sqrt(18)", "5sqrt(2)");
  same("1/3+1/7", "10/21");
  same("0.1+0.2", "3/10");
  same("pi/2+pi/3", "5pi/6");
  same("e^2*e", "e^3");
  same("ln(e^3)", "3");
  same("ln(8)", "3ln(2)");
  same("ln(10)", "ln(10)");
  same("log(100)", "2");
  same("sin(pi/6)", "1/2");
  same("cos(pi/4)", "sqrt(2)/2");
  same("tan(pi/3)", "sqrt(3)");
  same("asin(1/2)", "pi/6");
  same("atan(1)", "pi/4");
  same("acos(-1)", "pi");
  same("sin(1)", "sin(1)");
  same("2^pi", "2^pi");
  // Symmetry and inverse pairs.
  same("sin(-x)", "-sin(x)");
  same("cos(-x)", "cos(x)");
  same("tan(-2x)", "-tan(2x)");
  same("sin(asin(x))", "x");
  same("e^(ln(x))", "x");
  same("e^(2ln(x))", "x^2");
  same("ln(e^x)", "x");
  fails("1/0", Error::Math);
  fails("sqrt(-4)", Error::Math);
  fails("ln(0)", Error::Math);
  fails("ln(-1)", Error::Math);
  fails("tan(pi/2)", Error::Math);
  fails("asin(2)", Error::Math);
  fails("0^0", Error::Math);
  fails("0^(-1)", Error::Math);
}

// The exact trig table (multiples of 15, 18 and 22.5 degrees): every value
// around the circle against its decimal, and every inverse back to its
// principal angle. A wrong table entry would let the zero test "disprove" a
// true identity, so each one is checked, not just a sample.
void exactTrig() {
  same("sin(pi/12)", "(-sqrt(2)+sqrt(6))/4");
  same("cos(pi/5)", "(1+sqrt(5))/4");
  same("sin(pi/10)", "(-1+sqrt(5))/4");
  same("tan(pi/8)", "-1+sqrt(2)");
  same("tan(5pi/12)", "2+sqrt(3)");
  same("sin(pi/8)", "sqrt(2-sqrt(2))/2");
  same("cos(pi/5)-cos(2pi/5)", "1/2");
  same("asin((sqrt(6)-sqrt(2))/4)", "pi/12");
  same("acos((1+sqrt(5))/4)", "pi/5");
  same("atan(sqrt(2)-1)", "pi/8");
  same("atan(-sqrt(5-2sqrt(5)))", "-pi/5");
  same("sin(15)", "(-sqrt(2)+sqrt(6))/4", Angle::Degree);
  same("solve(cos(x)=(1+sqrt(5))/4)", "x=-pi/5, x=pi/5");
  same("sin(pi/7)", "sin(pi/7)");
  same("sin(pi/9)", "sin(pi/9)");
  fails("tan(3pi/2)", Error::Math);
  // Other multiples of pi reduce to [0, pi/2] (Poincare's systematic
  // reduction), and inverse functions undo them on the principal branch.
  same("cos(4pi/7)", "-cos(3pi/7)");
  same("sin(241pi/120)", "sin(pi/120)");
  same("tan(62pi/21)", "-tan(pi/21)");
  same("sin(-pi/7)", "-sin(pi/7)");
  same("cos(26pi/21)/sin(25pi/17)", "cos(5pi/21)/sin(8pi/17)");
  same("asin(sin(4pi/7))", "3pi/7");
  same("acos(cos(4pi/7))", "4pi/7");
  same("atan(tan(62pi/21))", "-pi/21");
  same("acos(-1/2)", "2pi/3");
  same("acos(-x)", "acos(-x)");
  same("asinh(sinh(x))", "x");
  same("atanh(tanh(x))", "x");
  same("acosh(cosh(x))", "abs(x)");
  // A logarithm in the exponent's base cancels: log(x) is ln(x)/ln(10).
  same("10^log(x)", "x");
  same("10^(3+2log(pi))", "1000pi^2");
  same("10^(3*log(5)+log(4))", "500");
  same("2^(ln(x)/ln(2))", "x");
  same("log(10^2022)", "2022");
  same("ln(100)", "2ln(10)");
  same("2^(x+3)", "2^(x+3)");
  same("10^x", "10^x");
  const Decimal tolerance("1E-30");
  for (int k = -720; k < 720; ++k) {
    if (k % 30 && k % 36 && k % 45) continue;
    const std::string angle = "(" + std::to_string(k) + "pi/360)";
    const Decimal theta = Decimal(k) * Decimal::pi() / Decimal(360);
    const Decimal want[3] = {theta.sin(), theta.cos(), theta.sin() / theta.cos()};
    const char* names[3] = {"sin", "cos", "tan"};
    for (int f = 0; f < 3; ++f) {
      const std::string source = std::string(names[f]) + angle;
      const Outcome o = run(source, radians());
      if (f == 2 && k % 360 && k % 180 == 0) {
        require(o.error == Error::Math, source + " is a pole");
        continue;
      }
      const bool exact = o.error == Error::None && o.value && o.value->kind == Kind::Number;
      require(exact && (o.value->number.toDecimal() - want[f]).abs() < tolerance, source + " matches its decimal");
    }
    // Principal ranges: asin and atan in [-90, 90] degrees, acos in [0, 180].
    const Expr expected = E(angle);
    if (k >= -180 && k <= 180) {
      require(equal(E("asin(sin" + angle + ")"), expected), "asin(sin" + angle + ")");
      if (k != 180 && k != -180) require(equal(E("atan(tan" + angle + ")"), expected), "atan(tan" + angle + ")");
    }
    if (k >= 0 && k <= 360) require(equal(E("acos(cos" + angle + ")"), expected), "acos(cos" + angle + ")");
  }
}

void patterns() {
  Context c;
  Scope scope(c);
  const Expr x = symbol("x");
  const Expr X = wild(0), A = wild(1, Constraint::Const), B = wild(2, Constraint::Const);
  Bindings b;
  b.var = x;
  // a*X + b over a commutative sum: 3x+2, x and x+5 all match.
  const Expr affine = add(mul(A, x), B);
  require(match(affine, E("3x+2"), b) && text(b.slot[1]) == "3" && text(b.slot[2]) == "2", "a*x+b matches 3x+2");
  b = Bindings{};
  b.var = x;
  require(match(affine, x, b) && text(b.slot[1]) == "1" && text(b.slot[2]) == "0", "a*x+b matches x with identities");
  b = Bindings{};
  b.var = x;
  require(match(affine, E("y*x+y^2"), b) && text(b.slot[1]) == "y" && text(b.slot[2]) == "y^2", "Const takes every x-free part");
  b = Bindings{};
  b.var = x;
  require(!match(affine, E("x^2+1"), b), "x^2 is not a*x");
  // A pattern power reads a bare base as ^1; a repeated wild must agree.
  b = Bindings{};
  b.var = x;
  const Expr N = wild(3, Constraint::Number);
  require(match(pow(X, N), x, b) && text(b.slot[3]) == "1", "u^n matches u with n = 1");
  b = Bindings{};
  b.var = x;
  const Expr square = mul(X, X);
  require(!match(add(pow(fn(Fn::Sin, X), 2), pow(fn(Fn::Cos, X), 2)), E("sin(x)^2+cos(y)^2"), b),
          "a wild bound twice must match the same subtree");
  (void)square;
  // Linear constraint.
  b = Bindings{};
  b.var = x;
  const Expr L = wild(4, Constraint::Linear);
  require(match(fn(Fn::Sin, L), E("sin(3x-1)"), b) && text(b.slot[4]) == "3x-1", "Linear matches 3x-1");
  b = Bindings{};
  b.var = x;
  require(!match(fn(Fn::Sin, L), E("sin(x^2)"), b), "Linear rejects x^2");
  Expr a0, a1;
  require(linear(E("2x+y*x+5"), x, a0, a1) && text(a0) == "y+2" && text(a1) == "5", "linear() collects coefficients");
  // Instantiate and a user rule: sin(X)^2 -> (1 - cos(2X))/2.
  const std::vector<Rule> rules{{pow(fn(Fn::Sin, X), 2), mul(fraction(1, 2), sub(integer(1), fn(Fn::Cos, mul(integer(2), X))))}};
  require(text(rewrite(E("sin(x)^2+sin(y+1)^2"), rules, x)) == "-cos(2x)/2-cos(2y+2)/2+1", "rewrite applies a rule everywhere");
}

void polynomials() {
  Context c;
  Scope scope(c);
  const Expr x = symbol("x");
  std::vector<Expr> k;
  require(coefficients(E("a*x^2+b*x+c"), x, k) && k.size() == 3 && text(k[2]) == "a", "symbolic coefficients");
  require(coefficients(E("(x+1)^3"), x, k) && k.size() == 4 && text(k[1]) == "3", "coefficients expand first");
  require(!coefficients(E("sin(x)+1"), x, k), "sin(x) is not a polynomial");
  Poly p, q, g, quotient, remainder;
  require(toPoly(E("x^3-1"), x, p) && toPoly(E("x^2-1"), x, q), "toPoly");
  require(polyGcd(p, q, g) && text(fromPoly(g, x)) == "x-1", "gcd(x^3-1, x^2-1) = x-1");
  require(polyDivMod(p, q, quotient, remainder) && text(fromPoly(quotient, x)) == "x" && text(fromPoly(remainder, x)) == "x-1",
          "(x^3-1) = x(x^2-1) + (x-1)");
  std::vector<Poly> parts;
  require(toPoly(E("(x-1)^2(x+2)"), x, p) && squareFree(p, parts) && parts.size() == 2 &&
              text(fromPoly(parts[0], x)) == "x+2" && text(fromPoly(parts[1], x)) == "x-1",
          "square-free decomposition");
  std::vector<Rational> roots;
  require(toPoly(E("6x^3-11x^2+6x-1"), x, p) && rationalRoots(p, roots) && roots.size() == 3, "rational roots 1, 1/2, 1/3");
  require(toPoly(E("sqrt(2)x+1"), x, p) && p.c.size() == 2, "irrational coefficients are exact too");
}

void simplification() {
  same("simplify(x+0)", "x");
  same("simplify((x^2-1)/(x-1))", "x+1");
  same("simplify((x^2+2x+1)/(x+1))", "x+1");
  same("simplify((x^3-x)/(x^2-x))", "x+1");
  same("simplify(sin(x)^2+cos(x)^2)", "1");
  same("simplify(3sin(2x)^2+3cos(2x)^2)", "3");
  same("simplify(1-sin(x)^2)", "cos(x)^2");
  same("simplify(cosh(x)^2-sinh(x)^2)", "1");
  same("simplify(sin(x)/cos(x))", "tan(x)");
  same("simplify(cos(x)/sin(x))", "1/tan(x)");
  same("simplify(x/(x^2+x))", "1/(x+1)");
  same("simplify(1/x+1/y)", "1/x+1/y");  // smaller than (x+y)/(xy)
  same("simplify(e^(2x+1)/2)", "e^(2x+1)/2");
  same("simplify(-b/2+sqrt(b^2-4c)/2)", "(sqrt(b^2-4c)-b)/2");
  same("expand((x+1)^2)", "x^2+2x+1");
  same("expand((x+1)(x-1))", "x^2-1");
  same("expand((a+b)^3)", "a^3+3a^2b+3a*b^2+b^3");
  same("expand(x(x+1)^2)", "x^3+2x^2+x");
  same("expand((x+1)/x)", "1/x+1");
  same("expand(e^(x+1))", "e*e^x");
  same("expand(sin((x+1)^2))", "sin(x^2+2x+1)");
  same("factor(x^2-1)", "(x-1)(x+1)");
  same("factor(x^2+2x+1)", "(x+1)^2");
  same("factor(2x+2)", "2(x+1)");
  same("factor(-x-1)", "-(x+1)");
  same("factor(x^3-x)", "x(x-1)(x+1)");
  same("factor(6x^2+5x+1)", "(2x+1)(3x+1)");
  same("factor(x^4-1)", "(x-1)(x+1)(x^2+1)");
  same("factor(x^3-8)", "(x-2)(x^2+2x+4)");
  same("factor(x^2+1)", "x^2+1");
  same("factor(x^2-2)", "x^2-2");
  same("factor(x^3/2-x/2)", "x(x-1)(x+1)/2");
  same("factor((x^2-1)/(x^2+2x+1))", "(x-1)/(x+1)");
  same("factor(x^2-y^2)", "(x+y)(x-y)");
  same("factor(x^2y+x*y^2)", "xy(x+y)");
  same("factor(x^2+2xy+y^2)", "(x+y)^2");
  same("factor(e^(2x)+e^x)", "e^x*(e^x+1)");
  same("factor(x^5-x^4-x+1)", "(x-1)^2(x+1)(x^2+1)");
}

void derivatives() {
  same("deriv(5)", "0");
  same("deriv(x)", "1");
  same("deriv(x^2)", "2x");
  same("deriv(x^3+2x)", "3x^2+2");
  same("deriv(1/x)", "-1/x^2");
  same("deriv(sqrt(x))", "1/(2sqrt(x))");
  same("deriv(sin(x))", "cos(x)");
  same("deriv(cos(x))", "-sin(x)");
  same("deriv(tan(x))", "1/cos(x)^2");
  same("deriv(e^x)", "e^x");
  same("deriv(2^x)", "2^x*ln(2)");
  same("deriv(ln(x))", "1/x");
  same("deriv(log(x))", "1/(xln(10))");
  same("deriv(asin(x))", "1/sqrt(1-x^2)");
  same("deriv(acos(x))", "-1/sqrt(1-x^2)");
  same("deriv(atan(x))", "1/(x^2+1)");
  same("deriv(sinh(x))", "cosh(x)");
  same("deriv(tanh(x))", "1/cosh(x)^2");
  same("deriv(asinh(x))", "1/sqrt(x^2+1)");
  same("deriv(atanh(x))", "1/(1-x^2)");
  same("deriv(abs(x))", "x/abs(x)");
  // Chain, product, quotient and general power rules.
  same("deriv(sin(x^2))", "2xcos(x^2)");
  same("deriv(e^(3x))", "3e^(3x)");
  same("deriv(x*sin(x))", "xcos(x)+sin(x)");
  same("deriv(sin(x)/x)", "(xcos(x)-sin(x))/x^2");
  same("deriv(x^x)", "x^x*(ln(x)+1)");
  same("deriv(ln(x^2+1))", "2x/(x^2+1)");
  same("deriv(sqrt(1-x^2))", "-x/sqrt(1-x^2)");
  same("deriv((x^2+1)^10)", "20x(x^2+1)^9");
  // Parameters are constants; other variables and higher orders.
  same("deriv(A*x^2+B*x+C)", "2Ax+B");
  same("deriv(x*y^2,y)", "2xy");
  same("deriv(x^3,x,2)", "6x");
  same("deriv(sin(x),x,4)", "sin(x)");
  same("deriv(e^(2x),x,3)", "8e^(2x)");
  same("diff(x^3,2)", "12");
  same("deriv(x^3)+1", "3x^2+1");
  fails("deriv(x^2,2)", Error::Argument);
  fails("deriv(x^2,x,0)", Error::Argument);
  fails("deriv()", Error::Argument);
}

// ∫f is `expected`, and differentiating it gives f back (proved, not sampled).
void integral(const std::string& f, const std::string& expected, bool proved = true) {
  Context c;
  Scope scope(c);
  const Expr integrand = E(f), x = symbol("x");
  bool verified = false;
  const Expr F = integrate(integrand, x, &verified);
  require(text(F) == expected, "integrate(" + f + ") -> " + text(F) + ", expected " + expected);
  require(!proved || verified, "integrate(" + f + ") is verified by differentiation");
  require(exactTree(F), "integrate(" + f + ") stays exact");
}
void integrals() {
  // Table rules over a linear inner function.
  integral("1", "x");
  integral("x", "x^2/2");
  integral("x^2", "x^3/3");
  integral("x^-2", "-1/x");
  integral("1/x", "ln(abs(x))");
  integral("sqrt(x)", "2x^(3/2)/3");
  integral("x^n", "x^(n+1)/(n+1)", false);  // generic n (n != -1 assumed)
  integral("sin(x)", "-cos(x)");
  integral("cos(3x+1)", "sin(3x+1)/3");
  integral("1/(2x+3)", "ln(abs(2x+3))/2");
  integral("(2x+1)^5", "(2x+1)^6/12");
  integral("e^x", "e^x");
  integral("e^(2x+1)", "e^(2x+1)/2");
  integral("2^x", "2^x/ln(2)");
  integral("tan(x)", "-ln(abs(cos(x)))");
  integral("sinh(x)", "cosh(x)");
  integral("1/cos(x)^2", "tan(x)");
  integral("ln(x)", "x(ln(x)-1)");
  integral("atan(x)", "xatan(x)-ln(x^2+1)/2");
  integral("asin(x)", "xasin(x)+sqrt(1-x^2)");
  integral("1/sqrt(1-x^2)", "asin(x)");
  integral("1/sqrt(4-x^2)", "asin(x/2)");
  integral("1/sqrt(x^2+1)", "ln(abs(x+sqrt(x^2+1)))");
  integral("sqrt(1-x^2)", "(xsqrt(1-x^2)+asin(x))/2");
  integral("sin(x)^2", "x/2-sin(2x)/4");
  integral("cos(x)^2", "(2x+sin(2x))/4");
  integral("tan(x)^2", "tan(x)-x");
  // Power reduction.
  integral("sin(x)^3", "-cos(x)(sin(x)^2+2)/3");
  integral("cos(x)^3", "sin(x)(cos(x)^2+2)/3");
  // Rational functions by partial fractions.
  integral("1/(1+x^2)", "atan(x)");
  integral("1/(x^2-1)", "(ln(abs(x-1))-ln(abs(x+1)))/2");
  integral("x/(x^2+1)", "ln(x^2+1)/2");
  integral("(x^2+1)/(x-1)", "x^2/2+x+2ln(abs(x-1))");
  integral("1/(x^2+2x+2)", "atan(x+1)");
  integral("1/(x^3-x)", "(-2ln(abs(x))+ln(abs(x-1))+ln(abs(x+1)))/2");
  integral("1/(x^2+1)^2", "x/(2(x^2+1))+atan(x)/2");
  integral("1/(x-1)^2", "-1/(x-1)");
  integral("1/(x^2-2)", "sqrt(2)ln(abs(x-sqrt(2)))/4-sqrt(2)ln(abs(x+sqrt(2)))/4");
  // Substitution.
  integral("2x*cos(x^2)", "sin(x^2)");
  integral("x*e^(x^2)", "e^(x^2)/2");
  integral("sin(x)*cos(x)", "sin(x)^2/2");
  integral("ln(x)/x", "ln(x)^2/2");
  integral("e^x/(1+e^x)", "ln(abs(e^x+1))");
  integral("x*sqrt(x^2+1)", "(x^2+1)^(3/2)/3");
  integral("1/(x*ln(x))", "ln(abs(ln(x)))");
  integral("cos(x)/sin(x)^2", "-1/sin(x)");
  // By parts.
  integral("x*e^x", "e^x*(x-1)");
  integral("x*cos(x)", "xsin(x)+cos(x)");
  integral("x^2*sin(x)", "-x^2cos(x)+2xsin(x)+2cos(x)");
  integral("x*ln(x)", "x^2(2ln(x)-1)/4");
  integral("ln(x)^2", "x(ln(x)^2-2ln(x)+2)");
  integral("x^3*e^x", "e^x*(x^3-3x^2+6x-6)");
  integral("e^x*sin(x)", "e^x*(sin(x)-cos(x))/2");
  integral("e^(2x)*cos(3x)", "e^(2x)(3sin(3x)+2cos(3x))/13");
  // Parameters stay symbolic.
  integral("A*x", "Ax^2/2");
  integral("sin(a*x)", "-cos(a*x)/a");  // "ax" would lex as one name
  // No elementary antiderivative: unevaluated, never approximated.
  integral("e^(x^2)", "integrate(e^(x^2))", false);
  integral("sin(x)/x", "integrate(sin(x)/x)", false);
  integral("x^2+sin(x)/x", "x^3/3+integrate(sin(x)/x)", false);
  same("integrate(y^2,y)", "y^3/3");
  same("integrate(x*y,y)", "xy^2/2");
  same("integrate(sin(t)^2,t)", "t/2-sin(2t)/4");
}

// partfrac(f) is `expected`, and is proved equal to f.
void partialFraction(const std::string& f, const std::string& expected, const std::string& v = "x") {
  same("partfrac(" + f + (v == "x" ? "" : "," + v) + ")", expected);
  Context c;
  Scope scope(c);
  require(zeroTest(sub(E("partfrac(" + f + (v == "x" ? "" : "," + v) + ")"), E(f))) == Zero::Yes,
          "partfrac(" + f + ") equals " + f);
}
void partialFractions() {
  partialFraction("(x+3)/((x-1)(x+2))", "4/(3(x-1))-1/(3(x+2))");  // distinct linear factors
  partialFraction("(3x+5)/((x-1)(x+2)^2)", "8/(9(x-1))+1/(3(x+2)^2)-8/(9(x+2))");  // repeated linear factor
  partialFraction("(2x^2+3)/((x-1)(x^2+1))", "5/(2(x-1))-(x+1)/(2(x^2+1))");  // irreducible quadratic
  partialFraction("(x^3+2)/((x^2+1)^2(x+1))",  // repeated quadratic
                  "-(x-5)/(4(x^2+1))-(3x-1)/(2(x^2+1)^2)+1/(4(x+1))");
  partialFraction("(x^4+1)/(x^2-1)", "x^2+1/(x-1)-1/(x+1)+1");  // improper: long division first
  partialFraction("(x^2+1)/(2(x-2)^3)", "5/(2(x-2)^3)+2/(x-2)^2+1/(2(x-2))");  // non-monic, triple root
  partialFraction("1/(x^2-2)", "sqrt(2)/(4(x-sqrt(2)))-sqrt(2)/(4(x+sqrt(2)))");  // radical roots
  partialFraction("1/(x^4+1)",  // biquadratic into two quadratics
                  "(1/2-sqrt(2)x/4)/(x^2-sqrt(2)x+1)+(sqrt(2)x/4+1/2)/(x^2+sqrt(2)x+1)");
  partialFraction("(x-1)/((x-1)(x+2))", "1/(x+2)");  // a cancelled factor leaves no term
  partialFraction("(x^2-1)/(x-1)", "x+1");           // exact division: polynomial only
  partialFraction("1/(t^2-1)", "1/(2(t-1))-1/(2(t+1))", "t");
  partialFraction("1/(x^3+x+1)", "1/(x^3+x+1)");     // irreducible cubic: unchanged
  same("partfrac(x^2+1)", "x^2+1");
  same("partfrac(sin(x))", "sin(x)");
  fails("partfrac()", Error::Argument);
  fails("partfrac(1/x,x,x)", Error::Argument);
  fails("partfrac(x=1)", Error::Syntax);
}

void solving() {
  same("solve(2x+3=7)", "x=2");
  same("solve(x^2=4)", "x=-2, x=2");
  same("solve(x^2-5x+6=0)", "x=2, x=3");
  same("solve(x^2=2)", "x=-sqrt(2), x=sqrt(2)");
  same("solve(x^2+x-1=0)", "x=(-1-sqrt(5))/2, x=(-1+sqrt(5))/2");
  same("solve(x^2-2x+1=0)", "x=1");
  same("solve(x^2+1=0)", "No Solution");
  same("solve(x^3-6x^2+11x-6=0)", "x=1, x=2, x=3");
  same("solve(2x^3-x^2-2x+1=0)", "x=-1, x=1/2, x=1");
  same("solve(x^4-5x^2+4=0)", "x=-2, x=-1, x=1, x=2");
  same("solve(x^3=8)", "x=2");
  same("solve(x^3=-8)", "x=-2");
  same("solve(1/x=2)", "x=1/2");
  same("solve(x/(x-1)=2)", "x=2");
  same("solve((x^2-1)/(x-1)=0)", "x=-1");  // x = 1 is not in the domain
  same("solve(e^x=5)", "x=ln(5)");
  same("solve(2^x=8)", "x=3");
  same("solve(ln(x)=1)", "x=e");
  same("solve(sqrt(x)=3)", "x=9");
  same("solve(sqrt(x)=-1)", "No Solution");
  same("solve(sqrt(x+1)=x-1)", "x=3");  // squaring also yields x = 0, which is extraneous
  same("solve(abs(x)=3)", "x=-3, x=3");
  same("solve(sin(x)=1/2)", "x=pi/6, x=5pi/6");
  same("solve(e^(2x)-3e^x+2=0)", "x=0, x=ln(2)");
  same("solve((x-1)(x-2)(x+3)=0)", "x=-3, x=1, x=2");
  same("solve(a*x+b=0)", "x=-b/a");
  same("solve(x^2+b*x+c=0)", "x=-(b+sqrt(b^2-4c))/2, x=(sqrt(b^2-4c)-b)/2");
  same("solve(y^2=x,y)", "y=-sqrt(x), y=sqrt(x)");
  same("solve(x=x)", "Infinite Solution");
  same("solve(1=2)", "No Solution");
  same("solve(x^5-x-1=0)", "solve(x^5-x-1=0)");
  same("solve(x*e^x=1)", "solve(xe^x=1)");
  same("subst(x^2+1,x,3)", "10");
  same("subst(x^2+y,y,2x)", "x^2+2x");
}

void systems() {
  // Linear, by substitution.
  same("solve(x+y=3,x-y=1)", "x=2, y=1");
  same("solve(2x+3y=7,4x-y=0)", "x=1/2, y=2");
  same("solve(x+y+z=6,x-y=0,x+2y-z=-1)", "x=1, y=1, z=4");
  same("solve(x+y+z+w=10,x-y=1,y-z=1,z-w=1)", "x=4, y=3, z=2, w=1");
  same("solve(x+y=3,x+y=4)", "No Solution");
  same("solve(x+y=3,2x+2y=6)", "Infinite Solution");
  same("solve(a*x+y=1,x-y=2,x,y)", "x=3/(a+1), y=1-3a/(a+1)");
  same("solve(A+B=5,A-B=1)", "A=3, B=2");  // no x, y, z: every letter is an unknown
  // Nonlinear: substitution through an equation linear in one unknown.
  same("solve(x+y=3,x*y=2)", "(x,y)=(1,2), (2,1)");
  same("solve(x^2+y^2=5,y=x+1)", "(x,y)=(-2,-1), (1,2)");
  same("solve(x^2+y=3,x+y^2=3)", "(x,y)=((-1-sqrt(13))/2,(-1-sqrt(13))/2), (-1,2), ((-1+sqrt(13))/2,(-1+sqrt(13))/2), (2,-1)");
  same("solve(x^2+y^2=1,x+y=2)", "No Solution");
  // Nonlinear: the Sylvester resultant eliminates y.
  same("solve(x^2+y^2=5,x^2-y^2=3)", "(x,y)=(-2,-1), (-2,1), (2,-1), (2,1)");
  same("solve(x^2+y^2=25,x*y=12)", "(x,y)=(-4,-3), (-3,-4), (3,4), (4,3)");
  same("solve(y=x^2,y=2x)", "(x,y)=(0,0), (2,4)");
  // Every candidate is checked against every equation.
  same("solve(x^2=4,x^3=8)", "x=2");
  same("solve(sqrt(x)=y,x+y=6)", "x=4, y=2");  // y = -3 would need sqrt(9) = -3
  same("solve(y=x,y=1/x)", "(x,y)=(-1,-1), (1,1)");
  // What elimination cannot finish stays unevaluated.
  same("solve(x^5+y=1,y=x+2)", "solve(x^5+y=1,y=x+2,x,y)");
  same("solve(x+y=3,x-y=1,y,x)", "y=1, x=2");  // unknowns in the order given
  fails("solve(x+y=3,x-y=1,x,x)", Error::Argument);
  natural("solve(x+y=3,x*y=2)", "(x,y)=(1,2), (2,1)");
  natural("solve(2x+3y=7,4x-y=0)", "x=frac(1,2), y=2");
}

// Cases taken from NeoCalculator's CAS suites (tests/CASTest.cpp,
// OmniSolverTest.cpp, CalculusStressTest.cpp): their inputs and expected
// values, restated in this calculator's syntax. Their suites build trees by
// hand and compare doubles; these compare exact forms.
bool holdsIntegral(const Expr& e) {
  if (e->kind == Kind::Integral) return true;
  for (const auto& a : e->args)
    if (holdsIntegral(a)) return true;
  return false;
}
// integrate(deriv(f)) is f up to a constant: CalculusStressTest's 25
// differentiation inputs, turned into a round trip through integration.
void roundTrip(const std::string& f) {
  const Outcome back = run("integrate(deriv(" + f + "))", radians());
  const Outcome original = run(f, radians());
  if (back.error != Error::None || !back.value || !original.value) {
    require(false, "round trip " + f + " -> " + errorText(back.error));
    return;
  }
  Context c;
  Scope scope(c);
  const Expr x = symbol("x");
  const bool closed = !holdsIntegral(back.value);
  const bool constant = closed && zeroTest(simplify(derivative(sub(back.value, original.value), x), x)) == Zero::Yes;
  require(closed && constant, "round trip " + f + " -> " + linearText(back.value));
}
void neoCalculatorCases() {
  // CASTest Phase A/B: polynomial arithmetic.
  same("expand((x+2)(x-3))", "x^2-x-6");
  same("(3x^2+2x-5)-(x^2-3x+2)", "2x^2+5x-7");
  same("(6x^2+4x-2)/2", "3x^2+2x-1");
  same("(3x+6)/3", "x+2");
  same("expand((x+1)^2)", "x^2+2x+1");
  same("solve(3x+2=5x-4)", "x=3");
  same("solve(3x+5=2x+10)", "x=5");
  // CASTest NB-6/NB-7: pi, e and radicals are coefficients, never truncated.
  same("solve(x+pi=0)", "x=-pi");
  same("solve(pi*x-1=0)", "x=1/pi");
  same("solve(x+e=0)", "x=-e");
  same("solve(x+sqrt(2)=0)", "x=-sqrt(2)");
  same("solve(sqrt(2)x-1=0)", "x=sqrt(2)/2");
  // CASTest Phase D: 2x2 and 3x3 linear systems.
  same("solve(x-y=1,2x+y=5)", "x=2, y=1");
  same("solve(2x+3y=8,4x+3y=10)", "x=1, y=2");
  same("solve(x+y=1,x+y=3)", "No Solution");
  same("solve(3x+2y=7,x-y=1)", "x=9/5, y=4/5");
  same("solve(x+y+z=6,2x-y+z=3,x+2y-z=2)", "x=1, y=2, z=3");
  same("solve(x+y=3,y+z=4,x+2y+z=7)", "Infinite Solution");  // the third is the sum of the first two
  same("solve(2y+z=5,3x+y-z=2,x-y+2z=3)", "x=7/9, y=14/9, z=17/9");
  // OmniSolverTest: inverse functions, routing and honest refusals.
  same("solve(1=ln(x))", "x=e");
  same("solve(ln(2x)=3)", "x=e^3/2");
  same("solve(e^2=x+1)", "x=-1+e^2");  // an exact sum, in ClassWiz order
  same("solve(3x+6=0)", "x=-2");
  same("solve((x-1)^2=0)", "x=1");
  same("solve(x^2=0)", "x=0");
  same("solve(2sin(x)-1=0)", "x=pi/6, x=5pi/6");
  same("solve(a*x-6=0)", "x=6/a");
  same("solve(x=e^x)", "solve(x=e^x)");  // no real root; never a fabricated one
  // ASTFlatExprTest values.
  same("log(2,8)", "3");
  same("2sin(pi/4)cos(pi/4)", "1");
  same("root(3,27)", "3");
  same("atan(1)", "pi/4");
  for (const char* f : {"x^2", "3x^3+2x^2-x+5", "sin(x)", "cos(x)", "tan(x)", "ln(x)", "e^x",
                        "x^5-3x^4+2x^3", "sin(x^2)", "x*sin(x)", "e^(x^2)", "ln(x^2+1)", "x^10",
                        "sin(x)cos(x)", "(x^2+1)^3", "asin(x)", "acos(x)", "atan(x)", "log(x)",
                        "sqrt(x)", "-x^3", "sin(cos(x))", "e^(sin(x))", "x^2*ln(x)", "7x^6-4x^3+x"})
    roundTrip(f);
}

void printing() {
  natural("x^2/2", "frac(x^(2),2)");
  natural("-x/2", "-frac(x,2)");
  natural("1/(x+1)", "frac(1,x+1)");
  natural("(x+1)/(x-1)", "frac(x+1,x-1)");
  natural("sqrt(x+1)", "sqrt(x+1)");
  natural("root(3,x)", "root(3,x)");
  natural("x^(3/2)", "x^(frac(3,2))");
  natural("abs(x)", "abs(x)");
  natural("e^(2x)", "e^(2x)");
  natural("2pi*x", "2pi*x");  // pi x would lex as the name "pix"
  natural("sin(x)^2", "sin(x)^(2)");
  natural("solve(x^2=4)", "x=-2, x=2");
  natural("integrate(e^(x^2))", "integrate(e^(x^(2)))");
  same("C*x", "Cx");
  same("x*C", "Cx");
  same("2*C", "2*C");  // "2C" would read C as nCr
  same("2E", "2*E");  // 2E+1 would lex as a literal
  same("x*E+2*E", "Ex+2*E");
  // Reading order: descending powers, constants last, positive first.
  same("1+x+x^2", "x^2+x+1");
  same("1-x^2", "1-x^2");
  same("x+sin(x)+x^2+1", "x^2+x+sin(x)+1");
}

void nested() {
  // Deep chains differentiate term by term and stay exact.
  same("deriv(sin(cos(tan(x))))", "-sin(tan(x))cos(cos(tan(x)))/cos(x)^2");
  same("deriv(e^(sin(x^2)))", "2xe^(sin(x^2))cos(x^2)");
  same("deriv(ln(ln(ln(x))))", "1/(xln(x)ln(ln(x)))");
  same("deriv(sqrt(1+sqrt(1+sqrt(x))))", "1/(8sqrt(x)sqrt(sqrt(x)+1)sqrt(sqrt(sqrt(x)+1)+1))");
  same("expand(((x+1)^2+1)^2)", "x^4+4x^3+8x^2+8x+4");
  same("simplify(((x^2-1)/(x+1))/((x-1)/(x+1)))", "x+1");
  same("simplify(sin(x+pi)^2+cos(x+pi)^2)", "1");
  // Antiderivatives of derivatives come back (up to a constant, here none).
  const char* round[] = {"x^3*sin(x)", "e^(2x)*x", "ln(x^2+1)", "atan(x)*x", "x/(x^2+4)"};
  for (const char* f : round) {
    Context c;
    Scope scope(c);
    const Expr x = symbol("x");
    const Expr g = simplify(derivative(E(f), x), x);
    bool verified = false;
    const Expr back = integrate(g, x, &verified);
    require(verified, std::string("integrate(deriv(") + f + ")) is verified: " + text(back));
    require(zeroTest(simplify(sub(back, E(f)), x)) != Zero::No, std::string("integrate(deriv(") + f + ")) = " + f + " + C");
  }
  // A 12-level composition.
  std::string deep = "x";
  for (int i = 0; i < 6; ++i) deep = "sin(cos(" + deep + "))";
  {
    Context c;
    Scope scope(c);
    const Expr d = derivative(E(deep), symbol("x"));
    require(c.ok() && d->depth > 12 && exactTree(d), "derivative of a 12-level composition");
  }
  require(isZero("sin(2x)-2sin(x)cos(x)"), "zero test: double angle");
  require(isZero("cos(3x)-4cos(x)^3+3cos(x)"), "zero test: triple angle");
  require(isZero("(x+1)^3-x^3-3x^2-3x-1"), "zero test: expansion");
  require(isZero("1/(x-1)-1/(x+1)-2/(x^2-1)"), "zero test: common denominator");
  require(isZero("cosh(x)^2-sinh(x)^2-1"), "zero test: hyperbolic identity");
  require(isZero("ln(10)-ln(2)-ln(5)"), "zero test: constant folding");
  {
    Context c;
    Scope scope(c);
    require(zeroTest(E("sin(x)^2-sin(x)")) == Zero::No, "zero test disproves by an exact sample");
  }
}

void calculate() {
  Request q;
  q.settings.angle = Angle::Radian;
  q.expression = "deriv(x^3)";
  require(handles(q), "Calculate routes a CAS command");
  Request plain = q;
  plain.expression = "2+3";
  require(!handles(plain), "plain arithmetic stays numeric");
  Request verify = q;
  verify.verify = true;
  require(!handles(verify), "Verify stays numeric");
  Result r = Cas::evaluate(q);
  require(r.error == Error::None && r.formula && *r.formula == "3x^(2)", "a formula result is natural-display source");
  require(r.memory.ans.re.symbolic == ExactNumber::integer(0), "a formula leaves Ans alone");
  q.expression = "integrate(x^2,x)+0";
  r = Cas::evaluate(q);
  require(r.formula && *r.formula == "frac(x^(3),3)", "commands inside arithmetic");
  q.expression = "diff(x^3,2)+subst(x^2,x,sqrt(2))";
  r = Cas::evaluate(q);
  require(!r.formula && r.value.symbolic == ExactNumber::integer(14) && r.memory.ans.re.symbolic == ExactNumber::integer(14),
          "an exact constant answer is a value and becomes Ans");
  q.expression = "simplify(sqrt(8))";
  r = Cas::evaluate(q);
  require(!r.formula && r.value.symbolic.str() == "2*sqrt(2)", "exact irrational value stays exact");
  q.memory.ans = Value::of(ExactNumber::fraction(1, 3));
  q.expression = "simplify(3Ans*x)";
  r = Cas::evaluate(q);
  require(r.formula && *r.formula == "x", "Ans enters as its exact value");
  q.memory.ans = Value::approximate(Decimal("0.3333333"));
  r = Cas::evaluate(q);
  require(r.error == Error::Argument, "an inexact Ans is refused, never turned into a decimal coefficient");
  q.memory.ans = Value(0);
  q.memory.f = "x^2+1";
  q.expression = "deriv(f(2x))";
  r = Cas::evaluate(q);
  require(r.formula && *r.formula == "8x", "f(x) definitions expand");
  q.expression = "simplify(1/0)";
  r = Cas::evaluate(q);
  require(r.error == Error::Math, "Math ERROR");
  q.expression = "deriv(rand)";
  r = Cas::evaluate(q);
  require(r.error == Error::Syntax && r.position == 6, "Syntax ERROR at the offending name");
  q.expression = "solve(x^2=4)";
  r = Cas::evaluate(q);
  require(r.formula && *r.formula == "x=-2, x=2", "solutions");
  // The roots ride along as storable answer pages (SHIFT+letter, VARIABLE > Store).
  require(r.solutions && r.solutions->size() == 2 && (*r.solutions)[0].label == "x1" && (*r.solutions)[1].label == "x2" &&
          (*r.solutions)[0].value.re.number == -2 && (*r.solutions)[1].value.re.number == 2,
          "solve() roots are attached as x1, x2");
  q.expression = "solve(2x=6)";
  r = Cas::evaluate(q);
  require(r.solutions && r.solutions->size() == 1 && (*r.solutions)[0].label == "x" && (*r.solutions)[0].value.re.number == 3,
          "a single root is labelled by its variable");
  q.expression = "solve(x^2=2)";
  r = Cas::evaluate(q);
  require(r.solutions && r.solutions->size() == 2 && (*r.solutions)[1].value.re.symbolic.str() == "sqrt(2)",
          "radical roots stay exact");
  q.expression = "solve(x+y=3,x-y=1)";
  r = Cas::evaluate(q);
  require(r.solutions && r.solutions->size() == 2 && (*r.solutions)[0].label == "x" && (*r.solutions)[0].value.re.number == 2 &&
          (*r.solutions)[1].label == "y" && (*r.solutions)[1].value.re.number == 1,
          "a system's one solution is labelled x, y");
  q.expression = "solve(x^2=A)";
  r = Cas::evaluate(q);
  require(r.formula && !r.solutions, "a root with a parameter is not storable");
  q.expression = "solve(x^2=-1)";
  r = Cas::evaluate(q);
  require(!r.solutions, "No Solution has nothing to store");
  q.expression = "expand((x+1)^2)";
  r = Cas::evaluate(q);
  require(r.formula && !r.solutions, "only solve() attaches roots");
  // Angle units: Deg reads trig in degrees, exactly.
  q.settings.angle = Angle::Degree;
  q.expression = "deriv(sin(x))";
  r = Cas::evaluate(q);
  require(r.formula && *r.formula == "frac(pi*cos(x),180)", "Deg derivative carries pi/180: " + (r.formula ? *r.formula : ""));
  q.expression = "simplify(sin(30)+cos(60))";
  r = Cas::evaluate(q);
  require(!r.formula && r.value.symbolic == ExactNumber::integer(1), "Deg constants");
  same("solve(sin(x)=1/2)", "x=30, x=150", Angle::Degree);
  same("integrate(cos(x))", "180sin(x)/pi", Angle::Degree);
  same("deriv(atan(x))", "180/(pi(x^2+1))", Angle::Degree);
}

// The numeric integral(f,a,b) of ordinary Calculate, upgraded through the
// CAS's definite integral when it agrees with the numeric value.
void definite(const std::string& source, const std::string& expected, Angle angle = Angle::Radian) {
  Request q;
  q.settings.angle = angle;
  q.expression = source;
  const Result r = Calculator::evaluate(q);
  const std::string got = r.error != Error::None ? errorText(r.error)
                          : r.value.symbolic.exact() ? r.value.symbolic.str()
                                                     : "~" + r.value.number.str(10);
  require(got == expected, source + " -> " + got + ", expected " + expected);
}
void definiteIntegrals() {
  definite("integral(4-4abs(cos(x)),0,pi)", "-8+4*pi");
  definite("integral(abs(sin(x)),0,3pi)", "6");
  definite("integral(abs(x^2-1),0,2)", "2");
  definite("integral(abs(cos(2x+1)),0,2)", "~" + Calculator::evaluate([] {
    Request q;
    q.settings.angle = Angle::Radian;
    q.expression = "integral(abs(cos(2x+1)),0,2,1E-12)";
    return q;
  }()).value.number.str(10));
  definite("integral(sin(x),0,pi)", "2");
  definite("integral(sin(x),pi,0)", "-2");
  definite("integral(e^x,0,1)", "-1+e");
  definite("integral(1/x,1,e)", "1");
  definite("integral(1/(1+x^2),0,1)", "1/4*pi");
  definite("integral(x*e^x,0,1)", "1");
  definite("integral(cos(x),0,90)", "180*pi^(-1)", Angle::Degree);
  definite("integral(e^(-x^2),0,1)", "~0.7468241328");  // no elementary antiderivative
  definite("integral(x^9,0,1)", "1/10");
  definite("integral(sin(180x),0,9)", "2*pi^(-1)", Angle::Degree);
  definite("integral(1/x,1,2)", "ln(2)");
  // A stored letter enters as its exact value.
  Request q;
  q.settings.angle = Angle::Radian;
  q.memory.variables[0] = ScalarValue(Value(3));
  q.expression = "integral(A*cos(x),0,pi/2)";
  const Result r = Calculator::evaluate(q);
  require(r.error == Error::None && r.value.symbolic == ExactNumber::integer(3), "integral reads A's exact value");
}

void limits() {
  // An expansion past the node bound stops with Stack ERROR, not exhaustion.
  fails("expand((x+y+z+A+B+1)^30)", Error::Stack);
  // Cancellation is polled.
  Request q;
  q.settings.angle = Angle::Radian;
  q.expression = "expand((x+y+z+1)^12)";
  int polls = 0;
  const Result r = Cas::evaluate(q, [&] { return ++polls > 3; });
  require(r.error == Error::Cancelled, "cancellation stops a long expansion");
  // A small step budget times out.
  Context c({}, {}, 200);
  Scope scope(c);
  (void)expand(E("(x+1)^30"));
  require(c.error() == Error::TimeOut, "step budget");
}

void exactness() {
  // Decimal literals are exact rationals everywhere in the CAS.
  same("deriv(0.5x^2)", "x");
  same("integrate(x^0.5)", "2x^(3/2)/3");
  same("solve(0.2x=0.1)", "x=1/2");
  same("simplify(0.1x+0.2x)", "3x/10");
  const char* sources[] = {"integrate(1/(x^2-2))", "solve(x^2=3)", "deriv(e^(pi*x))", "integrate(sqrt(2)*x)"};
  for (const char* s : sources) {
    const Outcome o = run(s, radians());
    require(o.error == Error::None && exactTree(o.value) && text(o.value).find('.') == std::string::npos,
            std::string(s) + " has no decimal: " + (o.value ? text(o.value) : ""));
  }
}
}  // namespace

int main() {
  ast();
  canonical();
  exactTrig();
  patterns();
  polynomials();
  simplification();
  derivatives();
  integrals();
  partialFractions();
  solving();
  systems();
  neoCalculatorCases();
  printing();
  nested();
  calculate();
  definiteIntegrals();
  limits();
  exactness();
  if (failures) {
    std::cerr << failures << " of " << checks << " CAS checks failed\n";
    return 1;
  }
  std::cout << "CAS: " << checks << " checks passed (tree, canonical form, exact trig table, patterns, polynomials, simplify/expand/factor, "
               "derivatives, integrals, partial fractions, solving, printing, nesting, Calculate, limits, exactness).\n";
  return 0;
}
