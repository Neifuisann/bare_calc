#include "calculator/runtime/calculator_clock.h"
#include "calculator/runtime/calculator_platform.h"
#include "config.h"

namespace Calculator {
namespace {
constexpr uint32_t kMinuteMs = 60000;

// Days since 1970-01-01 -> proleptic Gregorian date (Howard Hinnant's
// days_from_civil inverse): exact for every int64 day the clock can reach.
void dateFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}
}  // namespace

CivilTime civilFromUnix(int64_t unixSeconds, int32_t offsetSeconds) {
  const int64_t local = unixSeconds + offsetSeconds;
  int64_t days = local / 86400, rest = local % 86400;
  if (rest < 0) { rest += 86400; --days; }
  CivilTime t;
  dateFromDays(days, t.year, t.month, t.day);
  t.weekday = static_cast<int>(((days % 7) + 11) % 7);  // day 0 was a Thursday (4)
  t.hour = static_cast<int>(rest / 3600);
  t.minute = static_cast<int>(rest / 60 % 60);
  t.second = static_cast<int>(rest % 60);
  return t;
}

bool localNow(CivilTime& out) {
  int64_t unixSeconds = 0;
  if (!CalculatorPlatform::wallClock(unixSeconds)) return false;
  out = civilFromUnix(unixSeconds, Config::Clock::kUtcOffsetSeconds);
  return true;
}

const char* weekdayName(int weekday) {
  static const char* const names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return weekday >= 0 && weekday < 7 ? names[weekday] : "";
}
const char* monthName(int month) {
  static const char* const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return month >= 1 && month <= 12 ? names[month - 1] : "";
}

bool Countdown::start(uint32_t durationMs, uint32_t now) {
  if (durationMs == 0) return false;
  if (durationMs > kMaxDurationMs) durationMs = kMaxDurationMs;
  durationMs_ = durationMs;
  deadline_ = now + durationMs;
  armed_ = 0;
  for (size_t i = 0; i < kAlertMinutes.size(); ++i)
    if (kAlertMinutes[i] * kMinuteMs < durationMs) armed_ |= 1u << i;
  pending_ = -1;
  phase_ = Phase::Running;
  return true;
}

void Countdown::pause(uint32_t now) {
  // Input is dispatched before the shell's service pass. Account for time
  // already elapsed before freezing it, including a finish at this same key.
  service(now);
  if (phase_ != Phase::Running) return;
  pausedRemaining_ = remainingMs(now);
  phase_ = Phase::Paused;
}

void Countdown::resume(uint32_t now) {
  if (phase_ != Phase::Paused) return;
  deadline_ = now + pausedRemaining_;
  phase_ = Phase::Running;
}

void Countdown::stop() {
  phase_ = Phase::Idle;
  armed_ = 0;
  pending_ = -1;
}

uint32_t Countdown::remainingMs(uint32_t now) const {
  switch (phase_) {
    case Phase::Running: {
      // Signed difference: correct across the 49-day millis() wrap.
      const int32_t left = static_cast<int32_t>(deadline_ - now);
      return left > 0 ? static_cast<uint32_t>(left) : 0;
    }
    case Phase::Paused: return pausedRemaining_;
    case Phase::Finished: return 0;
    case Phase::Idle: break;
  }
  return durationMs_;
}

bool Countdown::service(uint32_t now) {
  if (phase_ != Phase::Running) return false;
  const uint32_t left = remainingMs(now);
  int fired = -1;
  for (size_t i = 0; i < kAlertMinutes.size(); ++i) {
    if (!(armed_ & (1u << i)) || left > kAlertMinutes[i] * kMinuteMs) continue;
    armed_ &= ~(1u << i);
    fired = static_cast<int>(kAlertMinutes[i]);  // ascending index = later milestone
  }
  if (left == 0) { phase_ = Phase::Finished; armed_ = 0; }
  if (fired < 0) return false;
  pending_ = fired;
  return true;
}

uint32_t Countdown::msUntilNextAlert(uint32_t now) const {
  if (phase_ != Phase::Running) return 0;
  const uint32_t left = remainingMs(now);
  for (size_t i = 0; i < kAlertMinutes.size(); ++i) {
    if (!(armed_ & (1u << i))) continue;
    const uint32_t at = kAlertMinutes[i] * kMinuteMs;
    return left > at ? left - at : 1;
  }
  return left ? left : 1;  // the finish itself is always armed while running
}

Countdown& countdown() {
  static Countdown instance;
  return instance;
}

uint32_t displaySeconds(uint32_t remainingMs) { return (remainingMs + 999) / 1000; }


}  // namespace Calculator
