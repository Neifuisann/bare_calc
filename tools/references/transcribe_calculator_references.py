"""One-time/repeatable transcription of spec facts, never native observations.

Run from the project root. Requires the separate research checkout; CI consumes JSON only.
"""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT.parent / '991CN-X-CW-Decompilation/research/CLASSWIZ_COMPATIBILITY_ENGINEERING_SPEC.md'
text = SPEC.read_text(encoding='utf-8')
rows = {}
for line in text.splitlines():
    if re.match(r'\| C\d\d \|', line):
        cells = [v.strip() for v in line.strip('|').split('|')]
        rows[cells[0]] = cells
scope = {'reference_model': 'fx-991CN CW Ver A',
         'rom_sha256': '4ab4791609cc8686fc6b4d1be34b3e6d19b3eb700abe8c9e3edd91ff2813471f',
         'emulator': 'CasioEmuX c887be7c956ddd970abb82e7a40a186c9c4e4289',
         'target_applicability': 'STRONG'}
cases = []

def case(n, keys):
    id = f'C{n:02}' if isinstance(n, int) else n
    row = rows.get(id)
    c = {'id': id, 'title': row[2] if row else id, 'scope': dict(scope),
         'sources': [{'kind': 'emu-cw', 'evidence': row[5] if row else 'spec section 3.6',
                      'specification': SPEC.name, 'section': '3.5' if row else '3.6'}],
         'initial': {'app': 'Calculate', 'settings': {'inputOutput': 'MathMath', 'angle': 'Degree', 'number': 'Norm1'},
                     'memory': 'cleared', 'history': []},
         'keys': keys.split() + [{'capture': 'final'}], 'checks': [], 'confidence': 'CONFIRMED',
         'notes': 'Source spellings encode the observed structure in native canonical syntax; they are not ROM input-buffer observations. '
                  'Reference outcomes and probes, not native renders, justify expectations.'}
    if row:
        c['reference'] = {'keys': row[1], 'outcome': row[2], 'research_verdict': row[4]}
    cases.append(c)
    return c

def check(c, expect, gap=None, capture='final', measure=None, pending=None):
    item = {'id': c['id'] + '-' + str(len(c['checks']) + 1), 'capture': capture,
            'expect': expect, 'native': {'gap': gap} if gap else 'pass'}
    if pending:
        item.update(native='pending', expect=None, status='pending', reason=pending,
                    owner='ClassWiz reference maintainer', resolution='Physical fx-880 capture or approved reference-kit probe')
    if measure:
        item['measure'] = measure
    c['checks'].append(item)
    return c

def source(c, value, gap=None, **kw):
    return check(c, {'logical': {'source': value, **kw}}, gap)

def result(c, value, gap=None):
    return check(c, {'result': {'text': value, 'hasResult': True}}, gap)

def checkpoint(c, keys, expect, gap=None):
    # Insert intermediate snapshots without resetting the cold scenario.
    name = f'step{len(c["checks"])}'
    c['keys'].extend(keys.split() + [{'capture': name}])
    return check(c, expect, gap, capture=name)

