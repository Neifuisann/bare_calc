#pragma once
#include "calculator/cas/calculator_cas_expr.h"
#include <string>

// Pretty-printing. Both forms are in the calculator's own expression syntax,
// so the existing layout draws them and the parser reads them back:
//  - naturalText: the template grammar the result region lays out as math
//    (frac(x,2), sqrt(x), x^(2), abs(x)) -- what the screen shows;
//  - linearText: one line with / and ^ ("x^2/2", "sqrt(x+1)") -- for tests,
//    logs and Line I/O.
// Terms read in descending powers of x, constants last; a product reads
// coefficient, variables, exponentials, functions, then parenthesized sums.
namespace Calculator {
namespace Cas {

std::string naturalText(const Expr& e);
std::string linearText(const Expr& e);

}  // namespace Cas
}  // namespace Calculator
