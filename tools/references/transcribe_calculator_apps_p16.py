"""P16 manual logical contracts. Physical raster is deliberately pending.

Key identities were visually read from the official HTML-linked PNGs:
0062 BACK; 0061 AC; 006F ON; 004F OK; 0045 EXE; 002B +; 002D -.
No pixels from those reference images are included in firmware.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'test/reference_cases'


def build(app, entries):
    chapter = 'ratio' if app == 'Ratio' else 'creating_a_number_table'
    cases = []
    for n, item in enumerate(entries, 1):
        # Optional 5th element: extra 880-MAN sources (e.g. the errors page for
        # the target-confirmed over-capacity Range ERROR).
        title, keys, expected, pending = item[:4]
        extra = item[4] if len(item) > 4 else []
        ident = f'P16-{app.upper()}-{n:02}'
        check = dict(id=ident+'-1', capture='final', expect=expected,
                     native='pending' if pending else 'pass',
                     evidence='PENDING' if pending else '880-MAN',
                     confidence='UNKNOWN' if pending else 'STRONG', owner='P16')
        if pending:
            check.update(expect=None, reason=pending, pending_reason=pending, resolution='Acquire the physical target observation described in reason; retain pending until reviewed.')
        sources = [dict(kind='880-MAN', evidence=f'../../../991CN-X-CW-Decompilation/research/downloads/manual-880/using_calculator_apps__{chapter}.html.txt', section=title)] + extra
        cases.append(dict(id=ident, title=title, app=app, surface=title,
                          scope=dict(reference_model='fx-880BTG PLUS', target_applicability='STRONG'),
                          sources=sources,
                          initial=dict(app=app, settings=dict(inputOutput='MathMath', angle='Degree', number='Norm1'), memory='cleared', history=[]),
                          keys=keys+[dict(capture='final')], checks=[check], confidence=check['confidence'], native=check['native'], owner='P16', gap_ids=[],
                          notes=pending or '880-MAN logical behavior; controller and exact arithmetic safety additionally tested in calculator_apps_p16_test.h. Raster is not a target pass.'))
    (ROOT/f'apps-{app.lower()}.json').write_text(json.dumps(dict(schema=1, phase='P16', cases=cases), indent=2)+'\n', encoding='utf-8')


def entry(text):
    return [{'-':'SUBTRACT', '.':'DOT', '/':'DIVIDE'}.get(c,c) for c in text]+['EXE']


def keys(*names):
    return list(names)


ratio = keys('OK')+entry('3')+entry('8')+entry('12')
solve = ratio+keys('EXE')
second = keys('DOWN','OK')+entry('2')+entry('3')+entry('4')+keys('EXE')
build('Ratio', [
    ('launch type menu', [], {'menu':{'labels':['A:B=X:D','A:B=C:X']}}, None),
    ('first form coefficient editor', keys('OK'), {'screen':{'id':'RatioCoefficients'},'grid':{'cols':3,'cells':['0','0','0']}}, None),
    ('coefficient navigation', keys('OK','RIGHT'), {'grid':{'col':1}}, None),
    ('3:8=X:12 exact solution', solve, {'result':{'text':'9/2','hasResult':True}}, None),
    ('second form selection', keys('DOWN','OK'), {'screen':{'id':'RatioCoefficients'}}, None),
    ('2:3=4:X exact solution', second, {'result':{'text':'6'}}, None),
    ('zero A', keys('OK')+entry('0')+entry('8')+entry('12')+keys('EXE'), {'error':{'title':'Math ERROR'}}, None),
    ('zero B', keys('OK')+entry('3')+entry('0')+entry('12')+keys('EXE'), {'error':{'title':'Math ERROR'}}, None),
    ('zero third coefficient', keys('OK')+entry('3')+entry('8')+entry('0')+keys('EXE'), {'error':{'title':'Math ERROR'}}, None),
    ('BACK result return', solve+keys('BACK'), {'screen':{'id':'RatioCoefficients'},'grid':{'cells':['3','8','12']}}, None),
    ('shared Store result return', solve+keys('VARIABLE','OK','DOWN','OK','BACK'), {'screen':{'id':'RatioResult'},'result':{'text':'9/2'}}, None),
    ('BACK type change resets', ratio+keys('BACK','DOWN','OK'), {'grid':{'cells':['0','0','0']}}, None),
    ('AC coefficient reset', ratio+keys('AC'), {'grid':{'cells':['0','0','0']}}, None),
    ('async timing evidence', [], {'screen':{'id':'RatioType'}}, 'Native leave/edit/type stale-job guards are tested. Target timing and busy raster need a physical capture; P20 owns capture.'),
    ('HOME clears coefficients', ratio+keys('HOME','OK'), {'grid':{'cells':['0','0','0']}}, None),
    ('physical menu icons raster', [], {'screen':{'id':'RatioType'}}, 'No physical fx-880 Ratio frames. Owner P20: capture both formula grids, exact fraction result and icon states.'),
])

def tools(index):
    return keys('TOOLS')+keys(*(['DOWN']*index))+keys('OK')


def definition(index, text):
    return tools(1)+keys(*(['DOWN']*index))+keys('OK')+text+keys('EXE','BACK')


defs = definition(0, keys('X','SQUARE','ADD','1','DIVIDE','2'))+definition(1, keys('X','SQUARE','SUBTRACT','1','DIVIDE','2'))
range5 = tools(0)+entry('-1')+entry('1')+entry('0.5')+keys('EXE')
example = defs+range5
single = tools(2)+keys('DOWN','OK')
# Range ERROR is target-confirmed ONLY for tables exceeding the 30/45-row
# maximum (880-MAN technical_information__errors). Step=0, negative Step,
# Start>End and Insert Row at capacity are safely bounded natively but the
# manual names no error for them, so they stay PENDING, never 880-MAN pass.
range_error_source = [dict(kind='880-MAN', evidence='../../../991CN-X-CW-Decompilation/research/downloads/manual-880/technical_information__errors.html.txt', section='Range ERROR')]
step0_pending = 'Native rejects Step=0 with Range ERROR as a safe bound (no rows published, x untouched). The manual names no error for a zero step; exact target behavior is PENDING. Owner P20: physical target capture.'
negstep_pending = 'Native rejects a negative Step with Range ERROR as a safe bound (no rows published, x untouched). The manual names no error for a negative step; exact target behavior is PENDING. Owner P20: physical target capture.'
descending_pending = 'Native rejects Start>End with Range ERROR as a safe bound instead of generating a descending range, which the manual never defines. Exact target behavior is PENDING; do not invent descending ranges. Owner P20: physical target capture.'
insertcap_pending = 'Native rejects Insert Row at capacity with Range ERROR as a safe bound (rows unchanged). The manual confirms the 45/30 maximum but names no error for row insertion at capacity; exact target behavior is PENDING. Owner P20: physical target capture.'
build('Table', [
    ('undefined Table launch', [], {'screen':{'id':'Table'},'grid':{'rows':0,'cols':3},'notice':'Not Defined'}, None),
    ('exact three Table Types', tools(2), {'menu':{'labels':['f(x)/g(x)','f(x)','g(x)']}}, None),
    ('single f mode', single, {'grid':{'cols':2,'rows':0}}, None),
    ('single g mode', tools(2)+keys('DOWN','DOWN','OK'), {'grid':{'cols':2}}, None),
    ('shared Define f editor', tools(1)+keys('OK'), {'screen':{'id':'Define'}}, None),
    ('shared Define g editor', tools(1)+keys('DOWN','OK'), {'screen':{'id':'Define'}}, None),
    ('range Start End Step Execute', tools(0), {'screen':{'id':'TableRange'}}, None),
    ('manual example real evaluation', example, {'grid':{'rows':5,'cols':3}}, None),
    ('30 dual rows', tools(0)+entry('1')+entry('30')+entry('1')+keys('EXE'), {'grid':{'rows':30}}, None),
    ('31 dual Range ERROR', tools(0)+entry('1')+entry('31')+entry('1')+keys('EXE'), {'error':{'title':'Range ERROR'}}, None, range_error_source),
    ('45 single rows', single+tools(0)+entry('1')+entry('45')+entry('1')+keys('EXE'), {'grid':{'rows':45}}, None),
    ('46 single Range ERROR', single+tools(0)+entry('1')+entry('46')+entry('1')+keys('EXE'), {'error':{'title':'Range ERROR'}}, None, range_error_source),
    ('x edit preserves row count', example+entry('2'), {'grid':{'rows':5}}, None),
    ('Delete Row', example+keys('DEL'), {'grid':{'rows':4}}, None),
    ('Insert Row', example+tools(3)+keys('OK'), {'grid':{'rows':6}}, None),
    ('Delete All', example+tools(3)+keys('DOWN','OK','DOWN','OK'), {'grid':{'rows':0}}, None),
    ('type change clears generated data', example+tools(2)+keys('DOWN','OK'), {'grid':{'rows':0,'cols':2}}, None),
    ('Recalculate preserves x rows', example+tools(4), {'grid':{'rows':5}}, None),
    ('Verify ON', example+tools(5), {'icons':{'VERIFY':True}}, None),
    ('Verify expected first f', example+tools(5)+keys('RIGHT')+entry('1.5'), {'result':{'text':'True'}}, None),
    ('Verify OFF', example+tools(5)+tools(5), {'icons':{'VERIFY':False},'grid':{'rows':5}}, None),
    ('shared Store returns selected cell', example+keys('VARIABLE','OK','DOWN','OK','BACK'), {'screen':{'id':'Table'},'grid':{'rows':5}}, None),
    ('physical grid and domain error raster', [], {'screen':{'id':'Table'}}, 'Owner P20: physical Table grid/menu/icon/domain-error captures absent. Shared grid error labels are provisional; no target raster pass.'),
    ('ON input delivery', [], {'screen':{'id':'Table'}}, '006F is visually confirmed ON. NativeOn transition is tested; current scanner exposes no ON event. Hardware owner must deliver the real ON event, never map AC to ON.'),
    ('initial range defaults', tools(0), {'screen':{'id':'TableRange'}}, 'Owner P20: 1,5,1 are safe initial range defaults; initial target values require physical capture. User-entered ranges are manual-tested.'),
    ('Step 0 exact target error', tools(0)+entry('1')+entry('5')+entry('0')+keys('EXE'), {'screen':{'id':'Table'}}, step0_pending),
    ('negative Step exact target error', tools(0)+entry('1')+entry('5')+entry('-1')+keys('EXE'), {'screen':{'id':'Table'}}, negstep_pending),
    ('Start greater than End exact target error', tools(0)+entry('5')+entry('1')+entry('1')+keys('EXE'), {'screen':{'id':'Table'}}, descending_pending),
    ('Insert Row at capacity exact target error', single+tools(0)+entry('1')+entry('45')+entry('1')+keys('EXE')+tools(3)+keys('OK'), {'screen':{'id':'Table'}}, insertcap_pending),
])
