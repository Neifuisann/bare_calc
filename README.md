# bare_calc

A self-contained ClassWiz-style scientific calculator in portable C++17: the
apps, the math and symbolic (CAS) engines, the UI, fonts and assets, and a
host test suite. It is fully offline and has no connectivity code of any
kind.

## What is in it

- **Apps**: Calculate (with Complex, Base-N, Matrix, Vector), Statistics,
  Distribution, Spreadsheet, Table, Equation, Inequality, Ratio and Math Box
  (clock and countdown), plus the Home menu, Settings, catalogs and help.
- **Math**: decNumber-backed 40-digit arithmetic, exact numbers
  ([docs/calculator/EXACT_ARITHMETIC.md](docs/calculator/EXACT_ARITHMETIC.md))
  and symbolic algebra ([docs/calculator/CAS.md](docs/calculator/CAS.md)).
- **UI**: the math editor and layout, menus, grids, rendering, status-icon
  mapping, the `UiKit` drawing grammar, the ClassWiz fonts and every baked
  asset ([docs/DISPLAY.md](docs/DISPLAY.md)).
- **Input**: the `ExpressionInput::Key` set, the Casio keypad-matrix key map,
  and the wire format of an AVR keypad scanner.
- **Tests and tools**: host tests, render and reference fixtures, review
  scripts and asset generators.

The Home menu has 13 apps; Math Box sits alone on the last row. The Math Box
clock reads the board's own wall time (`CalculatorPlatform::wallClock()`,
from an RTC or a user-set time) and shows "Clock not set" until there is one.

`tools/check.ps1 -Only isolation` keeps the sources offline: it fails on any
mention of networking or connectivity in `src/` or `include/`, comments
included.

## Layout

| Path | Contents |
| --- | --- |
| `src/calculator/` | Apps, math, CAS, runtime shell, UI, generated assets |
| `src/ui/`, `src/input/`, `src/display/`, `src/system/` | Shared drawing, keys, frame/icon contracts, allocators |
| `src/platform/` | `calculator_platform_arduino.cpp`, a starting point for a board |
| `include/config.h` | Math Box time zone |
| `lib/` | Adafruit GFX (canvas only), decNumber 3.68, ArduinoJson 7.4.3, each with its license |
| `assets/` | ClassWiz math font (OFL) and glyph overrides, inputs to the font baker |
| `test/` | Host tests, fakes, frozen render/reference fixtures |
| `tools/` | `check.ps1`, render/reference gates, review sheets, asset generators |
| `docs/` | [ARCHITECTURE.md](docs/ARCHITECTURE.md), [DISPLAY.md](docs/DISPLAY.md), calculator guides |

## Checking it on a PC

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1
```

Needs MinGW-w64 g++ (default `C:/msys64/mingw64/bin/g++.exe`, or `-Compiler`)
and, for the render, spreadsheet and reference gates, a Python with Pillow
(`-Python`, `$env:BARE_CALC_PYTHON`, or the nearest `*/.venv` above this
folder). Stages: isolation, editor, exact, engine, cas, home, font, uikit,
render, stress, apps, spreadsheet, mathbox, reference, platform. Logs and PNG
review sheets go to `.build/check/`. These checks prove the code on a PC, not
on a board.

## Bringing it up on a board

1. **Platform**: implement `src/calculator/runtime/calculator_platform.h`.
   `src/platform/calculator_platform_arduino.cpp` is a starting point for a
   single-threaded Arduino core. It runs evaluations inline and keeps storage
   in RAM, so replace its load/save functions with EEPROM/flash, and call
   `CalculatorPlatformArduino::setWallClock()` from an RTC if there is one. It
   is compile-checked only.
2. **Display**: decode frames and apply icons as in
   [docs/DISPLAY.md](docs/DISPLAY.md), which also has the Casio LCD
   controller reference (init sequence, frame update, icon columns).
3. **Keys**: deliver `ExpressionInput::Key` values to `CalculatorApp::press()`.
   `ExpressionInput::matrixKey(row, column)` maps the Casio keypad matrix, and
   `src/input/keypad_protocol.h` is the I2C frame format of the AVR keypad
   scanner.
4. **Loop**:

   ```cpp
   void setup() {
     // display, keypad, RTC ...
     CalculatorApp::openQuick();
   }
   void loop() {
     ExpressionInput::Key key;
     if (readKey(key)) CalculatorApp::press(key);          // board code
     static std::vector<uint8_t> frame;
     if (CalculatorApp::service(frame)) present(frame, CalculatorApp::desiredIcons());
     if (CalculatorApp::takeSystemSetup()) { showSetup(); CalculatorApp::redraw(); }
   }
   ```

5. **Build**: C++17. Compile every `.cpp` under `src/`, `lib/Adafruit_GFX`
   and the two decNumber `.c` files with `-DDECNUMDIGITS=40` (decNumber's
   `library.json` sets it for PlatformIO). ArduinoJson is header-only. Off
   Arduino, `test/render_fakes/` shows the small Arduino surface
   (`String`, `Print`) the canvas code needs.
6. **Memory**: see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#memory).

## Known limitations

- The Arduino platform file has not run on hardware, and its storage does not
  survive power-off until it is backed by EEPROM or flash.
- Settings > System Setup only raises `takeSystemSetup()`; the board supplies
  that page (for example auto power-off).
- Both font headers are frozen and hash-checked. The ClassWiz15 chrome font
  comes from an external baker that is not included.
  `tools/assets/bake_calculator_font.py` reads the math font in `assets/`, but
  the committed header was baked from an earlier revision of the glyph
  overrides, so a re-bake stops at the overrides audit until its policy table
  is updated.
