#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The calculator's only device dependencies: clock, entropy, the one worker,
// the log and persistent key/value storage. A board implements them once
// (src/platform/calculator_platform_arduino.cpp is a starting point); host
// tests link test/calculator_platform_fake.cpp instead, so the app, its
// controllers, the job service and the store run on the PC.
namespace CalculatorPlatform {
uint32_t millis();
// UTC seconds since 1970, or false while the board has no wall time (no RTC,
// or the user has not set one). Only Math Box's clock face uses it.
bool wallClock(int64_t& unixSeconds);
uint32_t entropy();
// Starts the single calculator worker running entry(arg); false when it could
// not be started. The worker calls endWorker() as its last act. A board with
// no threads may run entry(arg) inline and return true: the result is then
// ready on the next service().
bool startWorker(void (*entry)(void*), void* arg);
// Watch preparation, execution and cancellation even after UI navigation
// (a watchdog hook; no-ops are valid).
void beginJobWatch();
void adoptJobWatch();
void cancelJobWatch();
void finishJobWatch();
void endWorker();
// Reap a suspended finished worker on the UI task. True once its stack has
// been freed and another worker may start.
bool workerReleased();
// Cooperative yield inside long evaluations.
void yieldWorker();
// Unused worker stack in bytes (high-water mark), for the [CALCULATE] log.
unsigned workerStackFree();
void logf(const char* format, ...);
// Persistent storage, addressed by (space, key). Blob and string keys are
// different entries.
bool loadBlob(const char* space, const char* key, std::vector<uint8_t>& out);
bool saveBlob(const char* space, const char* key, const std::vector<uint8_t>& data);
bool loadString(const char* space, const char* key, std::string& out);
bool saveString(const char* space, const char* key, const std::string& value);
}  // namespace CalculatorPlatform
