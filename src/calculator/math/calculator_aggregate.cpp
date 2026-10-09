#include "calculator/math/calculator_aggregate.h"
#include "calculator/math/calculator_eng_symbols.h"
#include <cstring>
#include <memory>

namespace Calculator {
namespace {
constexpr int kPool = 8;
bool aggregateName(const std::string& t, bool vectors, int& slot) {
  const char* prefix = vectors ? "Vct" : "Mat";
  if (t.size() < 4 || t.compare(0, 3, prefix) != 0) return false;
  if (t.size() == 4 && t[3] >= 'A' && t[3] <= 'D') { slot = t[3] - 'A'; return true; }
  if (t.compare(3, std::string::npos, "Ans") == 0) { slot = kAnswerSlot; return true; }
  return false;
}
bool aggregateCall(const std::string& t) {
  return t == "det" || t == "trn" || t == "identity" || t == "unitv" || t == "angle";
}

class Evaluator {
 public:
  Evaluator(const Request& request, const Aggregates& aggregates, bool vectorApp,
            const std::function<bool()>& stop, const std::function<void()>& yield)
      : q(request), store(aggregates), vectors(vectorApp), cancel(stop), cooperate(yield) {}

  Result run() {
    Result r;
    r.memory = q.memory;
    const Parsed parsed = parse(q.expression);
    if (parsed.error != Error::None) { r.error = parsed.error; r.position = parsed.position; return r; }
    T v = eval(parsed.root);
    if (error == Error::None && v.slot >= 0) {
      auto out = std::make_shared<Aggregate>(pool[static_cast<size_t>(v.slot)]);
      for (auto& cell : out->cells) {
        cell = cell.rounded();
        if (!cell.finite() || cell.abs() > Decimal("9.999999999E99")) fail(Error::Math, 0);
        else if (!cell.zero() && cell.abs() < Decimal("1E-99")) cell = Decimal(0);
      }
      r.aggregate = out;
    } else if (error == Error::None) {
      v.s.number = v.s.number.rounded();
      if (!v.s.number.finite() || v.s.number.abs() > Decimal("9.999999999E99")) fail(Error::Math, 0);
      r.value = v.s;
      r.memory.ans = v.s;
    }
    if (error != Error::None) {
      r = Result{};
      r.memory = q.memory;
      r.error = error;
      r.position = position;
    }
    return r;
  }

 private:
  struct T {
    int slot = -1;  // >= 0: an aggregate in the pool
    Value s;
  };
  const Request& q;
  const Aggregates& store;
  bool vectors;
  const std::function<bool()>& cancel;
  const std::function<void()>& cooperate;
  std::array<Aggregate, kPool> pool;
  std::array<bool, kPool> used{};
  Error error = Error::None;
  size_t position = 0;
  unsigned steps = 0;

  T fail(Error e, size_t p) {
    if (error == Error::None) { error = e; position = p; }
    return {};
  }
  int take(size_t p) {
    for (int i = 0; i < kPool; ++i)
      if (!used[static_cast<size_t>(i)]) { used[static_cast<size_t>(i)] = true; return i; }
    fail(Error::Stack, p);
    return -1;
  }
  void give(const T& t) { if (t.slot >= 0) used[static_cast<size_t>(t.slot)] = false; }
  Aggregate& at(const T& t) { return pool[static_cast<size_t>(t.slot)]; }
  T aggregate(size_t p) {
    T t;
    t.slot = take(p);
    if (t.slot >= 0) at(t) = Aggregate{};
    return t;
  }
  T scalar(const Decimal& d) {
    T t;
    t.s = Value(d);
    return t;
  }
  T scalarOperation(const Expression& n, const std::vector<Value>& arguments) {
    const Result r = evaluateScalarOperation(n, arguments, q, cancel, cooperate);
    if (r.error != Error::None) return fail(r.error, r.position);
    T t;
    t.s = r.value;
    return t;
  }

  bool has(const Expression& n) const {
    if (!n) return false;
    int slot;
    if (n->kind == Node::Kind::Symbol && aggregateName(n->text, vectors, slot)) return true;
    if (n->kind == Node::Kind::Call && aggregateCall(n->text)) return true;
    if (n->kind == Node::Kind::Binary && n->text == "'") return true;
    for (const auto& child : n->children)
      if (has(child)) return true;
    return false;
  }

