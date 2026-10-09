#pragma once
#include "calculator/math/calculator_decimal.h"
#include <cstdint>
#include <functional>

 // Shared Decimal probability kernels for Statistics and Distribution (P17).
 // UI-free: no session, controller, renderer or NVS dependency. All paths use
 // the project's 40-digit Decimal/decNumber work precision; binary float never
 // appears on the answer path. Callers map Status onto the target error UX
 // (modal Math/Argument ERROR vs per-row list ERROR vs Time Out).
 //
 // References (design only; constants verified against an mpmath host oracle):
 // NIST DLMF 8.9 (incomplete gamma continued fractions), 8.17 (incomplete
 // beta), 7.12 (error function). Iteration limits below are design decisions.
namespace Calculator {
namespace Probability {

// Outcome of one kernel evaluation.
enum class Status : uint8_t {
  Ok,            // value holds the probability/quantile
  Domain,        // mathematically undefined for these inputs (NaN, x<0, ...)
  NoConvergence,  // iteration budget exhausted -> Distribution Time Out
  Cancelled      // caller cancellation observed
};

struct Outcome {
  Decimal value;
  Status status = Status::Ok;
};

using CancelFn = const std::function<bool()>&;
using YieldFn = const std::function<void()>&;

// Natural log of Gamma(x), x > 0. Stirling series with Bernoulli B2..B20
// (stored as exact numerator/denominator pairs) after shifting x up to >= 32.
// x <= 0 or non-finite -> Domain.
Outcome logGamma(const Decimal& x, CancelFn cancelled = {}, YieldFn cooperate = {});

// Regularized lower/upper incomplete gamma P(a,x)/Q(a,x), a > 0, x >= 0.
// Series for x < a+1, modified-Lentz continued fraction otherwise; the
// smaller tail is computed directly, never 1-(1-tiny). <= 4096 terms with
// relative change <= 1e-30 on three successive terms, else NoConvergence.
Outcome gammaP(const Decimal& a, const Decimal& x, CancelFn cancelled = {}, YieldFn cooperate = {});
Outcome gammaQ(const Decimal& a, const Decimal& x, CancelFn cancelled = {}, YieldFn cooperate = {});

// Regularized incomplete beta I_x(a,b), 0 <= x <= 1, a,b > 0. Symmetry to the
// smaller side, then the modified-Lentz fraction; same term budget.
Outcome betaI(const Decimal& x, const Decimal& a, const Decimal& b,
              CancelFn cancelled = {}, YieldFn cooperate = {});

// Normal PDF with mean mu and deviation sigma > 0.
Outcome normalPdf(const Decimal& x, const Decimal& mu, const Decimal& sigma);
// Normal CDF P(X <= x). Computed in the smaller tail via gammaQ/gammaP, so
// far-tail intervals never subtract two nearly-1 values.
Outcome normalCdf(const Decimal& x, const Decimal& mu, const Decimal& sigma,
                  CancelFn cancelled = {}, YieldFn cooperate = {});
// Inverse normal CDF: mu + sigma*z with CDF(z) = area. Expanding bracket,
// bisection to 1e-15, then guarded Newton polish. area must lie in (0,1);
// the exact endpoints are target-unresolved (O-2) and rejected as Domain.
Outcome normalInv(const Decimal& area, const Decimal& mu, const Decimal& sigma,
                  CancelFn cancelled = {}, YieldFn cooperate = {});

// Binomial PMF P(X = k), N >= 0 integer, k integer in [0,N], 0 <= p <= 1.
// Multiplicative coefficient for N <= 4096, log-domain via logGamma above.
// p = 0/1 bypass logarithms. k outside [0,N] -> Domain (caller: per-row ERROR).
Outcome binomialPmf(int64_t n, int64_t k, const Decimal& p, CancelFn cancelled = {}, YieldFn cooperate = {});
// Binomial CDF P(X <= k) via betaI (I_(1-p)(N-k,k+1)) with endpoint branches:
// k < 0 -> 0, k >= N -> 1. p = 0/1 handled without logarithms.
Outcome binomialCdf(int64_t n, int64_t k, const Decimal& p, CancelFn cancelled = {}, YieldFn cooperate = {});

// Poisson PMF exp(-L) L^x / x!, x integer >= 0, L >= 0, via log-domain.
// L = 0 is the explicit point mass (x = 0 -> 1, else 0).
Outcome poissonPmf(int64_t x, const Decimal& lambda, CancelFn cancelled = {}, YieldFn cooperate = {});
// Poisson CDF P(X <= x) = Q(x+1, L). x < 0 -> 0.
Outcome poissonCdf(int64_t x, const Decimal& lambda, CancelFn cancelled = {}, YieldFn cooperate = {});

}  // namespace Probability
}  // namespace Calculator
