#include "calculator/runtime/calculator_store.h"
#include "calculator/ui/calculator_editor.h"
#include "calculator/runtime/calculator_platform.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <memory>

namespace Calculator {
namespace {
constexpr const char* kSpaceV1 = "calc_v1";
constexpr const char* kSpaceV2 = "calc_v2";
constexpr const char* kKey = "state";
// v4 stores each value's exact form as its canonical text; v2/v3 stored at
// most two int64 display terms (still read, converted).
constexpr uint8_t kVersion = 4;
constexpr size_t kHeader = 11;  // magic 4, version 1, length 2, CRC 4
constexpr size_t kDigitsLimit = 64;
// A longer exact form (a huge integer, a long radical sum) is stored as
// decimal only: dropping exactness is safe, truncating its text is not.
constexpr size_t kExactTextLimit = 1024;
constexpr int64_t kExactLimit = 1000000000000000LL;

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
bool exactInRange(const Exact& e) {
  return e.numerator >= -kExactLimit && e.numerator <= kExactLimit && e.denominator > 0 &&
         e.denominator <= kExactLimit && e.radicand > 0 && e.radicand <= kExactLimit;
}

struct Writer {
  std::vector<uint8_t> bytes;
  void u8(uint8_t v) { bytes.push_back(v); }
  void u16(uint16_t v) { u8(static_cast<uint8_t>(v)); u8(static_cast<uint8_t>(v >> 8)); }
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>(v >> (8 * i))); }
  void i64(int64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<uint8_t>(static_cast<uint64_t>(v) >> (8 * i))); }
  void text(const std::string& s, size_t limit) {
    const size_t n = std::min(s.size(), limit);
    u16(static_cast<uint16_t>(n));
    bytes.insert(bytes.end(), s.begin(), s.begin() + static_cast<long>(n));
  }
  void value(const Value& v) {
    text(v.number.str(40), kDigitsLimit);
    const std::string form = v.symbolic.exact() ? v.symbolic.str() : std::string();
    text(form.size() <= kExactTextLimit ? form : std::string(), kExactTextLimit);
  }
  void scalar(const ScalarValue& v) { u8(v.complex ? 1 : 0); value(v.re); if (v.complex) value(v.im); }
  void aggregate(const Aggregate& a) {
    u8(a.rows);
    // Runtime retention undefines an answer by clearing rows alone. Do not
    // persist its leftover width as an invalid zero-by-N matrix.
    u8(a.defined() ? a.cols : 0);
    for (int r = 0; r < a.rows; ++r)
      for (int c = 0; c < a.cols; ++c) text(a.at(r, c).str(40), kDigitsLimit);
  }
};

// Every read past the end, oversize field or out-of-range value clears `ok`;
// the caller checks once at the end.
struct Reader {
  const uint8_t* at;
  const uint8_t* end;
  uint8_t version = kVersion;
  bool ok = true;
  bool done() const { return ok && at == end; }
  uint8_t u8() { if (at >= end) { ok = false; return 0; } return *at++; }
  uint16_t u16() { const uint16_t low = u8(); return static_cast<uint16_t>(low | u8() << 8); }
  int64_t i64() {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(u8()) << (8 * i);
    return static_cast<int64_t>(v);
  }
  std::string text(size_t limit) {
    const size_t n = u16();
    if (!ok || n > limit || static_cast<size_t>(end - at) < n) { ok = false; return {}; }
    std::string s(reinterpret_cast<const char*>(at), n);
    at += n;
    return s;
  }
  Exact exact() {
    const uint8_t flags = u8();
    Exact e;
    if (flags & ~3u) ok = false;
    if (!(flags & 1)) return e;
    e.valid = true;
    e.pi = flags & 2;
    e.numerator = i64();
    e.denominator = i64();
    e.radicand = i64();
    if (!exactInRange(e)) ok = false;
    return e;
  }
  Value value() {
    const std::string digits = text(kDigitsLimit);
    Value v;
    if (digits.empty()) ok = false;
    else v.number = Decimal(digits);
    if (!v.number.finite()) ok = false;
    if (version >= 4) {
      const std::string form = text(kExactTextLimit);
      if (!form.empty()) {
        v.symbolic = ExactNumber::parse(form);
        if (!v.symbolic.exact()) ok = false;  // our own canonical text always parses
      }
      return v;
    }
    const Exact first = exact(), second = exact();
    if (first.valid) v.symbolic = second.valid ? exactNumber(first) + exactNumber(second) : exactNumber(first);
    return v;
  }
  ScalarValue scalar() {
    const uint8_t tag = u8();
    if (tag > 1) ok = false;
    const Value re = value();
    if (!tag) return ScalarValue(re);
    return ScalarValue::of(re, value());
  }
  void aggregate(Aggregate& a) {
    a.rows = u8();
    a.cols = u8();
    if (a.rows > 4 || a.cols > 4 || (a.rows != 0 && a.cols == 0)) { ok = false; a.rows = a.cols = 0; }
    // Older writers saved a stale width after an app switch cleared rows.
    // Such an undefined slot has no cells; recover the rest of that record.
    if (a.rows == 0) a.cols = 0;
    for (int r = 0; r < a.rows && ok; ++r)
      for (int c = 0; c < a.cols && ok; ++c) {
        a.at(r, c) = Decimal(text(kDigitsLimit));
        if (!a.at(r, c).finite()) ok = false;
      }
  }
};

