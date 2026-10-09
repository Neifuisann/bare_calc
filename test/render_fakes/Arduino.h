#pragma once
// Just enough Arduino for the real Adafruit GFX, CanvasText, UiKit and the
// calculator renderer to compile on the host. Compile with -DARDUINO=100 so
// Adafruit_GFX.h takes its Arduino.h/Print.h branch.
#include "WString.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define PROGMEM
typedef bool boolean;
class __FlashStringHelper;
inline uint32_t millis() { return 0; }
inline float radians(float degrees) { return degrees * 0.017453292519943295f; }
#include "Print.h"
