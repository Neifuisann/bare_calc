#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

// Ordinary retained data. Empty vectors allocate nothing during global
// construction. A board with external RAM can route Allocator there; never
// use it for DMA buffers or task stacks.
namespace PsramMemory {

template <typename T> using Allocator = std::allocator<T>;

using Bytes = std::vector<uint8_t, Allocator<uint8_t>>;

}  // namespace PsramMemory
