#include "calculator/ui/calculator_layout.h"
#include "ui/canvas_gray.h"

#include "calculator/assets/calculator_constants.h"
#include "calculator/math/calculator_conversions.h"
#include "calculator/math/calculator_eng_symbols.h"
#include "calculator/math/calculator_loose_paren.h"
#include "calculator/ui/calculator_metrics.h"

#include <Adafruit_GFX.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Calculator {
namespace {
using Kind = EditDocument::Kind;
using Role = EditDocument::Role;
using Style = MathFont::Style;
using Glyph = MathFont::Glyph;
namespace M = MathMetrics;
constexpr uint16_t kNone = EditDocument::kNone;
constexpr uint32_t kPlaceholder = 0x25A1, kItalicX = 0x1D465, kArrow = 0x2192;

const M::Style& metrics(Style s) { return s == Style::Normal ? M::kNormal : M::kScript; }
using Rect = MathLayout::Rect;
Rect translated(Rect r, int x, int y) { r.x += x; r.y += y; return r; }
void unite(Rect& a, Rect b) {
  if (!b.width || !b.height) return;
  if (!a.width || !a.height) { a = b; return; }
  const int right = std::max(a.x + a.width, b.x + b.width);
  const int bottom = std::max(a.y + a.height, b.y + b.height);
  a.x = std::min(a.x, b.x); a.y = std::min(a.y, b.y);
  a.width = right - a.x; a.height = bottom - a.y;
}
Rect glyphInk(const Glyph& g) { return {static_cast<int16_t>(g.inkLeft()), g.yOffset, g.width, g.height}; }
Rect emptyFieldInk(Style s) {
  // Input boxes are layout outlines: the reference Script cell is 6x9,
  // whereas the frozen font's squarewhite.ssty glyph is only 5 columns.
  const auto& m = metrics(s);
  return {0, int16_t(-m.ascent), int16_t(s == Style::Normal ? 10 : 6), int16_t(m.ascent + m.descent)};
}

// The font test pins the short list of symbols with no native script glyph;
// those draw normal size. Anything never baked draws as a visible '?'.
const Glyph& glyph(uint32_t code, Style s) {
  if (const Glyph* g = MathFont::find(code, s)) return *g;
  if (const Glyph* g = MathFont::find(code, Style::Normal)) return *g;
  return *MathFont::find('?', s);
}
uint32_t glyphCode(unsigned char c) {
  switch (c) {
    case '*': return 0xD7;
    case '/': return 0xF7;
    case 'x': return kItalicX;
    case 'y': return 0x1D466;
    case 'z': return 0x1D467;
    case '@': return 0x2220;  // Complex polar angle r@theta
    case '\'': return 0x22C5;  // Vector dot product
    // Base-N prefixes: stored as symbols so they can never read as hex digits.
    case '$': return 'd';
    case '#': return 'h';
    case '&': return 'b';
    case '|': return 'o';
    // P12 display-document locale placeholder: comma decimal mark inside
    // templates. EditDocument sees 0x1C (not ','), so frac()/mixed() fields
    // never split; the layout draws the comma glyph. Direct display-document
    // lowering, never canonical editor source.
    case '\x1C': return ',';
    // An unmatched paren in a template field: a plain, unstretched glyph.
    case LooseParen::kOpen: return '(';
    case LooseParen::kClose: return ')';
    // Relation signs in fixed labels (Inequality menus and answer headers).
    case '\x03': return 0x2264;
    case '\x04': return 0x2265;
    default: return c;
  }
}
// P12 R-03 prime ticks (U+2032/U+2033): the OTF has no primes (verified), so
// the presentation emits private bytes 0x1E/0x1F and the layout draws distinct
// slanted ticks. Degree keeps the baked 0xB0 circle. Never three degrees,
// never ASCII apostrophe/quote.
bool isPrimeMinute(unsigned char c) { return c == '\x1E'; }
bool isPrimeSecond(unsigned char c) { return c == '\x1F'; }
int16_t primeAdvance(unsigned char c, Style s);
// Infix P/C operators draw fake-bold (double-strike, +1px) like the real
// Casio's bold P/C. 'P' as a glyph is always the operator (no variable P;
// "PreAns" is a Name, never a glyph). 'C' collides with variable C: it is
// the operator iff an operand ends immediately to its left in the same
// field, matching the parser (operator position = after a complete operand).
// A trailing C with no right operand yet (transient "4C" while typing)
// still shows bold so the operator appears the moment it is inserted; use
// explicit "*" for multiplication by variable C (e.g. "4*C").
bool combOperatorBold(const EditDocument& d, uint16_t i) {
  const EditDocument::Item& it = d.item(i);
  if (it.kind != Kind::Glyph) return false;
  const std::string& src = d.source();
  const char c = src[it.begin];
  if (c == 'P') return true;
  if (c != 'C') return false;
  const uint16_t left = d.itemEndingAt(it.begin, it.field);
  if (left == kNone) return false;
  const EditDocument::Item& li = d.item(left);
  if (li.kind != Kind::Glyph) return true;  // group/call/template/Name close
  const char lc = src[li.begin];
  if (std::isdigit(static_cast<unsigned char>(lc)) || lc == '.' || lc == ')' || lc == '!' || lc == '%') return true;
  if ((lc >= 'A' && lc <= 'F') || lc == 'x' || lc == 'y' || lc == 'z') return true;
  return false;
}
// Pixel art the OTF does not carry, transcribed from the reference captures
// (casio-880btg asinacosatan.bmp, engineersynmbo.bmp). Row bit (width-1-x) is
// column x; offsets are relative to the pen and the baseline like a Glyph.
struct Bits { uint8_t width, height; int8_t xOffset, yOffset; uint8_t advance; uint16_t rows[12]; };
// The raised "-1" of sin^-1/cos^-1/tan^-1, right after the name's last letter;
// the '(' follows 11 columns later. Script names get a smaller hand-made one.
constexpr Bits kInverse{9, 7, 0, -12, 11, {0x003, 0x00F, 0x00F, 0x1F3, 0x1F3, 0x003, 0x003}};
constexpr Bits kInverseScript{6, 5, 0, -9, 7, {0x03, 0x07, 0x3B, 0x03, 0x03}};
// Engineering symbols in EngSymbol::kTable order: m u n p f k M G T P E.
// Casio's bold 3-px strokes; like the symbols with no native script glyph,
// they draw normal size in every style.
constexpr Bits kEngineering[EngSymbol::kCount] = {
    {10, 9, 1, -9, 12, {0x36E, 0x3FF, 0x3B7, 0x3B7, 0x3B7, 0x3B7, 0x3B7, 0x3B7, 0x3B7}},
    {9, 10, 1, -9, 11, {0x077, 0x077, 0x077, 0x0EE, 0x0EE, 0x0FF, 0x1F7, 0x1C0, 0x1C0, 0x1C0}},
    {8, 9, 1, -9, 10, {0xDE, 0xFF, 0xF7, 0xE7, 0xE7, 0xE7, 0xE7, 0xE7, 0xE7}},
    {9, 10, 1, -9, 11, {0x1FE, 0x1FF, 0x1C7, 0x1C7, 0x1C7, 0x1FF, 0x1FE, 0x1C0, 0x1C0, 0x1C0}},
    {9, 12, 1, -12, 11, {0x03E, 0x07F, 0x073, 0x070, 0x1FE, 0x1FE, 0x070, 0x070, 0x070, 0x070, 0x070, 0x070}},
    {9, 12, 1, -12, 11, {0x1C0, 0x1C0, 0x1C0, 0x1C7, 0x1CF, 0x1DE, 0x1FC, 0x1F8, 0x1FC, 0x1DE, 0x1CF, 0x1C7}},
    {10, 12, 1, -12, 12, {0x387, 0x387, 0x3CF, 0x3CF, 0x3CF, 0x3FF, 0x3FF, 0x3B7, 0x3B7, 0x3B7, 0x387, 0x387}},
    {10, 12, 1, -12, 12, {0x07E, 0x1FF, 0x1E3, 0x3C0, 0x3C0, 0x3CF, 0x3CF, 0x3C3, 0x3C3, 0x1E3, 0x1FF, 0x07E}},
    {9, 12, 1, -12, 11, {0x1FF, 0x1FF, 0x038, 0x038, 0x038, 0x038, 0x038, 0x038, 0x038, 0x038, 0x038, 0x038}},
    {9, 12, 1, -12, 11, {0x1FE, 0x1FF, 0x1C7, 0x1C7, 0x1C7, 0x1FF, 0x1FE, 0x1C0, 0x1C0, 0x1C0, 0x1C0, 0x1C0}},
    {8, 12, 1, -12, 10, {0xFF, 0xFF, 0xE0, 0xE0, 0xE0, 0xFE, 0xFE, 0xE0, 0xE0, 0xE0, 0xFF, 0xFF}},
};
const Bits& inverseBits(Style s) { return s == Style::Normal ? kInverse : kInverseScript; }
// A text run as codepoints: names, function names, constant and conversion
// labels. Letters stay upright; " -> " in a conversion label is the arrow.
template <class F>
void eachCode(const char* text, size_t length, F f) {
  for (size_t i = 0; i < length; ++i) {
    if (length - i >= 4 && std::memcmp(text + i, " -> ", 4) == 0) { f(kArrow); i += 3; continue; }
    f(static_cast<unsigned char>(text[i]));
  }
}
// Name atoms that are a single mathematical symbol.
uint32_t nameSymbol(const std::string& s, size_t begin, size_t end) {
  auto is = [&](const char* n) { return end - begin == std::strlen(n) && s.compare(begin, end - begin, n) == 0; };
  if (is("pi")) return 0x1D70B;
  if (is("e")) return 0x1D452;
  if (is("i")) return 0x1D456;
  if (is("<=")) return 0x2264;
  if (is(">=")) return 0x2265;
  if (is("!=")) return 0x2260;
  if (is("==")) return '=';
  return 0;
}
int16_t runWidth(const char* text, size_t length, Style s) {
  int16_t w = 0;
  eachCode(text, length, [&](uint32_t c) { w += glyph(c, s).advance; });
  return w;
}
// A call's drawn name. The Complex CATALOG functions are stored lower-case
// (capital A-F lex as variables) and drawn with their native names; the
// inverse trig functions draw as the calculator's sin^-1/cos^-1/tan^-1.
struct CallName { const char* text; size_t length; bool inverse = false; };
CallName callName(const std::string& s, const EditDocument::Item& it) {
  static constexpr struct { const char* stored; const char* shown; bool inverse; } kNames[] = {
      {"conjg", "Conjg", false}, {"arg", "Arg", false}, {"rep", "ReP", false}, {"imp", "ImP", false},
      {"neg", "Neg", false}, {"not", "Not", false}, {"det", "Det", false}, {"trn", "Trn", false},
      {"identity", "Identity", false}, {"unitv", "UnitV", false}, {"angle", "Angle", false},
      {"asin", "sin", true}, {"acos", "cos", true}, {"atan", "tan", true}};
  const size_t length = it.nameEnd - it.begin;
  for (const auto& n : kNames)
    if (length == std::strlen(n.stored) && s.compare(it.begin, length, n.stored) == 0) return {n.shown, std::strlen(n.shown), n.inverse};
  return {s.c_str() + it.begin, length};
}
int16_t callNameWidth(const CallName& label, Style s) {
  return runWidth(label.text, label.length, s) + (label.inverse ? inverseBits(s).advance : 0);
}
const char* constantLabel(const std::string& s, const EditDocument::Item& it) {
  if (s.compare(it.begin, 4, "var(") == 0) {  // Base-N variable reference
    static const char kLetters[] = "A\0B\0C\0D\0E\0F\0x\0y\0z";
    const char c = s[it.begin + 4];
    const int i = c >= 'A' && c <= 'F' ? c - 'A' : c >= 'x' && c <= 'z' ? c - 'x' + 6 : -1;
    return i < 0 ? "?" : kLetters + 2 * i;
  }
  const long index = std::strtol(s.c_str() + it.begin + 9, nullptr, 10);  // after "constant("
  constexpr long count = sizeof(kConstants) / sizeof(kConstants[0]);
  return index >= 0 && index < count ? kConstants[index].label : "?";
}
const char* conversionLabel(const std::string& s, const EditDocument& doc, uint16_t item) {
  // The hidden unit index follows the operand's closing comma.
  const long index = std::strtol(s.c_str() + doc.field(doc.item(item).firstField).end + 1, nullptr, 10);
  constexpr long count = sizeof(kConversions) / sizeof(kConversions[0]);
  return index >= 0 && index < count ? kConversions[index].label : "?";
}

// A pair of parentheses sized to content: variant 0 is the style's own glyph
// on the baseline; taller content takes the first size variant tall enough,
// centred on the content. Anything within one ordinary line (the 12+1 row
// minima) keeps the base glyph: an empty argument's box is 13 rows against
// the 12-row '(' and used to open "cos(" with a bigger pair that shrank on
// the first digit.
struct Delims { uint8_t variant = 0; int16_t baseline = 0, top = 0, bottom = 0, left = 0, right = 0; };
Delims parens(const Rect& content, Style s) {
  const int16_t need = content.height;
  Delims d;
  const Glyph* g = &glyph('(', s);
  const M::Style& m = metrics(s);
  if (need > g->height && need > m.ascent + m.descent) {
    const uint8_t count = MathFont::variants('(');
    for (d.variant = 1; d.variant < count && MathFont::find('(', Style::Normal, d.variant)->height < need; ++d.variant) {}
    g = MathFont::find('(', Style::Normal, d.variant);
    const int16_t top = content.y + (content.height - g->height) / 2;
    d.baseline = top - g->yOffset;
  }
  d.top = -(d.baseline + g->yOffset);
  d.bottom = d.baseline + g->yOffset + g->height;
  d.left = g->advance;
  d.right = (d.variant ? *MathFont::find(')', Style::Normal, d.variant) : glyph(')', s)).advance;
  return d;
}
const Glyph& paren(char c, uint8_t variant, Style s) {
  return variant ? *MathFont::find(c, Style::Normal, variant) : glyph(c, s);
}
MathLayout::Box grow(MathLayout::Box b, int16_t ascent, int16_t descent) {
  b.ascent = std::max(b.ascent, ascent);
  b.descent = std::max(b.descent, descent);
  return b;
}
// The d/dx operator of a derivative: a small fraction, 'd' over 'dx'.
struct DiffOp { int16_t width, bar, numerator, denominator; };
DiffOp diffOp(Style s) {
  const M::Style& m = metrics(s);
  const int16_t d = glyph('d', s).advance, dx = d + glyph(kItalicX, s).advance;
  const int16_t bar = -m.axis;
  return {static_cast<int16_t>(dx + M::kFractionBoxPad), bar,
          static_cast<int16_t>(bar - M::kFractionNumeratorGapMin - m.descent),
          static_cast<int16_t>(bar + M::kFractionRuleThickness + M::kFractionDenominatorGapMin + m.ascent)};
}

struct Painter {
  Adafruit_GFX* canvas;
  MathLayout::Rect clip;
  Rect* ink = nullptr;
  uint8_t level = 1;
  void pixel(int x, int y) const {
    if (x >= clip.x && x < clip.x + clip.width && y >= clip.y && y < clip.y + clip.height) canvas->drawPixel(x, y, level);
  }
  void draw(const Glyph& g, int pen, int baseline) const {
    if (ink) { unite(*ink, translated(glyphInk(g), pen, baseline)); return; }
    for (int y = 0; y < g.height; ++y)
      for (int x = 0; x < g.width; ++x)
        if (MathFont::pixel(g, x, y)) pixel(pen + g.xOffset + x, baseline + g.yOffset + y);
  }
  int run(const char* text, size_t length, Style s, int pen, int baseline) const {
    eachCode(text, length, [&](uint32_t c) { const Glyph& g = glyph(c, s); draw(g, pen, baseline); pen += g.advance; });
    return pen;
  }
  void bits(const Bits& b, int pen, int baseline) const {
    const int x0 = pen + b.xOffset, y0 = baseline + b.yOffset;
    if (ink) { unite(*ink, {int16_t(x0), int16_t(y0), b.width, b.height}); return; }
    for (int y = 0; y < b.height; ++y)
      for (int x = 0; x < b.width; ++x)
        if (b.rows[y] >> (b.width - 1 - x) & 1) pixel(x0 + x, y0 + y);
  }
  void hline(int x, int y, int w) const {
    if (w <= 0) return;
    if (ink) { unite(*ink, {int16_t(x), int16_t(y), int16_t(w), 1}); return; }
    for (int i = 0; i < w; ++i) pixel(x + i, y);
  }
  void vline(int x, int y, int h) const {
    if (h <= 0) return;
    if (ink) { unite(*ink, {int16_t(x), int16_t(y), 1, int16_t(h)}); return; }
    for (int i = 0; i < h; ++i) pixel(x, y + i);
  }
};
int16_t primeAdvance(unsigned char c, Style s) {
  if (c == '\x1F') return s == Style::Normal ? 7 : 5;
  return s == Style::Normal ? 4 : 3;
}
void paintPrime(const Painter& p, unsigned char c, int pen, int baseline, Style s) {
  const int h = s == Style::Normal ? 7 : 5;
  const int top = baseline - 11;
  auto tick = [&](int dx) {
    for (int r = 0; r < h; ++r) {
      const int x = pen + dx + (r < h / 2 ? 1 : 0);
      p.pixel(x, top + r);
      if (r > 0 && r < h - 1) p.pixel(x + 1, top + r);
    }
  };
  tick(0);
  if (c == '\x1F') tick(3);
}
}  // namespace

