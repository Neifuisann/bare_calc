#include "calculator_platform_fake.h"

namespace CalculatorPlatform {
uint32_t fakeMillis = 0;
bool fakeDeferWorker = false;
bool fakeFailWorker = false;
JobWatchCalls fakeJobWatch;
namespace {
void (*pendingEntry)(void*) = nullptr;
void* pendingArg = nullptr;
std::string path(const char* space, const char* key) { return std::string(space) + "/" + key; }
}  // namespace

std::map<std::string, std::vector<uint8_t>>& fakeNvs() {
  static std::map<std::string, std::vector<uint8_t>> nvs;
  return nvs;
}
bool runPendingWorker() {
  if (!pendingEntry) return false;
  const auto entry = pendingEntry;
  pendingEntry = nullptr;
  entry(pendingArg);
  return true;
}

int64_t fakeUnixSeconds = 0;
uint32_t millis() { return fakeMillis; }
bool wallClock(int64_t& unixSeconds) {
  if (fakeUnixSeconds <= 0) return false;
  unixSeconds = fakeUnixSeconds;
  return true;
}
uint32_t entropy() { return 12345; }
bool startWorker(void (*entry)(void*), void* arg) {
  if (fakeFailWorker) return false;
  if (fakeDeferWorker) {
    pendingEntry = entry;
    pendingArg = arg;
  } else {
    entry(arg);
  }
  return true;
}
void endWorker() {}
void beginJobWatch() { ++fakeJobWatch.begin; }
void adoptJobWatch() { ++fakeJobWatch.adopt; }
void cancelJobWatch() { ++fakeJobWatch.cancel; }
void finishJobWatch() { ++fakeJobWatch.finish; }
bool workerReleased() { return true; }
void yieldWorker() {}
unsigned workerStackFree() { return 0; }
void logf(const char*, ...) {}
bool loadBlob(const char* space, const char* key, std::vector<uint8_t>& out) {
  const auto it = fakeNvs().find(path(space, key));
  if (it == fakeNvs().end()) return false;
  out = it->second;
  return true;
}
bool saveBlob(const char* space, const char* key, const std::vector<uint8_t>& data) {
  fakeNvs()[path(space, key)] = data;
  return true;
}
bool loadString(const char* space, const char* key, std::string& out) {
  std::vector<uint8_t> bytes;
  if (!loadBlob(space, key, bytes)) return false;
  out.assign(bytes.begin(), bytes.end());
  return true;
}
bool saveString(const char* space, const char* key, const std::string& value) {
  return saveBlob(space, key, std::vector<uint8_t>(value.begin(), value.end()));
}
}  // namespace CalculatorPlatform
