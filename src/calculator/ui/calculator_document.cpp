#include "calculator/ui/calculator_document.h"
#include "calculator/math/calculator_eng_symbols.h"

#include <cctype>
#include <cstring>

namespace Calculator {
namespace {
using Kind = EditDocument::Kind;
using Role = EditDocument::Role;
constexpr uint16_t kNone = EditDocument::kNone;

bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }
bool alpha(char c) { return std::isalpha(static_cast<unsigned char>(c)); }
bool alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

struct Template { const char* name; uint8_t arity; Kind kind; Role roles[3]; };
constexpr Template kTemplates[] = {
  {"pow", 2, Kind::Power, {Role::Base, Role::Exponent}},
  {"frac", 2, Kind::Fraction, {Role::Numerator, Role::Denominator}},
  {"mixed", 3, Kind::Mixed, {Role::Whole, Role::Numerator, Role::Denominator}},
  {"sqrt", 1, Kind::Sqrt, {Role::Radicand}},
  {"root", 2, Kind::Root, {Role::Index, Role::Radicand}},
  {"abs", 1, Kind::Abs, {Role::Argument}},
  {"log", 2, Kind::Log, {Role::Base, Role::Argument}},
  {"diff", 2, Kind::Diff, {Role::Body, Role::Point}},
  {"integral", 3, Kind::Integral, {Role::Body, Role::Lower, Role::Upper}},
  {"sum", 3, Kind::Sum, {Role::Body, Role::Lower, Role::Upper}},
  {"product", 3, Kind::Product, {Role::Body, Role::Lower, Role::Upper}},
  {"recur", 1, Kind::Recurring, {Role::Argument}},
};

// Up/Down relations: from `role` inside `kind`, the role it moves to. Power
// and Script are deliberately absent: neither a legacy prefix power nor a
// postfix script is a vertical pair (P3/E-02/E-03), so vertical() walks
// straight through them to the enclosing structure.
struct Vertical { Kind kind; Role from; bool up; Role to; };
constexpr Vertical kVertical[] = {
  {Kind::Fraction, Role::Numerator, false, Role::Denominator}, {Kind::Fraction, Role::Denominator, true, Role::Numerator},
  {Kind::Mixed, Role::Numerator, false, Role::Denominator}, {Kind::Mixed, Role::Denominator, true, Role::Numerator},
  {Kind::Mixed, Role::Whole, true, Role::Numerator}, {Kind::Mixed, Role::Whole, false, Role::Denominator},
  {Kind::Root, Role::Radicand, true, Role::Index}, {Kind::Root, Role::Index, false, Role::Radicand},
  {Kind::Diff, Role::Body, false, Role::Point}, {Kind::Diff, Role::Point, true, Role::Body},
};
bool limits(Kind k) { return k == Kind::Integral || k == Kind::Sum || k == Kind::Product; }
}  // namespace

uint16_t EditDocument::addField(uint16_t owner, uint8_t index, Role role) {
  Field f; f.owner = owner; f.index = index; f.role = role;
  fields_.push_back(f);
  return static_cast<uint16_t>(fields_.size() - 1);
}
uint16_t EditDocument::addItem(Kind kind, size_t begin, uint16_t field) {
  Item i; i.kind = kind; i.begin = static_cast<uint16_t>(begin); i.field = field;
  items_.push_back(i);
  return static_cast<uint16_t>(items_.size() - 1);
}
void EditDocument::link(uint16_t item) {
  Field& f = fields_[items_[item].field];
  if (f.first == kNone) f.first = item; else items_[f.last].next = item;
  f.last = item;
  // A closed item's end is a stop in its own field; an open one's end is the
  // end of the source, which its innermost open field already owns.
  if (items_[item].closed) slots_[items_[item].end] = items_[item].field;
}
void EditDocument::hide(size_t from, size_t to) {
  for (size_t o = from; o <= to && o < slots_.size(); ++o) slots_[o] = kNone;
}

