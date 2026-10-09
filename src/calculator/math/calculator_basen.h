#pragma once
#include "calculator/math/calculator_engine.h"
#include <cstdint>
#include <functional>
#include <string>

// Base-N (manual pp.134-137): signed 32-bit integer arithmetic and logic in
// four bases, on its own small parser -- the real lexer reads A-F as variables,
// here they are hex digits. Canonical text keeps every token's identity, so a
// FORMAT base change never reinterprets what was typed:
//   A-F        hex digits, valid only where the literal's base allows them
//   $ # & |    prefixes d h b o, binding the literal right after them
//   var(A)     a scalar-variable reference (drawn as its letter)
//   " and " " or " " xor " " xnor "   logic operators; neg( ) not( ) calls
// Overflow is Math ERROR, never wraparound. Division truncates toward zero at
// each step (plan O-6 decides whether native waits until the end).
namespace Calculator {

// Base-N's current base, which unprefixed literals use: 10, 16, 2 or 8.
Result evaluateBaseN(const Request& request, uint8_t base, const std::function<bool()>& cancelled = {});
// Decimal signed and unpadded; hexadecimal 8, binary 32 and octal 11 digits of
// the two's-complement pattern.
std::string formatBaseN(int32_t value, uint8_t base);
// FORMAT order: Decimal, Hexadecimal, Binary, Octal.
uint8_t nextBase(uint8_t base);
const char* baseName(uint8_t base);

}  // namespace Calculator
