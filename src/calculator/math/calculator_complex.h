#pragma once
#include "calculator/math/calculator_engine.h"
#include <functional>
#include <string>

// Complex app evaluation and presentation (module map: docs/ARCHITECTURE.md).
// A subtree holding no complex token goes to the real evaluator
// unchanged, so fractions, radicals, pi, trig, f/g and every exact rule keep
// Calculate's semantics. Only i, the polar angle '@', Conjg/Arg/ReP/ImP (stored
// as conjg/arg/rep/imp: capital A-F lex as variables), + - * /, integer powers
// and |z| act on complex values; any other function given a complex operand
// is Math ERROR -- no complex trig, logarithms or roots.
namespace Calculator {
// Updates Ans on success and never PreAns, which belongs to Calculate.
Result evaluateComplex(const Request& request, const std::function<bool()>& cancelled = {},
                       const std::function<void()>& cooperate = {});
// Modulus and principal argument, -180 < theta <= 180 degrees expressed in
// `unit`; both exact at the standard angles. False for zero, whose argument
// is undefined.
bool toPolar(const Value& re, const Value& im, Angle unit, Value& r, Value& theta);
// Result text: "a+bi" with zero parts and unit coefficients omitted ("3i",
// "-i", "2") or "r@theta" with '@' the angle sign. Standard/Decimal/ENG apply
// to each component; Rectangular/Polar pick the form, otherwise a nonreal
// value follows settings.complexResult. `present`, when given, maps each
// component first (the renderer passes presentResult for its template grammar).
std::string formatComplex(const Value& re, const Value& im, const Settings& settings, Format format,
                          std::string (*present)(const std::string&) = nullptr);
}  // namespace Calculator
