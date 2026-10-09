#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/apps/calculator_table.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/ui/calculator_render.h"
#include <algorithm>
namespace Calculator {
size_t tableCapacity(uint8_t type){return type==0?30:45;}
bool composedFunction(const std::string& source){
  const auto parsed=parse(source);
  std::function<bool(const Expression&)> visit=[&](const Expression& n){
    if(!n)return false;if(n->kind==Node::Kind::Call&&(n->text=="f"||n->text=="g"))return true;
    for(const auto& c:n->children)if(visit(c))return true;return false;
  };return visit(parsed.root);
}
Result evaluateTable(const TableTask& task,const Request& request,const std::function<bool()>& cancelled,const std::function<void()>& cooperate){
  Result output;auto table=std::make_shared<TableData>();
  const size_t cap=tableCapacity(task.type);
  if(task.operation==TableOperation::Generate){
    const auto& a=task.range[0];const auto& b=task.range[1];const auto& step=task.range[2];
    // 880-MAN (technical_information__errors, Range ERROR) names the error
    // only for a table exceeding the 30/45-row maximum. Every other rejection
    // here is a safe bound whose exact target label is PENDING: the manual
    // gives Step/Start/End no error name and defines no descending range, so
    // Start>End is rejected rather than generated descending.
    if(!a.number.finite()||!b.number.finite()||!step.number.finite()||step.number<=0||a.number>b.number){output.error=Error::Range;return output;}
    const Decimal count=((b.number-a.number)/step.number).floor()+Decimal(1);int64_t n=0;
    if(count>Decimal(static_cast<int>(cap))){output.error=Error::Range;return output;} // target-confirmed over-capacity
    if(!count.toInt(n)||n<1){output.error=Error::Range;return output;} // defensive conversion guard, never target-observed
    table->rows.resize(static_cast<size_t>(n));
    for(int64_t i=0;i<n;++i){auto& row=table->rows[static_cast<size_t>(i)];row.x=realCombine(a,realCombine(Value(static_cast<int>(i)),step,'*'),'+');row.hasX=true;}
  }else if(task.input){
    // Defensive: controller-bounded input can never exceed capacity, so this
    // is an internal safe bound, never a target-observed Range ERROR.
    if(task.input->rows.size()>cap){output.error=Error::Range;return output;}
    table->rows=task.input->rows;
  }
  Request local=request; // worker stack only; global x is untouched
  if(task.operation==TableOperation::EditX||task.operation==TableOperation::Verify){
    // Internal impossible state: the grid cursor is clamped by policy, so an
    // out-of-range row cannot come from the UI. Safe bound only.
    if(task.row<0||static_cast<size_t>(task.row)>table->rows.size()||static_cast<size_t>(task.row)>=cap){output.error=Error::Range;return output;}
    if(static_cast<size_t>(task.row)==table->rows.size()){table->rows.reserve(table->rows.size()+1);table->rows.emplace_back();}
    auto& row=table->rows[task.row];
    if(task.operation==TableOperation::Verify){
      if(!row.hasX){output.error=Error::NotDefined;return output;}
      local.memory.variables[6]=row.x;
      const std::string expected=request.expression;
      local.expression=(task.col==1?"f(x)":"g(x)")+std::string("=(")+expected+")";local.verify=true;
      output=evaluate(local,cancelled,cooperate);
      if(output.error!=Error::None)return output;
      local.expression=expected;local.verify=false;const auto entered=evaluate(local,cancelled,cooperate);
      if(entered.error!=Error::None){output.error=entered.error;return output;}
      if(task.col==1){row.f=entered.value;row.hasF=output.truth;row.fError=Error::None;}
      else{row.g=entered.value;row.hasG=output.truth;row.gError=Error::None;}
      output.table=table;return output;
    }
    const auto value=evaluate(local,cancelled,cooperate);
    if(value.error!=Error::None){output.error=value.error;output.position=value.position;return output;}
    row.x=value.value;row.hasX=true;row.hasF=row.hasG=false;row.fError=row.gError=Error::None;
  }
  for(size_t i=0;i<table->rows.size();++i){
    if(cancelled&&cancelled()){output.error=Error::Cancelled;return output;}
    if(task.operation==TableOperation::EditX&&i!=static_cast<size_t>(task.row))continue;
    auto& row=table->rows[i];if(!row.hasX)continue;
    local.memory=request.memory;local.memory.variables[6]=row.x;local.verify=false;
    for(int col=1;col<=2;++col){
      if((col==1&&task.type==2)||(col==2&&task.type==1)||!(task.columns&(1<<(col-1))))continue;
      bool& present=col==1?row.hasF:row.hasG;Error& error=col==1?row.fError:row.gError;Value& v=col==1?row.f:row.g;
      present=false;error=Error::None;
      if(task.verify)continue;
      if((col==1?request.memory.f:request.memory.g).empty())continue;
      local.expression=col==1?"f(x)":"g(x)";
      auto evaluated=evaluate(local,cancelled,cooperate);
      if(evaluated.error==Error::Cancelled){output.error=Error::Cancelled;return output;}
      error=evaluated.error;present=error==Error::None;if(present)v=evaluated.value;
    }
    if(cooperate)cooperate();
  }
  output.table=table;return output;
}
namespace TableApp {
namespace {
using K=ExpressionInput::Key;
enum class Screen : uint8_t { Grid,Tools,Type,Range,Define,Edit,Confirm,Error,VerifyResult };
struct State {
  Screen screen=Screen::Grid,errorOrigin=Screen::Grid;
  std::shared_ptr<const TableData> table;
  GridCursor cursor;
  CellEdit edit;
  InputTarget target;
  Viewport viewport;
  std::string f,g;
  uint32_t revision=0,epoch=0,seenRevision=0;
  int selection=0,first=0;
  uint8_t refresh=0;
  bool shared=false,pending=false,stale=false,truth=false,active=false;
  Error error=Error::None;
} state;
CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }
size_t rowCount(){return state.table?state.table->rows.size():0;}
int columns(){return session().tableType==0?3:2;}
int semanticColumn(){return state.cursor.col==0?0:session().tableType==2?2:state.cursor.col;}
GridPolicy policy(){return {static_cast<int>(std::min(tableCapacity(session().tableType),rowCount()+1)),columns(),4,3};}
void invalidate(){jobs().cancel();state.pending=false;++state.revision;}
void menu(Screen s,int selected=0){state.screen=s;state.selection=selected;state.first=0;}
void showError(Error e){state.errorOrigin=state.screen;state.screen=Screen::Error;state.error=e;}
void resetRows(){invalidate();state.table.reset();state.cursor={};state.edit.abort();state.refresh=0;state.stale=false;menu(Screen::Grid);}
void syncEpoch(){if(state.epoch!=session().tableEpoch){resetRows();state.epoch=session().tableEpoch;state.f=session().memory.f;state.g=session().memory.g;}}
std::shared_ptr<TableData> copyRows(){auto p=std::make_shared<TableData>();if(state.table)p->rows=state.table->rows;return p;}
void replace(std::shared_ptr<TableData> p){invalidate();state.table=std::move(p);state.edit.abort();clampGrid(state.cursor,policy());}
std::vector<MenuItem> rows(){
  switch(state.screen){
    // TOOLS carries the number badges every other list has (the hardware
    // photo shows none; a list without them here read as broken). No arrow on
    // Table Range, which opens a form, not a list. The native label reads
    // "Define f(x)/g(x)"; our ClassWiz15 advance (176 px) cannot fit it, so
    // menuDisplayLabel keeps the "Define f/g" alias.
    case Screen::Tools:return {{0,"Table Range",false},{1,"Define f(x)/g(x)",true},{2,"Table Type",true},{3,"Edit",true},{4,"Recalculate",false},{5,session().verify?"Verify OFF":"Verify ON",false}};
    case Screen::Type:return {{0,"f(x)/g(x)",false},{1,"f(x)",false},{2,"g(x)",false}};
    case Screen::Define:return {{0,"Define f(x)",false},{1,"Define g(x)",false}};
    case Screen::Edit:{std::vector<MenuItem> v={{0,"Insert Row",false},{1,"Delete All",false}};if(session().verify)v.push_back({2,"Delete f/g Column",false});return v;}
    case Screen::Confirm:return {{0,"No",false},{1,"Yes",false}};
    case Screen::Range:{std::vector<MenuItem> v;for(int i=0;i<3;++i)v.push_back({i,std::string(i==0?"Start: ":i==1?"End: ":"Step: ")+formatValue(session().tableRange[i],session().settings),false});v.push_back({3,"Execute",false});return v;}
    default:return {};
  }
}
void start(TableOperation op,uint8_t mask=3,const std::string& expression=""){
  if(jobs().running())return;
  auto task=std::make_shared<TableTask>();task->operation=op;task->type=session().tableType;task->range=session().tableRange;task->input=state.table;
  task->row=state.cursor.row;task->col=semanticColumn();task->columns=mask;task->verify=session().verify;
  Job& job=prepareJob(AppId::Table,0,expression);job.table=task;job.ownerRevision=state.revision;
  state.pending=jobs().start(job)==JobService::Start::Started;
  job.table.reset(); // prepareJob's static scratch must not retain a row buffer
  if(!state.pending)showError(Error::Stack);
}
void beginEdit(int row,int col,const std::string& text=""){
  invalidate();state.target={MenuOrigin::Cell,static_cast<int16_t>(row),static_cast<int16_t>(col),state.revision};state.edit.begin({row,col},state.revision,text);state.viewport={};
  state.edit.editor.setMathInput(session().settings.io<=IO::MathDecimal);
}
// Grid cells show decimals (native): the 5x7 face cannot draw 1/10, sqrt(2 or pi.
std::string cell(const TableRow& row,int col){
  if(col==0)return row.hasX?formatValue(row.x,session().settings,Format::Decimal):"";
  const Error e=col==1?row.fError:row.gError;if(e!=Error::None)return errorText(e);
  return (col==1?row.hasF:row.hasG)?formatValue(col==1?row.f:row.g,session().settings,Format::Decimal):"";
}
// Table Range is a form: its number keys start editing the selected value.
bool shortcuts(){return state.screen!=Screen::Range;}
// Table Range is the shared numeric form: fixed heading, three scrolling rows.
int visibleRows(){return state.screen==Screen::Range?kDistributionFormRows:kMenuVisibleRows;}
void clearOutputs(){auto p=copyRows();for(auto& r:p->rows){r.hasF=r.hasG=false;r.fError=r.gError=Error::None;}replace(p);}
void deleteRow(){if(static_cast<size_t>(state.cursor.row)>=rowCount())return;auto p=copyRows();p->rows.erase(p->rows.begin()+state.cursor.row);replace(p);}
}
void enter(){syncEpoch();state.active=true;state.shared=false;state.f=session().memory.f;state.g=session().memory.g;state.seenRevision=session().revision;menu(Screen::Grid);}
void pause(){invalidate();state.edit.abort();state.active=false;state.shared=false;menu(Screen::Grid);}
bool service(){
  if(session().app!=AppId::Table){if(state.table){state.table.reset();return true;}return false;}
  if(!state.active)return false;
  bool changed=state.epoch!=session().tableEpoch;syncEpoch();
  if(state.f!=session().memory.f||state.g!=session().memory.g){
    invalidate();state.stale=true;
    if(state.f!=session().memory.f&&!composedFunction(session().memory.f))state.refresh|=1;
    if(state.g!=session().memory.g&&!composedFunction(session().memory.g))state.refresh|=2;
    state.f=session().memory.f;state.g=session().memory.g;changed=true;
  }
  if(state.seenRevision!=session().revision){state.seenRevision=session().revision;state.stale=true;}
  if(state.refresh&&!jobs().running()&&!state.shared&&state.screen==Screen::Grid){
    const uint8_t mask=state.refresh;state.refresh=0;if(rowCount())start(TableOperation::Recalculate,mask);changed=true;
  }
  return changed;
}
void press(K key){
  syncEpoch();
  if(state.shared){CalculateApp::press(key);state.shared=CalculateApp::sharedActive();return;}
  if(state.screen==Screen::Error){if(key==K::Return||key==K::Clear||key==K::Okay){state.screen=state.errorOrigin;state.error=Error::None;}return;}
  if(state.screen==Screen::VerifyResult){if(key==K::Return||key==K::Clear||key==K::Okay){menu(Screen::Grid);if(state.truth){++state.cursor.row;clampGrid(state.cursor,policy());}}return;}
  if(jobs().running())invalidate();
  if(key==K::Interrupted){invalidate();state.edit.abort();return;}
  if(state.edit.active){
    if(QuickOverlay::open(state.edit.editor,key,AppId::Table))return;
    if(key==K::Return){state.edit.abort();return;}
    if(key==K::Execute||key==K::Okay){
      if(jobs().running()||state.edit.editor.text().empty())return;
      if(state.screen==Screen::Range){Job& job=prepareJob(AppId::Table,1,state.edit.editor.text());job.ownerRevision=state.revision;state.pending=jobs().start(job)==JobService::Start::Started;}
      else start(semanticColumn()==0?TableOperation::EditX:TableOperation::Verify,3,state.edit.editor.text());
    }else state.edit.editor.press(key);
    return;
  }
  if(state.screen==Screen::Grid){
    if(moveGrid(state.cursor,policy(),key))return;
    if(key==K::Tools){menu(Screen::Tools);return;}
    if(key==K::Settings||key==K::Function||key==K::Variable){
      ResultSummary summary;const ResultSummary* selected=nullptr;
      if(static_cast<size_t>(state.cursor.row)<rowCount()){
        const auto& r=state.table->rows[state.cursor.row];const int c=semanticColumn();
        if(c==0?r.hasX:c==1?r.hasF:r.hasG){summary.value=c==0?r.x:c==1?r.f:r.g;selected=&summary;}
      }
      CalculateApp::openShared(AppId::Table,key,selected);state.shared=true;return;
    }
    if(key==K::Delete){deleteRow();return;}
    const int col=semanticColumn();
    if(col==0&&state.cursor.row>0&&static_cast<size_t>(state.cursor.row-1)<rowCount()&&state.table->rows[state.cursor.row-1].hasX&&(key==K::Add||key==K::Subtract||key==K::Execute)){
      const Value value=realCombine(state.table->rows[state.cursor.row-1].x,session().tableRange[2],key==K::Subtract?'-':'+');
      beginEdit(state.cursor.row,0,value.number.str(40));start(TableOperation::EditX,3,state.edit.editor.text());return;
    }
    if((col==0||session().verify)&&((key>=K::Digit0&&key<=K::Digit9)||key==K::Dot||key==K::Subtract||key==K::Fraction||key==K::Root||key==K::Open||key==K::Ans||key==K::Shift)){
      beginEdit(state.cursor.row,col);state.edit.editor.press(key);
    }
    return;
  }
  if(key==K::Return||key==K::Clear){menu(Screen::Grid);return;}
  const auto list=rows();if(list.empty())return;
  if(shortcuts())menuShortcut(key,state.selection,list.size());
  if(key==K::Up)state.selection=(state.selection+static_cast<int>(list.size())-1)%list.size();
  else if(key==K::Down)state.selection=(state.selection+1)%list.size();
  else if(state.screen==Screen::Range&&state.selection<3&&((key>=K::Digit0&&key<=K::Digit9)||key==K::Subtract||key==K::Dot)){beginEdit(state.selection,-1);state.edit.editor.press(key);return;}
  else if(key==K::Okay||key==K::Execute){
    const int action=list[state.selection].action;
    switch(state.screen){
      case Screen::Tools:
        if(action==0)menu(Screen::Range);else if(action==1)menu(Screen::Define);else if(action==2)menu(Screen::Type,session().tableType);else if(action==3)menu(Screen::Edit);
        else if(action==4){menu(Screen::Grid);start(TableOperation::Recalculate);}
        else{session().verify=!session().verify;menu(Screen::Grid);if(session().verify)clearOutputs();else start(TableOperation::Recalculate);}
        break;
      case Screen::Type:session().tableType=static_cast<uint8_t>(action);applyTransition(session(),TransitionReason::TableTypeChanged);syncEpoch();break;
      case Screen::Define:
        CalculateApp::openShared(AppId::Table,K::Function);
        // The existing Function menu owns selection, parsing and P10 errors.
        for(int i=0;i<2+action;++i)CalculateApp::press(K::Down);
        CalculateApp::press(K::Okay);state.shared=true;menu(Screen::Grid);break;
      case Screen::Range:
        if(action==3)start(TableOperation::Generate);else beginEdit(action,-1);break;
      case Screen::Edit:
        if(action==0){if(rowCount()>=tableCapacity(session().tableType)){showError(Error::Range);break;} // provisional label: the manual confirms the 45/30 maximum but names no error for Insert Row at capacity; exact target behavior is PENDING
auto p=copyRows();p->rows.reserve(p->rows.size()+1);p->rows.insert(p->rows.begin()+std::min<size_t>(state.cursor.row,p->rows.size()),TableRow{});replace(p);menu(Screen::Grid);}
        else if(action==1)menu(Screen::Confirm);else{clearOutputs();menu(Screen::Grid);}break;
      case Screen::Confirm:if(action==1)resetRows();else menu(Screen::Grid);break;
      default:break;
    }
  }
  state.first=UiKit::ensureVisible(state.first,state.selection,static_cast<int>(rows().size()),visibleRows());
}
void adopt(const Job& job,Result& output){
  if(state.shared){CalculateApp::adopt(job,output);return;}
  if(!state.active||!state.pending||session().app!=AppId::Table||state.epoch!=session().tableEpoch||job.ownerRevision!=state.revision||job.request.memory.f!=session().memory.f||job.request.memory.g!=session().memory.g)return;
  state.pending=false;
  if(job.destination==1){
    if(state.screen!=Screen::Range||!state.edit.active||state.edit.stale(state.revision)||job.request.expression!=state.edit.editor.text())return;
    if(output.error!=Error::None){showError(output.error);return;}
    session().tableRange[state.target.row]=output.value;++state.revision;state.edit.abort();state.selection=std::min(3,state.selection+1);
    state.first=UiKit::ensureVisible(state.first,state.selection,4,kDistributionFormRows);return;
  }
  if(!job.table||job.table->type!=session().tableType||(state.screen!=Screen::Grid&&state.screen!=Screen::Range))return;
  if(job.table->range!=session().tableRange)return;
  if((job.table->operation==TableOperation::EditX||job.table->operation==TableOperation::Verify)&&(!state.edit.active||state.target.revision!=state.revision||job.request.expression!=state.edit.editor.text()))return;
  if(output.error!=Error::None){showError(output.error);return;}
  state.table=output.table;++state.revision;state.edit.abort();
  if(job.table->operation==TableOperation::Generate){session().memory.variables[6]=job.table->range[1];++session().revision;state.cursor={};}
  if(job.table->operation==TableOperation::Verify){state.truth=output.truth;menu(Screen::VerifyResult);}
  else menu(Screen::Grid);
  state.stale=job.table->columns!=3;state.seenRevision=session().revision;clampGrid(state.cursor,policy());
}
bool caretVisible(){return state.shared?CalculateApp::caretVisible():state.edit.active&&!jobs().running();}
std::vector<uint8_t> render(bool caretOn){
  if(state.shared)return CalculateApp::render(caretOn);
  if(state.screen==Screen::Error)return renderErrorDialog(state.error);
  if(jobs().running())return Busy::renderBusy();
  if(state.screen==Screen::VerifyResult)return renderSolution("",state.truth?"True":"False",session().settings);
  if(state.screen==Screen::Range){
    std::vector<DistributionField> fields;
    for(int i=0;i<3;++i)fields.push_back({i==0?"Start":i==1?"End":"Step",formatValue(session().tableRange[i],session().settings)});
    return renderDistributionForm("Table Range",fields,state.selection,state.first,state.edit.active?&state.edit.editor:nullptr,state.viewport,caretOn);
  }
  if(state.screen==Screen::Confirm)return renderConfirm("Delete All?",rows(),state.selection+1,state.first);
  if(state.screen!=Screen::Grid)return renderMenuList(rows(),state.selection,state.first,shortcuts());
  DataGridView v;v.headers=session().tableType==0?std::vector<std::string>{"x","f(x)","g(x)"}:std::vector<std::string>{"x",session().tableType==1?"f(x)":"g(x)"};
  if(state.table)for(const auto& r:state.table->rows){v.cells.push_back(cell(r,0));if(session().tableType!=2)v.cells.push_back(cell(r,1));if(session().tableType!=1)v.cells.push_back(cell(r,2));}
  if(rowCount()<tableCapacity(session().tableType))for(int c=0;c<columns();++c)v.cells.push_back("");
  v.cursor=state.cursor;
  v.table=true;v.editor=state.edit.active?&state.edit.editor:nullptr;v.caretOn=caretOn;
  const bool missingF=session().tableType!=2&&session().memory.f.empty();
  const bool missingG=session().tableType!=1&&session().memory.g.empty();
  if(missingF||missingG)v.footer=std::string(missingF?(missingG?"f(x)/g(x)":"f(x)"):"g(x)")+":None";
  else if(static_cast<size_t>(state.cursor.row)<rowCount())v.footer=cell(state.table->rows[state.cursor.row],semanticColumn());
  return renderDataGrid(v);
}
PanelIcons::State icons(){
  if(state.shared)return CalculateApp::icons();IconView v;
  if(state.screen==Screen::Grid||state.edit.active||state.screen==Screen::VerifyResult){v.calculation=true;v.io=session().settings.io;v.angle=session().settings.angle;v.number=session().settings.number;v.engineer=session().settings.engineer;v.verify=session().verify;v.shifted=state.edit.active&&state.edit.editor.shifted;}
  return desiredIcons(v);
}
size_t stateBytes(){return sizeof(State);}
size_t heapBytes(){return state.table?sizeof(TableData)+state.table->rows.capacity()*sizeof(TableRow):0;}
#ifdef CALCULATOR_REFERENCE_TEST
const TableData* data(){return state.table.get();}
bool needsRecalculate(){return state.stale;}
CalculatorApp::Snapshot snapshot(){
  if(state.shared)return CalculateApp::snapshot();CalculatorApp::Snapshot out;
  const char* names[]={"Table","TableTools","TableType","TableRange","TableDefine","TableEdit","TableConfirm","ErrorDialog","TableVerifyResult"};
  out.screen=names[static_cast<unsigned>(state.screen)];out.selection=state.selection;out.first=state.first;out.gridRow=state.cursor.row;out.gridCol=state.cursor.col;out.rows=static_cast<int>(rowCount());out.cols=columns();out.editing=state.edit.active;
  out.text=state.edit.active?state.edit.editor.text():"";for(const auto& r:rows())out.menuLabels.push_back(r.label);
  out.caret=lastRender().caret;out.caretRect=lastRender().caretRect;
  if(state.table)for(const auto& r:state.table->rows){out.cells.push_back(cell(r,0));if(session().tableType!=2)out.cells.push_back(cell(r,1));if(session().tableType!=1)out.cells.push_back(cell(r,2));}
  if(state.screen==Screen::Error){out.errorTitle=errorText(state.error);out.errorOrigin=names[static_cast<unsigned>(state.errorOrigin)];}
  if(state.screen==Screen::VerifyResult){out.hasResult=true;out.resultText=state.truth?"True":"False";}
  if((session().tableType!=2&&session().memory.f.empty())||(session().tableType!=1&&session().memory.g.empty()))out.notice="Not Defined";
  return out;
}
#endif
}
}