  T eval(const Expression& n) {
    if (!n || error != Error::None) return {};
    if (cancel && cancel()) return fail(Error::Cancelled, n->position);
    if ((++steps & 31) == 0 && cooperate) cooperate();
    if (!has(n)) {
      Request sub = q;
      sub.verify = false;
      const Result r = evaluateTree(n, sub, cancel, cooperate);
      if (r.error != Error::None) return fail(r.error, r.position);
      T t;
      t.s = r.value;
      return t;
    }
    const size_t p = n->position;
    const std::string& op = n->text;
    switch (n->kind) {
      case Node::Kind::Symbol: {
        int slot = 0;
        if (!aggregateName(op, vectors, slot)) return fail(Error::Syntax, p);
        const Aggregate& source = (vectors ? store.vectors : store.matrices)[static_cast<size_t>(slot)];
        if (!source.defined()) return fail(Error::NotDefined, p);
        T t = aggregate(p);
        if (t.slot >= 0) at(t) = source;
        return t;
      }
      case Node::Kind::Unary: {
        T a = eval(n->children[0]);
        if (error != Error::None) return {};
        if (a.slot < 0) return scalarOperation(n, {a.s});
        if (op == "+") return a;
        if (EngSymbol::is(op[0])) {  // postfix x10^n scales a scalar or every cell
          const int e = EngSymbol::exponent(op[0]);
          T k;
          k.s = Value(Decimal(("1E" + std::to_string(e < 0 ? -e : e)).c_str()));
          return binary(e < 0 ? "/" : "*", a, k, p);
        }
        if (op != "-") { give(a); return fail(Error::Math, p); }
        if (a.slot < 0) { a.s = realNegate(a.s); return a; }
        for (auto& cell : at(a).cells) cell = -cell;
        return a;
      }
      case Node::Kind::Binary: {
        T a = eval(n->children[0]);
        T b = eval(n->children[1]);
        if (error != Error::None) { give(a); give(b); return {}; }
        if (a.slot < 0 && b.slot < 0) return scalarOperation(n, {a.s, b.s});
        T out = binary(op, a, b, p);
        give(a == out ? b : a);
        if (!(b == out)) give(b);
        return out;
      }
      case Node::Kind::Call:
        return call(n);
      default:
        return fail(Error::Syntax, p);
    }
  }

  friend bool operator==(const T& x, const T& y) { return x.slot >= 0 && x.slot == y.slot; }

  // Result of `a op b`; it may reuse a's or b's slot (the caller frees the other).
  T binary(const std::string& op, T& a, T& b, size_t p) {
    const bool A = a.slot >= 0, B = b.slot >= 0;
    if (!A && !B) {
      if (op == "+" || op == "-" || op == "*" || op == "/") {
        if (op == "/" && b.s.number.zero()) return fail(Error::Math, p);
        T t;
        t.s = realCombine(a.s, b.s, op[0]);
        return t;
      }
      return fail(Error::Syntax, p);
    }
    if (op == "+" || op == "-") {
      if (!A || !B) return fail(Error::Math, p);
      Aggregate& x = at(a);
      const Aggregate& y = at(b);
      if (x.rows != y.rows || x.cols != y.cols) return fail(Error::Dimension, p);
      for (size_t i = 0; i < 16; ++i) x.cells[i] = op == "+" ? x.cells[i] + y.cells[i] : x.cells[i] - y.cells[i];
      return a;
    }
    if (op == "/") {
      if (!A || B || b.s.number.zero()) return fail(Error::Math, p);
      for (auto& cell : at(a).cells) cell = cell / b.s.number;
      return a;
    }
    if (op == "*" && (A != B)) {
      T& m = A ? a : b;
      const Decimal k = (A ? b : a).s.number;
      for (auto& cell : at(m).cells) cell = cell * k;
      return m;
    }
    if (op == "'") {
      if (!vectors || !A || !B) return fail(Error::Syntax, p);
      if (at(a).rows != at(b).rows) return fail(Error::Dimension, p);
      return scalar(dot(at(a), at(b)));
    }
    if (op == "^") {
      if (!A || B) return fail(Error::Math, p);
      return power(a, b.s, p);
    }
    if (op != "*") return fail(Error::Syntax, p);
    const Aggregate& x = at(a);
    const Aggregate& y = at(b);
    T out = aggregate(p);
    if (out.slot < 0) return {};
    Aggregate& z = at(out);
    if (vectors) {
      if (x.rows != y.rows) { give(out); return fail(Error::Dimension, p); }
      z.rows = 3;
      z.cols = 1;
      if (x.rows == 2) {
        z.at(2, 0) = x.at(0, 0) * y.at(1, 0) - x.at(1, 0) * y.at(0, 0);
      } else {
        z.at(0, 0) = x.at(1, 0) * y.at(2, 0) - x.at(2, 0) * y.at(1, 0);
        z.at(1, 0) = x.at(2, 0) * y.at(0, 0) - x.at(0, 0) * y.at(2, 0);
        z.at(2, 0) = x.at(0, 0) * y.at(1, 0) - x.at(1, 0) * y.at(0, 0);
      }
      return out;
    }
    if (x.cols != y.rows) { give(out); return fail(Error::Dimension, p); }
    multiply(x, y, z);
    return out;
  }
  static void multiply(const Aggregate& x, const Aggregate& y, Aggregate& z) {
    z.rows = x.rows;
    z.cols = y.cols;
    for (int r = 0; r < x.rows; ++r)
      for (int c = 0; c < y.cols; ++c) {
        Decimal sum(0);
        for (int k = 0; k < x.cols; ++k) sum = sum + x.at(r, k) * y.at(k, c);
        z.at(r, c) = sum;
      }
  }
  static Decimal dot(const Aggregate& x, const Aggregate& y) {
    Decimal sum(0);
    for (int r = 0; r < x.rows; ++r) sum = sum + x.at(r, 0) * y.at(r, 0);
    return sum;
  }
  static Decimal norm(const Aggregate& x) { return dot(x, x).sqrt(); }

