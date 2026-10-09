#include "calculator/math/calculator_basen.h"
#include <cctype>
#include <cstring>

namespace Calculator {
namespace {
constexpr int64_t kMin = -2147483648LL, kMax = 2147483647LL;

class Parser {
 public:
  Parser(const Request& request, uint8_t base, const std::function<bool()>& cancelled)
      : s(request.expression), q(request), base_(base), cancel(cancelled) {}
  Result run() {
    Result r;
    r.memory = q.memory;
    skip();
    int64_t v = expression(0, 0);
    if (error == Error::None && at < s.size()) fail(Error::Syntax, at);
    if (error == Error::None && (v < kMin || v > kMax)) fail(Error::Math, 0);
    if (error != Error::None) {
      r.error = error;
      r.position = position;
      return r;
    }
    r.value = Value(static_cast<int>(v));
    r.memory.ans = r.value;
    return r;
  }

 private:
  const std::string& s;
  const Request& q;
  uint8_t base_;
  const std::function<bool()>& cancel;
  size_t at = 0, position = 0;
  Error error = Error::None;

  int64_t fail(Error e, size_t p) {
    if (error == Error::None) { error = e; position = p; }
    return 0;
  }
  void skip() { while (at < s.size() && s[at] == ' ') ++at; }
  bool word(const char* w) {
    const size_t n = std::strlen(w);
    if (s.compare(at, n, w) != 0 || (at + n < s.size() && std::isalnum(static_cast<unsigned char>(s[at + n])))) return false;
    at += n;
    skip();
    return true;
  }
  int64_t range(int64_t v, size_t p) { return v < kMin || v > kMax ? fail(Error::Math, p) : v; }
  static uint32_t bits(int64_t v) { return static_cast<uint32_t>(static_cast<uint64_t>(v)); }
  static int64_t signedOf(uint32_t b) { return b & 0x80000000u ? static_cast<int64_t>(b) - 4294967296LL : b; }

