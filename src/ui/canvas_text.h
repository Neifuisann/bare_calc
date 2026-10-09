#pragma once

#include <Adafruit_GFX.h>
#include <WString.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Measurement, marquee and packing helpers shared by LogRender (6x8 body) and
// UiKit (ClassWiz chrome). Measurements use the canvas's current font, so
// setFont()/setTextSize() first.
namespace CanvasText {

// Selected-row marquee. Bind the packed frame when queuing a new screen;
// animation frames go directly to the existing display queue.
void prepareMarquee(const String &text, const GFXfont *font, int16_t x,
                    int16_t top, int16_t baseline, int16_t width,
                    int16_t height, int16_t overflow);
void captureMarqueeFrame(const std::vector<uint8_t> &raw,
                         const std::vector<uint8_t> &rle);
void captureMarqueeFrame(const uint8_t* raw, size_t size,
                         const std::vector<uint8_t>& rle, bool inverted);
void acceptMarqueeFrame(const std::vector<uint8_t> &rle, uint32_t now);
// Drops any marquee without touching the queued frame. Call on every ownership
// or screen transition (overlay, variable editor, hand-back/takeover) before
// queuing the next screen, so a stale row cannot animate over it. A restored
// saved frame stays static; fresh renders rebind via requestRender().
void invalidateMarquee();
bool nextMarqueeFrame(uint32_t now, std::vector<uint8_t> &rle);
// True while a selected row is animating, so a nap must stay short.
bool marqueeActive();

// Shrinks `text` with "..." until it fits maxWidth, by exact font metrics.
String fitToWidth(GFXcanvas1 &canvas, const String &text, int16_t maxWidth);

// Greedy word-wrap `text` to fit maxWidth using the canvas's font metrics.
std::vector<String> wrapText(GFXcanvas1 &canvas, const String &text, int16_t maxWidth);

// GFXcanvas1 pads rows to a byte; frames are one continuous bitstream
// (ApiClient::kPackedBitmapBytes, tools/png_to_bitmap.py). `out` must hold
// ApiClient::kPackedBitmapBytes bytes.
void repackContinuous(const uint8_t *rowPadded, size_t rowBytes, int16_t width,
                      int16_t height, uint8_t *out, size_t outBytes);

// PackBits as in tools/png_to_bitmap.py and api_client.h: control 0-127 = a
// literal run of control+1 bytes; 128-255 = the next byte repeated control-126
// times.
std::vector<uint8_t> packBitsRle(const uint8_t *raw, size_t length);

// repackContinuous + packBitsRle in one step for a filled 192x63 canvas.
std::vector<uint8_t> packCanvas(GFXcanvas1 &canvas, int16_t width, int16_t height,
                                size_t packedBytes);

}  // namespace CanvasText