void writeValue(JsonObject object, const Value& v);
Value readValue(JsonObjectConst object) {
  Value v(Decimal(object["number"] | "0"));
  if (!v.number.finite()) return Value(0);
  if (object["n"].is<int64_t>() && object["d"].is<int64_t>() && object["r"].is<int64_t>()) {
    const Exact e{object["n"].as<int64_t>(), object["d"].as<int64_t>(), object["r"].as<int64_t>(),
                  object["pi"] | false, true};
    if (exactInRange(e)) v.symbolic = exactNumber(exactRational(e.numerator, e.denominator, e.radicand, e.pi));
  }
  return v;
}
}  // namespace

std::vector<uint8_t> encodeV2(const Settings& settings, const Memory& memory, const Aggregates* aggregates) {
  Writer payload;
  payload.u8(static_cast<uint8_t>(settings.angle));
  payload.u8(static_cast<uint8_t>(settings.io));
  payload.u8(static_cast<uint8_t>(settings.number));
  payload.u8(settings.digits);
  payload.u8(static_cast<uint8_t>(settings.complexResult));
  payload.u8(static_cast<uint8_t>((settings.engineer ? 1 : 0) | (settings.mixed ? 2 : 0) | (settings.comma ? 4 : 0) |
                                  (settings.separators ? 8 : 0) | (settings.formatMenu ? 16 : 0) |
                                  (settings.scientificPower ? 32 : 0)));
  payload.scalar(memory.ans);
  payload.value(memory.preAns);
  for (const auto& v : memory.variables) payload.scalar(v);
  payload.text(memory.f, MathEditor::kLimit);
  payload.text(memory.g, MathEditor::kLimit);
  for (int slot = 0; slot < 10; ++slot) {
    if (aggregates) payload.aggregate(slot < 5 ? aggregates->matrices[slot] : aggregates->vectors[slot - 5]);
    else { payload.u8(0); payload.u8(0); }
  }
  Writer record;
  for (const char c : {'C', 'A', 'L', 'C'}) record.u8(static_cast<uint8_t>(c));
  record.u8(kVersion);
  record.u16(static_cast<uint16_t>(payload.bytes.size()));
  record.u32(crc32(payload.bytes.data(), payload.bytes.size()));
  record.bytes.insert(record.bytes.end(), payload.bytes.begin(), payload.bytes.end());
  return record.bytes;
}

bool decodeV2(const std::vector<uint8_t>& record, Settings& settings, Memory& memory, Aggregates* aggregates) {
  if (record.size() < kHeader || std::memcmp(record.data(), "CALC", 4) != 0 || record[4] < 2 || record[4] > kVersion)
    return false;
  const size_t length = static_cast<size_t>(record[5] | record[6] << 8);
  if (record.size() != kHeader + length) return false;
  const uint32_t crc = static_cast<uint32_t>(record[7]) | static_cast<uint32_t>(record[8]) << 8 |
                       static_cast<uint32_t>(record[9]) << 16 | static_cast<uint32_t>(record[10]) << 24;
  if (crc != crc32(record.data() + kHeader, length)) return false;
  Reader in{record.data() + kHeader, record.data() + record.size(), record[4]};
  Settings s;
  auto heap = std::make_unique<Memory>();  // off the loop task's 8 KB stack
  Memory& m = *heap;
  const uint8_t angle = in.u8(), io = in.u8(), number = in.u8(), digits = in.u8(), complex = in.u8(), flags = in.u8();
  if (angle > 2 || io > 3 || number > 3 || digits > 10 || complex > 1 || flags > 63) return false;
  s.angle = static_cast<Angle>(angle);
  s.io = static_cast<IO>(io);
  s.number = static_cast<NumberFormat>(number);
  s.digits = digits;
  if (s.number == NumberFormat::Sci && !s.digits) s.digits = 1;
  s.complexResult = static_cast<ComplexResult>(complex);
  s.engineer = flags & 1;
  s.mixed = flags & 2;
  s.comma = flags & 4;
  s.separators = flags & 8;
  s.formatMenu = flags & 16;
  s.scientificPower = flags & 32;
  m.ans = in.scalar();
  m.preAns = in.value();
  for (auto& v : m.variables) v = in.scalar();
  m.f = in.text(MathEditor::kLimit);
  m.g = in.text(MathEditor::kLimit);
  auto slots = std::make_unique<Aggregates>();  // 6 KB: heap, like the Memory
  if (record[4] >= 3)
    for (int slot = 0; slot < 10; ++slot) in.aggregate(slot < 5 ? slots->matrices[slot] : slots->vectors[slot - 5]);
  if (!in.done()) return false;
  settings = s;
  memory = std::move(m);
  if (aggregates) *aggregates = *slots;
  return true;
}

