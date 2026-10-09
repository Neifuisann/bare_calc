#pragma once
#include "calculator/apps/calculator_calculate.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/math/calculator_statistics_math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifdef CALCULATOR_REFERENCE_TEST
#include "calculator/runtime/calculator_app.h"
#endif

 // Statistics app controller (P17): type selector, data editor, tools,
 // result lists, Statistics Calc and the Statistics CATALOG. Mathematics
 // lives in calculator_statistics_math.h (UI-free); probability kernels in
 // calculator_probability.h. This file owns screens, jobs and rendering only.
namespace Calculator {

// Worker payloads. Summary jobs snapshot the dataset; Calculate jobs snapshot
// the dataset plus the caller-side StatsCalc Ans seed. EditCell jobs carry no
// dataset: adoption writes the evaluated value into the live rows iff the
// dataset revision is unchanged.
enum class StatisticsJobKind : uint8_t { Summary, Calculate, EditCell };

struct StatisticsTask {
  StatisticsJobKind kind = StatisticsJobKind::Summary;
  bool twoVariable = false, frequency = false;
  RegressionType regression = RegressionType::Linear;
  std::shared_ptr<const StatisticsDataset> dataset;
  std::string expression;
  Decimal calcAns;
  int row = 0, col = 0;
};

struct StatisticsResult {
  StatisticsJobKind kind = StatisticsJobKind::Summary;
  StatsSummary summary;
  RegressionFit regression;
  Decimal editValue;
  Error editError = Error::None;
};

Result evaluateStatistics(const StatisticsTask& task, const Request& request,
                          const std::function<bool()>& cancelled = {},
                          const std::function<void()>& cooperate = {});

namespace StatisticsApp {
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
const StatisticsDataset* data();
#endif
}  // namespace StatisticsApp
}  // namespace Calculator
