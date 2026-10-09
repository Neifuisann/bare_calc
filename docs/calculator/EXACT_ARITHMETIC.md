# Exact arithmetic

The calculator evaluates exactly and approximates only on request. `EXE`
keeps `1/3+1/7` as `10/21`, `sqrt(8)+sqrt(18)` as `5*sqrt(2)` and `pi/2+pi/3`
as `5/6*pi`; SHIFT+EXE / FORMAT > Decimal (and `approximate()`) produce
decimals. Code: `src/calculator/math/calculator_bigint.*` (integers, rationals),
`calculator_exact.*` (the exact number), `calculator_engine.cpp` (the bridge).
Tests: the `exact` and `engine` stages of `tools/check.ps1`.

## The model

`Value = {Decimal number, ExactNumber symbolic}` (`calculator_scalar.h`).

- `symbolic` is authoritative whenever it is exact. Equality, zero, sign,
  cancellation and simplification are decided on it, never on `number`.
- `number` is the 40-digit decimal approximation used for display, the
  calculator range check and every inexact continuation.
- `Value::exact()` / `extra()` are the **ClassWiz display view**: at most two
  terms `c`, `c*sqrt(n)`, `c*pi` or `c*e` with |c|, n <= 1E15. The `c*e`
  term goes beyond ClassWiz (so an exact `e-1` reads exactly) and follows
  the pi rules: shown while |value| < 1E6, Math I/O only. They are computed from
  `symbolic` on every call; nothing stores or writes them. Presentation reads
  only this view, so the screen shows the forms a ClassWiz shows (plus `c*e`).
  An exact value outside them (`ln(3)`, `root(3,2)`, `pi^2`, 3+ terms) is
  displayed as a decimal but **stays exact in Ans and variables**: after
  `ln(3)`, `e^Ans` is exactly 3.
- `Value::factored()` is the fallback view for a value whose every term
  carries the same pi or e next to a root, which the one-irrational-per-term
  rule above rejects: `integral(pi*sqrt(4x^2+1)x,0,1)` shows
  `pi(5sqrt(5)-1)/12`, `sqrt(5)*pi/12` shows `sqrt(5)pi/12`. Every term must
  be `c*pi` or `c*sqrt(n)*pi` (or all e), one or two of them, read off the
  normal form by `ExactNumber::constantTerms` -- never computed by dividing,
  because presentation runs on the loop task and an exact division there
  overflowed its 8 KB stack (`Stack canary watchpoint triggered (loopTask)`).
  It is put
  over their common denominator with the positive term first, and the sign
  outside when both terms are negative. It is shown in Math I/O under the pi
  rule (|value| < 1E6) while the numerator, pi and parentheses included,
  fits 12 characters, the denominator 6 digits and radicands stay < 1E6.
  FORMAT offers Decimal only. This view exists only in the typed result
  presentation; legacy `formatValue()` text (Equation solution pages) still
  shows such a value as a decimal.
- Presentation (`calculator_presentation.cpp`) shows a fraction while it fits
  10 characters, and a `c*sqrt(n)/d` term while radicand n < 1E6, the numerator
  (coefficient and radicand digits) fits 10 characters, and the denominator fits
  6 digits (`25sqrt(80278)/276` from `1000*sqrt(41*89*22)/(4*2760)`). Two-term exact
  views require both terms to fit individually and their combined visual width
  to be <= 20 characters (`w(e) + 1 + w(extra) <= 20`). This deliberately relaxes
  ClassWiz's |c|, d < 100 and older n < 1000 limits.