MathLayout::Box MathLayout::compose(uint16_t field) {
  const EditDocument& d = *doc_;
  const Style s = fieldStyle_[field];
  const M::Style& m = metrics(s);
  const EditDocument::Field& f = d.field(field);
  // An empty template field shows its box; an empty line shows only the caret.
  Rect& ink = fieldInk_[field]; ink = {};
  if (f.first == kNone) {
    if (field) ink = emptyFieldInk(s); // real painted outline, never caret ink
    return {static_cast<int16_t>(field ? glyph(kPlaceholder, s).advance : 0), m.ascent, m.descent};
  }
  Box box{0, m.ascent, m.descent}, previous{0, m.ascent, m.descent};
  Rect previousInk{};
  for (uint16_t i = f.first; i != kNone; i = d.item(i).next) {
    if (d.item(i).kind == Kind::Script) {
      // An exponent hangs off whatever precedes it, so it is finished here,
      // where that neighbour's box is known.
      const Box& e = fieldBox_[d.item(i).firstField];
      const Rect& ei = fieldInk_[d.item(i).firstField];
      const int16_t shift = std::max<int16_t>(previous.ascent - m.superscriptBaselineDropMax,
          m.superscriptBottomMin + ei.y + ei.height - (previousInk.y + previousInk.height));
      fieldOffset_[d.item(i).firstField] = {0, static_cast<int16_t>(-shift)};
      Box& b = itemBox_[i];
      b.ascent = shift + e.ascent;
      b.descent = std::max<int16_t>(0, e.descent - shift);
      if (!d.item(i).closed) b = grow(b, m.ascent, m.descent);
      itemInk_[i] = translated(ei, 0, -shift);
    }
    unite(ink, translated(itemInk_[i], box.width, 0));
    box.width += itemBox_[i].width;
    box = grow(box, itemBox_[i].ascent, itemBox_[i].descent);
    previous = itemBox_[i];
    previousInk = itemInk_[i];
  }
  return box;
}

