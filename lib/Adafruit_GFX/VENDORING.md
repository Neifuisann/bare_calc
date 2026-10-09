# Adafruit GFX, vendored

The `Adafruit_GFX` / `GFXcanvas1` core that every 192x63 frame builder draws
into (`canvas_text`, `ui_kit`, the calculator renderer and fonts).

- Upstream: <https://github.com/adafruit/Adafruit-GFX-Library>
- Imported from the PlatformIO registry package `adafruit/Adafruit GFX Library`
  **1.12.6** (2026-09-13)
- License: BSD (`license.txt`, unmodified)
- Files: `Adafruit_GFX.cpp`, `Adafruit_GFX.h`, `gfxfont.h`, `glcdfont.c`.
  `Adafruit_SPITFT*`, `Adafruit_GrayOLED*`, `Fonts/` and the examples are not
  imported; nothing here uses them.

The registry package declares `depends=Adafruit BusIO`, and
`Adafruit_GFX.h` includes BusIO's I2C and SPI device headers, which pull
in Arduino's `Wire` and its I2C driver. Nothing here uses them.

## Local delta

- **`Adafruit_GFX.h` — the two BusIO includes are removed.** Neither the
  header nor `Adafruit_GFX.cpp` uses a BusIO type; only the display-driver
  subclasses that were not imported do.
- **`Adafruit_GFX.cpp` — the vendor-specific `<pgmspace.h>` branch is
  removed.** The file's own `pgm_read_*` fallbacks cover every non-AVR core.

On an update, copy the four files again and redo both edits.
