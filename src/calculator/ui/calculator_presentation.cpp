#include "calculator/ui/calculator_presentation.h"
#include "calculator/math/calculator_complex.h"
#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <string_view>
#include <vector>

namespace Calculator {
namespace Presentation {
namespace {

// Flat-arena builders. Every builder appends a contiguous subtree: composites
// reserve a placeholder, append children, then patch first/count. Top-level
// runs append directly after the root (nodes[0]).
// Attachment rule shared by every node: the open composite, or with an empty
// stack the root at 0. Only makeOutput creates nodes on an empty stack (the
// root itself, parented to kNoIndex by hand).
uint32_t currentParent(const Output& o) { return o.depth ? o.parents[o.depth - 1] : 0; }
uint32_t addNode(Output& o, Kind kind, int64_t a = 0, int64_t b = 0) {
  Node n;
  n.kind = kind;
  n.a = a;
  n.b = b;
  n.parent = currentParent(o);
  o.nodes.push_back(n);
  return static_cast<uint32_t>(o.nodes.size() - 1);
}
uint32_t addText(Output& o, Kind kind, std::string_view s) {
  Node n;
  n.kind = kind;
  n.text = static_cast<uint32_t>(o.text.size());
  n.len = static_cast<uint32_t>(s.size());
  n.parent = currentParent(o);
  o.text.append(s.data(), s.size());
  o.nodes.push_back(n);
  return static_cast<uint32_t>(o.nodes.size() - 1);
}
// Reserve a composite placeholder and open it: following appends attach to it
// until endComposite. Tokens balance by construction; endComposite only pops.
uint32_t beginComposite(Output& o, Kind kind, int64_t a = 0, int64_t b = 0) {
  const uint32_t parent = addNode(o, kind, a, b);
  if (o.depth < 8) o.parents[o.depth++] = parent;
  return parent;
}
void endComposite(Output& o, uint32_t parent) {
  (void)parent;
  if (o.depth > 0) --o.depth;
}
uint32_t addDigits(Output& o, std::string_view d) { return addText(o, Kind::Digits, d); }
uint32_t addSign(Output& o, bool negative) { return addText(o, Kind::Sign, negative ? "-" : "+"); }
uint32_t addPoint(Output& o) { return addNode(o, Kind::Point); }
uint32_t addSeparator(Output& o) { return addNode(o, Kind::Separator); }
uint32_t addWord(Output& o, std::string_view t) { return addText(o, Kind::Text, t); }
// A fresh builder always carries the root Sequence at index 0. The root is
// the only node parented to kNoIndex; everything else attaches to the root
// or an open composite, so sibling order always matches construction order.
Output makeOutput() {
  Output o;
  Node root;
  root.kind = Kind::Sequence;
  root.parent = kNoIndex;
  o.nodes.push_back(root);
  return o;
}
struct Mark {
  size_t nodes;
  size_t text;
};
Mark markOut(const Output& o) { return {o.nodes.size(), o.text.size()}; }
void rollbackOut(Output& o, Mark m) {
  o.nodes.resize(m.nodes);
  o.text.resize(m.text);
}
// Integer emission without std::to_string temporaries (one shared routine;
// call sites stay calls, not inline formatting expansions).
char* formatInt(char* end, int64_t v) {
  bool neg = v < 0;
  uint64_t u = neg ? static_cast<uint64_t>(-(v + 1)) + 1u : static_cast<uint64_t>(v);
  do {
    *--end = static_cast<char>('0' + u % 10);
    u /= 10;
  } while (u);
  if (neg) *--end = '-';
  return end;
}
void appendIntDigits(Output& o, int64_t v) {
  char buf[24];
  char* end = buf + sizeof(buf);
  char* start = formatInt(end, v < 0 ? -v : v);
  addText(o, Kind::Digits, std::string_view(start, static_cast<size_t>(end - start)));
}
void appendIntStr(std::string& s, int64_t v) {
  char buf[24];
  char* end = buf + sizeof(buf);
  char* start = formatInt(end, v);
  s.append(start, static_cast<size_t>(end - start));
}
size_t intDigits(int64_t v) {
  size_t n = 1;
  uint64_t u = v < 0 ? static_cast<uint64_t>(-(v + 1)) + 1u : static_cast<uint64_t>(v);
  while (u >= 10) {
    u /= 10;
    ++n;
  }
  return n;
}

// Canonical decimal digit split (lexical only, no math-structure rediscovery).
// Input is engine-canonical plain text with '.' and no exponent (callers
// expand E-notation first). Any unexpected remainder degrades to unstructured
// text so nothing is silently dropped.
void appendDecimalRun(Output& o, const std::string& text) {
  size_t i = 0;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
    addSign(o, text[i] == '-');
    ++i;
  }
  size_t run = i;
  auto flushDigits = [&] {
    if (i > run) {
      addDigits(o, std::string_view(text.data() + run, i - run));
      run = i;
    }
  };
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c >= '0' && c <= '9') continue;
    if (c == '.') {
      flushDigits();
      run = i + 1;
      addPoint(o);
      continue;
    }
    break;
  }
  flushDigits();
  if (i < text.size()) addWord(o, std::string_view(text.data() + i, text.size() - i));
}