void MathLayout::measure(uint16_t i) {
  const EditDocument& d = *doc_;
  const EditDocument::Item& it = d.item(i);
  const std::string& src = d.source();
  const Style s = fieldStyle_[it.field];
  const M::Style& m = metrics(s);
  Extra& x = extra_[i];
  Box& box = itemBox_[i];
  box = {0, m.ascent, m.descent};
  uint16_t fields[3] = {kNone, kNone, kNone};
  {
    uint16_t f = it.firstField;
    for (int k = 0; k < 3 && f != kNone; ++k, f = d.field(f).next) fields[k] = f;
  }
  for (uint16_t f = it.firstField; f != kNone; f = d.field(f).next) fieldBox_[f] = compose(f);
  auto fb = [&](int k) -> const Box& { return fieldBox_[fields[k]]; };
  auto at = [&](int k, int dx, int dy) { fieldOffset_[fields[k]] = {static_cast<int16_t>(dx), static_cast<int16_t>(dy)}; };

  switch (it.kind) {
    case Kind::Glyph: {
      const unsigned char c = static_cast<unsigned char>(src[it.begin]);
      if (isPrimeMinute(c) || isPrimeSecond(c))
        box.width = primeAdvance(c, s);
      else if (EngSymbol::is(static_cast<char>(c)))
        box.width = kEngineering[EngSymbol::index(static_cast<char>(c))].advance;
      else
        box.width = glyph(glyphCode(c), s).advance;
      break;
    }
    case Kind::Name: {
      const uint32_t symbol = nameSymbol(src, it.begin, it.end);
      box.width = symbol ? glyph(symbol, s).advance : runWidth(src.c_str() + it.begin, it.end - it.begin, s);
      break;
    }
    case Kind::Constant: {
      const char* label = constantLabel(src, it);
      box.width = runWidth(label, std::strlen(label), s);
      break;
    }
    case Kind::Group: case Kind::Recurring: {
      const Box& c = fb(0);
      if (it.kind == Kind::Recurring) {  // overline one gap above the digits
        at(0, 0, 0);
        box = grow({c.width, 0, 0}, c.ascent + 2, c.descent);
        x.v[0] = -c.ascent - 1;
        break;
      }
      const Delims p = parens(fieldInk_[fields[0]], s);
      at(0, p.left, 0);
      box = grow({static_cast<int16_t>(p.left + c.width + (it.closed ? p.right : 0)), c.ascent, c.descent}, p.top, p.bottom);
      x.variant = p.variant; x.v[0] = p.baseline;
      break;
    }
    case Kind::Call: {
      const CallName label = callName(src, it);
      const int16_t name = callNameWidth(label, s);
      const int16_t comma = glyph(',', s).advance;
      Box content{0, m.ascent, m.descent};
      Rect contentInk{};
      for (uint16_t f = it.firstField; f != kNone; f = d.field(f).next)
      {
        unite(contentInk, fieldInk_[f]);
        content = grow({static_cast<int16_t>(content.width + fieldBox_[f].width + (f == it.firstField ? 0 : comma)), content.ascent, content.descent},
                       fieldBox_[f].ascent, fieldBox_[f].descent);
      }
      const Delims p = parens(contentInk, s);
      int16_t pen = name + p.left;
      for (uint16_t f = it.firstField; f != kNone; f = d.field(f).next) {
        if (f != it.firstField) pen += comma;
        fieldOffset_[f] = {pen, 0};
        pen += fieldBox_[f].width;
      }
      box = grow({static_cast<int16_t>(pen + (it.closed ? p.right : 0)), content.ascent, content.descent}, p.top, p.bottom);
      x.variant = p.variant; x.v[0] = p.baseline; x.v[1] = name;
      break;
    }
    case Kind::Power: {
      const Box &b = fb(0), &e = fb(1);
      const Rect bi = fieldInk_[fields[0]], ei = fieldInk_[fields[1]];
      const int16_t shift = std::max<int16_t>(b.ascent - m.superscriptBaselineDropMax,
          m.superscriptBottomMin + ei.y + ei.height - (bi.y + bi.height));
      at(0, 0, 0);
      at(1, b.width, -shift);
      box = grow({static_cast<int16_t>(b.width + e.width + M::kScriptExitPad), b.ascent, b.descent}, shift + e.ascent, e.descent - shift);
      break;
    }
    case Kind::Script:
      box.width = fb(0).width + M::kScriptExitPad;  // vertical extent finished in compose()
      break;
    case Kind::Fraction: case Kind::Mixed: {
      const int k = it.kind == Kind::Mixed ? 1 : 0;
      const int16_t left = k ? fb(0).width + M::kMixedGap : 0;
      const Box &n = fb(k), &dn = fb(k + 1);
      const int16_t w = std::max(n.width, dn.width) + M::kFractionBoxPad;
      const int16_t bar = -m.axis;
      const Rect ni = fieldInk_[fields[k]], di = fieldInk_[fields[k + 1]];
      const int16_t nb = bar - M::kFractionNumeratorInkGap - (ni.y + ni.height);
      const int16_t db = bar + M::kFractionRuleThickness + M::kFractionDenominatorInkGap - di.y;
      if (k) at(0, 0, 0);
      at(k, left + (w - n.width) / 2, nb);
      at(k + 1, left + (w - dn.width) / 2, db);
      box = grow({static_cast<int16_t>(left + w), static_cast<int16_t>(n.ascent - nb), static_cast<int16_t>(db + dn.descent)},
                 k ? fb(0).ascent : 0, k ? fb(0).descent : 0);
      // The structural rule box starts at the cell left, before the pen.
      // Child insertion slots retain their advance-centred +2 padding.
      x.v[0] = bar; x.v[1] = left + m.caretLeft; x.v[2] = w;
      break;
    }
    case Kind::Sqrt: case Kind::Root: {
      const int k = it.kind == Kind::Root ? 1 : 0;
      const Box& r = fb(k);
      const int16_t overbar = -(r.ascent + M::kRadicalVerticalGap + M::kRadicalRuleThickness);
      const int16_t need = r.descent - m.descent - overbar;
      const uint8_t count = MathFont::variants(0x221A);
      uint8_t v = 1;  // the base radical is a wider drawing; sizes run v1..
      while (v < count && MathFont::find(0x221A, Style::Normal, v)->height < need) ++v;
      const Glyph& g = *MathFont::find(0x221A, Style::Normal, v);
      const int16_t baseline = overbar - g.yOffset;
      int16_t pen = 0;
      if (k) {
        // The index sits in the notch left of the sign, its bottom half way
        // down the sign, tucked 3 px under the sign's left arm.
        const Box& ix = fb(0);
        const int16_t ib = overbar + g.height / 2 - ix.descent;
        at(0, 0, ib);
        pen = std::max<int16_t>(0, ix.width - M::kRootIndexTuck);
        box = grow(box, ix.ascent - ib, ix.descent + ib);
      }
      at(k, pen + g.advance, 0);
      box = grow({static_cast<int16_t>(pen + g.advance + r.width + 1), box.ascent, box.descent},
                 std::max<int16_t>(r.ascent, -overbar + M::kRadicalExtraAscender), std::max<int16_t>(r.descent, baseline + g.yOffset + g.height));
      x.variant = v; x.v[0] = pen; x.v[1] = baseline; x.v[2] = pen + g.xOffset + g.width; x.v[3] = overbar;
      break;
    }
    case Kind::Abs: {
      const Box& a = fb(0);
      at(0, 3, 0);
      box = grow({static_cast<int16_t>(a.width + 6), a.ascent, a.descent}, 0, 0);
      x.v[0] = -a.ascent; x.v[1] = a.descent - m.descent;  // bar rows, inclusive
      break;
    }
    case Kind::Log: {
      const int16_t name = runWidth("log", 3, s);
      const Box &b = fb(0), &a = fb(1);
      const int16_t sub = std::max<int16_t>(M::kSubscriptShiftDown, b.ascent - M::kSubscriptTopMax);
      const Delims p = parens(fieldInk_[fields[1]], s);
      at(0, name, sub);
      at(1, name + b.width + p.left, 0);
      box = grow(grow({static_cast<int16_t>(name + b.width + p.left + a.width + p.right), a.ascent, a.descent}, p.top, p.bottom),
                 b.ascent - sub, b.descent + sub);
      x.variant = p.variant; x.v[0] = p.baseline; x.v[1] = name + b.width;
      break;
    }
    case Kind::Diff: {
      const DiffOp op = diffOp(s);
      const Box &body = fb(0), &point = fb(1);
      const Delims p = parens(fieldInk_[fields[0]], s);
      const int16_t bar = op.width + p.left + body.width + p.right + 1;
      const int16_t prefix = glyph(kItalicX, Style::Script).advance + glyph('=', Style::Script).advance;
      const int16_t sub = body.descent - m.descent + std::max<int16_t>(M::kSubscriptShiftDown, point.ascent - M::kSubscriptTopMax);
      at(0, op.width + p.left, 0);
      at(1, bar + 2 + prefix, sub);
      box = grow(grow({static_cast<int16_t>(bar + 2 + prefix + point.width), body.ascent, body.descent}, p.top, p.bottom),
                 std::max<int16_t>(point.ascent - sub, m.ascent - op.numerator), std::max<int16_t>(point.descent + sub, op.denominator + m.descent));
      // v[2]: rows the evaluation bar reaches above the baseline (the
      // parentheses' top); v[3]: rows below it (the point's bottom).
      x.variant = p.variant; x.v[0] = p.baseline; x.v[1] = bar; x.v[2] = std::max<int16_t>(p.top, body.ascent); x.v[3] = sub + point.descent;
      x.v[4] = bar + 2;
      break;
    }
    case Kind::Integral: {
      const Box &body = fb(0), &lower = fb(1), &upper = fb(2);
      const int16_t need = std::max<int16_t>(body.ascent + body.descent - m.descent + 4,
                                             upper.ascent + upper.descent + lower.ascent + lower.descent + 2);
      const uint8_t count = MathFont::variants(0x222B);
      uint8_t v = M::kIntegralMinVariant;
      while (v < count && MathFont::find(0x222B, Style::Normal, v)->height < need) ++v;
      const Glyph& g = *MathFont::find(0x222B, Style::Normal, v);
      const int16_t top = -m.axis - g.height / 2, bottom = top + g.height - 1;
      const int16_t column = g.xOffset + g.width + M::kLimitGap;
      at(2, column, top + upper.ascent);
      at(1, column, bottom - lower.descent + 1);
      const int16_t bodyX = column + std::max(lower.width, upper.width) + 1;
      at(0, bodyX, 0);
      const int16_t dx = bodyX + body.width;
      box = grow({static_cast<int16_t>(dx + glyph('d', s).advance + glyph(kItalicX, s).advance), body.ascent, body.descent},
                 -top, bottom + 1);
      x.variant = v; x.v[0] = top - g.yOffset; x.v[1] = dx;
      break;
    }
    case Kind::Sum: case Kind::Product: {
      const Box &body = fb(0), &lower = fb(1), &upper = fb(2);
      const Glyph& g = glyph(it.kind == Kind::Sum ? 0x2211 : 0x220F, Style::Normal);
      const int16_t signTop = g.yOffset, signBottom = g.yOffset + g.height - 1;
      const int16_t prefix = glyph(kItalicX, Style::Script).advance + glyph('=', Style::Script).advance;
      const int16_t column = std::max<int16_t>(g.advance, std::max<int16_t>(upper.width, prefix + lower.width));
      const int16_t ub = signTop - M::kUpperLimitGapMin - upper.descent;
      const int16_t lb = signBottom + 1 + M::kLowerLimitGapMin + lower.ascent;
      const int16_t lowerX = (column - prefix - lower.width) / 2;
      at(2, (column - upper.width) / 2, ub);
      at(1, lowerX + prefix, lb);
      const Delims p = parens(fieldInk_[fields[0]], s);
      at(0, column + 1 + p.left, 0);
      box = grow(grow({static_cast<int16_t>(column + 1 + p.left + body.width + p.right), body.ascent, body.descent}, p.top, p.bottom),
                 upper.ascent - ub, lb + lower.descent);
      x.variant = p.variant; x.v[0] = (column - g.advance) / 2; x.v[1] = lowerX; x.v[2] = lb; x.v[3] = column + 1; x.v[4] = p.baseline;
      break;
    }
    case Kind::Convert: {
      const Box& o = fb(0);
      const char* label = conversionLabel(src, d, i);
      at(0, 0, 0);
      box = grow({static_cast<int16_t>(o.width + runWidth(label, std::strlen(label), s)), o.ascent, o.descent}, 0, 0);
      x.v[0] = o.width;
      break;
    }
  }
  Rect& ink = itemInk_[i]; ink = {};
  // The same primitives measure decorations without visiting bitmap pixels.
  paintItem(nullptr, i, 0, 0, {}, &ink);
  for (uint16_t f = it.firstField; f != kNone; f = d.field(f).next)
    unite(ink, translated(fieldInk_[f], fieldOffset_[f].x, fieldOffset_[f].y));
  if (ink.height) box = grow(box, -ink.y, ink.y + ink.height);
}

