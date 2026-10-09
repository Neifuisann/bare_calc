#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/apps/calculator_distribution.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/apps/calculator_calculate.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_render.h"
#include <algorithm>

namespace Calculator {

bool operator==(const DistributionParameters& a, const DistributionParameters& b) {
  return a.x == b.x && a.n == b.n && a.p == b.p && a.mu == b.mu && a.sigma == b.sigma &&
         a.lower == b.lower && a.upper == b.upper && a.area == b.area && a.lambda == b.lambda;
}

namespace {

const char* typeName(DistributionType t) {
  switch (t) {
    case DistributionType::BinomialPD: return "Binomial PD";
    case DistributionType::BinomialCD: return "Binomial CD";
    case DistributionType::NormalPD: return "Normal PD";
    case DistributionType::NormalCD: return "Normal CD";
    case DistributionType::InverseNormal: return "Inverse Normal";
    case DistributionType::PoissonPD: return "Poisson PD";
    case DistributionType::PoissonCD: return "Poisson CD";
  }
  return "";
}

// Session parameter slots. Values travel by name across types (880-MAN).
enum Param : int { Px = 0, PN, Pp, Pmu, Psigma, PLower, PUpper, PArea, Plambda };

const Decimal& paramValue(const DistributionParameters& p, int param) {
  switch (param) {
    case Px: return p.x;
    case PN: return p.n;
    case Pp: return p.p;
    case Pmu: return p.mu;
    case Psigma: return p.sigma;
    case PLower: return p.lower;
    case PUpper: return p.upper;
    case PArea: return p.area;
    default: return p.lambda;
  }
}

void setParamValue(DistributionParameters& p, int param, const Decimal& v) {
  switch (param) {
    case Px: p.x = v; break;
    case PN: p.n = v; break;
    case Pp: p.p = v; break;
    case Pmu: p.mu = v; break;
    case Psigma: p.sigma = v; break;
    case PLower: p.lower = v; break;
    case PUpper: p.upper = v; break;
    case PArea: p.area = v; break;
    default: p.lambda = v; break;
  }
}

const char* paramName(int param) {
  // Stable field identifiers; the form renderer paints Greek symbol labels.
  switch (param) {
    case Px: return "x";
    case PN: return "N";
    case Pp: return "p";
    case Pmu: return "mu";
    case Psigma: return "sigma";
    case PLower: return "Lower";
    case PUpper: return "Upper";
    case PArea: return "Area";
    default: return "lambda";
  }
}

struct Validated {
  Error error = Error::None;
  int64_t n = 0, k = 0;
  Decimal p, mu, sigma, lower, upper, area, lambda, x;
};

// Shared parameter validation. Domain rejections are provisional Math (or
// Argument for Lower>Upper); exact target labels are PENDING, only the
// domains 0<=p<=1, sigma>0, 0<=Area<=1 are 880-MAN.
Validated validateParams(DistributionType type, const DistributionParameters& params) {
  Validated v;
  auto finite = [&](const Decimal& d) { return d.finite(); };
  v.x = params.x;
  v.p = params.p;
  v.mu = params.mu;
  v.sigma = params.sigma;
  v.lower = params.lower;
  v.upper = params.upper;
  v.area = params.area;
  v.lambda = params.lambda;
  if (!finite(v.x) || !finite(v.p) || !finite(v.mu) || !finite(v.sigma) || !finite(v.lower) ||
      !finite(v.upper) || !finite(v.area) || !finite(v.lambda) || !params.n.finite()) {
    v.error = Error::Math;
    return v;
  }
  switch (type) {
    case DistributionType::BinomialPD:
    case DistributionType::BinomialCD:
      if (!params.n.integer() || !params.n.toInt(v.n) || v.n < 0) {
        v.error = Error::Math;
        return v;
      }
      if (v.p < Decimal(0) || v.p > Decimal(1)) v.error = Error::Math;
      return v;
    case DistributionType::NormalPD:
      if (!(v.sigma > Decimal(0))) v.error = Error::Math;
      return v;
    case DistributionType::NormalCD:
      if (!(v.sigma > Decimal(0))) {
        v.error = Error::Math;
        return v;
      }
      if (v.lower > v.upper) v.error = Error::Argument;  // provisional; O-2 PENDING
      return v;
    case DistributionType::InverseNormal:
      if (!(v.sigma > Decimal(0))) {
        v.error = Error::Math;
        return v;
      }
      if (v.area < Decimal(0) || v.area > Decimal(1)) v.error = Error::Math;
      return v;
    case DistributionType::PoissonPD:
    case DistributionType::PoissonCD:
      if (v.lambda < Decimal(0)) v.error = Error::Math;
      return v;
  }
  v.error = Error::Math;
  return v;
}

// Discrete x for list/variable evaluation. ok=false means per-row ERROR in a
// list; variable mode maps it to a modal Math ERROR. Integer/zero-floor
// acceptance is provisional (O-2 PENDING).
bool discreteX(const Decimal& x, int64_t& k) {
  if (!x.finite() || !x.integer() || !x.toInt(k) || k < 0) return false;
  return true;
}

Probability::Outcome evaluateOne(DistributionType type, const Validated& v, const Decimal& x,
                                const std::function<bool()>& cancelled,
                                const std::function<void()>& cooperate, bool& rowError) {
  using Probability::Outcome;
  using Probability::Status;
  rowError = false;
  switch (type) {
    case DistributionType::BinomialPD: {
      int64_t k = 0;
      if (!discreteX(x, k) || k > v.n) {
        rowError = true;
        return {Decimal(0), Status::Domain};
      }
      return Probability::binomialPmf(v.n, k, v.p, cancelled, cooperate);
    }
    case DistributionType::BinomialCD: {
      int64_t k = 0;
      // Per-row x domain is 0..N for PD and CD alike (provisional PENDING:
      // the manual proves per-row ERROR but does not separate PD/CD).
      if (!discreteX(x, k) || k > v.n) {
        rowError = true;
        return {Decimal(0), Status::Domain};
      }
      return Probability::binomialCdf(v.n, k, v.p, cancelled, cooperate);
    }
    case DistributionType::NormalPD:
      return Probability::normalPdf(x, v.mu, v.sigma);
    case DistributionType::NormalCD: {
      // Stable tail subtraction: compute in the smaller tail, never
      // CDF(upper)-CDF(lower) of two nearly-1 values.
      Outcome lo = Probability::normalCdf(v.lower, v.mu, v.sigma, cancelled, cooperate);
      if (lo.status != Status::Ok) return lo;
      Outcome hi = Probability::normalCdf(v.upper, v.mu, v.sigma, cancelled, cooperate);
      if (hi.status != Status::Ok) return hi;
      return {hi.value - lo.value, Status::Ok};
    }
    case DistributionType::InverseNormal:
      return Probability::normalInv(v.area, v.mu, v.sigma, cancelled, cooperate);
    case DistributionType::PoissonPD: {
      int64_t k = 0;
      if (!discreteX(x, k)) {
        rowError = true;
        return {Decimal(0), Status::Domain};
      }
      return Probability::poissonPmf(k, v.lambda, cancelled, cooperate);
    }
    case DistributionType::PoissonCD: {
      int64_t k = 0;
      if (!discreteX(x, k)) {
        rowError = true;
        return {Decimal(0), Status::Domain};
      }
      return Probability::poissonCdf(k, v.lambda, cancelled, cooperate);
    }
  }
  return {Decimal(0), Status::Domain};
}

Error statusError(Probability::Status s) {
  return s == Probability::Status::Domain          ? Error::Math
         : s == Probability::Status::NoConvergence ? Error::TimeOut
                                                   : Error::Cancelled;
}

}  // namespace

Result evaluateDistribution(const DistributionTask& task, const Request& request,
                            const std::function<bool()>& cancelled,
                            const std::function<void()>& cooperate) {
  Result output;
  if (task.kind == DistributionJobKind::EditX || task.kind == DistributionJobKind::EditParam) {
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
    auto payload = std::make_shared<DistributionResult>();
    payload->kind = task.kind;
    payload->editValue = v.value.number;
    payload->editParam = task.param;
    output.distribution = std::move(payload);
    return output;
  }
  Validated v = validateParams(task.type, task.params);
  if (v.error != Error::None) {
    output.error = v.error;  // parameter-level invalidity: modal error
    return output;
  }
  auto payload = std::make_shared<DistributionResult>();
  if (task.kind == DistributionJobKind::ExecuteScalar) {
    if (task.type == DistributionType::NormalCD || task.type == DistributionType::InverseNormal) {
      bool rowError = false;
      Probability::Outcome o = evaluateOne(task.type, v, Decimal(0), cancelled, cooperate, rowError);
      if (o.status != Probability::Status::Ok) {
        output.error = statusError(o.status);
        return output;
      }
      payload->kind = DistributionJobKind::ExecuteScalar;
      payload->scalar = o.value;
      output.distribution = std::move(payload);
      return output;
    }
    int64_t k = 0;
    if ((task.type == DistributionType::BinomialPD || task.type == DistributionType::BinomialCD ||
         task.type == DistributionType::PoissonPD || task.type == DistributionType::PoissonCD)) {
      if (!discreteX(task.params.x, k)) {
        output.error = Error::Math;  // variable-mode domain: modal (PENDING label)
        return output;
      }
      if (task.type == DistributionType::BinomialPD && k > v.n) {
        output.error = Error::Math;
        return output;
      }
      if (task.type == DistributionType::BinomialCD && k > v.n) {
        // Provisional PENDING: uniform per-row domain (see evaluateOne).
        output.error = Error::Math;
        return output;
      }
    }
    bool rowError = false;
    Probability::Outcome o = evaluateOne(task.type, v, task.params.x, cancelled, cooperate, rowError);
    if (o.status != Probability::Status::Ok) {
      output.error = statusError(o.status);
      return output;
    }
    payload->kind = DistributionJobKind::ExecuteScalar;
    payload->scalar = o.value;
    output.distribution = std::move(payload);
    return output;
  }
  // ExecuteList: one job for the whole list; per-row domain failures become
  // ERROR cells without aborting valid rows (880-MAN).
  if (!task.list) {
    output.error = Error::Math;
    return output;
  }
  payload->kind = DistributionJobKind::ExecuteList;
  payload->rows.reserve(task.list->rows.size());
  size_t n = 0;
  for (const auto& row : task.list->rows) {
    if (cancelled && cancelled()) {
      output.error = Error::Cancelled;
      return output;
    }
    DistributionRowResult r;
    if (!row.hasX) {
      r.error = true;
    } else {
      bool rowError = false;
      Probability::Outcome o = evaluateOne(task.type, v, row.x, cancelled, cooperate, rowError);
      if (o.status == Probability::Status::Ok) {
        r.p = o.value;
      } else if (o.status == Probability::Status::Domain || rowError) {
        r.error = true;
      } else {
        output.error = statusError(o.status);  // TimeOut/Cancelled aborts the job
        return output;
      }
    }
    payload->rows.push_back(r);
    if ((++n & 7) == 0 && cooperate) cooperate();
  }
  output.distribution = std::move(payload);
  return output;
}

// ---- Controller ------------------------------------------------------------

namespace DistributionApp {
namespace {
using K = ExpressionInput::Key;
enum class Screen : uint8_t { Type, Method, List, Params, Scalar, Edit, Confirm, Error };

struct State {
  Screen screen = Screen::Type, errorOrigin = Screen::Type;
  DistributionType type = DistributionType::BinomialPD;
  bool listMode = true;
  std::shared_ptr<DistributionList> list;
  GridCursor cursor;
  CellEdit edit;
  InputTarget target;
  Viewport viewport;
  uint32_t revision = 0, epoch = 0;
  int selection = 0, first = 0;
  bool resultsShown = false;
  Decimal scalarResult;
  bool shared = false, pending = false, active = false;
  Error error = Error::None;
} state;

CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }

size_t rowCount() { return state.list ? state.list->rows.size() : 0; }
GridPolicy policy() { return {static_cast<int>(std::min<size_t>(45, rowCount() + 1)), 2, 4, 2}; }
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
void clearList() {
  invalidate();
  state.list.reset();
  state.cursor = {};
  state.edit.abort();
  state.resultsShown = false;
}
void clearResults() {
  // Any list edit while results show returns to completed-input state (880-MAN).
  if (!state.resultsShown) return;
  invalidate();
  state.resultsShown = false;
  if (state.list)
    for (auto& r : state.list->rows) {
      r.hasP = false;
      r.error = false;
    }
}
void syncEpoch() {
  if (state.epoch != session().distEpoch) {
    state.epoch = session().distEpoch;
    clearList();
    menu(Screen::List);
  }
}
std::shared_ptr<DistributionList> copyRows() {
  auto p = std::make_shared<DistributionList>();
  if (state.list) p->rows = state.list->rows;
  return p;
}
void replace(std::shared_ptr<DistributionList> p) {
  invalidate();
  state.list = std::move(p);
  state.edit.abort();
  clampGrid(state.cursor, policy());
}

// Editable parameter rows for the current type/method. List mode omits x.
std::vector<int> paramRows() {
  const bool list = state.listMode;
  switch (state.type) {
    case DistributionType::BinomialPD:
    case DistributionType::BinomialCD:
      return list ? std::vector<int>{PN, Pp} : std::vector<int>{Px, PN, Pp};
    case DistributionType::NormalPD:
      return {Px, Pmu, Psigma};
    case DistributionType::NormalCD:
      return {PLower, PUpper, Pmu, Psigma};
    case DistributionType::InverseNormal:
      return {PArea, Pmu, Psigma};
    case DistributionType::PoissonPD:
    case DistributionType::PoissonCD:
      return list ? std::vector<int>{Plambda} : std::vector<int>{Px, Plambda};
  }
  return {};
}

std::vector<MenuItem> rows() {
  switch (state.screen) {
    case Screen::Type:
      return {{0, "Binomial PD", true},
              {1, "Binomial CD", true},
              {2, "Normal PD", false},
              {3, "Normal CD", false},
              {4, "Inverse Normal", false},
              {5, "Poisson PD", true},
              {6, "Poisson CD", true}};
    case Screen::Method:
      return {{0, "List", false}, {1, "Variable", false}};
    case Screen::Edit:
      return {{0, "Insert Row", false}, {1, "Delete All", false}};
    case Screen::Confirm:
      return {{0, "Yes", false}, {1, "Cancel", false}};
    case Screen::Params: {
      std::vector<MenuItem> v;
      for (int p : paramRows())
        v.push_back({p, std::string(paramName(p)) + "=" +
                          formatValue(Value(paramValue(session().distParams, p)), session().settings),
                   false});
      v.push_back({100, "Execute", false});
      return v;
    }
    default:
      return {};
  }
}

void start(DistributionJobKind kind, const std::string& expression = "") {
  if (jobs().running()) return;
  auto task = std::make_shared<DistributionTask>();
  task->kind = kind;
  task->type = state.type;
  task->listMode = state.listMode;
  task->params = session().distParams;
  if (kind == DistributionJobKind::ExecuteList)
    task->list = state.list ? std::shared_ptr<const DistributionList>(state.list)
                            : std::make_shared<DistributionList>();
  task->expression = expression;
  task->row = state.cursor.row;
  task->param = state.target.row;
  Job& job = prepareJob(AppId::Distribution, 0, expression);
  job.distribution = task;
  job.ownerRevision = state.revision;
  state.pending = jobs().start(job) == JobService::Start::Started;
  job.distribution.reset();  // the static scratch must not retain the list
  if (!state.pending) showError(Error::Stack);
}

void beginEdit(int row, int col, const std::string& text = "") {
  invalidate();
  state.target = {MenuOrigin::Cell, static_cast<int16_t>(row), static_cast<int16_t>(col), state.revision};
  state.edit.begin({row, col}, state.revision, text);
  state.viewport = {};
  // Parameter forms use one linear field on the selected row.
  state.edit.editor.setMathInput(col >= 0 && session().settings.io <= IO::MathDecimal);
}

std::string cellText(const DistRow& row, int col) {
  if (col == 0) return row.hasX ? formatValue(Value(row.x), session().settings) : "";
  if (row.error) return "ERROR";  // per-row domain ERROR (880-MAN)
  return row.hasP ? formatValue(Value(row.p), session().settings) : "";
}

void deleteRow() {
  if (static_cast<size_t>(state.cursor.row) >= rowCount()) return;
  clearResults();
  auto p = copyRows();
  p->rows.erase(p->rows.begin() + state.cursor.row);
  replace(p);
}

void insertRow() {
  if (rowCount() >= 45) {
    showError(Error::Range);  // provisional label; exact target error PENDING
    return;
  }
  clearResults();
  auto p = copyRows();
  DistRow blank;
  blank.x = Decimal(0);  // inserted rows default to 0 (880-MAN)
  blank.hasX = true;
  p->rows.reserve(p->rows.size() + 1);
  p->rows.insert(p->rows.begin() + std::min<size_t>(state.cursor.row, p->rows.size()), blank);
  replace(p);
  menu(Screen::List);
}
}  // namespace

void enter() {
  syncEpoch();
  state.active = true;
  state.shared = false;
  menu(Screen::Type, static_cast<int>(state.type));
}

void pause() {
  invalidate();
  state.edit.abort();
  state.active = false;
  state.shared = false;
  menu(Screen::List);
}

bool service() {
  if (session().app != AppId::Distribution) {
    if (state.list) {
      state.list.reset();
      return true;
    }
    return false;
  }
  if (!state.active) return false;
  if (state.epoch != session().distEpoch) {
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
  if (state.edit.active) {
    if(QuickOverlay::open(state.edit.editor,key,AppId::Distribution))return;
    if (key == K::Return) {
      state.edit.abort();
      return;
    }
    if (key == K::Execute || key == K::Okay) {
      if (jobs().running() || state.edit.editor.text().empty()) return;
      start(state.screen == Screen::Params ? DistributionJobKind::EditParam : DistributionJobKind::EditX,
            state.edit.editor.text());
      return;
    }
    state.edit.editor.press(key);
    return;
  }
  switch (state.screen) {
    case Screen::Type: {
      const auto list = rows();
      menuShortcut(key,state.selection,list.size());
      if (key == K::Up)
        state.selection = (state.selection + 6) % 7;
      else if (key == K::Down)
        state.selection = (state.selection + 1) % 7;
      else if (key == K::Okay || key == K::Execute) {
        state.type = static_cast<DistributionType>(state.selection);
        state.cursor = {};
        // The List/Variable menu never appears for the three Normal
        // operations (880-MAN): they always use Variable mode.
        if (distributionListCapable(state.type)) {
          menu(Screen::Method, state.listMode ? 0 : 1);
        } else {
          state.listMode = false;
          menu(Screen::Params);
        }
      }
      state.first = UiKit::ensureVisible(state.first, state.selection, static_cast<int>(list.size()),
                                         kMenuVisibleRows);
      return;
    }
    case Screen::Method: {
      menuShortcut(key,state.selection,2);
      if (key == K::Return || key == K::Clear) {
        menu(Screen::Type, static_cast<int>(state.type));
        return;
      }
      if (key == K::Up || key == K::Down) {
        state.selection = 1 - state.selection;
        return;
      }
      if (key == K::Okay || key == K::Execute) {
        state.listMode = state.selection == 0;
        state.cursor = {};
        menu(state.listMode ? Screen::List : Screen::Params);
      }
      return;
    }
    case Screen::List: {
      if (moveGrid(state.cursor, policy(), key)) return;
      if (key == K::Tools) {
        menu(Screen::Edit);
        return;
      }
      if (key == K::Okay || key == K::Execute) {
        menu(Screen::Params);
        return;
      }
      if (key == K::Settings || key == K::Function || key == K::Variable) {
        ResultSummary summary;
        const ResultSummary* selected = nullptr;
        if (static_cast<size_t>(state.cursor.row) < rowCount()) {
          const auto& r = state.list->rows[state.cursor.row];
          if (state.cursor.col == 0 && r.hasX) {
            summary.value = Value(r.x);
            selected = &summary;
          } else if (state.cursor.col == 1 && r.hasP && !r.error) {
            summary.value = Value(r.p);
            selected = &summary;
          }
        }
        CalculateApp::openShared(AppId::Distribution, key, selected);
        state.shared = true;
        return;
      }
      if (key == K::Delete) {
        deleteRow();
        return;
      }
      if (key == K::Clear) return;
      if (state.cursor.col == 0 &&
          ((key >= K::Digit0 && key <= K::Digit9) || key == K::Dot || key == K::Subtract ||
           key == K::Fraction || key == K::Root || key == K::Open || key == K::Ans || key == K::Shift)) {
        beginEdit(state.cursor.row, 0);
        state.edit.editor.press(key);
      }
      return;
    }
    case Screen::Params: {
      const auto params = paramRows();
      if (key == K::Return || key == K::Clear) {
        // Return to the list/method/type caller, never a hard-coded screen.
        if (state.listMode)
          menu(Screen::List);
        else if (distributionListCapable(state.type))
          menu(Screen::Method, 1);
        else
          menu(Screen::Type, static_cast<int>(state.type));
        return;
      }
      if (key == K::Up)
        state.selection = (state.selection + static_cast<int>(params.size())) % (params.size() + 1);
      else if (key == K::Down)
        state.selection = (state.selection + 1) % (params.size() + 1);
      else if ((key >= K::Digit0 && key <= K::Digit9) || key == K::Dot || key == K::Subtract) {
        if (static_cast<size_t>(state.selection) < params.size()) {
          beginEdit(params[state.selection], -1);
          state.edit.editor.press(key);
        }
        return;
      } else if (key == K::Okay || key == K::Execute) {
        if (static_cast<size_t>(state.selection) < params.size()) {
          beginEdit(params[state.selection], -1);
          return;
        }
        start(state.listMode ? DistributionJobKind::ExecuteList : DistributionJobKind::ExecuteScalar);
        return;
      }
      state.first = UiKit::ensureVisible(state.first, state.selection, static_cast<int>(params.size()) + 1,
                                         kDistributionFormRows);
      return;
    }
    case Screen::Scalar: {
      if (key == K::Variable && state.scalarResult.finite()) {
        ResultSummary summary;
        summary.value = Value(state.scalarResult);
        CalculateApp::openShared(AppId::Distribution, key, &summary);
        state.shared = true;
        return;
      }
      // EXE/Return/AC return to the parameter screen with values retained.
      menu(Screen::Params);
      return;
    }
    default:
      break;
  }
  if (key == K::Return || key == K::Clear) {
    if (state.screen == Screen::Edit)
      menu(Screen::List);
    else if (state.screen == Screen::Confirm)
      menu(Screen::List);
    else
      menu(Screen::List);
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
    if (state.screen == Screen::Edit) {
      if (action == 0)
        insertRow();
      else
        menu(Screen::Confirm);
    } else if (state.screen == Screen::Confirm) {
      // Native "Clear Data?" offers Yes first (selected) then Cancel.
      if (action == 0) clearList();
      menu(Screen::List);
    }
  }
  state.first = UiKit::ensureVisible(state.first, state.selection, static_cast<int>(list.size()), kMenuVisibleRows);
}

void adopt(const Job& job, Result& output) {
  if (state.shared) {
    CalculateApp::adopt(job, output);
    return;
  }
  if (!state.active || !state.pending || session().app != AppId::Distribution ||
      state.epoch != session().distEpoch || job.ownerRevision != state.revision || !job.distribution)
    return;
  state.pending = false;
  const DistributionTask& task = *job.distribution;
  if (task.type != state.type) return;
  if (task.kind == DistributionJobKind::EditX) {
    if (!state.edit.active || state.target.revision != state.revision ||
        job.request.expression != state.edit.editor.text())
      return;
    if (output.error != Error::None || !output.distribution) {
      showError(output.error != Error::None ? output.error : Error::Math);
      return;
    }
    clearResults();
    auto p = copyRows();
    if (static_cast<size_t>(state.cursor.row) >= p->rows.size()) {
      if (p->rows.size() >= 45) {
        showError(Error::Range);  // provisional label; PENDING exact target error
        return;
      }
      p->rows.reserve(p->rows.size() + 1);
      p->rows.emplace_back();
    }
    DistRow& row = p->rows[state.cursor.row];
    row.x = output.distribution->editValue;
    row.hasX = true;
    invalidate();
    state.list = std::move(p);
    state.edit.abort();
    ++state.cursor.row;
    clampGrid(state.cursor, policy());
    state.screen = Screen::List;
    return;
  }
  if (task.kind == DistributionJobKind::EditParam) {
    if (!state.edit.active || state.target.revision != state.revision ||
        job.request.expression != state.edit.editor.text() ||
        state.screen != Screen::Params)
      return;
    if (output.error != Error::None || !output.distribution) {
      showError(output.error != Error::None ? output.error : Error::Math);
      return;
    }
    setParamValue(session().distParams, task.param, output.distribution->editValue);
    ++session().revision;  // retained parameters persist through the debounce
    ++state.revision;
    state.edit.abort();
    if (static_cast<size_t>(state.selection) < paramRows().size())
      state.selection = std::min(state.selection + 1, static_cast<int>(paramRows().size()));
    state.first = UiKit::ensureVisible(state.first, state.selection,
        static_cast<int>(paramRows().size()) + 1, kDistributionFormRows);
    return;
  }
  if (task.params == session().distParams) {
    // Parameters moved under the job only for the execute kinds; Edit kinds
    // return above. A mismatch drops the result.
  } else if (task.kind == DistributionJobKind::ExecuteList ||
             task.kind == DistributionJobKind::ExecuteScalar) {
    return;
  }
  if (task.kind == DistributionJobKind::ExecuteScalar) {
    if (!output.distribution || output.error != Error::None) {
      showError(output.error != Error::None ? output.error : Error::Math);
      return;
    }
    state.scalarResult = output.distribution->scalar;
    session().memory.ans = ScalarValue(Value(state.scalarResult));  // Variable result in Ans (880-MAN)
    ++session().revision;
    menu(Screen::Scalar);
    return;
  }
  // ExecuteList.
  if (!output.distribution || output.error != Error::None) {
    if (output.error != Error::None) showError(output.error);
    return;
  }
  if (task.listMode != state.listMode || !state.list ||
      output.distribution->rows.size() != state.list->rows.size())
    return;
  for (size_t i = 0; i < state.list->rows.size(); ++i) {
    state.list->rows[i].p = output.distribution->rows[i].p;
    state.list->rows[i].hasP = !output.distribution->rows[i].error;
    state.list->rows[i].error = output.distribution->rows[i].error;
  }
  ++state.revision;  // P publication bumps the revision (edits after this clear P)
  state.resultsShown = true;
  state.screen = Screen::List;
}

bool caretVisible() {
  if (state.shared) return CalculateApp::caretVisible();
  return state.edit.active && !jobs().running();
}

std::vector<uint8_t> render(bool caretOn) {
  if (state.shared) return CalculateApp::render(caretOn);
  if (state.screen == Screen::Error) return renderErrorDialog(state.error);
  if (jobs().running()) return Busy::renderBusy();
  // List entry echoes below the grid; parameter entry stays on its own row.
  const bool editing = state.edit.active;
  const std::string draft = editing ? state.edit.editor.text() + (caretOn ? "_" : "") : std::string();
  switch (state.screen) {
    case Screen::Type:
    case Screen::Method:
    case Screen::Edit:
      return renderMenuList(rows(), state.selection, state.first);
    case Screen::Confirm:
      return renderConfirm("Clear Data?", rows(), state.selection + 1, state.first);
    case Screen::Params: {
      std::vector<DistributionField> fields;
      for (int p : paramRows())
        fields.push_back({paramName(p),formatValue(Value(paramValue(session().distParams,p)),session().settings)});
      return renderDistributionForm(typeName(state.type), fields, state.selection, state.first,
                                    editing ? &state.edit.editor : nullptr, state.viewport, caretOn);
    }
    case Screen::Scalar:
      return renderSolution(state.type == DistributionType::InverseNormal ? "xInv" : "P",
                            formatValue(Value(state.scalarResult), session().settings), session().settings);
    default:
      break;
  }
  // List grid: x editable, P read-only with per-row ERROR.
  DataGridView v;
  v.headers = {"x", "P"};
  v.context = typeName(state.type);
  if (state.list)
    for (const auto& r : state.list->rows) {
      v.cells.push_back(cellText(r, 0));
      v.cells.push_back(cellText(r, 1));
    }
  if (rowCount() < 45) {
    v.cells.push_back("");
    v.cells.push_back("");
  }
  v.cursor = state.cursor;
  if (editing)
    v.footer = draft;
  else if (state.list && static_cast<size_t>(state.cursor.row) < state.list->rows.size())
    v.footer = cellText(state.list->rows[state.cursor.row], state.cursor.col);
  return renderDataGrid(v);
}

PanelIcons::State icons() {
  if (state.shared) return CalculateApp::icons();
  IconView v;
  // Distribution profile: angle + FIX/SCI + E where applicable, no MATHIO/CHECK
  // (nine-apps plan section 17). SHIFT reflects an active entry.
  v.calculation = true;
  v.aggregate = true;
  v.angle = session().settings.angle;
  v.io = session().settings.io;
  v.number = session().settings.number;
  v.engineer = session().settings.engineer;
  v.shifted = state.edit.active && state.edit.editor.shifted;
  return desiredIcons(v);
}

size_t stateBytes() { return sizeof(State); }
size_t heapBytes() {
  return state.list ? sizeof(DistributionList) + state.list->rows.capacity() * sizeof(DistRow) : 0;
}

#ifdef CALCULATOR_REFERENCE_TEST
const DistributionList* data() { return state.list.get(); }
CalculatorApp::Snapshot snapshot() {
  if (state.shared) return CalculateApp::snapshot();
  CalculatorApp::Snapshot out;
  const char* names[] = {"DistributionType", "DistributionMethod", "DistributionList", "DistributionParams",
                         "DistributionResult", "DistributionEdit", "DistributionConfirm", "ErrorDialog"};
  out.screen = names[static_cast<unsigned>(state.screen)];
  out.selection = state.selection;
  out.first = state.first;
  out.gridRow = state.cursor.row;
  out.gridCol = state.cursor.col;
  out.rows = static_cast<int>(rowCount());
  out.cols = 2;
  out.editing = state.edit.active;
  out.text = state.edit.active ? state.edit.editor.text() : "";
  for (const auto& r : rows()) out.menuLabels.push_back(r.label);
  if (state.list)
    for (const auto& r : state.list->rows) {
      out.cells.push_back(cellText(r, 0));
      out.cells.push_back(cellText(r, 1));
    }
  if (state.screen == Screen::Scalar) {
    out.hasResult = true;
    out.resultText = formatValue(Value(state.scalarResult), session().settings);
  }
  if (state.screen == Screen::Error) {
    out.errorTitle = errorText(state.error);
    out.errorOrigin = names[static_cast<unsigned>(state.errorOrigin)];
  }
  return out;
}
#endif
}  // namespace DistributionApp
}  // namespace Calculator