// A c*sqrt(n)/d term is shown while numerator (coeff + radicand digits) <= 10 chars,
// denominator <= 6 digits, and radicand < 1E6 (25*sqrt(80278)/276 from
// 1000*sqrt(41*89*22)/(4*2760)), not ClassWiz's stricter |c|,d < 100.
bool permittedExact(const Exact& term, const Decimal& number) {
  if (term.constant()) return number.abs() < Decimal("1E6");
  if (term.radicand == 1) return true;
  return radicalTermFits(term);
}
// Shared exact-width rule (engine decimal fallback past 10 chars). One source
// so capabilities (standardView) and rendering (appendExactTerm) agree.
bool exactFitsWidth(int64_t numerator, int64_t denominator) {
  const int64_t absNum = numerator < 0 ? -numerator : numerator;
  const size_t mixedDigits =
      absNum > denominator ? intDigits(absNum / denominator) + intDigits(absNum % denominator) +
                                 intDigits(denominator) + 2
                           : intDigits(absNum) + intDigits(denominator) + 1;
  return mixedDigits <= 10;
}

// S<->D on an approximate result (a numeric integral's 10.125): the FORMAT
// fraction rows convert what the display shows, its 10 significant digits, to
// the simplest fraction that rounds back to them and fits the exact width.
// A view only -- the value stays approximate, and Standard keeps its decimal.
// Continued-fraction convergents are the simplest candidates, so sin(1) finds
// nothing within 10 chars while 81/8 and 1/3 (0.3333333333) do.
bool approximateFraction(const Decimal& number, Exact& out) {
  if (!number.finite() || number.zero() || number.integer()) return false;
  const Decimal target = number.abs().rounded(10);
  Decimal x = target;
  int64_t h0 = 0, h1 = 1, k0 = 1, k1 = 0;  // convergents h/k, previous and current
  for (int term = 0; term < 32; ++term) {
    int64_t a = 0;
    if (!x.floor().toInt(a) || a < 0 || a > 9999999999LL) return false;
    if (h1 > 0 && a > (INT64_MAX - h0) / h1) return false;
    const int64_t h = a * h1 + h0, k = a * k1 + k0;
    if (k > 9999999999LL) return false;
    h0 = h1, h1 = h, k0 = k1, k1 = k;
    if (k > 1 && (Decimal(std::to_string(h)) / Decimal(std::to_string(k))).rounded(10) == target) {
      if (!exactFitsWidth(h, k)) return false;  // simpler forms all failed; wider ones won't fit
      out.numerator = number.negative() ? -h : h;
      out.denominator = k;
      out.radicand = 1;
      out.pi = false;
      out.valid = true;
      return true;
    }
    const Decimal rest = x - x.floor();
    if (rest.zero()) return false;
    x = Decimal(1) / rest;
  }
  return false;
}

// Forward declarations for recursion via child indices (never via strings).
void appendValueTo(Output& o, const Value& value, const Settings& settings, Format format);

// Build a Sequence child holding one decimal run.
void appendDecimalChild(Output& o, const std::string& text) {
  const uint32_t seq = beginComposite(o, Kind::Sequence);
  appendDecimalRun(o, text);
  endComposite(o, seq);
}

// The irrational part of a term: pi, e or sqrt(radicand).
void appendSymbol(Output& o, const Exact& e) {
  if (e.pi)
    addNode(o, Kind::Pi, 1, 1);
  else if (e.euler)
    addNode(o, Kind::Euler, 1, 1);
  else
    addNode(o, Kind::Radical, 1, e.radicand);
}

// One exact term without extra/sign handling. Returns true when an exact view
// was produced; false means the caller should use decimal.
bool appendExactTerm(Output& o, const Exact& e, const Settings& settings, Format format) {
  if (!e.valid) return false;
  if (e.constant() || e.radicand != 1) {
    if (settings.io != IO::MathMath && settings.io != IO::MathDecimal) return false;
    // Irrational: coefficient x pi/e/sqrt, optionally over a denominator.
    const bool unit = e.numerator == 1 || e.numerator == -1;
    const bool neg = e.numerator < 0;
    if (neg) addSign(o, true);
    const int64_t coeff = e.numerator < 0 ? -e.numerator : e.numerator;
    if (e.denominator != 1) {
      // frac(coeff*pi-or-sqrt, den)
      const uint32_t frac = beginComposite(o, Kind::Fraction);
      const uint32_t num = beginComposite(o, Kind::Sequence);
      // Implicit product: no '*' run; layout juxtaposes coefficient and symbol.
      if (!unit) appendIntDigits(o, coeff);
      appendSymbol(o, e);
      endComposite(o, num);
      const uint32_t den = beginComposite(o, Kind::Sequence);
      appendIntDigits(o, e.denominator);
      endComposite(o, den);
      endComposite(o, frac);
    } else if (!unit) {
      appendIntDigits(o, coeff);
      appendSymbol(o, e);
    } else if (e.numerator == 0) {
      addDigits(o, "0");
    } else {
      // Coefficient 1 is omitted (sqrt, pi); -1 keeps only the sign above.
      appendSymbol(o, e);
    }
    return true;
  }
  // Rational.
  if (e.denominator == 1) {
    if (e.numerator < 0) addSign(o, true);
    appendIntDigits(o, e.numerator < 0 ? -e.numerator : e.numerator);
    return true;
  }
  const int64_t absNum = e.numerator < 0 ? -e.numerator : e.numerator;
  if (!exactFitsWidth(e.numerator, e.denominator)) return false;  // engine falls back to decimal
  if (e.numerator < 0) addSign(o, true);
  if (format == Format::Mixed && absNum > e.denominator) {
    const uint32_t mixed = beginComposite(o, Kind::Mixed);
    const uint32_t whole = beginComposite(o, Kind::Sequence);
    appendIntDigits(o, absNum / e.denominator);
    endComposite(o, whole);
    const uint32_t num = beginComposite(o, Kind::Sequence);
    appendIntDigits(o, absNum % e.denominator);
    endComposite(o, num);
    const uint32_t den = beginComposite(o, Kind::Sequence);
    appendIntDigits(o, e.denominator);
    endComposite(o, den);
    endComposite(o, mixed);
  } else {
    const uint32_t frac = beginComposite(o, Kind::Fraction);
    const uint32_t num = beginComposite(o, Kind::Sequence);
    appendIntDigits(o, absNum);
    endComposite(o, num);
    const uint32_t den = beginComposite(o, Kind::Sequence);
    appendIntDigits(o, e.denominator);
    endComposite(o, den);
    endComposite(o, frac);
  }
  return true;
}

