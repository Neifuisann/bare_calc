#include "calculator/cas/calculator_cas_print.h"
#include "calculator/cas/calculator_cas_canonical.h"
#include "calculator/math/calculator_eng_symbols.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Calculator {
namespace Cas {
namespace {
// Between two juxtaposed factors that would otherwise lex as one token
// (pi x -> "pix"). The layout draws '*' as a multiplication sign.
constexpr char kNaturalSeparator = '*';

Expr rawNode(Kind kind, Args args, Fn f = Fn::Sin) {
  Node n;
  n.kind = kind;
  n.fn = f;
  n.args = std::move(args);
  return finish(std::move(n));
}
Expr rawNegate(const Expr& e) {
  if (isNumber(e)) return number(-e->number);
  if (e->kind == Kind::Mul && isNumber(e->args[0])) {
    Args a = e->args;
    a[0] = number(-a[0]->number);
    return rawNode(Kind::Mul, std::move(a));
  }
  return rawNode(Kind::Mul, Args{integer(-1), e});
}
void appendFlat(Kind kind, const Expr& e, Args& out) {
  if (e->kind == kind) out.insert(out.end(), e->args.begin(), e->args.end());
  else out.push_back(e);
}
// The structure of an exact number's canonical text (5*sqrt(2), 2/3*pi,
// ln(2)+ln(5)) as a display-only tree: rational leaves, "pi"/"e" as named
// symbols, radicals as powers. Built without folding, so it prints as read.
Expr displayTree(const Expression& n) {
  if (!n) return integer(0);
  switch (n->kind) {
    case Calculator::Node::Kind::Number: return number(ExactNumber::fromLiteral(n->text));
    case Calculator::Node::Kind::Symbol: return symbol(n->text);
    case Calculator::Node::Kind::Unary: {
      const Expr a = displayTree(n->children[0]);
      if (EngSymbol::is(n->text[0]))
        return rawNode(Kind::Mul, Args{a, rawNode(Kind::Pow, Args{integer(10), integer(EngSymbol::exponent(n->text[0]))})});
      return n->text == "-" ? rawNegate(a) : a;
    }
    case Calculator::Node::Kind::Binary: {
      const Expr a = displayTree(n->children[0]), b = displayTree(n->children[1]);
      Args args;
      if (n->text == "+" || n->text == "-") {
        appendFlat(Kind::Add, a, args);
        appendFlat(Kind::Add, n->text == "-" ? rawNegate(b) : b, args);
        return rawNode(Kind::Add, std::move(args));
      }
      if (n->text == "*" || n->text == "/") {
        appendFlat(Kind::Mul, a, args);
        appendFlat(Kind::Mul, n->text == "/" ? rawNode(Kind::Pow, Args{b, integer(-1)}) : b, args);
        return rawNode(Kind::Mul, std::move(args));
      }
      return rawNode(Kind::Pow, Args{a, b});
    }
    case Calculator::Node::Kind::Call: {
      const Expr last = displayTree(n->children.back());
      if (n->text == "sqrt") return rawNode(Kind::Pow, Args{last, fraction(1, 2)});
      if (n->text == "root") return rawNode(Kind::Pow, Args{last, number(ExactNumber::integer(1) / displayTree(n->children[0])->number)});
      return rawNode(Kind::Func, Args{last}, Fn::Ln);
    }
  }
  return integer(0);
}
Expr displayTree(const ExactNumber& v) { return displayTree(parse(v.str()).root); }

// The terms of an exact sum's display tree as (rational coefficient, other
// factors), and the least common denominator of the coefficients.
BigInt sumTerms(const Expr& tree, std::vector<std::pair<Rational, Args>>& terms) {
  BigInt l(1);
  for (const auto& t : tree->args) {
    Rational c(1), q;
    Args rest;
    for (const auto& f : t->kind == Kind::Mul ? t->args : Args{t}) {
      if (rationalValue(f, q)) c = c * q;
      else if (f->kind == Kind::Pow && rationalValue(f->args[0], q) && isNumber(f->args[1]) &&
               f->args[1]->number == ExactNumber::integer(-1))
        c = c / q;
      else rest.push_back(f);
    }
    l = l / BigInt::gcd(l, c.den()) * c.den();
    terms.push_back({c, rest});
  }
  return l;
}
// Degree of f in v when f is a polynomial expression in v (powers of sums
// included: (y+1)^2 has degree 2); false otherwise.
bool degreeIn(const Expr& f, const Expr& v, Rational& d) {
  d = Rational(0);
  if (equal(f, v)) { d = Rational(1); return true; }
  if (freeOf(f, v)) return true;
  Rational q, part;
  switch (f->kind) {
    case Kind::Pow:
      if (!rationalValue(f->args[1], q)) return false;
      // x^(1/2) and 1/x keep their degree; a sum only under a whole power.
      if (!equal(f->args[0], v) && (!q.integer() || q.sign() < 0)) return false;
      if (!degreeIn(f->args[0], v, part)) return false;
      d = part * q;
      return true;
    case Kind::Mul: {
      // x^3*e^x still has degree 3 for ordering, though not a polynomial.
      bool polynomial = true;
      for (const auto& a : f->args) {
        if (degreeIn(a, v, part)) d = d + part;
        else polynomial = false;
      }
      return polynomial;
    }
    case Kind::Add:
      for (const auto& a : f->args) {
        if (!degreeIn(a, v, part)) return false;
        if (Rational::compare(part, d) > 0) d = part;
      }
      return true;
    default: return false;
  }
}

bool isName(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
// Would "left" + "right" lex differently from the two factors?
bool needsSeparator(const std::string& left, const std::string& right) {
  const char l = left.back(), r = right.front();
  if (std::isdigit(static_cast<unsigned char>(r)) || r == '.' || r == '-') return true;  // 2 3, x 2
  if (r == 'C') return true;                                                            // nCr in operator position
  if (r == 'E' && std::isdigit(static_cast<unsigned char>(l))) return true;             // 2E+1 is a literal
  if (std::islower(static_cast<unsigned char>(l)) && std::isalnum(static_cast<unsigned char>(r))) {
    // A lone x/y/z is its own token; any other lower-case run is a name
    // (pi, e) that would swallow the next letters.
    const bool variable = (l == 'x' || l == 'y' || l == 'z') && (left.size() == 1 || !isName(left[left.size() - 2]));
    return !variable;
  }
  return false;
}

// Does `text` have an operator outside parentheses (a leading sign aside)?
bool topLevel(const std::string& text, const char* operators) {
  int depth = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '(') ++depth;
    else if (c == ')') --depth;
    else if (depth == 0 && i > 0 && std::strchr(operators, c)) return true;
  }
  return false;
}

class Printer {
 public:
  explicit Printer(bool natural) : natural_(natural), x_(symbol("x")) {}
  std::string print(const Expr& e);

