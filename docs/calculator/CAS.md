# Symbolic algebra (CAS)

Calculate can simplify, expand, factor, differentiate, integrate, decompose
into partial fractions and solve symbolically. Results stay exact: rationals, radicals, pi, e and parameters are
never turned into decimals. Code: `src/calculator/cas/calculator_cas*.{h,cpp}`.
Tests: the `cas` stage of `tools/check.ps1`.

## Using it

CATALOG > **Symbolic** (Calculate only) inserts the commands. They are ordinary
calls inside an expression, so they nest and mix with arithmetic:

| Command | Does | Example |
|---|---|---|
| `simplify(f)` | smallest equivalent form | `simplify((x^2-1)/(x-1))` = `x+1` |
| `expand(f)` | multiply out | `expand((x+1)^3)` |
| `factor(f)` | factor over the rationals | `factor(x^3-x)` = `x(x-1)(x+1)` |
| `deriv(f[,v[,n]])` | n-th derivative in v (default x, 1) | `deriv(x^x)` = `x^x*(ln(x)+1)` |
| `integrate(f[,v])` | antiderivative (no +C) | `integrate(x*e^x)` = `e^x*(x-1)` |
| `partfrac(f[,v])` | partial fractions | `partfrac((x+3)/((x-1)(x+2)))` = `4/(3(x-1))-1/(3(x+2))` |
| `solve(f=g[,v])` | real solutions | `solve(x^2=2)` = `x=-sqrt(2), x=sqrt(2)` |
| `solve(f1=g1,f2=g2[,...][,v1,v2,...])` | a system | `solve(x+y=3,x*y=2)` = `(x,y)=(1,2), (2,1)` |
| `subst(f,v,value)` | substitute | `subst(x^2+1,x,3)` = `10` |

- Inside such an expression every variable letter (A-F, x, y, z) is a
  **symbol**, not its stored value. Ans/PreAns enter as their exact values; an
  inexact Ans is Argument ERROR rather than a decimal coefficient.
- `f(...)`/`g(...)` expand their definitions; `diff(f,a)` (the d/dx
  template) is computed from the symbolic derivative, so `diff(x^3,2)` is
  exactly 12.
- **Angle unit.** Trigonometry follows the setting. Internally every angle is
  in radians; in Deg, `sin(u)` is read as `sin(u*pi/180)` and displayed back
  in degrees, so `deriv(sin(x))` is `pi*cos(x)/180` and `solve(sin(x)=1/2)` is
  `x=30, x=150`. Use Rad for textbook forms.
- **Results.** An exact constant (`diff(x^3,2)`, `simplify(sqrt(8))`) is an
  ordinary answer: it becomes Ans and every FORMAT view applies. Anything else
  is a *formula*: drawn as natural math in the result region (scrolls when
  wide), kept in history, and Ans is unchanged. FORMAT offers nothing else for
  a formula.
- **Storing solve() roots.** A `solve` answer whose roots are all exact
  numbers also carries them as `Result::solutions` (`x1, x2...` for one
  unknown, `x`/`y` for one system solution, `x1`/`y1`, `x2`/`y2`... for
  several). On that result **SHIFT then a letter key** stores a root into
  that variable, with the editor's own mapping (SHIFT 4 5 6 1 2 3 = A-F,
  SHIFT 0 / `.` / x10 = x / y / z): one root stores at once ("Stored in A"
  in the notice band), several open a "Store in A" list to pick from.
  VARIABLE > a cell > Store result does the same. Only on such a result does
  SHIFT+letter not start a fresh line holding the letter. A root with a
  parameter (`solve(x^2=A)`) or a non-exact constant withholds the whole
  list, so nothing is storable; any other formula cannot be stored either
  (its `value` is a placeholder).
- What cannot be done symbolically stays unevaluated and exact, never
  approximated: `integrate(e^(x^2))`, `solve(x^5-x-1=0)`,
  `x^3/3+integrate(sin(x)/x)` (only the failing term of a sum).
- Verify mode, the other apps, CALC substitution and numeric `integral`,
  `sum`, `product` are unchanged (numeric). Inside a CAS expression the
  numeric-only functions (rand, gcd, int, integral...) are Syntax ERROR.

## Architecture

Independent modules over one expression tree. The import from the engine's
parse tree (`Node`, `parse()`) is the only coupling to the evaluator.

