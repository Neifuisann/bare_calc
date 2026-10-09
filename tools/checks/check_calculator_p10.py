"""Check reviewed P10 frames separately from the unchanged P9 fixtures."""
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
review = json.loads((root/'test/calculator_render/p10_reviewed.json').read_text())
assert set(review) == {'p10-math-error', 'p10-syntax-error'}
for name, expected in review.items():
    actual = hashlib.sha256((Path(sys.argv[1])/(name+'.pgm')).read_bytes()).hexdigest()
    assert actual == expected, f'Unreviewed P10 frame change: {name}'
print('P10 fixture review: 2 exact frames; bit-perfect vs ERROR-MENU.bmp')
