"""Validate the committed ledger, then run each case in a fresh native process."""
import json
import subprocess
import sys
import tempfile
import copy
from pathlib import Path

FIELDS = {
    'logical': {'source','cursor','fieldRole'}, 'screen': {'id'},
    'menu': {'id','selection','first','visibleRows','lastSelected','labels','labelSet'},
    'result': {'text','hasResult','page'}, 'history': {'cursor'},
    'app': {'id','base','historySize'},
    'grid': {'row','col','rows','cols','editing','cells'},
    'error': {'title','origin','cursor'},
    'caret': {'visible','kind','rect','x','width','height'},
    'icons': {'MATH','D','S','UP','DOWN','LEFT','RIGHT','allOff','i','angle','R','G','FIX','SCI','E','VERIFY'},
    'notice': None, 'physicalHomeMatch': None,
}
def validate_expectation(q):
    expected=q['expect']
    assert isinstance(expected,dict) and expected, 'Empty expectation'
    if 'measure' in q:
        assert set(q['measure']) <= {'region','maskCaret','caretOff','level','fractionGaps'}, 'Unknown measurement'
        assert set(expected) <= {'bbox','width','height','count','hasGrey','blankAboveRule','blankBelowRule'}, 'Unknown raster observation'
    elif 'delta' in q:
        assert set(expected)=={'value'} and q['delta']['path'] in (['caret','x'],), 'Unsupported delta'
    else:
        assert set(expected)<=set(FIELDS), 'Unknown observation'
        for name,value in expected.items():
            if FIELDS[name] is not None:
                assert isinstance(value,dict) and value and set(value)<=FIELDS[name], 'Unknown nested observation'
    def nonnull(value):
        assert value is not None, 'Unknown expectation must be pending'
        if isinstance(value,dict):
            for x in value.values():nonnull(x)
        if isinstance(value,list):
            for x in value:nonnull(x)
    nonnull(expected)

def validate(root):
    ledger = json.loads((root/'known_gaps.json').read_text(encoding='utf-8'))
    cases, checks, used = {}, set(), {k: [] for k in ledger}
    for name in ('editor', 'app', 'geometry', 'apps-complex', 'apps-basen', 'apps-matrix', 'apps-vector', 'apps-equation', 'apps-inequality', 'apps-ratio', 'apps-table', 'apps-statistics', 'apps-distribution'):
        path = root/(name+'.json')
        document = json.loads(path.read_text(encoding='utf-8'))
        assert document['schema'] == 1
        for c in document['cases']:
            assert c['id'] not in cases, 'Duplicate case'
            cases[c['id']] = (path, c)
            assert c['initial']['app'] in ('Calculate','Complex','Base-N','Matrix','Vector','Equation','Inequality','Ratio','Table','Statistics','Distribution') and c['initial']['memory']=='cleared' and c['initial']['history']==[], 'Unsupported initial state'
            assert c['initial']['settings']['inputOutput']=='MathMath' and c['initial']['settings']['angle']=='Degree' and c['initial']['settings']['number']=='Norm1', 'Unsupported initial state'
            assert set(c['initial']['settings'])<={'inputOutput','angle','number','formatMenu','complexResult'}, 'Unsupported initial state'
            assert c['initial']['settings'].get('complexResult','Rectangular') in ('Rectangular','Polar'), 'Unsupported complex result setting'
            assert c['initial']['settings'].get('formatMenu','Decimal toggle') in ('Menu','Decimal toggle'), 'Unsupported initial state'
            assert c['sources'] and c['confidence'] in ('CONFIRMED','STRONG','LIKELY','UNKNOWN')
            if name.startswith('apps-'):
                assert c['id'].startswith(('P15-', 'P16-', 'STAT-', 'DIST-')) and c['app']==c['initial']['app'] and c['surface'] and c['owner'], 'Missing app provenance'
                for q in c['checks']:
                    assert q['evidence'] in ('880-HW','880-MAN','EMU-CW','SOURCE-ONLY','PENDING') and q['confidence'] and q['owner'], 'Missing P15 check evidence'
                    assert q['native']!='pass' or q['evidence'] in ('880-HW','880-MAN','EMU-CW'), 'Source-only cannot establish fidelity'
            if any(s['kind']=='emu-cw' for s in c['sources']):
                assert len(c['scope']['rom_sha256'])==64 and c['scope']['emulator'].startswith('CasioEmuX ')
            captures = [s['capture'] for s in c['keys'] if isinstance(s,dict)]
            assert len(captures)==len(set(captures)), 'Duplicate capture'
            assert c['checks'], 'Empty case'
            gaps=sorted({q['native']['gap'] for q in c['checks'] if isinstance(q['native'],dict)})
            assert c['gap_ids']==gaps, 'Stale case gap index'
            assert c['native']==({'gap':gaps[0]} if gaps else 'pending' if all(q['native']=='pending' for q in c['checks']) else 'pass'), 'Stale case status'
            for q in c['checks']:
                assert q['id'] not in checks, 'Duplicate check'
                checks.add(q['id'])
                assert q['capture'] in captures
                status = q['native']
                if status == 'pending':
                    assert q['expect'] is None and q['reason'] and q['owner'] and q['resolution']
                else:
                    validate_expectation(q)
                    if 'delta' in q:assert q['delta']['capture'] in captures, 'Missing delta capture'
                    if isinstance(status,dict):
                        assert set(status)=={'gap'} and status['gap'] in ledger, 'Unknown gap'
                        used[status['gap']].append(q['id'])
                    else:
                        assert status=='pass', 'Invalid ledger status'
    assert {k for k in cases if not k.startswith(('P15-', 'P16-', 'STAT-', 'DIST-'))}=={f'C{i:02}' for i in range(1,71)}|{f'C{i}' for i in range(71,101)}|{f'GEO-{i:02}' for i in range(1,24)}|{f'GEO-P{i:02}' for i in range(1,5)}, 'Missing reference row'
    for k,v in ledger.items():
        assert sorted(v['checks'])==sorted(used[k]), f'Stale ledger {k}'
    for app in ('Complex','Base-N','Matrix','Vector','Equation','Inequality'):
        surfaces={c['surface'] for _,c in cases.values() if c.get('app')==app}
        assert {'launch','input','result','FORMAT','menu','error','retention','stale job','render reference'} <= surfaces, 'Missing P15 surface: '+app
    return cases

