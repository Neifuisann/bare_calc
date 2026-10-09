#pragma once
#include "calculator/math/calculator_engine.h"
#include <array>
#include <cstdint>
#include <functional>

// Matrix and Vector values (manual pp.137-150). One fixed shape for both: a
// vector is a column, rows = dimension and cols = 1. Cells are 23-digit
// decimals, real only; rows == 0 means undefined. Stride 4 keeps (r, c) at
// cells[r * 4 + c] whatever the shape.
namespace Calculator {

struct Aggregate {
  uint8_t rows = 0, cols = 0;
  std::array<Decimal, 16> cells{};
  bool defined() const { return rows != 0; }
  const Decimal& at(int r, int c) const { return cells[static_cast<size_t>(r * 4 + c)]; }
  Decimal& at(int r, int c) { return cells[static_cast<size_t>(r * 4 + c)]; }
};

// MatA-D then MatAns; VctA-D then VctAns.
constexpr int kAnswerSlot = 4;
struct Aggregates {
  std::array<Aggregate, 5> matrices, vectors;
};
const char* aggregateName(bool vector, int slot);

// Matrix or Vector app evaluation: aggregate references and operations are
// reduced here on a fixed pool of eight temporaries (Stack ERROR past it);
// subtrees without them go to the real evaluator. An aggregate outcome lands in
// Result::aggregate and leaves Ans alone; a scalar outcome sets Ans. PreAns is
// never touched. `vectors` picks the app: VctX names, cross product for x.
Result evaluateAggregate(const Request& request, const Aggregates& store, bool vectors,
                         const std::function<bool()>& cancelled = {}, const std::function<void()>& cooperate = {});

}  // namespace Calculator
