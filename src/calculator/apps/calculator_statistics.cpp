#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/apps/calculator_statistics.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/apps/calculator_calculate.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_render.h"
#include <algorithm>

namespace Calculator {

// ---- Statistics Calc token bridge -----------------------------------------
// Canonical serialization (documented, ASCII): the CATALOG inserts these
// spellings; the panel shows them as typed (native glyphs/postfix layout is
// raster-PENDING). Evaluation rewrites them onto Decimal literals, then the
// normal engine evaluates the rewritten tree exactly once.

enum class TokenKind : uint8_t { Unknown, Value, NeedMode, NeedValue };

struct TokenResult {
  TokenKind kind = TokenKind::Unknown;
  Decimal value;
};

TokenResult statVariable(const std::string& name, const StatsSummary& s, const RegressionFit& f,
                         RegressionType type, bool twoVar) {
  const bool quad = type == RegressionType::Quadratic;
  auto smp = [&](const Decimal& v) -> TokenResult {
    return s.smpValid ? TokenResult{TokenKind::Value, v} : TokenResult{TokenKind::NeedValue, Decimal()};
  };
  if (name == "StatN" && s.defined) return {TokenKind::Value, s.n};
  if (name == "StatSumX" && s.defined) return {TokenKind::Value, s.sumX};
  if (name == "StatSumX2" && s.defined) return {TokenKind::Value, s.sumX2};
  if (name == "StatMeanX" && s.defined) return {TokenKind::Value, s.meanX};
  if (name == "StatVarPX" && s.defined) return {TokenKind::Value, s.popVarX};
  if (name == "StatSdPX" && s.defined) return {TokenKind::Value, s.popSdX};
  if (name == "StatVarSX" && s.defined) return smp(s.smpVarX);
  if (name == "StatSdSX" && s.defined) return smp(s.smpSdX);
  if (name == "StatMinX" && s.defined) return {TokenKind::Value, s.minX};
  if (name == "StatMaxX" && s.defined) return {TokenKind::Value, s.maxX};
  if ((name == "StatQ1" || name == "StatMed" || name == "StatQ3") && s.defined)
    return twoVar ? TokenResult{TokenKind::NeedMode, Decimal()}
                  : TokenResult{TokenKind::Value,
                                name == "StatQ1" ? s.q1 : name == "StatMed" ? s.med : s.q3};
  if (!twoVar || !s.defined) {
    // 2-variable-only sums below; in 1-variable mode they are mode mismatch.
    static const char* kTwo[] = {"StatSumY", "StatSumY2", "StatSumXY", "StatSumX3", "StatSumX2Y",
                                 "StatSumX4", "StatMeanY", "StatVarPY", "StatSdPY", "StatVarSY",
                                 "StatSdSY", "StatMinY", "StatMaxY", "RegA", "RegB", "RegC", "RegR"};
    for (auto t : kTwo)
      if (name == t) return {TokenKind::NeedMode, Decimal()};
    return {TokenKind::Unknown, Decimal()};
  }
  if (name == "StatSumY") return {TokenKind::Value, s.sumY};
  if (name == "StatSumY2") return {TokenKind::Value, s.sumY2};
  if (name == "StatSumXY") return {TokenKind::Value, s.sumXY};
  if (name == "StatSumX3") return {TokenKind::Value, s.sumX3};
  if (name == "StatSumX2Y") return {TokenKind::Value, s.sumX2Y};
  if (name == "StatSumX4") return {TokenKind::Value, s.sumX4};
  if (name == "StatMeanY") return {TokenKind::Value, s.meanY};
  if (name == "StatVarPY") return {TokenKind::Value, s.popVarY};
  if (name == "StatSdPY") return {TokenKind::Value, s.popSdY};
  if (name == "StatVarSY") return smp(s.smpVarY);
  if (name == "StatSdSY") return smp(s.smpSdY);
  if (name == "StatMinY") return {TokenKind::Value, s.minY};
  if (name == "StatMaxY") return {TokenKind::Value, s.maxY};
  if (!f.defined) {
    if (name == "RegA" || name == "RegB" || name == "RegC" || name == "RegR")
      return {TokenKind::NeedValue, Decimal()};
    return {TokenKind::Unknown, Decimal()};
  }
  if (name == "RegA") return {TokenKind::Value, f.a};
  if (name == "RegB") return {TokenKind::Value, f.b};
  if (name == "RegC") return quad ? TokenResult{TokenKind::Value, f.c} : TokenResult{TokenKind::NeedMode, Decimal()};
  if (name == "RegR")
    return !quad && f.rValid ? TokenResult{TokenKind::Value, f.r}
           : !quad          ? TokenResult{TokenKind::NeedValue, Decimal()}
                            : TokenResult{TokenKind::NeedMode, Decimal()};
  return {TokenKind::Unknown, Decimal()};
}

bool statCallName(const std::string& name) {
  return name == "StatP" || name == "StatQ" || name == "StatR" || name == "StatT" ||
         name == "StatXHat" || name == "StatYHat" || name == "StatX1Hat" || name == "StatX2Hat";
}

namespace {
// Bottom-up expansion: stat symbols become Decimal literals; stat calls
// evaluate their (already expanded) single argument through the normal engine
// and fold to a literal. Everything else is left for evaluateTree.
struct Expander {
  Request request;
  const StatsSummary& summary;
  const RegressionFit& fit;
  RegressionType type;
  bool twoVar;
  const std::function<bool()>& cancelled;
  const std::function<void()>& cooperate;
  Error error = Error::None;
  size_t position = 0;

  void fail(Error e, size_t p) {
    if (error == Error::None) {
      error = e;
      position = p;
    }
  }

  Expression number(const Decimal& value, size_t p) {
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Number;
    n->text = value.str(40);
    n->position = p;
    return n;
  }

