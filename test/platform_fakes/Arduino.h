#pragma once
// Just enough of an Arduino core to compile-check
// src/platform/calculator_platform_arduino.cpp on the host. Never linked.
#include <cstdint>
uint32_t millis();
uint32_t micros();
long random(long howBig);
void yield();
struct HardwareSerial {
  void print(const char* text);
};
extern HardwareSerial Serial;
