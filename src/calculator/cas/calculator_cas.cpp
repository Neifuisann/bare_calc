#include "calculator/cas/calculator_cas.h"
#include "calculator/cas/calculator_cas_canonical.h"
#include "calculator/cas/calculator_cas_diff.h"
#include "calculator/cas/calculator_cas_integrate.h"
#include "calculator/cas/calculator_cas_partfrac.h"
#include "calculator/cas/calculator_cas_print.h"
#include "calculator/cas/calculator_cas_simplify.h"
#include "calculator/cas/calculator_cas_solve.h"
#include "calculator/assets/calculator_constants.h"
#include "calculator/math/calculator_eng_symbols.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Calculator {
namespace Cas {
namespace {
constexpr size_t kMaxFormula = 4000;  // natural-display source bytes
constexpr int64_t kMaxOrder = 9;      // deriv(f, x, n)

const char* const kCommands[] = {"simplify", "expand", "factor", "deriv", "integrate", "partfrac", "solve", "subst"};
bool isCommand(const std::string& name) {
  for (const char* c : kCommands)
    if (name == c) return true;
  return false;
}
bool containsCommand(const Expression& n) {
  if (!n) return false;
  if (n->kind == Calculator::Node::Kind::Call && isCommand(n->text)) return true;
  for (const auto& c : n->children)
    if (containsCommand(c)) return true;
  return false;
}

// One radian in the angle unit's terms: sin(u) in Deg is sin(u*pi/180).
Expr angleScale(Angle unit) {
  if (unit == Angle::Degree) return div(pi(), integer(180));
  if (unit == Angle::Gradian) return div(pi(), integer(200));
  return integer(1);
}
// log_b(a) = ln(a)/ln(b), folded when both are numbers with a rational quotient
// (log(100) = 2) -- ln(100) alone stays symbolic.
Expr logarithm(const Expr& a, const Expr& b) {
  if (isNumber(a) && isNumber(b)) {
    const ExactNumber r = a->number.log(b->number);
    if (r.failed()) { context().fail(Error::Math); return integer(0); }
    if (r.isRational()) return number(r);
  }
  return div(fn(Fn::Ln, a), fn(Fn::Ln, b));
}
bool trig(Fn f) { return f == Fn::Sin || f == Fn::Cos || f == Fn::Tan; }
bool inverseTrig(Fn f) { return f == Fn::Asin || f == Fn::Acos || f == Fn::Atan; }
Expr rawFunc(Fn f, const Expr& u) {
  Node n;
  n.kind = Kind::Func;
  n.fn = f;
  n.args = Args{u};
  return finish(std::move(n));
}
// Internally every angle is in radians. For display in Deg/Grad each trig
// function reads its argument back in the unit, and each inverse function's
// radians become scale*asin(v) with asin read in the unit. The rebuilt
// functions are raw: canonical folding would read their arguments as radians.
Expr exportAngles(const Expr& e, const Expr& scale) {
  const Expr t = mapArgs(e, [&](const Expr& a) { return exportAngles(a, scale); });
  if (t->kind != Kind::Func) return t;
  if (trig(t->fn)) return rawFunc(t->fn, div(t->args[0], scale));
  if (inverseTrig(t->fn)) return mul(scale, t);
  return t;
}

class Importer {
 public:
  Importer(const Settings& settings, const Memory& memory)
      : settings_(settings), memory_(memory), scale_(angleScale(settings.angle)), x_(symbol("x")) {}
  Expr run(const Expression& root) { return convert(root, 0); }
  Error error = Error::None;
  size_t position = 0;

 private:
  const Settings& settings_;
  const Memory& memory_;
  Expr scale_, x_;
  uint8_t functions_ = 0;  // f/g currently being expanded (Circular ERROR)

