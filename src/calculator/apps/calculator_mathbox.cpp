#include "calculator/apps/calculator_mathbox.h"
#include "calculator/ui/calculator_font.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/runtime/calculator_platform.h"
#include "calculator/ui/calculator_render.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/runtime/calculator_storage.h"
#include "config.h"
#include "ui/canvas_gray.h"
#include "ui/canvas_text.h"
#include "ui/ui_kit.h"
#include <cstdio>
#include <cstring>

namespace Calculator {
namespace MathBoxApp {
namespace {
using K = ExpressionInput::Key;
enum class Screen : uint8_t { Menu, Clock, Countdown };

// The set time is typed microwave-style: up to three digits read as H:MM, so
// "4","5" is 0:45 and "1","3","0" is 1:30. Minutes past 59 are kept as typed
// ("90" shows 0:90) and normalised when the run starts.
constexpr unsigned kDefaultEntry = 45;  // 0:45, one class test
constexpr unsigned kStepMinutes = 5;    // Up/Down
struct State {
  Screen screen = Screen::Menu;
  int selection = 0;
  unsigned entry = kDefaultEntry;
  bool entryFresh = true;  // the next digit replaces the entry instead of shifting in
  int64_t stamp = -1;      // what the last frame showed (service())
} state;

uint32_t now() { return CalculatorPlatform::millis(); }
unsigned entryMinutes(unsigned entry) { return entry / 100 * 60 + entry % 100; }
unsigned entryFromMinutes(unsigned minutes) { return minutes / 60 * 100 + minutes % 60; }

// Everything a frame of the current screen depends on, folded into one value:
// a change means repaint. Wall seconds tick the clock; displayed countdown
// seconds and phase tick both screens (the clock shows the run in its footer).
int64_t currentStamp() {
  if (state.screen == Screen::Menu) return 0;
  int64_t wall = -1;
  CalculatorPlatform::wallClock(wall);
  const Countdown& run = countdown();
  const int64_t left = displaySeconds(run.remainingMs(now()));
  return (wall + 1) * 131072 * 8 + left * 8 + static_cast<int>(run.phase());
}

// --- Drawing ----------------------------------------------------------------
// One 4-level frame in PSRAM (3 KB; never on the loop stack or in .bss).
GrayCanvas& canvas() {
  static GrayCanvas& instance = CalculatorStorage::makeState<GrayCanvas>();
  return instance;
}
std::vector<uint8_t> finish(GrayCanvas& c) {
  // No marquee row on these screens: a stale capture must not scroll over them.
  CanvasText::invalidateMarquee();
  const auto& raw = c.packed();
  return CanvasText::packBitsRle(raw.data(), raw.size());
}

// Chrome face (the menus' ClassWiz15): `baseline` is the pen row.
int16_t chromeWidth(GrayCanvas& c, const char* text) {
  c.setFont(UiKit::chromeFont());
  int16_t x1, y1;
  uint16_t w, h;
  c.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return static_cast<int16_t>(x1 + w);
}
void chrome(GrayCanvas& c, const char* text, int16_t x, int16_t baseline, uint8_t level) {
  c.setFont(UiKit::chromeFont());
  c.setTextWrap(false);
  c.setTextColor(level);
  c.setCursor(x, baseline);
  c.print(text);
}
// Body face (built-in 6x8, the notice band's): `top` is the cell's first row.
constexpr int16_t kSmallAdvance = 6;
int16_t smallWidth(const char* text) { return static_cast<int16_t>(std::strlen(text) * kSmallAdvance - 1); }
void small(GrayCanvas& c, const char* text, int16_t x, int16_t top, uint8_t level) {
  c.setFont(nullptr);
  c.setTextWrap(false);
  c.setTextSize(1);
  c.setTextColor(level);
  c.setCursor(x, top);
  c.print(text);
}

// Art as rows of characters: '#' black, '+' dark, '-' light, else paper.
void art(GrayCanvas& c, const char* const* rows, size_t count, int16_t x, int16_t y) {
  for (size_t r = 0; r < count; ++r)
    for (int16_t i = 0; rows[r][i]; ++i) {
      const char p = rows[r][i];
      if (p == '#') c.set(x + i, y + r, 3);
      else if (p == '+') c.set(x + i, y + r, 2);
      else if (p == '-') c.set(x + i, y + r, 1);
    }
}
const char* const kBell[] = {
    "--.....###.....--",
    ".-....#####....-.",
    ".....##---##.....",
    "-...##-#####+#..-",
    ".-..#-######+#.-.",
    "....#-######+#...",
    "...##-######+##..",
    "...#-#######++#..",
    "...#-########+#..",
    "..##-########+##.",
    ".##-##########+##",
    "#################",
    "#################",
    ".................",
    "......#####......",
    ".......###.......",
};
constexpr int16_t kBellW = 17, kBellH = 16;
const char* const kHourglass[] = {
    "#######",
    ".#---#.",
    "..#-#..",
    "...#...",
    "..#-#..",
    ".#-+-#.",
    "#######",
};
constexpr int16_t kHourglassW = 7, kHourglassH = 7;

// Status pill: white body text in a black lozenge, right-aligned at `right`.
void pill(GrayCanvas& c, const char* text, int16_t right, int16_t top) {
  const int16_t w = static_cast<int16_t>(smallWidth(text) + 6), x = static_cast<int16_t>(right - w);
  c.fillRect(x, top, w, 11, 3);
  for (int16_t dx : {int16_t(0), int16_t(w - 1)})
    for (int16_t dy : {int16_t(0), int16_t(10)}) c.set(x + dx, top + dy, 0);
  small(c, text, static_cast<int16_t>(x + 3), static_cast<int16_t>(top + 2), 0);
}

// Big digits: the calculator's own math face doubled, with Scale2x (EPX) edge
// directed smoothing. A pixel EPX adds is drawn one grey lighter and one it
// cuts from a corner stays light grey, so diagonals read smooth on the 4-grey
// glass instead of stair-stepped. Monospaced cells so a ticking clock never
// shifts sideways.
constexpr int16_t kBigCell = 2 * 11;  // MathMetrics::kNormalDigitAdvance, doubled
constexpr int16_t kBigColon = 10;
constexpr int16_t kBigHeight = 24;    // normal ink rows -12..-1, doubled
void bigGlyph(GrayCanvas& c, uint32_t codepoint, int16_t cellX, int16_t baseline, uint8_t level) {
  const MathFont::Glyph* g = MathFont::find(codepoint, MathFont::Style::Normal);
  if (!g) return;
  const int16_t pen = static_cast<int16_t>(cellX + (kBigCell - 2 * g->advance) / 2);
  const int ox = pen + 2 * g->xOffset, oy = baseline + 2 * g->yOffset;
  auto ink = [&](int x, int y) { return MathFont::pixel(*g, x, y); };
  const uint8_t soft = level > 1 ? level - 1 : level;
  for (int y = 0; y < g->height; ++y)
    for (int x = 0; x < g->width; ++x) {
      const bool p = ink(x, y), a = ink(x, y - 1), b = ink(x + 1, y), l = ink(x - 1, y), d = ink(x, y + 1);
      const bool e[4] = {
          (l == a && l != d && a != b) ? a : p, (a == b && a != l && b != d) ? b : p,
          (d == l && d != b && l != a) ? l : p, (b == d && b != a && d != l) ? d : p};
      for (int i = 0; i < 4; ++i) {
        const int16_t px = static_cast<int16_t>(ox + 2 * x + (i & 1)), py = static_cast<int16_t>(oy + 2 * y + (i >> 1));
        if (e[i]) c.set(px, py, p ? level : soft);
        else if (p) c.set(px, py, 1);
      }
    }
}
void bigColon(GrayCanvas& c, int16_t cellX, int16_t baseline, uint8_t level) {
  const int16_t x = static_cast<int16_t>(cellX + (kBigColon - 4) / 2);
  for (int16_t top : {int16_t(baseline - 18), int16_t(baseline - 8)}) {
    c.fillRect(x, top, 4, 4, level);
    // Softened corners, the same treatment the digits get.
    c.set(x, top, 1); c.set(x + 3, top, 1); c.set(x, top + 3, 1); c.set(x + 3, top + 3, 1);
  }
}
// "H:MM:SS" style text of digits and colons; returns the width it took.
int16_t bigWidth(const char* text) {
  int16_t w = 0;
  for (const char* p = text; *p; ++p) w = static_cast<int16_t>(w + (*p == ':' ? kBigColon : kBigCell));
  return w;
}
void bigText(GrayCanvas& c, const char* text, int16_t x, int16_t baseline, uint8_t level) {
  for (const char* p = text; *p; ++p) {
    if (*p == ':') { bigColon(c, x, baseline, level); x = static_cast<int16_t>(x + kBigColon); continue; }
    if (*p == '-') c.fillRect(static_cast<int16_t>(x + 4), static_cast<int16_t>(baseline - 13), kBigCell - 8, 3, level);
    else bigGlyph(c, static_cast<unsigned char>(*p), x, baseline, level);
    x = static_cast<int16_t>(x + kBigCell);
  }
}
// The math face at its own size (seconds beside the big clock).
int16_t mathText(GrayCanvas& c, const char* text, int16_t x, int16_t baseline, uint8_t level) {
  for (const char* p = text; *p; ++p) {
    const MathFont::Glyph* g = MathFont::find(static_cast<unsigned char>(*p), MathFont::Style::Normal);
    if (!g) continue;
    for (int y = 0; y < g->height; ++y)
      for (int xx = 0; xx < g->width; ++xx)
        if (MathFont::pixel(*g, xx, y)) c.set(x + g->xOffset + xx, baseline + g->yOffset + y, level);
    x = static_cast<int16_t>(x + g->advance);
  }
  return x;
}

void formatDuration(char* out, size_t size, uint32_t seconds) {
  std::snprintf(out, size, "%u:%02u:%02u", static_cast<unsigned>(seconds / 3600),
                static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60));
}
// "Ends 17:37" when the wall clock is known; empty otherwise.
bool endsAt(char* out, size_t size, uint32_t remainingMs) {
  int64_t wall = 0;
  if (!CalculatorPlatform::wallClock(wall)) return false;
  const CivilTime end = civilFromUnix(wall + displaySeconds(remainingMs), Config::Clock::kUtcOffsetSeconds);
  std::snprintf(out, size, "Ends %02d:%02d", end.hour, end.minute);
  return true;
}

// Shared layout rows (192x63).
constexpr int16_t kTitleBaseline = 12;  // chrome title band, rows 0..13
constexpr int16_t kBigBaseline = 40;    // big digits, rows 16..39
constexpr int16_t kInfoTop = 45;        // first body line
constexpr int16_t kHintTop = 55;        // bottom body line, rows 55..62

void progress(GrayCanvas& c, const Countdown& run, uint32_t remaining) {
  constexpr int16_t left = 2, width = 188, top = 45, height = 4;
  c.fillRect(left, top, width, height, 1);
  const uint32_t total = run.durationMs() ? run.durationMs() : 1;
  const int16_t fill = static_cast<int16_t>(static_cast<uint64_t>(remaining) * width / total);
  c.fillRect(left, top, fill, height, run.phase() == Countdown::Phase::Paused ? 2 : 3);
  // Notches where the milestones fall, so the next alert is visible ahead.
  for (uint32_t minutes : Countdown::kAlertMinutes) {
    const uint32_t at = minutes * 60000u;
    if (!minutes || at >= total) continue;
    const int16_t x = static_cast<int16_t>(left + static_cast<uint64_t>(at) * width / total);
    c.fillRect(x, top - 1, 1, height + 2, 0);
  }
}

void countdownFooter(GrayCanvas& c) {
  const Countdown& run = countdown();
  const uint32_t remaining = run.remainingMs(now());
  char buf[24];
  c.setFont(nullptr);
  art(c, kHourglass, kHourglassH, 2, kHintTop);
  formatDuration(buf, sizeof buf, displaySeconds(remaining));
  small(c, buf, 12, kHintTop, 3);
  const char* status = run.phase() == Countdown::Phase::Paused ? "Paused"
                       : run.phase() == Countdown::Phase::Finished ? "Time's up" : "";
  if (run.phase() == Countdown::Phase::Running && endsAt(buf, sizeof buf, remaining)) status = buf;
  small(c, status, static_cast<int16_t>(190 - smallWidth(status)), kHintTop, 2);
}

std::vector<uint8_t> renderClock() {
  GrayCanvas& c = canvas();
  c.clear();
  CivilTime t;
  const bool synced = localNow(t);
  char buf[24];
  // The full date takes the title band; the zone then moves to the footer
  // (with a countdown running, its status needs that corner instead).
  const char* zone = Config::Clock::kZoneLabel;
  const int16_t zoneX = static_cast<int16_t>(190 - smallWidth(zone));
  if (synced) {
    std::snprintf(buf, sizeof buf, "%s %d %s %d", weekdayName(t.weekday), t.day, monthName(t.month), t.year);
    chrome(c, buf, 2, kTitleBaseline, 3);
  } else {
    chrome(c, "Clock", 2, kTitleBaseline, 3);
    small(c, zone, zoneX, 3, 2);
  }
  // HH:MM big, seconds beside it in the plain math face.
  const int16_t groupWidth = static_cast<int16_t>(bigWidth("00:00") + 3 + 22);
  const int16_t x = static_cast<int16_t>((192 - groupWidth) / 2);
  if (synced) {
    std::snprintf(buf, sizeof buf, "%02d:%02d", t.hour, t.minute);
    bigText(c, buf, x, kBigBaseline, 3);
    std::snprintf(buf, sizeof buf, "%02d", t.second);
    mathText(c, buf, static_cast<int16_t>(x + bigWidth("00:00") + 3), kBigBaseline, 2);
  } else {
    bigText(c, "--:--", x, kBigBaseline, 1);
    mathText(c, "--", static_cast<int16_t>(x + bigWidth("00:00") + 3), kBigBaseline, 1);
  }
  c.drawFastHLine(2, kInfoTop + 4, 188, 1);
  const Countdown& run = countdown();
  if (run.phase() != Countdown::Phase::Idle) countdownFooter(c);
  else if (!synced) small(c, "Clock not set", 2, kHintTop, 2);
  else {
    small(c, "\x1a:Countdown", 2, kHintTop, 2);
    small(c, zone, zoneX, kHintTop, 2);
  }
  return finish(c);
}

std::vector<uint8_t> renderCountdown() {
  GrayCanvas& c = canvas();
  c.clear();
  const Countdown& run = countdown();
  const Countdown::Phase phase = run.phase();
  chrome(c, "Countdown", 2, kTitleBaseline, 3);
  pill(c, phase == Countdown::Phase::Running ? "RUN" : phase == Countdown::Phase::Paused ? "PAUSED"
           : phase == Countdown::Phase::Finished ? "DONE" : "SET", 190, 1);
  char buf[24];
  if (phase == Countdown::Phase::Idle) {
    std::snprintf(buf, sizeof buf, "%u:%02u:00", state.entry / 100, state.entry % 100);
    const int16_t x = static_cast<int16_t>((192 - bigWidth(buf)) / 2);
    bigText(c, buf, x, kBigBaseline, 3);
    // The editable H:MM, underlined in grey: digits type into it.
    c.fillRect(x, kBigBaseline + 2, static_cast<int16_t>(bigWidth("0:00")), 2, 2);
    small(c, "Alerts at 30, 15, 5 min left", 2, kInfoTop + 1, 2);
    small(c, "0-9:Set \x18\x19:5min EXE:Start", 2, kHintTop, 3);
    return finish(c);
  }
  const uint32_t remaining = run.remainingMs(now());
  formatDuration(buf, sizeof buf, displaySeconds(remaining));
  bigText(c, buf, static_cast<int16_t>((192 - bigWidth(buf)) / 2), kBigBaseline,
          phase == Countdown::Phase::Paused ? 2 : 3);
  progress(c, run, remaining);
  const char* hint = phase == Countdown::Phase::Running ? "EXE:Pause AC:Reset"
                     : phase == Countdown::Phase::Paused ? "EXE:Resume AC:Reset" : "EXE:Again AC:Reset";
  small(c, hint, 2, kHintTop, 3);
  if (phase == Countdown::Phase::Running && endsAt(buf, sizeof buf, remaining))
    small(c, buf, static_cast<int16_t>(190 - smallWidth(buf)), kHintTop, 2);
  return finish(c);
}

std::vector<MenuItem> menu() { return {{0, "Clock", false}, {1, "Countdown", false}}; }

void startEntry() {
  const uint32_t minutes = entryMinutes(state.entry);
  if (!minutes) return;
  countdown().start(minutes * 60000u, now());
  CalculatorPlatform::logf("[CLOCK] Countdown started: %u min.\n", static_cast<unsigned>(minutes));
}
void resetToEntry() {
  Countdown& run = countdown();
  if (run.durationMs()) state.entry = entryFromMinutes((run.durationMs() + 59999) / 60000);
  run.stop();
  state.entryFresh = true;
}

void pressCountdown(K key) {
  Countdown& run = countdown();
  switch (run.phase()) {
    case Countdown::Phase::Idle:
      if (key >= K::Digit0 && key <= K::Digit9) {
        const unsigned digit = static_cast<unsigned>(key) - static_cast<unsigned>(K::Digit0);
        state.entry = state.entryFresh ? digit : (state.entry * 10 + digit) % 1000;
        state.entryFresh = false;
      } else if (key == K::Delete) {
        state.entry /= 10;
        state.entryFresh = false;
      } else if (key == K::Clear) {
        state.entry = 0;
        state.entryFresh = false;
      } else if (key == K::Up || key == K::Down) {
        unsigned minutes = entryMinutes(state.entry);
        if (key == K::Up) minutes = minutes + kStepMinutes > 599 ? 599 : (minutes + kStepMinutes) / kStepMinutes * kStepMinutes;
        else minutes = minutes <= kStepMinutes ? 0 : (minutes - 1) / kStepMinutes * kStepMinutes;
        state.entry = entryFromMinutes(minutes);
        state.entryFresh = true;
      } else if (key == K::Execute || key == K::Okay) {
        startEntry();
      }
      return;
    case Countdown::Phase::Running:
      if (key == K::Execute || key == K::Okay) run.pause(now());
      else if (key == K::Clear) resetToEntry();
      return;
    case Countdown::Phase::Paused:
      if (key == K::Execute || key == K::Okay) run.resume(now());
      else if (key == K::Clear) resetToEntry();
      return;
    case Countdown::Phase::Finished:
      if (key == K::Execute || key == K::Okay) run.start(run.durationMs(), now());
      else if (key == K::Clear) resetToEntry();
      return;
  }
}
}  // namespace

void enter(bool fresh) {
  if (fresh) { state.screen = Screen::Menu; state.selection = 0; }
  state.stamp = -1;
}

void press(K key) {
  if (key == K::Interrupted) return;
  state.stamp = -1;
  switch (state.screen) {
    case Screen::Menu:
      menuShortcut(key, state.selection, menu().size());
      if (key == K::Up || key == K::Down) state.selection = 1 - state.selection;
      else if (key == K::Okay || key == K::Execute) {
        state.screen = state.selection ? Screen::Countdown : Screen::Clock;
        state.entryFresh = true;
      }
      return;
    case Screen::Clock:
      if (key == K::Return || key == K::Clear) state.screen = Screen::Menu;
      else if (key == K::Right || key == K::Okay || key == K::Execute) {
        state.screen = Screen::Countdown;
        state.entryFresh = true;
      }
      return;
    case Screen::Countdown:
      if (key == K::Return) { state.screen = Screen::Menu; state.selection = 1; return; }
      if (key == K::Left) { state.screen = Screen::Clock; return; }
      pressCountdown(key);
      return;
  }
}

bool service() {
  const int64_t stamp = currentStamp();
  if (stamp == state.stamp) return false;
  state.stamp = stamp;
  return true;
}

std::vector<uint8_t> render() {
  state.stamp = currentStamp();
  switch (state.screen) {
    case Screen::Clock: return renderClock();
    case Screen::Countdown: return renderCountdown();
    case Screen::Menu: break;
  }
  return renderMenuList(menu(), state.selection, 0);
}

std::vector<uint8_t> renderAlert(int minutesLeft) {
  GrayCanvas& c = canvas();
  c.clear();
  char big[8], line[32];
  const char* caption;
  if (minutesLeft > 0) {
    std::snprintf(big, sizeof big, "%d", minutesLeft);
    caption = "min left";
  } else {
    big[0] = '\0';  // no number: the words alone, level with the bell
    caption = "Time's up!";
  }
  // Bell, the number big and its caption, centred as one group on rows 3..30.
  constexpr int16_t baseline = 30, gap = 8;
  const int16_t bigW = bigWidth(big), numberGap = bigW ? 5 : 0;
  const int16_t captionWidth = chromeWidth(c, caption);
  const int16_t groupWidth = static_cast<int16_t>(kBellW + gap + bigW + numberGap + captionWidth);
  int16_t x = static_cast<int16_t>((192 - groupWidth) / 2);
  const int16_t bellTop = static_cast<int16_t>(baseline - kBigHeight + 4);
  art(c, kBell, kBellH, x, bellTop);
  x = static_cast<int16_t>(x + kBellW + gap);
  bigText(c, big, x, baseline, 3);
  x = static_cast<int16_t>(x + bigW + numberGap);
  // Chrome caps stand 11 rows: beside a number they share its baseline, alone
  // they centre on the bell.
  chrome(c, caption, x, bigW ? baseline : static_cast<int16_t>(bellTop + (kBellH + 11) / 2), 3);
  // What ran, in grey: the whole duration, and when it ends.
  const Countdown& run = countdown();
  char duration[16];
  formatDuration(duration, sizeof duration, run.durationMs() / 1000);
  char ends[16];
  if (minutesLeft > 0 && endsAt(ends, sizeof ends, run.remainingMs(now())))
    std::snprintf(line, sizeof line, "Countdown %s  %s", duration, ends);
  else
    std::snprintf(line, sizeof line, "Countdown %s", duration);
  small(c, line, static_cast<int16_t>((192 - smallWidth(line)) / 2), 36, 2);
  // The error dialog's black band, carrying the only instruction.
  c.fillRect(0, 48, 192, 15, 3);
  constexpr const char* kDismiss = "Press any key";
  chrome(c, kDismiss, static_cast<int16_t>((192 - chromeWidth(c, kDismiss)) / 2), 60, 0);
  return finish(c);
}

#ifdef CALCULATOR_REFERENCE_TEST
std::string screenName() {
  return state.screen == Screen::Menu ? "MathBoxMenu" : state.screen == Screen::Clock ? "MathBoxClock" : "MathBoxCountdown";
}
#endif

}  // namespace MathBoxApp
}  // namespace Calculator
