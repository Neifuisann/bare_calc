// CalculatorPlatform for a generic single-threaded Arduino-core board.
// A starting point: compile-checked on the host, not run on hardware.
//
//   * Worker: runs each evaluation inline inside startWorker(), so the UI
//     loop blocks until the result is ready (JobService adopts it on the next
//     service()). A board with an RTOS can start a real task here instead;
//     calculator_job.h describes the contract.
//   * Storage: kept in RAM, so the session is lost at power-off. Replace the
//     four load/save functions with EEPROM, flash or an SD card to persist it.
//   * Wall clock: unset until the board calls
//     CalculatorPlatformArduino::setWallClock().
#include "platform/calculator_platform_arduino.h"
#include "calculator/runtime/calculator_platform.h"
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

namespace {
bool clockSet = false;
int64_t clockBase = 0;      // UTC seconds at clockMillis
uint32_t clockMillis = 0;
std::map<std::string, std::vector<uint8_t>>& store() {
  static std::map<std::string, std::vector<uint8_t>> entries;
  return entries;
}
std::string path(const char* space, const char* key, char kind) {
  return std::string(1, kind) + space + "/" + key;
}
}  // namespace

namespace CalculatorPlatformArduino {
void setWallClock(int64_t unixSeconds) {
  clockBase = unixSeconds;
  clockMillis = ::millis();
  clockSet = true;
}
}  // namespace CalculatorPlatformArduino

namespace CalculatorPlatform {
uint32_t millis() { return ::millis(); }
bool wallClock(int64_t& unixSeconds) {
  if (!clockSet) return false;
  unixSeconds = clockBase + static_cast<int64_t>((::millis() - clockMillis) / 1000u);
  return true;
}
uint32_t entropy() {
  return static_cast<uint32_t>(random(0x7fffffffL)) ^ static_cast<uint32_t>(micros());
}
bool startWorker(void (*entry)(void*), void* arg) {
  entry(arg);
  return true;
}
void endWorker() {}
bool workerReleased() { return true; }
void beginJobWatch() {}
void adoptJobWatch() {}
void cancelJobWatch() {}
void finishJobWatch() {}
void yieldWorker() { yield(); }
unsigned workerStackFree() { return 0; }
void logf(const char* format, ...) {
  char line[192];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  Serial.print(line);
}
bool loadBlob(const char* space, const char* key, std::vector<uint8_t>& out) {
  const auto it = store().find(path(space, key, 'b'));
  if (it == store().end() || it->second.empty()) return false;
  out = it->second;
  return true;
}
bool saveBlob(const char* space, const char* key, const std::vector<uint8_t>& data) {
  store()[path(space, key, 'b')] = data;
  return true;
}
bool loadString(const char* space, const char* key, std::string& out) {
  const auto it = store().find(path(space, key, 's'));
  if (it == store().end()) return false;
  out.assign(it->second.begin(), it->second.end());
  return true;
}
bool saveString(const char* space, const char* key, const std::string& value) {
  store()[path(space, key, 's')] = std::vector<uint8_t>(value.begin(), value.end());
  return true;
}
}  // namespace CalculatorPlatform
