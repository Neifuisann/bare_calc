#pragma once
#include <cstdint>

// Board-side controls for calculator_platform_arduino.cpp.
namespace CalculatorPlatformArduino {
// Sets the wall clock (UTC seconds since 1970), from an RTC at boot or from
// a time the user entered. It then counts with millis().
void setWallClock(int64_t unixSeconds);
}  // namespace CalculatorPlatformArduino
