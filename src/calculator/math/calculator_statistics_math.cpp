#include "calculator/math/calculator_statistics_math.h"
#include "calculator/math/calculator_probability.h"
#include <algorithm>

namespace Calculator {
namespace {

// A row contributes iff it carries every active-column value. Frequency-off
// rows weigh 1; frequency-on rows require an explicit Freq cell.
struct WeightedObs {
  Decimal x, y, w;
};

bool collectObs(const StatisticsDataset& data, bool twoVariable, bool frequency,
                std::vector<WeightedObs>& obs, Error& error) {
  obs.clear();
  obs.reserve(data.rows.size());
  for (const auto& row : data.rows) {
    if (!row.hasX) continue;
    if (twoVariable && !row.hasY) continue;
    Decimal w(1);
    if (frequency) {
      if (!row.hasFreq) continue;
      w = row.freq;
    }
    if (!w.finite() || !row.x.finite() || (twoVariable && !row.y.finite()) || w < Decimal(0)) {
      error = Error::Math;
      return false;
    }
    if (w.zero()) continue;  // zero-weight rows ignored (provisional D; O-1 PENDING)
    obs.push_back({row.x, twoVariable ? row.y : Decimal(0), w});
  }
  return true;
}

// valueAt(pos): value of the 1-indexed unit observation at cumulative weight
// position pos (Tukey hinges on expanded data, without expanding it).
Decimal valueAt(const std::vector<WeightedObs>& sorted, const Decimal& pos) {
  Decimal cum(0);
  for (const auto& o : sorted) {
    cum = cum + o.w;
    if (cum >= pos) return o.x;
  }
  return sorted.back().x;
}

// Median of the 1-indexed observation range [a,b]; count m = b-a+1.
Decimal medianOf(const std::vector<WeightedObs>& sorted, const Decimal& a, const Decimal& b) {
  const Decimal m = b - a + Decimal(1);
  const Decimal half = m / Decimal(2);
  if (half.integer()) return (valueAt(sorted, a + half - Decimal(1)) + valueAt(sorted, a + half)) / Decimal(2);
  return valueAt(sorted, (a + b) / Decimal(2));
}

}  // namespace

StatsSummary computeStatsSummary(const StatisticsDataset& data, bool twoVariable, bool frequency,
                                 const std::function<bool()>& cancelled,
                                 const std::function<void()>& cooperate) {
  StatsSummary s;
  s.twoVariable = twoVariable;
  std::vector<WeightedObs> obs;
  if (!collectObs(data, twoVariable, frequency, obs, s.error)) return s;
  Decimal w(0), sx(0), sx2(0), sy(0), sy2(0), sxy(0), sx3(0), sx2y(0), sx4(0);
  size_t n = 0;
  for (const auto& o : obs) {
    if (cancelled && cancelled()) {
      s.error = Error::Cancelled;
      return s;
    }
    w = w + o.w;
    sx = sx + o.w * o.x;
    sx2 = sx2 + o.w * o.x * o.x;
    if (twoVariable) {
      sy = sy + o.w * o.y;
      sy2 = sy2 + o.w * o.y * o.y;
      sxy = sxy + o.w * o.x * o.y;
      sx3 = sx3 + o.w * o.x * o.x * o.x;
      sx2y = sx2y + o.w * o.x * o.x * o.y;
      sx4 = sx4 + o.w * o.x * o.x * o.x * o.x;
    }
    if ((++n & 31) == 0 && cooperate) cooperate();
  }
  if (!(w > Decimal(0))) {
    s.error = Error::Math;  // empty dataset: every quantity undefined
    return s;
  }
  s.n = w;
  s.sumX = sx;
  s.sumX2 = sx2;
  s.meanX = sx / w;
  if (twoVariable) {
    s.sumY = sy;
    s.sumY2 = sy2;
    s.sumXY = sxy;
    s.sumX3 = sx3;
    s.sumX2Y = sx2y;
    s.sumX4 = sx4;
    s.meanY = sy / w;
  }
  // Second centered pass: never mean(x^2)-mean(x)^2.
  Decimal m2x(0), m2y(0);
  n = 0;
  for (const auto& o : obs) {
    if (cancelled && cancelled()) {
      s.error = Error::Cancelled;
      return s;
    }
    const Decimal dx = o.x - s.meanX;
    m2x = m2x + o.w * dx * dx;
    if (twoVariable) {
      const Decimal dy = o.y - s.meanY;
      m2y = m2y + o.w * dy * dy;
    }
    if ((++n & 31) == 0 && cooperate) cooperate();
  }
  // Microscopic negative drift from centered accumulation clamps to zero;
  // anything larger is corrupt and errors.
  static const Decimal kDrift("1E-30");
  if (m2x < Decimal(0)) {
    if (m2x > -kDrift * (sx2.abs() > Decimal(1) ? sx2.abs() : Decimal(1)))
      m2x = Decimal(0);
    else {
      s.error = Error::Math;
      return s;
    }
  }
  s.popVarX = m2x / w;
  s.popSdX = m2x.sqrt() / w.sqrt();
  s.smpValid = w > Decimal(1);
  if (s.smpValid) {
    s.smpVarX = m2x / (w - Decimal(1));
    s.smpSdX = m2x.sqrt() / (w - Decimal(1)).sqrt();
  }
  if (twoVariable) {
    if (m2y < Decimal(0)) {
      if (m2y > -kDrift * (sy2.abs() > Decimal(1) ? sy2.abs() : Decimal(1)))
        m2y = Decimal(0);
      else {
        s.error = Error::Math;
        return s;
      }
    }
    s.popVarY = m2y / w;
    s.popSdY = m2y.sqrt() / w.sqrt();
    if (s.smpValid) {
      s.smpVarY = m2y / (w - Decimal(1));
      s.smpSdY = m2y.sqrt() / (w - Decimal(1)).sqrt();
    }
  }
  // Extremes and weighted Tukey hinges (min/max over positive-weight rows).
  std::vector<WeightedObs> sorted = obs;
  std::sort(sorted.begin(), sorted.end(), [](const WeightedObs& a, const WeightedObs& b) {
    return a.x.compare(b.x) < 0;
  });
  s.minX = sorted.front().x;
  s.maxX = sorted.back().x;
  if (twoVariable) {
    Decimal mn = sorted.front().y, mx = sorted.front().y;
    for (const auto& o : sorted) {
      if (o.y.compare(mn) < 0) mn = o.y;
      if (o.y.compare(mx) > 0) mx = o.y;
    }
    s.minY = mn;
    s.maxY = mx;
  } else {
    const Decimal half = (w / Decimal(2)).floor();
    s.med = medianOf(sorted, Decimal(1), w);
    if (half < Decimal(1)) {
      s.q1 = s.med;  // single-observation data: hinges equal the median
      s.q3 = s.med;  // (provisional D; odd-n/frequency edges PENDING)
    } else {
      s.q1 = medianOf(sorted, Decimal(1), half);
      s.q3 = medianOf(sorted, w - half + Decimal(1), w);
    }
  }
  s.defined = true;
  return s;
}

RegressionFit fitRegression(const StatisticsDataset& data, bool twoVariable, bool frequency,
                            RegressionType type, const std::function<bool()>& cancelled,
                            const std::function<void()>& cooperate) {
  RegressionFit fit;
  if (!twoVariable) {
    fit.error = Error::NotDefined;  // regression needs 2-variable data (PENDING label)
    return fit;
  }
  struct P {
    Decimal X, Y, w;
  };
  std::vector<P> pts;
  pts.reserve(data.rows.size());
  for (const auto& row : data.rows) {
    if (!row.hasX || !row.hasY) continue;
    Decimal w(1);
    if (frequency) {
      if (!row.hasFreq) continue;
      w = row.freq;
    }
    if (!w.finite() || w < Decimal(0) || !row.x.finite() || !row.y.finite() || w.zero()) {
      if (!w.finite() || w < Decimal(0)) {
        fit.error = Error::Math;
        return fit;
      }
      if (w.zero()) continue;
      fit.error = Error::Math;
      return fit;
    }
    const Decimal x = row.x, y = row.y;
    // Transformed regressions validate their domains row by row. Unknown
    // exact target UX -> PENDING; provisional: the whole fit Math ERRORs.
    switch (type) {
      case RegressionType::Linear:
      case RegressionType::Quadratic:
        pts.push_back({x, y, w});
        break;
      case RegressionType::Log:
        if (!(x > Decimal(0))) {
          fit.error = Error::Math;
          return fit;
        }
        pts.push_back({x.ln(), y, w});
        break;
      case RegressionType::ExpE:
      case RegressionType::ExpAB:
        if (!(y > Decimal(0))) {
          fit.error = Error::Math;
          return fit;
        }
        pts.push_back({x, y.ln(), w});
        break;
      case RegressionType::Power:
        if (!(x > Decimal(0)) || !(y > Decimal(0))) {
          fit.error = Error::Math;
          return fit;
        }
        pts.push_back({x.ln(), y.ln(), w});
        break;
      case RegressionType::Inverse:
        if (x.zero()) {
          fit.error = Error::Math;
          return fit;
        }
        pts.push_back({Decimal(1) / x, y, w});
        break;
    }
  }
  Decimal W(0), mx(0), my(0);
  for (const auto& p : pts) {
    W = W + p.w;
    mx = mx + p.w * p.X;
    my = my + p.w * p.Y;
  }
  if (!(W > Decimal(0))) {
    fit.error = Error::Math;
    return fit;
  }
  mx = mx / W;
  my = my / W;
  if (type == RegressionType::Quadratic) {
    // Centered/scaled 3-column solve with partial-pivot elimination (Decimal):
    // well-conditioned for ordinary data without an orthogonal factorization.
    Decimal sxx(0);
    for (const auto& p : pts) {
      const Decimal d = p.X - mx;
      sxx = sxx + p.w * d * d;
    }
    if (!(sxx > Decimal(0))) {
      fit.error = Error::Math;  // all x identical
      return fit;
    }
    const Decimal scale = (sxx / W).sqrt();
    // Normal equations for [1, u, u^2], u = (X-mx)/scale.
    Decimal m[3][4] = {};
    size_t k = 0;
    for (const auto& p : pts) {
      if (cancelled && cancelled()) {
        fit.error = Error::Cancelled;
        return fit;
      }
      const Decimal u = (p.X - mx) / scale;
      const Decimal u2 = u * u;
      const Decimal cols[3] = {Decimal(1), u, u2};
      for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i][j] = m[i][j] + p.w * cols[i] * cols[j];
        m[i][3] = m[i][3] + p.w * cols[i] * p.Y;
      }
      if ((++k & 31) == 0 && cooperate) cooperate();
    }
    for (int col = 0; col < 3; ++col) {
      int piv = col;
      for (int r = col + 1; r < 3; ++r)
        if (m[r][col].abs().compare(m[piv][col].abs()) > 0) piv = r;
      if (m[piv][col].zero()) {
        fit.error = Error::Math;  // rank-deficient
        return fit;
      }
      if (piv != col)
        for (int j = col; j < 4; ++j) std::swap(m[col][j], m[piv][j]);
      for (int r = 0; r < 3; ++r) {
        if (r == col) continue;
        const Decimal f = m[r][col] / m[col][col];
        for (int j = col; j < 4; ++j) m[r][j] = m[r][j] - f * m[col][j];
      }
    }
    Decimal c[3];
    for (int i = 0; i < 3; ++i) c[i] = m[i][3] / m[i][i];
    // y = c0 + c1*u + c2*u^2, u = (x-mx)/scale.
    fit.c = c[2] / (scale * scale);
    fit.b = c[1] / scale - Decimal(2) * c[2] * mx / (scale * scale);
    fit.a = c[0] - c[1] * mx / scale + c[2] * mx * mx / (scale * scale);
    fit.hasC = true;
    fit.defined = true;
    return fit;
  }
  Decimal sxx(0), syy(0), sxy(0);
  size_t k = 0;
  for (const auto& p : pts) {
    if (cancelled && cancelled()) {
      fit.error = Error::Cancelled;
      return fit;
    }
    const Decimal dx = p.X - mx, dy = p.Y - my;
    sxx = sxx + p.w * dx * dx;
    syy = syy + p.w * dy * dy;
    sxy = sxy + p.w * dx * dy;
    if ((++k & 31) == 0 && cooperate) cooperate();
  }
  if (!(sxx > Decimal(0))) {
    fit.error = Error::Math;  // degenerate independent values
    return fit;
  }
  const Decimal b0 = sxy / sxx, a0 = my - b0 * mx;
  fit.rValid = syy > Decimal(0);
  if (fit.rValid) {
    fit.r = sxy / (sxx.sqrt() * syy.sqrt());
    static const Decimal kTol("1E-30");
    if (fit.r > Decimal(1)) fit.r = fit.r < Decimal(1) + kTol ? Decimal(1) : fit.r;
    if (fit.r < Decimal(-1)) fit.r = fit.r > Decimal(-1) - kTol ? Decimal(-1) : fit.r;
    if (fit.r > Decimal(1) || fit.r < Decimal(-1)) {
      fit.error = Error::Math;
      return fit;
    }
  }
  switch (type) {
    case RegressionType::Linear:
    case RegressionType::Log:
    case RegressionType::Inverse:
      fit.a = a0;
      fit.b = b0;
      break;
    case RegressionType::ExpE:
      fit.a = a0.exp();
      fit.b = b0;
      break;
    case RegressionType::ExpAB:
      fit.a = a0.exp();
      fit.b = b0.exp();
      break;
    case RegressionType::Power:
      fit.a = a0.exp();
      fit.b = b0;
      break;
    case RegressionType::Quadratic:
      break;  // handled above
  }
  fit.defined = true;
  return fit;
}