// A FactoredExact over its terms' common denominator: pi(5sqrt(5)-1)/12, or
// 5sqrt(5)pi/12 for one term. Terms are reduced, so n1, n2 and den share no
// factor. The positive term leads (pi(5sqrt(5)-1), not pi(-1+5sqrt(5))); with
// both negative the sign goes outside. Shown under the pi rule (|value| < 1E6)
// while the numerator, pi and parentheses included, fits 12 characters and
// the denominator 6 digits; radicands stay < 1E6 as for a single term.
struct FactoredParts {
  int64_t n1 = 0, r1 = 1, n2 = 0, r2 = 1, den = 1;
  bool two = false, negative = false;
};
size_t factoredTermWidth(int64_t n, int64_t r) {
  return (r != 1 && (n == 1 || n == -1) ? 0 : intDigits(n)) + (r == 1 ? 0 : intDigits(r));
}
bool factoredParts(const FactoredExact& f, const Decimal& number, FactoredParts& p) {
  if (!f.valid || number.abs() >= Decimal("1E6")) return false;
  const Exact &a = f.first, &b = f.second;
  if (a.radicand >= kExactMaxRadicand || (b.valid && b.radicand >= kExactMaxRadicand)) return false;
  int64_t den = a.denominator;
  if (b.valid && __builtin_mul_overflow(a.denominator / std::gcd(a.denominator, b.denominator), b.denominator, &den))
    return false;
  p = FactoredParts{};
  p.two = b.valid;
  if (__builtin_mul_overflow(a.numerator, den / a.denominator, &p.n1)) return false;
  if (p.two && __builtin_mul_overflow(b.numerator, den / b.denominator, &p.n2)) return false;
  p.r1 = a.radicand;
  p.r2 = b.radicand;
  p.den = den;
  if (p.two && p.n1 < 0 && p.n2 > 0) {
    std::swap(p.n1, p.n2);
    std::swap(p.r1, p.r2);
  }
  if (p.n1 < 0) {
    p.negative = true;
    p.n1 = -p.n1;
    p.n2 = -p.n2;
  }
  const size_t width = 1 + factoredTermWidth(p.n1, p.r1) + (p.two ? 3 + factoredTermWidth(p.n2, p.r2) : 0);
  return width <= 12 && intDigits(den) <= 6;
}
void appendFactoredTerm(Output& o, int64_t n, int64_t r) {
  if (r == 1) {
    appendIntDigits(o, n);
    return;
  }
  if (n != 1) appendIntDigits(o, n);
  addNode(o, Kind::Radical, 1, r);
}
void appendFactored(Output& o, const FactoredExact& f, const FactoredParts& p) {
  if (p.negative) addSign(o, true);
  uint32_t frac = 0, num = 0;
  if (p.den != 1) {
    frac = beginComposite(o, Kind::Fraction);
    num = beginComposite(o, Kind::Sequence);
  }
  const Kind constant = f.pi ? Kind::Pi : Kind::Euler;
  if (p.two) {
    addNode(o, constant, 1, 1);
    addWord(o, "(");
    appendFactoredTerm(o, p.n1, p.r1);
    addSign(o, p.n2 < 0);
    appendFactoredTerm(o, p.n2 < 0 ? -p.n2 : p.n2, p.r2);
    addWord(o, ")");
  } else {
    appendFactoredTerm(o, p.n1, p.r1);
    addNode(o, constant, 1, 1);
  }
  if (p.den != 1) {
    endComposite(o, num);
    const uint32_t den = beginComposite(o, Kind::Sequence);
    appendIntDigits(o, p.den);
    endComposite(o, den);
    endComposite(o, frac);
  }
}

bool appendRecurring(Output& o, const Exact& e) {
  if (!e.valid || e.radicand != 1 || e.constant()) return false;
  const int64_t n = e.numerator < 0 ? -e.numerator : e.numerator, d = e.denominator;
  if (d == 0) return false;
  int64_t rem = n % d;
  const int64_t whole = n / d;
  std::string digits;
  std::vector<int64_t> remainders;
  while (rem && digits.size() < 100) {
    auto it = std::find(remainders.begin(), remainders.end(), rem);
    if (it != remainders.end()) {
      const size_t start = static_cast<size_t>(it - remainders.begin());
      // Structured node owns the whole prefix (integer + Point + non-repeat),
      // exactly like the legacy "0.recur(3)"/"0.1recur(6)" templates: the
      // Point is always present in a recurring decimal expansion.
      if (e.numerator < 0) addSign(o, true);
      const uint32_t recur = beginComposite(o, Kind::Recurring);
      const uint32_t pre = beginComposite(o, Kind::Sequence);
      appendIntDigits(o, whole);
      addPoint(o);
      if (start > 0 && start <= digits.size()) {
        addDigits(o, std::string_view(digits.data(), start));
      }
      endComposite(o, pre);
      const uint32_t rep = beginComposite(o, Kind::Sequence);
      addDigits(o, std::string_view(digits.data() + start, digits.size() - start));
      endComposite(o, rep);
      endComposite(o, recur);
      return true;
    }
    remainders.push_back(rem);
    rem *= 10;
    digits += char('0' + rem / d);
    rem %= d;
  }
  if (!rem) {
    if (e.numerator < 0) addSign(o, true);
    if (digits.empty()) {
      appendIntDigits(o, whole);
    } else {
      appendIntDigits(o, whole);
      addPoint(o);
      addDigits(o, std::string_view(digits));
    }
    return true;
  }
  return false;
}

