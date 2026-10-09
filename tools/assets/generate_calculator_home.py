"""Lossless 2-bpp asset import from user-supplied 192x63 calculator captures.

Requires PyMuPDF. No scaling, resampling, font substitution or thresholding.
The stored rows have the captured selection removed; firmware applies the
same rounded 60x31 inversion mask to whichever app is selected.
"""
import argparse
from pathlib import Path
import pymupdf
from blank_home_cell import HEADER_NOTE, apply as blank_unused_cell

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2]/'src/calculator/assets/calculator_home_assets.h')
args = parser.parse_args()
images = [pymupdf.Pixmap(str(args.source/f'calc-menu-{i}.png')) for i in (1,2,3)]
assert all((p.width,p.height)==(192,63) for p in images)
def mask(x,y):
    return x < 60 and not (y in (0,30) and x in (0,59))
rows=[]
for image, start, selected in [(images[0],0,True),(images[0],32,False),
                               (images[1],0,True),(images[1],32,False),(images[2],32,False)]:
    row=bytearray(48*31)
    for y in range(31):
        for x in range(192):
            shade=3-image.pixel(x,start+y)[0]//85 if x<190 else 0
            if selected and mask(x,y): shade=3-shade
            row[(y*192+x)//4] |= shade << (6-2*(x%4))
    rows.append(row)
blank_unused_cell(rows[4])
scroll=[]
for image in images:
    scroll.append([3-image.pixel(190,y)[0]//85 for y in range(63)])
lines=['#pragma once','#include <cstdint>','namespace Calculator {',
       *HEADER_NOTE,
       'inline constexpr uint8_t kHomeRows[5][1488] = {']
for row in rows:
    lines.append('{')
    lines.extend(','.join(str(v) for v in row[i:i+48])+',' for i in range(0,len(row),48))
    lines.append('},')
lines+=['};','inline constexpr uint8_t kHomeScroll[3][63] = {']
lines += ['{'+','.join(map(str,s))+'},' for s in scroll]
lines+=['};','}  // namespace Calculator','']
args.output.write_text('\n'.join(lines),encoding='ascii')