  Expr fail(Error e, size_t p) {
    if (error == Error::None) { error = e; position = p; }
    return integer(0);
  }
  bool stopped() const { return error != Error::None || !context().ok(); }
  Expr exactValue(const Value& v, size_t p) {
    if (!v.symbolic.exact()) return fail(Error::Argument, p);  // a decimal would enter the algebra
    return number(v.symbolic);
  }
  Expr convert(const Expression& n, unsigned depth);
  Expr call(const Expression& n, unsigned depth);
  Expr command(const Expression& n, unsigned depth);
  Expr solveCommand(const Expression& n, unsigned depth);
  bool variable(const Expression& n, Expr& out) {
    if (!n || n->kind != Calculator::Node::Kind::Symbol || n->text == "pi" || n->text == "e" || n->text == "Ans" || n->text == "PreAns")
      return false;
    out = symbol(n->text);
    return true;
  }
};

Expr Importer::convert(const Expression& n, unsigned depth) {
  if (!n) return fail(Error::Syntax, 0);
  if (stopped()) return integer(0);
  if (depth > 40) return fail(Error::Stack, n->position);
  const std::string& t = n->text;
  switch (n->kind) {
    case Calculator::Node::Kind::Number: {
      const ExactNumber v = ExactNumber::fromLiteral(t);
      return v.exact() ? number(v) : fail(Error::Syntax, n->position);
    }
    case Calculator::Node::Kind::Symbol:
      if (t == "pi") return pi();
      if (t == "e") return euler();
      if (t == "Ans") {
        if (memory_.ans.complex) return fail(Error::Math, n->position);
        return exactValue(memory_.ans.re, n->position);
      }
      if (t == "PreAns") return exactValue(memory_.preAns, n->position);
      // A-F, x, y, z; any other single letter reads as a parameter too.
      if (variableIndex(t) >= 0 || (t.size() == 1 && std::isalpha(static_cast<unsigned char>(t[0])))) return symbol(t);
      return fail(Error::Syntax, n->position);
    case Calculator::Node::Kind::Unary: {
      const Expr a = convert(n->children[0], depth + 1);
      if (t == "-") return neg(a);
      if (t == "%") return div(a, integer(100));
      if (EngSymbol::is(t[0])) return mul(a, pow(integer(10), static_cast<int64_t>(EngSymbol::exponent(t[0]))));
      if (t == "!") {
        int64_t k = 0;
        if (!integerValue(a, k) || k < 0 || k > 69) return fail(Error::Argument, n->position);
        ExactNumber product = ExactNumber::integer(1);
        for (int64_t i = 2; i <= k; ++i) product = product * ExactNumber::integer(i);
        return number(product);
      }
      return a;
    }
    case Calculator::Node::Kind::Binary: {
      const Expr a = convert(n->children[0], depth + 1), b = convert(n->children[1], depth + 1);
      if (t == "+") return add(a, b);
      if (t == "-") return sub(a, b);
      if (t == "*") return mul(a, b);
      if (t == "/") return div(a, b);
      if (t == "^") return pow(a, b);
      if (t == "=") return equation(a, b);
      if (t == "P" || t == "C") {
        int64_t total = 0, k = 0;
        if (!integerValue(a, total) || !integerValue(b, k) || k < 0 || total < k || total > 1000) return fail(Error::Argument, n->position);
        if (t == "C") k = std::min(k, total - k);
        ExactNumber v = ExactNumber::integer(1);
        for (int64_t j = 1; j <= k; ++j) {
          v = v * ExactNumber::integer(total - j + 1);
          if (t == "C") v = v / ExactNumber::integer(j);
        }
        return number(v);
      }
      return fail(Error::Syntax, n->position);  // other relations, polar @, dot product
    }
    case Calculator::Node::Kind::Call: return call(n, depth);
  }
  return fail(Error::Syntax, n->position);
}

Expr Importer::call(const Expression& n, unsigned depth) {
  const std::string& t = n->text;
  const auto& c = n->children;
  const size_t p = n->position;
  if (isCommand(t)) return command(n, depth);
  if ((t == "f" || t == "g") && c.size() == 1) {
    const uint8_t bit = t == "f" ? 1 : 2;
    if (functions_ & bit) return fail(Error::Circular, p);
    const std::string& source = t == "f" ? memory_.f : memory_.g;
    if (source.empty()) return fail(Error::NotDefined, p);
    const Parsed parsed = parse(source);
    if (parsed.error != Error::None) return fail(parsed.error, p);
    const Expr argument = convert(c[0], depth + 1);
    functions_ |= bit;
    const Expr body = convert(parsed.root, depth + 1);
    functions_ &= static_cast<uint8_t>(~bit);
    return substitute(body, x_, argument);
  }
  if (t == "constant" && c.size() == 1) {
    int64_t index = -1;
    const Expr i = convert(c[0], depth + 1);
    if (!integerValue(i, index) || index < 0 || index >= static_cast<int64_t>(sizeof(kConstants) / sizeof(kConstants[0])))
      return fail(Error::Argument, p);
    return convert(parse(kConstants[index].expression).root, depth + 1);
  }
  std::vector<Expr> a;
  for (const auto& child : c) a.push_back(convert(child, depth + 1));
  if (stopped()) return integer(0);
  if (t == "diff" && (a.size() == 2 || a.size() == 3)) {
    // The d/dx template at a point: exact from the symbolic derivative.
    return simplify(substitute(derivative(a[0], x_), x_, a[1]), x_);
  }
  if (t == "frac" && a.size() == 2) return div(a[0], a[1]);
  if (t == "pow" && a.size() == 2) return pow(a[0], a[1]);
  if (t == "mixed" && a.size() == 3) return add(a[0], div(a[1], a[2]));
  if (t == "root" && a.size() == 2) return pow(a[1], pow(a[0], integer(-1)));
  if (t == "log" && a.size() == 2) return logarithm(a[1], a[0]);
  if (a.size() != 1) return fail(t == "log" || t == "sqrt" || t == "abs" ? Error::Argument : Error::Syntax, p);
  static const struct { const char* name; Fn fn; } kFunctions[] = {
      {"sin", Fn::Sin}, {"cos", Fn::Cos}, {"tan", Fn::Tan}, {"asin", Fn::Asin}, {"acos", Fn::Acos},
      {"atan", Fn::Atan}, {"sinh", Fn::Sinh}, {"cosh", Fn::Cosh}, {"tanh", Fn::Tanh}, {"asinh", Fn::Asinh},
      {"acosh", Fn::Acosh}, {"atanh", Fn::Atanh}, {"ln", Fn::Ln}, {"abs", Fn::Abs}};
  if (t == "sqrt") return sqrt(a[0]);
  if (t == "exp") return exp(a[0]);
  if (t == "log") return logarithm(a[0], integer(10));
  for (const auto& f : kFunctions) {
    if (t != f.name) continue;
    if (trig(f.fn)) return fn(f.fn, mul(scale_, a[0]));
    if (inverseTrig(f.fn)) return div(fn(f.fn, a[0]), scale_);
    return fn(f.fn, a[0]);
  }
  return fail(Error::Syntax, p);  // rand, gcd, int, numeric integral/sum/product...
}

Expr Importer::command(const Expression& n, unsigned depth) {
  const std::string& t = n->text;
  const auto& c = n->children;
  const size_t p = n->position;
  if (t == "solve") return solveCommand(n, depth);
  const size_t minimum = t == "subst" ? 3 : 1;
  const size_t maximum = t == "subst" || t == "deriv" ? 3 : t == "integrate" || t == "partfrac" ? 2 : 1;
  if (c.size() < minimum || c.size() > maximum) return fail(Error::Argument, p);
  Expr var = x_;
  if (c.size() >= 2 && !variable(c[1], var)) return fail(Error::Argument, c[1] ? c[1]->position : p);
  const Expr body = convert(c[0], depth + 1);
  if (stopped()) return integer(0);
  if (body->kind == Kind::Equation && t != "solve" && t != "subst" && t != "expand" && t != "factor" && t != "simplify")
    return fail(Error::Syntax, p);
  if (t == "simplify") return simplify(body, x_);
  if (t == "expand") return expand(body);
  if (t == "factor") return factor(body, x_);
  if (t == "integrate") return integrate(body, var);
  if (t == "partfrac") return partfrac(body, var);
  if (t == "subst") return substitute(body, var, convert(c[2], depth + 1));
  // deriv
  int64_t order = 1;
  if (c.size() == 3 && (!integerValue(convert(c[2], depth + 1), order) || order < 1 || order > kMaxOrder))
    return fail(Error::Argument, c[2]->position);
  Expr d = body;
  for (int64_t i = 0; i < order && context().ok(); ++i) d = simplify(derivative(d, var), var);
  return d;
}

// solve(f=g[,v]), or a system: solve(f1=g1,f2=g2[,...][,v1,v2,...]). The
// arguments after the first are equations up to the first bare variable,
// then unknowns (so a lone `y` there is an unknown, never y = 0). A system
// without unknowns solves for the x, y, z it holds, then for other letters
// alphabetically while there are fewer unknowns than equations; the rest are
// parameters.
Expr Importer::solveCommand(const Expression& n, unsigned depth) {
  const auto& c = n->children;
  const size_t p = n->position;
  Expr v;
  size_t equations = 1;
  while (equations < c.size() && !variable(c[equations], v)) ++equations;
  if (c.empty() || equations > kMaxSystem || c.size() - equations > kMaxSystem) return fail(Error::Argument, p);
  Args eqs, vars;
  for (size_t i = 0; i < equations; ++i) eqs.push_back(convert(c[i], depth + 1));
  for (size_t i = equations; i < c.size(); ++i) {
    bool repeated = !variable(c[i], v);
    for (const auto& u : vars) repeated = repeated || equal(u, v);
    if (repeated) return fail(Error::Argument, c[i] ? c[i]->position : p);
    vars.push_back(v);
  }
  if (stopped()) return integer(0);
  if (equations == 1 && vars.size() <= 1) return solve(eqs[0], vars.empty() ? x_ : vars[0]);
  if (vars.empty()) {
    std::vector<std::string> names;
    for (const auto& e : eqs) collectSymbols(e, names);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
      const bool ax = a == "x" || a == "y" || a == "z", bx = b == "x" || b == "y" || b == "z";
      return ax != bx ? ax : a < b;
    });
    for (const auto& name : names)
      if (vars.size() < equations || name == "x" || name == "y" || name == "z") vars.push_back(symbol(name));
    if (vars.size() > kMaxSystem) return fail(Error::Argument, p);
  }
  return solveSystem(eqs, vars);
}

