"""Compare the real controller's PackBits frames with supplied Casio captures.

Run after tools/check.ps1 -Only spreadsheet. A frozen digest checks the blank
grid without distributing the supplied images. Local captures, when available,
are also tiled side by side for visual review, not accepted as exact goldens.
"""
from pathlib import Path
import argparse
from hashlib import sha256
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
REFS = ROOT / "test/calculator_references/spreadsheet"
# SHA-256 of the supplied 192x63 blank frame after four-level normalization.
EMPTY_DIGEST = "681c2b1fa01548501d3c9a6af925589350041568713411fa709428445a32b974"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path, nargs="?", default=ROOT / ".build/check/spreadsheet-frames")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/check/renders/spreadsheet/reference-review.png")
    parser.add_argument("--references", type=Path, default=REFS)
    args = parser.parse_args()
    actual = Image.open(args.frames / "spreadsheet-empty.pgm").convert("L")
    assert actual.size == (192, 63)
    assert sha256(actual.tobytes()).hexdigest() == EMPTY_DIGEST, "Spreadsheet blank grid drifted from supplied capture"
    print("Spreadsheet empty grid: frozen pixel digest matches / 12096 pixels")
    expected = None
    if (args.references / "empty.bmp").exists():
        reference = Image.open(args.references / "empty.bmp").convert("L")
        # Rank the BMP's white/192/128/black into panel white/170/85/black.
        palette = sorted(set(reference.tobytes()), reverse=True)
        mapping = {value: 255 - 85 * rank for rank, value in enumerate(palette)}
        expected = reference.point(lambda value: mapping.get(value, value))
        assert expected.size == actual.size
        mismatch = sum(a != b for a, b in zip(expected.tobytes(), actual.tobytes()))
        assert mismatch == 0, "Local reference differs from frozen blank grid"
        print(f"Spreadsheet empty grid: {mismatch} mismatched pixels / 12096")
    pairs = [("empty", None), ("catalog", 1), ("commands-top", 2),
             ("commands-mean", 3), ("commands-sum", 4), ("tools-top", 5),
             ("tools-middle", 6), ("tools-bottom", 7), ("auto-calc", 8),
             ("show-cell", 9), ("fill-value", 10), ("memory", 12),
             ("paste", 13), ("paste-down", 14), ("editing", 16)]
    pairs = [(name, number) for name, number in pairs
             if (expected is not None if number is None else
                 (args.references / f"reference-{number}.png").exists())]
    if not pairs:
        print("Spreadsheet reference review: local captures unavailable; pixel digest gate passed")
        return
    sheet = Image.new("RGB", (1168, 220 * len(pairs)), "#ddd")
    draw = ImageDraw.Draw(sheet)
    for row, (name, number) in enumerate(pairs):
        ref = expected if number is None else Image.open(args.references / f"reference-{number}.png").convert("L").crop((1, 11, 193, 74))
        got = Image.open(args.frames / f"spreadsheet-{name}.pgm").convert("L")
        draw.text((2, row * 220 + 5), f"Casio reference: {name}", fill="black")
        draw.text((592, row * 220 + 5), "Device renderer (sample values may differ)", fill="black")
        sheet.paste(ref.resize((576, 189), Image.Resampling.NEAREST), (0, row * 220 + 25))
        sheet.paste(got.resize((576, 189), Image.Resampling.NEAREST), (592, row * 220 + 25))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    print(f"Spreadsheet reference review: {args.output}")


if __name__ == "__main__":
    main()