bool appendPrime(Output& o, const Value& v) {
  int64_t n = 0;
  if (!v.number.toInt(n) || n <= 0 || n >= 10000000000LL) return false;
  if (n == 1) {
    addDigits(o, "1");
    return true;
  }
  bool first = true;
  int64_t rest = n;
  unsigned large = 0;
  struct Factor {
    int64_t base;
    unsigned exp;
  };
  std::vector<Factor> factors;
  for (int64_t p = 2; p <= 1009 && p * p <= rest; ++p) {
    if (rest % p) continue;
    if (p > 999 && ++large > 1) break;
    unsigned power = 0;
    while (rest % p == 0) {
      rest /= p;
      ++power;
    }
    factors.push_back({p, power});
  }
  if (rest > 1) {
    // Engine parenthesizes large leftover composites (>=1018081).
    // Keep the same value; lowering adds the parentheses structurally.
    factors.push_back({rest, 1});
  }
  for (const auto& f : factors) {
    if (!first) addWord(o, "*");
    first = false;
    const bool paren = f.base >= 1018081;
    auto emitBase = [&] {
      if (paren) addWord(o, "(");
      appendIntDigits(o, f.base);
      if (paren) addWord(o, ")");
    };
    if (f.exp > 1) {
      const uint32_t sup = beginComposite(o, Kind::Superscript);
      const uint32_t b = beginComposite(o, Kind::Sequence);
      emitBase();
      endComposite(o, b);
      const uint32_t e = beginComposite(o, Kind::Sequence);
      appendIntDigits(o, f.exp);
      endComposite(o, e);
      endComposite(o, sup);
    } else {
      emitBase();
    }
  }
  return true;
}

bool appendSexagesimal(Output& o, const Value& v) {
  const Decimal x = v.number.abs();
  const Decimal fl = x.floor();
  int64_t d = 0, m = 0;
  if (!fl.toInt(d) || d < 0 || d > 9999999) return false;
  if (!((x - fl) * 60).floor().toInt(m)) return false;
  const Decimal seconds =
      ((x - Decimal(static_cast<int>(d))) * 3600 - Decimal(static_cast<int>(m)) * 60).quantized(2);
  const std::string secText = seconds.str(10);
  if (v.number.negative()) addSign(o, true);
  addNode(o, Kind::Degree, d);
  addNode(o, Kind::Minute, m);
  const uint32_t sec = beginComposite(o, Kind::Second);
  const uint32_t run = beginComposite(o, Kind::Sequence);
  appendDecimalRun(o, secText);
  endComposite(o, run);
  endComposite(o, sec);
  return true;
}

// Shared mantissa/exponent normalization for scientific and engineering
// views (single copy of the Decimal scale loop).
Decimal scaleMantissa(Decimal d, int& exponent) {
  exponent = 0;
  Decimal mantissa = d;
  while (mantissa.abs() >= 10) {
    mantissa = mantissa / 10;
    ++exponent;
  }
  while (mantissa.abs() < 1) {
    mantissa = mantissa * 10;
    --exponent;
  }
  return mantissa;
}

void appendScaledPower(Output& o, const Decimal& mantissa, int exponent, unsigned digits) {
  const uint32_t t = beginComposite(o, Kind::TimesTenPower, exponent);
  const uint32_t run = beginComposite(o, Kind::Sequence);
  appendDecimalRun(o, mantissa.str(digits));
  endComposite(o, run);
  endComposite(o, t);
}

void appendDecimal(Output& o, const Value& v, const Settings& s) {
  Decimal d = v.number;
  if (s.number == NumberFormat::Fix) d = d.quantized(s.digits);
  const unsigned digits = s.number == NumberFormat::Sci ? std::max<unsigned>(1, s.digits) : 10;
  std::string text = d.str(digits);
  const bool wantSci =
      s.number == NumberFormat::Sci || (!d.zero() && (d.abs() >= Decimal("1E10") ||
                                                     d.abs() < Decimal(s.number == NumberFormat::Norm2 ? "1E-9" : "1E-2")));
  // Note: settings.engineer never reaches this branch; appendValueTo routes
  // engineering (setting or format) to appendEngineering below.
  if (s.number == NumberFormat::Sci || wantSci) {
    if (!d.zero()) {
      int exponent = 0;
      const Decimal mantissa = scaleMantissa(d, exponent);
      // The mantissa text already carries its sign; TimesTenPower keeps it.
      appendScaledPower(o, mantissa, exponent, digits);
      return;
    }
  }
  // Plain decimal: expand E-notation exactly like the engine (no precision change).
  auto ep = text.find('E');
  if (ep != std::string::npos) {
    const int exponent = std::atoi(text.c_str() + ep + 1);
    std::string mantissa = text.substr(0, ep);
    bool negative = false;
    if (!mantissa.empty() && mantissa[0] == '-') {
      negative = true;
      mantissa.erase(0, 1);
    }
    auto dot = mantissa.find('.');
    int point = dot == std::string::npos ? static_cast<int>(mantissa.size()) : static_cast<int>(dot);
    if (dot != std::string::npos) mantissa.erase(dot, 1);
    point += exponent;
    if (point <= 0)
      text = "0." + std::string(-point, '0') + mantissa;
    else if (point >= static_cast<int>(mantissa.size()))
      text = mantissa + std::string(point - mantissa.size(), '0');
    else
      text = mantissa.substr(0, point) + "." + mantissa.substr(point);
    if (negative) text = "-" + text;
  }
  if (s.number == NumberFormat::Fix) {
    auto dot = text.find('.');
    if (dot == std::string::npos && s.digits) {
      text += '.';
      dot = text.size() - 1;
    }
    if (dot != std::string::npos)
      while (text.size() - dot - 1 < s.digits) text += '0';
  }
  appendDecimalRun(o, text);
}

