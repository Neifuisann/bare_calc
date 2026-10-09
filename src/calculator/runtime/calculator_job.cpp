#include "calculator/runtime/calculator_job.h"
#include "calculator/runtime/calculator_storage.h"
#include "calculator/math/calculator_basen.h"
#include "calculator/cas/calculator_cas.h"
#include "calculator/math/calculator_complex.h"
#include "calculator/apps/calculator_distribution.h"
#include "calculator/math/calculator_polynomial.h"
#include "calculator/runtime/calculator_platform.h"
#include "calculator/apps/calculator_statistics.h"
#include "calculator/apps/calculator_spreadsheet.h"
#include "calculator/apps/calculator_table.h"

namespace Calculator {

JobService& calculatorJobs() {
  static auto& jobs = CalculatorStorage::makeState<JobService>();
  return jobs;
}

Job& prepareJob(AppId app, int destination, const std::string& expression) {
  static auto& job = CalculatorStorage::makeState<Job>();
  const CalculatorSession& session = calculatorSession();
  job.app = app;
  job.destination = destination;
  job.complex = app == AppId::Complex;
  job.base = session.base;
  // P12 R-01: the static job is reused across controllers (Calculate sets
  // execFormat in startJob; Equation does not), so a stale SHIFT+EXE Decimal
  // must never leak into a later job.
  job.execFormat = Format::Standard;
  job.aggregates.reset();
  job.equation.reset();
  job.table.reset();job.ownerRevision=0;
  job.statistics.reset();
  job.distribution.reset();
  job.spreadsheet.reset();
  job.request.expression = expression;
  job.request.settings = session.settings;
  job.request.memory = session.memory;
  job.request.verify = false;
  job.request.randomSeed = CalculatorPlatform::entropy();
  return job;
}

JobService::Start JobService::start(const Job& job) {
  if (running_) return Start::Busy;
  job_ = job;
  jobGeneration_ = ++generation_;
  cancel_.store(false, std::memory_order_relaxed);
  done_.store(false, std::memory_order_relaxed);
  running_ = true;
  CalculatorPlatform::beginJobWatch();
  waiting_ = preparation_ && !preparation_();
  if (waiting_) return Start::Started;
  if (!CalculatorPlatform::startWorker(&JobService::run, this)) {
    CalculatorPlatform::finishJobWatch();
    running_ = false;
    return Start::NoMemory;
  }
  return Start::Started;
}

void JobService::cancel() {
  ++generation_;
  cancel_.store(true, std::memory_order_relaxed);
  if (running_) CalculatorPlatform::cancelJobWatch();
}

void JobService::discardCancelled() {
  if (!running_ || jobGeneration_ == generation_) return;
  if (!waiting_ && !done_.load(std::memory_order_acquire)) return;
  if (waiting_) CalculatorPlatform::finishJobWatch();
  waiting_ = false;
  running_ = false;
  done_.store(false, std::memory_order_relaxed);
  job_ = Job();
  output_ = Result();
}

bool JobService::take(Job& job, Result& result) {
  if (waiting_) {
    if (jobGeneration_ != generation_) { CalculatorPlatform::finishJobWatch(); waiting_ = false; running_ = false; return false; }
    if (preparation_ && !preparation_()) return false;
    waiting_ = false;
    if (!CalculatorPlatform::startWorker(&JobService::run, this)) {
      CalculatorPlatform::finishJobWatch();
      output_ = Result();
      output_.error = Error::Stack;
      done_.store(true, std::memory_order_release);
    }
  }
  if (!running_ || !done_.load(std::memory_order_acquire)) return false;
  running_ = false;
  done_.store(false, std::memory_order_relaxed);
  if (jobGeneration_ != generation_) {job_.table.reset();output_.table.reset();job_.statistics.reset();output_.statistics.reset();job_.distribution.reset();output_.distribution.reset();job_.spreadsheet.reset();output_.spreadsheet.reset();return false;}
  job = std::move(job_);
  result = std::move(output_);
  return true;
}

void JobService::run(void* self) {
  CalculatorPlatform::adoptJobWatch();
  auto* jobs = static_cast<JobService*>(self);
  const uint32_t start = CalculatorPlatform::millis();
  const std::function<bool()> cancelled = [jobs] { return jobs->cancel_.load(std::memory_order_relaxed); };
  const std::function<void()> cooperate = [] { CalculatorPlatform::yieldWorker(); };
  const Job& job = jobs->job_;
  if (job.spreadsheet) jobs->output_ = evaluateSpreadsheet(*job.spreadsheet, job.request, cancelled, cooperate);
  else if (job.table) jobs->output_ = evaluateTable(*job.table, job.request, cancelled, cooperate);
  else if (job.statistics) jobs->output_ = evaluateStatistics(*job.statistics, job.request, cancelled, cooperate);
  else if (job.distribution) jobs->output_ = evaluateDistribution(*job.distribution, job.request, cancelled, cooperate);
  else if (job.equation) jobs->output_ = solveEquation(*job.equation, job.request, cancelled, cooperate);
  else if (job.app == AppId::BaseN) jobs->output_ = evaluateBaseN(job.request, job.base, cancelled);
  else if (job.aggregates)
    jobs->output_ = evaluateAggregate(job.request, *job.aggregates, job.app == AppId::Vector, cancelled, cooperate);
  else if (job.complex) jobs->output_ = evaluateComplex(job.request, cancelled, cooperate);
  // Symbolic commands (simplify, deriv, integrate...) are Calculate's alone.
  else if (job.app == AppId::Calculate && Cas::handles(job.request)) jobs->output_ = Cas::evaluate(job.request, cancelled, cooperate);
  else jobs->output_ = evaluate(job.request, cancelled, cooperate);
  CalculatorPlatform::logf("[CALCULATE] elapsed_ms=%lu stack_hwm=%u error=%u complex=%u app=%u\n",
                           static_cast<unsigned long>(CalculatorPlatform::millis() - start),
                           CalculatorPlatform::workerStackFree(),
                           static_cast<unsigned>(jobs->output_.error),
                           static_cast<unsigned>(job.complex), static_cast<unsigned>(job.app));
  // Every output write happens before the UI's acquire on done_.
  CalculatorPlatform::finishJobWatch();
  jobs->done_.store(true, std::memory_order_release);
  CalculatorPlatform::endWorker();
}

}  // namespace Calculator