  // Gauss-Jordan with partial pivoting on a copy. A pivot at or below 1E-30 of
  // the largest entry is singular: exact zeros stay zero in 40-digit decimal,
  // and the scale keeps the test independent of the matrix's units.
  bool invert(const Aggregate& m, Aggregate& inverse, Decimal& determinant) {
    const int n = m.rows;
    std::array<Decimal, 16> a = m.cells, e{};
    Decimal scale(0);
    for (const auto& cell : a) if (cell.abs() > scale) scale = cell.abs();
    for (int i = 0; i < n; ++i) e[static_cast<size_t>(i * 5)] = Decimal(1);
    determinant = Decimal(1);
    const Decimal tiny = scale * Decimal("1E-30");
    auto A = [&](int r, int c) -> Decimal& { return a[static_cast<size_t>(r * 4 + c)]; };
    auto E = [&](int r, int c) -> Decimal& { return e[static_cast<size_t>(r * 4 + c)]; };
    for (int col = 0; col < n; ++col) {
      int best = col;
      for (int r = col + 1; r < n; ++r) if (A(r, col).abs() > A(best, col).abs()) best = r;
      if (A(best, col).abs() <= tiny || A(best, col).zero()) { determinant = Decimal(0); return false; }
      if (best != col) {
        for (int c = 0; c < 4; ++c) { std::swap(A(best, c), A(col, c)); std::swap(E(best, c), E(col, c)); }
        determinant = -determinant;
      }
      const Decimal pivot = A(col, col);
      determinant = determinant * pivot;
      for (int c = 0; c < 4; ++c) { A(col, c) = A(col, c) / pivot; E(col, c) = E(col, c) / pivot; }
      for (int r = 0; r < n; ++r) {
        if (r == col || A(r, col).zero()) continue;
        const Decimal f = A(r, col);
        for (int c = 0; c < 4; ++c) { A(r, c) = A(r, c) - f * A(col, c); E(r, c) = E(r, c) - f * E(col, c); }
      }
    }
    inverse = m;
    inverse.cells = e;
    return true;
  }

  T power(T& a, const Value& exponent, size_t p) {
    int64_t k;
    Aggregate& m = at(a);
    if (vectors || !exponent.number.toInt(k) || (k != -1 && (k < 1 || k > 3))) return fail(Error::Math, p);
    if (m.rows != m.cols) return fail(Error::Dimension, p);
    if (k == -1) {
      Aggregate inverse;
      Decimal det;
      if (!invert(m, inverse, det)) return fail(Error::Math, p);
      m = inverse;
      return a;
    }
    const Aggregate base = m;
    Aggregate product;
    for (int64_t i = 1; i < k; ++i) {
      multiply(m, base, product);
      m = product;
    }
    return a;
  }