c=source(case(1,'9 POWER 3'),'9^(3)',cursor=4,fieldRole='Exponent')
check(c,{'caret':{'width':3,'height':9}},'L-03')
check(c,{'hasGrey':True},'L-05',measure={'region':[0,0,30,30],'maskCaret':True})
source(case(2,'9 POWER 3 2'),'9^(32)')
c=source(case(3,'9 POWER 3 RIGHT'),'9^(3)',cursor=5,fieldRole='Root')
check(c,{'caret':{'width':3,'height':17}},'L-03')
source(case(4,'9 POWER 3 RIGHT 2'),'9^(3)2')
source(case(5,'9 POWER 3 LEFT 2'),'9^(23)')
c=source(case(6,'9 POWER 3 DOWN'),'9^(3)',cursor=4,fieldRole='Exponent')
checkpoint(c,'2',{'logical':{'source':'9^(32)'}})
source(case(7,'9 POWER 3 UP'),'9^(3)',cursor=4,fieldRole='Exponent')
c=source(case(8,'9 POWER 3 DEL'),'9^()')
checkpoint(c,'2',{'logical':{'source':'9^(2)'}})
source(case(9,'9 POWER 3 POWER'),'9^(3^())')
source(case(10,'9 POWER 3 SQUARE 2'),'9^(3^(2)2)')
c=source(case(11,'9 POWER 3 FRACTION'),'9^(frac(3,))')
checkpoint(c,'2 FRACTION',{'logical':{'source':'9^(frac(3,frac(2,)))'}})
c=source(case(12,'9 POWER 3 ROOT'),'9^(3sqrt())')
c=source(case(13,'9 POWER 3 PAGE_UP 2'),'29^(3)')
checkpoint(c,'AC 9 POWER 3 PAGE_DOWN 2',{'logical':{'source':'9^(3)2'}})
check(case(14,'9 POWER 3 RIGHT 2 RIGHT'),{'logical':{'cursor':0,'fieldRole':'Root'}})
source(case(15,'1 2 PAGE_UP LEFT 3'),'123')
source(case(16,'9 POWER 3 LEFT LEFT UP 2'),'92^(3)')
c=check(case(17,'FRACTION 1 RIGHT'),{'logical':{'fieldRole':'Root'}})
checkpoint(c,'2',{'logical':{'source':'frac(1,)2'}})
source(case(18,'FRACTION 1 DOWN 2'),'frac(1,2)')
source(case(19,'FRACTION 1 UP 2'),'frac(12,)')
source(case(20,'FRACTION 1 LEFT 2'),'frac(21,)')
source(case(21,'FRACTION 1 DOWN 2 RIGHT 3'),'frac(1,2)3')
source(case(22,'FRACTION 1 DOWN LEFT 3'),'3frac(1,)')
source(case(23,'FRACTION 1 DOWN 2 LEFT LEFT 3'),'3frac(1,2)')
source(case(24,'FRACTION 1 LEFT LEFT 3'),'3frac(1,)')
source(case(25,'FRACTION 9 POWER 3 DOWN 2'),'frac(9^(3),2)')
source(case(26,'FRACTION 1 DOWN 2 UP 3'),'frac(13,2)')
source(case(27,'1 2 PAGE_UP DEL 3'),'32')
c=source(case(28,'FRACTION 1 LEFT DEL'),'frac(,)',fieldRole='Numerator')
checkpoint(c,'3',{'logical':{'source':'frac(3,)'}})
source(case(29,'2 ADD FRACTION 1 LEFT DEL 3'),'2+3frac(1,)')
source(case(30,'ROOT 9 LEFT DEL 2'),'29')
source(case(31,'FRACTION 1 DOWN 2 LEFT DEL 3'),'132')
source(case(32,'9 POWER 3 LEFT DEL 2'),'923')
c=source(case(33,'FRACTION 1 DOWN 2 RIGHT DEL'),'frac(1,)',fieldRole='Denominator')
checkpoint(c,'DEL',{'logical':{'source':'1'}})
c=source(case(34,'9 POWER DEL'),'9')
checkpoint(c,'DEL',{'logical':{'source':''}})
c=source(case(35,'ROOT 9 RIGHT DEL'),'sqrt()')
checkpoint(c,'DEL',{'logical':{'source':''}})
source(case(36,'ROOT 9 RIGHT 2'),'sqrt(9)2')
c=source(case(37,'OPEN 1 ADD 2 CLOSE DEL'),'(1+2')
checkpoint(c,'DEL',{'logical':{'source':'(1+'}})
c=source(case(38,'SIN 3 CLOSE DEL'),'sin(3')
checkpoint(c,'DEL',{'logical':{'source':'sin('}})
checkpoint(c,'DEL',{'logical':{'source':''}})
source(case(39,'9 PAGE_UP ROOT 2'),'sqrt(2)9')
source(case(40,'9 POWER 3 RIGHT PAGE_UP SHIFT DEL ROOT 2'),'sqrt(29^(3))')
source(case(41,'FRACTION 1 DOWN 2 RIGHT POWER 3'),'frac(1,2)^(3)')
c=source(case(42,'2 X POWER 3'),'2x^(3)')
c['confidence']='LIKELY';c['scope']['target_applicability']='LIKELY'
check(c,None,pending='Value with x=2 and exact base scope not established by the visual probe (C42).')
c=source(case(43,'SHIFT SHIFT 9'),'9');check(c,{'icons':{'S':False}})
c=result(case(44,'9 POWER 3 EXE'),'729');check(c,{'icons':{'UP':True}})
source(case(45,'9 POWER 3 EXE 2'),'2')
source(case(46,'1 EXE ADD 2'),'Ans+2')
c=source(case(47,'1 DIVIDE 3 EXE LEFT'),'1/3',cursor=3)
checkpoint(c,'EXE RIGHT',{'logical':{'source':'1/3','cursor':0}})
source(case(48,'1 EXE BACK 2'),'12','A-13')
result(case(49,'1 DIVIDE 3 SHIFT EXE'),'0.3333333333','R-01')
c=check(case(50,'1 DIVIDE 3 EXE FORMAT'),{'screen':{'id':'Format'}},'R-05')
# P12 closure: the emu-cw capture was recorded with the FORMAT Key setting on
# Menu, while cold firmware defaults to Decimal toggle (plain FORMAT flips the
# view, SHIFT FORMAT opens the menu). The mismatch is transcribed explicitly
# instead of preserved as a gap: FORMAT behavior stays setting-aware.
c['initial']['settings']['formatMenu']='Menu'
c['notes'] += ' Emulator evidence captured with FORMAT Key=Menu; initial settings pin that choice.'
checkpoint(c,'SHIFT FORMAT',{'screen':{'id':'Format'}})
checkpoint(c,'AC',{'screen':{'id':'Result'}},'A-04')
result(case(51,'9 POWER 3 PAGE_UP 2 EXE'),'24389')
result(case(52,'9 POWER 3 PAGE_UP RIGHT 2 EXE'),'778688')
c=check(case(53,'1 EXE 2 EXE UP'),{'result':{'hasResult':True,'text':'1'},'caret':{'visible':False}},'A-01')
check(c,{'icons':{'UP':False,'DOWN':True}})
checkpoint(c,'UP',{'result':{'hasResult':True,'text':'1'}},'A-01')
checkpoint(c,'DOWN',{'result':{'hasResult':True,'text':'2'}},'A-01')
c=result(case(54,'1 EXE AC UP'),'1','A-02');check(c,{'icons':{'UP':True}},'I-01')
source(case(55,'1 EXE 2 EXE UP LEFT 3'),'13','A-01')
result(case(56,'1 EXE 2 EXE UP EXE'),'1')
c=source(case(57,'1 DIVIDE 3 EXE AC LEFT'),'1/3','A-03',cursor=3)
checkpoint(c,'AC RIGHT',{'logical':{'source':'1/3','cursor':0}},'A-03')
c=check(case(58,'1 DIVIDE 0 EXE'),{'screen':{'id':'ErrorDialog'}},'A-05')
check(c,{'icons':{'allOff':True}},'I-02')
check(c,{'error':{'title':'Math ERROR','origin':'Entry','cursor':3},'caret':{'visible':False},'result':{'hasResult':False}})
for key in ['OK','AC','BACK']:
    checkpoint(c,key,{'screen':{'id':'Entry'},'logical':{'source':'1/0','cursor':3}},'A-05' if key in ['OK','AC'] else 'E-14')
    if key!='BACK':
        name=f'error{key}';c['keys'].extend('AC 1 DIVIDE 0 EXE'.split())
