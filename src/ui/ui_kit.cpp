#include "calculator/ui/calculator_shortcuts.h"
#include "ui/ui_kit.h"

#include <algorithm>

#include "ui/canvas_text.h"
#include "ui/font_classwiz15.h"

namespace UiKit {

const GFXfont* chromeFont() { return &ClassWiz15; }

namespace {

constexpr int16_t kGlyphGap = 4;        // space between a mark/arrow and label text
constexpr int16_t kRadioSize = 7;
constexpr int16_t kCheckboxSize = 9;
constexpr int16_t kArrowWidth = 8;
constexpr int16_t kArrowHeight = 7;

int16_t chromeBaselineY(int16_t rowTop) {
  return static_cast<int16_t>(rowTop + kRowTopPad + kChromeAscent);
}

}  // namespace

int16_t contentRight(bool scrollbarVisible) {
  return static_cast<int16_t>(kWidth - (scrollbarVisible ? (kScrollbarWidth + 1) : 0));
}

void header(GFXcanvas1 &canvas, const String &title) {
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  canvas.setTextColor(1);
  canvas.setCursor(kMarginLeft, 0);
  canvas.print(CanvasText::fitToWidth(canvas, title, kWidth - kMarginLeft - kMarginRight));
  canvas.drawFastHLine(kMarginLeft, kBandHeight, kWidth - kMarginLeft - kMarginRight, 1);
}

void hintBar(GFXcanvas1 &canvas, const Hints &hints) {
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  canvas.setTextColor(1);
  const int16_t y = kHeight - kBandHeight;

  // Fixed left-to-right order so a key's hint never moves between screens,
  // even when a screen only uses two of the four.
  String line;
  if (hints.mode.length()) line += "MODE:" + hints.mode;
  if (hints.back.length()) {
    if (line.length()) line += "  ";
    line += "BACK:" + hints.back;
  }
  if (hints.next.length()) {
    if (line.length()) line += "  ";
    line += "NEXT:" + hints.next;
  }
  if (hints.ok.length()) {
    if (line.length()) line += "  ";
    line += "OK:" + hints.ok;
  }
  canvas.setCursor(kMarginLeft, y);
  canvas.print(CanvasText::fitToWidth(canvas, line, kWidth - kMarginLeft - kMarginRight));
}

void radio(GFXcanvas1 &canvas, int16_t x, int16_t y, bool filled, uint8_t ink) {
  canvas.drawCircle(x + kRadioSize / 2, y + kRadioSize / 2, kRadioSize / 2, ink);
  if (filled) canvas.fillCircle(x + kRadioSize / 2, y + kRadioSize / 2, kRadioSize / 2 - 2, ink);
}

void checkbox(GFXcanvas1 &canvas, int16_t x, int16_t y, bool checked, uint8_t ink) {
  canvas.drawRect(x, y, kCheckboxSize, kCheckboxSize, ink);
  if (!checked) return;
  // A short two-stroke tick, inset by 2px so it reads inside the box at
  // this size rather than touching the frame.
  canvas.drawLine(x + 2, y + 4, x + 4, y + 6, ink);
  canvas.drawLine(x + 4, y + 6, x + 7, y + 2, ink);
}

void submenuArrow(GFXcanvas1 &canvas, int16_t x, int16_t y, uint8_t ink) {
  // Seven left-aligned rows with widths 2, 4, 6, 8, 6, 4, 2 pixels.
  for (int16_t row = 0; row < kArrowHeight; ++row) {
    const int16_t distance = row <= kArrowHeight / 2 ? row : kArrowHeight - 1 - row;
    const int16_t width = kArrowWidth - 2 * (kArrowHeight / 2 - distance);
    canvas.drawFastHLine(x, y + row, width, ink);
  }
}

ThumbRect scrollbarThumb(int16_t top, int16_t height, int first, int visible, int total) {
  if (total <= 0 || visible <= 0 || visible >= total || height <= 0) return {};
  const int16_t x = kWidth - kScrollbarWidth;

  const int16_t thumbHeight = std::max<int16_t>(
      kScrollbarMinThumb,
      static_cast<int16_t>((static_cast<int32_t>(height) * visible) / total));
  const int16_t travel = static_cast<int16_t>(height - thumbHeight);
  const int lastFirst = std::max(0, total - visible);
  const int16_t thumbTop = static_cast<int16_t>(
      top + (lastFirst > 0 ? (static_cast<int32_t>(travel) * first) / lastFirst : 0));
  return {x, thumbTop, kScrollbarWidth, thumbHeight};
}

void scrollbar(GFXcanvas1 &canvas, int16_t top, int16_t height, int first, int visible,
              int total) {
  const auto thumb = scrollbarThumb(top, height, first, visible, total);
  if (!thumb.width) return;
  canvas.fillRect(thumb.x, top, kScrollbarWidth, height, 0);
  canvas.drawFastVLine(thumb.x - 1, top, height, 0);
  canvas.fillRect(thumb.x, thumb.y, thumb.width, thumb.height, 1);
}

int clampFirst(int first, int total, int visibleRows) {
  if (visibleRows <= 0 || total <= visibleRows) return 0;
  if (first < 0) return 0;
  const int last = total - visibleRows;
  return first > last ? last : first;
}

int ensureVisible(int first, int selected, int total, int visibleRows) {
  first = clampFirst(first, total, visibleRows);
  if (selected < 0 || total <= visibleRows || visibleRows <= 0) return first;
  if (selected < first) return selected;
  if (selected >= first + visibleRows) return selected - visibleRows + 1;
  return first;
}

void list(GFXcanvas1 &canvas, const std::vector<Row> &rows, int selected, int16_t top,
         int16_t visibleRows, bool leadingInfo, const Window *window, const ListStyle &style) {
  const int total = static_cast<int>(rows.size());
  if (total == 0 || visibleRows <= 0) return;

  const bool scrollbarVisible = total > visibleRows;
  const int16_t rowRight = style.barRight < 0 ? contentRight(scrollbarVisible) : style.barRight + 1;

  const int safeSelected = selected < 0 ? -1 : std::min(selected, total - 1);
  int first = 0;
  if (window != nullptr) {
    // Caller-owned window: clamp it, then pull just far enough to keep the
    // selection visible. Painting, paging and the scrollbar below all read
    // this one value.
    first = ensureVisible(window->first, safeSelected, total, visibleRows);
  } else if (total > visibleRows) {
    const int anchor = safeSelected < 0 || (leadingInfo && safeSelected == 1) ? 0 : safeSelected;
    first = std::min(anchor, total - static_cast<int>(visibleRows));
  }
  const int end = std::min(total, first + static_cast<int>(visibleRows));

  canvas.setFont(&ClassWiz15);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);