void MathLayout::layout(const EditDocument& document) {
  doc_ = &document;
  const EditDocument& d = document;
  const size_t items = d.items(), fields = d.fields();
  itemBox_.assign(items, {}); itemAt_.assign(items, {}); extra_.assign(items, {});
  itemInk_.assign(items, {}); fieldInk_.assign(fields, {});
  fieldBox_.assign(fields, {}); fieldAt_.assign(fields, {}); fieldOffset_.assign(fields, {});
  fieldStyle_.assign(fields, Style::Normal); visible_.assign(fields, false); pen_.assign(fields, 0);
  // Styles and visibility, top-down: a field is always created after the
  // field its owner sits in. Scripts clamp at the one native script size.
  visible_[0] = true;
  for (size_t f = 1; f < fields; ++f) {
    const EditDocument::Field& fl = d.field(f);
    const EditDocument::Item& owner = d.item(fl.owner);
    bool linked = false;
    for (uint16_t g = owner.firstField; g != kNone; g = d.field(g).next) linked |= g == f;
    visible_[f] = linked && visible_[owner.field];
    const Role r = fl.role;
    const bool script = r == Role::Exponent || r == Role::Index || r == Role::Lower || r == Role::Upper ||
                        r == Role::Point || (r == Role::Base && owner.kind == Kind::Log);
    fieldStyle_[f] = script ? Style::Script : fieldStyle_[owner.field];
  }
  // Measure bottom-up: every item's fields hold only later items.
  for (size_t i = items; i-- > 0;) if (visible_[d.item(i).field]) measure(static_cast<uint16_t>(i));
  fieldBox_[0] = compose(0);
  // Place top-down: an item's fields are positioned before their items.
  for (size_t i = 0; i < items; ++i) {
    const EditDocument::Item& it = d.item(i);
    if (!visible_[it.field]) continue;
    itemAt_[i] = Place{pen_[it.field], fieldAt_[it.field].y};
    pen_[it.field] += itemBox_[i].width;
    for (uint16_t f = it.firstField; f != kNone; f = d.field(f).next) {
      fieldAt_[f] = {static_cast<int16_t>(itemAt_[i].x + fieldOffset_[f].x), static_cast<int16_t>(itemAt_[i].y + fieldOffset_[f].y)};
      pen_[f] = fieldAt_[f].x;
    }
  }
  // One slot per stop, written by the field that owns it.
  slots_.assign(d.source().size() + 1, {});
  for (size_t f = 0; f < fields; ++f) {
    if (!visible_[f]) continue;
    const EditDocument::Field& fl = d.field(f);
    const Style s = fieldStyle_[f];
    if (d.slotField(fl.begin) == f) slots_[fl.begin] = {fieldAt_[f].x, fieldAt_[f].y, s};
    for (uint16_t i = fl.first; i != kNone; i = d.item(i).next)
      if (d.slotField(d.item(i).end) == f)
        slots_[d.item(i).end] = {static_cast<int16_t>(itemAt_[i].x + itemBox_[i].width), fieldAt_[f].y, s};
  }
}