Outcome runIn(const std::string& source, const Settings& settings, const Memory& memory) {
  Outcome out;
  const Parsed parsed = parse(source);
  if (parsed.error != Error::None) {
    out.error = parsed.error;
    out.position = parsed.position;
    return out;
  }
  Importer importer(settings, memory);
  Expr e = importer.run(parsed.root);
  if (importer.error != Error::None) {
    out.error = importer.error;
    out.position = importer.position;
    return out;
  }
  if (!context().ok()) {
    out.error = context().error();
    return out;
  }
  if (settings.angle != Angle::Radian) e = exportAngles(e, angleScale(settings.angle));
  out.value = e;
  if (!context().ok()) out.error = context().error();
  return out;
}

// The engine's exact upgrade of a numeric integral(f, a, b) (setExactIntegral).
// x is the variable; any other letter is its stored exact value, as the
// numeric evaluation reads it. A speculative attempt: its own small budget,
// and every failure (an integrand the CAS cannot read, Time Out) is just no
// exact result -- the numeric value stands.
constexpr uint32_t kDefiniteSteps = 300000;
bool exactIntegral(const Expression& integrand, const ExactNumber& low, const ExactNumber& high,
                   const Settings& settings, const Memory& memory, const std::function<bool()>& cancelled,
                   const std::function<void()>& cooperate, ExactNumber& out) {
  Context c(cancelled, cooperate, kDefiniteSteps);
  Scope scope(c);
  Importer importer(settings, memory);
  Expr f = importer.run(integrand);
  if (importer.error != Error::None || !c.ok()) return false;
  std::vector<std::string> names;
  collectSymbols(f, names);
  for (const auto& name : names) {
    if (name == "x") continue;
    const int index = variableIndex(name);
    if (index < 0 || memory.variables[index].complex || !memory.variables[index].re.symbolic.exact()) return false;
    f = substitute(f, symbol(name), number(memory.variables[index].re.symbolic));
  }
  const Expr r = definiteIntegral(f, symbol("x"), number(low), number(high));
  if (!r || !c.ok()) return false;
  out = r->number;
  return true;
}
[[maybe_unused]] const bool kExactIntegralInstalled = (setExactIntegral(exactIntegral), true);