c=check(case(59,'1 MULTIPLY MULTIPLY 2 EXE'),{'screen':{'id':'ErrorDialog'}},'A-05')
check(c,{'error':{'title':'Syntax ERROR','origin':'Entry','cursor':2},'logical':{'source':'1**2'}})
checkpoint(c,'BACK',{'logical':{'source':'1**2','cursor':2}},'E-14')
checkpoint(c,'3',{'logical':{'source':'1*3*2'}},'E-14')
c=case(60,'CATALOG AC');check(c,{'screen':{'id':'Entry'}},'A-04')
for menu in ['TOOLS','SETTINGS','VARIABLE','FUNCTION']:
    checkpoint(c,'BACK '+menu+' AC',{'screen':{'id':'Entry'}},'A-04')
c=source(case(61,'1 CATALOG AC'),'1');check(c,{'screen':{'id':'Entry'}},'A-04')
c=check(case(62,'SETTINGS'),{'menu':{'labels':['Calc Settings','System Settings','Reset','Get Started']}},'A-08')
c['sources'].append({'kind':'user-supplied-emulator','evidence':'test/calculator_references/settings-root-emulator-paint-original.bmp'})
c['notes'] += ' Four-row root explicitly requested; supplied BMP was clarified as another emulator pasted into Paint, not physical evidence. Get Started destination pending.'
checkpoint(c,'OK',{'screen':{'id':'CalcSettings'}},'A-08')
checkpoint(c,'BACK',{'screen':{'id':'Settings'}})
checkpoint(c,'BACK',{'screen':{'id':'Entry'}})
c=check(case(63,'SETTINGS UP'),{'menu':{'lastSelected':True}})
checkpoint(c,'DOWN DOWN DOWN',{'menu':{'selection':2}})
check(case(64,'SETTINGS OK HOME'),{'screen':{'id':'Home'}})
check(case(65,'1 2 BACK'),{'screen':{'id':'Home'}},'A-06')
c=case(66,' '.join(['9']*20));check(c,{'icons':{'LEFT':False,'RIGHT':False}})
check(c,{'bbox':[2,3,8,7]},'L-08',measure={'region':[0,0,10,14]})
checkpoint(c,'PAGE_UP',{'icons':{'LEFT':False,'RIGHT':False}})
check(c,{'bbox':[183,3,8,7]},'L-08',capture='step2',measure={'region':[183,0,9,14]})
c=check(case(67,'VARIABLE'),{'screen':{'id':'Variables'}})
checkpoint(c,'2',{'screen':{'id':'Store'},'logical':{'source':'2'}})
check(case(68,'FUNCTION'),{'menu':{'labels':['f(x)','g(x)','Define f(x)','Define g(x)']}})
source(case(69,'TIMES_TEN_POWER'),'*10^()')
c=case(70,'LOG');c['scope']['target_applicability']='MODEL-SPECIFIC'
check(c,None,pending='CN CW log-base key is not target evidence. Follow fx-880 manual; target log/template and cursor capture required.')
# P2 pending: authentic empty-line Power base-slot behavior is UNKNOWN (spec
# 9 P2, E-17). Native keeps the legacy pow(,) fallback; never infer from it.
c=case(71,'POWER')
c['confidence']='UNKNOWN'
check(c,None,pending='Power with no preceding operand: authentic ClassWiz base-slot behavior UNKNOWN. Native retains pow(,) fallback.')
# P2 pending: unary-minus power sign convention is UNKNOWN without kit/reference
# evidence (spec 9 P2 risks). Do not encode -2^(2)=-4 as compatibility truth.
c=case(72,'SUBTRACT 2 POWER 2 EXE')
c['confidence']='UNKNOWN'
check(c,None,pending='-2^(2) sign convention UNKNOWN: requires reference-kit EXE value before asserting.')
# P3 pending: horizontal/vertical stops the specification leaves UNKNOWN (spec
# 9 P3 tests, E-06/E-07 scope). Probes only; never infer from native output.
for n,keys,reason in [
    (73,'SHIFT FRACTION 1 RIGHT 2 RIGHT','Right at the numerator end of a mixed number UNKNOWN: only Fraction numerator-exit is reference-covered.'),
    (74,'SHIFT SQUARE 2 RIGHT','Right from a log-base field UNKNOWN: E-06 covers Fraction only.'),
    (75,'FRACTION 1 DOWN 9 POWER 3 DOWN','Down from an exponent nested inside a denominator UNKNOWN: E-02 covers root level and numerator nesting only.'),
    (76,'VARIABLE 2 PAGE_UP LEFT','Horizontal wrapping in the Store field UNKNOWN: P3 enables wrap for the main Calculate editor only.'),
    (77,'FUNCTION DOWN DOWN OK 1 PAGE_UP LEFT','Horizontal wrapping in the Define field UNKNOWN: P3 enables wrap for the main Calculate editor only.'),
    (78,'X TOOLS OK 5 PAGE_UP LEFT','Horizontal wrapping in the CALC field UNKNOWN: P3 enables wrap for the main Calculate editor only.'),
]:
    c=case(n,keys)
    c['confidence']='UNKNOWN'
    check(c,None,pending=reason)