MathLayout::Rect MathLayout::caret(size_t offset) const {
  const Slot& s = slots_[offset];
  const M::Style& m = metrics(s.style);
  const uint16_t field = doc_->slotField(offset);
  const Box& fieldBox = fieldBox_[field];
  // The caret follows the line it sits on, not the whole field union: after
  // a digit next to a tall nested fraction it shrinks to the digit's height,
  // while after the tall structure itself it stays tall (photo 1 stays big).
  // An empty field has no neighbours and keeps the field box (its minima).
  const uint16_t prev = doc_->itemEndingAt(offset, field);
  const uint16_t next = doc_->itemStartingAt(offset, field);
  const Box* line = &fieldBox;
  if (prev != kNone && next != kNone) {
    // Between two items the digit/line height wins so a tall sibling never
    // keeps the caret big once a number is beside it.
    const Box& a = itemBox_[prev];
    const Box& b = itemBox_[next];
    line = (a.ascent + a.descent <= b.ascent + b.descent) ? &a : &b;
  } else if (prev != kNone) {
    line = &itemBox_[prev];
  } else if (next != kNone) {
    line = &itemBox_[next];
  }
  const int16_t ascent = std::max<int16_t>(line->ascent, m.caretMinAscent);
  const int16_t descent = std::max<int16_t>(line->descent, m.caretMinDescent);
  return {static_cast<int16_t>(s.x + m.caretLeft), static_cast<int16_t>(s.baseline - ascent), m.caretWidth, static_cast<int16_t>(ascent + descent)};
}

