#pragma once
#include "calculator/cas/calculator_cas_canonical.h"

// Symbolic differentiation: the sum, product, power and chain rules applied
// recursively, with each function's derivative taken from a rule table
// (calculator_cas_diff.cpp). The result is canonical but not simplified.
namespace Calculator {
namespace Cas {

Expr derivative(const Expr& e, const Expr& x);

}  // namespace Cas
}  // namespace Calculator
