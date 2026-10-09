"""Verify P8 fixture masks against the captured, accepted P7 renderer.

No baseline-update mode: p7_masks.json hashes the 45 pre-edit P7 PBM masks.
Only entry-long's explicitly reserved left arrow band is excluded.
"""
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
current = Path(sys.argv[1])
baseline = json.loads((root/'test/calculator_render/p7_masks.json').read_text())
# The twelve individually reviewed P9 menu frames now have exact 2-bpp
# hashes in p9_reviewed.json, checked by check_calculator_p9.py. Every other
# frozen P7 geometry mask remains enforced here without changing its hash.
p9 = json.loads((root/'test/calculator_render/p9_reviewed.json').read_text())
assert set(p9) == set(baseline)
assert all(name.startswith('menu-') and name != 'menu-notice'
           for name,item in p9.items() if item['classification'] != ['UNCHANGED'])
baseline = {name: value for name,value in baseline.items()
            if p9[name]['classification'] == ['UNCHANGED']}
counts = dict(level_only=0, arrows=0, promoted=0, suspicious=0)
review = ['# P8 fixture review', '', 'P7 mask hashes were captured before P8 edits (45 fixtures, 1,994 passing checks).',
          'Only `entry-long` excludes x0..9 for the new left raster arrow and clipping band.', '',
          '| Fixture | Classification | Level-2 pixels |', '|---|---|---:|']
for name, old in baseline.items():
    content = (current/(name+'.pgm')).read_bytes()
    assert content.startswith(b'P5\n192 63\n255\n'), name
    pixels = content[len(b'P5\n192 63\n255\n'):]
    assert len(pixels) == 12096 and set(pixels) <= {0,85,170,255}, name
    mask = bytes(v != 255 for v in pixels)
    gray = pixels.count(85) + pixels.count(170)
    key = 'sha256'
    if 'left_band_excluded_sha256' in old:
        assert name == 'entry-long'
        mask = bytes(v if i%192 >= 10 else 0 for i,v in enumerate(mask))
        key = 'left_band_excluded_sha256'
        classification = 'EXPECTED_ARROW_CHANGE'; counts['arrows'] += 1
    elif gray:
        classification = 'EXPECTED_2BPP_LEVEL_CHANGE'; counts['level_only'] += 1
    else:
        classification = 'UNCHANGED_PROMOTED_1BPP'; counts['promoted'] += 1
    if hashlib.sha256(mask).hexdigest() != old[key]:
        classification = 'SUSPICIOUS_GEOMETRY_CHANGE'; counts['suspicious'] += 1
    review.append(f'| {name} | {classification} | {gray} |')
review += ['', f'Totals: {len(baseline)} fixtures; {counts}.',
           'No P7 math-mask change outside the single arrow reservation. P9 menu frames checked separately against reviewed hashes.',
           'Semantic results are checked separately by the reference, apps, engine and stress suites.']
(current/'P8_REVIEW.md').write_text('\n'.join(review)+'\n')
print(f'P8 frozen P7 masks: {len(baseline)} fixtures; {counts}')
sys.exit(bool(counts['suspicious']))