  // Binary precedence (manual p.196): * / above + -, both above and, which is
  // above or/xor/xnor. Implicit multiplication binds like *.
  int64_t expression(int min, unsigned depth) {
    if (depth > 32) return fail(Error::Stack, at);
    if (cancel && cancel()) return fail(Error::Cancelled, at);
    int64_t a = unary(depth);
    while (error == Error::None) {
      const size_t p = at;
      int prec;
      char op;
      if (at < s.size() && (s[at] == '*' || s[at] == '/')) { prec = 20; op = s[at]; }
      else if (at < s.size() && (s[at] == '+' || s[at] == '-')) { prec = 10; op = s[at]; }
      else if (s.compare(at, 3, "and") == 0) { prec = 6; op = '&'; }
      else if (s.compare(at, 4, "xnor") == 0) { prec = 4; op = 'n'; }
      else if (s.compare(at, 3, "xor") == 0) { prec = 4; op = 'x'; }
      else if (s.compare(at, 2, "or") == 0) { prec = 4; op = '|'; }
      else if (at < s.size() && primary()) { prec = 20; op = 'i'; }  // implicit multiplication
      else break;
      if (prec < min) break;
      if (op == '&') word("and");
      else if (op == 'n') word("xnor");
      else if (op == 'x') word("xor");
      else if (op == '|') word("or");
      else if (op != 'i') { ++at; skip(); }
      const int64_t b = expression(prec + 1, depth + 1);
      if (error != Error::None) return 0;
      switch (op) {
        case '+': a = range(a + b, p); break;
        case '-': a = range(a - b, p); break;
        case '*': case 'i': a = range(a * b, p); break;  // |a|,|b| <= 2^31: no int64 overflow
        case '/':
          if (b == 0) return fail(Error::Math, p);
          a = range(a / b, p);  // C++ truncates toward zero; INT_MIN/-1 fails the range
          break;
        case '&': a = signedOf(bits(a) & bits(b)); break;
        case '|': a = signedOf(bits(a) | bits(b)); break;
        case 'x': a = signedOf(bits(a) ^ bits(b)); break;
        default: a = signedOf(~(bits(a) ^ bits(b))); break;
      }
    }
    return a;
  }
  bool primary() const {
    const char c = s[at];
    return std::isalnum(static_cast<unsigned char>(c)) || c == '(' || c == '$' || c == '#' || c == '&' || c == '|';
  }
  int64_t unary(unsigned depth) {
    if (depth > 32) return fail(Error::Stack, at);
    if (cancel && cancel()) return fail(Error::Cancelled, at);
    if (at < s.size() && (s[at] == '-' || s[at] == '+')) {
      const bool minus = s[at] == '-';
      const size_t p = at++;
      skip();
      const int64_t v = unary(depth + 1);
      return minus ? range(-v, p) : v;
    }
    return atom(depth);
  }
  int64_t call(unsigned depth, bool negate) {
    const size_t p = at;
    at += 4;  // "neg(" / "not("
    skip();
    const int64_t v = expression(0, depth + 1);
    if (error != Error::None) return 0;
    if (at >= s.size() || s[at] != ')') {
      if (at < s.size()) return fail(Error::Syntax, at);
    } else {
      ++at;
      skip();
    }
    return negate ? range(-v, p) : signedOf(~bits(v));
  }
  int64_t atom(unsigned depth) {
    if (at >= s.size()) return fail(Error::Syntax, at);
    const size_t p = at;
    const char c = s[at];
    if (c == '(') {
      ++at;
      skip();
      const int64_t v = expression(0, depth + 1);
      if (error != Error::None) return 0;
      if (at < s.size() && s[at] == ')') { ++at; skip(); }
      else if (at < s.size()) return fail(Error::Syntax, at);
      return v;
    }
    if (s.compare(at, 4, "neg(") == 0) return call(depth, true);
    if (s.compare(at, 4, "not(") == 0) return call(depth, false);
    if (s.compare(at, 6, "PreAns") == 0) return fail(Error::Syntax, p);
    if (s.compare(at, 3, "Ans") == 0) { at += 3; skip(); return scalar(q.memory.ans, p); }
    if (s.compare(at, 4, "var(") == 0 && at + 5 < s.size() && s[at + 5] == ')') {
      const int index = variableIndex(std::string(1, s[at + 4]));
      if (index < 0) return fail(Error::Syntax, p);
      at += 6;
      skip();
      return scalar(q.memory.variables[index], p);
    }
    uint8_t literalBase = base_;
    bool prefixed = true;
    switch (c) {
      case '$': literalBase = 10; break;
      case '#': literalBase = 16; break;
      case '&': literalBase = 2; break;
      case '|': literalBase = 8; break;
      default: prefixed = false; break;
    }
    if (prefixed) ++at;
    const size_t start = at;
    uint64_t v = 0;
    while (at < s.size() && (std::isdigit(static_cast<unsigned char>(s[at])) || (s[at] >= 'A' && s[at] <= 'F'))) {
      const unsigned digit = s[at] <= '9' ? s[at] - '0' : s[at] - 'A' + 10;
      if (digit >= literalBase) return fail(Error::Syntax, at);
      v = v * literalBase + digit;
      if (v > 0xFFFFFFFFull) return fail(Error::Math, p);
      ++at;
    }
    if (at == start) return fail(Error::Syntax, at);
    skip();
    // A decimal literal is a magnitude (2147483648 is valid only under a
    // minus); the others spell the 32-bit two's-complement pattern.
    if (literalBase == 10) return v > 2147483648ull ? fail(Error::Math, p) : static_cast<int64_t>(v);
    return signedOf(static_cast<uint32_t>(v));
  }
  // A scalar recalled into Base-N must already be an in-range integer; its
  // decimal value is never reinterpreted bit by bit (plan O-6).
  int64_t scalar(const ScalarValue& v, size_t p) {
    int64_t n;
    if (v.complex || !v.re.number.toInt(n) || n < kMin || n > kMax) return fail(Error::Math, p);
    return n;
  }
};
}  // namespace

Result evaluateBaseN(const Request& request, uint8_t base, const std::function<bool()>& cancelled) {
  return Parser(request, base, cancelled).run();
}

std::string formatBaseN(int32_t value, uint8_t base) {
  if (base == 10) return std::to_string(value);
  uint32_t b = static_cast<uint32_t>(value);
  const unsigned width = base == 16 ? 8 : base == 2 ? 32 : 11;
  std::string out(width, '0');
  for (unsigned i = width; i-- > 0 && b;) {
    out[i] = "0123456789ABCDEF"[b % base];
    b /= base;
  }
  return out;
}

uint8_t nextBase(uint8_t base) { return base == 10 ? 16 : base == 16 ? 2 : base == 2 ? 8 : 10; }

const char* baseName(uint8_t base) {
  return base == 16 ? "Hex" : base == 2 ? "Bin" : base == 8 ? "Oct" : "Dec";
}

}  // namespace Calculator