Probability::Outcome regressionYHat(const RegressionFit& fit, RegressionType type, const Decimal& x) {
  using Probability::Outcome;
  using Probability::Status;
  if (!fit.defined || !x.finite()) return {Decimal::nan(), Status::Domain};
  const Decimal a = fit.a, b = fit.b, c = fit.c;
  switch (type) {
    case RegressionType::Linear:
      return {a + b * x, Status::Ok};
    case RegressionType::Quadratic:
      return {a + b * x + c * x * x, Status::Ok};
    case RegressionType::Log:
      if (!(x > Decimal(0))) return {Decimal::nan(), Status::Domain};
      return {a + b * x.ln(), Status::Ok};
    case RegressionType::ExpE:
      return {a * (b * x).exp(), Status::Ok};
    case RegressionType::ExpAB:
      return {a * b.pow(x), Status::Ok};
    case RegressionType::Power:
      if (!(x > Decimal(0))) return {Decimal::nan(), Status::Domain};
      return {a * x.pow(b), Status::Ok};
    case RegressionType::Inverse:
      if (x.zero()) return {Decimal::nan(), Status::Domain};
      return {a + b / x, Status::Ok};
  }
  return {Decimal::nan(), Status::Domain};
}

Probability::Outcome regressionXHat(const RegressionFit& fit, RegressionType type, const Decimal& y,
                                    int branch) {
  using Probability::Outcome;
  using Probability::Status;
  if (!fit.defined || !y.finite()) return {Decimal::nan(), Status::Domain};
  const Decimal a = fit.a, b = fit.b, c = fit.c;
  switch (type) {
    case RegressionType::Linear:
      if (b.zero()) return {Decimal::nan(), Status::Domain};
      return {(y - a) / b, Status::Ok};
    case RegressionType::Quadratic: {
      if (c.zero()) {
        // Validated reduced formula (PENDING native behavior): linear
        // fall-back on branch 0, undefined on branch 1.
        if (branch != 0 || b.zero()) return {Decimal::nan(), Status::Domain};
        return {(y - a) / b, Status::Ok};
      }
      const Decimal disc = b * b - Decimal(4) * c * (a - y);
      if (disc < Decimal(0)) return {Decimal::nan(), Status::Domain};
      const Decimal root = disc.sqrt();
      return {(branch == 0 ? -b + root : -b - root) / (Decimal(2) * c), Status::Ok};
    }
    case RegressionType::Log:
      if (b.zero()) return {Decimal::nan(), Status::Domain};
      return {((y - a) / b).exp(), Status::Ok};
    case RegressionType::ExpE: {
      if (b.zero() || !(y / a > Decimal(0))) return {Decimal::nan(), Status::Domain};
      return {(y / a).ln() / b, Status::Ok};
    }
    case RegressionType::ExpAB: {
      const Decimal lb = b.ln();
      if (lb.zero() || !(y / a > Decimal(0))) return {Decimal::nan(), Status::Domain};
      return {(y / a).ln() / lb, Status::Ok};
    }
    case RegressionType::Power: {
      if (b.zero() || !(y / a > Decimal(0))) return {Decimal::nan(), Status::Domain};
      return {(y / a).pow(Decimal(1) / b), Status::Ok};
    }
    case RegressionType::Inverse:
      if ((y - a).zero()) return {Decimal::nan(), Status::Domain};
      return {b / (y - a), Status::Ok};
  }
  return {Decimal::nan(), Status::Domain};
}

}  // namespace Calculator
