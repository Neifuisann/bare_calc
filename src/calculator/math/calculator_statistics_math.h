#pragma once
#include "calculator/math/calculator_decimal.h"
#include "calculator/math/calculator_engine.h"
#include "calculator/math/calculator_probability.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

 // Reusable weighted-statistics mathematics (P17): no UI state, no session,
 // no renderer. The controller (calculator_statistics.h) and any future
 // engine reuse this. All paths use 40-digit Decimal; aggregates use
 // two-pass centered sums, never mean(x^2)-mean(x)^2.
namespace Calculator {

// One grid cell per column; rows missing an active-column value are skipped
// by every aggregate (PENDING: manual is silent on partial rows).
struct StatRow {
  Decimal x, y, freq;
  bool hasX = false, hasY = false, hasFreq = false;
};

struct StatisticsDataset {
  std::vector<StatRow> rows;
};

enum class StatisticsMode : uint8_t { OneVariable, TwoVariable };

// Regression menu order follows the manual's supported-type list.
enum class RegressionType : uint8_t { Linear, Quadratic, Log, ExpE, ExpAB, Power, Inverse };

// Compatibility capacities (880-MAN): columns in use -> max rows.
inline size_t statisticsCapacity(bool twoVariable, bool frequency) {
  const int cols = (twoVariable ? 2 : 1) + (frequency ? 1 : 0);
  return cols <= 1 ? 160 : cols == 2 ? 80 : 53;
}

// Weighted summary snapshot. One computation per dataset revision; renderers
// consume this, never raw rows. smpValid is false when W <= 1 (sample
// variance undefined); hasY* fields apply in 2-variable mode only.
struct StatsSummary {
  bool defined = false;
  Error error = Error::None;
  Decimal n, sumX, sumX2, sumY, sumY2, sumXY, sumX3, sumX2Y, sumX4;
  Decimal meanX, meanY, popVarX, popSdX, smpVarX, smpSdX;
  Decimal popVarY, popSdY, smpVarY, smpSdY;
  bool smpValid = false, twoVariable = false;
  Decimal minX, q1, med, q3, maxX, minY, maxY;
};

StatsSummary computeStatsSummary(const StatisticsDataset& data, bool twoVariable, bool frequency,
                                 const std::function<bool()>& cancelled = {},
                                 const std::function<void()>& cooperate = {});

// Least-squares fit for one regression type. rValid is false when the
// transformed Y is constant (correlation undefined); hasC only for quadratic.
// error is Math for domain/singular fits. Unknown exact target UX -> PENDING;
// provisional: the fit errors, raw sums/moments stay usable.
struct RegressionFit {
  bool defined = false;
  Error error = Error::None;
  Decimal a, b, c, r;
  bool hasC = false, rValid = false;
};

RegressionFit fitRegression(const StatisticsDataset& data, bool twoVariable, bool frequency,
                            RegressionType type, const std::function<bool()>& cancelled = {},
                            const std::function<void()>& cooperate = {});

// Estimated values from a fit. branch selects xHat1/xHat2 for quadratic.
Probability::Outcome regressionYHat(const RegressionFit& fit, RegressionType type, const Decimal& x);
Probability::Outcome regressionXHat(const RegressionFit& fit, RegressionType type, const Decimal& y,
                                    int branch = 0);

}  // namespace Calculator