# P4 pending: structural-deletion behavior the specification leaves UNKNOWN
# (spec 9 P4 tests). Probes only; never infer from native output. Later
# generalization of splice() or deleteForward() must not silently claim
# these structures without a reference capture each.
for n,keys,reason in [
    (79,'FRACTION DOWN 2 UP DEL','DEL at the start of an empty first field UNKNOWN: E-08 covers forward delete into a populated first field only.'),
    (80,'SHIFT FRACTION 1 DOWN 2 UP DEL','DEL at the start of a Mixed numerator (non-first field) UNKNOWN: E-10 covers Fraction and Power/Script only.'),
    (81,'SHIFT SQUARE 2 RIGHT DEL','DEL at the start of a Log argument UNKNOWN: E-10 covers Fraction and Power/Script only.'),
    (82,'SHIFT ROOT 3 RIGHT DEL','DEL at the start of a Root radicand (non-first field) UNKNOWN: E-10 covers Fraction and Power/Script only.'),
    (83,'CATALOG OK DOWN OK 1 DOWN DEL','DEL at the start of an Integral lower limit UNKNOWN: E-10 covers Fraction and Power/Script only.'),
    (84,'CATALOG OK DOWN DOWN OK 1 DOWN DEL','DEL at the start of a Sum lower limit UNKNOWN: E-10 covers Fraction and Power/Script only.'),
    (85,'CATALOG OK DOWN DOWN DOWN OK 1 DOWN DEL','DEL at the start of a Product lower limit UNKNOWN: E-10 covers Fraction and Power/Script only.'),
    (86,'FRACTION FRACTION 1 LEFT DEL','Forward DEL inside a nested first field UNKNOWN: E-08 covers a first field at the root start only.'),
    (87,'FRACTION 1 DOWN 2 PAGE_UP DEL','Forward DEL when the next item is a complete structure UNKNOWN: E-08 covers the next item being an ordinary glyph.'),
    (88,'FRACTION FRACTION 1 DOWN 2 RIGHT DEL','DEL after a nested structure UNKNOWN: E-09 covers a structure directly in its own field.'),
]:
    c=case(n,keys)
    c['confidence']='UNKNOWN'
    check(c,None,pending=reason)