def main():
    exe, root = Path(sys.argv[1]), Path(sys.argv[2])
    cases=validate(root)
    # This runs the complete controller safety/fuzz suite, unlike each single
    # replay below. It can exceed 30 seconds while firmware compiles in parallel.
    subprocess.run([str(exe),'--self-test'],check=True,timeout=120)
    # Prove both ledger directions and malformed input fail. Never modify the
    # committed cases or use native output to generate expected values.
    base=copy.deepcopy(cases['C02'][1])
    mutations=[('pass regression',{'logical':{'source':'deliberately wrong'}},'pass',None,1),
               ('unexpected correction',{'logical':{'source':'9^(32)'}},{'gap':'E-17'},None,1),
               ('unknown observation',{'notAField':1},{'gap':'E-17'},None,2),
               ('unknown key',{'logical':{'source':'9^(32)'}},'pass','NOT_A_KEY',2)]
    with tempfile.TemporaryDirectory(prefix='calculator-ledger-') as tmp:
        path=Path(tmp)/'mutation.json'
        for title,expected,status,bad_key,code in mutations:
            c=copy.deepcopy(base);c['checks']=[{'id':'mutation','capture':'final','expect':expected,'native':status}]
            if bad_key:c['keys'].insert(0,bad_key)
            path.write_text(json.dumps({'cases':[c]}),encoding='utf-8')
            run=subprocess.run([str(exe),str(path),c['id']],capture_output=True,timeout=30)
            assert run.returncode==code, f'{title} guard failed: {run.stdout!r} {run.stderr!r}'
    print('Reference ledger: pass regression, unexpected correction, unknown field and unknown key rejection passed.')
    failures=0
    totals=[0,0,0]
    import re
    for id,(path,c) in cases.items():
        run=subprocess.run([str(exe),str(path),id],capture_output=True,text=True,encoding='utf-8',timeout=30)
        print(run.stdout,end='')
        if run.stderr: print(run.stderr,end='',file=sys.stderr)
        failures+=run.returncode!=0
        m=re.search(r': (\d+) pass, (\d+) known gaps, (\d+) pending,',run.stdout)
        if m: totals=[a+int(b) for a,b in zip(totals,m.groups())]
    print(f'Reference suite: {len(cases)} cold cases, {totals[0]} pass, {totals[1]} known gaps, {totals[2]} pending; {failures} failed cases.')
    return bool(failures)

if __name__=='__main__':
    sys.exit(main())
