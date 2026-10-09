#pragma once
#include <string>
class String {
  std::string value_;
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}
  String(char value) : value_(1, value) {}
  String(int value) : value_(std::to_string(value)) {}
  size_t length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  char operator[](size_t i) const { return value_[i]; }
  String substring(size_t from, size_t to) const { return value_.substr(from, to - from); }
  void remove(size_t from) { value_.erase(from); }
  String& operator+=(const String& other) { value_ += other.value_; return *this; }
  friend String operator+(String a, const String& b) { return a += b; }
  friend String operator+(const char* a, const String& b) { return String(a) += b; }
  friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
};