# P4 review follow-up: the splice table covers legacy Kind::Power per the
# section 6.3 pseudocode, but no live reference case builds a legacy pow (new
# powers are postfix Scripts since P2). Pin the coverage gap as pending
# rather than claiming the splice as compatible.
c=case(89,'POWER 9 RIGHT 3 LEFT DEL')
c['confidence']='UNKNOWN'
check(c,None,pending='DEL at the start of a legacy pow exponent UNKNOWN: E-10 names Power, but no reference case builds a legacy pow since P2 made new powers postfix Scripts.')
# P5: edit transactions (E-13), INS following-operand wrap (E-11), SHIFT
# template jumps (E-12) and open calls (E-16). Executable checks assert only
# reference-backed behavior; uncertain boundaries stay pending probes.
c=source(case(90,'CATALOG DOWN DOWN DOWN DOWN OK DOWN DOWN DOWN DOWN DOWN DOWN OK'),'sin(',cursor=4)
checkpoint(c,'TOOLS DOWN OK',{'logical':{'source':'','cursor':0}})
source(case(91,'1 ADD FRACTION 7 DOWN 6 LEFT LEFT SHIFT DEL ROOT'),'1+sqrt(frac(7,6))')
source(case(92,'CATALOG OK DOWN OK 1 DOWN SHIFT RIGHT'),'integral(1,,)',cursor=13)
source(case(93,'CATALOG OK DOWN DOWN OK 1 SHIFT LEFT'),'sum(1,,)',cursor=0)
for n,keys,reason in [
    (94,'9 POWER 3 RIGHT PAGE_UP SHIFT DEL 2','INS followed by a non-template key UNKNOWN: the manual only pins consumption by a template key.'),
    (95,'9 POWER 3 RIGHT PAGE_UP SHIFT DEL CATALOG BACK','INS followed by a menu open UNKNOWN: no target capture pins whether arming survives.'),
    (96,'9 POWER 3 RIGHT PAGE_UP SHIFT DEL FRACTION','INS followed by a Fraction key UNKNOWN: only Root consumption is reference-covered.'),
    (97,'ROOT 9 SHIFT RIGHT','SHIFT arrows inside sqrt/unsupported structures UNKNOWN: E-12 covers Mixed/Integral/Sum/Product only.'),
    (98,'SIN 3 CLOSE CLOSE','Close on an already-closed ordinary call UNKNOWN: no target capture pins the second close.'),
    (100,'9 POWER 3 RIGHT PAGE_UP SHIFT DEL SHIFT ROOT','INS followed by a shifted Root (nth-root) UNKNOWN: only unshifted Root consumption is reference-covered.'),
]:
    c=case(n,keys)
    c['confidence']='UNKNOWN'
    check(c,None,pending=reason)
