#include "calculator/cas/calculator_cas_pattern.h"

namespace Calculator {
namespace Cas {
namespace {
bool admissible(const Node& w, const Expr& s, const Expr& var) {
  switch (static_cast<Constraint>(w.constraint)) {
    case Constraint::Any: return true;
    case Constraint::Const: return !var || freeOf(s, var);
    case Constraint::Number: return isNumber(s);
    case Constraint::Integer: return isNumber(s) && s->number.isInteger();
    case Constraint::Linear: {
      Expr a, b;
      return var && linear(s, var, a, b);
    }
  }
  return false;
}
bool bindWild(const Expr& w, const Expr& s, Bindings& b) {
  Expr& slot = b.slot[w->wild];
  if (slot) return equal(slot, s);
  if (!admissible(*w, s, b.var)) return false;
  slot = s;
  return true;
}
Expr combine(Kind k, Args args) {
  if (args.empty()) return integer(k == Kind::Add ? 0 : 1);
  return k == Kind::Add ? add(std::move(args)) : mul(std::move(args));
}
bool matchFixed(const Args& fixed, size_t i, const Args& subject, std::vector<bool>& used, const Expr& rest, Kind k,
                Bindings& b) {
  if (i == fixed.size()) {
    Args left;
    for (size_t j = 0; j < subject.size(); ++j)
      if (!used[j]) left.push_back(subject[j]);
    if (rest) return bindWild(rest, combine(k, std::move(left)), b);
    return left.empty();
  }
  for (size_t j = 0; j < subject.size(); ++j) {
    if (used[j]) continue;
    const Bindings saved = b;
    used[j] = true;
    if (match(fixed[i], subject[j], b) && matchFixed(fixed, i + 1, subject, used, rest, k, b)) return true;
    used[j] = false;
    b = saved;
  }
  return false;
}
bool matchCommutative(const Expr& p, const Expr& s, Bindings& b) {
  const Kind k = p->kind;
  Args subject;
  if (s->kind == k) subject = s->args;
  else subject.push_back(s);
  Expr constWild, restWild;
  Args fixed;
  for (const auto& a : p->args) {
    if (a->kind == Kind::Wild) {
      const auto c = static_cast<Constraint>(a->constraint);
      if (c == Constraint::Const && !constWild) { constWild = a; continue; }
      if (c == Constraint::Any && !restWild) { restWild = a; continue; }
    }
    fixed.push_back(a);
  }
  const Bindings saved = b;
  if (constWild) {
    Args constant, others;
    for (const auto& x : subject) (!b.var || freeOf(x, b.var) ? constant : others).push_back(x);
    if (!bindWild(constWild, combine(k, std::move(constant)), b)) { b = saved; return false; }
    subject = std::move(others);
  }
  std::vector<bool> used(subject.size(), false);
  if (matchFixed(fixed, 0, subject, used, restWild, k, b)) return true;
  b = saved;
  return false;
}
}  // namespace

Expr wild(uint8_t index, Constraint c) {
  Node n;
  n.kind = Kind::Wild;
  n.wild = index;
  n.constraint = static_cast<uint8_t>(c);
  return finish(std::move(n));
}

bool match(const Expr& p, const Expr& s, Bindings& b) {
  if (!context().step()) return false;
  if (p->kind == Kind::Wild) return bindWild(p, s, b);
  if (p->kind == Kind::Add || p->kind == Kind::Mul) return matchCommutative(p, s, b);
  if (p->kind == Kind::Pow) {
    const Bindings saved = b;
    if (s->kind == Kind::Pow && match(p->args[0], s->args[0], b) && match(p->args[1], s->args[1], b)) return true;
    b = saved;
    // u^n also reads a bare u as u^1.
    if (match(p->args[1], integer(1), b) && match(p->args[0], s, b)) return true;
    b = saved;
    return false;
  }
  if (p->kind != s->kind || p->fn != s->fn || p->args.size() != s->args.size()) return false;
  if (p->kind == Kind::Number) return p->number == s->number;
  if (p->kind == Kind::Symbol || p->kind == Kind::Text) return p->name == s->name;
  const Bindings saved = b;
  for (size_t i = 0; i < p->args.size(); ++i)
    if (!match(p->args[i], s->args[i], b)) { b = saved; return false; }
  return true;
}

Expr instantiate(const Expr& t, const Bindings& b) {
  if (t->kind == Kind::Wild) return b.slot[t->wild] ? b.slot[t->wild] : t;
  if (t->args.empty()) return t;
  Args a;
  a.reserve(t->args.size());
  for (const auto& x : t->args) a.push_back(instantiate(x, b));
  return rebuild(t, std::move(a));
}

bool applyFirst(const std::vector<Rule>& rules, const Expr& s, const Expr& var, Expr& out) {
  for (const auto& r : rules) {
    Bindings b;
    b.var = var;
    if (!match(r.pattern, s, b) || (r.when && !r.when(b))) continue;
    out = r.build ? r.build(b) : instantiate(r.replacement, b);
    if (out && context().ok()) return true;
  }
  return false;
}

namespace {
Expr rewriteAt(const Expr& e, const std::vector<Rule>& rules, const Expr& var, unsigned depth) {
  if (!context().ok()) return e;
  Expr cur = mapArgs(e, [&](const Expr& a) { return rewriteAt(a, rules, var, depth); });
  Expr next;
  if (depth < 4 && applyFirst(rules, cur, var, next) && !equal(next, cur)) return rewriteAt(next, rules, var, depth + 1);
  return cur;
}
}  // namespace
Expr rewrite(const Expr& e, const std::vector<Rule>& rules, const Expr& var) { return rewriteAt(e, rules, var, 0); }

bool linear(const Expr& e, const Expr& x, Expr& a, Expr& b) {
  if (freeOf(e, x)) return false;
  Args as, bs;
  auto term = [&](const Expr& t) {
    if (freeOf(t, x)) { bs.push_back(t); return true; }
    if (equal(t, x)) { as.push_back(integer(1)); return true; }
    if (t->kind != Kind::Mul) return false;
    Args rest;
    bool found = false;
    for (const auto& f : t->args) {
      if (!found && equal(f, x)) { found = true; continue; }
      if (!freeOf(f, x)) return false;
      rest.push_back(f);
    }
    if (found) as.push_back(mul(std::move(rest)));
    return found;
  };
  if (e->kind == Kind::Add) {
    for (const auto& t : e->args)
      if (!term(t)) return false;
  } else if (!term(e)) {
    return false;
  }
  a = add(std::move(as));
  b = add(std::move(bs));
  return !zero(a);
}

}  // namespace Cas
}  // namespace Calculator