void appendEngineering(Output& o, const Value& v, const Settings& s) {
  Decimal d = v.number;
  if (s.number == NumberFormat::Fix) d = d.quantized(s.digits);
  const unsigned digits = s.number == NumberFormat::Sci ? std::max<unsigned>(1, s.digits) : 10;
  if (d.zero()) {
    appendDecimalRun(o, "0");
    return;
  }
  int exponent = 0;
  Decimal mantissa = scaleMantissa(d, exponent);
  while (exponent % 3) {
    mantissa = mantissa * 10;
    --exponent;
  }
  appendScaledPower(o, mantissa, exponent, digits);
}

void appendValueTo(Output& o, const Value& v, const Settings& s, Format f) {
  Format eff = f;
  if (eff == Format::Standard && (s.io == IO::MathDecimal || s.io == IO::LineDecimal)) eff = Format::Decimal;
  if (eff == Format::Standard && s.mixed) eff = Format::Mixed;
  const Exact e = v.exact(), extra = v.extra();
  // Explicit Improper/Mixed on a value with no rational exact view of its own
  // converts its decimal (never via Standard, so the mixed setting alone does
  // not turn approximate results into fractions).
  const bool rationalView = e.valid && !extra.valid && e.radicand == 1 && !e.constant() &&
                            permittedExact(e, v.number) && exactFitsWidth(e.numerator, e.denominator);
  if ((f == Format::Improper || f == Format::Mixed) && !rationalView && !extra.valid) {
    Exact approx;
    if (approximateFraction(v.number, approx) && appendExactTerm(o, approx, s, f)) return;
  }
  if (e.valid && (!permittedExact(e, v.number) ||
                  (extra.valid && (!permittedExact(extra, v.number) || !twoTermFits(e, extra)))))
    eff = Format::Decimal;
  if (extra.valid && (eff == Format::Standard || eff == Format::Mixed || eff == Format::Improper)) {
    if (s.io == IO::MathMath || s.io == IO::MathDecimal) {
      // Each display term on its own; the first keeps the whole value's
      // number for the pi magnitude rule, the second's is a dummy zero.
      Value first, second;
      first.symbolic = exactNumber(e);
      first.number = v.number;
      second.symbolic = exactNumber(extra);
      appendValueTo(o, first, s, eff);
      // The second term's sign is authoritative from its exact numerator
      // (its number slot is a dummy zero, never formatted).
      if (extra.numerator < 0) {
        appendValueTo(o, second, s, eff);
      } else {
        addSign(o, false);
        appendValueTo(o, second, s, eff);
      }
      return;
    }
    eff = Format::Decimal;
  }
  // Fallible views build directly with rollback: no temporary Outputs.
  if ((eff == Format::Standard || eff == Format::Mixed || eff == Format::Improper) && e.valid) {
    const Mark mark = markOut(o);
    if (appendExactTerm(o, e, s, eff)) return;
    rollbackOut(o, mark);
  }
  if ((eff == Format::Standard || eff == Format::Mixed || eff == Format::Improper) && !e.valid &&
      (s.io == IO::MathMath || s.io == IO::MathDecimal)) {
    const FactoredExact factored = v.factored();
    FactoredParts parts;
    if (factoredParts(factored, v.number, parts)) {
      appendFactored(o, factored, parts);
      return;
    }
  }
  if (eff == Format::Recurring && e.valid && e.radicand == 1 && !e.constant()) {
    const Mark mark = markOut(o);
    if (appendRecurring(o, e)) return;
    rollbackOut(o, mark);
  }
  if (eff == Format::Prime) {
    const Mark mark = markOut(o);
    if (appendPrime(o, v)) return;
    rollbackOut(o, mark);
  }
  if (eff == Format::Sexagesimal) {
    const Mark mark = markOut(o);
    if (appendSexagesimal(o, v)) return;
    rollbackOut(o, mark);
  }
  if (eff == Format::Engineering || s.engineer) {
    appendEngineering(o, v, s);
    return;
  }
  appendDecimal(o, v, s);
}

// Unit/compound magnitude rules live inline in appendComplexTo, read from the
// Value directly (no string search, no structural rescan). Unreduced exacts
// like 2/2 keep their fraction view; two-term exacts parenthesize.

}  // namespace

