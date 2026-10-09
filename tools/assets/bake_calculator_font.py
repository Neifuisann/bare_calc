"""Bake the calculator's mathematical glyphs into src/calculator/assets/font_calculator_math.h.

Source: ClassWizMathCW-Regular.otf 1.003 (SIL OFL 1.1, notice in
assets/fonts/OFL.txt), a community reconstruction of the ClassWiz
calculator's glyphs -- not an official Casio font. The bake pins its SHA-256
and refuses any other file.

The OTF is a pixel-grid outline font: every point sits on a 10-unit grid and
14 px/em is exactly 10 units per pixel. So the correct rasteriser is not a
font renderer. Pixel column c (relative to the pen) and row r (relative to
the baseline, r = -1 the row just above it, rows growing downward) is ink
when the font-space point (10c + 5, -(10r + 5)) lies inside the glyph's
contours under the non-zero winding rule. That reproduces the reference
raster exactly: 12-row normal digits and 9-row `ssty` script digits. The old
bake went through PIL/FreeType, whose hinter stretched the same outlines to
14 and 11 rows (research spec L-01); nothing here uses FreeType or Pillow.

Crossings are counted on non-horizontal edges only (horizontal edges add no
winding), each spanning the half-open range min(y0,y1) <= y < max(y0,y1).
A sample exactly on a non-horizontal edge line skips that edge (an
approximation: under strict non-zero winding a diagonal-edge sample is not
truly "outside", but skipping keeps the rule deterministic with no float
noise). So a sample on a vertex row counts once, and one on a horizontal
edge row takes the fill of the rows it bounds from below (the half-pixel
paren variants keep solid 2 px strokes). The few glyphs whose samples touch
an edge or vertex row (half-pixel parenthesis variants) are listed in the
report as boundary-sample glyphs; only they are affected by the approximation.
Curves are flattened into FLATTEN_STEPS chords each; this font has none, and
any that appear are listed in the report.

Normal glyphs come from the base cmap glyph, script glyphs from the first
`ssty` alternate (never a scaled normal glyph); symbols without one are
listed and the layout draws them normal size. Vertical size variants come
from the MATH table. Advances are the OTF hmtx advance / 10 -- never the
bitmap width -- so ink may lie outside [0, advance). Bitmaps are trimmed
tight, which makes a glyph's horizontal ink extents exactly
[xOffset, xOffset + width) (MathFont::Glyph::inkLeft/inkRight).

assets/glyph_overrides.json holds hand-tuned glyph drawings shared with the
ClassWiz15 chrome font and is never modified here. Every entry the old bake
consumed (face classwiz-cw, 14 px normal / 9.8 px script, a baked symbol) is
classified in OVERRIDE_POLICY with a reason; the bake fails if that consumed
subset changes, so a redrawn or new drawing cannot be silently applied or
silently ignored.

Outputs are deterministic (sorted, no timestamps, repo-relative paths): the
header and tools/out/calculator_font_report.json (git-ignored audit).

The committed header was baked from an earlier revision of
assets/glyph_overrides.json; the current file fails the policy audit, so a
re-bake needs OVERRIDE_POLICY updated first.

Usage (from bare_calc/, with a Python that has fontTools):
    <python> tools/assets/bake_calculator_font.py [out.h]
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ASSETS = REPO / "assets"
FONT_PATH = ASSETS / "fonts" / "ClassWizMathCW-Regular.otf"
FONT_SHA256 = "478860316e36b1d84f93fd55895ee71e5cc88aa6545e985707179742c4c60334"
FONT_VERSION = "Version 1.003"
OVERRIDES_PATH = ASSETS / "glyph_overrides.json"
OFL_NOTICE = "assets/fonts/OFL.txt"
DEFAULT_OUTPUT = REPO / "src" / "calculator" / "assets" / "font_calculator_math.h"
REPORT_PATH = REPO / "tools" / "out" / "calculator_font_report.json"
POLICY = "grid-exact-v1"
FACE, NORMAL_SIZE, SCRIPT_SIZE, UNIT = "classwiz-cw", 14.0, 9.8, 10
FLATTEN_STEPS = 16  # chords per curve segment; far finer than the 10-unit pixel
GLYPH_BYTES_TARGET = 12  # sizeof(MathFont::Glyph) on a 32-bit target (11 bytes + padding)

NORMAL, SCRIPT = 0, 1
# Everything the calculator layout can ask for. ASCII covers digits, variable
# letters, function names, operators and punctuation; the rest are the
# natural-display symbols.
# The OTF has no grave accent (U+0060); nothing in Calculate produces one.
SYMBOLS = [cp for cp in range(0x20, 0x7F) if cp != 0x60] + [
    0x00B0,  # degree (sexagesimal results)
    0x00D7, 0x00F7,  # multiply, divide
    0x2192,  # arrow (unit conversions)
    0x2212,  # minus
    0x2260, 0x2264, 0x2265,  # not equal, less/greater or equal
    0x25A1,  # empty-field placeholder
    0x1D452, 0x1D465, 0x1D466, 0x1D467,  # mathematical italic e, x, y, z
    0x1D70B,  # mathematical italic pi
    0x222B, 0x2211, 0x220F, 0x221A,  # integral, summation, product, radical
    0x2220,  # angle (Complex polar form r∠θ)
    0x22C5,  # dot operator (Vector dot product)
    0x1D456,  # mathematical italic i (Complex imaginary unit)
]
# Vertical size variants live under the base codepoint's key.
VARIANT_BASES = {0x28: "parenleft", 0x29: "parenright", 0x222B: "integral", 0x221A: "radical"}

# P6 audit of every override the pre-P6 bake read: a 14 px entry for a baked
# symbol (normal ink) or a 9.8 px entry for a symbol with an `ssty` glyph
# (script ink). Groups of (status, reason, chars); the per-entry evidence is
# in tools/out/calculator_font_report.json. Nothing here edits the JSON:
# - LEGACY_SIZE_COMPENSATION_INACTIVE: not applied (the bake-side denylist).
#   After the grid-vs-override comparison (bake load_font/raster/trim, rows
#   compared exactly) every consumed entry differs from the grid raster, so
#   P6 applies none: the baked pixels are identical to the outline grid (the
#   P6 objective, spec 6.1). The old 32 UNKNOWN entries are reclassified below
#   into their legacy families with true reasons -- no UNKNOWN remains.
# - NO_EFFECT: identical to the grid raster, or never applied before P6.
# - ACTIVE_SHAPE_FIX: a deliberate correction kept on purpose (none today,
#   so the header reports 0 applied).
APPLIED_STATUSES = ("ACTIVE_SHAPE_FIX",)
_STRETCH = ("FreeType-era 14 px drawing: taller/wider than the outline (rows duplicated to 13-14, "
            "x-height 11 vs 9); the grid outline is the reference geometry (digits 8x12)")
_PLACE = "same pixels as the outline, moved by FreeType's bearing/baseline rounding"
# Same size class and shifted box as _PLACE, but different pixels (FreeType
# hinting variant, rows differ exactly): , is 3x5@(3,0) vs grid 3x5@(2,-4),
# :/g are dy+1 same-size variants, ; is 4x11 vs 3x11, x-multiply is same-box
# different-pixels. Grid outline is the reference.
_PLACE_HINTING = ("FreeType-era 14 px drawing in the same size class as the outline, shifted box by "
                  "bearing/baseline rounding but different pixels (hinting variant, rows differ); "
                  "the grid outline is the reference geometry (digits 8x12)")
_MATHTEXT = ("drawn for the 9.8 px mathtext script (0.7 em: 7-row caps/digits, other widths, "
             "+1 px placement), not the font's ssty glyph; the ssty outline is the reference (digits 5x9)")
_MATHTEXT_PLACE = "same pixels as the ssty outline, moved +1 px right / 1 row down by the 9.8 px mathtext placement"
# Same box size and +1/+1 placement as _MATHTEXT_PLACE, but different pixels
# (rows differ exactly): a/b/d/g/k/n/r/s/u/z are 5x7 or 5x9 same-size dy+1/dx+1
# hinting variants. The ssty outline is the reference (digits 5x9).
_MATHTEXT_HINTING = ("9.8 px mathtext-era drawing in the same box size as the ssty outline, placed "
                     "+1 px right / 1 row down but different pixels (hinting variant, rows differ); "
                     "the ssty outline is the reference (digits 5x9)")
OVERRIDE_POLICY = {
    NORMAL_SIZE: [
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _STRETCH, "%0157ARast|/∏"),
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _PLACE, "+>"),
        ("NO_EFFECT", "identical to the grid raster", "!<=−"),
        # x is 10x9 vs grid 8x9 (wider); arrow/sum/integral/pi are taller/wider
        # (14x10 vs 10x8, 14x14 vs 10x13, 9x17 vs 9x13, 12x12 vs 10x9).
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _STRETCH, "x\u2192\u2211\u222b\U0001d70b"),
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _PLACE_HINTING, ",:;g\u00d7"),
    ],
    SCRIPT_SIZE: [
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _MATHTEXT,
         "0123456789ABCDFGHIJKLMNOPQRSTUWYZfhimqtw;=[]+×→\U0001d70b"),
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _MATHTEXT_PLACE, "./ceov"),
        ("NO_EFFECT", "never applied: excluded before P6 (wider than the 6 px advance, or unpaired with its partner)", "XV)"),
        # Same-box +1/+1 hinting variants (rows differ, boxes match the
        # _MATHTEXT_PLACE signature): 5x7/5x9 at (1,-6)/(1,-8) vs (0,-7)/(0,-9).
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _MATHTEXT_HINTING, "abdgknrsuz"),
        # Size-differing mathtext drawings (rows and boxes differ): E is 5x7 vs
        # 5x9 (7-row cap), minus is 3x1@(4,-4) vs 5x1@(0,-5) (narrow, past the
        # 6 px advance), p/x/y add a row, j/l/</>/,/÷ change size and pixels.
        ("LEGACY_SIZE_COMPENSATION_INACTIVE", _MATHTEXT, "Ejlpxy<>?,\u00f7\u2212"),
    ],
}
# sha256 of the audited subset (sorted JSON of those entries): a redrawn or
# added drawing fails the bake until it is classified.
AUDITED_OVERRIDES_SHA256 = "26c9f6b1a47c9a87ab9f85478597b22af37f41e1d54c78d027a912305b1bfee1"


def require(ok, message):
    """A check that also runs under python -O."""
    if not ok:
        raise SystemExit(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_font():
    """(font, glyph set, ssty alternates, vertical variants)."""
    from fontTools.ttLib import TTFont

    data = FONT_PATH.read_bytes()
    if sha256(data) != FONT_SHA256:
        raise SystemExit(f"{FONT_PATH.name}: sha256 {sha256(data)} is not the pinned {FONT_SHA256}")
    font = TTFont(str(FONT_PATH))
    require(font["head"].unitsPerEm == 140, "10 units per pixel assumes 140 units/em")
    require(font["name"].getDebugName(5) == FONT_VERSION, f"{FONT_PATH.name} is not {FONT_VERSION}")
    alternates = {}
    gsub = font["GSUB"].table
    for record in gsub.FeatureList.FeatureRecord:
        if record.FeatureTag != "ssty":
            continue
        for index in record.Feature.LookupListIndex:
            for sub in gsub.LookupList.Lookup[index].SubTable:
                alternates.update(getattr(sub, "alternates", {}) or {})
    variants = {}
    math_variants = font["MATH"].table.MathVariants
    for name, construction in zip(math_variants.VertGlyphCoverage.glyphs, math_variants.VertGlyphConstruction):
        variants[name] = [r.VariantGlyph for r in construction.MathGlyphVariantRecord]
    return font, font.getGlyphSet(), alternates, variants


def contours(glyph_set, name):
    """(closed polygons in font units, whether any curve was flattened)."""
    from fontTools.pens.basePen import BasePen

    class PolygonPen(BasePen):
        def __init__(self):
            super().__init__(glyph_set)  # glyph_set lets BasePen decompose components
            self.polygons, self.curved = [], False

        def _moveTo(self, pt):
            self.polygons.append([pt])

        def _lineTo(self, pt):
            self.polygons[-1].append(pt)

        def _curveToOne(self, p1, p2, p3):  # BasePen routes quadratics here too
            self.curved = True
            p0 = self._getCurrentPoint()
            for i in range(1, FLATTEN_STEPS + 1):
                t = i / FLATTEN_STEPS
                u = 1 - t
                self.polygons[-1].append(tuple(u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d
                                               for a, b, c, d in zip(p0, p1, p2, p3)))

    pen = PolygonPen()
    glyph_set[name].draw(pen)
    return [p for p in pen.polygons if len(p) > 2], pen.curved


def raster(glyph_set, name):
    """(rows, x_offset, y_offset, curved, boundary): tight ink box, dx from the
    pen, dy of the top row from the baseline (negative is above)."""
    polygons, curved = contours(glyph_set, name)
    edges = [(p[i], p[(i + 1) % len(p)]) for p in polygons for i in range(len(p))]
    if not edges:
        return [], 0, 0, curved, False
    boundary = False

    def inside(x, y):
        nonlocal boundary
        winding = 0
        for (x0, y0), (x1, y1) in edges:
            if y0 == y1:
                continue
            if y == y0 or y == y1:
                boundary = True
            if min(y0, y1) <= y < max(y0, y1):
                cross = (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0)
                if cross == 0:
                    boundary = True
                elif (cross > 0) == (y1 > y0):
                    winding += 1 if y1 > y0 else -1
        return winding != 0

    xs = [x for (x, _), _ in edges]
    ys = [y for (_, y), _ in edges]
    columns = range(int(min(xs) // UNIT) - 1, int(max(xs) // UNIT) + 1)
    rows = range(int(-max(ys) // UNIT) - 1, int(-min(ys) // UNIT) + 1)
    ink = [[inside(UNIT * c + UNIT // 2, -(UNIT * r + UNIT // 2)) for c in columns] for r in rows]
    trimmed, x_off, y_off = trim(ink, columns.start, rows.start)
    return trimmed, x_off, y_off, curved, boundary


def trim(rows, x_offset, y_offset):
    while rows and not any(rows[0]):
        rows, y_offset = rows[1:], y_offset + 1
    while rows and not any(rows[-1]):
        rows = rows[:-1]
    if not rows:
        return [], 0, 0
    while not any(r[0] for r in rows):
        rows, x_offset = [r[1:] for r in rows], x_offset + 1
    while not any(r[-1] for r in rows):
        rows = [r[:-1] for r in rows]
    return rows, x_offset, y_offset


def pack(rows):
    """Row-major, MSB-first, contiguous across rows -- the GFXfont rule."""
    out, cur, bits = [], 0, 0
    for row in rows:
        for bit in row:
            cur, bits = (cur << 1) | bit, bits + 1
            if bits == 8:
                out.append(cur)
                cur, bits = 0, 0
    if bits:
        out.append(cur << (8 - bits))
    return out


def key(codepoint, style, variant=0):
    return codepoint | style << 21 | variant << 24


def audit_overrides(script_chars):
    """Classify every JSON entry against OVERRIDE_POLICY.

    Returns (report rows, {(size, char): (status, reason, entry)} for the
    audited subset, audited-subset sha256, whole-file sha256)."""
    raw = OVERRIDES_PATH.read_bytes()
    glyphs = json.loads(raw.decode("utf-8"))["glyphs"]
    readable = {NORMAL_SIZE: {chr(cp) for cp in SYMBOLS}, SCRIPT_SIZE: script_chars}
    policy = {}
    for size, groups in OVERRIDE_POLICY.items():
        for status, reason, chars in groups:
            for ch in chars:
                require((size, ch) not in policy, f"override {ch!r} @ {size} classified twice")
                policy[(size, ch)] = (status, reason)
    rows, audited = [], {}
    for index, g in enumerate(glyphs):
        size = round(float(g["size"]), 1)  # overrides bucket sizes to one decimal
        row = {"index": index, "char": g["char"], "size": g["size"]}
        if g["face"] == FACE and g["char"] in readable.get(size, ()):
            if (size, g["char"]) not in policy:
                raise SystemExit(f"glyph_overrides.json entry {index} ({g['char']!r} @ {g['size']}) is not audited in OVERRIDE_POLICY")
            require((size, g["char"]) not in audited, f"duplicate override {g['char']!r} @ {size}")
            row["status"], row["reason"] = policy[(size, g["char"])]
            audited[(size, g["char"])] = (row["status"], row["reason"], g)
        else:
            row["status"] = "NOT_CONSUMED"
            row["reason"] = "the calculator bake never reads this size/character (chrome font only, or no ssty glyph)"
        row["applied"] = row["status"] in APPLIED_STATUSES
        rows.append(row)
    stale = sorted(set(policy) - set(audited))
    require(not stale, f"OVERRIDE_POLICY names overrides that no longer exist: {stale}")
    digest = sha256(json.dumps([audited[k][2] for k in sorted(audited)], sort_keys=True, ensure_ascii=True).encode())
    if digest != AUDITED_OVERRIDES_SHA256:
        raise SystemExit(f"audited glyph overrides changed (sha256 {digest}); re-audit OVERRIDE_POLICY")
    return rows, audited, digest, sha256(raw)


def bake():
    font, glyph_set, alternates, variants = load_font()
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    script_chars = {chr(cp) for cp in SYMBOLS if alternates.get(cmap[cp])}
    override_rows, audited, consumed_sha, overrides_file_sha = audit_overrides(script_chars)
    entries = {}  # key -> dict

    def add(k, name, source, audit=None):
        advance_units = hmtx[name][0]
        require(advance_units % UNIT == 0, f"{name}: advance {advance_units} is off the pixel grid")
        rows, x_off, y_off, curved, boundary = raster(glyph_set, name)
        grid = (rows, x_off, y_off)
        override = audit[2] if audit else None
        drawn = trim([[c == "#" for c in r] for r in override["rows"]], override["offset_x"], override["offset_y"]) if override else grid
        if audit and audit[0] in APPLIED_STATUSES:
            rows, x_off, y_off = drawn
        entries[k] = {"rows": rows, "x": x_off, "y": y_off, "advance": advance_units // UNIT, "name": name,
                      "source": source, "curved": curved, "boundary": boundary,
                      "override": audit[0] if audit else None, "override_differs_from_grid": drawn != grid}

    missing_script = []
    for cp in SYMBOLS:
        name = cmap.get(cp)
        if name is None:
            raise KeyError(f"U+{cp:04X} is not in the font")
        add(key(cp, NORMAL), name, "cmap", audited.get((NORMAL_SIZE, chr(cp))))
        script = alternates.get(name)
        if script:
            add(key(cp, SCRIPT), script[0], "ssty[0]", audited.get((SCRIPT_SIZE, chr(cp))))
        elif cp != 0x20:
            missing_script.append(cp)
    for cp, base in VARIANT_BASES.items():
        for index, name in enumerate(variants[base][1:], start=1):
            add(key(cp, NORMAL, index), name, f"MATH vertical variant {index}")

    ssty2_differs = sorted(cmap_cp for cmap_cp, name in cmap.items() if cmap_cp in SYMBOLS
                           and len(alternates.get(name, [])) > 1
                           and (raster(glyph_set, alternates[name][0])[:3], hmtx[alternates[name][0]][0])
                           != (raster(glyph_set, alternates[name][1])[:3], hmtx[alternates[name][1]][0]))

    constants = font["MATH"].table.MathConstants

    def px(field):
        value = getattr(constants, field)
        return getattr(value, "Value", value) // UNIT

    metrics = {
        "kAxisHeight": px("AxisHeight"),
        "kFractionRuleThickness": px("FractionRuleThickness"),
        "kFractionNumeratorGapMin": px("FractionNumeratorGapMin"),
        "kFractionDenominatorGapMin": px("FractionDenominatorGapMin"),
        "kRadicalRuleThickness": px("RadicalRuleThickness"),
        "kRadicalVerticalGap": px("RadicalVerticalGap"),
        "kRadicalExtraAscender": px("RadicalExtraAscender"),
        "kSuperscriptBottomMin": px("SuperscriptBottomMin"),
        "kSuperscriptBaselineDropMax": px("SuperscriptBaselineDropMax"),
        "kSubscriptShiftDown": px("SubscriptShiftDown"),
        "kSubscriptTopMax": px("SubscriptTopMax"),
        "kUpperLimitGapMin": px("UpperLimitGapMin"),
        "kLowerLimitGapMin": px("LowerLimitGapMin"),
    }

    def extent(cps, style):
        glyphs = [entries[key(cp, style)] for cp in cps if key(cp, style) in entries]
        return max(-g["y"] for g in glyphs if g["rows"]), max(g["y"] + len(g["rows"]) for g in glyphs if g["rows"])

    digits_upper = list(range(0x30, 0x3A)) + list(range(0x41, 0x5B))
    lower = list(range(0x61, 0x7B))
    for style, prefix in ((NORMAL, "kNormal"), (SCRIPT, "kScript")):
        metrics[prefix + "Ascent"] = extent(digits_upper, style)[0]
        metrics[prefix + "Descent"] = extent(lower, style)[1]
        metrics[prefix + "DigitAdvance"] = entries[key(0x30, style)]["advance"]

    return {
        "entries": entries, "metrics": metrics, "missing_script": missing_script, "ssty2_differs": ssty2_differs,
        "override_rows": override_rows, "consumed_sha": consumed_sha,
        "active": sorted((size, ch, status) for (size, ch), (status, _, _) in audited.items() if status in APPLIED_STATUSES),
        "overrides_file_sha": overrides_file_sha, "outline_format": "CFF" if "CFF " in font else "glyf",
    }


def emit(b):
    entries = b["entries"]
    bitmap, glyph_lines = [], []
    for k in sorted(entries):
        e = entries[k]
        rows = e["rows"]
        width = len(rows[0]) if rows else 0
        offset = len(bitmap)
        bitmap.extend(pack(rows))
        style = "script" if k >> 21 & 7 else "normal"
        label = f"U+{k & 0x1FFFFF:04X} {style if not k >> 24 else 'variant ' + str(k >> 24)} ({e['name']})"
        glyph_lines.append(f"    {{0x{k:07X}, {offset}, {width}, {len(rows)}, {e['advance']}, {e['x']}, {e['y']}}},  // {label}")
    active = b["active"]
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/bake_calculator_font.py -- do not hand-edit.",
        f"// Policy {POLICY}: grid-exact outline sampling, non-zero winding, no FreeType/Pillow.",
        f"// Source: assets/fonts/ClassWizMathCW-Regular.otf, {FONT_VERSION}, sha256 {FONT_SHA256}",
        "// Derived from ClassWiz Math CW by Windy Deng, a community reconstruction (not an",
        "// official Casio font). Copyright (c) 2020-2025, Windy Deng; Copyright (c) 2014-2025,",
        f"// CASIO Computer Co., Ltd. SIL Open Font License 1.1: see {OFL_NOTICE}",
        f"// Overrides applied ({len(active)}, status and reason in the bake's OVERRIDE_POLICY; audited subset of",
        f"// glyph_overrides.json sha256 {b['consumed_sha']}):",
        "//   normal " + "".join(ascii(ch)[1:-1] for size, ch, _ in active if size == NORMAL_SIZE)
        + " | script " + "".join(ascii(ch)[1:-1] for size, ch, _ in active if size == SCRIPT_SIZE),
        f"// {len(entries)} glyphs, {len(bitmap)} bitmap bytes. ssty2 alternates differing from ssty: {len(b['ssty2_differs'])}",
        "// Symbols with no native script glyph (the layout draws them normal-size): "
        + " ".join(f"U+{cp:04X}" for cp in b["missing_script"]),
        "// Included only by src/calculator/calculator_font.cpp.",
        "namespace CalculatorFontData {",
        "constexpr uint8_t kBitmaps[] = {",
    ]
    for i in range(0, len(bitmap), 16):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in bitmap[i:i + 16]) + ",")
    lines += ["};", "// key = codepoint | style << 21 | variant << 24; sorted for binary search.",
              "constexpr Calculator::MathFont::Glyph kGlyphs[] = {"]
    lines += glyph_lines
    lines += ["};", "// MATH table constants and extents measured from the baked glyphs (units / 10).",
              "// Starting values only: calculator_metrics.h owns the calibrated line geometry."]
    lines += [f"constexpr int {name} = {value};" for name, value in b["metrics"].items()]
    lines += ["}  // namespace CalculatorFontData", ""]
    return "\n".join(lines), len(bitmap)


def report(b, bitmap_bytes):
    glyphs = []
    for k in sorted(b["entries"]):
        e = b["entries"][k]
        width, height = (len(e["rows"][0]), len(e["rows"])) if e["rows"] else (0, 0)
        ink_left, ink_right = (e["x"], e["x"] + width) if width else (0, 0)
        glyphs.append({
            "key": f"0x{k:07X}", "codepoint": f"U+{k & 0x1FFFFF:04X}",
            "style": "script" if k >> 21 & 7 else "normal", "variant": k >> 24, "glyph": e["name"],
            "gsub": e["source"], "advance": e["advance"], "width": width, "height": height,
            "xOffset": e["x"], "yOffset": e["y"], "inkLeft": ink_left, "inkRight": ink_right,
            "inkTop": e["y"] if height else 0, "inkBottom": e["y"] + height if height else 0,
            "inkOutsideAdvance": bool(width) and (ink_left < 0 or ink_right > e["advance"]),
            "override": e["override"], "overrideApplied": e["override"] in APPLIED_STATUSES,
            "overrideDiffersFromGrid": e["override_differs_from_grid"],
            "curved": e["curved"], "boundarySamples": e["boundary"],
        })
    codepoints = []
    for cp in SYMBOLS:
        n, s = b["entries"][key(cp, NORMAL)], b["entries"].get(key(cp, SCRIPT))
        codepoints.append({"codepoint": f"U+{cp:04X}", "glyph": n["name"], "normal": True, "script": s is not None,
                           "normalAdvance": n["advance"], "scriptAdvance": s["advance"] if s else None,
                           "scriptGlyph": s["name"] if s else None})
    statuses = {}
    for row in b["override_rows"]:
        statuses[row["status"]] = statuses.get(row["status"], 0) + 1
    return {
        "generator": "tools/assets/bake_calculator_font.py", "policy": POLICY,
        "source": {"path": "assets/fonts/ClassWizMathCW-Regular.otf", "sha256": FONT_SHA256,
                   "version": FONT_VERSION, "unitsPerEm": 140, "outlineFormat": b["outline_format"]},
        "licenseNotice": OFL_NOTICE,
        "raster": {"unitsPerPixel": UNIT, "sample": "(10c+5, -(10r+5))", "fillRule": "nonzero",
                   "edgeRule": "non-horizontal edges only, half-open [min y, max y); samples on a non-horizontal edge are outside", "flattenSteps": FLATTEN_STEPS},
        "glyphCount": len(glyphs), "bitmapBytes": bitmap_bytes, "glyphTableBytesTarget": len(glyphs) * GLYPH_BYTES_TARGET,
        "missingScript": [f"U+{cp:04X}" for cp in b["missing_script"]],
        "ssty2Differs": [f"U+{cp:04X}" for cp in b["ssty2_differs"]],
        "curveGlyphs": sorted({g["glyph"] for g in glyphs if g["curved"]}),
        "boundarySampleGlyphs": sorted({g["glyph"] for g in glyphs if g["boundarySamples"]}),
        "inkOutsideAdvance": [f"{g['codepoint']} {g['style']} v{g['variant']} ({g['glyph']})" for g in glyphs if g["inkOutsideAdvance"]],
        "overrides": {"path": "assets/glyph_overrides.json", "fileSha256": b["overrides_file_sha"],
                      "consumedSha256": b["consumed_sha"], "statusCounts": dict(sorted(statuses.items())),
                      "entries": b["override_rows"]},
        "codepoints": codepoints, "glyphs": glyphs,
    }


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT
    b = bake()
    header, bitmap_bytes = emit(b)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(header, encoding="utf-8", newline="\n")
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report(b, bitmap_bytes), indent=1, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")
    print(f"Wrote {output} and {REPORT_PATH.relative_to(REPO).as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
