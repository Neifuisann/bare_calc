"""Capture actual Table/Equation controller frames for hardware/emu review.

Usage: review_table_equation.py reference.exe output-directory
These previews are not exact Casio raster goldens.
"""
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageDraw


def main():
    exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Run the existing app ledgers directly; report this scoped run separately
    # from the full reference suite and its shared CALC self-test gate.
    reference_root = Path(__file__).resolve().parents[2] / 'test/reference_cases'
    count = 0
    for filename in ('apps-table.json', 'apps-equation.json'):
        source = reference_root / filename
        for case in json.loads(source.read_text(encoding='utf-8'))['cases']:
            run = subprocess.run([str(exe), str(source), case['id']], capture_output=True, text=True)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            count += 1
    print(f'Table/Equation reference cases: {count} passed (pending evidence stays pending).')
    cap = lambda name: {'capture': name}
    sequences = {
        'table': ('Table', [cap('empty'), '9', cap('entry'), 'BACK', 'TOOLS', cap('tools'),
                            'DOWN', cap('tools-define'), 'UP', 'OK',
                            'DOWN', cap('range'), '2', cap('range-edit'), 'EXE', cap('range-committed'),
                            'DOWN', cap('range-execute'), 'UP', cap('range-step')]),
        'define': ('Table', ['TOOLS', 'DOWN', 'OK', cap('menu'), 'OK', cap('f')]),
        'coefficients': ('Equation', ['OK', 'OK', '1', cap('entry')]),
        'polynomial2': ('Equation', ['DOWN', 'OK', cap('menu'), 'OK', cap('empty'), '2', cap('draft'), 'EXE', cap('next')]),
        'polynomial3': ('Equation', ['DOWN', 'OK', 'DOWN', 'OK', cap('empty'), 'RIGHT', 'RIGHT', 'RIGHT', cap('constant')]),
        'polynomial4': ('Equation', ['DOWN', 'OK', 'DOWN', 'DOWN', 'OK', cap('empty'), 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', cap('constant')]),
        'simul2': ('Equation', ['OK', 'OK', cap('empty'), 'RIGHT', 'RIGHT', cap('rhs')]),
        'simul3': ('Equation', ['OK', 'DOWN', 'OK', cap('empty'), 'RIGHT', 'RIGHT', 'RIGHT', cap('rhs')]),
        'simul4': ('Equation', ['OK', 'DOWN', 'DOWN', 'OK', cap('empty'), 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', cap('rhs')]),
        'solver': ('Equation', ['DOWN', 'DOWN', 'OK', cap('empty'), '9', 'X', cap('equation'),
                                'EXE', cap('initial'), 'OK', cap('seed-edit'), 'EXE',
                                cap('execute'), 'EXE', cap('result')]),
        'solver-errors': ('Equation', ['DOWN', 'DOWN', 'OK', 'X', 'MULTIPLY', 'MULTIPLY', '2', 'EXE', cap('syntax'),
                                       'OK', 'AC', '2', 'EXE', cap('variable'), 'BACK', cap('restored')]),
        'solver-cancel': ('Equation', ['DOWN', 'DOWN', 'OK', '9', 'X', 'EXE',
                                       'OK', 'AC', '2', 'BACK', cap('restored'),
                                       'DOWN', 'EXE', cap('result')]),
    }
    cases = [{'id': name, 'initial': {'app': app, 'settings': {'inputOutput': 'MathMath',
              'angle': 'Radian', 'number': 'Norm1'}, 'memory': 'cleared', 'history': []},
              'keys': keys, 'checks': []} for name, (app, keys) in sequences.items()]
    expectations = {
        'table': {'entry': {'caret': {'visible': True, 'width': 2, 'height': 12}},
                  'range-edit': {'caret': {'visible': True}}},
        'solver': {'initial': {'screen': {'id': 'SolverSeed'}, 'caret': {'visible': False}},
                   'seed-edit': {'screen': {'id': 'SolverSeed'}, 'caret': {'visible': True}},
                   'execute': {'screen': {'id': 'SolverSeed'}, 'caret': {'visible': False}},
                   'result': {'result': {'hasResult': True, 'text': 'x=0; L-R=0'}}},
        'solver-cancel': {'restored': {'logical': {'source': '0'}, 'caret': {'visible': False}},
                          'result': {'result': {'text': 'x=0; L-R=0'}}},
        'solver-errors': {'syntax': {'screen': {'id': 'ErrorDialog'}, 'error': {'title': 'Syntax ERROR', 'origin': 'SolverEntry', 'cursor': 2}, 'caret': {'visible': False}},
                          'variable': {'screen': {'id': 'ErrorDialog'}, 'error': {'title': 'Variable ERROR', 'origin': 'SolverEntry'}, 'caret': {'visible': False}},
                          'restored': {'screen': {'id': 'SolverEntry'}, 'logical': {'source': '2'}, 'caret': {'visible': True}}},
        'coefficients': {'entry': {'caret': {'visible': True, 'width': 8, 'height': 1}}},
    }
    for case in cases:
        case['checks'] = [{'id': case['id'] + '-' + capture, 'capture': capture,
                           'expect': expected, 'native': 'pass'}
                          for capture, expected in expectations.get(case['id'], {}).items()]
    inputs = out / 'review-cases.json'
    inputs.write_text(json.dumps({'cases': cases}, indent=2), encoding='utf-8')
    captures = []
    for case in cases:
        subprocess.run([str(exe), str(inputs), case['id'], str(out)], check=True)
        captures += [case['id'] + '-' + step['capture'] for step in case['keys'] if isinstance(step, dict)]
    width, height = 600, 218
    sheet = Image.new('RGB', (width * 3, height * ((len(captures) + 2) // 3)), '#eeeeee')
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(captures):
        x, y = (i % 3) * width, (i // 3) * height
        draw.text((x + 8, y + 5), name, fill='black')
        panel = Image.open(out / (name + '.pgm')).convert('RGB')
        sheet.paste(panel.resize((576, 189), Image.Resampling.NEAREST), (x + 8, y + 23))
    sheet.save(out / 'table-equation-review.png')
    print(out / 'table-equation-review.png')


if __name__ == '__main__':
    main()