// P12 R-05: capabilities model target fx-880 availability, not every view the
// firmware can technically render. Availability follows the Standard view
// kind, mirroring appendValueTo's Standard decisions so advertised rows match
// rendered views: exact fractions offer their fraction family (+Decimal);
// integers offer Prime; irrationals and two-term exacts offer Decimal only;
// ENG/Sexagesimal belong to non-exact decimal views. In particular 3/2 shows
// only Standard/Decimal/Improper/Mixed, and √2 only Standard/Decimal.
enum class ViewKind : uint8_t { Fraction, Integer, Irrational, Decimal, TwoTerm };
// True when the reduced denominator repeats in decimal (has a prime factor
// other than 2 or 5). Terminating fractions like 3/2 get no Recurring row.
bool repeatsDecimal(int64_t den) {
  if (den <= 1) return false;
  while (den % 2 == 0) den /= 2;
  while (den % 5 == 0) den /= 5;
  return den > 1;
}
ViewKind standardView(const Value& v, const Settings& s) {
  const Exact e = v.exact(), extra = v.extra();
  if (extra.valid) {
    if ((s.io == IO::MathMath || s.io == IO::MathDecimal) && permittedExact(e, v.number) &&
        permittedExact(extra, v.number) && twoTermFits(e, extra))
      return ViewKind::TwoTerm;
    return ViewKind::Decimal;
  }
  if (!e.valid) {
    // A factored pi/e view (appendValueTo) is irrational: Decimal only.
    FactoredParts parts;
    return (s.io == IO::MathMath || s.io == IO::MathDecimal) && factoredParts(v.factored(), v.number, parts)
               ? ViewKind::Irrational
               : ViewKind::Decimal;
  }
  if (!permittedExact(e, v.number)) return ViewKind::Decimal;
  if (e.constant() || e.radicand != 1)
    return (s.io == IO::MathMath || s.io == IO::MathDecimal) ? ViewKind::Irrational : ViewKind::Decimal;
  if (e.denominator == 1) return ViewKind::Integer;
  // Width rule mirrors appendExactTerm: past 10 chars the exact view falls
  // back to decimal, so no fraction-family rows apply.
  if (!exactFitsWidth(e.numerator, e.denominator)) return ViewKind::Decimal;
  return ViewKind::Fraction;
}
static Capabilities capsForParts(const Value& value, bool complex, bool verified, Error error,
                                 const Settings& settings) {
  Capabilities c;
  c.standard = true;
  if (error != Error::None || verified) return c;
  if (complex) {
    c.decimal = true;
    c.rectangular = true;
    c.polar = true;
    return c;
  }
  c.decimal = true;
  // Prime follows the integer value itself (any view): 12 factors however it
  // is displayed, while 3/2 and √2 never do.
  int64_t n = 0;
  if (value.number.toInt(n) && n > 0 && n < 10000000000LL) c.primeFactor = true;
  switch (standardView(value, settings)) {
    case ViewKind::Fraction: {
      c.improper = true;
      c.mixed = true;
      if (repeatsDecimal(value.exact().denominator)) c.recurringDecimal = true;
      break;
    }
    case ViewKind::Integer:
      break;
    case ViewKind::Decimal: {
      c.engineering = true;
      // Sexagesimal only where the converter genuinely renders it: huge
      // magnitudes fall back to decimal, so they are not advertised.
      const Decimal x = value.number.abs();
      int64_t d = 0;
      if (x.floor().toInt(d) && d >= 0 && d <= 9999999) c.sexagesimal = true;
      // A decimal that reads as a short fraction (81/8 from a numeric
      // integral) offers the fraction rows appendValueTo converts it with.
      Exact approx;
      if (approximateFraction(value.number, approx)) {
        c.improper = true;
        c.mixed = true;
      }
      break;
    }
    case ViewKind::Irrational:
    case ViewKind::TwoTerm:
      break;
  }
  return c;
}
Capabilities capabilitiesForSummary(const ResultSummary& summary, const Settings& settings) {
  if (summary.formula) return Capabilities{};  // a CAS formula has only its own form
  return capsForParts(summary.value, summary.complex, summary.verified, summary.error, settings);
}

Capabilities capabilitiesForResult(const Result& result, const Settings& settings) {
  // Pair results reuse the single-value capabilities of their first element;
  // separators follow R-02 regardless.
  if (result.formula) return Capabilities{};
  return capsForParts(result.value, result.complex, result.verified, result.error, settings);
}

Output presentValue(const Value& value, const Settings& settings, Format format) {
  Output o = makeOutput();
  appendValueTo(o, value, settings, format);
  return o;
}

void appendComplexTo(Output& out, const Value& re, const Value& im, const Settings& settings, Format format) {
  const Format part =
      format == Format::Rectangular || format == Format::Polar ? Format::Standard : format;
  const bool polar = format == Format::Polar ||
                     (format != Format::Rectangular && !im.number.zero() &&
                      settings.complexResult == ComplexResult::Polar);
  if (polar) {
    Value r, theta;
    if (!::Calculator::toPolar(re, im, settings.angle, r, theta)) {
      appendValueTo(out, Value(0), settings, part);
      return;
    }
    appendValueTo(out, r, settings, part);
    addNode(out, Kind::Angle);
    appendValueTo(out, theta, settings, part);
    return;
  }
  // Magnitude properties read from im directly (sign-agnostic): realNegate
  // would only flip signs, and only validity/magnitude decide unit/compound.
  // The emitted run still formats the non-negative magnitude, so it never
  // carries a leading sign.
  const bool hasImag = !im.number.zero();
  int64_t mm = 0;
  const bool intOne = hasImag && im.number.toInt(mm) && (mm == 1 || mm == -1);
  const Exact ime = im.exact(), imExtra = im.extra();
  const bool unitOne =
      intOne && (!ime.valid || (ime.radicand == 1 && !ime.constant() && ime.denominator == 1 && !imExtra.valid));
  const bool compound = hasImag && imExtra.valid;
  const Value absIm = !hasImag ? Value(0) : im.number.negative() ? realNegate(im) : im;
  const bool hasReal = !re.number.zero();
  if (!hasReal && !hasImag) {
    appendValueTo(out, Value(0), settings, part);
    return;
  }
  const bool neg = im.number.negative();
  if (!hasReal) {
    if (neg) addSign(out, true);
    if (!unitOne) {
      if (compound) addWord(out, "(");
      appendValueTo(out, absIm, settings, part);
      if (compound) addWord(out, ")");
    }
    addNode(out, Kind::Imaginary);
    return;
  }
  appendValueTo(out, re, settings, part);
  if (hasImag) {
    addSign(out, neg);
    if (!unitOne) {
      if (compound) addWord(out, "(");
      appendValueTo(out, absIm, settings, part);
      if (compound) addWord(out, ")");
    }
    addNode(out, Kind::Imaginary);
  }
}

