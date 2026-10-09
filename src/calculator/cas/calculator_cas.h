#pragma once
#include "calculator/cas/calculator_cas_expr.h"
#include "calculator/math/calculator_engine.h"
#include <functional>
#include <string>

// The CAS as Calculate sees it: symbolic commands inside an ordinary
// expression (docs/calculator/CAS.md).
//
//   simplify(f)   expand(f)   factor(f)
//   deriv(f[,v[,n]])   integrate(f[,v])   partfrac(f[,v])
//   solve(f=g[,v])   subst(f,v,value)
//
// The variable v defaults to x. Inside such an expression every variable
// letter is a symbol, not its stored value; Ans/PreAns are their exact values.
// Commands nest and mix with arithmetic (deriv(x^3)+1). diff(f,a), the d/dx
// template, is exact there too. Trigonometry follows the angle setting: in
// Deg, deriv(sin(x)) is pi*cos(x)/180.
namespace Calculator {
namespace Cas {

// Calculate's EXE routes here when the expression uses a CAS command.
bool handles(const Request& request);
// An exact constant result is an ordinary Value (Ans and PreAns advance, as
// for any answer). Anything else is Result::formula -- natural-display source
// for the result region -- with Ans untouched. Errors: Syntax for what the
// CAS cannot read (rand, a Verify relation), Argument for a malformed command,
// Math for a certain domain error, Stack for an expression past the size
// bounds, TimeOut/Cancelled as for any evaluation.
Result evaluate(const Request& request, const std::function<bool()>& cancelled = {},
                const std::function<void()>& cooperate = {});

// The tree `source` evaluates to, for tests and tools. Runs under its own
// context; the tree stays valid after it returns.
struct Outcome {
  Expr value;
  Error error = Error::None;
  size_t position = 0;
};
Outcome run(const std::string& source, const Settings& settings, const Memory& memory = {},
            const std::function<bool()>& cancelled = {}, const std::function<void()>& cooperate = {});

}  // namespace Cas
}  // namespace Calculator
