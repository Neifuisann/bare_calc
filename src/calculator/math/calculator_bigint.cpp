#include "calculator/math/calculator_bigint.h"
#include <algorithm>

namespace Calculator {
namespace {
using Limbs = BigInt::Limbs;

void trim(Limbs& a) {
  while (!a.empty() && !a.back()) a.pop_back();
}
int compareMagnitude(const Limbs& a, const Limbs& b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  for (size_t i = a.size(); i-- > 0;)
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}
Limbs addMagnitude(const Limbs& a, const Limbs& b) {
  const Limbs& longer = a.size() >= b.size() ? a : b;
  const Limbs& shorter = a.size() >= b.size() ? b : a;
  Limbs r(longer.size() + 1, 0);
  uint64_t carry = 0;
  for (size_t i = 0; i < longer.size(); ++i) {
    carry += static_cast<uint64_t>(longer[i]) + (i < shorter.size() ? shorter[i] : 0u);
    r[i] = static_cast<uint32_t>(carry);
    carry >>= 32;
  }
  r[longer.size()] = static_cast<uint32_t>(carry);
  trim(r);
  return r;
}
// a >= b.
Limbs subtractMagnitude(const Limbs& a, const Limbs& b) {
  Limbs r(a.size(), 0);
  int64_t borrow = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    int64_t d = static_cast<int64_t>(a[i]) - (i < b.size() ? b[i] : 0u) - borrow;
    borrow = d < 0;
    if (d < 0) d += int64_t(1) << 32;
    r[i] = static_cast<uint32_t>(d);
  }
  trim(r);
  return r;
}
Limbs multiplyMagnitude(const Limbs& a, const Limbs& b) {
  if (a.empty() || b.empty()) return {};
  Limbs r(a.size() + b.size(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    uint64_t carry = 0;
    for (size_t j = 0; j < b.size(); ++j) {
      carry += static_cast<uint64_t>(a[i]) * b[j] + r[i + j];
      r[i + j] = static_cast<uint32_t>(carry);
      carry >>= 32;
    }
    r[i + b.size()] = static_cast<uint32_t>(carry);
  }
  trim(r);
  return r;
}
Limbs shiftLeftMagnitude(const Limbs& a, size_t bits) {
  if (a.empty()) return {};
  const size_t words = bits / 32, rest = bits % 32;
  Limbs r(a.size() + words + 1, 0);
  for (size_t i = 0; i < a.size(); ++i) {
    const uint64_t v = static_cast<uint64_t>(a[i]) << rest;
    r[i + words] |= static_cast<uint32_t>(v);
    r[i + words + 1] |= static_cast<uint32_t>(v >> 32);
  }
  trim(r);
  return r;
}
Limbs shiftRightMagnitude(const Limbs& a, size_t bits) {
  const size_t words = bits / 32, rest = bits % 32;
  if (words >= a.size()) return {};
  Limbs r(a.size() - words, 0);
  for (size_t i = 0; i < r.size(); ++i) {
    uint64_t v = a[i + words] >> rest;
    if (rest && i + words + 1 < a.size()) v |= static_cast<uint64_t>(a[i + words + 1]) << (32 - rest);
    r[i] = static_cast<uint32_t>(v);
  }
  trim(r);
  return r;
}
// Knuth algorithm D (Hacker's Delight divmnu); v nonzero and trimmed.
void divideMagnitude(const Limbs& u, const Limbs& v, Limbs& q, Limbs& r) {
  if (compareMagnitude(u, v) < 0) { q.clear(); r = u; return; }
  if (v.size() == 1) {
    uint64_t rem = 0;
    q.assign(u.size(), 0);
    for (size_t i = u.size(); i-- > 0;) {
      const uint64_t cur = (rem << 32) | u[i];
      q[i] = static_cast<uint32_t>(cur / v[0]);
      rem = cur % v[0];
    }
    trim(q);
    r.clear();
    if (rem) r.push_back(static_cast<uint32_t>(rem));
    return;
  }
  const int s = __builtin_clz(v.back());
  const size_t n = v.size(), m = u.size() - n;
  Limbs vn = shiftLeftMagnitude(v, static_cast<size_t>(s));
  Limbs un = shiftLeftMagnitude(u, static_cast<size_t>(s));
  un.resize(u.size() + 1, 0);
  vn.resize(n, 0);
  q.assign(m + 1, 0);
  const uint64_t base = uint64_t(1) << 32;
  for (size_t j = m + 1; j-- > 0;) {
    const uint64_t numerator = (static_cast<uint64_t>(un[j + n]) << 32) | un[j + n - 1];
    uint64_t qhat = numerator / vn[n - 1], rhat = numerator % vn[n - 1];
    while (qhat >= base || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
      --qhat;
      rhat += vn[n - 1];
      if (rhat >= base) break;
    }
    int64_t k = 0, t = 0;
    for (size_t i = 0; i < n; ++i) {
      const uint64_t p = qhat * vn[i];
      t = static_cast<int64_t>(un[i + j]) - k - static_cast<int64_t>(p & 0xFFFFFFFFu);
      un[i + j] = static_cast<uint32_t>(t);
      k = static_cast<int64_t>(p >> 32) - (t >> 32);
    }
    t = static_cast<int64_t>(un[j + n]) - k;
    un[j + n] = static_cast<uint32_t>(t);
    q[j] = static_cast<uint32_t>(qhat);
    if (t < 0) {  // qhat was one too large: add the divisor back
      --q[j];
      uint64_t carry = 0;
      for (size_t i = 0; i < n; ++i) {
        carry += static_cast<uint64_t>(un[i + j]) + vn[i];
        un[i + j] = static_cast<uint32_t>(carry);
        carry >>= 32;
      }
      un[j + n] = static_cast<uint32_t>(un[j + n] + carry);
    }
  }
  trim(q);
  un.resize(n);
  trim(un);
  r = shiftRightMagnitude(un, static_cast<size_t>(s));
}
uint64_t unsignedAbs(int64_t v) { return v < 0 ? uint64_t(0) - static_cast<uint64_t>(v) : static_cast<uint64_t>(v); }
}  // namespace

Limbs BigInt::magnitude() const {
  if (big_) return *big_;
  Limbs r;
  const uint64_t v = unsignedAbs(small_);
  if (v) r.push_back(static_cast<uint32_t>(v));
  if (v >> 32) r.push_back(static_cast<uint32_t>(v >> 32));
  return r;
}

BigInt BigInt::make(Limbs&& mag, bool negative) {
  trim(mag);
  BigInt r;
  if (mag.size() <= 2) {
    const uint64_t v = (mag.empty() ? 0 : mag[0]) | (mag.size() > 1 ? static_cast<uint64_t>(mag[1]) << 32 : 0);
    if (!negative && v <= static_cast<uint64_t>(INT64_MAX)) { r.small_ = static_cast<int64_t>(v); return r; }
    if (negative && v <= uint64_t(1) << 63) { r.small_ = static_cast<int64_t>(~v + 1); return r; }
  }
  r.small_ = negative ? -1 : 1;
  r.big_ = std::allocate_shared<const Limbs>(ExactAllocator<Limbs>(), std::move(mag));
  return r;
}

BigInt BigInt::fromLimbs(const uint32_t* limbs, size_t count) { return make(Limbs(limbs, limbs + count), false); }

bool BigInt::parse(const std::string& text, BigInt& out) {
  size_t i = 0;
  bool negative = false;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
  if (i == text.size()) return false;
  BigInt value;
  while (i < text.size()) {
    int64_t chunk = 0, scale = 1;
    for (int d = 0; d < 9 && i < text.size(); ++d, ++i) {
      if (text[i] < '0' || text[i] > '9') return false;
      chunk = chunk * 10 + (text[i] - '0');
      scale *= 10;
    }
    value = value * BigInt(scale) + BigInt(chunk);
  }
  out = negative ? -value : value;
  return true;
}

std::string BigInt::str() const {
  if (!big_) return std::to_string(small_);
  Limbs mag = *big_, q, r;
  const Limbs billion{1000000000u};
  std::vector<uint32_t> chunks;
  while (!mag.empty()) {
    divideMagnitude(mag, billion, q, r);
    chunks.push_back(r.empty() ? 0 : r[0]);
    mag.swap(q);
  }
  std::string s = sign() < 0 ? "-" : "";
  s += std::to_string(chunks.back());
  for (size_t i = chunks.size() - 1; i-- > 0;) {
    const std::string part = std::to_string(chunks[i]);
    s += std::string(9 - part.size(), '0') + part;
  }
  return s;
}

size_t BigInt::bits() const {
  if (!big_) {
    const uint64_t v = unsignedAbs(small_);
    return v ? 64 - static_cast<size_t>(__builtin_clzll(v)) : 0;
  }
  return (big_->size() - 1) * 32 + 32 - static_cast<size_t>(__builtin_clz(big_->back()));
}

BigInt BigInt::operator-() const {
  if (!big_ && small_ != INT64_MIN) return BigInt(-small_);
  return make(magnitude(), sign() > 0);
}

BigInt BigInt::shifted(int64_t bits) const {
  if (zero() || !bits) return *this;
  if (bits > 0) {
    if (!big_ && this->bits() + static_cast<size_t>(bits) <= 62) return BigInt(small_ * (int64_t(1) << bits));
    return make(shiftLeftMagnitude(magnitude(), static_cast<size_t>(bits)), sign() < 0);
  }
  // Negating INT64_MIN overflows, and narrowing a large count to a 32-bit
  // target's size_t can wrap. Discard all bits before either operation.
  const uint64_t count = unsignedAbs(bits);
  if (count >= this->bits()) return BigInt(0);
  if (!big_) {
    const uint64_t v = unsignedAbs(small_) >> count;
    return small_ < 0 ? -BigInt(static_cast<int64_t>(v)) : BigInt(static_cast<int64_t>(v));
  }
  return make(shiftRightMagnitude(*big_, static_cast<size_t>(count)), sign() < 0);
}

BigInt operator+(const BigInt& a, const BigInt& b) {
  int64_t r;
  if (!a.big_ && !b.big_ && !__builtin_add_overflow(a.small_, b.small_, &r)) return BigInt(r);
  const Limbs x = a.magnitude(), y = b.magnitude();
  if ((a.sign() < 0) == (b.sign() < 0)) return BigInt::make(addMagnitude(x, y), a.sign() < 0);
  const int c = compareMagnitude(x, y);
  if (!c) return BigInt(0);
  return c > 0 ? BigInt::make(subtractMagnitude(x, y), a.sign() < 0) : BigInt::make(subtractMagnitude(y, x), b.sign() < 0);
}
BigInt operator-(const BigInt& a, const BigInt& b) {
  int64_t r;
  if (!a.big_ && !b.big_ && !__builtin_sub_overflow(a.small_, b.small_, &r)) return BigInt(r);
  return a + (-b);
}
BigInt operator*(const BigInt& a, const BigInt& b) {
  int64_t r;
  if (!a.big_ && !b.big_ && !__builtin_mul_overflow(a.small_, b.small_, &r)) return BigInt(r);
  return BigInt::make(multiplyMagnitude(a.magnitude(), b.magnitude()), (a.sign() < 0) != (b.sign() < 0));
}
void BigInt::divMod(const BigInt& a, const BigInt& b, BigInt& quotient, BigInt& remainder) {
  if (!a.big_ && !b.big_ && !(a.small_ == INT64_MIN && b.small_ == -1)) {
    const int64_t q = a.small_ / b.small_, r = a.small_ % b.small_;
    quotient = BigInt(q);
    remainder = BigInt(r);
    return;
  }
  Limbs q, r;
  divideMagnitude(a.magnitude(), b.magnitude(), q, r);
  quotient = make(std::move(q), (a.sign() < 0) != (b.sign() < 0));
  remainder = make(std::move(r), a.sign() < 0);
}
BigInt operator/(const BigInt& a, const BigInt& b) {
  BigInt q, r;
  BigInt::divMod(a, b, q, r);
  return q;
}
BigInt operator%(const BigInt& a, const BigInt& b) {
  BigInt q, r;
  BigInt::divMod(a, b, q, r);
  return r;
}

int BigInt::compare(const BigInt& a, const BigInt& b) {
  if (!a.big_ && !b.big_) return (a.small_ > b.small_) - (a.small_ < b.small_);
  if (a.sign() != b.sign()) return a.sign() < b.sign() ? -1 : 1;
  const int c = compareMagnitude(a.magnitude(), b.magnitude());
  return a.sign() < 0 ? -c : c;
}

BigInt BigInt::gcd(const BigInt& a, const BigInt& b) {
  BigInt x = a.abs(), y = b.abs();
  while (!y.zero()) {
    if (!x.big_ && !y.big_) {
      uint64_t u = static_cast<uint64_t>(x.small_), v = static_cast<uint64_t>(y.small_);
      while (v) { const uint64_t t = u % v; u = v; v = t; }
      return BigInt(static_cast<int64_t>(u));
    }
    BigInt r = x % y;
    x = y;
    y = r;
  }
  return x;
}

BigInt BigInt::pow(uint32_t exponent) const {
  BigInt result(1), base = *this;
  while (exponent) {
    if (exponent & 1u) result = result * base;
    exponent >>= 1;
    if (exponent) base = base * base;
  }
  return result;
}

BigInt BigInt::root(uint32_t k) const {
  if (zero() || k == 1) return *this;
  // Integer Newton from above: 2^ceil(bits/k) >= the root, and the iterates
  // decrease monotonically to floor(x^(1/k)).
  BigInt x = BigInt(1).shifted(static_cast<int64_t>((bits() + k - 1) / k));
  const BigInt kk(static_cast<int64_t>(k)), km1(static_cast<int64_t>(k - 1));
  for (;;) {
    const BigInt y = (x * km1 + *this / x.pow(k - 1)) / kk;
    if (y >= x) return x;
    x = y;
  }
}

bool BigInt::exactRoot(uint32_t k, BigInt& result) const {
  if (sign() < 0) return false;
  const BigInt r = root(k);
  if (r.pow(k) != *this) return false;
  result = r;
  return true;
}

uint32_t BigInt::modSmall(uint32_t divisor) const {
  if (!big_) return static_cast<uint32_t>(unsignedAbs(small_) % divisor);
  uint64_t rem = 0;
  for (size_t i = big_->size(); i-- > 0;) rem = ((rem << 32) | (*big_)[i]) % divisor;
  return static_cast<uint32_t>(rem);
}

Rational Rational::of(const BigInt& n, const BigInt& d) {
  Rational r;
  const BigInt g = BigInt::gcd(n, d);
  r.num_ = n / g;
  r.den_ = d / g;
  if (r.den_.sign() < 0) { r.num_ = -r.num_; r.den_ = -r.den_; }
  return r;
}
Rational Rational::operator-() const { Rational r = *this; r.num_ = -num_; return r; }
Rational Rational::inverse() const { return of(den_, num_); }
BigInt Rational::floor() const {
  BigInt q, r;
  BigInt::divMod(num_, den_, q, r);
  return r.sign() < 0 ? q - BigInt(1) : q;
}
int Rational::compare(const Rational& a, const Rational& b) {
  if (a.den_ == b.den_) return BigInt::compare(a.num_, b.num_);
  return BigInt::compare(a.num_ * b.den_, b.num_ * a.den_);
}
Rational operator+(const Rational& a, const Rational& b) {
  if (a.integer() && b.integer()) return Rational(a.num_ + b.num_);
  return Rational::of(a.num_ * b.den_ + b.num_ * a.den_, a.den_ * b.den_);
}
Rational operator-(const Rational& a, const Rational& b) { return a + (-b); }
Rational operator*(const Rational& a, const Rational& b) {
  if (a.integer() && b.integer()) return Rational(a.num_ * b.num_);
  // Cross-cancel first so intermediates stay as small as the result.
  const BigInt g1 = BigInt::gcd(a.num_, b.den_), g2 = BigInt::gcd(b.num_, a.den_);
  Rational r;
  r.num_ = (a.num_ / g1) * (b.num_ / g2);
  r.den_ = (a.den_ / g2) * (b.den_ / g1);
  return r;
}
Rational operator/(const Rational& a, const Rational& b) { return a * b.inverse(); }
}  // namespace Calculator
