#pragma once

#include <Adafruit_GFX.h>
#include <WString.h>

#include <cstdint>
#include <vector>

// Shared drawing grammar for menus, dialogs and lists, modeled on the
// calculator's own system UI (grid: docs/DISPLAY.md "Screen layout").
//
// Two type scales:
//   * Chrome -- ClassWiz15 (ui/font_classwiz15.h) in a 16px pitch: list rows.
//   * Body   -- the built-in 6x8 font: model output, readouts, and the
//     header()/hintBar() bands (dense text must not run at 12px throughout).
//
// Draws into a caller-supplied GFXcanvas1 (packed by CanvasText::packCanvas);
// never allocates a canvas or touches CasioLcd/PanelLink.
namespace UiKit {

// The chrome face, for gray canvases that need it without copying its tables.
const GFXfont* chromeFont();

constexpr int16_t kWidth = 192;
constexpr int16_t kHeight = 63;
constexpr int16_t kMarginLeft = 2;
constexpr int16_t kMarginRight = 4;

// Chrome row metrics. The hand-drawn A-Z/a-z/0-9 (size-14 entries of
// assets/glyph_overrides.json) stand 12px tall, 11px above the
// baseline row, so ascent 11 leaves 1px free above the ink and 2px below it
// on the calculator's 15-row bar (3px on a 16px list row). Glyphs still at
// the face's native 13px ascent ('(', '/', '?', ...) reach 1px past the row top.
constexpr int16_t kRowPitch = 16;
constexpr int16_t kRowTopPad = 1;
constexpr int16_t kChromeAscent = 11;

// Body-scale bands: header and hint bar both use the 8px built-in font.
constexpr int16_t kBandHeight = 8;

// Scrollbar: 2px thumb, 1px white gap on its left, 8px minimum thumb.
constexpr int16_t kScrollbarWidth = 2;
constexpr int16_t kScrollbarMinThumb = 8;

// Row content right edge; a list without a scrollbar gets the column back.
int16_t contentRight(bool scrollbarVisible);

// --- Bands -------------------------------------------------------------

// Body-scale title at y=0..7, reading as metadata about the chrome rows.
void header(GFXcanvas1 &canvas, const String &title);

// Fixed key order MODE, BACK, NEXT, OK so a hint never moves; empty = omit.
// Body scale, y=55..62, `KEY:verb` dialect (docs/DISPLAY.md).
struct Hints {
  String mode;
  String back;
  String next;
  String ok;
};
void hintBar(GFXcanvas1 &canvas, const Hints &hints);

// --- List engine ---------------------------------------------------------

enum class Mark : uint8_t { kNone, kRadioOn, kRadioOff, kCheckOn, kCheckOff };

struct Row {
  String label;
  bool submenu = false;  // draws the right-pointing chevron, opens a child page
  Mark mark = Mark::kNone;
  char shortcut = 0; // Optional compact calculator key badge, outside marquee text.
};

// The one scroll-window implementation: full-width inverted selection bar,
// right-edge scrollbar on overflow, radio/checkbox/submenu glyphs. Rows are
// chrome scale and ellipsized, never wrapped (a selection bar needs rows).
//
// `top`/`visibleRows` give the pixel band this call owns, so bands combine
// freely: bare list (0, 4), titled (9, 3). leadingInfo keeps info row zero
// visible with the first option; the caller offsets action indices past it.
// A Window lets the caller own the scroll position (calculator menus); nullptr
// keeps the selection-derived window.
struct Window {
  int first = 0;
};
// Negative barRight keeps the historical content-dependent exclusive edge.
// Otherwise barRight is inclusive. Defaults are the frozen 16px list style.
struct ListStyle {
  int16_t barRows = kRowPitch;
  int16_t barRight = -1;
  bool drawScrollbar = true;
};
// Clamp a caller-owned first row into [0, total-visible]; no-op for short
// lists. ensureVisible additionally pulls the window just far enough to show
// `selected`.
int clampFirst(int first, int total, int visibleRows);
int ensureVisible(int first, int selected, int total, int visibleRows);
void list(GFXcanvas1 &canvas, const std::vector<Row> &rows, int selected,
         int16_t top, int16_t visibleRows, bool leadingInfo = false,
         const Window *window = nullptr, const ListStyle &style = ListStyle{});

// --- Glyph primitives (chrome scale; `ink` 1 = black, 0 = on the bar) ---

// 7x7 outline; filled center dot when `filled`.
void radio(GFXcanvas1 &canvas, int16_t x, int16_t y, bool filled, uint8_t ink = 1);
// 9x9 box; a check mark inside when `checked`.
void checkbox(GFXcanvas1 &canvas, int16_t x, int16_t y, bool checked, uint8_t ink = 1);
// 8x7 right-pointing triangle, right-aligned (the `Calc Settings ►` idiom).
void submenuArrow(GFXcanvas1 &canvas, int16_t x, int16_t y, uint8_t ink = 1);
// Thumb at kWidth - kScrollbarWidth, sized visible/total, kScrollbarMinThumb floor.
void scrollbar(GFXcanvas1 &canvas, int16_t top, int16_t height, int first,
              int visible, int total);
struct ThumbRect { int16_t x = 0, y = 0, width = 0, height = 0; };
ThumbRect scrollbarThumb(int16_t top, int16_t height, int first, int visible, int total);

}  // namespace UiKit
