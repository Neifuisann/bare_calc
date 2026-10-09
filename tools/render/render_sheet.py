"""Pack host-rendered panel frames into labelled PNG contact sheets.

The host render harnesses write each 192x63 frame as `<name>.pgm` (4-level
grey) and/or `<name>.pbm` (1-bit). PNG is what an image viewer -- or an AI
agent's file reader -- can actually open, so this tiles them 3x-upscaled with
the fixture name under each, `--per-sheet` frames per PNG.

    python tools/render/render_sheet.py <frame_dir> <out_dir> [--filter SUBSTR] [--per-sheet N]

Writes `<out_dir>/sheet-01.png`, `sheet-02.png`, ... and prints their paths.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw

SCALE, COLS, LABEL_H, PAD = 3, 3, 14, 6
W, H = 192 * SCALE, 63 * SCALE


def frames(directory: Path, needle: str):
    # Prefer the grey PGM; fall back to the PBM for fixtures that only have one.
    stems = sorted({p.stem for p in directory.glob("*.pgm")} | {p.stem for p in directory.glob("*.pbm")})
    for stem in stems:
        if needle and needle not in stem:
            continue
        path = directory / f"{stem}.pgm"
        if not path.exists():
            path = directory / f"{stem}.pbm"
        yield stem, Image.open(path).convert("L")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frame_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--filter", default="")
    ap.add_argument("--per-sheet", type=int, default=18)
    args = ap.parse_args()
    items = list(frames(args.frame_dir, args.filter))
    if not items:
        raise SystemExit(f"no frames in {args.frame_dir}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for old in args.out_dir.glob("sheet-*.png"):
        old.unlink()
    for sheet, start in enumerate(range(0, len(items), args.per_sheet), 1):
        chunk = items[start:start + args.per_sheet]
        rows = (len(chunk) + COLS - 1) // COLS
        canvas = Image.new("L", (COLS * (W + PAD) + PAD, rows * (H + LABEL_H + PAD) + PAD), 128)
        draw = ImageDraw.Draw(canvas)
        for i, (name, img) in enumerate(chunk):
            x = PAD + (i % COLS) * (W + PAD)
            y = PAD + (i // COLS) * (H + LABEL_H + PAD)
            canvas.paste(img.resize((W, H), Image.NEAREST), (x, y))
            draw.text((x, y + H + 1), name, fill=255)
        out = args.out_dir / f"sheet-{sheet:02d}.png"
        canvas.save(out)
        print(f"render sheet: {out}")


if __name__ == "__main__":
    main()