source(case(99,'CATALOG OK DOWN OK CATALOG OK DOWN DOWN OK 1 DOWN SHIFT RIGHT'),'integral(sum(1,,),,)',cursor=17)

# P7 probes: no coordinates may be learned from the native implementation.
for id,keys,title in [
    ('GEO-P01','FRACTION 1 2 DOWN 3','12/3 multi-width alignment'),
    ('GEO-P02','FRACTION 1 DOWN 2 3','1/23 multi-width alignment'),
    ('GEO-P03','FRACTION FRACTION 1 DOWN 2 RIGHT DOWN 3','Nested fraction alignment'),
    ('GEO-P04','SHIFT ROOT 3 DOWN X','Nth-root index geometry'),
]:
    c=case(id,keys);c['title']=title;c['confidence']='UNKNOWN'
    check(c,None,pending=title+': no authoritative raster coordinates in spec 3.6; native output is not evidence.')
    c['checks'][-1]['owner']='P7 geometry / ClassWiz reference maintainer'
    c['checks'][-1]['resolution']='Capture this exact probe on the target or approved reference kit; measure ink, fields and rules before replacing null.'

# One case per section 3.6 table row; preserve the measured fact verbatim in metadata.
geo_rows=[]
section=text.split('### 3.6 Reference raster geometry (measured)')[1].split('### 3.7')[0]
for line in section.splitlines():
    if line.startswith('| ') and not line.startswith('| Quantity'):
        geo_rows.append([v.strip() for v in line.strip('|').split('|')])
def geo(n,keys,expected,gap=None,measure=None,pending=None):
    c=case(f'GEO-{n:02}',keys);row=geo_rows[n-1]
    c['title']=row[0];c['reference']={'quantity':row[0],'outcome':row[1],'evidence':row[3]}
    c['sources'][0]['evidence']=row[3]
    return check(c,expected,gap,measure=measure,pending=pending)
