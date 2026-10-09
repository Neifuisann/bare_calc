#!/usr/bin/env python3
"""Bake the Casio 880BTG Math ERROR menu BMP into src/calculator/assets/calculator_error_assets.h.

Source: a capture of the 880BTG Math ERROR menu, ERROR-MENU.bmp
(192x63, uncompressed 4-bit; not included). Palette luminance orders white > gray > black,
mapped to GrayCanvas levels 0/2/3 (level 1 unused, matching the P10 dialog).

Exact reference blits (bit-perfect vs hardware):
  kErrMath      107x12 at (2,1)    black title, level 3
  kErrBack      44x12  at (2,49)   white "Back", level 0 over the band
  kErrWarnBlack 22x21  at (85,21)  outline + "!", level 3
  kErrWarnGray  22x21  at (85,21)  triangle fill, level 2
  Band          x0..187 y48..62    fillRect level 3 (x188..191 stay paper)

Other error titles ("Syntax ERROR", "Argument ERROR", ...) share the same
12px chrome style (ClassWiz15 shifted left 1px, two duplicate stem rows
dropped per glyph, 'a' bottom pixel corrected). "Math ERROR" generated this
way is 0 diff vs the reference, so "Syntax ERROR" (same code path) follows.
Generated titles are baked here so the firmware only blits.

Usage:
    <python> tools/assets/bake_error_menu.py <path/to/ERROR-MENU.bmp>
Writes src/calculator/assets/calculator_error_assets.h (do not hand-edit).
"""

import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BMP_PATH = Path(next((a for a in sys.argv[1:] if not a.startswith("--")), "ERROR-MENU.bmp"))
FONT_PATH = REPO / "src" / "ui" / "font_classwiz15.h"
OUT_PATH = REPO / "src" / "calculator" / "assets" / "calculator_error_assets.h"

PANEL_W, PANEL_H = 192, 63

# Manual exact row drops (0-indexed original rows) that make generated
# "Math ERROR" 0 diff vs reference (brute-forced). 'a' needs its bottom
# pixel set (our bake has 6px, hardware 7px).
MANUAL_DROPS = {
    "M": (3, 9), "t": (4, 5), "h": (6, 7), "E": (2, 8), "R": (2, 9),
    "O": (3, 4), "B": (2, 8), "c": (3, 4), "k": (6, 9), "a": (2, 6),
}


