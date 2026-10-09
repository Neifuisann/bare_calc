# P1 ClassWiz references

Authoritative source: external engineering specification `991CN-X-CW-Decompilation/research/CLASSWIZ_COMPATIBILITY_ENGINEERING_SPEC.md`,
sections 3.5, 3.6, P1 and 11. The JSON files pin its SHA-256 and preserve each
row's reference outcome, evidence locator, confidence and model scope.
No production compatibility behavior changes in P1.

## Run

P10 status: **127 cold cases, 133 pass, 13 known gaps, 32 pending,
zero failed cases**. P9 previously moved twelve supported checks to pass: C50-3,
C60-1..5, C61-2, C62-1..2, GEO-17-1, GEO-19-1, GEO-20-1.
The four-row Settings requirement supersedes C62's old three-row root.
P10 resolves C58/C59 and the GEO-22 Back band. M-03 remains pending because
`bench.json` still records the missing physical Settings capture. M-04's
cell geometry passes; its separate seven-row versus nine-row text difference
is not resolved. See [display guide](../../docs/hardware/DISPLAY.md) and
[fixture review](../calculator_render/P9_REVIEW.md).

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1 -Only engine,home,font,render,stress,apps,reference
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1 -Only reference
```

Both compile the real app/controller/editor/layout/render/session/job sources
against `calculator_platform_fake.cpp`. Python validates the ledger and starts
one native process per case. The suite requires the existing sibling Python
venv, MSYS2 g++, vendored GFX and resolved ArduinoJson. No ROM is needed.

## Coverage and ledger

- `editor.json`: C01-C43, including intermediate deletion/navigation states.
- `app.json`: C44-C78, including results, history, errors, all five AC menu
  variants, and the P3 pending traversal probes C73-C78. C70's CN log template
  is explicitly model-specific and pending.
- `geometry.json`: GEO-01 through GEO-23, **in section 3.6 table-row order**.
  These IDs differ from the seven grouped GEO examples in section 11.4; the
  title/reference quantity identifies the exact authoritative row.
- `known_gaps.json`: every E/L/R/A/M/I entry from section 4, with reverse links
  to executable checks. An empty check list means not covered by this phase,
  **not fixed**. E-01 and the matching I/M entries are reference rules, not
  claims that every entry in this registry is an outstanding defect.
- `bench.json`: two blocked physical captures and unavailable runtime baseline.

There are 127 cold cases and 178 independent checks. Each check carries its own
`native: "pass"`, `native: {"gap": "E-06"}`, or explicit pending status. A pass
mismatch fails; a matching gap fails until that check and its ledger backlink
are updated. No automatic baseline acceptance exists. Case-level `native` and
`gap_ids` summarize those checks and are validated against them.

Checks are split so a visual gap cannot conceal a working logical check. A
capture compares only its specified fields; missing/unknown observations and
keys are harness errors, never known gaps. Subset comparisons preserve the
logical/geometry/raster/icons ordering in the expectation. Pending checks have
null expectations, a reason, an owner and a resolution plan.

P1 extends section 11.2's single `expect` object into independent `checks` at
named `capture` steps, so one scenario can track separate gap IDs without
masking unexpected corrections. Nested observation fields retain `logical`,
`screen`, `menu`, `result`, `caret`, `history` and `icons`. `measure` requests
exact raster-region bounds, ink dimensions/counts, grey presence or fraction
gaps. `delta` compares numeric observations across two captures (script advance).
All raster coordinates have zero tolerance; 1-bpp output is expanded to levels
0/3 and 2-bpp output retains 0/1/2/3. Carets are masked only when requested;
empty-slot measurements use a separate caret-off render. No native frame hash
is substituted for an unknown reference hash. Full ROM raster goldens remain
unapproved; region measurements assert only the published evidence.

`logical.source` is a justified native encoding of observed structure, not a
claim that the ROM stores that string. P2 writes newly keyed powers in postfix
`^()` script syntax (`9^(3)`, `2x^(3)`, `*10^()`); legacy `pow(...)` remains
readable, evaluable and renderable for stored history, f/g and NVS, and mixed
forms evaluate identically. Disambiguating C51/C52 assert numeric values
independently of source spelling.
Unknown C42 base scope/value, C70 target log template and target menu Latin
glyph height remain pending. Existing Home captures are compared exactly.

## Observation and asynchronous behavior

`CalculatorApp::snapshot(lastFrame)` returns detached data, built only with
`CALCULATOR_REFERENCE_TEST`. It reads the controller and `lastRender()` without
rendering, ticking the clock, adopting work, modifying menu indices or retaining
another framebuffer. The test owns and passes the latest service frame. Menu
screens do not expose stale math caret geometry. Firmware does not compile the
accessor, its strings, or any extra state.

Each key enters through `CalculatorApp::press`. The fake parks every job;
the runner calls `service`, runs the pending worker, then adopts through
`service`, with a bounded loop. Each case cold-starts with cleared fake NVS,
MathMath/Degree/Norm1, empty history and Calculate selected. Snapshot mutation,
deferred completion, service-only adoption and cancelled completion are tested.
Negative fixtures prove pass regression, unexpected gap correction, unknown
observation and unknown semantic key all fail.

## Findings and deliberate deviations

Full-controller replay resolves the research's unverified positions:

- **A-13 (new)**: C48 produces `21`, not `12`; successful-result BACK uses
  `result.position`. This is a P11 follow-up, unchanged here.
- **E-14**: C58 BACK lands at offset 1; C59 BACK at offset 3, yielding `1**32`.
- **R-05** (P12 closure): FORMAT is setting-aware — `formatMenu=false`
  (firmware default, Decimal toggle) flips the view on plain FORMAT and opens
  the menu on SHIFT FORMAT; `formatMenu=true` (Menu) does the reverse. C50 pins
  `formatMenu=Menu` in its initial settings (matching its emu-cw evidence) and
  passes fully; geometry-only FORMAT fixtures keep SHIFT FORMAT under defaults.
- Snapshot plumbing also touches `calculator_calculate.h/.cpp` because the
  shell cannot observe controller-private state without that read-only bridge.
- The optional reference kit lives outside this project
  (`991CN-X-CW-Decompilation/calc-reference-kit/README.md`).
- `BOOST.md` was not present in this checkout or its workspace parents. The
  provided user instructions, CLAUDE.md and subsystem docs were followed.

No case was fixed by changing firmware behavior. Geometry measurements express
published facts; gaps remain owned by P2 onward. The specified physical capture
path no longer exists, so the two requested files are absent and marked blocked,
not replaced with emulator/native captures.

## Maintaining the transcription

`tools/references/transcribe_calculator_references.py` is the reviewed transcription source.
It reads the sibling specification to attach row text/provenance and emits the
three case files and ledger. It never runs native code or learns expectations
from native output. Update it together with JSON when implementing a later
phase; `tools/checks/run_calculator_references.py` validates completeness and backlinks.

## Earlier-phase file inventory

Production behavior changes (P5): `src/calculator_editor.h/.cpp` (outer
`Edit` transaction guard with depth counter, `insArmed` + `setMathInput`
policy, `wrapFollowing`, Root-only `isInsTemplateKey`, SHIFT jumps across
`jumpEnclosing`, open Sin/Cos/Tan/Log keys, logical `CaretKind`),
`src/calculator_document.h/.cpp` (`operandEnd`, `jumpEnclosing`; traversal
untouched), `src/calculator_calculate.cpp` (CATALOG/ordinary-call
`insertExpression` open form, `disarmIns` on menu/execute/return paths,
`syncInputStyle` from Input/Output, logical caret kind in the reference
snapshot), `src/calculator_render.h/.cpp` (`RenderInfo::caretKind` mirror,
same caret geometry).
Production behavior changes (P4): `src/calculator_editor.h/.cpp`
(`backspace` structural rules E-08/E-09/E-10, new private `deleteForward`,
`splice` plus file-local `nothingLeftOf`/`spliceOnDelete`; traversal
untouched).
Production behavior changes (P3): `src/calculator_document.cpp`
(`vertical` skips Script/Power, `left`/`right` fraction exits E-06/E-07),
`src/calculator_editor.h/.cpp` (`wrapHorizontal` policy, root-boundary
wrapping E-04/E-05), `src/calculator_calculate.cpp` (main entry editor
`wrapHorizontal=true`, field editor `false`).
Production behavior changes (P2): `src/calculator_editor.h/.cpp`
(`insertScript`, Power/Square/Scientific key actions),
`src/calculator_render.cpp` (`presentResult` scientific `*10^(…)`).
Host-only additions (P1): `src/calculator_app.h/.cpp`,
`src/calculator_calculate.h/.cpp`.

Tests: `test/calculator_reference_test.cpp`; this README; `editor.json`,
`app.json`, `geometry.json`, `known_gaps.json`, `bench.json`.
`test/calculator_editor_stress_test.cpp` is the P4 cursor-validity fuzz
(smoke 40×200 in the normal suite, full 1000×200 via `-Stress`), extended in
P5 with SHIFT/INS/open-call keys plus depth/stale-INS assertions.
`test/calculator_render_test.cpp` gains `p5EditorPolicyExpectations` (the
35-semantic focused checks: transactions, INS, SHIFT jumps, open calls,
caret kind, Linear overwrite, invariants).

Tools: `tools/test_calculator.ps1` (new `-Stress` option), `tools/checks/run_calculator_references.py`,
`tools/references/transcribe_calculator_references.py`. Documentation:
`docs/development/BUILD_TEST_FLASH.md`. The external kit's README lists its own files.
# P10 continuation

C58/C59 and GEO-22 now pass their confirmed modal, error-cursor, icon and
Back-band checks. APP-ERR-01/02 alias C58; APP-ERR-03 aliases C59. Two explicit
modal observations were added: 127 cold cases, 178 checks, 133 pass, 13 known
gaps, 32 pending. `error.title/origin/cursor` are detached host observations.
P11+ gaps remain. The P9 root now has Get Started [14]; its supplied BMP was
clarified as emulator/Paint, not physical. M-03 remains pending under the
user's explicit authorization to continue P10 with physical gaps documented.
See `docs/P9_CAPTURE_CLOSURE_REPORT.md` and `docs/P10_ERROR_REPORT.md`.
