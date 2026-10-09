# Spreadsheet app

Home > Spreadsheet (shortcut **4**) opens a local 45-row by 5-column sheet,
A1 through E45. Calculations run on the existing cancellable calculator worker.
Everything runs locally. Values, formulas and cached results are volatile;
the sheet survives Home and app changes, and clears on power-off, native ON,
Settings & Data reset and Initialize All.

## Controls

- Arrows select a cell; Page Up/Down move four rows. Column E scrolls into view.
- Typing replaces a cell. **EXE** commits and moves down. **OK** edits its
  existing value or formula. Return abandons a draft; DEL clears a selected cell.
- SHIFT + `(` inserts `=`. SHIFT + 4/5/6/1/2 enter A/B/C/D/E respectively.
- A constant such as `7*5` or `A2+7` is evaluated once and stored as a value.
  A leading `=` retains a formula and its cell dependencies.
- CATALOG > Spreadsheet supplies Grab, `=`, `$`, `:`, Min, Max, Mean and Sum.
  Grab parks the original editor, lets arrows select a reference, then OK inserts
  its address back into the original editor. Return cancels the selection.
- TOOLS: Fill Formula, Fill Value, Edit Cell, Available Memory, Cut & Paste,
  Copy & Paste, Delete All, Recalculate, Auto Calc, Show Cell.
- Fill edits the Form/Value and Range lines, then Confirm applies the range.
  A range is written `A1:B3`. Fill Formula adds `=` automatically. Fill Value
  freezes each adjusted expression's result.
- Copy & Paste stays armed for repeated pastes; cut exits after one paste.
  Return or AC cancels standby. Copy adjusts relative references; cut preserves
  their spelling. `$A1`, `A$1`, `$A$1` fix the column, row, or both.
- Auto Calc defaults to On; Off retains cached formula values until Recalculate.
  Show Cell defaults to Formula; Value changes the bottom readout. Neither
  option changes the stored formula. Changes to shared variables, angle or f/g
  require explicit Recalculate, as in the manual.
- The shared VARIABLE, Function and SETTINGS menus remain available. Variable
  storage uses the selected cell's numeric value; picker insertion returns to
  the cell editor. Spreadsheet entries do not advance Ans, PreAns or history.

## Limits and evaluation

Constants round to 10 significant digits when committed. Formula results use
the same precision. Constants account for 14 native bytes; a formula accounts
for 15 plus its input token bytes, including `=`. Input is limited to 49 tokens.
Functions and their opening parenthesis count as one native command token.

The capacity is **2,380 bytes**, matching the supplied Available Memory capture.
There is a documentation discrepancy: the current fx-880BTG/fx-991CW online
error pages say 1,700 bytes; another current Casio CW manual says 2,380. The
provided target capture takes precedence for this implementation.

Dependencies are resolved iteratively with compact bitsets in PSRAM, avoiding
recursive cell evaluation on the device stack. Writes and fills operate on a
worker-owned snapshot and publish together only if still current. Invalid input,
cycles, capacity overflow and cancellation retain the previous sheet. Copied
references leaving A1:E45 become `?`, with `ERROR` displayed in that cell.
Errors in existing dependent formulas also display `ERROR`.

Empty cell references currently evaluate to zero, including cells in aggregate
ranges. The manuals do not specify blank-cell aggregate behavior or the precise
native threshold for the documented chain-reference Memory ERROR. These remain
target-probe questions; no undocumented chain threshold has been invented.

## Evidence and validation

Behavior sources, read 2026-10-05:

- [Casio fx-880BTG Spreadsheet](https://support.casio.com/global/vi/calc/manual/fx-880BTG_vi/using_calculator_apps/using_spreadsheet.html)
- [Casio fx-991CW Spreadsheet](https://support.casio.com/global/en/calc/manual/fx-570CW_991CW_en/using_calculator_apps/using_spreadsheet.html)
- [Casio fx-880BTG errors](https://support.casio.com/global/vi/calc/manual/fx-880BTG_vi/technical_information/errors.html)
- [Casio CW 2,380-byte error documentation](https://support.casio.com/global/en/calc/manual/fx-97SGCW_en/technical_information/errors.html)

`test/calculator_references/spreadsheet/` is an ignored, local-only directory
for the user's BMP and 16 supplied captures. Reference images are excluded from
Git. The BMP is a 192x63 drawable frame. The PNG captures include an icon strip
and border; review uses the drawable crop `(1,11)-(193,74)`.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1 -Only spreadsheet`.
It tests the manual's five examples, constants/formulas, mixed absolute
references, rectangular aggregate functions, cached recalculation, memory and
input limits, circular/error handling, app retention, E45 navigation, cancelled
worker adoption, Grab, menus and caret blinking. It writes real controller
PackBits output as PGM images under `.pio/check/spreadsheet-frames/`.

`tools/render/review_spreadsheet.py` checks a frozen SHA-256 of the normalized blank
frame, so the pixel gate runs without reference images in a fresh checkout.
When local captures are available, it additionally enforces **zero differing
pixels** against the blank BMP and writes
`.pio/check/renders/spreadsheet/reference-review.png` with the other supplied
captures beside the real renderer. `--references` selects another local directory.
Shared chrome glyphs and
some menu marks differ slightly from the native captures; those are visual
review evidence, not asserted exact menu goldens.

The complete pipeline also builds production and recovery firmware and runs
the existing app, font, render, reference, input, watchdog and other checks.
The 2026-10-05 integration run (`tools/check.ps1 -Jobs 2`), after integrating upstream
merge `940a905`, passed all 29 stages in 228.3 seconds, including 88 Spreadsheet
checks and the zero-difference blank grid comparison. The merge's Equation
result layout fix and all Spreadsheet edits are retained together.
Production firmware is 2,614,464 bytes with 334,656 bytes of
partition headroom and 28.4% static RAM use; recovery is 1,095,504 bytes with
84,144 bytes of partition headroom. The existing reference suite reports
314 passes, one known gap and 93 pending checks, with no failed cases.
Per-stage output is saved in `.pio/check/spreadsheet-merge-check.txt`.
After excluding the images from Git, the Spreadsheet stage passed again.
The image-free review also passed, and a one-pixel change correctly failed its
digest gate.

Host frames and successful builds do not establish physical panel/keypad
acceptance. Flashing and on-device entry, paste/fill timing, OFF/ON and long-sheet
interaction remain hardware acceptance steps.
