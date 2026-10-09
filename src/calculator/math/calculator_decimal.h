#pragma once

#include <cstdint>
#include <string>
#define DECNUMDIGITS 40
extern "C" {
#include <decNumber.h>
}

namespace Calculator {
// Value type: no global decimal context, allocation ownership or binary float.
// 40-digit work precision is deliberately separate from calculator storage.
class Decimal {
 public:
  Decimal();
  Decimal(int value);
  explicit Decimal(const char* value);
  explicit Decimal(const std::string& value) : Decimal(value.c_str()) {}
  std::string str(unsigned digits = 23) const;
  Decimal rounded(unsigned digits = 23) const;
  Decimal quantized(int places) const;
  bool finite() const;
  bool zero() const;
  bool negative() const;
  bool integer() const;
  bool toInt(int64_t& value) const;
  int compare(const Decimal& other) const;
  Decimal operator-() const;
  Decimal abs() const;
  Decimal floor() const;
  Decimal trunc() const;
  Decimal sqrt() const;
  Decimal ln() const;
  Decimal exp() const;
  Decimal pow(const Decimal& exponent) const;
  Decimal root(unsigned n) const;  // the n-th root of a non-negative value
  Decimal sin() const;
  Decimal cos() const;
  Decimal atan() const;
  static Decimal pi();
  static Decimal nan();
  friend Decimal operator+(const Decimal&, const Decimal&);
  friend Decimal operator-(const Decimal&, const Decimal&);
  friend Decimal operator*(const Decimal&, const Decimal&);
  friend Decimal operator/(const Decimal&, const Decimal&);
  friend Decimal operator%(const Decimal&, const Decimal&);
 private:
  decNumber number_;
};
inline bool operator<(const Decimal& a, const Decimal& b) { return a.compare(b) < 0; }
inline bool operator>(const Decimal& a, const Decimal& b) { return a.compare(b) > 0; }
inline bool operator==(const Decimal& a, const Decimal& b) { return a.compare(b) == 0; }
inline bool operator!=(const Decimal& a, const Decimal& b) { return !(a == b); }
inline bool operator<=(const Decimal& a, const Decimal& b) { return a.compare(b) <= 0; }
inline bool operator>=(const Decimal& a, const Decimal& b) { return a.compare(b) >= 0; }
}  // namespace Calculator
