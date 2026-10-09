// Math Box clock and countdown: civil time, the countdown's milestones, and
// the CalculatorApp flows (Home > Math Box, the alert modal, alerts raised
// while suspended). Writes every screen as a PGM into <dir> for
// tools/render/render_sheet.py.
//
//   mathbox.exe <frame-dir>
#include "calculator/runtime/calculator_app.h"
#include "calculator/runtime/calculator_clock.h"
#include "calculator/apps/calculator_mathbox.h"
#include "calculator_platform_fake.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Calculator;
using K = ExpressionInput::Key;

namespace {
int failures = 0, checks = 0;
void check(const std::string& name, bool ok) {
  ++checks;
  if (!ok) { std::cout << "FAIL  " << name << "\n"; ++failures; }
}

std::vector<uint8_t> unpack(const std::vector<uint8_t>& rle) {
  std::vector<uint8_t> raw;
  for (size_t i = 0; i < rle.size();) {
    const uint8_t control = rle[i++];
    if (control < 128) { raw.insert(raw.end(), rle.begin() + i, rle.begin() + i + control + 1); i += control + 1; }
    else { raw.insert(raw.end(), control - 126, rle[i]); ++i; }
  }
  return raw;
}
uint8_t level(const std::vector<uint8_t>& raw, int x, int y) {
  const size_t p = size_t(y) * 192 + x;
  return (raw[p / 4] >> (6 - 2 * (p % 4))) & 3;
}
std::string outDir;
void save(const std::string& name, const std::vector<uint8_t>& rle) {
  const auto raw = unpack(rle);
  check(name + ": 4-grey 192x63 frame", raw.size() == 3024);
  if (outDir.empty() || raw.size() != 3024) return;
  std::ofstream pgm(outDir + "/mathbox-" + name + ".pgm", std::ios::binary);
  pgm << "P5\n192 63\n255\n";
  for (int y = 0; y < 63; ++y) for (int x = 0; x < 192; ++x) pgm.put(static_cast<char>(255 - 85 * level(raw, x, y)));
}
bool inkIn(const std::vector<uint8_t>& rle, int top, int bottom) {
  const auto raw = unpack(rle);
  for (int y = top; y <= bottom; ++y) for (int x = 0; x < 192; ++x) if (level(raw, x, y)) return true;
  return false;
}

std::vector<uint8_t> frame;
bool serviceFrame() { return CalculatorApp::service(frame); }
void press(K key) { CalculatorApp::press(key); serviceFrame(); }
void advance(uint32_t ms) { CalculatorPlatform::fakeMillis += ms; }

// 2026-09-26 09:52:38 UTC, a Saturday: 16:52:38 in UTC+7.
constexpr int64_t kSample = 1790416358;

void civil() {
  const CivilTime t = civilFromUnix(kSample, 7 * 3600);
  check("civil: UTC+7 date", t.year == 2026 && t.month == 9 && t.day == 26);
  check("civil: UTC+7 time", t.hour == 16 && t.minute == 52 && t.second == 38);
  check("civil: Saturday", t.weekday == 6 && std::string(weekdayName(t.weekday)) == "Sat");
  const CivilTime late = civilFromUnix(kSample + 8 * 3600, 7 * 3600);  // 00:52 next day
  check("civil: offset rolls the date", late.day == 27 && late.hour == 0 && late.weekday == 0);
  const CivilTime epoch = civilFromUnix(0, 0);
  check("civil: epoch Thursday", epoch.year == 1970 && epoch.month == 1 && epoch.day == 1 && epoch.weekday == 4);
  const CivilTime leap = civilFromUnix(1709251199, 0);
  check("civil: leap day", leap.month == 2 && leap.day == 29 && leap.hour == 23);
  check("civil: month names", std::string(monthName(1)) == "Jan" && std::string(monthName(12)) == "Dec");
}

void countdownLogic() {
  constexpr uint32_t min = 60000;
  Countdown run;
  check("countdown: zero refused", !run.start(0, 1000) && run.phase() == Countdown::Phase::Idle);
  run.start(45 * min, 1000);
  check("countdown: running", run.running() && run.remainingMs(1000) == 45 * min);
  check("countdown: display rounds up", displaySeconds(run.remainingMs(1001)) == 45 * 60);
  check("countdown: next alert is 30 min left", run.msUntilNextAlert(1000) == 15 * min);
  check("countdown: nothing before 30", !run.service(1000 + 15 * min - 1) && run.pendingAlert() < 0);
  check("countdown: 30 min left", run.service(1000 + 15 * min) && run.pendingAlert() == 30);
  run.clearAlert();
  check("countdown: fires once", !run.service(1000 + 16 * min));
  // Paused through the 15-minute mark: nothing fires, time does not move.
  run.pause(1000 + 25 * min);
  check("countdown: paused", run.phase() == Countdown::Phase::Paused && run.remainingMs(1000 + 60 * min) == 20 * min);
  check("countdown: paused arms nothing", !run.service(1000 + 60 * min) && run.msUntilNextAlert(1000 + 60 * min) == 0);
  run.resume(2000000);
  check("countdown: resumed", run.running() && run.remainingMs(2000000) == 20 * min);
  // Switched off across both remaining milestones: one alert, the latest.
  check("countdown: missed alerts collapse", run.service(2000000 + 21 * min) && run.pendingAlert() == 0);
  check("countdown: finished", run.phase() == Countdown::Phase::Finished && run.remainingMs(0) == 0);
  run.stop();
  check("countdown: stop keeps duration", run.phase() == Countdown::Phase::Idle && run.durationMs() == 45 * min);

  Countdown shortRun;
  shortRun.start(20 * min, 0);
  check("countdown: 20 min run skips 30", shortRun.msUntilNextAlert(0) == 5 * min);
  check("countdown: 15 left", shortRun.service(5 * min) && shortRun.pendingAlert() == 15);
  check("countdown: 5 left", shortRun.service(15 * min) && shortRun.pendingAlert() == 5);
  check("countdown: finish", shortRun.service(20 * min) && shortRun.pendingAlert() == 0);
  Countdown exact;
  exact.start(30 * min, 0);
  check("countdown: a 30 min run does not alert at its start", !exact.service(0) && exact.msUntilNextAlert(0) == 15 * min);
  Countdown wrap;
  wrap.start(10 * min, 0xFFFFFFFFu - 1000);
  check("countdown: survives the millis wrap", wrap.remainingMs(5 * min) == 5 * min - 1001);
  Countdown clamp;
  clamp.start(20 * 3600 * 1000u, 0);
  check("countdown: clamped to 9:59", clamp.durationMs() == Countdown::kMaxDurationMs);
  // Key handling can run before this loop's service: pausing at a milestone
  // must publish the elapsed alert, and a finished timer cannot become paused.
  Countdown latePause;
  latePause.start(20 * min, 0);
  latePause.pause(5 * min);
  check("countdown: pause preserves elapsed milestone", latePause.pendingAlert() == 15 &&
        latePause.phase() == Countdown::Phase::Paused && latePause.remainingMs(6 * min) == 15 * min);
  latePause.clearAlert();
  latePause.resume(6 * min);
  latePause.pause(22 * min);
  check("countdown: expired pause finishes", latePause.phase() == Countdown::Phase::Finished &&
        latePause.pendingAlert() == 0 && latePause.msUntilNextAlert(22 * min) == 0);
}

void appFlows() {
  CalculatorPlatform::fakeMillis = 5000;
  CalculatorPlatform::fakeUnixSeconds = 0;
  countdown().stop();

  // Home > Math Box ('*' is its shortcut, cell 12).
  CalculatorApp::openHome();
  serviceFrame();
  press(K::Multiply);
  check("app: Math Box opened", !CalculatorApp::isHome() && !CalculatorApp::alertActive());
  save("01-menu", frame);

  press(K::Okay);  // Clock
  save("02-clock-unset", frame);
  check("app: unset clock draws", inkIn(frame, 16, 39));
  CalculatorPlatform::fakeUnixSeconds = kSample;
  check("app: the clock ticks once set", serviceFrame());
  save("03-clock", frame);
  check("app: no repaint within the second", !serviceFrame());
  ++CalculatorPlatform::fakeUnixSeconds;
  check("app: repaint on the next second", serviceFrame());

  press(K::Right);  // Countdown setup
  save("04-countdown-set", frame);
  press(K::Digit1); press(K::Digit3); press(K::Digit0);  // 1:30
  save("05-countdown-typed", frame);
  press(K::Up);  // 1:35
  press(K::Execute);
  check("app: countdown started", countdown().running() && countdown().durationMs() == 95 * 60000u);
  save("06-countdown-run", frame);
  advance(20 * 60000 + 400);
  serviceFrame();
  save("07-countdown-progress", frame);
  press(K::Execute);
  check("app: paused", countdown().phase() == Countdown::Phase::Paused);
  save("08-countdown-paused", frame);
  press(K::Execute);
  check("app: resumed", countdown().running());

  // Home, then Home again, comes back to Math Box.
  press(K::Home);
  check("app: Home menu", CalculatorApp::isHome());
  press(K::Home);
  check("app: Home-on-Home resumes Math Box", !CalculatorApp::isHome() && CalculatorApp::active());

  // Across the 30-minute mark the alert takes over, and any key only closes it.
  CalculatorPlatform::fakeUnixSeconds += 45 * 60;
  advance(45 * 60000);
  check("app: alert repaints", serviceFrame());
  check("app: alert showing", CalculatorApp::alertActive());
  save("09-alert-30", frame);
  const auto phase = countdown().phase();
  press(K::Digit7);
  check("app: key closes the alert", !CalculatorApp::alertActive() && CalculatorApp::active());
  check("app: the closing key is swallowed", countdown().phase() == phase);
  press(K::Left);
  save("10-clock-with-run", frame);

  // Power off; the next milestone wakes the device, and quick calculate on
  // power-on shows the alert over Calculate.
  CalculatorApp::suspend();
  advance(25 * 60000);
  check("suspended: alert due", CalculatorApp::alertDue());
  check("suspended: wake is the finish", CalculatorApp::countdownWakeMs() > 0 && CalculatorApp::countdownWakeMs() < 5 * 60000u);
  CalculatorApp::openQuick();
  check("wake: alert overlay", serviceFrame() && CalculatorApp::alertActive());
  save("11-alert-5", frame);
  CalculatorApp::press(K::Return);
  check("wake: dismissal stays in the calculator", !CalculatorApp::alertActive() && CalculatorApp::active());
  CalculatorApp::suspend();

  advance(5 * 60000);
  CalculatorApp::openQuick();
  serviceFrame();
  check("wake: time up", CalculatorApp::alertActive() && countdown().phase() == Countdown::Phase::Finished);
  check("wake: finish alert blocks auto-off", !CalculatorApp::countdownRunning() &&
        CalculatorApp::countdownBlocksAutoPowerOff());
  save("12-alert-done", frame);
  CalculatorApp::press(K::Execute);
  check("wake: second dismissal stays", !CalculatorApp::alertActive() && CalculatorApp::active());
  check("wake: dismissed alert releases auto-off block", !CalculatorApp::countdownBlocksAutoPowerOff());
  check("wake: nothing more armed", !CalculatorApp::alertDue() && CalculatorApp::countdownWakeMs() == 0);
  CalculatorApp::suspend();

  // Back in: the finished run, then AC returns to the set screen at 1:35.
  CalculatorApp::openHome();
  serviceFrame();
  press(K::Multiply);
  press(K::Down);
  press(K::Okay);
  save("13-countdown-done", frame);
  press(K::Clear);
  check("app: reset to idle", countdown().phase() == Countdown::Phase::Idle);
  save("14-countdown-reset", frame);
  // Settings from Math Box is the shared list, and Return lands back here.
  press(K::Settings);
  press(K::Return);
  check("app: settings return to Math Box", !CalculatorApp::isHome() && CalculatorApp::active());
  CalculatorApp::suspend();
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) outDir = argv[1];
  civil();
  countdownLogic();
  appFlows();
  std::cout << "mathbox: " << checks - failures << "/" << checks << " checks passed\n";
  return failures ? 1 : 0;
}