def load_bmp_levels(path: Path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp, comp = struct.unpack_from("<H", data, 28)[0], struct.unpack_from("<I", data, 30)[0]
    if bpp != 4 or comp != 0:
        raise ValueError(f"{path}: need uncompressed 4-bit BMP")
    width, height = width, abs(height)
    if (width, height) != (PANEL_W, PANEL_H):
        raise ValueError(f"{path}: need {PANEL_W}x{PANEL_H}, got {width}x{height}")
    palette = data[14 + header_size:pixel_offset]
    lum = [(palette[i + 2] * 299 + palette[i + 1] * 587 + palette[i] * 114) // 1000
           for i in range(0, len(palette), 4)]
    stride = ((width * 4 + 31) // 32) * 4
    bottom_up = struct.unpack_from("<i", data, 18)[0] > 0
    indices = []
    for row in range(height):
        src = (height - 1 - row) if bottom_up else row
        base = pixel_offset + src * stride
        for x in range(width):
            byte = data[base + (x >> 1)]
            indices.append((byte >> 4) & 0xF if x % 2 == 0 else byte & 0xF)
    used = sorted(set(indices), key=lambda i: -lum[i])
    # Expect white + gray + black (3 colors). Map to levels 0/2/3.
    if len(used) != 3:
        raise ValueError(f"{path}: need 3 colors (white/gray/black), got {len(used)}")
    level_of = {used[0]: 0, used[1]: 2, used[2]: 3}
    rows = [[level_of[i] for i in indices[y * width:(y + 1) * width]] for y in range(height)]
    return rows


def mask_from_levels(rows, x0, y0, w, h, level):
    out = bytearray(((w + 7) // 8) * h)
    stride = (w + 7) // 8
    for y in range(h):
        for x in range(w):
            if rows[y0 + y][x0 + x] == level:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def load_font():
    text = FONT_PATH.read_text(encoding="utf-8")
    m = re.search(r"ClassWiz15Bitmaps\[\].*?\{(.*?)\};", text, re.S)
    data = bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
    g = re.search(r"ClassWiz15Glyphs\[\].*?\{(.*?)\};", text, re.S)
    entries = re.findall(r"\{(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\}", g.group(1))
    return data, [(int(a), int(b), int(c), int(d), int(e), int(f)) for a, b, c, d, e, f in entries]


def glyph_rows(data, entries, ch):
    off, w, h, adv, xo, yo = entries[ord(ch) - 0x20]
    rows = []
    for y in range(h):
        row = ""
        for x in range(w):
            bit = y * w + x
            v = (data[off + (bit >> 3)] >> (7 - (bit & 7))) & 1
            row += "#" if v else "."
        rows.append(row)
    return rows, adv, yo


def trim_best(rows):
    shifted = [r[1:] + "." for r in rows]
    n = len(shifted)
    mid = n // 2
    cur_rows, cur_orig = list(shifted), list(range(n))
    for part in ("upper", "lower"):
        best, best_len, best_dist = None, 0, None
        i = 0
        while i < len(cur_rows):
            j = i
            while j + 1 < len(cur_rows):
                if cur_rows[j + 1] != cur_rows[i]:
                    break
                o_next, o_start = cur_orig[j + 1], cur_orig[i]
                if part == "upper":
                    if o_next >= mid or o_start >= mid:
                        break
                elif o_next < mid or o_start < mid:
                    break
                j += 1
            s = cur_orig[i]
            in_part = (s < mid and part == "upper") or (s >= mid and part == "lower")
            if in_part:
                dist = (mid - j) if part == "upper" else (i - mid)
                if j - i + 1 > best_len or (j - i + 1 == best_len and (best_dist is None or dist < best_dist)):
                    best_len, best, best_dist = j - i + 1, (i, j), dist
            i = j + 1
        if best and best_len >= 2:
            del cur_rows[best[0] + 1]
            del cur_orig[best[0] + 1]
    return cur_rows


def trim_char(data, entries, ch):
    rows, _, _ = glyph_rows(data, entries, ch)
    if ch in MANUAL_DROPS:
        drops = set(MANUAL_DROPS[ch])
        tr = [r for i, r in enumerate([r[1:] + "." for r in rows]) if i not in drops]
    else:
        tr = trim_best(rows)
    if ch == "a":
        tr = list(tr)
        last = list(tr[-1])
        last[7] = "#"
        tr[-1] = "".join(last)
    return tr


def render_title(data, entries, text):
    W, H = 192, 40
    canv = [["."] * W for _ in range(H)]
    cx = 0
    for ch in text:
        if ch == " ":
            cx += 11
            continue
        _, adv, yo = glyph_rows(data, entries, ch)
        tr = trim_char(data, entries, ch)
        top = 14 + yo - 1  # relative to y1
        for k, row in enumerate(tr):
            y = top + k
            for dx, c in enumerate(row):
                if c == "#" and 0 <= cx + dx < W and 0 <= y < H:
                    canv[y][cx + dx] = "#"
        cx += adv
    xs = [x for y in range(H) for x in range(cx) if canv[y][x] == "#"]
    ys = [y for y in range(H) for x in range(cx) if canv[y][x] == "#"]
    w, h = max(xs) + 1, max(ys) + 1
    return ["".join(canv[y][x] for x in range(w)) for y in range(h)]


def emit(name, rows):
    h, w = len(rows), len(rows[0])
    stride = (w + 7) // 8
    out = bytearray()
    for y in range(h):
        for s in range(stride):
            byte = 0
            for b in range(8):
                x = s * 8 + b
                if x < w and rows[y][x] == "#":
                    byte |= 0x80 >> b
            out.append(byte)
    lines = [f"// {name}: {w}x{h}, {len(out)} bytes"]
    lines.append(f"constexpr uint8_t {name}[] = {{")
    for i in range(0, len(out), 12):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in out[i:i + 12]) + ",")
    lines.append("};")
    return "\n".join(lines), w, h, len(out)


def main():
    # Add a new title without re-baking previously reviewed P10 artwork from
    # a newer chrome face. Full regeneration remains the legacy default.
    if "--append-memory" in sys.argv:
        data, entries = load_font()
        code, _, _, _ = emit("kErrMemoryERROR", render_title(data, entries, "Memory ERROR"))
        existing = OUT_PATH.read_text(encoding="utf-8")
        existing = re.sub(r"// kErrMemoryERROR:[\s\S]*?\n};\n?", "", existing)
        OUT_PATH.write_text(existing.replace("}  // namespace ErrorArt", code + "\n}  // namespace ErrorArt"), encoding="utf-8")
        print(f"{OUT_PATH}: appended Memory ERROR, reviewed titles preserved")
        return 0
    rows = load_bmp_levels(BMP_PATH)
    data, entries = load_font()
    parts = []
    # Exact reference blits
    math_rows = [["#" if rows[1 + y][2 + x] == 3 else "." for x in range(107)] for y in range(12)]
    back_rows = [["#" if rows[49 + y][2 + x] == 0 else "." for x in range(44)] for y in range(12)]
    warn_black = [["#" if rows[21 + y][85 + x] == 3 else "." for x in range(22)] for y in range(21)]
    warn_gray = [["#" if rows[21 + y][85 + x] == 2 else "." for x in range(22)] for y in range(21)]
    for name, r in (("kErrMath", math_rows), ("kErrBack", back_rows),
                    ("kErrWarnBlack", warn_black), ("kErrWarnGray", warn_gray)):
        code, w, h, n = emit(name, r)
        parts.append(code)
    # Generated titles share the validated 12px style (Math renders 0 diff).
    titles = ["Syntax ERROR", "Argument ERROR", "Stack ERROR", "Circular ERROR",
              "Not Defined", "No Operator", "Time Out", "Cancelled",
              "Dimension ERROR", "Variable ERROR", "Cannot Solve", "Range ERROR", "Memory ERROR"]
    for title in titles:
        name = "kErr" + "".join(c for c in title if c.isalnum())
        code, w, h, n = emit(name, render_title(data, entries, title))
        parts.append(code)
    header = """#pragma once
#include <cstdint>

// Error-menu artwork, baked from the Casio 880BTG capture ERROR-MENU.bmp
// (192x63 4-bit: white/gray/black -> levels 0/2/3) plus the validated 12px
// chrome style for the remaining titles (ClassWiz15 shifted left 1px, two
// duplicate stem rows dropped per glyph, 'a' bottom pixel corrected;
// generated "Math ERROR" is 0 diff vs the reference). Band is x0..187
// y48..62 at level 3 (x188..191 stay paper); titles at (2,1) level 3;
// "Back" 44x12 at (2,49) level 0; warning 22x21 at (85,21) black + gray.
// Generated by tools/bake_error_menu.py; do not edit by hand.
namespace Calculator {
namespace ErrorArt {
""" + "\n".join(parts) + """
}  // namespace ErrorArt
}  // namespace Calculator
"""
    OUT_PATH.write_text(header, encoding="utf-8")
    print(f"{OUT_PATH}: baked from {BMP_PATH.name} + font_classwiz15.h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