MathLayout::Rect MathLayout::itemInk(uint16_t i) const { return translated(itemInk_[i], itemAt_[i].x, itemAt_[i].y); }
MathLayout::Rect MathLayout::fieldInk(uint16_t f) const { return translated(fieldInk_[f], fieldAt_[f].x, fieldAt_[f].y); }

size_t MathLayout::nearest(uint16_t field, int16_t x) const {
  const EditDocument& d = *doc_;
  const EditDocument::Field& fl = d.field(field);
  size_t best = fl.begin;
  auto consider = [&](size_t o) {
    if (d.slotField(o) == field && std::abs(slots_[o].x - x) < std::abs(slots_[best].x - x)) best = o;
  };
  for (uint16_t i = fl.first; i != kNone; i = d.item(i).next) consider(d.item(i).end);
  return best;
}

void MathLayout::paint(GFXcanvas1& canvas, int16_t dx, int16_t dy, const Rect& clip) const {
  paintOn(canvas, dx, dy, clip, {kNone, 1, 1});
}
void MathLayout::paint(GrayCanvas& canvas, int16_t dx, int16_t dy, const Rect& clip) const {
  paint(canvas, dx, dy, clip, PaintStyle{});
}
void MathLayout::paint(GrayCanvas& canvas, int16_t dx, int16_t dy, const Rect& clip, PaintStyle style) const {
  paintOn(canvas, dx, dy, clip, style);
}
uint8_t MathLayout::fieldLevel(uint16_t field, PaintStyle style) const {
  if (style.activeField == kNone || style.activeField == doc_->root()) return style.activeLevel;
  // Structural ancestry, including every descendant of the active field.
  while (field != kNone) {
    if (field == style.activeField) return style.activeLevel;
    const uint16_t owner = doc_->field(field).owner;
    field = owner == kNone ? kNone : doc_->item(owner).field;
  }
  return style.otherLevel;
}
void MathLayout::paintOn(Adafruit_GFX& canvas, int16_t dx, int16_t dy, const Rect& clip, PaintStyle style) const {
  const EditDocument& d = *doc_;
  for (size_t f = 1; f < d.fields(); ++f)
    if (visible_[f] && d.field(f).first == kNone) {
      const Painter p{&canvas, clip, nullptr, fieldLevel(static_cast<uint16_t>(f), style)};
      const Rect r = translated(emptyFieldInk(fieldStyle_[f]), fieldAt_[f].x + dx, fieldAt_[f].y + dy);
      p.hline(r.x, r.y, r.width); p.hline(r.x, r.y + r.height - 1, r.width);
      p.vline(r.x, r.y, r.height); p.vline(r.x + r.width - 1, r.y, r.height);
      // Normal boxes read 2px: one more round just inside the outer outline.
      // Outer size is unchanged so metrics/advances never move. Small script
      // cells stay 1px.
      if (fieldStyle_[f] == Style::Normal && r.width >= 3 && r.height >= 3) {
        p.hline(r.x + 1, r.y + 1, r.width - 2); p.hline(r.x + 1, r.y + r.height - 2, r.width - 2);
        p.vline(r.x + 1, r.y + 1, r.height - 2); p.vline(r.x + r.width - 2, r.y + 1, r.height - 2);
      }
    }
  for (size_t i = 0; i < d.items(); ++i)
    if (visible_[d.item(i).field]) paintItem(&canvas, static_cast<uint16_t>(i), dx, dy, clip, nullptr, fieldLevel(d.item(i).field, style));
}

