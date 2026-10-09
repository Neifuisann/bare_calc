"""Blank the unused row-4 middle cell of calculator_home_assets.h.

The Home menu has 13 Casio apps; the last row holds Math Box alone, so its
middle and right cells are empty and never selectable. The Home captures
(generate_calculator_home.py) and this script leave the middle cell white,
like the right one. The firmware draws no shortcut badge on an empty cell.

Usage (from bare_calc/): python tools/assets/blank_home_cell.py
Rewrites the row-4 middle cell of kHomeRows in place; --png writes a preview.
"""
import argparse
import re
from pathlib import Path

HEADER = Path(__file__).resolve().parents[2] / 'src/calculator/assets/calculator_home_assets.h'
ROW, CELL_X, CELL_W, CELL_H = 4, 64, 60, 31
HEADER_NOTE = [
    '// Generated losslessly by tools/generate_calculator_home.py.',
    '// Row 4 holds Math Box alone; its middle cell is blanked by',
    '// tools/blank_home_cell.py. All other cells are lossless Casio captures.',
]


def apply(row: bytearray) -> None:
    """Clear the row-4 middle cell of a 192x31 2-bpp home row to white."""
    for y in range(CELL_H):
        for x in range(CELL_W):
            off = y * 192 + CELL_X + x
            shift = 6 - 2 * (off % 4)
            row[off // 4] &= ~(3 << shift)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--header', type=Path, default=HEADER)
    parser.add_argument('--png', type=Path)
    args = parser.parse_args()
    text = args.header.read_text(encoding='ascii')
    starts = [m.end() for m in re.finditer(r'\n\{\n', text)]
    begin = starts[ROW]
    end = text.index('\n},', begin)
    row = bytearray(int(v) for v in re.findall(r'\d+', text[begin:end]))
    assert len(row) == 1488
    apply(row)
    body = '\n'.join(','.join(str(v) for v in row[i:i + 48]) + ',' for i in range(0, 1488, 48))
    text = text[:begin] + body + text[end:]
    head = text.index('// Generated losslessly')
    text = text[:head] + '\n'.join(HEADER_NOTE) + '\n' + text[text.index('inline constexpr uint8_t kHomeRows'):]
    args.header.write_text(text, encoding='ascii')
    if args.png:
        from PIL import Image
        img = Image.new('L', (192, 31))
        for y in range(31):
            for x in range(192):
                img.putpixel((x, y), 255 - 85 * ((row[(y * 192 + x) // 4] >> (6 - 2 * (x % 4))) & 3))
        img.resize((192 * 6, 31 * 6), Image.NEAREST).save(args.png)


if __name__ == '__main__':
    main()
