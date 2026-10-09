"""Capture actual Distribution controller frames with the native reference app.

Usage: python tools/render/review_distribution.py reference.exe output-directory
Build .build/check/bin/reference.exe with tools/check.ps1 -Only reference first.
These are review artifacts, not asserted Casio raster goldens.
"""
import copy
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageDraw


def main():
    exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[2] / 'test/reference_cases/apps-distribution.json'
    initial = json.loads(source.read_text(encoding='utf-8'))['cases'][0]['initial']
    cap = lambda name: {'capture': name}
    sequences = {
        'binomial': [cap('types'), 'OK', cap('method'), 'OK', cap('list'), 'BACK',
                     'HOME', 'OK', 'OK', 'DOWN', 'OK', cap('params')],
        'normal': ['DOWN'] * 3 + ['OK', cap('lower'), 'DOWN', 'DOWN', 'DOWN', 'DOWN', cap('execute')],
        'inverse': ['DOWN'] * 4 + ['OK', 'DOWN', cap('mu')] + list('123456') + [cap('typing')],
    }
    cases = [{'id': name, 'initial': copy.deepcopy(initial), 'keys': keys, 'checks': []}
             for name, keys in sequences.items()]
    inputs = out / 'review-cases.json'
    inputs.write_text(json.dumps({'cases': cases}, indent=2), encoding='utf-8')
    captures = []
    for case in cases:
        subprocess.run([str(exe), str(inputs), case['id'], str(out)], check=True)
        captures += [(case['id'] + '-' + step['capture']) for step in case['keys'] if isinstance(step, dict)]
    scale, width, height = 3, 600, 218
    sheet = Image.new('RGB', (width * 3, height * ((len(captures) + 2) // 3)), '#e8e8e8')
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(captures):
        x, y = (i % 3) * width, (i // 3) * height
        draw.text((x + 8, y + 5), name, fill='black')
        panel = Image.open(out / (name + '.pgm')).convert('RGB')
        sheet.paste(panel.resize((192 * scale, 63 * scale), Image.Resampling.NEAREST), (x + 8, y + 23))
    sheet.save(out / 'distribution-review.png')
    print(out / 'distribution-review.png')


if __name__ == '__main__':
    main()
