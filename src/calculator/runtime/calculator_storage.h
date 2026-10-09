#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>

// Boot-lifetime calculator data, allocated on first use by the UI thread.
// Never call makeState from a global initializer. A board with external RAM
// can place this data there in allocateState(); only ordinary data belongs
// here, never task stacks or DMA buffers.
namespace CalculatorStorage {
struct Usage {
  size_t psram = 0;
  size_t internal = 0;
};
inline Usage& usage() {
  static Usage value;
  return value;
}

inline void* allocateState(size_t bytes) {
  void* storage = std::malloc(bytes);
  if (storage) usage().internal += bytes;
  return storage;
}

template <typename T> T& makeState() {
  static_assert(alignof(T) <= alignof(std::max_align_t), "over-aligned calculator state");
  void* storage = allocateState(sizeof(T));
  // Like the existing required std::vector/new allocations, exhausting the
  // heap is fatal. Never placement-construct or return a reference to null.
  if (!storage) std::abort();
  return *new (storage) T();
}
}  // namespace CalculatorStorage
