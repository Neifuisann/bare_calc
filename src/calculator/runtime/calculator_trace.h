#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Pure storage/wire helpers, shared with the native replay test.
namespace CalculatorTrace {
constexpr size_t kRecords = 192, kChunkRecords = 32, kRecordBytes = 156;
struct Record {
  uint32_t index = 0, sdaUs = 0, a0Us = 0;
  int32_t sdaEdge = -1, a0Edge = -1;
  uint16_t sdaBits = 0, a0Bits = 0;
  uint32_t flags = 0;
  uint8_t sda[64]{}, a0[64]{};
};
inline size_t slot(size_t sequence) {
  return sequence < 96 ? sequence : 96 + (sequence - 96) % 96;
}
inline size_t sequence(size_t retainedIndex, size_t total) {
  return retainedIndex < 96 || total <= kRecords ? retainedIndex : total - kRecords + retainedIndex;
}
inline void encode(const Record& item, std::vector<uint8_t>& out) {
  auto u32 = [&](uint32_t value) { for (int i = 0; i < 4; ++i) out.push_back(value >> (8 * i)); };
  auto u16 = [&](uint16_t value) { out.push_back(value); out.push_back(value >> 8); };
  u32(item.index); u32(item.sdaUs); u32(item.a0Us); u32(item.sdaEdge); u32(item.a0Edge);
  u16(item.sdaBits); u16(item.a0Bits); u32(item.flags);
  out.insert(out.end(), item.sda, item.sda + 64); out.insert(out.end(), item.a0, item.a0 + 64);
}
}  // namespace CalculatorTrace