// One pass with an explicit stack of open fields, so the loop task's stack use
// does not grow with nesting depth (and nesting needs no limit).
void EditDocument::rebuild(const std::string& source) {
  const std::string& s = source_ = source.size() > 0xFFF0 ? source.substr(0, 0xFFF0) : source;
  items_.clear(); fields_.clear(); open_.clear();
  slots_.assign(s.size() + 1, kNone);
  auto begin = [&](uint16_t field, size_t pos, uint16_t owner, Context context) {
    fields_[field].begin = static_cast<uint16_t>(pos);
    slots_[pos] = field;
    open_.push_back({field, owner, context});
  };
  auto token = [&](Kind kind, size_t pos, size_t end) {
    const uint16_t item = addItem(kind, pos, open_.back().field);
    items_[item].end = static_cast<uint16_t>(end);
    link(item);
    return end;
  };
  // Ends the innermost open field at `pos` and completes its owner there.
  auto finish = [&](size_t pos, bool closed) {
    const Open top = open_.back();
    open_.pop_back();
    fields_[top.field].end = static_cast<uint16_t>(pos);
    Item& it = items_[top.owner];
    it.closed = closed;
    it.end = static_cast<uint16_t>(closed ? pos + 1 : pos);
    if (it.kind == Kind::Call) classify(top.owner);
    link(top.owner);
  };
  begin(addField(kNone, 0, Role::Root), 0, kNone, Context::Root);
  size_t pos = 0;
  while (pos < s.size()) {
    const char c = s[pos];
    const Open top = open_.back();
    if (top.context != Context::Root && c == ')') { finish(pos, true); ++pos; continue; }
    if (top.context == Context::Arguments && c == ',' && items_[top.owner].fieldCount < 250) {
      fields_[top.field].end = static_cast<uint16_t>(pos);
      open_.pop_back();
      Item& call = items_[top.owner];
      const uint16_t next = addField(top.owner, call.fieldCount++, Role::Argument);
      fields_[top.field].next = next;
      begin(next, pos + 1, top.owner, Context::Arguments);
      ++pos;
      continue;
    }
    // Infix P operator: a bare 'P' is one operator glyph so "4P3" stays
    // three items. "PreAns" keeps its multi-char name token. 'C' is
    // already a single glyph (variable alphabet); operator vs variable is
    // decided by position at parse/paint time, not here.
    if (alpha(c) && !((c >= 'A' && c <= 'F' && s.compare(pos, 3, "Ans") != 0) || c == 'x' || c == 'y' || c == 'z') &&
        !(c == 'P' && s.compare(pos, 6, "PreAns") != 0)) {
      size_t end = pos + 1;
      while (end < s.size() && alnum(s[end])) ++end;
      auto is = [&](const char* name) { return end - pos == std::strlen(name) && s.compare(pos, end - pos, name) == 0; };
      const bool symbol = is("pi") || is("e") || is("Ans") || is("PreAns");
      if (end < s.size() && s[end] == '(' && !symbol) {
        const uint16_t call = addItem(Kind::Call, pos, top.field);
        items_[call].nameEnd = static_cast<uint16_t>(end);
        items_[call].fieldCount = 1;
        items_[call].firstField = addField(call, 0, Role::Argument);
        begin(items_[call].firstField, end + 1, call, Context::Arguments);
        pos = end + 1;
      } else {
        pos = token(Kind::Name, pos, end);
      }
    } else if (c == '(' || (c == '^' && pos + 1 < s.size() && s[pos + 1] == '(')) {
      // A user group, or the ^( ) exponent CATALOG's Square/Cube/Reciprocal
      // insert: its content is a script field attached to whatever precedes it.
      const bool script = c == '^';
      const uint16_t item = addItem(script ? Kind::Script : Kind::Group, pos, top.field);
      items_[item].fieldCount = 1;
      items_[item].firstField = addField(item, 0, script ? Role::Exponent : Role::Group);
      pos += script ? 2 : 1;
      begin(items_[item].firstField, pos, item, Context::Group);
    } else if ((c == '<' || c == '>' || c == '!' || c == '=') && pos + 1 < s.size() && s[pos + 1] == '=') {
      pos = token(Kind::Name, pos, pos + 2);
    } else {
      pos = token(Kind::Glyph, pos, pos + 1);
    }
  }
  // The source ended with fields still open: each owner stays open, and the
  // end offset remains the innermost field's stop.
  while (open_.size() > 1) finish(s.size(), false);
  fields_[0].end = static_cast<uint16_t>(s.size());
}