 private:
  bool natural_;
  Expr x_;
  std::string number(const ExactNumber& v);
  std::string numberSum(const Expr& tree);
  std::string sum(const Expr& e);
  std::string product(const Expr& e);
  std::string power(const Expr& base, const Expr& exponent);
  std::string factor(const Expr& e);
  std::string join(const Args& factors);
  std::string side(const Args& factors, bool inFraction);
  void order(Args& factors) const;
  int rank(const Expr& e) const;
};

std::string Printer::number(const ExactNumber& v) {
  if (v.isRational()) {
    const Rational& r = v.rationalValue();
    const std::string sign = r.sign() < 0 ? "-" : "";
    if (r.integer()) return sign + r.num().abs().str();
    const std::string top = r.num().abs().str(), bottom = r.den().str();
    return sign + (natural_ ? "frac(" + top + "," + bottom + ")" : top + "/" + bottom);
  }
  const Expr tree = displayTree(v);
  return tree->kind == Kind::Add ? numberSum(tree) : print(tree);
}

// An exact sum reads as ClassWiz shows it: in its own term order (rational
// first) over one common denominator, (-1+sqrt(5))/2.
std::string Printer::numberSum(const Expr& tree) {
  std::vector<std::pair<Rational, Args>> terms;
  const BigInt l = sumTerms(tree, terms);
  std::string out;
  for (const auto& term : terms) {
    const Rational k = term.first * Rational(l);
    Args factors = term.second;
    if (!k.isOne() || factors.empty()) factors.insert(factors.begin(), Cas::number(ExactNumber::rational(k)));
    Node n;
    n.kind = Kind::Mul;
    n.args = std::move(factors);
    const std::string s = print(n.args.size() == 1 ? n.args[0] : finish(std::move(n)));
    if (!out.empty() && s.front() != '-') out += '+';
    out += s;
  }
  if (l == BigInt(1)) return out;
  return natural_ ? "frac(" + out + "," + l.str() + ")" : "(" + out + ")/" + l.str();
}

// Reading order inside a product: constants (pi, sqrt(2)), variables in
// alphabetical order, exponentials, functions, then parenthesized sums.
int Printer::rank(const Expr& e) const {
  Expr b, x;
  splitPower(e, b, x);
  switch (b->kind) {
    case Kind::Symbol: return b->name == "pi" || b->name == "e" ? 0 : 1;
    case Kind::Number:
      if (!isNumber(x)) return 2;
      return b->number.isRational() || displayTree(b->number)->kind != Kind::Func ? 0 : 3;
    case Kind::Func: return 3;
    case Kind::Add: return 4;
    default: return 5;
  }
}
void Printer::order(Args& factors) const {
  std::stable_sort(factors.begin(), factors.end(), [&](const Expr& a, const Expr& b) {
    const int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb;
    if (ra == 1) {
      Expr ba, bb, xa, xb;
      splitPower(a, ba, xa);
      splitPower(b, bb, xb);
      return ba->name < bb->name;
    }
    return false;
  });
}

std::string Printer::join(const Args& factors) {
  std::string out, last;
  for (const auto& f : factors) {
    const std::string t = factor(f);
    // Linear 2^x followed by ln(2) parses right but reads as 2^(x ln 2).
    const size_t caret = last.rfind('^');
    const bool letterExponent = !natural_ && caret != std::string::npos && last.find('(', caret) == std::string::npos &&
                                isName(last.back()) && (isName(t.front()) || t.front() == '(');
    if (!out.empty() && (letterExponent || needsSeparator(out, t))) out += natural_ ? kNaturalSeparator : '*';
    out += t;
    last = t;
  }
  return out;
}

// One side of a fraction. A lone sum needs no parentheses inside a natural
// fraction bar; the linear form adds them where the / would bind wrongly.
std::string Printer::side(const Args& factors, bool inFraction) {
  if (factors.empty()) return "1";
  if (inFraction && natural_ && factors.size() == 1 && factors[0]->kind == Kind::Add) return sum(factors[0]);
  return join(factors);
}

std::string Printer::factor(const Expr& e) {
  if (e->kind == Kind::Add) return "(" + sum(e) + ")";
  if (isNumber(e) && !e->number.isRational()) {
    const Expr tree = displayTree(e->number);
    return tree->kind == Kind::Add ? "(" + numberSum(tree) + ")" : print(tree);
  }
  return print(e);
}

std::string Printer::product(const Expr& e) {
  const Args all = e->kind == Kind::Mul ? e->args : Args{e};
  ExactNumber c = ExactNumber::integer(1);
  Args top, bottom;
  auto place = [&](const Expr& f) {
    bool certain = false;
    if (f->kind == Kind::Pow && isNumber(f->args[1]) && f->args[1]->number.sign(&certain) < 0 && certain)
      bottom.push_back(pow(f->args[0], Cas::number(-f->args[1]->number)));
    else
      top.push_back(f);
  };
  auto rational = [&](const Rational& r) {
    if (r.den() != BigInt(1)) bottom.push_back(Cas::number(ExactNumber::integer(r.den())));
    if (r.num() != BigInt(1)) top.push_back(Cas::number(ExactNumber::integer(r.num())));
  };
  for (const auto& f : all) {
    if (isNumber(f)) c = c * f->number;
    else place(f);
  }
  bool negativeSign = false, certain = false;
  if (c.sign(&certain) < 0 && certain && (c.isRational() || displayTree(c)->kind != Kind::Add)) {
    negativeSign = true;
    c = -c;
  }
  if (c.isRational()) {
    rational(c.rationalValue());
  } else {
    // An irrational coefficient contributes its own factors: 2/3*pi puts 2
    // and pi on top and 3 below; 1/ln(2) puts ln(2) below.
    const Expr tree = displayTree(c);
    if (tree->kind == Kind::Add) {
      // (-1+sqrt(5))/2 * x reads (-1+sqrt(5))x / 2.
      std::vector<std::pair<Rational, Args>> terms;
      const BigInt d = sumTerms(tree, terms);
      top.push_back(Cas::number(c * ExactNumber::integer(d)));
      if (d != BigInt(1)) bottom.push_back(Cas::number(ExactNumber::integer(d)));
    } else {
      for (const auto& f : tree->kind == Kind::Mul ? tree->args : Args{tree}) {
        if (isNumber(f) && f->number.isRational()) rational(f->number.rationalValue());
        else place(f);
      }
    }
  }
  auto numbersFirst = [&](Args& list) {
    Args numbers, rest;
    for (const auto& f : list) (isNumber(f) && f->number.isRational() ? numbers : rest).push_back(f);
    order(rest);
    numbers.insert(numbers.end(), rest.begin(), rest.end());
    list = std::move(numbers);
  };
  numbersFirst(top);
  numbersFirst(bottom);
  const std::string sign = negativeSign ? "-" : "";
  if (bottom.empty()) return sign + side(top, false);
  const std::string topText = side(top, true), bottomText = side(bottom, true);
  if (natural_) return sign + "frac(" + topText + "," + bottomText + ")";
  const bool wrapTop = topLevel(topText, "+-"), wrapBottom = bottom.size() > 1 || topLevel(bottomText, "+-*/");
  return sign + (wrapTop ? "(" + topText + ")" : topText) + "/" + (wrapBottom ? "(" + bottomText + ")" : bottomText);
}

std::string Printer::power(const Expr& b, const Expr& e) {
  Rational q;
  if (rationalValue(e, q) && q.num() == BigInt(1) && q.den().fitsInt64()) {
    const int64_t k = q.den().toInt64();
    if (k == 2) return "sqrt(" + print(b) + ")";
    if (k > 2) return "root(" + std::to_string(k) + "," + print(b) + ")";
  }
  std::string base = print(b);
  const bool plain = b->kind == Kind::Symbol || b->kind == Kind::Func ||
                     (isNumber(b) && b->number.isInteger() && b->number.rationalValue().sign() > 0) ||
                     (isNumber(b) && !b->number.isRational() && displayTree(b->number)->kind == Kind::Symbol);
  if (!plain) base = "(" + base + ")";
  const std::string exponent = print(e);
  if (natural_) return base + "^(" + exponent + ")";
  const bool simple = (isNumber(e) && e->number.isInteger() && e->number.rationalValue().sign() > 0) || e->kind == Kind::Symbol ||
                      (isNumber(e) && !e->number.isRational() && displayTree(e->number)->kind == Kind::Symbol);
  return base + "^" + (simple ? exponent : "(" + exponent + ")");
}

std::string Printer::sum(const Expr& e) {
  // Terms in the main variable (x, else the first symbol alphabetically)
  // first, by descending degree (a polynomial term before a transcendental
  // one of the same degree); then terms in other symbols by total degree and
  // alphabetically; the constant last.
  Expr main = x_;
  if (freeOf(e, x_)) {
    std::vector<std::string> names;
    collectSymbols(e, names);
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& n) { return n == "pi" || n == "e"; }), names.end());
    if (!names.empty()) main = symbol(*std::min_element(names.begin(), names.end()));
  }
  struct Entry {
    Expr term;
    int group = 0;
    bool polynomial = true;
    Rational degree, total;
    std::vector<std::pair<std::string, Rational>> powers;
  };
  std::vector<Entry> entries;
  for (const auto& t : e->args) {
    Entry entry;
    entry.term = t;
    entry.group = hasSymbols(t) ? (freeOf(t, main) ? 1 : 0) : 2;
    entry.polynomial = degreeIn(t, main, entry.degree);
    const Args factors = t->kind == Kind::Mul ? t->args : Args{t};
    for (const auto& f : factors) {
      Expr b, x;
      splitPower(f, b, x);
      Rational q;
      if (b->kind == Kind::Symbol && rationalValue(x, q)) {
        entry.total = entry.total + q;
        entry.powers.push_back({b->name, q});
      }
    }
    std::sort(entry.powers.begin(), entry.powers.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    entries.push_back(std::move(entry));
  }
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.group != b.group) return a.group < b.group;
    if (a.group == 2) return false;
    if (int c = Rational::compare(a.degree, b.degree)) return c > 0;
    if (a.polynomial != b.polynomial) return a.polynomial;
    if (int c = Rational::compare(a.total, b.total)) return c > 0;
    for (size_t i = 0; i < a.powers.size() && i < b.powers.size(); ++i) {
      if (a.powers[i].first != b.powers[i].first) return a.powers[i].first < b.powers[i].first;
      if (int c = Rational::compare(a.powers[i].second, b.powers[i].second)) return c > 0;
    }
    return false;
  });
  std::vector<std::string> texts;
  for (const auto& entry : entries) texts.push_back(print(entry.term));
  // Two terms read positive first: 1-x^2, tan(x)-x.
  if (texts.size() == 2 && texts[0].front() == '-' && texts[1].front() != '-') std::swap(texts[0], texts[1]);
  std::string out;
  for (const auto& t : texts) {
    if (!out.empty() && t.front() != '-') out += '+';
    out += t;
  }
  return out;
}