geo(1,'',{'caret':{'rect':[0,0,3,13]}},'L-03')
# P6: the grid-exact bake gives the reference 8x12 digit, so the bbox check's
# remaining difference is the screen origin (x3,y2 vs x2,y0) -- L-02, which
# P7 owns. GEO-02-2 holds the row's size fact (8 wide x 12 rows) on its own.
c=geo(2,'9',{'bbox':[2,0,8,12]},'L-02',{'region':[0,0,11,30],'maskCaret':True})
check(c,{'width':8,'height':12},measure={'region':[0,0,11,30],'maskCaret':True})
geo(3,'9',{'caret':{'x':11}})
geo(4,'9',{'caret':{'x':11,'width':3}})
geo(5,'9 POWER 3',{'width':5,'height':9},None,{'region':[11,0,8,20],'maskCaret':True})
c=geo(6,'9 POWER 3',{'caret':{'width':3,'height':9}},'L-03')
# Advance independently: comparing two real snapshots avoids baking a native value as a reference.
checkpoint(c,'2',{'value':6})
c['checks'][-1]['delta']={'capture':'final','path':['caret','x']}
geo(7,'9 POWER 3',{'bbox':[2,4,8,12]},'L-11',{'region':[0,0,11,30],'maskCaret':True})
geo(8,'FRACTION 1',{'bbox':[3,17,10,13]},'L-04',{'region':[0,17,16,20],'maskCaret':True})
# P6 fix (L1): the script placeholder ink is grid-exact 5x9 (ssty outline), so
# the 6-vs-5 width is not glyph scale (L-01). With caretOff the extra column can
# only be caret overlap accounting or layout/measurement, owned by P7 (L-03).
geo(9,'9 POWER',{'width':6,'height':9},'L-03',{'region':[11,0,12,20],'caretOff':True})
geo(10,'FRACTION 1',{'bbox':[1,14,14,1]},'L-04',{'region':[0,14,18,1]})
geo(11,'FRACTION 1',{'blankAboveRule':2,'blankBelowRule':2},'L-04',{'region':[0,0,18,40],'fractionGaps':True,'maskCaret':True})
geo(12,'FRACTION 1 RIGHT',{'caret':{'width':3,'height':30}},'L-03')
geo(13,'9 POWER 3 RIGHT',{'caret':{'width':3,'height':17}},'L-03')
geo(14,'1 DIVIDE 3 EXE',{'bbox':[178,46,14,1]},'L-04',{'region':[170,46,22,1]})
geo(15,'1 DIVIDE 3 SHIFT EXE',{'result':{'text':'0.3333333333'}},'R-01')
geo(16,'9 POWER 3',{'hasGrey':True},'L-05',{'region':[0,0,11,30],'maskCaret':True})
geo(17,'SETTINGS',{'bbox':[0,0,188,15]},'M-01',{'region':[0,0,189,16]})
c=geo(18,'1 DIVIDE 3 EXE SHIFT FORMAT',{'menu':{'visibleRows':4}})
check(c,None,pending='M-03: supplied Settings BMP is emulator/Paint evidence, not physical. Display 3.009 matches its text, but physical font acceptance remains blocked.')
geo(19,'1 DIVIDE 3 EXE SHIFT FORMAT',{'hasGrey':True},'M-02',{'region':[188,0,4,63]})
geo(20,'VARIABLE',{'bbox':[0,0,91,11]},'M-04',{'region':[0,0,96,14]})
geo(21,' '.join(['9']*20),{'bbox':[2,3,8,7]},'L-08',{'region':[0,0,10,14]})
geo(22,'1 DIVIDE 0 EXE',{'bbox':[0,48,188,15]},'M-06',{'region':[0,48,188,15]})
c=geo(23,'HOME',{'physicalHomeMatch':True})
c['scope']={'reference_model':'fx-880BTG PLUS','target_applicability':'CONFIRMED'}
c['sources']=[{'kind':'880-hw','evidence':'test/calculator_references/home-1.bin'}]

# P7 moves only checks supported by unchanged reference expectations.
for c in cases:
    for q in c['checks']:
        if isinstance(q['native'],dict) and q['native']['gap'] in ('L-02','L-03','L-04','L-11'):
            q['native']='pass'

# P8 closes only the supported grey/arrow observations. L-10 alternate
# shapes remain UNKNOWN; logical kind propagation does not settle that gap.
# Scoped to explicit check IDs so future cases reusing L-05/L-08 record a
# known gap instead of auto-passing before implementation.
P8_CLOSED_IDS={'C01-3','GEO-16-1','C66-2','C66-4','GEO-21-1'}
for c in cases:
    for q in c['checks']:
        if isinstance(q['native'],dict) and q['id'] in P8_CLOSED_IDS and q['native']['gap'] in ('L-05','L-08'):
            q['native']='pass'

# P9 closes only the captured menu behavior/geometry checks. M-03's physical
# font gate and VARIABLE's separate 9-row font difference remain unresolved.
P9_CLOSED_IDS={'C50-3','C60-1','C60-2','C60-3','C60-4','C60-5','C61-2',
               'C62-1','C62-2','GEO-17-1','GEO-19-1','GEO-20-1'}