bool decodeV1(const std::string& json, Settings& settings, Memory& memory) {
  JsonDocument doc;
  if (json.size() > 8192 || deserializeJson(doc, json) || doc["version"] != 1) return false;
  unsigned a = doc["angle"] | 0u, io = doc["io"] | 0u, n = doc["number"] | 0u, d = doc["digits"] | 3u;
  if (a > 2 || io > 3 || n > 3 || d > 10) return false;
  Settings s;
  auto heap = std::make_unique<Memory>();  // off the loop task's 8 KB stack
  Memory& m = *heap;
  s.angle = static_cast<Angle>(a);
  s.io = static_cast<IO>(io);
  s.number = static_cast<NumberFormat>(n);
  s.digits = static_cast<uint8_t>(d);
  // Sci needs at least one significant digit; without this clamp a stored
  // Sci+0 state leaves the Digits option page with no radio match.
  if (s.number == NumberFormat::Sci && !s.digits) s.digits = 1;
  s.engineer = doc["engineer"] | false;
  s.mixed = doc["mixed"] | false;
  s.comma = doc["comma"] | false;
  s.separators = doc["separators"] | false;
  // v1 stored the Complex Result row as a bool: true was the second choice.
  s.complexResult = (doc["complex"] | false) ? ComplexResult::Polar : ComplexResult::Rectangular;
  s.formatMenu = doc["formatMenu"] | false;
  s.scientificPower = doc["scientificPower"] | true;
  m.ans = readValue(doc["ans"]);
  m.preAns = readValue(doc["preAns"]);
  auto vars = doc["vars"].as<JsonArrayConst>();
  for (size_t i = 0; i < 9 && i < vars.size(); ++i) m.variables[i] = readValue(vars[i]);
  m.f = doc["f"] | "";
  m.g = doc["g"] | "";
  if (m.f.size() > MathEditor::kLimit) m.f.clear();
  if (m.g.size() > MathEditor::kLimit) m.g.clear();
  settings = s;
  memory = std::move(m);
  return true;
}

void loadStoredState(CalculatorSession& session) {
  Settings settings;
  // Heap, not stack: this runs on the loop task, and each decoder holds a
  // second Memory while it parses.
  auto heap = std::make_unique<Memory>();
  Memory& memory = *heap;
  auto aggregates = std::make_unique<Aggregates>();
  std::vector<uint8_t> record;
  std::string json;
  if (CalculatorPlatform::loadBlob(kSpaceV2, kKey, record)) {
    if (decodeV2(record, settings, memory, aggregates.get())) {
      session.settings = settings;
      session.memory = std::move(memory);
      session.aggregates = *aggregates;
      return;
    }
    // A device fault, not a math error: say so and fall back to v1/defaults.
    CalculatorPlatform::logf("[CALC_STORE] v2 record rejected (%u bytes)\n", static_cast<unsigned>(record.size()));
  }
  if (CalculatorPlatform::loadString(kSpaceV1, kKey, json) && decodeV1(json, settings, memory)) {
    session.settings = settings;
    session.memory = std::move(memory);
  }
}

bool saveStoredState(const CalculatorSession& session) {
  return CalculatorPlatform::saveBlob(kSpaceV2, kKey, encodeV2(session.settings, session.memory, &session.aggregates));
}

}  // namespace Calculator
