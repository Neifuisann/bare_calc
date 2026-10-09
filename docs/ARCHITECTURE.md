# Architecture

The calculator is one UI-thread shell (`CalculatorApp`) over a shared session,
a set of app controllers, a single evaluation worker and a small platform
layer. Nothing in it draws to hardware: every screen is returned as a packed
192x63 frame ([DISPLAY.md](DISPLAY.md)) for the board's display driver.

## Module map (`src/`)

| Directory | Responsibility |
| --- | --- |
| `calculator/math/` | Numeric engine, exact/decimal arithmetic (decNumber, 40 digits), complex and Base-N operations, statistics kernels |
| `calculator/cas/` | Symbolic expressions, normalization, differentiation, integration and solving ([CAS.md](calculator/CAS.md)) |
| `calculator/apps/` | App controllers: Calculate (shared by Complex, Base-N, Matrix, Vector), Statistics, Distribution, Spreadsheet, Table, Equation/Inequality, Ratio, Math Box, the quick catalog |
| `calculator/runtime/` | App shell, shared session, job service, persistence, platform interface |
| `calculator/ui/` | Expression editing/layout, menus, grids, presentation, rendering, Home menu, status-icon mapping |
| `calculator/assets/` | Frozen generated glyphs, icons, constants and bitmap headers |
| `ui/` | Shared drawing grammar (`UiKit`), canvas packing (`CanvasText`), 2-bpp canvas, ClassWiz15 chrome font |
| `input/` | `ExpressionInput::Key`, the keypad-matrix key map, the AVR keypad wire format |
| `display/` | `PanelIcons::State` and the frame decoder (`FrameCodec`) |
| `system/` | Allocators that prefer external RAM when the target has it |
| `platform/` | A starting-point `CalculatorPlatform` for a single-threaded Arduino-core board |

`include/config.h` holds the only tunables (Math Box time zone).

Key modules: `calculator_app` (shell: Home, dispatch, alerts, debounced
persistence), `calculator_calculate` (the expression controller),
`calculator_session` (app ids, shared settings/memory/history and the
retention reducer: what survives an app switch, reset or setting change),
`calculator_job` (the single worker and its generation check),
`calculator_store` (the persisted record), `calculator_platform.h` (the only
device dependency), `calculator_storage.h` (boot-lifetime state placement).

## The shell API (`calculator/runtime/calculator_app.h`)

| Call | When |
| --- | --- |
| `openQuick()` | Boot and power-on: Calculate with the last result and history |
| `openHome()` | Optional: start on the Home menu instead |
| `press(Key)` | Every key event, on the UI thread |
| `service(frame)` | Every loop pass; returns true with a new packed frame when the screen changed |
| `desiredIcons()` | After a new frame: the full status-icon state for that frame |
| `sleepBudgetMs(max)` | How long the loop may sleep before the next `service()` (0 while a job runs) |
| `takeSystemSetup()` | True once after Settings > System Setup; the board shows its own setup page, then calls `redraw()` |
| `suspend()` | Power-off: persists and stops presenting |
| `alertDue()`, `countdownWakeMs()`, `countdownBlocksAutoPowerOff()` | Math Box countdown, for the board's power policy |

Home toggles between the last app and the menu. With no app visited since a
fresh `openHome()`, Home keeps the menu up. Nothing in the shell exits to
another mode.

## Jobs

`JobService` runs one evaluation at a time. The job is copied in and the UI
does not touch it while it runs; results are adopted on the UI thread inside
`service()`. Every `start()` and `cancel()` advances a generation counter, so a
cancelled job may finish but never publishes (Home, AC, Return, a keypad
interruption and an app switch simply cancel).

`CalculatorPlatform::startWorker()` may start a real thread or run the job
inline. Give a threaded worker a 32 KB stack (the measured reference) and do
not shrink it without worst-case measurements on the target (engine depth
caps do not bound the compiled stack footprint of every recursive path).

## Memory

Large, long-lived state (`Job`, `Result`, sessions, app datasets) is created
once on first use through `CalculatorStorage::makeState<T>()`, never from a
global constructor and never as a loop-stack local. It uses `malloc`; a board
with external RAM can place it there in `allocateState()`. Exhausting memory
is fatal by design (`std::abort()`), as with the other required allocations.

The reference device had 4 MB of external RAM and a 32 KB worker stack. An
8-bit AVR board cannot hold this code; pick a 32-bit board with generous RAM
or external RAM and measure the worst paths (CAS integration, spreadsheet
recalculation, matrix work) before committing to it.

## Persistence

`calculator_store` saves through `CalculatorPlatform::saveBlob/saveString`,
once the session has been unchanged for 2 s.

- `calc_v2` / `state` (blob): `CALC`, version byte 4, little-endian payload
  length and CRC-32, then the settings bytes (angle, IO, number format, digits,
  Complex Result, flag bits), Ans as a tagged real/complex scalar, PreAns,
  A–F/x/y/z as tagged scalars, f/g (≤ 256 bytes each), then MatA–D, MatAns,
  VctA–D and VctAns, each as rows, cols (0 = undefined) and every cell's
  decimal text row-major. Each real is its 40-digit decimal text plus its
  exact form as canonical text (`ExactNumber::str()`, empty when inexact or
  longer than 1024 characters; see
  [EXACT_ARITHMETIC.md](calculator/EXACT_ARITHMETIC.md)). Version 3 and
  version 2 records still load, converted; the next save writes version 4. A
  corrupt record is rejected whole and logged as
  `[CALC_STORE] v2 record rejected`.
- `calc_v1` / `state` (string): the older JSON record (ArduinoJson). Read only
  when no valid v2 record exists, migrated in memory, never rewritten.
- Replay history, Verify, the Base-N base, Equation/Inequality coefficients
  and screen state are never persisted.

## Log tags

`[CALCULATE]` (job timing and stack), `[CALC_STORE]` and `[CLOCK]` lines go
through `CalculatorPlatform::logf()`. Keep the tags if anything parses logs.
