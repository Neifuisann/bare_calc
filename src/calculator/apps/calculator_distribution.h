#pragma once
#include "calculator/apps/calculator_calculate.h"
#include "calculator/math/calculator_decimal.h"
#include "calculator/math/calculator_engine.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/math/calculator_probability.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifdef CALCULATOR_REFERENCE_TEST
#include "calculator/runtime/calculator_app.h"
#endif

 // Distribution app (P17): the seven fx-880 menu types with List/Variable
 // input, a 45-row x/P list, named retained parameters and per-row ERROR.
 //
 // Evidence: 880-MAN using_calculator_apps__distribution_calculations (cached
 // HTML .txt). Seven type names/order, List-vs-Variable routing (no method
 // menu for the three Normal types), 45-row list, inserted-row default 0,
 // edit-after-result clears all P, per-row ERROR, Variable result in Ans,
 // six-significant-digit accuracy, parameter sharing by name, retention until
 // Settings&Data/Initialize All, and list clearing on other-app launch are
 // all MANUAL-CONFIRMED. Discrete x/N/lambda acceptance details, Lower>Upper
 // behavior, list-result Ans behavior and endpoint quantiles are PENDING.
namespace Calculator {

enum class DistributionType : uint8_t {
  BinomialPD, BinomialCD, NormalPD, NormalCD, InverseNormal, PoissonPD, PoissonCD
};

inline bool distributionListCapable(DistributionType t) {
  return t == DistributionType::BinomialPD || t == DistributionType::BinomialCD ||
         t == DistributionType::PoissonPD || t == DistributionType::PoissonCD;
}

// Named parameter bank. Retained across type/method switches until
// Settings&Data/Initialize All (880-MAN); lives in the session, never in
// controller-local state. See Calculator::DistributionParameters
// (calculator_session.h) for the provisional defaults (O-2 PENDING).

struct DistRow {
  Decimal x;
  Decimal p;
  bool hasX = false, hasP = false, error = false;
};

struct DistributionList {
  std::vector<DistRow> rows;
};

// Worker payloads. List jobs snapshot the x column plus the parameters;
// scalar jobs snapshot the parameters only. EditX/EditParam jobs carry no
// snapshots: adoption writes into live state iff the revision is unchanged.
enum class DistributionJobKind : uint8_t { ExecuteList, ExecuteScalar, EditX, EditParam };

struct DistributionTask {
  DistributionJobKind kind = DistributionJobKind::ExecuteList;
  DistributionType type = DistributionType::BinomialPD;
  bool listMode = true;
  std::shared_ptr<const DistributionList> list;
  DistributionParameters params;
  std::string expression;
  int row = 0, param = 0;
};

struct DistributionRowResult {
  Decimal p;
  bool error = false;
};

struct DistributionResult {
  DistributionJobKind kind = DistributionJobKind::ExecuteList;
  std::vector<DistributionRowResult> rows;
  Decimal scalar;
  Error scalarError = Error::None;
  Decimal editValue;
  Error editError = Error::None;
  int editParam = 0;
};

Result evaluateDistribution(const DistributionTask& task, const Request& request,
                            const std::function<bool()>& cancelled = {},
                            const std::function<void()>& cooperate = {});

namespace DistributionApp {
void enter();
void pause();
void press(ExpressionInput::Key key);
void adopt(const Job&, Result&);
bool service();
bool caretVisible();
std::vector<uint8_t> render(bool caretOn);
PanelIcons::State icons();
size_t stateBytes();
size_t heapBytes();
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
const DistributionList* data();
#endif
}  // namespace DistributionApp
}  // namespace Calculator
