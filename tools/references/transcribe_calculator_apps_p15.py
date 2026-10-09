"""Reviewed manual-derived P15 cases. Never learns expectations from native output."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'test/reference_cases'
MAN = '../../../991CN-X-CW-Decompilation/research/downloads/manual-880/'
CHAPTERS = dict(Complex='complex_number', Matrix='matrix', Vector='vector', Equation='equation', Inequality='inequality')
PREFIX = {'Complex':'CX', 'Base-N':'BN', 'Matrix':'MAT', 'Vector':'VCT', 'Equation':'EQ', 'Inequality':'IN'}
APP_IDS = {'Complex':7,'Base-N':8,'Matrix':9,'Vector':10,'Equation':5,'Inequality':6}

def source(app):
    if app == 'Base-N':
        return 'https://support.casio.com/global/vi/calc/manual/fx-880BTGPLUS_vi/using_calculator_apps/base-n_calculations.html'
    return MAN + 'using_calculator_apps__' + CHAPTERS[app] + '_calculations.html.txt'

def build(app):
    cases=[]
    def add(surface, keys, expected=None, section='', reason='', evidence='880-MAN', settings=None):
        ident=f'P15-{PREFIX[app]}-{len(cases)+1:02}'
        q={'id':ident+'-1','capture':'final','expect':expected,'native':'pass' if expected is not None else 'pending',
           'evidence':evidence if expected is not None else 'PENDING','confidence':'STRONG' if expected is not None else 'UNKNOWN','owner':'P15' if expected is not None else 'P15/P20 physical'}
        if expected is None:
            q.update(reason=reason or 'Target chapter does not establish exact behavior/raster for this surface.',
                     resolution='Capture this action on fx-880BTG PLUS with settings and icon row; review independently before replacing null.')
        cases.append({'id':ident,'title':app+' '+surface,'app':app,'surface':surface,
          'scope':{'reference_model':'fx-880BTG PLUS','target_applicability':'STRONG'},
          'sources':[{'kind':evidence,'evidence':source(app),'section':section or surface}],
          'initial':{'app':app,'settings':{'inputOutput':'MathMath','angle':'Degree','number':'Norm1'},'memory':'cleared','history':[]},
          'keys':keys+[{'capture':'final'}],'checks':[q],'confidence':q['confidence'],
          'native':q['native'],'gap_ids':[],'owner':q['owner'],
          'notes':'Manual logical contract only; native field names encode state, not target RAM. Exact raster remains separate and pending.'})
        if settings: cases[-1]['initial']['settings'].update(settings)
        if app=='Complex' and (surface.startswith('icons') or surface=='FORMAT'):
            cases[-1]['sources'].append({'kind':'880-MAN','evidence':MAN+'before_using_the_calculator__getting_started.html.txt','section':'Indicators: i / angle reflect Complex Result setting'})
        if surface.startswith('FORMAT') and app=='Complex':
            cases[-1]['sources'].append({'kind':'880-MAN','evidence':MAN+'changing_calculation_result_format__changing_the_display_format_calc_results.html.txt','section':'FORMAT order and result-dependent applicability'})
        if app=='Complex' and surface in ('input','result','FORMAT','icons entry','icons rectangular'):
            cases[-1]['sources'].append({'kind':'EMU-CW','evidence':'p15-evidence.json','section':'cw.reviewed_facts; supplementary shared behavior only'})
            cases[-1]['scope'].update(rom_sha256='4ab4791609cc8686fc6b4d1be34b3e6d19b3eb700abe8c9e3edd91ff2813471f',emulator='CasioEmuX c887be7c956ddd970abb82e7a40a186c9c4e4289')
    initial={'Complex':'Entry','Base-N':'Entry','Matrix':'Entry','Vector':'Entry','Equation':'Mode','Inequality':'Size'}[app]
    add('launch',[],{'screen':{'id':initial},'app':{'id':APP_IDS[app]}},'launch instructions')
    if app=='Complex':
        value=['2','ADD','3','SHIFT','9']
        add('input',value,{'logical':{'source':'2+3i'}},'example 1')
        add('result',value+['EXE'],{'result':{'text':'2+3i','hasResult':True}},'example 1; rectangular identity')
        add('FORMAT',value+['EXE','SHIFT','FORMAT'],{'screen':{'id':'Format'},'menu':{'labels':['Standard','Decimal','Rectangular Coord','Polar Coord']},'icons':{'allOff':True}},'FORMAT chapter global order filtered to complex capabilities')
        add('FORMAT AC',value+['EXE','SHIFT','FORMAT','AC'],{'screen':{'id':'Result'},'result':{'text':'2+3i'}},'getting_started: close user-opened menus')
        add('icons entry',[],{'icons':{'i':True,'angle':False}},'getting_started: i / angle indicate Complex Result setting')
        add('icons rectangular',value+['EXE'],{'icons':{'i':True,'angle':False}},'getting_started: Complex Result indicator')
        add('icons local polar view',value+['EXE','SHIFT','FORMAT','DOWN','DOWN','DOWN','EXE'],{'screen':{'id':'Result'},'icons':{'i':True,'angle':False}},'FORMAT example 9 changes view; getting_started indicator tracks setting')
        add('error',['1','DIVIDE','0','EXE'],{'screen':{'id':'ErrorDialog'},'error':{'title':'Math ERROR'},'icons':{'allOff':True}},'technical_information errors; shared P10 modal contract')
        add('menu',['CATALOG'],{'screen':{'id':'Catalog'},'icons':{'allOff':True}},'CATALOG and P9 menu contract')
        add('retention',value+['EXE','HOME','OK'],None,reason='Exact draft/result retention after Home needs target replay; shared-memory reducer is tested separately.')
        add('imaginary glyph',value,None,reason='No physical fx-880 imaginary-unit glyph capture. Keep converted native glyph; do not copy ROM pixels.')
        add('icons polar setting',[],{'icons':{'i':False,'angle':True}},'getting_started indicators',settings={'complexResult':'Polar'})
        add('polar result',['1','ADD','SHIFT','9','EXE'],{'screen':{'id':'Result'},'icons':{'i':False,'angle':True}},'Complex Result r/theta; angle Degree',settings={'complexResult':'Polar'})
    elif app=='Base-N':
        add('initial base',[],{'app':{'base':10}},'initial default Decimal')
        add('input',['1','5','MULTIPLY','3','7'],{'logical':{'source':'15*37'}},'example 4')
        add('result',['1','5','MULTIPLY','3','7','EXE'],{'result':{'text':'555'}},'example 4')
        add('FORMAT',['1','5','MULTIPLY','3','7','EXE','FORMAT'],{'app':{'base':16},'result':{'text':'0000022B'}},'example 4 decimal to hexadecimal')
        add('hex digits',['FORMAT','1','SHIFT','3','ADD','1','EXE'],{'result':{'text':'00000020'}},'example 2')
        add('binary',['FORMAT','FORMAT','1','1','ADD','1','EXE'],{'app':{'base':2},'result':{'text':'00000000000000000000000000000100'}},'example 1')
        add('base cycle',['FORMAT']*4,{'app':{'base':10},'screen':{'id':'Entry'}},'Decimal Hexadecimal Binary Octal cycle')
        add('invalid digit',['FORMAT','FORMAT','9'],None,reason='Manual gives base ranges, not whether invalid digit key is ignored immediately or rejected on EXE.')
        add('error',list('2147483647')+['ADD','1','EXE'],{'error':{'title':'Math ERROR'},'screen':{'id':'ErrorDialog'}},'32-bit overflow note')
        add('menu',['CATALOG'],{'menu':{'labelSet':['Base Prefix','Hex Value','Logic Operation']}},'three named Base-N categories; order deliberately not asserted',evidence='880-MAN')
        add('retention',['FORMAT','HOME','LEFT','OK','HOME','RIGHT','OK'],{'app':{'base':10}},'session transition manual-backed policy: other app resets base')
        add('icons',[],None)
        add('BACK AC',['1','AC'],{'logical':{'source':''}},'basic clear operation')
    elif app in ('Matrix','Vector'):
        vector=app=='Vector'; name='Vct' if vector else 'Mat'
        setup=['TOOLS','OK']+['DOWN']*(1 if vector else 2)+['OK']
        values=[1,2] if vector else [2,1,1,1]
        data=sum(([str(v),'EXE'] for v in values),[])
        # Shared catalog has seven ordinary groups preceding the aggregate group.
        recall=['CATALOG']+['DOWN']*7+['OK','OK']
        result=setup+data+['BACK']+recall+['EXE']
        add('menu',['TOOLS'],{'screen':{'id':'List'},'menu':{'labels':[name+x+': None' for x in 'ABCD']}},'variable list, four named variables')
        add('dimension choice',['TOOLS','OK','OK'],{'screen':{'id':'Dimension'},'menu':{'labels':['2 Dimensions','3 Dimensions'] if vector else ['1 Row','2 Rows','3 Rows','4 Rows']}},'example 2 dimension submenu')
        add('grid',setup,{'screen':{'id':'Editor'},'grid':{'rows':2,'cols':1 if vector else 2}},'example 1 default size')
        add('input',setup+data,{'grid':{'cells':[str(v) for v in values],'editing':False}},'example 1 data entry')
        add('grid boundaries',setup+['LEFT','UP'],None,reason='Shared cursor clamp is regression-tested; target boundary wrap/clamp is not captured.')
        add('result',result,{'screen':{'id':'Answer'},'grid':{'cells':[str(v) for v in values]}},'answer memory: recall named aggregate')
        add('answer TOOLS',result+['TOOLS'],{'menu':{'labels':['Define '+app,'Store']}},'display variable list from editor OR answer: TOOLS Define')
        add('BACK',result+['BACK'],{'screen':{'id':'Entry'},'logical':{'source':name+'A'}},'answer BACK returns completed expression')
        add('AC',result+['AC'],{'screen':{'id':'Entry'},'logical':{'source':''}},'answer AC clears expression')
        add('FORMAT',result+['FORMAT'],None,reason='Answer-grid FORMAT/no-op not explicitly established by available target chapter.')
        add('error',setup+['1','DIVIDE','0','EXE'],None,reason='Cell Math ERROR delivery and exact footer/modal need target capture; native error regression separately exercised.')
        add('retention',setup+data+['BACK','HOME','RIGHT','OK','HOME','LEFT','OK','TOOLS'],{'menu':{'labels':[name+'A: '+('2' if vector else '2x2')]+[name+x+': None' for x in 'BCD']}},'named aggregates survive other-app launch')
        add('icons',setup,None)
    else:
        inequality=app=='Inequality'
        setup=['OK','DOWN','DOWN','OK'] if inequality else ['DOWN','OK','OK']
        coeff=['1','EXE','2','EXE','SUBTRACT','3','EXE']
        result=setup+coeff+['EXE']
        add('menu',[],None,reason='Semantic degree/type choices known; exact formula rows/raster not yet transcribed. Do not substitute CN labels.')
        add('input',setup+coeff,{'screen':{'id':'Coefficients'},'grid':{'cells':['1','2','-3']}},'coefficient-entry procedure; x^2+2x-3')
        add('grid navigation',setup+['RIGHT'],{'grid':{'col':1}},'coefficient selection')
        add('result',result,{'screen':{'id':'Solutions'},'result':{'hasResult':True}},'solve coefficients')
        add('solution presentation',result,None,reason='Interval/solution mathematical raster and exact radical typography require target image review; do not bless native strings.')
        if inequality:
            add('no solution',['OK','DOWN','OK','1','EXE','0','EXE','0','EXE','EXE'],{'result':{'text':'No Solution'}},'x^2 < 0 note')
            add('all real',['OK','DOWN','DOWN','OK','1','EXE','0','EXE','0','EXE','EXE'],{'result':{'text':'All Real Numbers'}},'x^2 >= 0 note')
            add('BACK',result+['BACK'],{'screen':{'id':'Coefficients'}},'solution BACK returns coefficient editor')
        else:
            add('next solution',result+['DOWN'],{'result':{'page':1},'icons':{'UP':True}},'polynomial solution pages')
            add('BACK previous',result+['DOWN','BACK'],{'screen':{'id':'Solutions'},'result':{'page':0}},'polynomial example 2: BACK while previous indicator is present')
        add('AC',result+['AC'],{'screen':{'id':'Coefficients'}},'AC returns coefficient editor')
        add('FORMAT',result+['FORMAT'],None,reason='Legacy solution FORMAT applicability needs target verification; retain no-op until evidenced.')
        add('error',setup+['1','DIVIDE','0','EXE'],None,reason='Coefficient error modal/notice ownership needs target capture; source regression retained.')
        add('retention',setup+coeff+['HOME','OK'],None,reason='Coefficient draft retention on Home versus other app is provisional O-4.')
        add('icons',result,None)
        if not inequality:
            complex_result=setup+['1','EXE','0','EXE','1','EXE','EXE']
            add('Complex Roots menu',complex_result+['TOOLS','OK'],{'screen':{'id':'ComplexRoots'},'menu':{'labels':['On','Off']},'icons':{'allOff':True}},'Displaying complex roots: TOOLS Complex Roots On/Off')
            add('Complex Roots Off',complex_result+['TOOLS','OK','DOWN','OK','AC'],{'screen':{'id':'Solutions'},'result':{'text':'No Real Roots'}},'Complex Roots Off: no real roots message for complex roots')
            add('Complex Roots On restore',complex_result+['TOOLS','OK','DOWN','OK','AC','TOOLS','OK','UP','OK','AC'],{'screen':{'id':'Solutions'},'result':{'hasResult':True}},'Complex Roots On: return to existing solution; exact root order is not asserted')
    add('stale job',[],None,reason='Async generation/revision behavior is a native architecture safety contract, not externally visible target internals. Executed by P15 controller checks.',evidence='SOURCE-ONLY')
    add('render reference',[],None,reason='No approved physical app raster; existing render fixtures are regression evidence only.')
    return {'schema':1,'phase':'P15','cases':cases}

if __name__=='__main__':
    ledger=['# P15 reference ledger','', 'Generated from the reviewed transcription. A pass covers only its explicit logical assertion; exact raster is separate.', '', '| Case | Surface | Evidence | Old | New | Status | Owner |', '|---|---|---|---|---|---|---|']
    corrected={'P15-MAT-03','P15-MAT-08','P15-VCT-03','P15-VCT-08','P15-EQ-08'}
    for app in APP_IDS:
        slug={'Base-N':'basen'}.get(app,app.lower())
        document=build(app)
        (ROOT/f'apps-{slug}.json').write_text(json.dumps(document,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        for c in document['cases']:
            q=c['checks'][0];pending=q['native']=='pending'
            old='PROVEN GAP (cold replay)' if c['id'] in corrected else 'PROVEN GAP (source/manual)' if c['surface'].startswith('Complex Roots') else 'unmeasured' if pending else 'matches logical rule'
            new='null; capture plan recorded' if pending else 'matches logical rule'
            evidence='/'.join(dict.fromkeys(s['kind'] for s in c['sources']))
            ledger.append(f"| {c['id']} | {c['surface']} | {evidence} | {old} | {new} | {q['native']} | {q['owner']} |")
    report = ROOT.parents[1] / 'tools/out/references/p15-ledger.md'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text('\n'.join(ledger)+'\n', encoding='utf-8')
