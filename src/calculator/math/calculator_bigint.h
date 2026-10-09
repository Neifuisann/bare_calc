#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace Calculator {
// Exact values outlive a calculation (Ans, variables, history). A board with
// external RAM can place them there, like the rest of the calculator state.
// Exhausting the heap is fatal, as for CalculatorStorage.
template <typename T> struct ExactAllocator {
  using value_type = T;
  ExactAllocator() = default;
  template <typename U> ExactAllocator(const ExactAllocator<U>&) {}
  T* allocate(size_t n) {
    const size_t bytes = n * sizeof(T);
    void* p = std::malloc(bytes);
    if (!p) std::abort();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t) { std::free(p); }
};
template <typename T, typename U> bool operator==(const ExactAllocator<T>&, const ExactAllocator<U>&) { return true; }
template <typename T, typename U> bool operator!=(const ExactAllocator<T>&, const ExactAllocator<U>&) { return false; }

// Arbitrary-precision integer. Anything that fits int64 lives inline (no heap:
// nearly every calculator integer); larger magnitudes share one immutable
// limb vector, so copies are a reference-count bump. Values never mutate.
class BigInt {
 public:
  using Limbs = std::vector<uint32_t, ExactAllocator<uint32_t>>;  // little-endian, trimmed
  BigInt() = default;
  BigInt(int64_t value) : small_(value) {}  // implicit: literals and int64 results
  // [+-]?[0-9]+ only; false leaves `out` untouched.
  static bool parse(const std::string& text, BigInt& out);
  // Non-negative value from little-endian 32-bit limbs (stored constants).
  static BigInt fromLimbs(const uint32_t* limbs, size_t count);
  std::string str() const;
  bool fitsInt64() const { return !big_; }
  int64_t toInt64() const { return small_; }  // meaningful only when fitsInt64()
  int sign() const { return big_ ? static_cast<int>(small_) : (small_ > 0) - (small_ < 0); }
  bool zero() const { return !big_ && !small_; }
  bool odd() const { return big_ ? ((*big_)[0] & 1u) != 0 : (small_ & 1) != 0; }
  size_t bits() const;  // bit length of |x|; 0 for zero
  size_t heapBytes() const { return big_ ? big_->capacity() * sizeof(uint32_t) + 32 : 0; }
  BigInt operator-() const;
  BigInt abs() const { return sign() < 0 ? -*this : *this; }
  BigInt shifted(int64_t bits) const;  // |x|*2^bits, right shifts truncate; sign kept
  BigInt pow(uint32_t exponent) const;
  BigInt root(uint32_t k) const;  // floor(x^(1/k)) for x >= 0, k >= 1
  bool exactRoot(uint32_t k, BigInt& root) const;  // x >= 0 is a perfect k-th power
  uint32_t modSmall(uint32_t divisor) const;       // |x| mod divisor, divisor > 0
  // Truncating division (C semantics): remainder takes the dividend's sign.
  static void divMod(const BigInt& a, const BigInt& b, BigInt& quotient, BigInt& remainder);
  static int compare(const BigInt& a, const BigInt& b);
  static BigInt gcd(const BigInt& a, const BigInt& b);  // non-negative
  friend BigInt operator+(const BigInt& a, const BigInt& b);
  friend BigInt operator-(const BigInt& a, const BigInt& b);
  friend BigInt operator*(const BigInt& a, const BigInt& b);
  friend BigInt operator/(const BigInt& a, const BigInt& b);
  friend BigInt operator%(const BigInt& a, const BigInt& b);

 private:
  // Small: the value. Big: the sign (+1/-1) with the magnitude in big_.
  int64_t small_ = 0;
  std::shared_ptr<const Limbs> big_;
  Limbs magnitude() const;
  static BigInt make(Limbs&& magnitude, bool negative);
};
inline bool operator==(const BigInt& a, const BigInt& b) { return BigInt::compare(a, b) == 0; }
inline bool operator!=(const BigInt& a, const BigInt& b) { return BigInt::compare(a, b) != 0; }
inline bool operator<(const BigInt& a, const BigInt& b) { return BigInt::compare(a, b) < 0; }
inline bool operator>(const BigInt& a, const BigInt& b) { return BigInt::compare(a, b) > 0; }
inline bool operator<=(const BigInt& a, const BigInt& b) { return BigInt::compare(a, b) <= 0; }
inline bool operator>=(const BigInt& a, const BigInt& b) { return BigInt::compare(a, b) >= 0; }

// Normalized fraction: den > 0 and gcd(num, den) = 1, so equal values are
// equal representations (0 is 0/1).
class Rational {
 public:
  Rational() = default;
  Rational(int64_t n) : num_(n) {}
  Rational(const BigInt& n) : num_(n) {}
  static Rational of(const BigInt& numerator, const BigInt& denominator);  // denominator != 0
  const BigInt& num() const { return num_; }
  const BigInt& den() const { return den_; }
  bool zero() const { return num_.zero(); }
  int sign() const { return num_.sign(); }
  bool integer() const { return den_ == BigInt(1); }
  bool isOne() const { return integer() && num_ == BigInt(1); }
  size_t bits() const { return num_.bits() > den_.bits() ? num_.bits() : den_.bits(); }
  size_t heapBytes() const { return num_.heapBytes() + den_.heapBytes(); }
  Rational operator-() const;
  Rational inverse() const;  // nonzero only
  Rational abs() const { return sign() < 0 ? -*this : *this; }
  BigInt floor() const;
  static int compare(const Rational& a, const Rational& b);
  friend Rational operator+(const Rational& a, const Rational& b);
  friend Rational operator-(const Rational& a, const Rational& b);
  friend Rational operator*(const Rational& a, const Rational& b);
  friend Rational operator/(const Rational& a, const Rational& b);  // b nonzero

 private:
  BigInt num_{0}, den_{1};
};
inline bool operator==(const Rational& a, const Rational& b) { return a.num() == b.num() && a.den() == b.den(); }
inline bool operator!=(const Rational& a, const Rational& b) { return !(a == b); }
}  // namespace Calculator
