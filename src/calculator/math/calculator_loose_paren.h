#pragma once

namespace Calculator {
// A '(' or ')' typed inside a natural template field (integral body, fraction
// box, exponent, ...) that has no partner in that same field. The serialized
// template uses real parentheses and commas as its own syntax, so an unmatched
// real one would swallow the template's separators ("integral((,0,1)" reads as
// an inline call). Each is instead one private source byte: EditDocument sees
// an atomic Glyph, the layout draws an ordinary paren, Linear text shows
// the ASCII one, and the parser closes an open one at the end of its field
// (an unmatched close is a Syntax ERROR, as on the calculator). The editor
// keeps each field canonical: a matched pair is always the real "()", an
// unmatched one always the private byte (MathEditor::canonicalize()).
// 0x01-0x04 and 0x1C/0x1E/0x1F are presentation bytes, 0x09-0x0D whitespace,
// 0x10-0x1A engineering symbols.
namespace LooseParen {
constexpr char kOpen = '\x05';
constexpr char kClose = '\x06';
constexpr bool is(char c) { return c == kOpen || c == kClose; }
constexpr char ascii(char c) { return c == kOpen ? '(' : c == kClose ? ')' : c; }
}  // namespace LooseParen
}  // namespace Calculator