for c in cases:
    for q in c['checks']:
        if isinstance(q['native'],dict) and q['id'] in P9_CLOSED_IDS and q['native']['gap'] in ('A-04','A-08','M-01','M-02','M-04'):
            q['native']='pass'

# P10 scope only: confirmed main-entry error returns, dialog band and icons.
# APP-ERR labels in the spec are aliases of these same C58/C59 sequences.
for c in cases:
    if c['id'] in ('C58','C59'):
        c['aliases']=['APP-ERR-01','APP-ERR-02'] if c['id']=='C58' else ['APP-ERR-03']
    if c['id'] in ('C58','C59','GEO-22'):
        for q in c['checks']:
            if isinstance(q['native'],dict) and q['native']['gap'] in ('A-05','E-14','M-06','I-02'):
                q['native']='pass'

# P11 scope only: history with answers (A-01), post-AC browsing (A-02),
# post-AC recall (A-03), history icons incl. sole-entry replay (I-01), and
# successful-result BACK-at-end (A-13/C48). A-07 long-result scrolling has no
# reference checks; it is covered by host controller/render tests, with exact
# target arrow geometry pending physical capture. Scoped to explicit check IDs
# so future cases reusing these gaps record a known gap instead of auto-passing.
P11_CLOSED_IDS={'C48-1','C53-1','C53-3','C53-4','C54-1','C54-2','C55-1','C57-1','C57-2'}
for c in cases:
    for q in c['checks']:
        if isinstance(q['native'],dict) and q['id'] in P11_CLOSED_IDS and q['native']['gap'] in ('A-01','A-02','A-03','I-01','A-13'):
            q['native']='pass'

# P12 scope only: SHIFT+EXE decimal presentation (R-01) and the FORMAT menu
# with FORMAT Key=Menu pinned in C50's initial settings (R-05). R-02/R-03/R-04/
# R-06 have no reference checks (host presentation/render tests own them);
# E-15 Sci Notat stays unresolved. Scoped to explicit IDs so future reuses of
# these gaps still record a known gap instead of auto-passing.
P12_CLOSED_IDS={'C49-1','GEO-15-1','C50-1'}
for c in cases:
    for q in c['checks']:
        if isinstance(q['native'],dict) and q['id'] in P12_CLOSED_IDS and q['native']['gap'] in ('R-01','R-05'):
            q['native']='pass'

out=ROOT/'test/reference_cases';out.mkdir(exist_ok=True)
for c in cases:
    gaps=sorted({q['native']['gap'] for q in c['checks'] if isinstance(q['native'],dict)})
    c['gap_ids']=gaps
    c['native']={'gap':gaps[0]} if gaps else 'pending' if all(q['native']=='pending' for q in c['checks']) else 'pass'
for name,predicate in [('editor',lambda c:c['id'].startswith('C') and int(c['id'][1:])<=43),
                       ('app',lambda c:c['id'].startswith('C') and int(c['id'][1:])>43),
                       ('geometry',lambda c:c['id'].startswith('GEO'))]:
    (out/(name+'.json')).write_text(json.dumps({'schema':1,'spec_sha256':hashlib.sha256(SPEC.read_bytes()).hexdigest(),
                                              'cases':[c for c in cases if predicate(c)]},indent=2,ensure_ascii=False)+'\n',encoding='utf-8')

# Every specification gap stays listed, even if outside P1's executable evidence.
ledger={}
for line in text.split('\n## 4.')[1].split('\n## 5.')[0].splitlines():
    match=re.match(r'\| ([ELRAMI]-\d\d) \| (.*?) \|',line)
    if match:
        ledger[match[1]]={'reference':match[2],'checks':[]}
ledger['A-13']={'reference':'C48: BACK on a successful result edits at the expression end; 1 EXE BACK 2 must give 12.',
                'discovered':'P1 full-controller replay: native gives 21 because result.position is used for successful results.',
                'phase':'P11', 'checks':[]}
for c in cases:
    for q in c['checks']:
        if isinstance(q['native'],dict):ledger[q['native']['gap']]['checks'].append(q['id'])
(out/'known_gaps.json').write_text(json.dumps(ledger,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(f'Transcribed {len(cases)} cases and {sum(len(c["checks"]) for c in cases)} independent checks.')
