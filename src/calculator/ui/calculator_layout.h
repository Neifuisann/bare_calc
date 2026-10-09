#pragma once
#include "calculator/ui/calculator_document.h"
#include "calculator/ui/calculator_font.h"

#include <cstdint>
#include <vector>

class GFXcanvas1;
class Adafruit_GFX;
class GrayCanvas;

namespace Calculator {
// Natural-display geometry for one EditDocument revision: every item and
// field gets a box, a position and (for fields) a style; every cursor stop
// gets exactly one slot, written by the field that owns it. Measurement runs
// bottom-up over the items in reverse order (children are always after their
// parent), placement top-down in order, so the work is linear in the
// document -- there is no recursive re-measurement.
//
// layout() is the only step that computes geometry. slot()/caret() read it
// and paint() draws from it; neither changes anything, so painting twice, or
// asking for the caret before or after painting, gives the same answer.
// Coordinates put the root field's baseline at y=0 and its left edge at x=0;
// the caller translates.
class MathLayout {
 public:
  struct Box { int16_t width = 0, ascent = 0, descent = 0; };  // rows [y-ascent, y+descent)
  struct Slot { int16_t x = 0, baseline = 0; MathFont::Style style = MathFont::Style::Normal; };
  struct Rect { int16_t x = 0, y = 0, width = 0, height = 0; };
  struct PaintStyle {
    uint16_t activeField = EditDocument::kNone;
    uint8_t activeLevel = 3, otherLevel = 2;
  };

  void layout(const EditDocument& document);
  const Box& box() const { return fieldBox_[0]; }
  const Rect& ink() const { return fieldInk_[0]; }
  Rect itemInk(uint16_t item) const;
  Rect fieldInk(uint16_t field) const;
  const Box& fieldBox(uint16_t field) const { return fieldBox_[field]; }
  // For a valid (normalized) offset: where the next insertion appears.
  Slot slot(size_t offset) const { return slots_[offset]; }
  Rect caret(size_t offset) const;
  // Nearest stop in `field` to x, for Up/Down.
  size_t nearest(uint16_t field, int16_t x) const;
  // Draws into [clip.x, clip.x+width) x [clip.y, clip.y+height) only.
  void paint(GFXcanvas1& canvas, int16_t dx, int16_t dy, const Rect& clip) const;
  void paint(GrayCanvas& canvas, int16_t dx, int16_t dy, const Rect& clip) const;
  void paint(GrayCanvas& canvas, int16_t dx, int16_t dy, const Rect& clip, PaintStyle style) const;
 private:
  uint8_t fieldLevel(uint16_t field, PaintStyle style) const;
  void paintOn(Adafruit_GFX& canvas, int16_t dx, int16_t dy, const Rect& clip, PaintStyle style) const;
  struct Place { int16_t x = 0, y = 0; };
  // Per-item decoration geometry, relative to the item's pen and baseline;
  // meaning depends on the kind (see calculator_layout.cpp).
  struct Extra { int16_t v[6] = {}; uint8_t variant = 0; };
  const EditDocument* doc_ = nullptr;
  std::vector<Box> itemBox_, fieldBox_;
  std::vector<Rect> itemInk_, fieldInk_; // local to each item's/field's pen and baseline
  std::vector<Place> itemAt_, fieldAt_, fieldOffset_;
  std::vector<MathFont::Style> fieldStyle_;
  std::vector<bool> visible_;  // false for fields hidden inside atoms (constant, convert units)
  std::vector<int16_t> pen_;   // placement cursor per field
  std::vector<Extra> extra_;
  std::vector<Slot> slots_;
  Box compose(uint16_t field);
  void measure(uint16_t item);
  void paintItem(Adafruit_GFX* canvas, uint16_t i, int16_t dx, int16_t dy, const Rect& clip, Rect* ink = nullptr, uint8_t level = 1) const;
};
// Field labels ("A=", "f(x)=") in the expression face: the same glyph mapping
// the layout uses (x/y/z italic, '?' for anything unbaked) so a Store/Define
// label never renders in the 6x8 built-in font next to math. Advances from x
// on `baseline`; paintLabel returns the pen for the caller to place the field
// after it. `ink` 0 draws paper on an inverted selection.
int16_t labelAdvance(const char* text, MathFont::Style style);
int16_t paintLabel(GFXcanvas1& canvas, const char* text, int16_t x, int16_t baseline, MathFont::Style style,
                   uint16_t ink = 1);
}  // namespace Calculator