void EditDocument::classify(uint16_t item) {
  Item& it = items_[item];
  const size_t length = it.nameEnd - it.begin;
  auto is = [&](const char* name) { return length == std::strlen(name) && source_.compare(it.begin, length, name) == 0; };
  auto digitsOnly = [&](uint16_t f) {
    const Field& fl = fields_[f];
    if (fl.begin == fl.end) return false;
    for (size_t o = fl.begin; o < fl.end; ++o) if (!digit(source_[o])) return false;
    return true;
  };
  if (is("constant") && it.fieldCount == 1 && it.closed && digitsOnly(it.firstField)) {
    it.kind = Kind::Constant; it.fieldCount = 0; it.firstField = kNone;
    hide(it.begin + 1, it.end - 1);
    return;
  }
  // Base-N's scalar-variable reference: drawn as its letter, deleted whole.
  if (is("var") && it.fieldCount == 1 && it.closed && fields_[it.firstField].end == fields_[it.firstField].begin + 1) {
    it.kind = Kind::Constant; it.fieldCount = 0; it.firstField = kNone;
    hide(it.begin + 1, it.end - 1);
    return;
  }
  if (is("convert") && it.fieldCount == 2 && it.closed && digitsOnly(fields_[it.firstField].next)) {
    it.kind = Kind::Convert; it.fieldCount = 1;
    const uint16_t unit = fields_[it.firstField].next;
    fields_[it.firstField].role = Role::Operand;
    fields_[it.firstField].next = kNone;
    hide(fields_[unit].begin, fields_[unit].end);
    return;
  }
  for (const auto& t : kTemplates) {
    if (!is(t.name) || it.fieldCount != t.arity) continue;
    it.kind = t.kind;
    uint16_t f = it.firstField;
    for (uint8_t i = 0; i < t.arity; ++i, f = fields_[f].next) fields_[f].role = t.roles[i];
    return;
  }
}

bool EditDocument::hidden(uint16_t item) const {
  const Kind k = items_[item].kind;
  return k != Kind::Glyph && k != Kind::Name && k != Kind::Group && k != Kind::Call;
}
uint16_t EditDocument::fieldAt(uint16_t item, uint8_t index) const {
  uint16_t f = items_[item].firstField;
  for (uint8_t i = 0; f != kNone && i < index; ++i) f = fields_[f].next;
  return f;
}
uint16_t EditDocument::lastField(uint16_t item) const {
  uint16_t f = items_[item].firstField;
  while (f != kNone && fields_[f].next != kNone) f = fields_[f].next;
  return f;
}
bool EditDocument::allEmpty(uint16_t item) const {
  for (uint16_t f = items_[item].firstField; f != kNone; f = fields_[f].next)
    if (fields_[f].begin != fields_[f].end) return false;
  return true;
}
uint16_t EditDocument::itemEndingAt(size_t offset, uint16_t field) const {
  for (uint16_t i = fields_[field].first; i != kNone; i = items_[i].next) if (items_[i].end == offset) return i;
  return kNone;
}
uint16_t EditDocument::itemStartingAt(size_t offset, uint16_t field) const {
  for (uint16_t i = fields_[field].first; i != kNone; i = items_[i].next) if (items_[i].begin == offset) return i;
  return kNone;
}