- **S<->D on approximate results**: FORMAT > Improper/Mixed is also offered for
  an inexact value (a non-polynomial numeric integral's `1.75`) whose 10 displayed
  significant digits read back as the simplest continued-fraction convergent
  that fits 10 chars (`7/4`). This is only a view, so the value stays
  approximate and Standard keeps the decimal.

## The normal form (`ExactNumber`)

A sum of terms, each

```
coefficient * root(k, n) * pi^a * e^b * ln(p)^i * ln(A)^j * root(m, B)^l
```

| Part | Canonical rule |
|---|---|
| coefficient | `Rational`: den > 0, gcd 1. Rationals are stored inline without heap (the common case). |
| `root(k, n)` | n is free of k-th powers of primes < 1000 and not a perfect d-th power for any d \| k (`root(6,8)` is `sqrt(2)`). |
| `pi^a`, `e^b` | rational exponents (`sqrt(e)` is `e^(1/2)`). |
| `ln(p)` | p is a prime < 1000 or a cofactor with no such factor and not a perfect power: `ln(12)` is `2*ln(2)+ln(3)`. |
| `ln(A)` | A is a non-rational exact value with A > 1: `ln(-1+sqrt(2))` is `-ln(1+sqrt(2))`. |
| `root(m, B)` | a nested radical that did not denest; B is primitive (coprime integer coefficients) and positive; exponent in [1, m), or (-m, 0) when B has no exact inverse. |

Every atom is positive, so a single term's sign is its coefficient's. Terms
are sorted (rational, radicals, then transcendental) with one term per
monomial, so **equal values have equal representations**. That makes
`operator==` value equality and `zero()` a structural test.

Nonzero-ness of distinct monomials rests on the standard CAS assumption that
pi, e and the logarithms of distinct primes are algebraically independent
(proven for the linear logarithm case by Baker; conjectural, per Schanuel, for
products with pi and e). Distinct radicals are linearly independent
(Besicovitch).

### Simplification rules

- **Rational normalization** at every step (`Rational::of`).
- **Perfect powers leave radicals**: `sqrt(72)` -> `6*sqrt(2)`. Trial division
  stops at 1000, and a whole leftover cofactor that is a perfect power is still
  extracted. A hidden large square (`sqrt(1009^2*2026)`) is caught when two
  terms are merged: `root(k,n)` and `root(k,m)` are commensurable exactly when
  `m*n^(k-1)` is a perfect k-th power. Zero detection therefore never depends
  on complete factoring.
- **Like terms combine**: `pi+pi` -> `2*pi`, `sqrt(2)*sqrt(3)` -> `sqrt(6)`,
  `sqrt(2)*root(3,2)` -> `root(6,32)`.
- **Sign normalization**: odd roots of negatives are negative
  (`root(3,-54)` -> `-3*root(3,2)`); even roots of negatives are `NotReal`.
- **Rationalized denominators** (`inverse()`):
  - A monomial inverts directly.
  - Sums of square roots multiply by the conjugate under each
    `sqrt(p) -> -sqrt(p)` automorphism of a coprime basis of the radicands.
  - Two terms with higher roots use the geometric-series identity.
  - A common transcendental factor is divided out first
    (`1/(pi+pi*sqrt(2))`).
  - A multi-term transcendental denominator (`1/(1+pi)`) is **Inexact**.
- **Proportional quotients**: `ln(100)/ln(10)` -> 2.
- **Denesting**: `sqrt(a+b*sqrt(c))` when `a^2-b^2c` is a rational square
  (`sqrt(3+2*sqrt(2))` -> `1+sqrt(2)`); `sqrt(a*sqrt(p)+b*sqrt(q))` after
  pulling out `sqrt(p)` or `sqrt(q)` (`sqrt(57sqrt(17)+68sqrt(10))` ->
  `17^(1/4)*(2sqrt(10)+sqrt(17))`). Other nested radicals become
  `root(m, B)` atoms: exact, but not simplified further.
- **Perfect squares of sums** of rationals and pi/e/log monomials have their
  root, found by the polynomial square root on leading terms and verified
  exactly: `sqrt(pi^2+2pi+1)` -> `1+pi`, `sqrt((pi-4)^2)` -> `4-pi`.
- **Commensurable nested radicals merge.** Unlike radicals of integers,
  nested ones can be linearly dependent: `sqrt(5-sqrt(5))` is
  `(sqrt(10)+sqrt(2))/2 * sqrt(5-2sqrt(5))`. When two square-root atoms
  `root(2,A)`, `root(2,B)` meet in one sum and `sqrt(A*B)` needs no new atom,
  `root(2,B)` is rewritten as `sqrt(A*B)/A * root(2,A)` (onto the
  structurally smaller atom), so a zero sum is structurally zero. Two atoms
  that are *separately* built and never summed can still differ in form for
  equal values; compare by `(a-b).zero()`, not `==`, when that matters.
- **exp/ln fold**: `exp(r + sum c_i ln(A_i)) = e^r * prod A_i^c_i`.
  Anything else under `exp` (for example `e^pi`, `2^pi`) is Inexact.

### Status, limits and errors

Every operation returns one of: **Exact**; **DivisionByZero** or **NotReal**
(certain; the engine turns them into Math ERROR); or **Inexact** (no exact
form; the engine continues in decimal). An exact-looking result is never only
approximately right. Bounds, each past which the result is Inexact:

- 4096 bits per integer (`2^4095` is exact, `2^4096` is not)
- 64 terms
- nesting depth 4 of `ln()`/`root()` atoms
- radical index 64
- 1000 digits for `approximate()`

The calculator's own range (9.999999999E99, flush below 1E-99) is still
applied to `number` at every evaluator node, so intermediates stay far below
those bounds.

### Sign and approximation

`sign()` and ordering in `compare()` evaluate the terms as binary floats over
`BigInt` mantissas, with an error bound for the sum. Precision rises until the
bound certifies the requested digits, up to 20000 bits. Equality never uses
this path; it only orders values already known to differ. π and ln 2 are
stored to 320 bits (so 40-digit work runs no series), and higher precision
uses Machin and atanh series. `toDecimal()` rounds once, straight to the 40
work digits. `approximate(d)` gives d significant digits (1..1000), half away
from zero, trailing zeros trimmed.

## Engine integration

- **Literals are exact rationals** at any length (`0.1` is `1/10`); `pi`
  and `e` are exact atoms.
- `+ - * /`, powers, `sqrt`, `root`, `ln`, `log`, `exp`, `abs`, `int`/`floor`
  of rationals, `dms`, `%`, `!`, nPr/nCr, Σ and Π go through `ExactNumber`
  when both operands are exact.
- **Engineering symbols** (CATALOG > Engineer Symbol, m μ n p f k M G T P E)
  are postfix ×10ⁿ operators bound like `%` (`5k` = 5000, `(5k)²` for `5k^2`),
  exact since 10^|n| ≤ 10^18 is an integer. Each is one private source byte
  0x10–0x1A (`calculator_eng_symbols.h`): atomic for DEL, drawn from the
  reference bitmaps, shown as its ASCII letter in Linear text.
- **Parentheses in template fields.** `(` and `)` each type exactly one
  paren (no auto-pair, no jump out of a template). Inside a natural template
  field (∫ body or limit, fraction box, exponent, ...) a paren with no partner
  in that field is a private source byte, 0x05 `(` / 0x06 `)`
  (`calculator_loose_paren.h`), so `integral(` keeps its own commas; a matched
  pair is the real `()`. The editor re-pairs after every edit. The parser
  closes a loose `(` at its field's end; a loose `)` is Syntax ERROR. Function
  keys (`sin(` ...) still insert closed calls inside a template.
- **Trigonometry**:
  - `sin`/`cos`/`tan` are exact at multiples of 30 and 45 degrees in every
    angle unit, recognized from the exact angle (a rational in Deg/Grad,
    `q*pi` in Rad).
  - `asin`/`acos`/`atan` return `pi/6`-style exact angles for the matching
    exact arguments; `deg`/`rad`/`grad` convert exactly.
  - `Pol`/`Rec` are exact where both parts are (`Pol(1,1)` = `sqrt(2)`, 45).
- **The decimal twin.** Each operation also computes `number` from its
  operands' decimals, as before, because that costs nothing and loses nothing.
  It is replaced by `toDecimal()` when:
  - the result is rational (cheap and exactly rounded);
  - a sum fell 20 orders below its operands (cancellation);
  - it is the published answer (once per EXE, then rounded to 23 digits as
    always).
  Irrational `toDecimal()` costs roughly 20–120 µs on a PC (about 1–5 ms on
  the reference device), so it is not run at every node.
- **Verify** compares exact sides exactly (`compareValues`).
- The polynomial/simultaneous solvers test zero on the exact form.
- **Approximate evaluation stays separate.** Numerical d/dx, ∫ and the
  Solver bind their variable as an inexact decimal
  (`Value::approximate`), so integrands run at decimal speed; their results
  are inexact; those kernels take exact inputs and produce approximations.
  Symbolic differentiation, integration and solving are the separate CAS
  commands in Calculate ([CAS.md](CAS.md)), built on `ExactNumber`.
- **Equation > Solver finds every real root** (at most 8, ascending as
  `x1..xn`, one page each, no residual page). CAS `solve` names the roots
  exactly when it can; anything it leaves unevaluated, or a parameter it
  cannot see through, goes to damped Newton from the initial value plus a
  160-point sign-change scan around it (decimal roots). The root nearest the initial value is
  assigned to the variable. Answer pages follow Polynomial: FORMAT toggles
  exact/decimal, VARIABLE stores the shown value, Left/Right scroll only a
  wide answer and otherwise page, and the last page returns to the editor.
- **Storing Equation answers.** On any Simul Equation, Polynomial (roots,
  complex roots with their imaginary part, Min/Max pages) or Solver answer
  page, **SHIFT then a letter key** stores the shown value into that
  variable (SHIFT 4 5 6 1 2 3 = A-F, SHIFT 0 / `.` / x10 = x / y / z, as the
  editor types them). "Stored in A" replaces the header for one key and the
  page stays, so Down then SHIFT 5 stores the next root into B. VARIABLE >
  a cell > Store result works on the same pages. Inequality answers are
  intervals: SHIFT+letter shows "Cannot store", VARIABLE does nothing. Ans
  is never touched. Calculate's `solve()` does the same ([CAS.md](CAS.md)).
- **Numeric ∫** (`integral(f,a,b[,tol])`, default tol 1E-10, after NumWorks
  Poincare's `ApproximateIntegral`):
  - a bound beyond ±1000 maps its side onto a finite interval,
    `x = origin ± s/(1-s)` (both beyond: split at 0), so `e^(-x^2)` over
    `[0, 100000]` is not lost between nodes;
  - an integrand undefined at an end (`1/sqrt(x)`, `ln(x)`) uses tanh-sinh
    quadrature, whose nodes crowd the ends and never touch them;
  - otherwise globally adaptive Gauss 7 / Kronrod 15: 8 starting pieces (a
    narrow peak is seen), then the piece with the largest error estimate is
    bisected until the summed estimate is within tol or within 1E-15 of the
    value (`e^x` over `[0, 200]` is 7E86), at most 128 pieces (PSRAM);
    tanh-sinh is the fallback when that cannot settle (`sqrt(x)` near 0);
  - nothing settling is Time Out (`1/x` over `[0, 1]` diverges).
  - A fractional power `x^(p/q)` (q <= 100) in the integrand, as anywhere,
    is the q-th root by Newton (`Decimal::root`) and an integer power, not
    40-digit exp(ln): ~35x cheaper per node.
  - **Exact first, then a rough check**: on exact bounds the two exact forms
    below are looked for *before* the numeric run. With one in hand, the
    numeric value is only its check, settled to a rough 1E-7 and agreeing to
    1E-6 relative (the exact form's own decimal is then the twin). A cusp
    (`x^(1/3)` at 0) costs several bisections per digit past that, so
    `integral(2(x^(2/3)-x^(1/3))/3+x^(7/3),-1,1)` = `4/5` no longer pays for
    1E-10. A rough disagreement falls through to the full-tol run, which
    then returns the numeric value; no exact form at all means just the
    full-tol run. A tol of 1E-7 or looser skips the rough check and keeps
    the 1E-8 agreement below.
  - **Exact polynomial upgrade**: exact bounds sample the integrand exactly at 10 equally spaced points. When the
    finite differences vanish past degree d <= 8, Newton-Cotes over the
    interpolant is the exact integral. It is kept only if it agrees with the
    numeric value (to 1E-8 relative), because samples alone can be fooled
    (`sin(180x)` on whole degrees reads 0). So `integral(9-4x,0,9/4)` is `81/8`,
    and `*3*sqrt(2)` keeps it exact: `243/8*sqrt(2)`.
  - **Exact symbolic upgrade**: otherwise the CAS's definite integral
    (`Cas::definiteIntegral`, [CAS.md](CAS.md#definite-integrals)) is tried
    on exact bounds: `integral(4-4abs(cos(x)),0,pi)` is `4pi-8`,
    `integral(e^x,0,1)` is `e-1`, `integral(1/x,1,2)` is `ln(2)`. It too is
    kept only within 1E-8 relative of the numeric value. The engine reaches
    the CAS through a hook (`setExactIntegral`, installed by
    `calculator_cas.cpp`), so builds without the CAS stay numeric. An integrand
    without an elementary antiderivative (`e^(-x^2)`), one the CAS cannot read
    (a nested `integral`, `rand`), or a letter without an exact stored value
    keeps the numeric value.
- `Value(Decimal)` is exact only for int64 integers (library routines return
  integral results that way). Anything computed approximately uses
  `Value::approximate`.

## Memory

- Exact values own heap only past int64 rationals. `BigInt` keeps int64
  inline and shares big limbs; the normal form is one shared immutable block.
  All of it is allocated through `ExactAllocator`: PSRAM first, internal RAM as
  a fallback, abort if both are exhausted.
- History budgets it through `historyValueHeap` (`heapBytes()`, an
  overcount).
- On the 32-bit reference device, `sizeof(Value)` fell from 104 to 88 bytes, and the
  evaluator's recursion frame (eval + body) from 1776 to 1248 bytes of the
  32 KB Calculate stack. On 64-bit hosts `Value` grew slightly, which shifts
  host-only size prints such as the P11 retained count.

## Persistence

The store record v4 (`calc_v2`/`state`, see [ARCHITECTURE.md](../ARCHITECTURE.md#persistence))
saves each value's canonical `str()`, which `ExactNumber::parse` reads back to
the identical form. A form longer than 1024 characters is saved as decimal
only: dropping exactness is safe, and truncating its text is not. v2/v3
records (two int64 display terms) still load, converted to exact forms.
