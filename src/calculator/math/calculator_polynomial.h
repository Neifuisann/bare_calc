#pragma once
#include "calculator/math/calculator_engine.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Equation and Inequality kernels (manual pp.119-130), run on the calculator
// worker. Coefficients keep their exact terms, so textbook systems and
// quadratics answer with fractions and radicals; cubic and quartic roots come
// from Durand-Kerner in 40-digit decimal, with rational roots found there
// confirmed exactly and deflated so the remainder can be exact too.
namespace Calculator {

enum class EquationKind : uint8_t { Simul, Polynomial, Solver, Inequality };
// Inequality relations, in menu order: p(x) > 0, < 0, >= 0, <= 0.
enum class Relation : uint8_t { Greater, Less, GreaterEqual, LessEqual };

struct EquationTask {
  EquationKind kind = EquationKind::Simul;
  uint8_t size = 2;  // unknowns, or degree
  Relation relation = Relation::Greater;
  // Simul: row-major, size rows of size+1 (the constant last). Polynomial and
  // Inequality: size+1 coefficients, highest power first.
  std::array<Value, 20> c{};
  std::string seed;  // Solver: the initial value's expression; request.expression is the equation
};

// Answer pages in native order in Result::solutions. Solver also assigns the
// root to its variable in Result::memory. Ans and PreAns are never touched.
Result solveEquation(const EquationTask& task, const Request& request,
                     const std::function<bool()>& cancelled = {}, const std::function<void()>& cooperate = {});

}  // namespace Calculator