std::string Printer::print(const Expr& e) {
  switch (e->kind) {
    case Kind::Number: return number(e->number);
    case Kind::Symbol:
    case Kind::Text: return e->name;
    case Kind::Wild: return "?";
    case Kind::Add: return sum(e);
    case Kind::Mul: return product(e);
    case Kind::Pow: {
      bool certain = false;
      if (isNumber(e->args[1]) && e->args[1]->number.sign(&certain) < 0 && certain) return product(e);
      return power(e->args[0], e->args[1]);
    }
    case Kind::Func: return std::string(fnName(e->fn)) + "(" + print(e->args[0]) + ")";
    case Kind::Equation: return print(e->args[0]) + "=" + print(e->args[1]);
    case Kind::List: {
      // Several solutions of a system (Lists of `v = value`) read as tuples:
      // (x,y)=(1,2), (2,1).
      if (!e->args.empty() && e->args[0]->kind == Kind::List) {
        auto tuple = [&](const Expr& solution, bool names) {
          std::string out;
          for (const auto& eq : solution->args) out += (out.empty() ? "" : ",") + print(eq->args[names ? 0 : 1]);
          return "(" + out + ")";
        };
        std::string out = tuple(e->args[0], true) + "=";
        for (size_t i = 0; i < e->args.size(); ++i) out += (i ? ", " : "") + tuple(e->args[i], false);
        return out;
      }
      std::string out;
      for (const auto& item : e->args) out += (out.empty() ? "" : ", ") + print(item);
      return out;
    }
    case Kind::Integral:
    case Kind::Solve: {
      // A system prints its equations and then every unknown, so it reads
      // back as the same command.
      auto items = [&](const Expr& part) {
        if (part->kind != Kind::List) return print(part);
        std::string out;
        for (const auto& item : part->args) out += (out.empty() ? "" : ",") + print(item);
        return out;
      };
      std::string out = (e->kind == Kind::Integral ? "integrate(" : "solve(") + items(e->args[0]);
      if (e->args[1]->kind == Kind::List || !equal(e->args[1], x_)) out += "," + items(e->args[1]);
      return out + ")";
    }
  }
  return "?";
}
}  // namespace

std::string naturalText(const Expr& e) { return Printer(true).print(e); }
std::string linearText(const Expr& e) { return Printer(false).print(e); }

}  // namespace Cas
}  // namespace Calculator