  for (int i = first; i < end; ++i) {
    const int16_t rowTop = static_cast<int16_t>(top + (i - first) * kRowPitch);
    const bool active = (i == safeSelected);
    const uint8_t ink = active ? 0 : 1;

    if (active) {
      canvas.fillRect(0, rowTop, rowRight, style.barRows, 1);
    }
    canvas.setTextColor(ink);

    const Row &row = rows[i];
    int16_t textX = kMarginLeft;
    if(row.shortcut){
      for(unsigned y=0;y<7;++y)for(unsigned x=0;x<7;++x)
        if(Calculator::shortcutCircleRow(y)&(1u<<(6-x)))
          canvas.drawPixel(textX+x,rowTop+4+y,Calculator::shortcutDigitPixel(row.shortcut,x,y)?1-ink:ink);
      textX+=9;
    }
    switch (row.mark) {
      case Mark::kRadioOn:
      case Mark::kRadioOff:
        radio(canvas, textX, static_cast<int16_t>(rowTop + (kRowPitch - kRadioSize) / 2),
              row.mark == Mark::kRadioOn, ink);
        textX = static_cast<int16_t>(textX + kRadioSize + kGlyphGap);
        break;
      case Mark::kCheckOn:
      case Mark::kCheckOff:
        checkbox(canvas, textX, static_cast<int16_t>(rowTop + (kRowPitch - kCheckboxSize) / 2),
                 row.mark == Mark::kCheckOn, ink);
        textX = static_cast<int16_t>(textX + kCheckboxSize + kGlyphGap);
        break;
      case Mark::kNone:
        break;
    }

    int16_t labelRight = static_cast<int16_t>(rowRight - kMarginRight);
    if (row.submenu) labelRight = static_cast<int16_t>(labelRight - kArrowWidth - kGlyphGap);

    if (active) {
      int16_t bx, by;
      uint16_t bw, bh;
      canvas.getTextBounds(row.label, 0, 0, &bx, &by, &bw, &bh);
      CanvasText::prepareMarquee(row.label, &ClassWiz15, textX, rowTop,
          chromeBaselineY(rowTop), labelRight - textX,
          std::min<int16_t>(style.barRows, kHeight - rowTop),
          std::max<int32_t>(0, static_cast<int32_t>(bx) + bw - (labelRight - textX)));
    }
    canvas.setCursor(textX, chromeBaselineY(rowTop));
    canvas.print(CanvasText::fitToWidth(canvas, row.label,
                                        static_cast<int16_t>(labelRight - textX)));

    if (row.submenu) {
      submenuArrow(canvas, static_cast<int16_t>(rowRight - kMarginRight - kArrowWidth),
                   static_cast<int16_t>(rowTop + (kRowPitch - kArrowHeight) / 2), ink);
    }
  }

  canvas.setTextColor(1);
  canvas.setFont(nullptr);  // restore the built-in font for whatever draws next

  if (scrollbarVisible && style.drawScrollbar) {
    scrollbar(canvas, top, static_cast<int16_t>(visibleRows * kRowPitch), first, end - first,
              total);
  }
}

}  // namespace UiKit
