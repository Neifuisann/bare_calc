#pragma once
#include "WString.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* data, size_t size) {
    size_t n = 0;
    while (size--) n += write(*data++);
    return n;
  }
  size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)) : 0; }
  size_t print(const char* s) { return write(s); }
  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int value) { return write(std::to_string(value).c_str()); }
  size_t print(unsigned value) { return write(std::to_string(value).c_str()); }
};