size_t EditDocument::normalize(size_t offset) const {
  if (offset >= slots_.size()) return slots_.size() - 1;
  if (valid(offset)) return offset;
  for (const Item& it : items_)
    if (it.fieldCount == 0 && it.begin < offset && offset < it.end) return it.begin;
  while (!valid(offset)) ++offset;  // the source end is always a stop
  return offset;
}

size_t EditDocument::left(size_t offset) const {
  const uint16_t f = slotField(offset);
  if (f == kNone) return normalize(offset);
  const Field& fl = fields_[f];
  if (offset == fl.begin) {
    if (fl.owner == kNone) return offset;
    // P3/E-07: horizontal arrows never step between a fraction's vertically
    // stacked fields. Left at a denominator start leaves to before the whole
    // fraction; Up owns the trip back to the numerator.
    if (items_[fl.owner].kind == Kind::Fraction && fl.index == 1) return items_[fl.owner].begin;
    // Limits sit left of the body and the mixed denominator sits below its
    // numerator: all are vertical pairs, so Left at their start leaves to
    // before the whole template (Up/Down own the vertical moves, Right owns
    // the trip from a limit into the body).
    if (items_[fl.owner].kind == Kind::Mixed && fields_[f].role == Role::Denominator)
      return items_[fl.owner].begin;
    if (limits(items_[fl.owner].kind) &&
        (fields_[f].role == Role::Lower || fields_[f].role == Role::Upper))
      return items_[fl.owner].begin;
    uint16_t previous = kNone;
    for (uint16_t g = items_[fl.owner].firstField; g != f; g = fields_[g].next) previous = g;
    return previous != kNone ? fields_[previous].end : items_[fl.owner].begin;
  }
  const uint16_t p = itemEndingAt(offset, f);
  if (atomic(p)) return items_[p].begin;
  // Limits are stored after the body but drawn left of it, so Left from
  // after an integral/sum/product enters the body's end (the box drawn
  // nearest), mirroring Right from the body end leaving the template.
  if (limits(items_[p].kind))
    for (uint16_t g = items_[p].firstField; g != kNone; g = fields_[g].next)
      if (fields_[g].role == Role::Body) return fields_[g].end;
  return fields_[lastField(p)].end;
}

size_t EditDocument::right(size_t offset, uint16_t* close) const {
  if (close) *close = kNone;
  const uint16_t f = slotField(offset);
  if (f == kNone) return normalize(offset);
  const Field& fl = fields_[f];
  if (offset == fl.end) {
    if (fl.owner == kNone) return offset;
    // P3/E-06: Right at a numerator end leaves the whole fraction to the
    // root/right side instead of descending into the denominator; Down owns
    // numerator->denominator.
    if (items_[fl.owner].kind == Kind::Fraction && fl.index == 0) {
      const Item& frac = items_[fl.owner];
      if (frac.closed) return frac.end;
      if (frac.kind != Kind::Group && close) *close = fl.owner;
      return offset;
    }
    // Limits sit left of the body (integral/sum/product): Right from either
    // limit enters the body, Right from the body exits; Up/Down own the
    // limit<->body and limit<->limit moves. A mixed numerator/denominator is
    // a vertical pair like a fraction: Right from the numerator exits.
    if (limits(items_[fl.owner].kind)) {
      if (fields_[f].role == Role::Lower || fields_[f].role == Role::Upper) {
        for (uint16_t g = items_[fl.owner].firstField; g != kNone; g = fields_[g].next)
          if (fields_[g].role == Role::Body) return fields_[g].begin;
      }
      if (fields_[f].role == Role::Body) {
        const Item& owner = items_[fl.owner];
        if (owner.closed) return owner.end;
        if (close) *close = fl.owner;
        return offset;
      }
    }
    if (items_[fl.owner].kind == Kind::Mixed && fields_[f].role == Role::Numerator) {
      const Item& owner = items_[fl.owner];
      if (owner.closed) return owner.end;
      if (close) *close = fl.owner;
      return offset;
    }
    if (fl.next != kNone) return fields_[fl.next].begin;
    const Item& owner = items_[fl.owner];
    if (owner.closed) return owner.end;
    // An open user group simply has nothing after it; an open call or
    // template needs its ')' materialized before the cursor can leave.
    if (owner.kind != Kind::Group && close) *close = fl.owner;
    return offset;
  }
  const uint16_t p = itemStartingAt(offset, f);
  return atomic(p) ? items_[p].end : fields_[items_[p].firstField].begin;
}