  Expression rewrite(const Expression& n) {
    if (!n || error != Error::None) return n;
    if (n->kind == Node::Kind::Symbol) {
      TokenResult t = statVariable(n->text, summary, fit, type, twoVar);
      if (t.kind == TokenKind::Value) return number(t.value, n->position);
      if (t.kind == TokenKind::NeedMode) fail(Error::NotDefined, n->position);
      if (t.kind == TokenKind::NeedValue) fail(Error::Math, n->position);
      return n;
    }
    if (n->kind == Node::Kind::Call && statCallName(n->text)) {
      if (n->children.size() != 1) {
        fail(Error::Argument, n->position);
        return n;
      }
      Expression arg = rewrite(n->children[0]);
      if (error != Error::None) return n;
      Request sub = request;
      sub.verify = false;
      Result r = evaluateTree(arg, sub, cancelled, cooperate);
      if (r.error != Error::None) {
        fail(r.error, r.position);
        return n;
      }
      const Decimal a = r.value.number;
      const std::string& fn = n->text;
      if (fn == "StatP" || fn == "StatQ" || fn == "StatR" || fn == "StatT") {
        if (!twoVar) {
          // Norm Dist helpers are 1-variable-only (880-MAN).
          Probability::Outcome t;
          if (fn == "StatT") {
            if (!summary.defined || summary.popSdX.zero()) {
              fail(Error::Math, n->position);
              return n;
            }
            t = {(a - summary.meanX) / summary.popSdX, Probability::Status::Ok};
          } else {
            t = Probability::normalCdf(a, Decimal(0), Decimal(1), cancelled, cooperate);
          }
          if (t.status != Probability::Status::Ok) {
            fail(t.status == Probability::Status::Domain    ? Error::Math
                 : t.status == Probability::Status::NoConvergence ? Error::TimeOut
                                                                 : Error::Cancelled,
                 n->position);
            return n;
          }
          Decimal v = t.value;
          if (fn == "StatQ") v = v - Decimal("0.5");  // signed integral definition (plan I)
          if (fn == "StatR") v = Decimal(1) - v;
          return number(v, n->position);
        }
        fail(Error::NotDefined, n->position);
        return n;
      }
      if (!twoVar || !fit.defined) {
        fail(!twoVar ? Error::NotDefined : Error::Math, n->position);
        return n;
      }
      Probability::Outcome o = fn == "StatYHat"  ? regressionYHat(fit, type, a)
                       : fn == "StatX2Hat" ? regressionXHat(fit, type, a, 1)
                                           : regressionXHat(fit, type, a, 0);
      if (o.status != Probability::Status::Ok) {
        fail(o.status == Probability::Status::Domain ? Error::Math : Error::Cancelled, n->position);
        return n;
      }
      return number(o.value, n->position);
    }
    bool changed = false;
    std::vector<Expression> kids;
    kids.reserve(n->children.size());
    for (const auto& c : n->children) {
      Expression k = rewrite(c);
      if (error != Error::None) return n;
      changed = changed || k != c;
      kids.push_back(k);
    }
    if (!changed) return n;
    auto out = std::make_shared<Node>();
    out->kind = n->kind;
    out->text = n->text;
    out->position = n->position;
    out->children = std::move(kids);
    return out;
  }
};
}  // namespace

Result evaluateStatistics(const StatisticsTask& task, const Request& request,
                          const std::function<bool()>& cancelled,
                          const std::function<void()>& cooperate) {
  Result output;
  if (task.kind == StatisticsJobKind::EditCell) {
    // Cell commits evaluate the entered expression through the real engine
    // (range-checked like every other cell), then validate the column domain.
    Request local = request;
    local.verify = false;
    Result v = evaluate(local, cancelled, cooperate);
    if (v.error != Error::None) {
      output.error = v.error;
      output.position = v.position;
      return output;
    }
    if (!v.value.number.finite()) {
      output.error = Error::Math;
      return output;
    }
    if (task.col == 2 && v.value.number < Decimal(0)) {
      // Frequency domain: finite non-negative numerics (provisional D; the
      // manual constrains neither sign nor integer-only, so this stays
      // PENDING rather than 880-MAN).
      output.error = Error::Math;
      return output;
    }
    auto payload = std::make_shared<StatisticsResult>();
    payload->kind = StatisticsJobKind::EditCell;
    payload->editValue = v.value.number;
    output.statistics = std::move(payload);
    return output;
  }
  if (!task.dataset) {
    output.error = Error::Math;
    return output;
  }
  StatsSummary s = computeStatsSummary(*task.dataset, task.twoVariable, task.frequency, cancelled, cooperate);
  if (s.error == Error::Cancelled) {
    output.error = Error::Cancelled;
    return output;
  }
  RegressionFit f;
  if (task.twoVariable) {
    f = fitRegression(*task.dataset, task.twoVariable, task.frequency, task.regression, cancelled, cooperate);
    if (f.error == Error::Cancelled) {
      output.error = Error::Cancelled;
      return output;
    }
  }
  if (task.kind == StatisticsJobKind::Summary) {
    if (s.error != Error::None) {
      output.error = s.error;  // empty dataset: modal error, no payload
      return output;
    }
    auto payload = std::make_shared<StatisticsResult>();
    payload->kind = StatisticsJobKind::Summary;
    payload->summary = s;
    payload->regression = f;
    output.statistics = std::move(payload);
    return output;
  }
  // Calculate: expression against the snapshot with a caller-local Ans.
  if (s.error != Error::None) {
    output.error = s.error;
    return output;
  }
  Request local = request;
  local.verify = false;
  local.memory.ans = ScalarValue(Value(task.calcAns));
  Parsed parsed = parse(task.expression);
  if (parsed.error != Error::None) {
    output.error = parsed.error;
    output.position = parsed.position;
    return output;
  }
  Expander ex{local, s, f, task.regression, task.twoVariable, cancelled, cooperate};
  Expression root = ex.rewrite(parsed.root);
  if (ex.error != Error::None) {
    output.error = ex.error;
    output.position = ex.position;
    return output;
  }
  Result r = evaluateTree(root, local, cancelled, cooperate);
  // Statistical results present as decimals: strip the engine's exact
  // rational/radical metadata (a rewritten 119/20 must show 5.95, per the
  // manual's decimal result screens; exact FORMAT behavior stays PENDING).
  output.value = Value(r.value.number);
  output.error = r.error;
  output.position = r.position;
  return output;
}

// ---- Controller ------------------------------------------------------------

namespace StatisticsApp {
namespace {
using K = ExpressionInput::Key;
constexpr int kResultVisibleRows = 6;  // CW capture: six small rows per result page.
enum class Screen : uint8_t {
  Type, Data, Tools, Frequency, Edit, Sort, Dispatch, RegType, Results,
  Calc, CalcResult, Catalog, CatalogGroup, Confirm, Error
};
enum class RegFrom : uint8_t { Dispatch, Calc };
// Destructive data transitions go through the native "Clear Data?"
// Yes/Cancel confirm (CW panel capture): mode switch, Frequency toggle,
// and Delete All. The pending transition executes only on Yes.
enum class ClearPending : uint8_t { None, DeleteAll, ModeSwitch, FreqToggle };

struct State {
  Screen screen = Screen::Type, errorOrigin = Screen::Data, returnTo = Screen::Data;
  RegFrom regFrom = RegFrom::Dispatch;
  StatisticsMode mode = StatisticsMode::OneVariable;
  bool frequency = false;
  RegressionType regression = RegressionType::Linear;
  std::shared_ptr<StatisticsDataset> dataset;
  GridCursor cursor;
  CellEdit edit;
  InputTarget target;
  Viewport viewport;
  uint32_t revision = 0, epoch = 0;
  int selection = 0, first = 0;
  int catalog = 0, catalogFirst = 0, entry = 0, entryFirst = 0;
  int resultSelection = 0, resultFirst = 0;
  bool showRegList = false;
  StatsSummary summary;
  RegressionFit fit;
  uint32_t computedRevision = 0xFFFFFFFF;
  RegressionType computedReg = RegressionType::Linear;
  MathEditor calc;
  Viewport calcView;
  std::string calcExpr, calcValue;
  Decimal calcAns;
  bool calcHasResult = false, shared = false, pending = false, active = false;
  ClearPending clearPending = ClearPending::None;
  StatisticsMode clearMode = StatisticsMode::OneVariable;
  bool clearFreq = false;
  Screen clearReturn = Screen::Data;
  Error error = Error::None;
} state;

CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }

bool twoVar() { return state.mode == StatisticsMode::TwoVariable; }
size_t rowCount() { return state.dataset ? state.dataset->rows.size() : 0; }
int columns() { return (twoVar() ? 2 : 1) + (state.frequency ? 1 : 0); }
size_t capacity() { return statisticsCapacity(twoVar(), state.frequency); }
GridPolicy policy() {
  return {static_cast<int>(std::min(capacity(), rowCount() + 1)), columns(), 4, 3};
}
void invalidate() {
  jobs().cancel();
  state.pending = false;
  ++state.revision;
}
void menu(Screen s, int selected = 0) {
  state.screen = s;
  state.selection = selected;
  state.first = 0;
}
void showError(Error e) {
  state.errorOrigin = state.screen;
  state.screen = Screen::Error;
  state.error = e;
}
void clearData() {
  invalidate();
  state.dataset.reset();
  state.cursor = {};
  state.edit.abort();
  state.summary = StatsSummary{};
  state.fit = RegressionFit{};
  state.computedRevision = 0xFFFFFFFF;
  state.resultSelection = state.resultFirst = 0;
  state.calcHasResult = false;
  state.clearPending = ClearPending::None;
}
bool hasData() {
  if (!state.dataset) return false;
  for (const auto& r : state.dataset->rows)
    if (r.hasX || r.hasY || r.hasFreq) return true;
  return false;
}
// Native "Clear Data?" gate (CW capture): prompt only when committed data
// would be lost; an empty editor applies the transition silently.
void requestClear(ClearPending kind, Screen returnTo);
void runPendingClear() {
  const ClearPending kind = state.clearPending;
  state.clearPending = ClearPending::None;
  if (kind == ClearPending::ModeSwitch) {
    state.mode = state.clearMode;
    state.regression = RegressionType::Linear;
    clearData();  // mode switch clears all data (880-MAN)
    state.cursor = {};
    menu(Screen::Data);
  } else if (kind == ClearPending::FreqToggle) {
    state.frequency = state.clearFreq;
    clearData();  // Frequency change clears all data (880-MAN)
    state.cursor = {};
    menu(Screen::Data);
  } else if (kind == ClearPending::DeleteAll) {
    clearData();
    menu(Screen::Data);
  }
}
void requestClear(ClearPending kind, Screen returnTo) {
  if (!hasData()) {
    state.clearPending = kind;
    runPendingClear();
    return;
  }
  state.clearPending = kind;
  state.clearReturn = returnTo;
  menu(Screen::Confirm);
}
void syncEpoch() {
  if (state.epoch != session().statEpoch) {
    state.epoch = session().statEpoch;
    clearData();
    menu(Screen::Data);
  }
}
std::shared_ptr<StatisticsDataset> copyRows() {
  auto p = std::make_shared<StatisticsDataset>();
  if (state.dataset) p->rows = state.dataset->rows;
  return p;
}
void replace(std::shared_ptr<StatisticsDataset> p) {
  invalidate();
  state.dataset = std::move(p);
  state.edit.abort();
  state.summary = StatsSummary{};
  state.fit = RegressionFit{};
  state.computedRevision = 0xFFFFFFFF;
  clampGrid(state.cursor, policy());
}
std::string cellText(const StatRow& row, int col) {
  const bool has = col == 0 ? row.hasX : col == 1 && twoVar() ? row.hasY : row.hasFreq;
  if (!has) return "";
  const Decimal& v = col == 0 ? row.x : col == 1 && twoVar() ? row.y : row.freq;
  return formatValue(Value(v), session().settings);
}
std::string headerAt(int col) {
  if (col == 0) return "x";
  if (twoVar() && col == 1) return "y";
  return "Freq";
}

const char* regressionEquation(RegressionType t) {
  switch (t) {
    case RegressionType::Linear: return "y=a+bx";
    case RegressionType::Quadratic: return "y=a+bx+cx^2";
    case RegressionType::Log: return "y=a+b*ln(x)";
    case RegressionType::ExpE: return "y=a*e^(bx)";
    case RegressionType::ExpAB: return "y=a*b^x";
    case RegressionType::Power: return "y=a*x^b";
    case RegressionType::Inverse: return "y=a+b/x";
  }
  return "";
}

// Result pages use the shared compact renderer (six small `label=value`
// rows, CW capture; exact native glyphs remain raster-PENDING). Row order
// follows the manual's result screenshots via the nine-apps plan.
struct ResultRow {
  std::string label;
  Decimal value;
  bool valid = true;
};

std::vector<ResultRow> resultData() {
  std::vector<ResultRow> v;
  if (!state.summary.defined) return v;
  const StatsSummary& s = state.summary;
  auto push = [&](const char* label, const Decimal& value, bool valid = true) {
    v.push_back({label, value, valid});
  };
  push("xbar", s.meanX);
  push("sumX", s.sumX);
  push("sumX2", s.sumX2);
  push("popVarX", s.popVarX);
  push("popSdX", s.popSdX);
  push("smpVarX", s.smpVarX, s.smpValid);
  push("smpSdX", s.smpSdX, s.smpValid);
  push("n", s.n);
  if (!twoVar()) {
    push("minX", s.minX);
    push("Q1", s.q1);
    push("Med", s.med);
    push("Q3", s.q3);
    push("maxX", s.maxX);
    return v;
  }
  push("ybar", s.meanY);
  push("sumY", s.sumY);
  push("sumY2", s.sumY2);
  push("popVarY", s.popVarY);
  push("popSdY", s.popSdY);
  push("smpVarY", s.smpVarY, s.smpValid);
  push("smpSdY", s.smpSdY, s.smpValid);
  push("sumXY", s.sumXY);
  push("sumX3", s.sumX3);
  push("sumX2Y", s.sumX2Y);
  push("sumX4", s.sumX4);
  push("minX", s.minX);
  push("maxX", s.maxX);
  push("minY", s.minY);
  push("maxY", s.maxY);
  return v;
}

std::vector<ResultRow> regData() {
  std::vector<ResultRow> v;
  if (!state.fit.defined) return v;
  const RegressionFit& f = state.fit;
  const bool quad = state.computedReg == RegressionType::Quadratic;
  auto push = [&](const char* label, const Decimal& value, bool valid = true) {
    v.push_back({label, value, valid});
  };
  push("a", f.a);
  push("b", f.b);
  if (quad)
    push("c", f.c);
  else
    push("r", f.r, f.rValid);
  return v;
}

std::vector<MenuItem> formatRows(const std::vector<ResultRow>& data) {
  std::vector<MenuItem> v;
  for (size_t i = 0; i < data.size(); ++i)
    v.push_back({static_cast<int>(i),
                 data[i].valid ? data[i].label + "=" + formatValue(Value(data[i].value), session().settings)
                               : data[i].label + "=" + errorText(Error::Math),
                 false});
  return v;
}

std::vector<MenuItem> resultRows() { return formatRows(resultData()); }
std::vector<MenuItem> regRows() { return formatRows(regData()); }

std::vector<MenuItem> rows() {
  switch (state.screen) {
    case Screen::Type:
      return {{0, "1-Variable", false}, {1, "2-Variable", false}};
    case Screen::Tools:
      return {{0, "Frequency", true}, {1, "Edit", true}, {2, "Sort", true}};
    case Screen::Frequency:
      return {{0, "On", false, state.frequency ? UiKit::Mark::kRadioOn : UiKit::Mark::kRadioOff},
              {1, "Off", false, state.frequency ? UiKit::Mark::kRadioOff : UiKit::Mark::kRadioOn}};
    case Screen::Edit:
      return {{0, "Insert Row", false}, {1, "Delete All", false}};
    case Screen::Sort: {
      std::vector<MenuItem> v = {{0, "x Ascending", false}, {1, "x Descending", false}};
      if (twoVar()) {
        v.push_back({2, "y Ascending", false});
        v.push_back({3, "y Descending", false});
      }
      if (state.frequency) {
        const int base = twoVar() ? 4 : 2;
        v.push_back({base, "Freq Ascending", false});
        v.push_back({base + 1, "Freq Descending", false});
      }
      return v;
    }
    case Screen::Dispatch: {
      // From StatsCalc TOOLS the dispatch offers result navigation plus
      // Select Reg Type; from the data editor it offers the Calc entry.
      // (880-MAN TOOLS structure for Statistics Calc.)
      const bool fromCalc = state.returnTo == Screen::Calc;
      if (!twoVar()) {
        if (!fromCalc) return {{0, "1-Var Results", false}, {1, "Statistics Calc", false}};
        return {{0, "1-Var Results", false}};
      }
      if (!fromCalc) return {{0, "2-Var Results", false}, {1, "Reg Results", false}, {2, "Statistics Calc", false}};
      return {{0, "2-Var Results", false}, {1, "Reg Results", false}, {2, "Select Reg Type", false}};
    }
    case Screen::RegType: {
      std::vector<MenuItem> v;
      for (int i = 0; i < 7; ++i)
        v.push_back({i, regressionEquation(static_cast<RegressionType>(i)), false});
      return v;
    }
    case Screen::Confirm:
      // Native "Clear Data?" offers Yes first (selected) then Cancel (CW capture).
      return {{0, "Yes", false}, {1, "Cancel", false}};
    case Screen::Catalog: {
      std::vector<MenuItem> v = {{0, "Summation", true}, {1, "Mean/Var/Dev...", true}};
      if (!twoVar()) {
        v.push_back({2, "Min/Max/Quartile", true});
        v.push_back({3, "Norm Dist", true});
      } else {
        v.push_back({2, "Min/Max", true});
        v.push_back({3, "Regression", true});
      }
      return v;
    }
    default:
      return {};
  }
}

struct CatalogEntry {
  const char* label;
  const char* insert;
  bool isTemplate;
  // Postfix native operators (t, xHat, yHat): wrap the operand before the
  // cursor into a call (StatT(2)) instead of appending an empty template.
  bool wrapOperand = false;
};

std::vector<CatalogEntry> catalogEntries() {
  // Insert spellings are the canonical Stat* serialization (ASCII); labels
  // are provisional ASCII (native statistical glyphs are raster-PENDING).
  switch (state.catalog) {
    case 0: {  // Summation
      std::vector<CatalogEntry> v = {{"sumX", "StatSumX", false}, {"sumX2", "StatSumX2", false}};
      if (twoVar())
        v.insert(v.end(), {{"sumY", "StatSumY", false},
                           {"sumY2", "StatSumY2", false},
                           {"sumXY", "StatSumXY", false},
                           {"sumX3", "StatSumX3", false},
                           {"sumX2Y", "StatSumX2Y", false},
                           {"sumX4", "StatSumX4", false}});
      return v;
    }
    case 1: {  // Mean/Var/Dev...
      std::vector<CatalogEntry> v = {{"meanX", "StatMeanX", false},
                                     {"popVarX", "StatVarPX", false},
                                     {"popSdX", "StatSdPX", false},
                                     {"smpVarX", "StatVarSX", false},
                                     {"smpSdX", "StatSdSX", false}};
      if (twoVar())
        v.insert(v.end(), {{"meanY", "StatMeanY", false},
                           {"popVarY", "StatVarPY", false},
                           {"popSdY", "StatSdPY", false},
                           {"smpVarY", "StatVarSY", false},
                           {"smpSdY", "StatSdSY", false}});
      v.push_back({"n", "StatN", false});
      return v;
    }
    case 2:
      if (!twoVar())
        return {{"minX", "StatMinX", false},
                {"Q1", "StatQ1", false},
                {"Med", "StatMed", false},
                {"Q3", "StatQ3", false},
                {"maxX", "StatMaxX", false}};
      return {{"minX", "StatMinX", false},
              {"maxX", "StatMaxX", false},
              {"minY", "StatMinY", false},
              {"maxY", "StatMaxY", false}};
    case 3:
      if (!twoVar())
        return {{"P(", "StatP(", true},
                {"Q(", "StatQ(", true},
                {"R(", "StatR(", true},
                {"t", "StatT", false, true}};
      if (state.computedReg == RegressionType::Quadratic)
        return {{"a", "RegA", false},
                {"b", "RegB", false},
                {"c", "RegC", false},
                {"xHat1", "StatX1Hat", false, true},
                {"xHat2", "StatX2Hat", false, true},
                {"yHat", "StatYHat", false, true}};
      return {{"a", "RegA", false},
              {"b", "RegB", false},
              {"r", "RegR", false},
              {"xHat", "StatXHat", false, true},
              {"yHat", "StatYHat", false, true}};
    default:
      return {};
  }
}

void start(StatisticsJobKind kind, const std::string& expression = "") {
  if (jobs().running()) return;
  auto task = std::make_shared<StatisticsTask>();
  task->kind = kind;
  task->twoVariable = twoVar();
  task->frequency = state.frequency;
  task->regression = state.regression;
  task->dataset = state.dataset ? std::shared_ptr<const StatisticsDataset>(state.dataset)
                                : std::make_shared<StatisticsDataset>();
  task->expression = expression;
  task->calcAns = state.calcAns;
  task->row = state.cursor.row;
  task->col = state.cursor.col;
  Job& job = prepareJob(AppId::Statistics, 0, expression);
  job.statistics = task;
  job.ownerRevision = state.revision;
  state.pending = jobs().start(job) == JobService::Start::Started;
  job.statistics.reset();  // the static scratch must not retain the dataset
  if (!state.pending) showError(Error::Stack);
}

void ensureSummary(Screen returnTo, bool showReg) {
  state.returnTo = returnTo;
  state.showRegList = showReg;
  state.resultSelection = state.resultFirst = 0;
  if (state.computedRevision == state.revision && state.computedReg == state.regression &&
      state.summary.defined) {
    menu(Screen::Results);
    return;
  }
  menu(Screen::Results);
  start(StatisticsJobKind::Summary);
}

void beginEdit(int row, int col, const std::string& text = "") {
  invalidate();
  state.target = {MenuOrigin::Cell, static_cast<int16_t>(row), static_cast<int16_t>(col), state.revision};
  state.edit.begin({row, col}, state.revision, text);
  state.viewport = {};
  state.edit.editor.setMathInput(session().settings.io <= IO::MathDecimal);
}

void deleteRow() {
  if (static_cast<size_t>(state.cursor.row) >= rowCount()) return;
  auto p = copyRows();
  p->rows.erase(p->rows.begin() + state.cursor.row);
  replace(p);
}

void insertRow() {
  if (rowCount() >= capacity()) {
    // Provisional label: the manual confirms capacities but names no error
    // for Insert Row at capacity; exact target behavior is PENDING.
    showError(Error::Range);
    return;
  }
  auto p = copyRows();
  StatRow blank;
  if (twoVar()) {
    blank.y = Decimal(0);
    blank.hasY = true;
  }
  if (state.frequency) {
    blank.freq = Decimal(1);
    blank.hasFreq = true;
  }
  p->rows.reserve(p->rows.size() + 1);
  p->rows.insert(p->rows.begin() + std::min<size_t>(state.cursor.row, p->rows.size()), blank);
  replace(p);
  menu(Screen::Data);
}

void sortRows(int action) {
  // Entire row tuples move; column associations are never broken. Stable.
  const int col = action >= 4 ? 2 : action >= 2 ? 1 : 0;
  const bool descending = action % 2 == 1;
  auto key = [&](const StatRow& r) -> std::pair<bool, Decimal> {
    const bool has = col == 0 ? r.hasX : col == 1 && twoVar() ? r.hasY : r.hasFreq;
    // Rows missing the sort key order first (provisional D; PENDING target).
    return {has, has ? (col == 0 ? r.x : col == 1 && twoVar() ? r.y : r.freq) : Decimal(0)};
  };
  auto p = copyRows();
  std::stable_sort(p->rows.begin(), p->rows.end(), [&](const StatRow& a, const StatRow& b) {
    const auto ka = key(a), kb = key(b);
    if (ka.first != kb.first) return ka.first < kb.first;
    if (!ka.first) return false;
    const int c = ka.second.compare(kb.second);
    return descending ? c > 0 : c < 0;
  });
  replace(p);
  menu(Screen::Data);
}
}  // namespace

void enter() {
  syncEpoch();
  state.active = true;
  state.shared = false;
  menu(Screen::Type, twoVar() ? 1 : 0);
}

void pause() {
  invalidate();
  state.edit.abort();
  state.active = false;
  state.shared = false;
  menu(Screen::Data);
}

bool service() {
  if (session().app != AppId::Statistics) {
    if (state.dataset) {
      state.dataset.reset();
      return true;
    }
    return false;
  }
  if (!state.active) return false;
  if (state.epoch != session().statEpoch) {
    syncEpoch();
    return true;
  }
  return false;
}

void press(K key) {
  syncEpoch();
  if (state.shared) {
    CalculateApp::press(key);
    state.shared = CalculateApp::sharedActive();
    return;
  }
  if (state.screen == Screen::Error) {
    if (key == K::Return || key == K::Clear || key == K::Okay) {
      state.screen = state.errorOrigin;
      state.error = Error::None;
    }
    return;
  }
  if (jobs().running()) invalidate();
  if (key == K::Interrupted) {
    invalidate();
    state.edit.abort();
    return;
  }
  // Cell editor active on the Data grid.
  if (state.edit.active) {
    if(QuickOverlay::open(state.edit.editor,key,AppId::Statistics))return;
    if (key == K::Return) {
      state.edit.abort();
      return;
    }
    if (key == K::Execute || key == K::Okay) {
      if (jobs().running() || state.edit.editor.text().empty()) return;
      start(StatisticsJobKind::EditCell, state.edit.editor.text());
      return;
    }
    state.edit.editor.press(key);
    return;
  }
  switch (state.screen) {
    case Screen::Type: {
      menuShortcut(key,state.selection,2);
      if (key == K::Up || key == K::Down) {
        state.selection = 1 - state.selection;
        return;
      }
      if (key == K::Okay || key == K::Execute) {
        const StatisticsMode next = state.selection == 0 ? StatisticsMode::OneVariable : StatisticsMode::TwoVariable;
        if (next != state.mode) {
          state.clearMode = next;
          requestClear(ClearPending::ModeSwitch, Screen::Type);
          return;
        }
        state.cursor = {};
        menu(Screen::Data);
        return;
      }
      return;
    }
    case Screen::Data: {
      if (moveGrid(state.cursor, policy(), key)) return;
      if (key == K::Tools) {
        menu(Screen::Tools);
        return;
      }
      if (key == K::Okay || key == K::Execute) {
        menu(Screen::Dispatch);
        return;
      }
      if (key == K::Settings || key == K::Function || key == K::Variable) {
        ResultSummary summary;
        const ResultSummary* selected = nullptr;
        if (static_cast<size_t>(state.cursor.row) < rowCount()) {
          const auto& r = state.dataset->rows[state.cursor.row];
          const int c = state.cursor.col;
          const bool has = c == 0 ? r.hasX : c == 1 && twoVar() ? r.hasY : r.hasFreq;
          if (has) {
            summary.value = Value(c == 0 ? r.x : c == 1 && twoVar() ? r.y : r.freq);
            selected = &summary;
          }
        }
        CalculateApp::openShared(AppId::Statistics, key, selected);
        state.shared = true;
        return;
      }
      if (key == K::Delete) {
        deleteRow();
        return;
      }
      if (key == K::Clear) return;  // AC never clears the dataset outside Delete All
      if ((key >= K::Digit0 && key <= K::Digit9) || key == K::Dot || key == K::Subtract ||
          key == K::Fraction || key == K::Root || key == K::Open || key == K::Ans || key == K::Shift) {
        beginEdit(state.cursor.row, state.cursor.col);
        state.edit.editor.press(key);
      }
      return;
    }
    case Screen::Calc: {
      if(QuickOverlay::open(state.calc,key,AppId::Statistics))return;
      if (key == K::Tools) {
        menu(Screen::Dispatch);
        state.returnTo = Screen::Calc;
        // Calc TOOLS offers result navigation, not data dispatch.
        return;
      }
      if (key == K::Catalog) {
        state.catalog = 0;
        state.catalogFirst = 0;
        state.entry = 0;
        state.entryFirst = 0;
        menu(Screen::Catalog);
        return;
      }
      if (key == K::Return) {
        menu(Screen::Data);
        return;
      }
      if (key == K::Variable) {
        ResultSummary summary;
        const ResultSummary* selected = nullptr;
        if (state.calcHasResult) {
          summary.value = Value(state.calcAns);
          selected = &summary;
        }
        CalculateApp::openShared(AppId::Statistics, key, selected);
        state.shared = true;
        return;
      }
      if (key == K::Execute || key == K::Okay) {
        if (jobs().running() || state.calc.text().empty()) return;
        start(StatisticsJobKind::Calculate, state.calc.text());
        return;
      }
      state.calc.press(key);
      return;
    }
    case Screen::CalcResult: {
      if (key == K::Clear) state.calc.begin("");
      menu(Screen::Calc);
      return;
    }
    case Screen::Results: {
      const std::vector<ResultRow> data = state.showRegList ? regData() : resultData();
      if (key == K::Return || key == K::Clear) {
        menu(state.returnTo);
        return;
      }
      if (key == K::Up || key == K::PageUp) {
        if (state.resultSelection > 0) {
          state.resultSelection -= (key == K::PageUp ? kResultVisibleRows : 1);
          if (state.resultSelection < 0) state.resultSelection = 0;
        }
        state.resultFirst = UiKit::ensureVisible(state.resultFirst, state.resultSelection,
                                                 static_cast<int>(data.size()), kResultVisibleRows);
        return;
      }
      if (key == K::Down || key == K::PageDown) {
        if (!data.empty() && static_cast<size_t>(state.resultSelection) + 1 < data.size()) {
          state.resultSelection += (key == K::PageDown ? kResultVisibleRows : 1);
          if (static_cast<size_t>(state.resultSelection) >= data.size())
            state.resultSelection = static_cast<int>(data.size()) - 1;
        }
        state.resultFirst = UiKit::ensureVisible(state.resultFirst, state.resultSelection,
                                                 static_cast<int>(data.size()), kResultVisibleRows);
        return;
      }
      if (key == K::Variable) {
        // Store the displayed result value when it is a plain quantity.
        // Grid-cell Store is the manual-confirmed path; result Store stays
        // PENDING, so only valid rows offer a value.
        ResultSummary summary;
        const ResultSummary* selected = nullptr;
        if (!data.empty() && state.resultSelection >= 0 &&
            static_cast<size_t>(state.resultSelection) < data.size() &&
            data[state.resultSelection].valid) {
          summary.value = Value(data[state.resultSelection].value);
          selected = &summary;
        }
        CalculateApp::openShared(AppId::Statistics, key, selected);
        state.shared = true;
        return;
      }
      return;
    }
    default:
      break;
  }
  // Menu screens: Type handled above; Tools/Frequency/Edit/Sort/Dispatch/
  // RegType/Catalog/CatalogGroup/Confirm share navigation + dispatch.
  if (state.screen == Screen::CatalogGroup) {
    const auto entries = catalogEntries();
    if (entries.empty()) {
      menu(Screen::Catalog);
      return;
    }
    if (key == K::Return || key == K::Clear) {
      menu(Screen::Catalog);
      return;
    }
    menuShortcut(key,state.entry,entries.size());
    if (key == K::Up)
      state.entry = (state.entry + static_cast<int>(entries.size()) - 1) % entries.size();
    else if (key == K::Down)
      state.entry = (state.entry + 1) % entries.size();
    else if (key == K::Okay || key == K::Execute) {
      const auto& e = entries[state.entry];
      if (e.wrapOperand)
        state.calc.wrap(e.insert);
      else if (e.isTemplate)
        state.calc.insertTemplate(e.insert);
      else
        state.calc.insert(e.insert);
      menu(Screen::Calc);
    }
    state.entryFirst =
        UiKit::ensureVisible(state.entryFirst, state.entry, static_cast<int>(entries.size()), kMenuVisibleRows);
    return;
  }
  if (key == K::Return || key == K::Clear) {
    // Every menu returns to its caller, never to a hard-coded screen.
    if (state.screen == Screen::Frequency || state.screen == Screen::Edit || state.screen == Screen::Sort)
      menu(Screen::Tools);
    else if (state.screen == Screen::RegType)
      menu(state.regFrom == RegFrom::Calc ? Screen::Calc : Screen::Dispatch);
    else if (state.screen == Screen::Dispatch)
      menu(state.returnTo == Screen::Calc ? Screen::Calc : Screen::Data);
    else if (state.screen == Screen::Catalog)
      menu(Screen::Calc);
    else if (state.screen == Screen::Confirm)
      menu(state.clearReturn);
    else
      menu(Screen::Data);
    return;
  }
  const auto list = rows();
  if (list.empty()) return;
  menuShortcut(key,state.selection,list.size());
  if (key == K::Up)
    state.selection = (state.selection + static_cast<int>(list.size()) - 1) % list.size();
  else if (key == K::Down)
    state.selection = (state.selection + 1) % list.size();
  else if (key == K::Okay || key == K::Execute) {
    const int action = list[state.selection].action;
    switch (state.screen) {
      case Screen::Tools:
        if (action == 0)
          menu(Screen::Frequency, state.frequency ? 0 : 1);
        else if (action == 1)
          menu(Screen::Edit);
        else
          menu(Screen::Sort);
        break;
      case Screen::Frequency: {
        const bool next = action == 0;
        if (next != state.frequency) {
          state.clearFreq = next;
          requestClear(ClearPending::FreqToggle, Screen::Frequency);
          break;
        }
        state.cursor = {};
        menu(Screen::Data);
        break;
      }
      case Screen::Edit:
        if (action == 0)
          insertRow();
        else
          requestClear(ClearPending::DeleteAll, Screen::Data);
        break;
      case Screen::Sort:
        sortRows(action);
        break;
      case Screen::Dispatch: {
        const bool fromCalc = state.returnTo == Screen::Calc;
        if (!twoVar()) {
          if (!fromCalc && action == 1) {
            menu(Screen::Calc);
            break;
          }
          ensureSummary(fromCalc ? Screen::Calc : Screen::Data, false);
          break;
        }
        if (!fromCalc) {
          if (action == 0)
            ensureSummary(Screen::Data, false);
          else if (action == 1) {
            state.regFrom = RegFrom::Dispatch;
            menu(Screen::RegType, static_cast<int>(state.regression));
          } else
            menu(Screen::Calc);
        } else {
          if (action == 2) {
            state.regFrom = RegFrom::Calc;
            menu(Screen::RegType, static_cast<int>(state.regression));
          } else
            ensureSummary(Screen::Calc, action == 1);
        }
        break;
      }
      case Screen::RegType:
        state.regression = static_cast<RegressionType>(action);
        if (state.regFrom == RegFrom::Calc) {
          menu(Screen::Calc);
        } else {
          ensureSummary(Screen::Data, true);
        }
        break;
      case Screen::Catalog:
        state.catalog = action;
        state.entry = 0;
        state.entryFirst = 0;
        menu(Screen::CatalogGroup);
        break;
      case Screen::Confirm:
        if (action == 0)
          runPendingClear();
        else
          menu(state.clearReturn);
        break;
      default:
        break;
    }
  }
  state.first = UiKit::ensureVisible(state.first, state.selection, static_cast<int>(rows().size()), kMenuVisibleRows);
}

void adopt(const Job& job, Result& output) {
  if (state.shared) {
    CalculateApp::adopt(job, output);
    return;
  }
  if (!state.active || !state.pending || session().app != AppId::Statistics || state.epoch != session().statEpoch ||
      job.ownerRevision != state.revision || !job.statistics)
    return;
  state.pending = false;
  const StatisticsTask& task = *job.statistics;
  if (task.kind == StatisticsJobKind::EditCell) {
    if (output.error == Error::Cancelled) return;  // silent drop, never a modal
    if (!state.edit.active || state.target.revision != state.revision ||
        job.request.expression != state.edit.editor.text())
      return;
    if (output.error != Error::None || !output.statistics) {
      showError(output.error != Error::None ? output.error : Error::Math);
      return;
    }
    auto p = copyRows();
    if (static_cast<size_t>(state.cursor.row) >= p->rows.size()) {
      if (p->rows.size() >= capacity()) {
        // Provisional Range label; exact target behavior is PENDING.
        showError(Error::Range);
        return;
      }
      p->rows.reserve(p->rows.size() + 1);
      p->rows.emplace_back();
    }
    StatRow& row = p->rows[state.cursor.row];
    const Decimal v = output.statistics->editValue;
    if (task.col == 0) {
      row.x = v;
      row.hasX = true;
      // CW capture: entering x with Frequency On seeds Freq=1 on that row.
      if (state.frequency && !row.hasFreq) {
        row.freq = Decimal(1);
        row.hasFreq = true;
      }
    } else if (task.col == 1 && twoVar()) {
      row.y = v;
      row.hasY = true;
      // CW capture: entering y with Frequency On seeds Freq=1 on that row.
      if (state.frequency && !row.hasFreq) {
        row.freq = Decimal(1);
        row.hasFreq = true;
      }
    } else if (task.col == (twoVar() ? 2 : 1)) {
      row.freq = v;
      row.hasFreq = true;
    } else {
      showError(Error::Argument);
      return;
    }
    invalidate();
    state.dataset = std::move(p);
    state.edit.abort();
    state.summary = StatsSummary{};
    state.fit = RegressionFit{};
    state.computedRevision = 0xFFFFFFFF;
    ++state.cursor.row;
    clampGrid(state.cursor, policy());
    state.screen = Screen::Data;
    return;
  }
  if (task.kind == StatisticsJobKind::Calculate) {
    if (output.error == Error::Cancelled) return;  // silent drop, never a modal
    if (state.screen != Screen::Calc || job.request.expression != state.calc.text()) return;
    if (output.error != Error::None) {
      showError(output.error);
      return;
    }
    state.calcExpr = job.request.expression;
    state.calcValue = formatValue(output.value, session().settings);
    state.calcAns = output.value.number;
    state.calcHasResult = true;
    menu(Screen::CalcResult);
    return;
  }
  // Summary.
  if (output.error == Error::Cancelled) return;  // silent drop, never a modal
  if (!output.statistics || output.error != Error::None) {
    if (output.error != Error::None && state.screen == Screen::Results) showError(output.error);
    return;
  }
  if (task.twoVariable != twoVar() || task.frequency != state.frequency) return;
  state.summary = output.statistics->summary;
  state.fit = output.statistics->regression;
  state.computedRevision = state.revision;
  state.computedReg = task.regression;
  state.resultSelection = state.resultFirst = 0;
  if (state.showRegList && state.screen == Screen::Results && !state.fit.defined) {
    // A failed regression leaves the raw sums usable but cannot show Reg
    // Results: provisional modal Math ERROR (exact target UX is PENDING).
    showError(state.fit.error != Error::None ? state.fit.error : Error::Math);
  }
}

bool caretVisible() {
  if (state.shared) return CalculateApp::caretVisible();
  return (state.edit.active || state.screen == Screen::Calc) && !jobs().running();
}

std::vector<uint8_t> render(bool caretOn) {
  if (state.shared) return CalculateApp::render(caretOn);
  if (state.screen == Screen::Error) return renderErrorDialog(state.error);
  if (jobs().running()) return Busy::renderBusy();
  // Data grid. An active cell entry echoes in the bottom band (draft +
  // caret) while the grid stays on screen (CW capture); it never takes
  // over the display the way a full-screen assignment field does.
  const bool editing = state.edit.active;
  switch (state.screen) {
    case Screen::Type:
      return renderMenuList(rows(), state.selection, state.first);
    case Screen::Tools:
    case Screen::Frequency:
    case Screen::Edit:
    case Screen::Sort:
    case Screen::Dispatch:
    case Screen::RegType:
      return renderMenuList(rows(), state.selection, state.first);
    case Screen::Confirm:
      return renderConfirm("Clear Data?", rows(), state.selection + 1, state.first);
    case Screen::Catalog:
      return renderMenuList(rows(), state.selection, state.first);
    case Screen::CatalogGroup: {
      const auto entries = catalogEntries();
      std::vector<MenuItem> items;
      for (size_t i = 0; i < entries.size(); ++i) items.push_back({static_cast<int>(i), entries[i].label, false});
      return renderMenuList(items, state.entry, state.entryFirst);
    }
    case Screen::Results: {
      if (!state.summary.defined) return renderDialog("No Data");
      if (state.showRegList) return renderConfirm(regressionEquation(state.computedReg), regRows(),
                                                  state.resultSelection + 1, state.resultFirst);
      const auto list = resultRows();
      return renderResultList(list, state.resultSelection, state.resultFirst);
    }
    case Screen::Calc:
      return renderAssignment("", state.calc, session().settings, false, state.calcView, caretOn);
    case Screen::CalcResult:
      return renderSolution(state.calcExpr, state.calcValue, session().settings);
    default:
      break;
  }
  // Data grid.
  DataGridView v;
  v.statistics = true;
  v.editor = editing ? &state.edit.editor : nullptr;
  v.caretOn = caretOn;
  for (int c = 0; c < columns(); ++c) v.headers.push_back(headerAt(c));
  if (state.dataset)
    for (const auto& r : state.dataset->rows)
      for (int c = 0; c < columns(); ++c) v.cells.push_back(cellText(r, c));
  if (rowCount() < capacity())
    for (int c = 0; c < columns(); ++c) v.cells.push_back("");
  v.cursor = state.cursor;
  if (editing)
    v.footer = state.edit.editor.text();
  else if (static_cast<size_t>(state.cursor.row) < rowCount())
    v.footer = cellText(state.dataset->rows[state.cursor.row], state.cursor.col);
  return renderDataGrid(v);
}

PanelIcons::State icons() {
  if (state.shared) return CalculateApp::icons();
  IconView v;
  // Statistics profile: angle + FIX/SCI + E where applicable, no MATHIO/CHECK
  // (nine-apps plan section 17). SHIFT reflects an active cell entry.
  v.calculation = true;
  v.aggregate = true;
  v.angle = session().settings.angle;
  v.io = session().settings.io;
  v.number = session().settings.number;
  v.engineer = session().settings.engineer;
  v.shifted = (state.edit.active && state.edit.editor.shifted) || state.calc.shifted;
  return desiredIcons(v);
}

size_t stateBytes() { return sizeof(State); }
size_t heapBytes() {
  return state.dataset ? sizeof(StatisticsDataset) + state.dataset->rows.capacity() * sizeof(StatRow) : 0;
}

#ifdef CALCULATOR_REFERENCE_TEST
const StatisticsDataset* data() { return state.dataset.get(); }
CalculatorApp::Snapshot snapshot() {
  if (state.shared) return CalculateApp::snapshot();
  CalculatorApp::Snapshot out;
  const char* names[] = {"StatisticsType",       "Statistics",     "StatisticsTools", "StatisticsFrequency",
                         "StatisticsEdit",       "StatisticsSort", "StatisticsDispatch", "StatisticsRegType",
                         "StatisticsResults",    "StatisticsCalc", "StatisticsCalcResult", "StatisticsCatalog",
                         "StatisticsCatalogGroup", "StatisticsConfirm", "ErrorDialog"};
  out.screen = names[static_cast<unsigned>(state.screen)];
  out.selection = state.selection;
  out.first = state.first;
  out.gridRow = state.cursor.row;
  out.gridCol = state.cursor.col;
  out.rows = static_cast<int>(rowCount());
  out.cols = columns();
  out.editing = state.edit.active;
  out.text = state.screen == Screen::Calc          ? state.calc.text()
             : state.screen == Screen::CalcResult ? state.calcExpr
             : state.edit.active                  ? state.edit.editor.text()
                                                  : "";
  if (state.screen == Screen::Results) {
    const auto list = state.showRegList ? regRows() : resultRows();
    for (const auto& r : list) out.menuLabels.push_back(r.label);
    out.resultText = "";
    out.hasResult = state.summary.defined;
    out.page = state.resultSelection;
  } else {
    for (const auto& r : rows()) out.menuLabels.push_back(r.label);
    if (state.screen == Screen::CatalogGroup) {
      out.menuLabels.clear();
      for (const auto& e : catalogEntries()) out.menuLabels.push_back(e.label);
    }
    if (state.screen == Screen::CalcResult) {
      out.hasResult = true;
      out.resultText = state.calcValue;
    }
  }
  if (state.dataset)
    for (const auto& r : state.dataset->rows)
      for (int c = 0; c < columns(); ++c) out.cells.push_back(cellText(r, c));
  if (state.screen == Screen::Error) {
    out.errorTitle = errorText(state.error);
    out.errorOrigin = names[static_cast<unsigned>(state.errorOrigin)];
  }
  return out;
}
#endif
}  // namespace StatisticsApp
}  // namespace Calculator
