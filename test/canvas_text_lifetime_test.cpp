// Exercise production marquee ownership as well as public animation behavior.
// Including the implementation lets us check retained capacity without adding
// firmware-only inspection APIs. GFX and PackBits decoding are the real code.
#include "ui/canvas_text.cpp"
#include "display/frame_codec.h"
#include <cassert>
#include <cstdio>


int main() {
  using namespace CanvasText;
  for (bool inverted : {false, true}) {
    invalidateMarquee();
    std::vector<uint8_t> source(MarqueeGeometry::kGrayscaleBytes, 0x55);
    const auto initial = packBitsRle(source.data(), source.size());
    prepareMarquee("A long variable label", nullptr, 6, 48, 49, 48, 15, 80);
    captureMarqueeFrame(source.data(), source.size(), initial, inverted);
    const auto* captured = marquee.base.data();
    acceptMarqueeFrame(initial, 0);
    assert(marqueeActive() && displayedMarquee.base.data() == captured);
    assert(marquee.base.capacity() == 0 && marquee.initial.capacity() == 0);
    std::vector<uint8_t> animated, decoded(source.size());
    assert(nextMarqueeFrame(1080, animated));
    assert(FrameCodec::unpackBitsRle(animated.data(), animated.size(), decoded.data(), decoded.size()));
    assert(displayedMarquee.base.size() == source.size());
    assert(std::equal(source.begin(), source.end(), displayedMarquee.base.begin()));
    bool changed = false;
    for (int y = 0; y < 63; ++y) for (int x = 0; x < 192; ++x) {
      const size_t p = y * 192 + x;
      const int level = (decoded[p >> 2] >> (6 - 2 * (p & 3))) & 3;
      if (y < 48 || x < 6 || x >= 54) assert(level == 1);
      else if (level != 1) changed = true;
    }
    assert(changed);
    invalidateMarquee();
    assert(!marqueeActive() && !nextMarqueeFrame(2000, animated));
    assert(marquee.base.capacity() == 0 && marquee.initial.capacity() == 0);
    assert(displayedMarquee.base.capacity() == 0 && displayedMarquee.initial.capacity() == 0);
    // A saved frame returned by another screen does not resurrect an old row.
    acceptMarqueeFrame(initial, 2100);
    assert(!marqueeActive());
  }
  puts("PASS: marquee ownership transfer, storage release, grayscale/inverted animation clipping, stale-frame rejection");
}
