#include "calculator/math/calculator_probability.h"
#include <cstdio>
#include <string>

namespace Calculator {
namespace Probability {
namespace {

// Exact Bernoulli numbers B2..B20 as numerator/denominator pairs (A&S 23.2;
// verified against the mpmath host oracle in calculator_apps_p17_test.h).
constexpr int64_t kBernoulliNum[10] = {1, -1, 1, -1, 5, -691, 7, -3617, 43867, -174611};
constexpr int64_t kBernoulliDen[10] = {6, 30, 42, 30, 66, 2730, 6, 510, 798, 330};

// Iteration contract shared by the series/fraction kernels: at most 4096
// terms, relative change <= 1e-30 on three successive terms.
constexpr int kMaxTerms = 4096;
constexpr int kSteadyTerms = 3;

Decimal decI64(int64_t value) { return Decimal(std::to_string(value)); }

// Final drift clamp: rounding may push a true 0/1 by ~1e-40. Anything larger
// is a real failure and must surface, never hide behind a clamp.
Outcome clampUnit(const Decimal& value) {
  static const Decimal kTol("1E-30");
  if (value < Decimal(0)) return {value > -kTol ? Decimal(0) : Decimal::nan(),
                                  value > -kTol ? Status::Ok : Status::Domain};
  if (value > Decimal(1)) return {value < Decimal(1) + kTol ? Decimal(1) : Decimal::nan(),
                                  value < Decimal(1) + kTol ? Status::Ok : Status::Domain};
  return {value, Status::Ok};
}

bool converged(const Decimal& delta, const Decimal& scale, int& steady) {
  static const Decimal kEps("1E-30");
  const Decimal change = delta.abs();
  const Decimal bound = scale.abs() * kEps;
  if (change <= bound) {
    if (++steady >= kSteadyTerms) return true;
  } else {
    steady = 0;
  }
  return false;
}

}  // namespace

Outcome logGamma(const Decimal& x, CancelFn cancelled, YieldFn cooperate) {
  if (!x.finite() || !(x > Decimal(0))) return {Decimal::nan(), Status::Domain};
  // Shift upward: lnG(x) = lnG(z) - sum ln(x+j), z in [32,33).
  Decimal z = x, shift = Decimal(0);
  while (z < Decimal(32)) {
    if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
    shift = shift + z.ln();
    z = z + Decimal(1);
  }
  static const Decimal kHalfLog2Pi = (Decimal(2) * Decimal::pi()).ln() / Decimal(2);
  Decimal out = (z - Decimal("0.5")) * z.ln() - z + kHalfLog2Pi;
  Decimal power = z;  // z^(2k-1)
  const Decimal square = z * z;
  for (int k = 1; k <= 10; ++k) {
    const Decimal term = Decimal(decI64(kBernoulliNum[k - 1]) / decI64(kBernoulliDen[k - 1])) /
                         (Decimal(2 * k * (2 * k - 1)) * power);
    out = out + term;
    power = power * square;
  }
  if (cooperate) cooperate();
  return {out - shift, Status::Ok};
}

namespace {
// log prefactor a*ln(x) - x - lnG(a) shared by the gamma kernels.
Outcome gammaLogPrefactor(const Decimal& a, const Decimal& x, Decimal& prefactor,
                          CancelFn cancelled, YieldFn cooperate) {
  Outcome lg = logGamma(a, cancelled, cooperate);
  if (lg.status != Status::Ok) return lg;
  prefactor = a * x.ln() - x - lg.value;
  return {prefactor, Status::Ok};
}
}  // namespace

Outcome gammaP(const Decimal& a, const Decimal& x, CancelFn cancelled, YieldFn cooperate) {
  if (!a.finite() || !x.finite() || !(a > Decimal(0)) || x < Decimal(0))
    return {Decimal::nan(), Status::Domain};
  if (x.zero()) return {Decimal(0), Status::Ok};
  Decimal prefactor;
  Outcome ready = gammaLogPrefactor(a, x, prefactor, cancelled, cooperate);
  if (ready.status != Status::Ok) return ready;
  Decimal result;
  if (x < a + Decimal(1)) {
    // Power series for the lower tail.
    Decimal term = Decimal(1) / a, sum = term;
    int steady = 0;
    int n = 0;
    for (n = 1; n <= kMaxTerms; ++n) {
      term = term * x / (a + Decimal(n));
      const Decimal next = sum + term;
      if (converged(next - sum, sum, steady)) {
        sum = next;
        break;
      }
      sum = next;
      if ((n & 31) == 0) {
        if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
        if (cooperate) cooperate();
      }
    }
    if (n > kMaxTerms) return {Decimal::nan(), Status::NoConvergence};
    result = sum * prefactor.exp();
    return clampUnit(result);
  }
  Outcome q = gammaQ(a, x, cancelled, cooperate);
  if (q.status != Status::Ok) return q;
  result = Decimal(1) - q.value;
  return clampUnit(result);
}

Outcome gammaQ(const Decimal& a, const Decimal& x, CancelFn cancelled, YieldFn cooperate) {
  if (!a.finite() || !x.finite() || !(a > Decimal(0)) || x < Decimal(0))
    return {Decimal::nan(), Status::Domain};
  if (x.zero()) return {Decimal(1), Status::Ok};
  if (x < a + Decimal(1)) {
    Outcome p = gammaP(a, x, cancelled, cooperate);
    if (p.status != Status::Ok) return p;
    return clampUnit(Decimal(1) - p.value);
  }
  Decimal prefactor;
  Outcome ready = gammaLogPrefactor(a, x, prefactor, cancelled, cooperate);
  if (ready.status != Status::Ok) return ready;
  // Modified Lentz continued fraction (DLMF 8.9): b = x+1-a,
  // a_n = -n(n-a), b incremented by 2 each step.
  static const Decimal kTiny("1E-100");
  Decimal b = x + Decimal(1) - a;
  Decimal c = Decimal(1) / kTiny;
  Decimal d = b.abs() < kTiny ? Decimal(1) / kTiny : Decimal(1) / b;
  Decimal h = d;
  int steady = 0, n = 0;
  for (n = 1; n <= kMaxTerms; ++n) {
    const Decimal an = decI64(-n) * (decI64(n) - a);
    b = b + Decimal(2);
    d = an * d + b;
    if (d.abs() < kTiny) d = kTiny;
    d = Decimal(1) / d;
    c = b + an / c;
    if (c.abs() < kTiny) c = kTiny;
    const Decimal cd = c * d;
    const Decimal next = h * cd;
    if (converged(next - h, h, steady)) {
      h = next;
      break;
    }
    h = next;
    if ((n & 31) == 0) {
      if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
      if (cooperate) cooperate();
    }
  }
  if (n > kMaxTerms) return {Decimal::nan(), Status::NoConvergence};
  return clampUnit(h * prefactor.exp());
}

Outcome betaI(const Decimal& x, const Decimal& a, const Decimal& b,
              CancelFn cancelled, YieldFn cooperate) {
  if (!x.finite() || !a.finite() || !b.finite() || !(a > Decimal(0)) || !(b > Decimal(0)) ||
      x < Decimal(0) || x > Decimal(1))
    return {Decimal::nan(), Status::Domain};
  if (x.zero()) return {Decimal(0), Status::Ok};
  if (x == Decimal(1)) return {Decimal(1), Status::Ok};
  // Symmetry to the smaller side (DLMF 8.17).
  if (x > (a + Decimal(1)) / (a + b + Decimal(2))) {
    Outcome flipped = betaI(Decimal(1) - x, b, a, cancelled, cooperate);
    if (flipped.status != Status::Ok) return flipped;
    return clampUnit(Decimal(1) - flipped.value);
  }
  Outcome la = logGamma(a, cancelled, cooperate);
  if (la.status != Status::Ok) return la;
  Outcome lb = logGamma(b, cancelled, cooperate);
  if (lb.status != Status::Ok) return lb;
  Outcome lab = logGamma(a + b, cancelled, cooperate);
  if (lab.status != Status::Ok) return lab;
  const Decimal front = (a * x.ln() + b * (Decimal(1) - x).ln() - la.value - lb.value + lab.value).exp() / a;
  // Modified-Lentz beta fraction; reciprocal form times front/a.
  const Decimal qab = a + b, qap = a + Decimal(1), qam = a - Decimal(1);
  static const Decimal kTiny("1E-100");
  Decimal c = Decimal(1);
  Decimal d = Decimal(1) - qab * x / qap;
  if (d.abs() < kTiny) d = kTiny;
  d = Decimal(1) / d;
  Decimal h = d;
  int steady = 0, m = 0;
  for (m = 1; m <= kMaxTerms; ++m) {
    const int m2 = 2 * m;
    Decimal aa = decI64(m) * (b - decI64(m)) * x / ((qam + decI64(m2)) * (a + decI64(m2)));
    d = Decimal(1) + aa * d;
    if (d.abs() < kTiny) d = kTiny;
    d = Decimal(1) / d;
    c = Decimal(1) + aa / c;
    if (c.abs() < kTiny) c = kTiny;
    const Decimal first = d * c;
    h = h * first;
    aa = -(a + decI64(m)) * (qab + decI64(m)) * x / ((a + decI64(m2)) * (qap + decI64(m2)));
    d = Decimal(1) + aa * d;
    if (d.abs() < kTiny) d = kTiny;
    d = Decimal(1) / d;
    c = Decimal(1) + aa / c;
    if (c.abs() < kTiny) c = kTiny;
    const Decimal both = first * d * c;
    const Decimal next = h * d * c;
    if (converged(next - h, h, steady)) {
      h = next;
      break;
    }
    h = next;
    (void)both;
    if ((m & 15) == 0) {
      if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
      if (cooperate) cooperate();
    }
  }
  if (m > kMaxTerms) return {Decimal::nan(), Status::NoConvergence};
  return clampUnit(front * h);
}

Outcome normalPdf(const Decimal& x, const Decimal& mu, const Decimal& sigma) {
  if (!x.finite() || !mu.finite() || !sigma.finite() || !(sigma > Decimal(0)))
    return {Decimal::nan(), Status::Domain};
  const Decimal z = (x - mu) / sigma;
  static const Decimal kInvSqrt2Pi = (Decimal(2) * Decimal::pi()).sqrt();
  const Decimal pdf = ((-z * z) / Decimal(2)).exp() / (sigma * kInvSqrt2Pi);
  if (!(pdf >= Decimal(0))) return {Decimal::nan(), Status::Domain};
  return {pdf, Status::Ok};
}

Outcome normalCdf(const Decimal& x, const Decimal& mu, const Decimal& sigma,
                  CancelFn cancelled, YieldFn cooperate) {
  if (!x.finite() || !mu.finite() || !sigma.finite() || !(sigma > Decimal(0)))
    return {Decimal::nan(), Status::Domain};
  const Decimal z = (x - mu) / sigma;
  const Decimal half = Decimal("0.5");
  // Tail-first: for z >= 0 the upper tail Q is small; for z < 0 the same Q
  // value is the small lower tail. Never 1-(1-tiny).
  Outcome tail = gammaQ(half, (z * z) / Decimal(2), cancelled, cooperate);
  if (tail.status != Status::Ok) return tail;
  const Decimal cdf = z.negative() ? tail.value / Decimal(2) : Decimal(1) - tail.value / Decimal(2);
  return clampUnit(cdf);
}

Outcome normalInv(const Decimal& area, const Decimal& mu, const Decimal& sigma,
                  CancelFn cancelled, YieldFn cooperate) {
  if (!area.finite() || !mu.finite() || !sigma.finite() || !(sigma > Decimal(0)) || !(area > Decimal(0)) ||
      !(area < Decimal(1)))
    return {Decimal::nan(), Status::Domain};
  // Expanding bracket on the standard normal, then bisection, then guarded
  // Newton polish with the PDF derivative.
  Decimal lo(-1), hi(1);
  auto cdf = [&](const Decimal& z, Status& status) {
    Outcome out = normalCdf(z, Decimal(0), Decimal(1), cancelled, cooperate);
    status = out.status;
    return out.value;
  };
  Status status = Status::Ok;
  Decimal fHi = cdf(hi, status);
  if (status != Status::Ok) return {Decimal::nan(), status};
  int expansions = 0;
  while (fHi < area) {
    lo = hi;
    hi = hi + hi;
    fHi = cdf(hi, status);
    if (status != Status::Ok) return {Decimal::nan(), status};
    if (++expansions > 64) return {Decimal::nan(), Status::NoConvergence};
  }
  Decimal fLo = cdf(lo, status);
  if (status != Status::Ok) return {Decimal::nan(), status};
  expansions = 0;
  while (fLo > area) {
    hi = lo;
    lo = lo + lo;
    fLo = cdf(lo, status);
    if (status != Status::Ok) return {Decimal::nan(), status};
    if (++expansions > 64) return {Decimal::nan(), Status::NoConvergence};
  }
  static const Decimal kBisectTol("1E-15");
  Decimal z = (lo + hi) / Decimal(2);
  for (int n = 0; n < 200; ++n) {
    if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
    if ((n & 7) == 0 && cooperate) cooperate();
    Outcome mid = normalCdf(z, Decimal(0), Decimal(1));
    if (mid.status != Status::Ok) return {Decimal::nan(), mid.status};
    if (mid.value < area)
      lo = z;
    else
      hi = z;
    const Decimal next = (lo + hi) / Decimal(2);
    const Decimal width = (hi - lo).abs();
    const Decimal scale = z.abs() > Decimal(1) ? z.abs() : Decimal(1);
    z = next;
    if (width <= kBisectTol * scale) break;
  }
  // Guarded Newton polish: stay inside the bracket, require progress.
  for (int n = 0; n < 16; ++n) {
    if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
    Outcome c = normalCdf(z, Decimal(0), Decimal(1));
    if (c.status != Status::Ok) return {Decimal::nan(), c.status};
    Outcome p = normalPdf(z, Decimal(0), Decimal(1));
    if (p.status != Status::Ok || !(p.value > Decimal(0))) break;
    const Decimal step = (c.value - area) / p.value;
    const Decimal next = z - step;
    if (!next.finite() || next < lo || next > hi || step.abs() >= (z - lo).abs() + (hi - z).abs()) break;
    if ((next - z).abs() >= (z - (lo + hi) / Decimal(2)).abs() && n > 2) {
      z = next;
      break;
    }
    z = next;
    if (step.abs() <= Decimal("1E-30") * (z.abs() > Decimal(1) ? z.abs() : Decimal(1))) break;
  }
  return {mu + sigma * z, Status::Ok};
}

Outcome binomialPmf(int64_t n, int64_t k, const Decimal& p, CancelFn cancelled, YieldFn cooperate) {
  if (n < 0 || k < 0 || k > n || !p.finite() || p < Decimal(0) || p > Decimal(1))
    return {Decimal::nan(), Status::Domain};
  if (p.zero()) return {k == 0 ? Decimal(1) : Decimal(0), Status::Ok};
  if (p == Decimal(1)) return {k == n ? Decimal(1) : Decimal(0), Status::Ok};
  if (n <= 4096) {
    // Multiplicative coefficient with interleaved division: no factorial
    // overflow, no BigInt, bounded loop.
    const int64_t r = k > n - k ? n - k : k;
    Decimal coeff(1);
    for (int64_t i = 1; i <= r; ++i) {
      coeff = coeff * decI64(n - r + i) / decI64(i);
      if ((i & 63) == 0) {
        if (cancelled && cancelled()) return {Decimal::nan(), Status::Cancelled};
        if (cooperate) cooperate();
      }
    }
    const Decimal pmf = coeff * p.pow(decI64(k)) * (Decimal(1) - p).pow(decI64(n - k));
    if (pmf < Decimal(0)) return {Decimal::nan(), Status::Domain};
    return clampUnit(pmf);
  }
  // Log-domain for large N.
  Outcome l1 = logGamma(decI64(n + 1), cancelled, cooperate);
  if (l1.status != Status::Ok) return l1;
  Outcome l2 = logGamma(decI64(k + 1), cancelled, cooperate);
  if (l2.status != Status::Ok) return l2;
  Outcome l3 = logGamma(decI64(n - k + 1), cancelled, cooperate);
  if (l3.status != Status::Ok) return l3;
  const Decimal pmf =
      (decI64(k) * p.ln() + decI64(n - k) * (Decimal(1) - p).ln() + l1.value - l2.value - l3.value).exp();
  if (pmf < Decimal(0)) return {Decimal::nan(), Status::Domain};
  return clampUnit(pmf);
}

Outcome binomialCdf(int64_t n, int64_t k, const Decimal& p, CancelFn cancelled, YieldFn cooperate) {
  if (n < 0 || !p.finite() || p < Decimal(0) || p > Decimal(1))
    return {Decimal::nan(), Status::Domain};
  if (k < 0) return {Decimal(0), Status::Ok};
  if (k >= n) return {Decimal(1), Status::Ok};
  if (p.zero()) return {Decimal(1), Status::Ok};
  if (p == Decimal(1)) return {Decimal(0), Status::Ok};
  // P(X <= k) = I_(1-p)(N-k, k+1): one stable fraction, tail-first by
  // construction, no per-term resummation.
  return betaI(Decimal(1) - p, decI64(n - k), decI64(k + 1), cancelled, cooperate);
}

Outcome poissonPmf(int64_t x, const Decimal& lambda, CancelFn cancelled, YieldFn cooperate) {
  if (x < 0 || !lambda.finite() || lambda < Decimal(0)) return {Decimal::nan(), Status::Domain};
  if (lambda.zero()) return {x == 0 ? Decimal(1) : Decimal(0), Status::Ok};
  if (x == 0) return {(-lambda).exp(), Status::Ok};
  Outcome lg = logGamma(decI64(x + 1), cancelled, cooperate);
  if (lg.status != Status::Ok) return lg;
  const Decimal pmf = (decI64(x) * lambda.ln() - lambda - lg.value).exp();
  if (pmf < Decimal(0)) return {Decimal::nan(), Status::Domain};
  return clampUnit(pmf);
}

Outcome poissonCdf(int64_t x, const Decimal& lambda, CancelFn cancelled, YieldFn cooperate) {
  if (!lambda.finite() || lambda < Decimal(0)) return {Decimal::nan(), Status::Domain};
  if (x < 0) return {Decimal(0), Status::Ok};
  // P(X <= x) = Q(x+1, L): accumulated stably inside gammaQ, no per-term
  // factorial resummation, correct far-tail direction by construction.
  return gammaQ(decI64(x + 1), lambda, cancelled, cooperate);
}

}  // namespace Probability
}  // namespace Calculator