uint16_t EditDocument::vertical(uint16_t field, bool up) const {
  for (uint16_t f = field; fields_[f].owner != kNone; f = items_[fields_[f].owner].field) {
    const uint16_t owner = fields_[f].owner;
    const Kind kind = items_[owner].kind;
    // P3/E-02: scripts and legacy powers are not vertical pairs. A postfix
    // exponent has no Down target of its own, so ascend through the owning
    // item and keep looking: at root level that finds nothing (a no-op, C06),
    // inside a numerator it finds the fraction denominator (C25).
    if (kind == Kind::Script || kind == Kind::Power) continue;
    const Role from = fields_[f].role;
    Role to = from;
    if (limits(kind)) {
      // Body and limits: Up reaches the upper limit, Down the lower one.
      if (from == Role::Body || (from == Role::Upper && !up) || (from == Role::Lower && up)) to = up ? Role::Upper : Role::Lower;
    } else {
      for (const auto& v : kVertical) if (v.kind == kind && v.from == from && v.up == up) to = v.to;
    }
    if (to == from) continue;
    for (uint16_t g = items_[owner].firstField; g != kNone; g = fields_[g].next)
      if (fields_[g].role == to) return g;
  }
  return kNone;
}

uint16_t EditDocument::verticalEnter(uint16_t field, bool up) const {
  // Up arrives from below, so it enters the bottom box; Down arrives from
  // above and enters the top box. Each step goes strictly deeper, so the
  // loop ends at the innermost stacked box (bounded by nesting depth; no
  // allocation, safe on the loop task). Multi-item fields keep the container
  // so the editor's nearest-x pick still applies.
  for (;;) {
    const Field& fl = fields_[field];
    const uint16_t first = fl.first;
    if (first == kNone || items_[first].next != kNone) return field;
    const Kind kind = items_[first].kind;
    if (kind != Kind::Fraction && kind != Kind::Mixed) return field;
    const Role want = up ? Role::Denominator : Role::Numerator;
    uint16_t next = kNone;
    for (uint16_t g = items_[first].firstField; g != kNone; g = fields_[g].next)
      if (fields_[g].role == want) next = g;
    if (next == kNone) return field;
    field = next;
  }
}

size_t EditDocument::operandStart(size_t cursor) const {
  const uint16_t f = slotField(cursor);
  if (f == kNone || cursor == fields_[f].begin) return cursor;
  // Re-lex the field's items up to the cursor with the evaluator's number
  // rule. `start` is where the operand ending at the current token begins:
  // a number, variable, name or structure starts one; ! % and ^( ) scripts
  // extend the one before them; an operator ends it.
  constexpr size_t kNoOperand = static_cast<size_t>(-1);
  size_t start = kNoOperand;
  bool number = false, exponent = false;  // inside a number / its E[+-]digits tail
  for (uint16_t i = fields_[f].first; i != kNone && items_[i].end <= cursor; i = items_[i].next) {
    const Item& it = items_[i];
    const char c = source_[it.begin];
    const char after = it.end < source_.size() ? source_[it.end] : '\0';
    const bool glyph = it.kind == Kind::Glyph;
    if (glyph && number && (digit(c) || (c == '.' && !exponent))) continue;
    if (glyph && number && !exponent && c == 'E' && (digit(after) || after == '+' || after == '-')) { exponent = true; continue; }
    if (glyph && number && exponent && (c == '+' || c == '-') && source_[it.begin - 1] == 'E') continue;
    number = exponent = false;
    if (glyph && (digit(c) || c == '.')) { number = true; start = it.begin; continue; }
    if ((glyph && (c == '!' || c == '%' || EngSymbol::is(c))) || it.kind == Kind::Script) continue;  // keeps `start`
    const bool variable = glyph && ((c >= 'A' && c <= 'F') || c == 'x' || c == 'y' || c == 'z');
    const bool operand = variable || (!glyph && !(it.kind == Kind::Name && !alpha(c)));  // <= and != are operators
    start = operand ? it.begin : kNoOperand;
  }
  return start == kNoOperand ? cursor : start;
}

