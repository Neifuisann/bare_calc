#pragma once
#include "calculator/math/calculator_engine.h"
#include "calculator/runtime/calculator_session.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Calculator {
namespace Presentation {

// Typed result presentation: Value + Settings + Format -> Presentation ->
// template source / linear text -> EditDocument / MathLayout -> GrayCanvas.
//
// Output-only semantic structure. Never fed to parse()/evaluate(). The
// formatter emits semantic nodes; the renderer does not rediscover structure
// by parsing display text (R-04).
//
// Flat arena: nodes are plain integers in one vector, digit text lives in one
// shared pool. No per-node std::string/vector, so moves are memcpys and the
// transient heap stays ~one small vector plus one short string. nodes[0] is
// always the root Sequence; structure comes from parent links (each node's
// parent index), so nested descendants never disturb sibling order. Renderers
// traverse children by scanning for parent links; presentations stay tiny
// (tens of nodes), keeping the walk trivial.

enum class Kind : uint8_t {
  Digits,        // [0-9]+ only, no structure (text pool slice)
  Sign,          // "+" / "-" (text pool slice, length 1)
  Point,         // decimal mark (locale applied at lowering)
  Fraction,      // children: numerator seq, denominator seq
  Mixed,         // children: whole seq, numerator seq, denominator seq
  Radical,       // a=coeff, b=radicand
  Pi,            // a=num, b=den (rational * pi)
  TimesTenPower,  // child: mantissa seq; a=exponent
  Recurring,     // children: prefix seq, repeat-digits seq
  Degree,        // a=degrees
  Minute,        // a=minutes
  Second,        // child: seconds decimal seq
  Imaginary,     // marker emitting 'i'; the coefficient run precedes it inline
  Angle,         // polar '@' marker (no payload)
  Separator,     // multi-value separator (locale applied at lowering)
  Text,          // True/False, "*", parens and other unstructured words
  Sequence,      // ordered children
  Superscript,   // children: base seq, exponent seq (prime powers)
  Euler,         // a=num, b=den (rational * e), lowered like Pi
};

struct Node {
  Kind kind = Kind::Sequence;
  int64_t a = 0, b = 0;
  uint32_t text = 0xFFFFFFFFu;    // offset into Output::text, or kNoIndex
  uint32_t len = 0;
  uint32_t parent = 0xFFFFFFFFu;  // parent node index, or kNoIndex for root
};

struct Output {
  std::vector<Node> nodes;  // nodes[0] is the root Sequence
  std::string text;         // shared pool for Digits/Sign/Text payloads
  // Open-composite stack for builders (max nesting is root/frac-or-mixed/seq
  // plus one symbol level; 8 is ample headroom, guarded at push).
  uint32_t parents[8] = {};
  uint32_t depth = 0;
};

constexpr uint32_t kNoIndex = 0xFFFFFFFFu;
// True when the presentation holds no runs (fresh root only).
inline bool empty(const Output& o) { return o.nodes.size() <= 1; }

// Display-document private bytes (never canonical editor source, never
// evaluated). EditDocument treats them as single Glyphs, so they never split
// frac()/mixed() fields. Layout maps them to the right ink:
// - kCommaPoint (0x1C) draws as ',' (comma decimal mark inside templates)
// - kPrimeMinute (0x1E) / kPrimeSecond (0x1F) draw as prime ticks (R-03)
// Degree reuses the baked single-byte 0xB0. Dot '.', ';', digits, letters,
// '+', '-', '@' are safe everywhere and emitted directly.
constexpr char kCommaPoint = '\x1C';
constexpr char kPrimeMinute = '\x1E';
constexpr char kPrimeSecond = '\x1F';

// Ten manual FORMAT items in fx-880 order (R-05 §26).
struct Capabilities {
  bool standard = true;
  bool decimal = false;
  bool primeFactor = false;
  bool recurringDecimal = false;
  bool rectangular = false;
  bool polar = false;
  bool improper = false;
  bool mixed = false;
  bool engineering = false;
  bool sexagesimal = false;
  bool anyAlternative() const {
    return decimal || primeFactor || recurringDecimal || rectangular || polar ||
           improper || mixed || engineering || sexagesimal;
  }
};

Capabilities capabilitiesForSummary(const ResultSummary& summary, const Settings& settings);
Capabilities capabilitiesForResult(const Result& result, const Settings& settings);

// Value -> typed presentation (no pretty-string reparse for exact forms).
// Decimal-family digit strings come from the engine's Decimal formatting
// (R-06 precision preserved); exact forms come directly from Exact.
Output presentValue(const Value& value, const Settings& settings, Format format);
Output presentSummary(const ResultSummary& summary, const Settings& settings, Format format);
// Complex parts: rectangular (real +/- imag i) or polar (r @ theta).
Output presentComplex(const Value& re, const Value& im, const Settings& settings, Format format);

// Semantic lowering (recursive over child indices, no heuristic reparse).
// toTemplateSource: canonical template grammar for EditDocument/MathLayout.
// Locale Point inside templates uses kCommaPoint (safe); top-level pair
// separators use ','/';' directly (pairs never nest inside frac fields).
// Degree uses 0xB0; minute/second use kPrimeMinute/kPrimeSecond (layout draws
// distinct prime ticks; OTF has no U+2032/U+2033 -- verified).
std::string toTemplateSource(const Output& presentation, const Settings& settings);
// toLinearText: UTF-8 display text for linear consumers and snapshots.
// Uses U+00B0 degree, U+2032 prime, U+2033 double-prime, locale Point and
// R-02 separators. Built-in-font linear rendering of primes is a known
// fallback limitation (see render path).
std::string toLinearText(const Output& presentation, const Settings& settings);

// sizeof reporting for the firmware budget (arena node: no heap members).
constexpr size_t kNodeSize = sizeof(Node);

}  // namespace Presentation
}  // namespace Calculator
