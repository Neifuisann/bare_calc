#pragma once
#include "calculator/math/calculator_decimal.h"
#include "calculator/math/calculator_scalar.h"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Calculator {
enum class Angle : uint8_t { Degree, Radian, Gradian };
enum class IO : uint8_t { MathMath, MathDecimal, LineLine, LineDecimal };
enum class NumberFormat : uint8_t { Norm1, Norm2, Fix, Sci };
// Values are FORMAT row actions: append only.
enum class Format : uint8_t { Standard, Decimal, Mixed, Improper, Recurring, Prime, Engineering, Sexagesimal, Rectangular, Polar };
// Global Complex Result setting: the Complex app's default presentation of a
// complex result and the etched icon it lights (i or the angle mark). A local
// FORMAT choice never changes it.
enum class ComplexResult : uint8_t { Rectangular, Polar };
struct Settings {
  Angle angle = Angle::Degree;
  IO io = IO::MathMath;
  NumberFormat number = NumberFormat::Norm1;
  uint8_t digits = 3;
  bool engineer = false, mixed = false, comma = false, separators = false;
  ComplexResult complexResult = ComplexResult::Rectangular;
  bool formatMenu = false, scientificPower = true;
};
struct Memory {
  ScalarValue ans{Value(0)};
  Value preAns{0};  // Calculate's alone, always real; no other app reads or advances it
  std::array<ScalarValue,9> variables{};
  std::string f, g;
};
enum class Error : uint8_t {
  None, Syntax, Math, Argument, Stack, Circular, NotDefined, NoOperator, TimeOut, Cancelled,
  Dimension, Variable, CannotSolve, Range, Memory
};
const char* errorText(Error error);
struct Node {
  enum class Kind : uint8_t { Number, Symbol, Unary, Binary, Call };
  Kind kind;
  std::string text;
  size_t position;
  std::vector<std::shared_ptr<Node>> children;
};
using Expression = std::shared_ptr<Node>;
struct Parsed {
  Expression root;
  Error error = Error::None;
  size_t position = 0; // Source byte offset: rejected token start, or source end at EOF.
};
Parsed parse(const std::string& source);
struct Request {
  std::string expression;
  Settings settings;
  Memory memory;
  bool verify = false;
  uint32_t randomSeed = 1;
};
struct Aggregate;
struct TableData;
struct StatisticsResult;
struct DistributionResult;
struct SpreadsheetData;
// One Equation/Inequality answer page: `label`=`value`, or `text` alone
// (No Solution, an inequality's solution set).
struct Solution {
  std::string label;
  ScalarValue value;
  std::string text;
};
struct Result {
  Value value;  // the real part of a complex result
  Memory memory;
  Error error = Error::None;
  size_t position = 0; // Source byte offset (parser token or failing AST node), not a UI cursor.
  bool verified = false, truth = false;
  bool pair = false;
  Value second;
  // Complex app: set only when the imaginary part is nonzero.
  bool complex = false;
  Value imag;
  ScalarValue scalar() const { return complex ? ScalarValue::of(value, imag) : ScalarValue(value); }
  // Matrix/Vector: an aggregate outcome for MatAns/VctAns; Ans is untouched.
  std::shared_ptr<const Aggregate> aggregate;
  // Equation/Inequality answer pages, in native order.
  std::shared_ptr<const std::vector<Solution>> solutions;
  std::shared_ptr<const TableData> table;
  // Statistics/Distribution (P17): adopted summaries/rows, never Calculate history.
  std::shared_ptr<const StatisticsResult> statistics;
  std::shared_ptr<const DistributionResult> distribution;
  std::shared_ptr<const SpreadsheetData> spreadsheet;
  // CAS (calculator_cas.h): a symbolic answer as natural-display source
  // (frac(x,2), sqrt(x)...). `value` is then a placeholder and Ans is unchanged.
  std::shared_ptr<const std::string> formula;
};
// cancellation and cooperative yield belong to the caller. No UI, device or
// persistent state is touched by evaluation, including intermediate stores.
Result evaluate(const Request& request, const std::function<bool()>& cancelled = {},
                const std::function<void()>& cooperate = {});
// Real evaluation of an already-parsed subtree against the request's settings
// and memory, for evaluators that delegate their real work. No Ans/PreAns
// update and no result rounding; side effects stay in the returned memory.
Result evaluateTree(const Expression& root, const Request& request, const std::function<bool()>& cancelled = {},
                    const std::function<void()>& cooperate = {});
// Apply one scalar operator/function to already evaluated arguments. Aggregate
// evaluators use this after det/norm/dot etc. become scalars; exact values and
// the caller's variable/function bindings survive without text round-trips.
Result evaluateScalarOperation(const Expression& operation, const std::vector<Value>& arguments,
                               const Request& request, const std::function<bool()>& cancelled = {},
                               const std::function<void()>& cooperate = {});
// The exact value of integral(f, low, high) when the CAS finds one (x is the
// variable; other letters read `memory`), else false. The engine adopts it
// only after it agrees with the numeric value. The CAS installs this
// (calculator_cas.cpp), so the engine does not link against it: builds
// without the CAS keep numeric integrals.
using ExactIntegral = bool (*)(const Expression& integrand, const ExactNumber& low, const ExactNumber& high,
                               const Settings& settings, const Memory& memory,
                               const std::function<bool()>& cancelled, const std::function<void()>& cooperate,
                               ExactNumber& out);
void setExactIntegral(ExactIntegral integral);
// Verify chains: the operands between relations, left to right.
void flattenRelations(const Expression& root, std::vector<Expression>& operands, std::vector<std::string>& relations);
// No Operator without a relation; Syntax ERROR for opposite directions, or for
// = or != mixed into an ordered chain (it has no single reading).
Error verifyChainError(const std::vector<std::string>& relations);

// Exact-where-possible real arithmetic shared by the evaluators, over
// Value::symbolic (calculator_exact.h). An exact failure (division by an
// exact zero, an even root of an exact negative) yields a NaN number, which
// the callers' range checks already report as Math ERROR. None of these
// range-check; callers apply the calculator range to what they publish.
// The ClassWiz display term for n/d * sqrt(radicand) (or * pi), normalized.
Exact exactRational(int64_t numerator, int64_t denominator = 1, int64_t radicand = 1, bool pi = false);
// The exact value of a display term (Inexact for an invalid one).
ExactNumber exactNumber(const Exact& term);
Value realCombine(const Value& a, const Value& b, char op);  // + - * /
Value realNegate(const Value& a);
Value realSqrt(const Value& a);  // a >= 0
// sin or cos of an angle in `unit`, exact at multiples of 30 and 45 degrees.
Value realTrig(bool sine, const Value& angle, Angle unit);
// Exact numeric order when both sides are exact (equality is structural);
// the decimals otherwise. -1, 0 or 1.
int compareValues(const Value& a, const Value& b);
// The approximate command: `digits` significant digits (1..1000) of the exact
// value, or of the decimal (at most its 40 work digits) when inexact. Empty
// for a failed value.
std::string approximate(const Value& value, unsigned digits);

std::string formatValue(const Value&, const Settings&, Format = Format::Standard);
int variableIndex(const std::string& name);
}  // namespace Calculator
