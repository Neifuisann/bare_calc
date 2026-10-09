#pragma once
#include "calculator/math/calculator_bigint.h"
#include "calculator/math/calculator_engine.h"
#include "calculator/math/calculator_exact.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Symbolic algebra (CAS) for the on-device calculator; docs/calculator/CAS.md is the
// spec. This header is the expression tree every CAS module shares.
//
// Nodes are immutable and reference-counted, so subtrees are shared freely
// and a transformation rebuilds only the path it changes. Numeric leaves are
// ExactNumbers (calculator_exact.h): 2/3, 5*sqrt(2), pi/2 and e^2 are single
// Number leaves, and nothing in the CAS ever holds a decimal. A constant that
// has no exact form (sin(1), 2^pi) stays a symbolic subtree instead.
//
// Nodes are allocated through ExactAllocator: one CAS run can hold a few
// thousand of them.
namespace Calculator {
namespace Cas {

enum class Kind : uint8_t {
  Number,    // exact constant (Node::number)
  Symbol,    // free variable or parameter (Node::name): x, y, A..F
  Wild,      // pattern placeholder (calculator_cas_pattern.h); never in a result
  Add,       // n-ary sum, flattened and sorted
  Mul,       // n-ary product, flattened and sorted; a Number, if any, is first
  Pow,       // base ^ exponent; sqrt(u) is u^(1/2), exp(u) is e^u
  Func,      // Node::fn applied to args[0]
  Equation,  // args[0] = args[1]
  List,      // solutions
  Integral,  // unevaluated antiderivative of args[0] in args[1]
  Solve,     // unevaluated solve of args[0] (an Equation) for args[1]
  Text,      // a printable verdict (Node::name): "No Solution"
};
enum class Fn : uint8_t { Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Asinh, Acosh, Atanh, Ln, Abs };
// The calculator's spelling ("sin", "asinh", "ln", "abs").
const char* fnName(Fn fn);

struct Node;
using Expr = std::shared_ptr<const Node>;
using Args = std::vector<Expr, ExactAllocator<Expr>>;

struct Node {
  Kind kind = Kind::Number;
  Fn fn = Fn::Sin;
  uint8_t wild = 0, constraint = 0;  // Wild only
  uint16_t depth = 1;                // longest root-to-leaf path
  uint32_t size = 1;                 // nodes in the subtree
  uint32_t hash = 0;                 // structural; equal trees hash equal
  ExactNumber number;
  std::string name;
  Args args;
};

// Bounds on any one expression. A result past them is Stack ERROR: an
// expansion that explodes must stop, not exhaust PSRAM. The depth also bounds
// every recursive pass on the Calculate worker's 32 KB stack.
constexpr uint32_t kMaxNodes = 6000;
constexpr uint16_t kMaxDepth = 40;

// Finalizes size/depth/hash and allocates. Every node is built through here.
Expr finish(Node&& node);

// One CAS evaluation's budget and error state. Errors are sticky, like the
// evaluator's: once one is set, constructors return quickly and the caller
// reports it. Math (a certain domain error such as 1/0) is the only one a
// search may recover from (Context::recover) when it abandons a failed
// attempt; TimeOut, Cancelled and Stack always end the run.
class Context {
 public:
  static constexpr uint32_t kDefaultSteps = 1500000;
  explicit Context(std::function<bool()> cancelled = {}, std::function<void()> cooperate = {},
                   uint32_t steps = kDefaultSteps);
  bool ok() const { return error_ == Error::None; }
  Error error() const { return error_; }
  void fail(Error error) { if (error_ == Error::None) error_ = error; }
  void recover() { if (error_ == Error::Math) error_ = Error::None; }
  // Counts one unit of work; yields and polls cancellation periodically.
  bool step();
  uint32_t steps() const { return steps_; }

 private:
  std::function<bool()> cancelled_;
  std::function<void()> cooperate_;
  uint32_t steps_ = 0, budget_;
  Error error_ = Error::None;
};
// The active context. Only one CAS run happens at a time (the Calculate
// worker, or a host test), so this is a plain pointer installed by Scope; a
// permissive default serves code that runs outside any Scope.
Context& context();
class Scope {
 public:
  explicit Scope(Context& context);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  Context* previous_;
};
// Builds a static rule table under a private, unlimited context: a table must
// not inherit the error state or budget of whichever run first touches it.
template <class F>
auto buildTable(F build) -> decltype(build()) {
  Context c({}, {}, UINT32_MAX);
  Scope scope(c);
  return build();
}

// ---- Structural queries --------------------------------------------------
bool equal(const Expr& a, const Expr& b);
// A total order consistent with equal(): 0 exactly for equal trees. It only
// makes canonical forms unique; the printer chooses the reading order.
int compare(const Expr& a, const Expr& b);
// `target` does not occur anywhere in `e` (structurally).
bool freeOf(const Expr& e, const Expr& target);
bool hasSymbols(const Expr& e);
// Symbol names in first-seen order, without duplicates.
void collectSymbols(const Expr& e, std::vector<std::string>& names);
// Replaces every occurrence of `target` by `replacement`, rebuilding the
// changed path canonically (so substituting a number folds what it can).
Expr substitute(const Expr& e, const Expr& target, const Expr& replacement);

}  // namespace Cas
}  // namespace Calculator