Output presentComplex(const Value& re, const Value& im, const Settings& settings, Format format) {
  Output out = makeOutput();
  appendComplexTo(out, re, im, settings, format);
  return out;
}

Output presentSummary(const ResultSummary& summary, const Settings& settings, Format format) {
  Output out = makeOutput();
  if (summary.error != Error::None || summary.verified) return out;  // text path
  if (summary.complex) {
    appendComplexTo(out, summary.value, summary.imag, settings, format);
    return out;
  }
  Format part = format;
  if (part == Format::Rectangular || part == Format::Polar) part = Format::Standard;
  appendValueTo(out, summary.value, settings, part);
  if (summary.pair) {
    addSeparator(out);
    appendValueTo(out, summary.second, settings, part);
  }
  return out;
}

namespace {
// Recursive lowering over parent links (structure from the arena, never
// reparsed). Children of P are the nodes with parent==P in index order;
// presentations hold tens of nodes, so the scans are trivial.
void emitKids(std::string& s, const Output& o, uint32_t parent, const Settings& settings);
void emitNode(std::string& s, const Output& o, uint32_t idx, const Settings& settings);
void emitKids(std::string& s, const Output& o, uint32_t parent, const Settings& settings) {
  for (uint32_t i = parent + 1; i < o.nodes.size(); ++i) {
    if (o.nodes[i].parent != parent) continue;
    if (o.nodes[i].kind == Kind::Sequence)
      emitKids(s, o, i, settings);
    else
      emitNode(s, o, i, settings);
  }
}
// Emit the k-th direct child of parent (a Sequence child emits its run).
void emitKid(std::string& s, const Output& o, uint32_t parent, uint32_t k, const Settings& settings) {
  uint32_t seen = 0;
  for (uint32_t i = parent + 1; i < o.nodes.size(); ++i) {
    if (o.nodes[i].parent != parent) continue;
    if (seen++ != k) continue;
    if (o.nodes[i].kind == Kind::Sequence)
      emitKids(s, o, i, settings);
    else
      emitNode(s, o, i, settings);
    return;
  }
}
void emitNode(std::string& s, const Output& o, uint32_t idx, const Settings& settings) {
  const Node& n = o.nodes[idx];
  const bool comma = settings.comma;
  switch (n.kind) {
    case Kind::Digits:
    case Kind::Sign:
    case Kind::Text:
      s.append(o.text, n.text, n.len);
      break;
    case Kind::Point:
      s += comma ? kCommaPoint : '.';
      break;
    case Kind::Separator:
      // R-02: dot-decimal uses comma, comma-decimal uses semicolon.
      // Pairs are top-level only, so a direct ',' never splits frac fields.
      // Comma-Point inside templates uses kCommaPoint instead (safe).
      if (comma)
        s += "; ";
      else
        s += ", ";
      break;
    case Kind::Fraction:
      s += "frac(";
      emitKid(s, o, idx, 0, settings);
      s += ",";
      emitKid(s, o, idx, 1, settings);
      s += ")";
      break;
    case Kind::Mixed:
      s += "mixed(";
      emitKid(s, o, idx, 0, settings);
      s += ",";
      emitKid(s, o, idx, 1, settings);
      s += ",";
      emitKid(s, o, idx, 2, settings);
      s += ")";
      break;
    case Kind::Radical: {
      const bool unit = n.a == 1 || n.a == -1;
      if (n.a == 0) {
        s += "0";
      } else {
        if (!unit) appendIntStr(s, n.a < 0 ? -n.a : n.a);
        if (n.a == -1) s += "-";
        s += "sqrt(";
        appendIntStr(s, n.b);
        s += ")";
      }
      break;
    }
    case Kind::Pi:
    case Kind::Euler: {
      const char* name = n.kind == Kind::Pi ? "pi" : "e";
      const bool unit = n.a == 1 || n.a == -1;
      if (n.b != 1) {
        // frac(coeff*pi, den) or frac(coeff*e, den): build without string surgery.
        s += "frac(";
        if (!unit) appendIntStr(s, n.a < 0 ? -n.a : n.a);
        s += name;
        s += ",";
        appendIntStr(s, n.b);
        s += ")";
      } else {
        if (n.a == -1) s += "-";
        if (!unit) appendIntStr(s, n.a < 0 ? -n.a : n.a);
        s += name;
      }
      break;
    }
    case Kind::TimesTenPower:
      emitKid(s, o, idx, 0, settings);
      s += "*10^(";
      appendIntStr(s, n.a);
      s += ")";
      break;
    case Kind::Recurring:
      emitKid(s, o, idx, 0, settings);
      s += "recur(";
      emitKid(s, o, idx, 1, settings);
      s += ")";
      break;
    case Kind::Degree:
      appendIntStr(s, n.a);
      s += '\xB0';
      break;
    case Kind::Minute:
      appendIntStr(s, n.a);
      s += kPrimeMinute;
      break;
    case Kind::Second:
      emitKid(s, o, idx, 0, settings);
      s += kPrimeSecond;
      break;
    case Kind::Imaginary:
      // Coefficient children are appended inline (no kids index by design).
      s += "i";
      break;
    case Kind::Angle:
      s += "@";
      break;
    case Kind::Sequence:
      emitKids(s, o, idx, settings);
      break;
    case Kind::Superscript:
      emitKid(s, o, idx, 0, settings);
      s += "^(";
      emitKid(s, o, idx, 1, settings);
      s += ")";
      break;
  }
}

void emitLinearKids(std::string& s, const Output& o, uint32_t parent, const Settings& settings);
void emitLinearNode(std::string& s, const Output& o, uint32_t idx, const Settings& settings);
void emitLinearKid(std::string& s, const Output& o, uint32_t parent, uint32_t k, const Settings& settings) {
  uint32_t seen = 0;
  for (uint32_t i = parent + 1; i < o.nodes.size(); ++i) {
    if (o.nodes[i].parent != parent) continue;
    if (seen++ != k) continue;
    if (o.nodes[i].kind == Kind::Sequence)
      emitLinearKids(s, o, i, settings);
    else
      emitLinearNode(s, o, i, settings);
    return;
  }
}
void emitLinearNode(std::string& s, const Output& o, uint32_t idx, const Settings& settings) {
  const Node& n = o.nodes[idx];
  const bool comma = settings.comma;
  switch (n.kind) {
    case Kind::Digits:
    case Kind::Sign:
    case Kind::Text:
      s.append(o.text, n.text, n.len);
      break;
    case Kind::Point:
      s += comma ? ',' : '.';
      break;
    case Kind::Separator:
      s += comma ? "; " : ", ";
      break;
    case Kind::Fraction:
      emitLinearKid(s, o, idx, 0, settings);
      s += "/";
      emitLinearKid(s, o, idx, 1, settings);
      break;
    case Kind::Mixed:
      emitLinearKid(s, o, idx, 0, settings);
      s += " ";
      emitLinearKid(s, o, idx, 1, settings);
      s += "/";
      emitLinearKid(s, o, idx, 2, settings);
      break;
    case Kind::Radical: {
      const bool unit = n.a == 1 || n.a == -1;
      if (n.a == 0) {
        s += "0";
      } else {
        if (!unit) appendIntStr(s, n.a < 0 ? -n.a : n.a);
        if (n.a == -1) s += "-";
        s += "sqrt(";
        appendIntStr(s, n.b);
        s += ")";
      }
      break;
    }
    case Kind::Pi:
    case Kind::Euler: {
      // Reaches here only via linear text; template emission above never
      // builds strings. Cases mirror it exactly (unit coeff omitted).
      const char* name = n.kind == Kind::Pi ? "pi" : "e";
      const bool unit = n.a == 1 || n.a == -1;
      if (n.b != 1) {
        if (n.a == -1)
          s += "-";
        else if (!unit)
          appendIntStr(s, n.a);
        s += name;
        s += "/";
        appendIntStr(s, n.b);
      } else {
        if (n.a == -1)
          s += "-";
        else if (!unit)
          appendIntStr(s, n.a);
        s += name;
      }
      break;
    }
    case Kind::TimesTenPower:
      emitLinearKid(s, o, idx, 0, settings);
      s += "E";
      s += (n.a >= 0 ? "+" : "");
      appendIntStr(s, n.a);
      break;
    case Kind::Recurring:
      emitLinearKid(s, o, idx, 0, settings);
      s += "(";
      emitLinearKid(s, o, idx, 1, settings);
      s += ")";
      break;
    case Kind::Degree:
      appendIntStr(s, n.a);
      s += "\xC2\xB0";  // U+00B0
      break;
    case Kind::Minute:
      appendIntStr(s, n.a);
      s += "\xE2\x80\xB2";  // U+2032
      break;
    case Kind::Second:
      emitLinearKid(s, o, idx, 0, settings);
      s += "\xE2\x80\xB3";  // U+2033
      break;
    case Kind::Imaginary:
      s += "i";
      break;
    case Kind::Angle:
      s += "@";
      break;
    case Kind::Sequence:
      emitLinearKids(s, o, idx, settings);
      break;
    case Kind::Superscript:
      emitLinearKid(s, o, idx, 0, settings);
      s += "^";
      emitLinearKid(s, o, idx, 1, settings);
      break;
  }
}
void emitLinearKids(std::string& s, const Output& o, uint32_t parent, const Settings& settings) {
  for (uint32_t i = parent + 1; i < o.nodes.size(); ++i) {
    if (o.nodes[i].parent != parent) continue;
    if (o.nodes[i].kind == Kind::Sequence)
      emitLinearKids(s, o, i, settings);
    else
      emitLinearNode(s, o, i, settings);
  }
}
}  // namespace

std::string toTemplateSource(const Output& presentation, const Settings& settings) {
  std::string s;
  emitKids(s, presentation, 0, settings);
  return s;
}

std::string toLinearText(const Output& presentation, const Settings& settings) {
  std::string s;
  emitLinearKids(s, presentation, 0, settings);
  return s;
}

}  // namespace Presentation
}  // namespace Calculator
