#include "calculator/cas/calculator_cas_expr.h"
#include <algorithm>

namespace Calculator {
namespace Cas {
namespace {
Context* active = nullptr;
Context& fallback() {
  static Context permissive({}, {}, UINT32_MAX);
  return permissive;
}
uint32_t mix(uint32_t h, uint32_t v) { return h ^ (v + 0x9e3779b9u + (h << 6) + (h >> 2)); }
uint32_t textHash(const std::string& s) {
  uint32_t h = 2166136261u;
  for (char c : s) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
  return h;
}
// Equal exact numbers have equal canonical forms, so either path hashes a
// given value the same way.
uint32_t numberHash(const ExactNumber& n) {
  if (n.isRational()) {
    const Rational& r = n.rationalValue();
    if (r.num().fitsInt64() && r.den().fitsInt64()) {
      const uint64_t a = static_cast<uint64_t>(r.num().toInt64()), b = static_cast<uint64_t>(r.den().toInt64());
      return mix(mix(static_cast<uint32_t>(a), static_cast<uint32_t>(a >> 32)), static_cast<uint32_t>(b));
    }
  }
  return textHash(n.str());
}
// Rationals first by value; anything else by canonical text. Only a total
// order is needed, and text is cheap where a sign comparison is not.
int numberOrder(const ExactNumber& a, const ExactNumber& b) {
  if (a.isRational() && b.isRational()) return Rational::compare(a.rationalValue(), b.rationalValue());
  if (a.isRational() != b.isRational()) return a.isRational() ? -1 : 1;
  const int c = a.str().compare(b.str());
  return (c > 0) - (c < 0);
}
}  // namespace

const char* fnName(Fn fn) {
  static const char* const names[] = {"sin", "cos", "tan", "asin", "acos", "atan", "sinh",
                                      "cosh", "tanh", "asinh", "acosh", "atanh", "ln", "abs"};
  return names[static_cast<unsigned>(fn)];
}

Context::Context(std::function<bool()> cancelled, std::function<void()> cooperate, uint32_t steps)
    : cancelled_(std::move(cancelled)), cooperate_(std::move(cooperate)), budget_(steps) {}

bool Context::step() {
  if (error_ != Error::None) return false;
  if (++steps_ > budget_) { error_ = Error::TimeOut; return false; }
  if ((steps_ & 63u) == 0) {
    if (cooperate_) cooperate_();
    if (cancelled_ && cancelled_()) { error_ = Error::Cancelled; return false; }
  }
  return true;
}

Context& context() { return active ? *active : fallback(); }
Scope::Scope(Context& c) : previous_(active) { active = &c; }
Scope::~Scope() { active = previous_; }

Expr finish(Node&& n) {
  uint32_t size = 1, h = mix(static_cast<uint32_t>(n.kind) * 131u + static_cast<uint32_t>(n.fn),
                             static_cast<uint32_t>(n.wild) << 8 | n.constraint);
  uint32_t depth = 1;
  if (n.kind == Kind::Number) h = mix(h, numberHash(n.number));
  if (!n.name.empty()) h = mix(h, textHash(n.name));
  for (const auto& a : n.args) {
    size += a->size;
    depth = std::max<uint32_t>(depth, a->depth + 1u);
    h = mix(h, a->hash);
  }
  n.size = size;
  n.depth = static_cast<uint16_t>(std::min<uint32_t>(depth, 0xFFFF));
  n.hash = h;
  if (size > kMaxNodes || depth > kMaxDepth) context().fail(Error::Stack);
  return std::allocate_shared<Node>(ExactAllocator<Node>(), std::move(n));
}

bool equal(const Expr& a, const Expr& b) {
  if (a == b) return true;
  if (!a || !b) return false;
  if (a->hash != b->hash || a->kind != b->kind || a->size != b->size || a->fn != b->fn || a->wild != b->wild ||
      a->args.size() != b->args.size() || a->name != b->name)
    return false;
  if (a->kind == Kind::Number && a->number != b->number) return false;
  for (size_t i = 0; i < a->args.size(); ++i)
    if (!equal(a->args[i], b->args[i])) return false;
  return true;
}

int compare(const Expr& a, const Expr& b) {
  if (a == b) return 0;
  if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
  switch (a->kind) {
    case Kind::Number: return numberOrder(a->number, b->number);
    case Kind::Symbol:
    case Kind::Text: {
      const int c = a->name.compare(b->name);
      return (c > 0) - (c < 0);
    }
    case Kind::Wild: return a->wild == b->wild ? 0 : a->wild < b->wild ? -1 : 1;
    case Kind::Func:
      if (a->fn != b->fn) return a->fn < b->fn ? -1 : 1;
      break;
    default: break;
  }
  const size_t n = std::min(a->args.size(), b->args.size());
  for (size_t i = 0; i < n; ++i)
    if (const int c = compare(a->args[i], b->args[i])) return c;
  if (a->args.size() != b->args.size()) return a->args.size() < b->args.size() ? -1 : 1;
  return 0;
}

bool freeOf(const Expr& e, const Expr& target) {
  if (e->size < target->size) return true;
  if (equal(e, target)) return false;
  for (const auto& a : e->args)
    if (!freeOf(a, target)) return false;
  return true;
}

bool hasSymbols(const Expr& e) {
  if (e->kind == Kind::Symbol) return true;
  for (const auto& a : e->args)
    if (hasSymbols(a)) return true;
  return false;
}

void collectSymbols(const Expr& e, std::vector<std::string>& names) {
  if (e->kind == Kind::Symbol) {
    if (std::find(names.begin(), names.end(), e->name) == names.end()) names.push_back(e->name);
    return;
  }
  for (const auto& a : e->args) collectSymbols(a, names);
}

}  // namespace Cas
}  // namespace Calculator