// An exact constant as the answer Value Calculate shows: Math past the
// display range, a denormal-small value as 0, rounded like any result.
bool answerValue(const ExactNumber& exact, Value& out) {
  Value v = Value::of(exact);
  if (!v.number.finite() || v.number.abs() > Decimal("9.999999999E99")) return false;
  if (!v.number.zero() && v.number.abs() < Decimal("1E-99")) v = Value(0);
  v.number = v.number.rounded();
  out = v;
  return true;
}

// `v = root` with an exact numeric root, as one storable answer page.
bool rootPage(const Expr& e, Solution& page) {
  if (!e || e->kind != Kind::Equation || e->args.size() != 2) return false;
  const Expr& v = e->args[0];
  const Expr& root = e->args[1];
  if (v->kind != Kind::Symbol || !isNumber(root)) return false;
  Value value;
  if (!answerValue(root->number, value)) return false;
  page.label = v->name;
  page.value = ScalarValue(value);
  return true;
}

// solve()'s answer as Result::solutions, so Calculate can store a root into
// a variable (SHIFT+letter, VARIABLE > Store). A List of `x = root` (one
// equation's roots, or one solution of a system) or a List of such Lists
// (several solutions). Every root must be an exact number: one with a
// parameter (x = a) or a non-exact constant (sin(1)) withholds the whole
// list, so the picker never skips a root and renumbers the rest.
// Labels: x1, x2... for repeated roots of one unknown, x/y for one system
// solution, x1/y1, x2/y2... for several.
std::shared_ptr<const std::vector<Solution>> solveRoots(const Expr& answer) {
  if (!answer || answer->kind != Kind::List || answer->args.empty()) return nullptr;
  auto pages = std::make_shared<std::vector<Solution>>();
  const bool nested = answer->args[0]->kind == Kind::List;
  for (size_t i = 0; i < answer->args.size(); ++i) {
    const Expr& item = answer->args[i];
    if (nested) {
      if (item->kind != Kind::List || item->args.empty()) return nullptr;
      for (const auto& e : item->args) {
        Solution page;
        if (!rootPage(e, page)) return nullptr;
        page.label += std::to_string(i + 1);
        pages->push_back(std::move(page));
      }
    } else {
      Solution page;
      if (!rootPage(item, page)) return nullptr;
      pages->push_back(std::move(page));
    }
  }
  if (!nested && pages->size() > 1) {
    bool same = true;
    for (const auto& page : *pages) same = same && page.label == pages->front().label;
    if (same)
      for (size_t i = 0; i < pages->size(); ++i) (*pages)[i].label += std::to_string(i + 1);
  }
  return pages;
}
}  // namespace

