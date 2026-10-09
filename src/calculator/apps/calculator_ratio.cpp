#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/apps/calculator_ratio.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/ui/calculator_render.h"
namespace Calculator { namespace RatioApp {
namespace {
using K=ExpressionInput::Key;
enum class Screen : uint8_t { TypeSelect, Coefficients, Result, Error };
struct State {
  Screen screen=Screen::TypeSelect, errorOrigin=Screen::Coefficients;
  int type=0,selection=0;
  std::array<Value,3> coefficients{{Value(0),Value(0),Value(0)}};
  GridCursor cursor;
  CellEdit edit;
  InputTarget target;
  uint32_t revision=0,epoch=0;
  ResultSummary result;
  Error error=Error::None;
  bool shared=false,pending=false;
  MathEditor label;
  Viewport viewport;
  int16_t scroll=0;
} state;
CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }
std::vector<MenuItem> menu(){return {{0,"A:B=X:D",false},{1,"A:B=C:X",false}};}
void invalidate(){jobs().cancel();state.pending=false;++state.revision;}
void reset(){invalidate();for(auto& c:state.coefficients)c=Value(0);state.edit.abort();state.cursor={};}
void error(Error e){state.errorOrigin=state.screen;state.screen=Screen::Error;state.error=e;}
void submit(int destination,const std::string& expression){
  Job& job=prepareJob(AppId::Ratio,destination,expression);job.ownerRevision=state.revision;
  if(destination==0){
    job.request.memory.variables[0]=state.coefficients[0];
    job.request.memory.variables[1]=state.coefficients[1];
    job.request.memory.variables[2]=state.coefficients[2];
  }
  state.pending=jobs().start(job)==JobService::Start::Started;
  if(!state.pending)error(Error::Stack);
}
void sync(){if(state.epoch!=session().ratioEpoch){reset();state.epoch=session().ratioEpoch;state.screen=Screen::TypeSelect;state.shared=false;}}
}
void enter(bool relaunch){sync();if(relaunch){reset();state.screen=Screen::TypeSelect;}state.shared=false;}
void pause(bool home){
  invalidate();state.edit.abort();state.shared=false;
  if(home){reset();if(state.screen==Screen::Result||state.screen==Screen::Error)state.screen=Screen::Coefficients;}
}
void press(K key){
  sync();
  if(state.shared){CalculateApp::press(key);state.shared=CalculateApp::sharedActive();return;}
  if(state.screen==Screen::Error){
    if(key==K::Return||key==K::Clear||key==K::Okay){state.screen=state.errorOrigin;state.error=Error::None;}
    return;
  }
  if(jobs().running()){
    // A key can invalidate ownership before the cancelled worker has drained.
    // The user can navigate/edit immediately; submission waits for that drain.
    invalidate();
    if(key==K::Interrupted){state.edit.abort();return;}
  }
  if(key==K::Interrupted){invalidate();state.edit.abort();return;}
  if(state.screen==Screen::TypeSelect){
    menuShortcut(key,state.selection,menu().size());
    if(key==K::Up||key==K::Down)state.selection=1-state.selection;
    if(key==K::Okay||key==K::Execute){reset();state.type=state.selection;state.screen=Screen::Coefficients;}
    return;
  }
  if(state.screen==Screen::Result){
    if(key==K::Variable){CalculateApp::openShared(AppId::Ratio,key,&state.result);state.shared=true;return;}
    // 880-MAN ratio p.21: 0062 BACK, 0061 AC, 004F OK return to the
    // coefficient editor. EXE is not listed there (0045 solves from the
    // editor per the second worked example), so it does nothing here.
    if(key==K::Return||key==K::Clear||key==K::Okay){state.screen=Screen::Coefficients;return;}
    return;
  }
  if(state.edit.active){
    if(QuickOverlay::open(state.edit.editor,key,AppId::Ratio))return;
    if(key==K::Return){state.edit.abort();return;}
    if(key==K::Execute||key==K::Okay){
      if(!jobs().running()&&!state.edit.editor.text().empty())submit(1,state.edit.editor.text());
    }else state.edit.editor.press(key);
    return;
  }
  if(key==K::Return){reset();state.screen=Screen::TypeSelect;state.selection=state.type;return;}
  if(key==K::Clear){reset();return;}
  if(moveGrid(state.cursor,{1,3,4,3},key))return;
  if(key==K::Execute||key==K::Okay){
    for(const auto& c:state.coefficients)if(c.number.zero()){error(Error::Math);return;}
    if(!jobs().running())submit(0,state.type?"B*C/A":"A*C/B");
    return;
  }
  if((key>=K::Digit0&&key<=K::Digit9)||key==K::Dot||key==K::Subtract||key==K::Fraction||key==K::Root||key==K::Open||key==K::Ans||key==K::Shift){
    invalidate();state.target={MenuOrigin::Cell,0,static_cast<int16_t>(state.cursor.col),state.revision};
    state.edit.begin({0,state.cursor.col},state.revision);state.edit.editor.press(key);
  }
}
void adopt(const Job& job,Result& output){
  if(state.shared){CalculateApp::adopt(job,output);return;}
  if(!state.pending||session().app!=AppId::Ratio||state.screen!=Screen::Coefficients||job.ownerRevision!=state.revision||state.epoch!=session().ratioEpoch)return;
  state.pending=false;
  if(output.error!=Error::None){error(output.error);return;}
  if(job.destination==1){
    if(!state.edit.active||state.edit.stale(state.revision)||state.target.revision!=state.revision||job.request.expression!=state.edit.editor.text())return;
    state.coefficients[state.target.col]=output.value;++state.revision;state.edit.abort();
    if(state.cursor.col<2)++state.cursor.col;
  }else{
    state.result=toSummary(output);state.screen=Screen::Result;state.label.begin("X=");state.viewport={};state.scroll=0;
  }
}
bool caretVisible(){return state.shared?CalculateApp::caretVisible():state.edit.active&&!jobs().running();}
std::vector<uint8_t> render(bool caretOn){
  if(state.shared)return CalculateApp::render(caretOn);
  if(state.screen==Screen::Error)return renderErrorDialog(state.error);
  if(jobs().running())return Busy::renderBusy();
  if(state.screen==Screen::TypeSelect)return renderMenuList(menu(),state.selection,0);
  if(state.screen==Screen::Result)return renderResultSummary(state.label,session().settings,&state.result,Format::Standard,false,false,state.viewport,"",false,&state.scroll);
  RatioView v;v.unknownLast=state.type!=0;v.selected=state.cursor.col;
  // Decimal cells like every native grid: the cell face has no fraction/radical.
  for(size_t i=0;i<3;++i)v.cells[i]=formatValue(state.coefficients[i],session().settings,Format::Decimal);
  v.draft=state.edit.active;
  v.footer=state.edit.active?state.edit.editor.text():v.cells[static_cast<size_t>(state.cursor.col)];
  return renderRatio(v);
}
PanelIcons::State icons(){
  if(state.shared)return CalculateApp::icons();
  IconView v;if(state.screen==Screen::Coefficients||state.screen==Screen::Result){v.calculation=true;v.io=session().settings.io;v.angle=session().settings.angle;v.number=session().settings.number;v.engineer=session().settings.engineer;v.shifted=state.edit.active&&state.edit.editor.shifted;}return desiredIcons(v);
}
size_t stateBytes(){return sizeof(State);}
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot(){
  if(state.shared)return CalculateApp::snapshot();
  CalculatorApp::Snapshot out;const char* names[]={"RatioType","RatioCoefficients","RatioResult","ErrorDialog"};
  out.screen=names[static_cast<unsigned>(state.screen)];out.selection=state.selection;out.gridCol=state.cursor.col;out.rows=1;out.cols=3;
  out.editing=state.edit.active;out.text=state.edit.active?state.edit.editor.text():"";
  if(state.screen==Screen::TypeSelect)for(const auto& r:menu())out.menuLabels.push_back(r.label);
  for(const auto& c:state.coefficients)out.cells.push_back(formatValue(c,session().settings));
  out.hasResult=state.screen==Screen::Result;if(out.hasResult)out.resultText=summaryText(state.result,session().settings,Format::Standard);
  if(state.screen==Screen::Error){out.errorTitle=errorText(state.error);out.errorOrigin="RatioCoefficients";}
  return out;
}
#endif
} }
