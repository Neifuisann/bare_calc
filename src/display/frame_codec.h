#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// The frames CalculatorApp::service() returns, and how a display driver
// expands them. Every frame is PackBits-compressed (CanvasText::packBitsRle)
// and expands to one of two 192x63 layouts:
//
//   * Monochrome: 1 bit per pixel, one continuous row-major bitstream with no
//     per-row padding -- bit (y * 192 + x), most significant bit first,
//     1 = ink. 192 * 63 is a whole number of bytes, so there is no ragged tail.
//   * Grayscale: row-major 2-bpp pairs, high pair first, levels 0..3
//     (0 = white, 3 = black).
//
// bitmapRawSize() tells them apart by validating the stream, never by
// guessing from its compressed length.
namespace FrameCodec {

constexpr int kWidth = 192;
constexpr int kHeight = 63;
constexpr size_t kPackedBitmapBytes = 192 * 63 / 8;
constexpr size_t kGrayscaleBitmapBytes = 192 * 63 / 4;

// Every token must be complete and the stream must expand to exactly the
// advertised bitmap size. Allocation-free.
inline bool validatePackBitsRle(const uint8_t *rle, size_t rleLength,
                                size_t expectedBytes) {
  if (!rle || rleLength == 0 || expectedBytes == 0) return false;

  size_t input = 0;
  size_t output = 0;
  while (input < rleLength) {
    const uint8_t control = rle[input++];
    const size_t run = control < 128 ? static_cast<size_t>(control) + 1
                                     : static_cast<size_t>(control) - 126;
    const size_t payloadBytes = control < 128 ? run : 1;
    if (payloadBytes > rleLength - input || run > expectedBytes - output) {
      return false;
    }
    input += payloadBytes;
    output += run;
  }
  return output == expectedBytes;
}

// Expands the PackBits variant: 0-127 -> a literal run of (c+1) bytes
// follows; 128-255 -> repeat the next single byte (c-126) times. Returns false
// unless the stream expands to exactly `outSize` bytes.
inline bool unpackBitsRle(const uint8_t *rle, size_t rleSize, uint8_t *out,
                          size_t outSize) {
  size_t in = 0;
  size_t written = 0;
  while (in < rleSize) {
    const uint8_t control = rle[in++];
    if (control < 128) {
      const size_t run = static_cast<size_t>(control) + 1;
      if (in + run > rleSize || written + run > outSize) return false;
      std::memcpy(out + written, rle + in, run);
      in += run;
      written += run;
    } else {
      const size_t run = static_cast<size_t>(control) - 126;
      if (in >= rleSize || written + run > outSize) return false;
      std::memset(out + written, rle[in++], run);
      written += run;
    }
  }
  return written == outSize;
}

// kPackedBitmapBytes, kGrayscaleBitmapBytes, or 0 for an invalid stream.
inline size_t bitmapRawSize(const uint8_t *rle, size_t length) {
  if (validatePackBitsRle(rle, length, kPackedBitmapBytes)) return kPackedBitmapBytes;
  if (validatePackBitsRle(rle, length, kGrayscaleBitmapBytes)) return kGrayscaleBitmapBytes;
  return 0;
}

}  // namespace FrameCodec