| Module | Role |
|---|---|
| `calculator_cas_expr` | The tree: immutable, reference-counted `Node`s in PSRAM (`ExactAllocator`), with `Number` leaves that are `ExactNumber`s. Structural equality, a total order, hashing, size/depth bounds, the `Context` (step budget, cancellation, sticky errors). |
| `calculator_cas_canonical` | Canonical constructors: every tree is built through them, so every tree is in normal form (below). |
| `calculator_cas_pattern` | Wildcards, commutative matching, templates, rewrite rules. |
| `calculator_cas_poly` | Univariate polynomials with exact coefficients: division, gcd, square-free decomposition, rational roots. |
| `calculator_cas_simplify` | `expand`, `together`/`cancel`, `factor`, `simplify`, identity rules, the zero test. |
| `calculator_cas_diff` | Differentiation. |
| `calculator_cas_integrate` | Integration. |
| `calculator_cas_partfrac` | Partial fraction decomposition (`partfrac`, and the integrator's rational functions). |
| `calculator_cas_solve` | Solving. |
| `calculator_cas_print` | Natural-display source and linear text. |
| `calculator_cas` | Import from `Node`, commands, angle units, `evaluate(Request)`. |

`JobService::run` sends a Calculate job to `Cas::evaluate` when
`Cas::handles()` finds a command in the expression; it runs on the Calculate
worker like any evaluation. `Result::formula` / `ResultSummary::formula`
carry a formula to the renderer.

### Canonical form

Automatic simplification, applied by every constructor:

- sums and products flattened and sorted; like terms and like factors
  combine (`2x+3x = 5x`, `x*x^2 = x^3`, `2^x*2^y = 2^(x+y)`);
- identities vanish: `x+0`, `x*1`, `x^1`, `x^0 = 1`, `0*u = 0`;
- a numeric coefficient distributes over a lone sum (`2(x+1) = 2x+2`), so
  `2(x+1)-2x` is 2 (`factor()` builds its products without this);
- constant subtrees with an exact value fold through `ExactNumber`
  (`sqrt(8) = 2sqrt(2)`, `sin(pi/6) = 1/2`, `ln(8) = 3ln(2)`); a logarithm
  whose value is a sum stays symbolic (`ln(10)`) but gives up a perfect
  power (`ln(100) = 2ln(10)`, `ln(b^c) = c*ln(b)` for a number `b > 0`, so
  `log(10^2022) = 2022`), and anything without an exact form stays a subtree
  (`sin(1)`, `2^pi`);
- **exact trig table** (NumWorks Poincare's `ExactFormula`): sin, cos, tan
  and their inverses are exact at every multiple of 15, 18 and 22.5 degrees
  (`sin(pi/12) = (sqrt(6)-sqrt(2))/4`, `cos(pi/5) = (1+sqrt(5))/4`,
  `tan(pi/8) = sqrt(2)-1`, `atan(2-sqrt(3)) = pi/12`); other rational
  multiples of pi reduce to `[0, pi/2]` (`cos(4pi/7) = -cos(3pi/7)`,
  `tan(62pi/21) = -tan(pi/21)`). Calculate's own (ClassWiz) trig is unchanged:
  exact at multiples of 30 and 45 degrees only;
- a numeric base cancels a logarithm in its own base from its exponent:
  `10^log(x) = x`, `10^(3+2log(pi)) = 1000pi^2` (`log(x)` is `ln(x)/ln(10)`);
- powers combine only where valid over the reals: `(u^a)^n = u^(an)` for an
  integer n or an odd-numerator a, otherwise through `|u|`
  (`sqrt(x^2) = |x|`); positive coefficients separate under roots;
- odd/even symmetry (`sin(-x) = -sin(x)`), inverse pairs (`sin(asin u) = u`,
  `e^(ln u) = u`, `ln(e^u) = u`, `asinh(sinh u) = u`, `atanh(tanh u) = u`,
  `acosh(cosh u) = |u|`), and on the principal branch
  `asin(sin(4pi/7)) = 3pi/7`, `acos(-1/2) = pi - acos(1/2)`;
- certain errors: `1/0`, `sqrt(-1)`, `ln(0)`, `tan(pi/2)` are Math ERROR.

### Patterns and rules

A pattern is a tree with wildcards constrained Any, Const (free of the
variable), Number, Integer or Linear (`a*x+b`, `a != 0`). Sums and products
match commutatively; a Const wild takes every variable-free operand and an
Any wild the leftover operands (identity when none), so `a*X+b` matches `3x+2`,
`x` and `x+5`. A pattern `u^n` also matches `u`. The derivative table, the
integration table and the simplifier's identities are rule tables built once
(`buildTable`, under a private context).

### Simplify

`simplify` rewrites with the identity rules (tan = sin/cos, sin^2+cos^2 = 1,
cosh^2-sinh^2 = 1 and their rearrangements) and returns the least complex of:
that form, the expanded form, the cancelled rational form (polynomial gcd),
and for small sums the factored form. Complexity is node count, then the
number of fractional constants, so `(sqrt(b^2-4c)-b)/2` beats
`-b/2+sqrt(b^2-4c)/2`.

`factor` extracts numeric and monomial content (including `e^x` from
`e^(2x)+e^x`), then factors the main variable over Q (square-free
decomposition, rational roots, primitive integer factors) or recognizes
`a^2-b^2`, `a^3+-b^3`, `a^2+-2ab+b^2` over monomials. Irreducible factors
stay (`x^2+1`, `x^2-2`).

### Differentiation

Sum, product, general power (`u^v`) and chain rules, recursively; each
function's outer derivative is a template in the rule table
(`d/du |u| = u/|u|`). Results are simplified.

### Integration

In order: constants and linearity; the table over a linear inner argument
(`X^n`, `1/X`, `A^X`, trig/hyperbolic and inverse functions, `ln`, squares
and reciprocals of trig functions, `1/sqrt(A+BX^2)`, `sqrt(A+BX^2)`,
`A^U*sin(V)`); power reduction (`sin^n`, `cos^n`, `tan^n`); rational functions
by partial fractions (exact linear factors, including radical roots, and
irreducible quadratics with any multiplicity; biquadratic denominators such
as `x^4+1` split); substitution (`g(u(x))u'(x)`, with sin/cos Pythagorean
rewriting); integration by parts ordered by LIATE (repeated for `x^n e^x`);
expansion.

Every antiderivative is differentiated and compared with the integrand by
the zero test. A result it disproves is discarded (never returned). The
generic `x^n` rule assumes `n != -1`.

### Definite integrals

`definiteIntegral(f, x, a, b)` serves the numeric `integral(f,a,b)` of
ordinary Calculate (it is not a CAS command): the engine tries it before the
numeric run (when the polynomial form is absent or disagrees) and adopts it
only when the two agree -- 1E-6 relative against a rough 1E-7 value, 1E-8
against one settled to a looser user tol
([EXACT_ARITHMETIC.md](EXACT_ARITHMETIC.md#engine-integration)).
`|g|` is resolved piecewise: `[a, b]` splits at every zero of each `g`
inside it -- all periods for `sin`/`cos` of a linear argument (solve() gives
principal branches only), factors of products separately, solve() for the
rest -- and on each piece `|g|` becomes `+-g` by its sign at the midpoint.
Each piece contributes `F(right) - F(left)` of its antiderivative, which must
be an exact number at both ends. That assumes `F` is continuous on the
piece; a jump inside it (or a missed zero) gives a wrong difference, which
the numeric agreement rejects. So `integral(4-4abs(cos(x)),0,pi)` splits at
`pi/2` into `2pi-4` twice: `4pi-8`. It runs under its own 300k-step budget,
and any failure leaves the numeric value.

### Partial fractions

`partfrac(f[,v])` and the integrator's rational-function strategy share one
decomposition. Long division makes `f = P/Q` proper (`(x^4+1)/(x^2-1)` =
`x^2+1+1/(x-1)-1/(x+1)`); the monic denominator splits by square-free
decomposition (multiplicities), rational roots, exact radical roots of
quadratics (`x^2-2`), biquadratics into two quadratics (`x^4+1`), and
otherwise irreducible quadratics (`x^2+1`). The numerators -- `A` over a
linear factor, `Bx+C` over a quadratic, one per power up to the multiplicity
-- solve one exact linear system. A rational `Bx+C` is printed with its
content outside: `-(x+1)/(2(x^2+1))`. A numerator of 0 drops its term, so
a cancelling factor leaves nothing.

No root is ever numeric: a denominator that does not split exactly (an
irreducible cubic, a symbolic coefficient) or a non-rational `f` comes back
unchanged.

### Solving

`f = g` becomes `f - g = 0` over one denominator. Products split into their
factors. Polynomials: degree 1 and 2 exactly (radicals, symbolic
coefficients), rational roots by deflation at any degree, biquadratics
through `x^2`. Otherwise the variable is isolated through inverse functions
(principal branches: `sin(x) = 1/2` gives `pi/6, 5pi/6`), a radical is
eliminated by raising to its power, or a kernel such as `e^x` becomes a new
unknown. Every candidate is substituted back: one outside the domain or
disproved by the zero test is dropped (`sqrt(x+1) = x-1` gives only 3).
Output: `x=...` equations in ascending order, `No Solution`,
`Infinite Solution`, or an unevaluated `solve(...)` for what remains.

### Systems

`solve` with two to four equations (arguments up to the first bare
variable) is a system; the trailing variables, if any, are the unknowns.
Without them the unknowns are the x, y, z present, then other letters
alphabetically while there are fewer unknowns than equations; the remaining
letters are parameters (`solve(a*x+y=1,x-y=2)` solves for x and y).

Elimination, after NeoCalculator's `SystemSolver` but exact and verified:

1. An equation linear in an unknown, with a coefficient that is a known
   nonzero free of the unknowns, is solved for it and substituted into the
   others (the last unknown first, so x is solved last). This covers every
   linear system and curves cut by a line.
2. Otherwise the last unknown is eliminated by the Sylvester resultant of
   the equation of least degree in it with each other equation (a
   division-free determinant with memoized minors, order at most 10).
3. The one unknown left is solved by the single-equation solver; the values
   are substituted back, one unknown at a time.

Elimination only adds candidates, so each is substituted into every
original equation and dropped when it leaves the domain or the zero test
disproves it (`solve(sqrt(x)=y,x+y=6)` keeps only x=4, y=2). An equation
reduced to 0 = 0 with an unknown left is `Infinite Solution`; c = 0 ends
that branch. A step without an exact way forward (a residue the solver
cannot solve, a resultant that vanishes identically) leaves the whole
system unevaluated: an answer is always complete.

Output: one solution as `x=2, y=1`; several as tuples in ascending order,
`(x,y)=(1,2), (2,1)`.

### Zero test

`Yes` is proved: fold constants, expand, combine over one denominator, then
again after rewriting tan and multiple angles through sin/cos, even cos
powers through sin, and hyperbolics as exponentials. `No` is proved by an
exact nonzero value at a sample point (rationals, and multiples of pi for
trigonometry), evaluated in a sandbox so a sample outside the domain only
skips. Otherwise `Unknown`.

### Printing

Two printers over the same tree, both in the calculator's own syntax so the
parser reads them back:

- **natural** (the screen): the template grammar `EditDocument`/`MathLayout`
  draw as math: `frac(x^(2),2)`, `sqrt(x+1)`, `root(3,x)`, `abs(x)`, `e^(2x)`.
- **linear** (Line I/O via `LinearPresenter`, tests): `x^2/2`, `(x+1)/(x-1)`.

Terms read in descending powers of the main variable, constants last, two
terms positive first (`1-x^2`). Exact sums read as ClassWiz shows them:
`(-1+sqrt(5))/2`. Juxtaposition inserts `*` where the lexer would misread:
`pi*x` (`pix` is one name), `2*C` (C after an operand is nCr), `2*E`
(`2E+1` is a literal), `a*x`.

## Limits

| Bound | Value | Past it |
|---|---|---|
| nodes in one expression | 6000 | Stack ERROR |
| expression depth | 40 | Stack ERROR |
| terms in one distribution step | 2000 | Stack ERROR |
| work steps per EXE | 1.5 M | Time Out |
| formula source | 4000 bytes | Stack ERROR |
| integration strategy nesting | 8 | unevaluated |
| equations / unknowns in a system | 4 / 4 | Argument ERROR |
| degree of one unknown for elimination; resultant order | 8; 10 | unevaluated |
| exact numbers | as [EXACT_ARITHMETIC.md](EXACT_ARITHMETIC.md) | Stack ERROR |

Cancellation (AC) is polled every 64 steps. Host timings are 0.1-5 ms per
command; the reference device is roughly 20-50x slower. **Unverified on hardware:**
latency, the worker's stack high-water mark (`[CALCULATE] stack_hwm=` in the
log; recursion is bounded by the depth limit) and PSRAM use on large
expansions. On the host, typical commands reach 5-12 KB of stack and a
14-level nested power (which ends in Stack ERROR) about 20 KB, against the
worker's 32 KB.