bool handles(const Request& request) {
  if (request.verify) return false;
  const Parsed parsed = parse(request.expression);
  return parsed.error == Error::None && containsCommand(parsed.root);
}

Outcome run(const std::string& source, const Settings& settings, const Memory& memory,
            const std::function<bool()>& cancelled, const std::function<void()>& cooperate) {
  Context c(cancelled, cooperate);
  Scope scope(c);
  return runIn(source, settings, memory);
}

Result evaluate(const Request& q, const std::function<bool()>& cancelled, const std::function<void()>& cooperate) {
  Result r;
  r.memory = q.memory;
  Context c(cancelled, cooperate);
  Scope scope(c);
  const Outcome out = runIn(q.expression, q.settings, q.memory);
  if (out.error != Error::None) {
    r.error = out.error;
    r.position = out.position;
    return r;
  }
  if (isNumber(out.value)) {
    // An exact constant is an ordinary answer.
    Value v;
    if (!answerValue(out.value->number, v)) {
      r.error = Error::Math;
      return r;
    }
    r.memory.preAns = q.memory.ans.complex ? Value(0) : q.memory.ans.re;
    r.memory.ans = v;
    r.value = v;
    return r;
  }
  std::string text = naturalText(out.value);
  if (!c.ok()) {
    r.error = c.error();
    return r;
  }
  if (text.size() > kMaxFormula) {
    r.error = Error::Stack;
    return r;
  }
  r.value = Value(0);
  r.formula = std::make_shared<const std::string>(std::move(text));
  r.solutions = solveRoots(out.value);
  return r;
}

}  // namespace Cas
}  // namespace Calculator
