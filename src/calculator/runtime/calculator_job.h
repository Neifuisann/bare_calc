#pragma once
#include "calculator/math/calculator_engine.h"
#include "calculator/runtime/calculator_session.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

// One evaluation at a time. The on-demand worker stack is reclaimed by the
// UI after the worker suspends. The job is copied in and the UI does not touch it while the worker
// runs; results are taken on the UI loop only. Every start() and cancel()
// advances the generation, so a cancelled job may finish but can never
// publish: Home, AC, Return, a keypad interruption and an app switch simply
// cancel, and a late result cannot set Ans, Store or anything else.
namespace Calculator {

struct EquationTask;
struct TableTask;
struct StatisticsTask;
struct DistributionTask;
struct SpreadsheetTask;
struct Job {
  AppId app = AppId::Calculate;
  int destination = 0;   // controller-defined: the screen that adopts the result
  bool complex = false;  // evaluateComplex() instead of the real evaluator
  uint8_t base = 10;     // Base-N: the base of unprefixed literals
  // P12 R-01: execution presentation requested at submission (SHIFT+EXE).
  // Captured in pending-job metadata so adoption/history reproduce the shown
  // mode; the exact Value is unchanged, only presentation differs.
  Format execFormat = Format::Standard;
  // Payloads shared with the worker by pointer, never copied onto a stack:
  std::shared_ptr<const Aggregates> aggregates;  // Matrix/Vector: named slots
  std::shared_ptr<const EquationTask> equation;  // Equation/Inequality kernel input
  std::shared_ptr<const TableTask> table;
  std::shared_ptr<const StatisticsTask> statistics;  // P17 Statistics kernel input
  std::shared_ptr<const DistributionTask> distribution;  // P17 Distribution kernel input
  std::shared_ptr<const SpreadsheetTask> spreadsheet;
  uint32_t ownerRevision = 0;
  Request request;
};
// The one Job every controller fills, with the session's settings, memory and
// base and no payloads. Boot-lifetime PSRAM data: a Job holds a whole Memory
// and controllers run on the 8 KB loop stack.
Job& prepareJob(AppId app, int destination, const std::string& expression);

class JobService {
 public:
  enum class Start : uint8_t { Started, Busy, NoMemory };
  // Busy while any worker -- including a cancelled one -- is still finishing.
  Start start(const Job& job);
  void cancel();
  void discardCancelled();
  bool running() const { return running_; }
  void setPreparationHook(bool (*hook)()) { preparation_ = hook; }
  // The finished job and its result, only while still current. A stale
  // completion is consumed and dropped here; the worker is idle afterwards
  // either way.
  bool take(Job& job, Result& result);

 private:
  static void run(void* self);
  Job job_;
  Result output_;
  uint32_t generation_ = 0, jobGeneration_ = 0;
  bool running_ = false;
  bool waiting_ = false;
  bool (*preparation_)() = nullptr;
  std::atomic<bool> done_{false}, cancel_{false};
};

JobService& calculatorJobs();

}  // namespace Calculator
