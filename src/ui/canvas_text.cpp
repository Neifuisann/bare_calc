#include "ui/canvas_text.h"
#include "ui/marquee_geometry.h"
#include "system/psram_scratch.h"
#include "system/psram_allocator.h"

#include <array>
#include <cstring>
#include <algorithm>
#include <utility>

namespace CanvasText {
namespace {
struct Marquee {
  String text;
  const GFXfont *font = nullptr;
  int16_t x=0, top=0, baseline=0, width=0, height=0, overflow=0;
  PsramMemory::Bytes base, initial;
  uint32_t started=0, lastFrame=0;
  int32_t lastOffset=-1;
  bool prepared=false, active=false;
  bool inverted=false;
} marquee, displayedMarquee;

bool sameFrame(const std::vector<uint8_t>& shown, const PsramMemory::Bytes& saved) {
  return shown.size() == saved.size() && std::equal(shown.begin(), shown.end(), saved.begin());
}
}
void prepareMarquee(const String &text, const GFXfont *font, int16_t x,
                    int16_t top, int16_t baseline, int16_t width,
                    int16_t height, int16_t overflow) {
  marquee.text=text; marquee.font=font; marquee.x=x; marquee.top=top;
  marquee.baseline=baseline; marquee.width=width; marquee.height=height;
  marquee.overflow=overflow;
  marquee.prepared=width>0 && height>0 && overflow>0;
}
void captureMarqueeFrame(const std::vector<uint8_t> &raw,
                         const std::vector<uint8_t> &rle) {
  captureMarqueeFrame(raw.data(), raw.size(), rle, false);
}
void captureMarqueeFrame(const uint8_t* raw, size_t size,
                         const std::vector<uint8_t>& rle, bool inverted) {
  if (marquee.prepared) {
    marquee.base.assign(raw, raw + size);
    marquee.initial.assign(rle.begin(), rle.end());
    marquee.inverted = inverted;
  } else {
    marquee.initial.clear();
  }
  marquee.prepared = false;
}
void acceptMarqueeFrame(const std::vector<uint8_t> &rle, uint32_t now) {
  if (!marquee.initial.empty() && sameFrame(rle, marquee.initial)) {
    // The accepted frame has one owner. Copying kept a second 3 KB base and
    // RLE allocation in the pending slot throughout the displayed animation.
    displayedMarquee = std::move(marquee);
    displayedMarquee.active = true;
  } else if (!sameFrame(rle, displayedMarquee.initial)) {
    displayedMarquee.active = false;
  }
  displayedMarquee.started = displayedMarquee.lastFrame = now;
  displayedMarquee.lastOffset = -1;
  marquee.initial.clear();
}
void invalidateMarquee() {
  // clear() only changed vector sizes: their frame capacity survived screen
  // transitions. Empty move assignment releases storage.
  marquee = Marquee{};
  displayedMarquee = Marquee{};
}
bool marqueeActive() { return displayedMarquee.active; }

bool nextMarqueeFrame(uint32_t now, std::vector<uint8_t> &rle) {
  if (!displayedMarquee.active || now-displayedMarquee.lastFrame<80) return false;
  // Defensive viewport validation: a corrupt or stale marquee must stop
  // animating, never allocate an unbounded strip or index outside the frame.
  if (displayedMarquee.width <= 0 || displayedMarquee.height <= 0 ||
      displayedMarquee.overflow <= 0 ||
      displayedMarquee.width > MarqueeGeometry::kPanelWidth ||
      displayedMarquee.height > MarqueeGeometry::kPanelHeight) {
    displayedMarquee.active = false;
    return false;
  }
  const bool grayscale =
      displayedMarquee.base.size() == MarqueeGeometry::kGrayscaleBytes;
  const bool mono = displayedMarquee.base.size() == MarqueeGeometry::kMonoBytes;
  if (!grayscale && !mono) {
    displayedMarquee.active = false;
    return false;
  }
  const uint32_t travel=static_cast<uint32_t>(displayedMarquee.overflow)*40;
  const uint32_t phase=(now-displayedMarquee.started) % (1000+travel+1200);
  const int32_t offset=phase<1000 ? -1 :
      static_cast<int32_t>(std::min<uint32_t>((phase-1000)/40, displayedMarquee.overflow));
  if (offset==displayedMarquee.lastOffset) return false;
  displayedMarquee.lastFrame=now;
  if (offset<0) {
    displayedMarquee.lastOffset=offset;
    rle.assign(displayedMarquee.initial.begin(), displayedMarquee.initial.end());
    return true;
  }
  // A separate viewport clips text without touching arrows or adjacent rows.
  GFXcanvas1 strip(displayedMarquee.width, displayedMarquee.height);
  if (!strip.getBuffer()) return false;
  const bool isGrayscale = grayscale;
  strip.fillScreen(grayscale ? 0 : 1); strip.setFont(displayedMarquee.font);
  strip.setTextWrap(false); strip.setTextColor(grayscale ? 1 : 0);
  strip.setCursor(-offset, displayedMarquee.baseline-displayedMarquee.top);
  strip.print(displayedMarquee.text);
  if (isGrayscale) {
    // Ordinary UI scratch, used with the cache enabled. Avoid a 3 KB internal
    // allocation on every animation frame; retain the immutable source frame.
    using RawFrame = std::array<uint8_t, MarqueeGeometry::kGrayscaleBytes>;
    static RawFrame& raw = PsramScratch::object<RawFrame>("marquee_frame");
    std::memcpy(raw.data(), displayedMarquee.base.data(), raw.size());
    for (int y = 0; y < displayedMarquee.height; ++y) {
      for (int x = 0; x < displayedMarquee.width; ++x) {
        // Clip the destination against the 192x63 panel before indexing the
        // 3024-byte grayscale frame. The fourth variable row's viewport used
        // to reach y=63, writing bytes 3025..3047 past the allocation and
        // corrupting the heap tail (BAAD5678 -> 00005078).
        const int32_t destX = static_cast<int32_t>(displayedMarquee.x) + x;
        const int32_t destY = static_cast<int32_t>(displayedMarquee.top) + y;
        if (!MarqueeGeometry::destPixelInBounds(destX, destY)) continue;
        const size_t pixel = static_cast<size_t>(destY) * 192 + static_cast<size_t>(destX);
        const size_t byteIndex = pixel >> 2;
        if (byteIndex >= raw.size()) continue;
        const uint8_t shift = 6 - 2 * (pixel & 3);
        raw[byteIndex] = (raw[byteIndex] & ~(3 << shift)) |
            (((strip.getPixel(x, y) != displayedMarquee.inverted) ? 3 : 0) << shift);
      }
    }
    rle = packBitsRle(raw.data(), raw.size());
  } else {
    // Mono needs a writable frame; grayscale already has its packed copy.
    GFXcanvas1 frame(192,63);
    if (!frame.getBuffer()) return false;
    if (displayedMarquee.base.size() != MarqueeGeometry::kMonoBytes) {
      displayedMarquee.active = false;
      return false;
    }
    std::memcpy(frame.getBuffer(), displayedMarquee.base.data(), displayedMarquee.base.size());
    // drawBitmap clips the destination through GFXcanvas1::drawPixel, so an
    // oversized viewport cannot escape the 1512-byte mono frame here.
    frame.drawBitmap(displayedMarquee.x, displayedMarquee.top, strip.getBuffer(),
                     displayedMarquee.width, displayedMarquee.height, 1, 0);
    rle=packBitsRle(frame.getBuffer(), displayedMarquee.base.size());
  }
  displayedMarquee.lastOffset=offset;
  return true;
}


String fitToWidth(GFXcanvas1 &canvas, const String &text, int16_t maxWidth) {
  // Adafruit's String overload leaves its outputs untouched for "", so
  // initialize deterministically and return empty early (UiKit::hintBar can
  // pass an empty line when no hints are set; header titles are also generic).
  if (text.length() == 0) return text;
  int16_t bx = 0, by = 0;
  uint16_t bw = 0, bh = 0;
  canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  if (bx + bw <= maxWidth) return text;

  static const char *kEllipsis = "...";
  String line = text;
  while (line.length() > 0) {
    line.remove(line.length() - 1);
    String candidate = line + kEllipsis;
    canvas.getTextBounds(candidate, 0, 0, &bx, &by, &bw, &bh);
    if (bx + bw <= maxWidth) return candidate;
  }
  return String(kEllipsis);
}

std::vector<String> wrapText(GFXcanvas1 &canvas, const String &text, int16_t maxWidth) {
  std::vector<String> lines;
  String current;
  int i = 0;
  const int n = text.length();
  while (i < n) {
    int j = i;
    while (j < n && text[j] != ' ') ++j;
    const String word = text.substring(i, j);
    i = (j < n) ? j + 1 : j;
    if (word.length() == 0) continue;

    const String candidate = current.length() ? current + " " + word : word;
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(candidate, 0, 0, &bx, &by, &bw, &bh);
    if (bx + bw <= maxWidth) {
      current = candidate;
    } else {
      if (current.length()) lines.push_back(current);
      current = word;  // an over-long single word is ellipsized at draw time
    }
  }
  if (current.length()) lines.push_back(current);
  return lines;
}

void repackContinuous(const uint8_t *rowPadded, size_t rowBytes, int16_t width,
                      int16_t height, uint8_t *out, size_t outBytes) {
  std::memset(out, 0, outBytes);
  for (int16_t y = 0; y < height; ++y) {
    for (int16_t x = 0; x < width; ++x) {
      const size_t rowByteIndex = static_cast<size_t>(y) * rowBytes + (x / 8);
      const bool set = (rowPadded[rowByteIndex] & (0x80 >> (x & 7))) != 0;
      if (!set) continue;
      const size_t bitIndex = static_cast<size_t>(y) * width + x;
      out[bitIndex >> 3] |= 0x80 >> (bitIndex & 7);
    }
  }
}

std::vector<uint8_t> packBitsRle(const uint8_t *raw, size_t length) {
  std::vector<uint8_t> out;
  size_t index = 0;
  while (index < length) {
    size_t run = 1;
    while (index + run < length && raw[index + run] == raw[index] && run < 129) {
      ++run;
    }

    if (run >= 2) {
      out.push_back(static_cast<uint8_t>(128 + run - 2));
      out.push_back(raw[index]);
      index += run;
      continue;
    }

    const size_t start = index;
    size_t literalLength = 0;
    while (index < length && literalLength < 128 &&
           !(index + 1 < length && raw[index + 1] == raw[index])) {
      ++index;
      ++literalLength;
    }
    out.push_back(static_cast<uint8_t>(literalLength - 1));
    out.insert(out.end(), raw + start, raw + start + literalLength);
  }
  return out;
}

std::vector<uint8_t> packCanvas(GFXcanvas1 &canvas, int16_t width, int16_t height,
                                size_t packedBytes) {
  std::vector<uint8_t> packed(packedBytes);
  repackContinuous(canvas.getBuffer(), (width + 7) / 8, width, height,
                    packed.data(), packedBytes);
  auto rle = packBitsRle(packed.data(), packed.size());
  if (marquee.prepared && width == 192 && height == 63) {
    marquee.base.assign(canvas.getBuffer(), canvas.getBuffer() + packedBytes);
    marquee.initial.assign(rle.begin(), rle.end());
  } else {
    marquee.initial.clear();
  }
  marquee.prepared = false;
  return rle;
}

}  // namespace CanvasText
