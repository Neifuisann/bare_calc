# Display

The calculator draws into off-screen canvases and hands the board a packed
frame. The board's display driver decodes it and puts it on the panel, plus
the etched status icons. This page covers that contract, the screen grammar
the renderer follows, and the reference for the Casio LCD controller the
original hardware drove, in case the new board drives the same panel.

## Frame contract

`CalculatorApp::service(frame)` returns true with a new frame only when the
screen changed. The frame is PackBits-compressed; `display/frame_codec.h`
validates and expands it:

```cpp
std::vector<uint8_t> frame;
if (CalculatorApp::service(frame)) {
  static uint8_t raw[FrameCodec::kGrayscaleBitmapBytes];
  const size_t size = FrameCodec::bitmapRawSize(frame.data(), frame.size());
  if (size && FrameCodec::unpackBitsRle(frame.data(), frame.size(), raw, size))
    drawFrame(raw, size == FrameCodec::kGrayscaleBitmapBytes);  // board code
  applyIcons(CalculatorApp::desiredIcons());                     // board code
}
```

| Layout | Bytes | Pixel (x, y), 0 ≤ x < 192, 0 ≤ y < 63 |
| --- | --- | --- |
| Monochrome | 1512 | bit `y*192 + x` of a continuous row-major stream, MSB first; 1 = ink |
| Grayscale | 3024 | 2-bit pair `y*192 + x`, high pair first; 0 = white … 3 = black |

On a monochrome-only panel, map levels 2–3 to ink and 0–1 to paper (or
dither). Several screens (Home, menus with a gray scrollbar, the error dialog)
are grayscale.

## Screen layout

All 63 rows are content. Two type scales:

- **Chrome** — `ClassWiz15` (`src/ui/font_classwiz15.h`, generated and
  frozen), the calculator's own face at 14 px on a 15–16 px row pitch (four
  rows per screen). Every menu, dialog and list.
- **Body** — Adafruit 6x8, 7 lines x 31 characters, for dense text and hint
  bands.

List grammar (`UiKit`): a titled list's question is its first scrolling row;
the selection is a full-width inverted bar; overflow gets a 2 px right-edge
scrollbar; rows that open a child get a chevron; exclusive/toggle rows use
radio/checkbox glyphs. Overflowing selected labels marquee (1 s pause,
25 px/s, 1.2 s hold, repeat). Root-level labels must fit 16 characters
(`tools/checks/check_calculator_menu_labels.py`).

Calculator lists use `UiKit::ListStyle{15,187,false}`: a 15-row bar ending at
x187 with the 16-pixel pitch and four visible rows; the scrollbar thumb is
painted at gray level 2 in x190..191, and x188..189 stay blank. VARIABLE cells
are 91x11 at column origins 0/97 with a 13-pixel row pitch.

Error dialogs (`renderErrorDialog()`): 12 px chrome title at (2,1), a 22x21
warning sign at (85,21), and an inverted Back band x0..187, y48..62 with white
"Back" at (2,49). No caret or scrollbar. OK/AC/BACK restore the original
source and cursor.

## Status icons

`CalculatorApp::desiredIcons()` returns a complete `PanelIcons::State` for
the frame just returned: set the requested glyphs and clear every other one.
Calculation views report MATHIO, the angle unit, FIX/SCI/E, Verify (CHECK),
history arrows and, in Complex, `i` or the angle mark; field views report their parent's settings plus SHIFT;
menus report none. The mapping lives in `calculator/ui/calculator_icons.*`.

On the Casio LCD each glyph is exactly one column of the icon strip (page 15,
bit pair 0):

| Glyph | Column | Glyph | Column | Glyph | Column |
| --- | --- | --- | --- | --- | --- |
| S (shift) | 85 | SCI | 155 | arrow LEFT | 210 |
| mathio | 100 | E | 165 | arrow DOWN | 215 |
| D (deg) | 110 | i (complex) | 175 | arrow UP | 225 |
| R (rad) | 120 | angle | 180 | arrow RIGHT | 230 |
| G (gra) | 130 | hollow down arrow | 190 | pause | 245 |
| FIX | 145 | tick in circle | 200 | sun | 255 |

## Casio LCD controller reference

Measured on the original hardware.

### Coordinates and pixel format

```
x: 0..191 left to right    y: 0..62 top to bottom (below the icon strip)
level: 0..3, 0 = lightest
```

DDRAM is 192 columns (real columns 64–255) x 16 pages; one byte holds four
vertically stacked pixels, 2 bits each. Screen row `R = (15 - page) * 4 + sub`
— mirrored at page level and within the byte; horizontal is not mirrored.
`R = 0` (page 15, sub 0) is the icon strip, which is why the drawable area is
63 rows: frame row `y` is `R = y + 1`, so
`page = 15 - ((y + 1) >> 2)`, `shift = 2 * ((y + 1) & 3)`, and the byte is
`fb[page * 192 + x]`.

### Bus

- 8.00 MHz, SPI mode 0 (clock idles low, sample on rising edge); A0 set before
  CS falls. A0 low = command, A0 high = parameter or pixel data.
- One CS pulse per command/parameter byte; the 3072-byte pixel payload may go
  as one CS-low burst.

### Frame update

```
30          Extension Command 1
75 00 0F    Page address 0..15   (page BEFORE column)
15 40 FF    Column address 64..255
5C          Write to DDRAM
<3072 bytes>
EE          End — mandatory, or the next opcode is eaten as data
```

### Init

Byte-identical to the calculator MCU's own capture (C = command, D = data,
then the delay after the byte):

```
C 30            Extension Command 1
C 6E            unexplained, but the real host sends it
C 31            Extension Command 2
C D7  D 9F      Auto Read control: disable OTP auto-read
C E0  D 00      OTP read enable; wait 10 ms
C E3            OTP read wait; wait 21 ms
C E1            OTP control out
C 30            Extension Command 1
C 94            Sleep Out
C AE            Display OFF; wait 51 ms
C 20  D 0B      Power Control: VB/VR/VF on
C 81  D 33 01   Electronic Volume (VOP = 0x133, contrast)
C 31            Extension Command 2
C 20  D 00 00 00 0D 0D 0D 00 00 16 00 00 16 16 16 00 00   grey levels
C F0  D 08 08 0B 0D                                       frame rate per temperature band
C 32  D 00 01 05                                          Analog Circuit Set (bias 5)
C 51  D FA      unexplained; one command + one parameter
C 30            Extension Command 1
C F0  D 11      Display Mode: 4-level grey
C CA  D 00 3F 00  Display Control: duty 64 lines
C BC  D 02      Data Scan Direction
C A6            Normal display
C AF            Display ON
```

The capture also contains `31 40` and `30 77` before `AF`; skip them:
`0x77` takes a parameter and swallows the `0xAF` Display ON.

### Gotchas

- Parameters need A0 HIGH — the most common way to get a dead panel.
- `0xA5` (all points on) does nothing on this chip; it is no liveness test.
- RSTB must be tied high (3.3 V). Held low, the controller sits in reset: a
  blank panel with a driver that times perfectly.
- A loose CLK/SDA/CS wire looks like a firmware bug: SPI writes fail silently.
- A freshly initialised panel is blank (level 0 is the lightest) until drawn.