  T call(const Expression& n) {
    const std::string& t = n->text;
    const size_t p = n->position;
    const auto& args = n->children;
    // The x^2, x^-1 and fraction keys write templates: the same rules as ^ and /.
    if ((t == "pow" || t == "frac") && args.size() == 2) {
      T a = eval(args[0]);
      T b = eval(args[1]);
      if (error != Error::None) { give(a); give(b); return {}; }
      if (a.slot < 0 && b.slot < 0) return scalarOperation(n, {a.s, b.s});
      T out = binary(t == "pow" ? "^" : "/", a, b, p);
      if (!(a == out)) give(a);
      if (!(b == out)) give(b);
      return out;
    }
    if (!aggregateCall(t) && t != "abs") {
      std::vector<Value> values;
      for (const auto& arg : args) {
        T a = eval(arg);
        if (error != Error::None) { give(a); return {}; }
        if (a.slot >= 0) { give(a); return fail(Error::Math, p); }
        values.push_back(a.s);
      }
      return scalarOperation(n, values);
    }
    if (t == "angle") {
      if (!vectors || args.size() != 2) return fail(Error::Argument, p);
      T a = eval(args[0]);
      T b = eval(args[1]);
      if (error != Error::None) { give(a); give(b); return {}; }
      if (a.slot < 0 || b.slot < 0) { give(a); give(b); return fail(Error::Math, p); }
      const Aggregate& x = at(a);
      const Aggregate& y = at(b);
      if (x.rows != y.rows) { give(a); give(b); return fail(Error::Dimension, p); }
      const Decimal nx = norm(x), ny = norm(y), d = dot(x, y);
      give(a);
      give(b);
      if (nx.zero() || ny.zero()) return fail(Error::Math, p);
      // atan2(|x cross y|, x.y): accurate near parallel, where acos is not.
      Decimal c;
      if (x.rows == 2) c = (x.at(0, 0) * y.at(1, 0) - x.at(1, 0) * y.at(0, 0)).abs();
      else {
        const Decimal i = x.at(1, 0) * y.at(2, 0) - x.at(2, 0) * y.at(1, 0);
        const Decimal j = x.at(2, 0) * y.at(0, 0) - x.at(0, 0) * y.at(2, 0);
        const Decimal k = x.at(0, 0) * y.at(1, 0) - x.at(1, 0) * y.at(0, 0);
        c = (i * i + j * j + k * k).sqrt();
      }
      const Decimal pi = Decimal::pi();
      Decimal theta = d.zero() ? pi / 2 : (c / d).atan();
      if (d.negative()) theta = theta + pi;
      const Angle unit = q.settings.angle;
      return scalar(unit == Angle::Degree ? theta * 180 / pi : unit == Angle::Gradian ? theta * 200 / pi : theta);
    }
    if (args.size() != 1) return fail(Error::Argument, p);
    if (t == "identity") {
      if (vectors) return fail(Error::Syntax, p);
      T size = eval(args[0]);
      int64_t k;
      if (error != Error::None) return {};
      if (size.slot >= 0 || !size.s.number.toInt(k) || k < 1 || k > 4) { give(size); return fail(Error::Math, p); }
      T out = aggregate(p);
      if (out.slot < 0) return {};
      at(out).rows = at(out).cols = static_cast<uint8_t>(k);
      for (int i = 0; i < k; ++i) at(out).at(i, i) = Decimal(1);
      return out;
    }
    T a = eval(args[0]);
    if (error != Error::None) return {};
    if (a.slot < 0) {
      if (t == "abs") { if (compareValues(a.s, Value(0)) < 0) a.s = realNegate(a.s); return a; }
      return fail(Error::Math, p);
    }
    Aggregate& m = at(a);
    if (t == "abs") {
      if (!vectors) { for (auto& cell : m.cells) cell = cell.abs(); return a; }
      give(a);
      return scalar(norm(m));
    }
    if (t == "unitv") {
      if (!vectors) { give(a); return fail(Error::Syntax, p); }
      const Decimal length = norm(m);
      if (length.zero()) { give(a); return fail(Error::Math, p); }
      for (auto& cell : m.cells) cell = cell / length;
      return a;
    }
    if (vectors) { give(a); return fail(Error::Syntax, p); }
    if (t == "trn") {
      Aggregate out;
      out.rows = m.cols;
      out.cols = m.rows;
      for (int r = 0; r < m.rows; ++r)
        for (int c = 0; c < m.cols; ++c) out.at(c, r) = m.at(r, c);
      m = out;
      return a;
    }
    if (t == "det") {
      if (m.rows != m.cols) { give(a); return fail(Error::Dimension, p); }
      Aggregate inverse;
      Decimal det;
      invert(m, inverse, det);
      give(a);
      return scalar(det);
    }
    give(a);
    return fail(Error::Math, p);
  }
};
}  // namespace

const char* aggregateName(bool vector, int slot) {
  static const char* const kMatrices[] = {"MatA", "MatB", "MatC", "MatD", "MatAns"};
  static const char* const kVectors[] = {"VctA", "VctB", "VctC", "VctD", "VctAns"};
  return slot < 0 || slot > kAnswerSlot ? "?" : vector ? kVectors[slot] : kMatrices[slot];
}

Result evaluateAggregate(const Request& request, const Aggregates& store, bool vectors,
                         const std::function<bool()>& cancelled, const std::function<void()>& cooperate) {
  // The evaluator carries eight 650-byte temporaries: heap, not the worker stack.
  auto evaluator = std::make_unique<Evaluator>(request, store, vectors, cancelled, cooperate);
  return evaluator->run();
}

}  // namespace Calculator