size_t EditDocument::operandEnd(size_t cursor) const {
  const uint16_t f = slotField(cursor);
  if (f == kNone) return cursor;
  // The first item at the cursor decides the operand kind, mirroring
  // operandStart: a number (with its E[+-]digits tail), one variable letter,
  // a name, or one complete structure. Anything else (operator, delimiter,
  // field end) means there is no following operand.
  const uint16_t first = itemStartingAt(cursor, f);
  if (first == kNone) return cursor;
  const Item& head = items_[first];
  size_t end = cursor;
  if (head.kind == Kind::Glyph) {
    const char c = source_[head.begin];
    const bool variable = (c >= 'A' && c <= 'F') || c == 'x' || c == 'y' || c == 'z';
    if (variable) {
      end = head.end;
    } else if (digit(c) || c == '.') {
      // Forward number lex with the same E-tail rule operandStart uses.
      bool exponent = false;
      uint16_t j = first;
      end = head.end;
      for (;;) {
        const uint16_t n = items_[j].next;
        if (n == kNone || items_[n].kind != Kind::Glyph) break;
        const char nc = source_[items_[n].begin];
        const char after = items_[n].end < source_.size() ? source_[items_[n].end] : '\0';
        if (digit(nc) || (nc == '.' && !exponent)) { j = n; end = items_[n].end; continue; }
        if (!exponent && nc == 'E' && (digit(after) || after == '+' || after == '-')) {
          exponent = true; j = n; end = items_[n].end; continue;
        }
        if (exponent && (nc == '+' || nc == '-') && source_[items_[n].begin - 1] == 'E') {
          j = n; end = items_[n].end; continue;
        }
        break;
      }
    } else {
      return cursor;  // operator or delimiter: no following operand
    }
  } else if (head.kind == Kind::Name) {
    if (!alpha(source_[head.begin])) return cursor;  // <= and != are operators
    end = head.end;
  } else {
    end = head.end;  // one complete structure: call, group or template
  }
  // Postfix modifiers extend the operand they follow, as in operandStart.
  for (;;) {
    const uint16_t n = itemStartingAt(end, f);
    if (n == kNone) break;
    const Item& tail = items_[n];
    if (!((tail.kind == Kind::Glyph &&
           (source_[tail.begin] == '!' || source_[tail.begin] == '%' || EngSymbol::is(source_[tail.begin]))) ||
          tail.kind == Kind::Script))
      break;
    end = tail.end;
  }
  return end;
}

uint16_t EditDocument::jumpEnclosing(size_t offset) const {
  uint16_t f = slotField(offset);
  if (f == kNone) return kNone;
  // Walk the owner chain outward; the first Mixed/Integral/Sum/Product met
  // is the innermost qualifying template.
  for (;;) {
    const uint16_t owner = fields_[f].owner;
    if (owner == kNone) return kNone;
    const Kind kind = items_[owner].kind;
    if (kind == Kind::Mixed || kind == Kind::Integral || kind == Kind::Sum || kind == Kind::Product)
      return owner;
    f = items_[owner].field;
  }
}
}  // namespace Calculator
