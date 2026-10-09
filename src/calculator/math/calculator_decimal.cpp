#include "calculator/math/calculator_decimal.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Calculator {
namespace {
decContext context(unsigned digits = 40) {
  decContext c;
  decContextDefault(&c, DEC_INIT_BASE);
  c.digits = digits;
  c.emax = 999999;
  c.emin = -999999;
  c.traps = 0;
  c.round = DEC_ROUND_HALF_UP;
  return c;
}
}
Decimal::Decimal() { decNumberZero(&number_); }
Decimal::Decimal(int n) { decNumberFromInt32(&number_, n); }
Decimal::Decimal(const char* s) {
  auto c = context(); decNumberFromString(&number_, s, &c);
}
Decimal Decimal::nan() { return Decimal("NaN"); }
Decimal Decimal::pi() { return Decimal("3.141592653589793238462643383279502884197"); }
bool Decimal::finite() const { return decNumberIsFinite(&number_); }
bool Decimal::zero() const { return decNumberIsZero(&number_); }
bool Decimal::negative() const { return decNumberIsNegative(&number_) && !zero(); }
Decimal Decimal::rounded(unsigned digits) const {
  auto c = context(digits); Decimal r; decNumberPlus(&r.number_, &number_, &c); return r;
}
std::string Decimal::str(unsigned digits) const {
  char s[DECNUMDIGITS + 16]; auto r = rounded(digits);
  decNumberTrim(&r.number_); decNumberToString(&r.number_, s); return s;
}
Decimal Decimal::quantized(int places) const {
  auto c = context(); Decimal r, e(1); e.number_.exponent = -places;
  decNumberQuantize(&r.number_, &number_, &e.number_, &c); return r;
}
Decimal Decimal::trunc() const {
  auto c = context(); c.round = DEC_ROUND_DOWN; Decimal r;
  decNumberToIntegralValue(&r.number_, &number_, &c); return r;
}
Decimal Decimal::floor() const {
  auto c = context(); c.round = DEC_ROUND_FLOOR; Decimal r;
  decNumberToIntegralValue(&r.number_, &number_, &c); return r;
}
bool Decimal::integer() const { return finite() && *this == trunc(); }
bool Decimal::toInt(int64_t& out) const {
  if (!integer() || *this < Decimal("-9223372036854775808") ||
      *this > Decimal("9223372036854775807")) return false;
  char s[DECNUMDIGITS + 16]; Decimal r = quantized(0);
  decNumberToString(&r.number_, s);
  char* end = nullptr; errno = 0;
  const auto value = std::strtoll(s, &end, 10);
  if (errno || !end || *end) return false;
  out = value; return true;
}
int Decimal::compare(const Decimal& b) const {
  auto c = context(); Decimal r;
  decNumberCompare(&r.number_, &number_, &b.number_, &c);
  if (!r.finite()) return 2;  // callers reject NaN before comparisons
  return r.zero() ? 0 : r.negative() ? -1 : 1;
}
#define BINARY(OP, FN) Decimal operator OP(const Decimal& a, const Decimal& b) { \
  auto c = context(); Decimal r; FN(&r.number_, &a.number_, &b.number_, &c); return r; }
BINARY(+, decNumberAdd)
BINARY(-, decNumberSubtract)
BINARY(*, decNumberMultiply)
BINARY(/, decNumberDivide)
BINARY(%, decNumberRemainder)
#undef BINARY
#define UNARY(NAME, FN) Decimal Decimal::NAME() const { auto c = context(); Decimal r; \
  FN(&r.number_, &number_, &c); return r; }
UNARY(abs, decNumberAbs)
UNARY(sqrt, decNumberSquareRoot)
UNARY(ln, decNumberLn)
UNARY(exp, decNumberExp)
#undef UNARY
Decimal Decimal::operator-() const { return Decimal(0) - *this; }
Decimal Decimal::pow(const Decimal& p) const {
  auto c = context(); Decimal r;
  decNumberPower(&r.number_, &number_, &p.number_, &c); return r;
}
// decNumberPower takes a fractional exponent through exp(ln) at 40 digits,
// ~35x the cost of a square root: x^(1/3) in an integrand dominated
// integral() runs. Newton on y^n = x instead, y <- ((n-1)y + x/y^(n-1))/n,
// whose y^(n-1) is decNumber's cheap integer power. A double seed (the only
// binary float here, and only a seed) carries ~16 digits, so three steps
// reach the 40-digit working precision. The exponent is split off first --
// x = m*10^(qn+rem) -- since decimal exponents run far past a double's.
Decimal Decimal::root(unsigned n) const {
  if (!finite() || negative() || n == 0) return nan();
  if (zero() || n == 1) return *this;
  if (n == 2) return sqrt();
  const int32_t adjusted = number_.exponent + number_.digits - 1;
  const int32_t whole = adjusted >= 0 ? adjusted / int32_t(n) : -((-adjusted + int32_t(n) - 1) / int32_t(n));
  Decimal m = *this; m.number_.exponent -= whole * int32_t(n);  // m in [1, 10^n)
  const double seed = std::pow(std::strtod(m.str(17).c_str(), nullptr), 1.0 / n);
  if (!(seed > 0) || seed > 1e300) return pow(Decimal(1) / Decimal(int(n)));
  char s[32]; std::snprintf(s, sizeof s, "%.17g", seed);
  Decimal y(s); y.number_.exponent += whole;
  const Decimal k(static_cast<int>(n)), k1(static_cast<int>(n - 1));
  for (int i = 0; i < 6; ++i) {
    const Decimal next = (y * k1 + *this / y.pow(k1)) / k;
    if (next == y) break;
    y = next;
  }
  return y;
}
Decimal Decimal::sin() const {
  if (!finite()) return nan();
  Decimal x = *this % (pi()*2);
  if (x > pi()) x = x - pi()*2;
  if (x < -pi()) x = x + pi()*2;
  if (x > pi()/2) x = pi()-x;
  if (x < -pi()/2) x = -pi()-x;
  const Decimal xx = -x*x;
  Decimal term = x, sum = x;
  for (int i=1; i<100; ++i) {
    term = term*xx/Decimal((2*i)*(2*i+1));
    const Decimal next = sum+term;
    if (next == sum) break;
    sum = next;
  }
  return sum;
}
Decimal Decimal::cos() const { return (*this + pi()/2).sin(); }
Decimal Decimal::atan() const {
  if (!finite()) return nan();
  if (negative()) return -(-*this).atan();
  if (*this > Decimal(1)) return pi()/2-(Decimal(1)/ *this).atan();
  // Half-angle reduction makes the alternating series converge rapidly even
  // at x=1, without libm or binary range reduction.
  Decimal x = *this; int scale = 1;
  while (x > Decimal("0.1")) { x=x/(1+(1+x*x).sqrt()); scale*=2; }
  Decimal power=x, sum=x, xx=-x*x;
  for (int i=1; i<200; ++i) {
    power=power*xx; Decimal next=sum+power/Decimal(2*i+1);
    if (next == sum) break;
    sum=next;
  }
  return sum*Decimal(scale);
}
}  // namespace Calculator
