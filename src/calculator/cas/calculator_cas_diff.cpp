#include "calculator/cas/calculator_cas_diff.h"
#include "calculator/cas/calculator_cas_pattern.h"

namespace Calculator {
namespace Cas {
namespace {
// d/du f(u) for each function, as a template over the wild U. The chain rule
// multiplies by u'. Real-valued forms: d/du |u| = u/|u|, and acosh uses
// 1/sqrt(u^2-1) (its domain is u >= 1).
const Expr& outerDerivative(Fn f) {
  static const std::vector<Expr> table = buildTable([] {
    const Expr U = wild(0);
    const Expr minusHalf = fraction(-1, 2);
    const Expr oneMinusSquare = sub(integer(1), pow(U, 2));
    std::vector<Expr> t(14);
    t[static_cast<size_t>(Fn::Sin)] = fn(Fn::Cos, U);
    t[static_cast<size_t>(Fn::Cos)] = neg(fn(Fn::Sin, U));
    t[static_cast<size_t>(Fn::Tan)] = pow(fn(Fn::Cos, U), -2);
    t[static_cast<size_t>(Fn::Asin)] = pow(oneMinusSquare, minusHalf);
    t[static_cast<size_t>(Fn::Acos)] = neg(pow(oneMinusSquare, minusHalf));
    t[static_cast<size_t>(Fn::Atan)] = pow(add(integer(1), pow(U, 2)), -1);
    t[static_cast<size_t>(Fn::Sinh)] = fn(Fn::Cosh, U);
    t[static_cast<size_t>(Fn::Cosh)] = fn(Fn::Sinh, U);
    t[static_cast<size_t>(Fn::Tanh)] = pow(fn(Fn::Cosh, U), -2);
    t[static_cast<size_t>(Fn::Asinh)] = pow(add(pow(U, 2), integer(1)), minusHalf);
    t[static_cast<size_t>(Fn::Acosh)] = pow(sub(pow(U, 2), integer(1)), minusHalf);
    t[static_cast<size_t>(Fn::Atanh)] = pow(oneMinusSquare, -1);
    t[static_cast<size_t>(Fn::Ln)] = pow(U, -1);
    t[static_cast<size_t>(Fn::Abs)] = mul(U, pow(fn(Fn::Abs, U), -1));
    return t;
  });
  return table[static_cast<size_t>(f)];
}
}  // namespace

Expr derivative(const Expr& e, const Expr& x) {
  if (!context().step()) return integer(0);
  if (freeOf(e, x)) return integer(0);
  switch (e->kind) {
    case Kind::Symbol: return integer(equal(e, x) ? 1 : 0);
    case Kind::Add: {
      Args terms;
      for (const auto& t : e->args) terms.push_back(derivative(t, x));
      return add(std::move(terms));
    }
    case Kind::Mul: {
      // (f g h)' = f'gh + fg'h + fgh', skipping constant factors.
      Args terms;
      for (size_t i = 0; i < e->args.size(); ++i) {
        if (freeOf(e->args[i], x)) continue;
        Args f = e->args;
        f[i] = derivative(e->args[i], x);
        terms.push_back(mul(std::move(f)));
      }
      return add(std::move(terms));
    }
    case Kind::Pow: {
      const Expr& u = e->args[0];
      const Expr& v = e->args[1];
      if (freeOf(v, x)) return mul(Args{v, pow(u, sub(v, integer(1))), derivative(u, x)});
      if (freeOf(u, x)) return mul(Args{e, fn(Fn::Ln, u), derivative(v, x)});
      // u^v = e^(v ln u): (u^v)' = u^v (v' ln u + v u'/u)
      return mul(e, add(mul(derivative(v, x), fn(Fn::Ln, u)), mul(Args{v, derivative(u, x), pow(u, -1)})));
    }
    case Kind::Func: {
      Bindings b;
      b.slot[0] = e->args[0];
      return mul(instantiate(outerDerivative(e->fn), b), derivative(e->args[0], x));
    }
    case Kind::Integral:
      // d/dx of an antiderivative in x is the integrand.
      if (equal(e->args[1], x)) return e->args[0];
      return integral(derivative(e->args[0], x), e->args[1]);
    case Kind::Equation: return equation(derivative(e->args[0], x), derivative(e->args[1], x));
    case Kind::List: {
      Args items;
      for (const auto& t : e->args) items.push_back(derivative(t, x));
      return list(std::move(items));
    }
    default:
      context().fail(Error::Argument);
      return integer(0);
  }
}

}  // namespace Cas
}  // namespace Calculator