void MathLayout::paintItem(Adafruit_GFX* canvas, uint16_t i, int16_t dx, int16_t dy, const Rect& clip, Rect* ink, uint8_t level) const {
  const EditDocument& d = *doc_;
  const EditDocument::Item& it = d.item(i);
  const std::string& src = d.source();
  const Style s = fieldStyle_[it.field];
  const Extra& x = extra_[i];
  const Box& box = itemBox_[i];
  const Painter p{canvas, clip, ink, level};
  const int X = ink ? 0 : itemAt_[i].x + dx, Y = ink ? 0 : itemAt_[i].y + dy;
  auto fieldX = [&](uint16_t f) { return ink ? fieldOffset_[f].x : fieldAt_[f].x + dx; };
  auto fieldY = [&](uint16_t f) { return ink ? fieldOffset_[f].y : fieldAt_[f].y + dy; };
  auto parensAround = [&](int left, int right) {
    p.draw(paren('(', x.variant, s), left, Y + x.v[0]);
    if (it.closed) p.draw(paren(')', x.variant, s), right, Y + x.v[0]);
  };
  auto fieldEnd = [&](int k) { const uint16_t f = d.fieldAt(i, k); return fieldX(f) + fieldBox_[f].width; };
  switch (it.kind) {
    case Kind::Glyph: {
      const unsigned char c = static_cast<unsigned char>(src[it.begin]);
      if (isPrimeMinute(c) || isPrimeSecond(c))
        paintPrime(p, c, X, Y, s);
      else if (EngSymbol::is(static_cast<char>(c)))
        p.bits(kEngineering[EngSymbol::index(static_cast<char>(c))], X, Y);
      else if ((c == 'P' || c == 'C') && combOperatorBold(d, i)) {
        // Fake-bold double-strike; advance unchanged so metrics/caret stay put.
        const Glyph& g = glyph(glyphCode(c), s);
        p.draw(g, X, Y);
        p.draw(g, X + 1, Y);
      } else
        p.draw(glyph(glyphCode(c), s), X, Y);
      break;
    }
    case Kind::Name: {
      const uint32_t symbol = nameSymbol(src, it.begin, it.end);
      if (symbol) p.draw(glyph(symbol, s), X, Y);
      else p.run(src.c_str() + it.begin, it.end - it.begin, s, X, Y);
      break;
    }
    case Kind::Constant: {
      const char* label = constantLabel(src, it);
      p.run(label, std::strlen(label), s, X, Y);
      break;
    }
    case Kind::Group:
      parensAround(X, fieldEnd(0));
      break;
    case Kind::Recurring:
      p.hline(X, Y + x.v[0], box.width);
      break;
    case Kind::Call: {
      const CallName label = callName(src, it);
      const int pen = p.run(label.text, label.length, s, X, Y);
      if (label.inverse) p.bits(inverseBits(s), pen, Y);
      const uint16_t last = d.lastField(i);
      for (uint16_t f = it.firstField; f != last; f = d.field(f).next)
        p.draw(glyph(',', s), fieldX(f) + fieldBox_[f].width, Y);
      parensAround(X + x.v[1], fieldEnd(it.fieldCount - 1));
      break;
    }
    case Kind::Power: case Kind::Script:
      break;  // their fields draw themselves
    case Kind::Fraction: case Kind::Mixed:
      for (int r = 0; r < M::kFractionRuleThickness; ++r) p.hline(X + x.v[1] + M::kFractionRuleInsetLeft, Y + x.v[0] + r, x.v[2] - M::kFractionRuleInsetLeft);
      break;
    case Kind::Sqrt: case Kind::Root: {
      p.draw(*MathFont::find(0x221A, Style::Normal, x.variant), X + x.v[0], Y + x.v[1]);
      for (int r = 0; r < M::kRadicalRuleThickness; ++r) p.hline(X + x.v[2], Y + x.v[3] + r, box.width - x.v[2]);
      break;
    }
    case Kind::Abs:
      p.vline(X + 1, Y + x.v[0], x.v[1] - x.v[0] + 1);
      p.vline(X + box.width - 2, Y + x.v[0], x.v[1] - x.v[0] + 1);
      break;
    case Kind::Log:
      p.run("log", 3, s, X, Y);
      parensAround(X + x.v[1], fieldEnd(1));
      break;
    case Kind::Diff: {
      const DiffOp op = diffOp(s);
      const int d0 = glyph('d', s).advance;
      p.draw(glyph('d', s), X + (op.width - d0) / 2, Y + op.numerator);
      const int dxPen = X + M::kFractionBoxPad / 2;
      p.draw(glyph('d', s), dxPen, Y + op.denominator);
      p.draw(glyph(kItalicX, s), dxPen + d0, Y + op.denominator);
      p.hline(X, Y + op.bar, op.width);
      parensAround(X + op.width, fieldEnd(0));
      p.vline(X + x.v[1], Y - x.v[2], x.v[2] + x.v[3]);
      const Glyph& xs = glyph(kItalicX, Style::Script);
      const int sub = fieldY(d.fieldAt(i, 1));
      p.draw(xs, X + x.v[4], sub);
      p.draw(glyph('=', Style::Script), X + x.v[4] + xs.advance, sub);
      break;
    }
    case Kind::Integral:
      p.draw(*MathFont::find(0x222B, Style::Normal, x.variant), X, Y + x.v[0]);
      p.draw(glyph('d', s), X + x.v[1], Y);
      p.draw(glyph(kItalicX, s), X + x.v[1] + glyph('d', s).advance, Y);
      break;
    case Kind::Sum: case Kind::Product: {
      p.draw(glyph(it.kind == Kind::Sum ? 0x2211 : 0x220F, Style::Normal), X + x.v[0], Y);
      const Glyph& xs = glyph(kItalicX, Style::Script);
      p.draw(xs, X + x.v[1], Y + x.v[2]);
      p.draw(glyph('=', Style::Script), X + x.v[1] + xs.advance, Y + x.v[2]);
      p.draw(paren('(', x.variant, s), X + x.v[3], Y + x.v[4]);
      if (it.closed) p.draw(paren(')', x.variant, s), fieldEnd(0), Y + x.v[4]);
      break;
    }
    case Kind::Convert: {
      const char* label = conversionLabel(src, d, i);
      p.run(label, std::strlen(label), s, X + x.v[0], Y);
      break;
    }
  }
}

// P12: the 0x1C/0x1E/0x1F display-document bytes never reach labels (they
// originate only in result presentation, which lays out through MathLayout,
// not paintLabel). An unbaked byte here falls back to '?' by design.
int16_t labelAdvance(const char* text, MathFont::Style style) {
  int16_t w = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    w += glyph(glyphCode(*p), style).advance;
  return w;
}
int16_t paintLabel(GFXcanvas1& canvas, const char* text, int16_t x, int16_t baseline, MathFont::Style style,
                   uint16_t ink) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    const Glyph& g = glyph(glyphCode(*p), style);
    MathFont::draw(canvas, g, x, baseline, ink);
    x = static_cast<int16_t>(x + g.advance);
  }
  return x;
}
}  // namespace Calculator
