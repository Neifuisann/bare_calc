#include "calculator/runtime/calculator_job.h"
#include "calculator_platform_fake.h"
#include <cassert>
#include <cstdio>
namespace CP = CalculatorPlatform;
bool prepared = false;
bool prepare() { return prepared; }
int main() {
  Calculator::Job job;
  job.request.expression = "2+3";
  Calculator::Job out;
  Calculator::Result result;
  Calculator::JobService jobs;
  jobs.setPreparationHook(prepare);
  assert(jobs.start(job) == Calculator::JobService::Start::Started);
  assert(CP::fakeJobWatch.begin == 1 && CP::fakeJobWatch.adopt == 0);
  for (int i = 0; i < 10; ++i) assert(!jobs.take(out, result));
  assert(CP::fakeJobWatch.begin == 1 && CP::fakeJobWatch.finish == 0);
  jobs.cancel();
  assert(CP::fakeJobWatch.cancel == 1 && CP::fakeJobWatch.finish == 0);
  jobs.discardCancelled();
  assert(!jobs.running() && CP::fakeJobWatch.finish == 1);

  prepared = true;
  CP::fakeDeferWorker = true;
  assert(jobs.start(job) == Calculator::JobService::Start::Started);
  jobs.cancel();
  jobs.discardCancelled();
  assert(jobs.running() && CP::fakeJobWatch.finish == 1);
  assert(CP::runPendingWorker());
  assert(CP::fakeJobWatch.adopt == 1 && CP::fakeJobWatch.finish == 2);
  jobs.discardCancelled();
  assert(!jobs.running());

  CP::fakeDeferWorker = false;
  CP::fakeFailWorker = true;
  assert(jobs.start(job) == Calculator::JobService::Start::NoMemory);
  assert(CP::fakeJobWatch.begin == 3 && CP::fakeJobWatch.finish == 3);
  CP::fakeFailWorker = false;
  assert(jobs.start(job) == Calculator::JobService::Start::Started);
  assert(CP::fakeJobWatch.adopt == 2 && CP::fakeJobWatch.finish == 4);
  assert(jobs.take(out, result));
  assert(CP::fakeJobWatch.finish == 4);  // result adoption never renews/releases twice
  std::puts("PASS real calculator job lifecycle watches preparation, cancellation, creation failure and completion");
}
