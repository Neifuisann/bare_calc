#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Calculator {
// Tolerant structural reading of a serialized expression, shared by editor and
// layout so cursor stops, field membership, Power's operand and draw position
// have one answer. The text stays canonical (evaluator, history, f/g, NVS).
// Nothing evaluates; incomplete input is read as far as it goes.
//
// Tokens follow Parser::next() in calculator_engine.cpp: numbers with an
// optional E[+-]digits tail, A-F (not "Ans") and x/y/z as variables, other
// letters as one identifier; identifier + '(' is a call. Natural templates
// (pow, frac, mixed, sqrt, root, abs, log/2, diff, integral, sum, product) at
// the right arity draw as math with name, parentheses and commas hidden; any
// other arity stays a visible call.
//
// Cursor stops: a field's start and the end of every item directly in it,
// never hidden syntax or the inside of a name. Each offset belongs to one field
// (a last field's end and the slot after its template are distinct), except
// the source end inside an open template: the innermost open field owns it.
// Storage is reused, so rebuild() does not allocate after warm-up.
class EditDocument {
 public:
  enum class Kind : uint8_t {
    Glyph,      // one visible character
    Name,       // atomic multi-character token: Ans, pi, <=, an unknown identifier
    Group,      // user parentheses; both are visible characters
    Call,       // ordinary function call: name, parentheses and commas visible
    Power, Script, Fraction, Mixed, Sqrt, Root, Abs, Log, Diff, Integral, Sum, Product,
    Constant,   // constant(n): one atomic symbol
    Convert,    // convert(x,n): editable operand plus an atomic unit suffix
    Recurring,  // recur(digits): overlined digits, result presentation only
  };
  enum class Role : uint8_t {
    Root, Group, Argument, Base, Exponent, Numerator, Denominator, Whole,
    Radicand, Index, Body, Lower, Upper, Point, Operand,
  };
  static constexpr uint16_t kNone = 0xFFFF;
  struct Item {
    uint16_t begin = 0, end = 0;  // source span [begin, end)
    uint16_t field = kNone;       // the field this item sits in
    uint16_t next = kNone;        // next item in that field
    uint16_t firstField = kNone;  // this item's own fields, linked by Field::next
    uint16_t nameEnd = 0;         // calls: end of the name, where '(' sits
    Kind kind = Kind::Glyph;
    uint8_t fieldCount = 0;       // visible (editable) fields
    bool closed = true;           // false: the source ended before its ')'
  };
  struct Field {
    uint16_t begin = 0, end = 0;  // content span [begin, end)
    uint16_t owner = kNone;       // owning item; kNone for the root
    uint16_t first = kNone, last = kNone;  // items
    uint16_t next = kNone;        // owner's next field
    uint8_t index = 0;
    Role role = Role::Root;
  };

  void rebuild(const std::string& source);
  const std::string& source() const { return source_; }
  size_t items() const { return items_.size(); }
  size_t fields() const { return fields_.size(); }
  const Item& item(uint16_t i) const { return items_[i]; }
  const Field& field(uint16_t f) const { return fields_[f]; }
  uint16_t root() const { return 0; }

  // Field owning the cursor stop at `offset`, or kNone if it is not a stop.
  uint16_t slotField(size_t offset) const { return offset < slots_.size() ? slots_[offset] : kNone; }
  bool valid(size_t offset) const { return slotField(offset) != kNone; }
  // Nearest stop: an offset inside hidden template syntax goes to the field
  // it introduces, one inside an atomic name to the name's start. Used for
  // evaluator error positions and anything else that did not come from here.
  size_t normalize(size_t offset) const;
  uint16_t itemEndingAt(size_t offset, uint16_t field) const;
  uint16_t itemStartingAt(size_t offset, uint16_t field) const;
  uint16_t fieldAt(uint16_t item, uint8_t index) const;  // kNone past the last
  uint16_t lastField(uint16_t item) const;
  // Natural templates hide their name, parentheses and commas; calls and
  // groups show them.
  bool hidden(uint16_t item) const;
  bool atomic(uint16_t item) const { return items_[item].fieldCount == 0; }
  bool allEmpty(uint16_t item) const;

  // Arrow traversal between stops. right() reports an open template it
  // would have to close (set *close to that item) instead of moving.
  size_t left(size_t offset) const;
  size_t right(size_t offset, uint16_t* close) const;
  // Field an Up/Down press moves to from `field`, walking outwards through
  // enclosing structures; kNone if there is none. The editor picks the slot
  // in it nearest the current caret x.
  uint16_t vertical(uint16_t field, bool up) const;
  // Field actually entered: descends through single nested fractions so Up
  // lands in the bottom box and Down in the top (recursively). Any field that
  // is not one Fraction/Mixed item comes back unchanged.
  uint16_t verticalEnter(uint16_t field, bool up) const;
  // Start of the atom ending at `cursor` (what Power, x^2 and the fraction key
  // wrap): a whole number with any E tail, one variable, a name, or one
  // structure, plus trailing ! % and ^( ) scripts. `cursor` if none ends there.
  size_t operandStart(size_t cursor) const;
  // The symmetric end of the atom starting at `cursor` (E-11); `cursor` if none
  // starts there (operator, delimiter, end of field).
  size_t operandEnd(size_t cursor) const;
  // Innermost enclosing Mixed/Integral/Sum/Product, or kNone (E-12): what
  // SHIFT+Left/Right jump across; anything else keeps start/end behavior.
  uint16_t jumpEnclosing(size_t offset) const;

 private:
  std::string source_;
  std::vector<Item> items_;
  std::vector<Field> fields_;
  std::vector<uint16_t> slots_;
  enum class Context : uint8_t { Root, Group, Arguments };
  struct Open { uint16_t field, owner; Context context; };
  std::vector<Open> open_;  // parse stack of open fields, innermost last
  uint16_t addField(uint16_t owner, uint8_t index, Role role);
  uint16_t addItem(Kind kind, size_t begin, uint16_t field);
  void link(uint16_t item);
  void classify(uint16_t item);
  void hide(size_t from, size_t to);
};
}  // namespace Calculator
