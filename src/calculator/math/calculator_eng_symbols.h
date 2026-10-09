#pragma once
#include <cstdint>

namespace Calculator {
// Engineering symbols (CATALOG > Engineer Symbol): m u n p f k M G T P E.
// Each is one private source byte, 0x10..0x1A, read as a postfix x10^n
// operator exactly like '%' (5k = 5000, 2+3m = 2.003). A private byte keeps
// them one atomic Glyph item in EditDocument (DEL removes the whole symbol)
// and can never collide with a variable letter, a name or E-notation. The
// layout draws the reference bitmaps (calculator_layout.cpp); Linear text uses
// the ASCII letter. 0x1C/0x1E/0x1F stay the presentation's own bytes.
namespace EngSymbol {
struct Entry { char byte; int8_t exponent; char ascii; };
constexpr Entry kTable[] = {
    {'\x10', -3, 'm'}, {'\x11', -6, 'u'}, {'\x12', -9, 'n'}, {'\x13', -12, 'p'}, {'\x14', -15, 'f'},
    {'\x15', 3, 'k'},  {'\x16', 6, 'M'},  {'\x17', 9, 'G'},  {'\x18', 12, 'T'},  {'\x19', 15, 'P'},
    {'\x1A', 18, 'E'},
};
constexpr int kCount = sizeof(kTable) / sizeof(kTable[0]);
constexpr bool is(char c) { return c >= '\x10' && c <= '\x1A'; }
constexpr int index(char c) { return is(c) ? c - '\x10' : -1; }
constexpr int exponent(char c) { return is(c) ? kTable[c - '\x10'].exponent : 0; }
constexpr char ascii(char c) { return is(c) ? kTable[c - '\x10'].ascii : c; }
}  // namespace EngSymbol
}  // namespace Calculator
