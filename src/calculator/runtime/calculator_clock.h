#pragma once
#include <array>
#include <cstdint>

// Math Box's clock and countdown, without pixels or keys (calculator_mathbox.*
// draws them). Pure and Arduino-free so host tests drive both on a fake clock.
//
// Wall time comes from the board (an RTC, or a time the user set) through
// CalculatorPlatform::wallClock(), which reports false while no time is set;
// Math Box then shows "--:--".
//
// The countdown never needs wall time: it runs on CalculatorPlatform::millis()
// (monotonic, kept through light sleep), so it works with no clock set.
namespace Calculator {

// Broken-down local time (Config::Clock::kUtcOffsetSeconds applied).
struct CivilTime {
  int year = 1970, month = 1, day = 1;  // month 1..12, day 1..31
  int weekday = 4;                      // 0 = Sunday (1970-01-01 was a Thursday)
  int hour = 0, minute = 0, second = 0;
};
CivilTime civilFromUnix(int64_t unixSeconds, int32_t offsetSeconds);
// Local time now, or false while the board has no wall time set.
bool localNow(CivilTime& out);
const char* weekdayName(int weekday);  // "Sun".."Sat"
const char* monthName(int month);      // "Jan".."Dec"

// One countdown for the whole device: it keeps running whichever screen is
// up, and its alerts are raised wherever the user is (CalculatorApp shows
// them over any screen).
class Countdown {
 public:
  // Minutes-left milestones, then the finish (0). Only milestones below the
  // set duration arm: a 20-minute run alerts at 15, 5 and 0, never "30 left".
  static constexpr std::array<uint32_t, 4> kAlertMinutes{{30, 15, 5, 0}};
  static constexpr uint32_t kMaxDurationMs = (9 * 60 + 59) * 60000u;  // 9:59:00

  enum class Phase : uint8_t { Idle, Running, Paused, Finished };

  // Starts from `durationMs` (clamped to kMaxDurationMs); 0 is refused.
  bool start(uint32_t durationMs, uint32_t now);
  void pause(uint32_t now);
  void resume(uint32_t now);
  // Back to Idle; the duration stays as the next start's default.
  void stop();

  Phase phase() const { return phase_; }
  bool running() const { return phase_ == Phase::Running; }
  // Idle and Finished report 0 / the full duration as the dial shows them.
  uint32_t remainingMs(uint32_t now) const;
  uint32_t durationMs() const { return durationMs_; }

  // Advances milestones against `now`; true when a new alert became pending.
  // Several missed at once (a long switch-off) collapse into the latest one.
  bool service(uint32_t now);
  // The pending alert's minutes-left (0 = time is up), or -1 when none.
  int pendingAlert() const { return pending_; }
  void clearAlert() { pending_ = -1; }
  // Milliseconds until the next milestone, for the idle sleep's wake timer:
  // 0 when nothing is armed (idle, paused, finished), at least 1 otherwise.
  uint32_t msUntilNextAlert(uint32_t now) const;

 private:
  Phase phase_ = Phase::Idle;
  uint32_t durationMs_ = 0;
  uint32_t deadline_ = 0;        // millis() at zero, while Running
  uint32_t pausedRemaining_ = 0; // while Paused
  uint8_t armed_ = 0;            // bit i: kAlertMinutes[i] still to fire
  int pending_ = -1;
};
Countdown& countdown();

// Display helpers: whole seconds rounded UP, so a fresh 45:00 run reads
// 0:45:00 for its first second and 0:00:00 only at the finish.
uint32_t displaySeconds(uint32_t remainingMs);

}  // namespace Calculator
