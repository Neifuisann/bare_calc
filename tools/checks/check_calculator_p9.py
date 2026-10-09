"""Check individually reviewed P9 frames; no update/accept mode."""
import hashlib
import json
import sys
from pathlib import Path

root=Path(__file__).resolve().parents[2]
current=Path(sys.argv[1])
review=json.loads((root/'test/calculator_render/p9_reviewed.json').read_text())
assert len(review)==45
changed=0
for name,item in review.items():
    actual=hashlib.sha256((current/(name+'.pgm')).read_bytes()).hexdigest()
    assert actual==item['p9_sha256'], f'Unreviewed P9 frame change: {name}'
    if item['classification']==['UNCHANGED']:
        assert actual==item['p8_sha256'], f'P8 frame regression: {name}'
    else:
        changed+=1
assert changed==12
print(f'P9 fixture review: {len(review)} exact frames; {changed} reviewed changes, {len(review)-changed} byte-identical P8 frames; zero suspicious')
