#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>

// Boot-lifetime UI scratch (3 KB grayscale canvases and frame buffers).
// Constructed on first use, by the UI thread, and never freed: allocating per
// frame would only churn. Hold the result in a function-local static. A board
// with external RAM can place these there in allocate(); ordinary data only,
// never DMA buffers or stacks.
namespace PsramScratch {

inline void* allocate(size_t bytes, const char* owner) {
  (void)owner;
  return std::malloc(bytes);
}

// Null only when the heap is exhausted, for callers that can skip their work.
template <typename T> T* tryObject(const char* owner) {
  static_assert(alignof(T) <= alignof(std::max_align_t), "over-aligned scratch");
  void* p = allocate(sizeof(T), owner);
  return p ? new (p) T() : nullptr;
}

// Required scratch: exhausting the heap is fatal, like every other required
// std::vector/new allocation. Never placement-construct on null.
template <typename T> T& object(const char* owner) {
  T* p = tryObject<T>(owner);
  if (!p) std::abort();
  return *p;
}

}  // namespace PsramScratch
