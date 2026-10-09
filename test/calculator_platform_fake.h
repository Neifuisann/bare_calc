#pragma once
#include "calculator/runtime/calculator_platform.h"
#include <map>
#include <string>
#include <vector>

// Host stand-in for src/calculator_platform.cpp: a settable clock, an
// in-memory NVS, and a worker that runs inline or waits for the test.
namespace CalculatorPlatform {
extern uint32_t fakeMillis;
// Wall clock for Math Box; 0 = never synced (wallClock() reports false).
extern int64_t fakeUnixSeconds;
// When set, startWorker() parks the job until runPendingWorker(), so a test
// can press keys while it is running.
extern bool fakeDeferWorker;
extern bool fakeFailWorker;
struct JobWatchCalls { unsigned begin = 0, adopt = 0, cancel = 0, finish = 0; };
extern JobWatchCalls fakeJobWatch;
bool runPendingWorker();
// "space/key" -> bytes; strings are stored as their characters.
std::map<std::string, std::vector<uint8_t>>& fakeNvs();
}  // namespace CalculatorPlatform
