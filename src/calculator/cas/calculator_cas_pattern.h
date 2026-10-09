#pragma once
#include "calculator/cas/calculator_cas_canonical.h"
#include <vector>

// Pattern matching and rewrite rules. The differentiation table, the
// integration table and the simplifier's identities are data -- patterns
// with wildcards plus replacement templates -- rather than code that
// recognizes whole expressions.
//
// A pattern is an ordinary tree with Wild leaves. Matching is structural,
// with three extensions that make one rule cover a family of inputs:
//  - Add and Mul match commutatively. In a pattern sum or product, the first
//    Const wild takes every operand free of the variable (the identity when
//    there is none), and the first Any wild takes whatever operands the other
//    pattern operands left over. So a*X+b matches 3x+2, x and x+5.
//  - A pattern power u^n also matches a bare u, with n = 1.
//  - A wild bound once must match the same subtree everywhere.
namespace Calculator {
namespace Cas {

enum class Constraint : uint8_t {
  Any,      // anything
  Const,    // free of the variable
  Number,   // an exact constant
  Integer,  // an integer constant
  Linear,   // a*x+b with a != 0 and a, b free of x (the variable must occur)
};
constexpr unsigned kWilds = 8;
Expr wild(uint8_t index, Constraint constraint = Constraint::Any);

struct Bindings {
  Expr slot[kWilds];
  Expr var;  // the variable Const/Linear constraints refer to
};
bool match(const Expr& pattern, const Expr& subject, Bindings& bindings);
// The template with every bound wild replaced, rebuilt canonically.
Expr instantiate(const Expr& pattern, const Bindings& bindings);

struct Rule {
  Expr pattern;
  Expr replacement;                            // a template over the wilds, or
  Expr (*build)(const Bindings&) = nullptr;    // computed when a template cannot say it
  bool (*when)(const Bindings&) = nullptr;     // extra side condition
};
// The first rule whose pattern matches `subject` at its root.
bool applyFirst(const std::vector<Rule>& rules, const Expr& subject, const Expr& var, Expr& out);
// Bottom-up rewriting to a fixed point (bounded): children first, then the
// node, and a rewritten node is rewritten again.
Expr rewrite(const Expr& e, const std::vector<Rule>& rules, const Expr& var);

// e = a*x + b with a nonzero and a, b free of x.
bool linear(const Expr& e, const Expr& x, Expr& a, Expr& b);

}  // namespace Cas
}  // namespace Calculator
