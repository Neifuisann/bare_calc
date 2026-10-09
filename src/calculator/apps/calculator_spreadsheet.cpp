#include "calculator/apps/calculator_spreadsheet.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/runtime/calculator_storage.h"
#include "calculator/ui/calculator_render.h"
#include "system/psram_allocator.h"
#include <algorithm>

namespace Calculator { namespace SpreadsheetApp {
namespace {
using K=ExpressionInput::Key;
enum class Screen : uint8_t { Grid, Tools, Catalog, Commands, Category, Fill, AutoCalc, ShowCell, Memory, Grab, Paste, Error };
struct State {
  std::shared_ptr<const SpreadsheetData> data;
  GridCursor cursor, savedCursor;
  MathEditor editor;
  Screen screen=Screen::Grid, origin=Screen::Grid, errorOrigin=Screen::Grid;
  uint32_t revision=0, epoch=0;
  int selection=0, first=0, group=0, fillSelection=0;
  std::string fillExpression, fillRange;
  bool active=false, editing=false, shared=false, pending=false;
  bool autoCalc=true, showFormula=true, fillFormula=true, cut=false;
  Error error=Error::None;
};
State& state(){static auto& s=CalculatorStorage::makeState<State>();return s;}
CalculatorSession& session(){return calculatorSession();}
JobService& jobs(){return calculatorJobs();}
constexpr GridPolicy policy{kSheetRows,kSheetCols,4,4};
const SpreadsheetCell* cell(){auto& s=state();return s.data?&s.data->cells[s.cursor.row*kSheetCols+s.cursor.col]:nullptr;}
std::string valueText(const SpreadsheetCell* c) {
  if(!c||!c->occupied)return "";
  if(c->error!=Error::None)return "ERROR";
  return formatValue(Value::approximate(c->value),session().settings,Format::Decimal);
}
std::string sourceText(const SpreadsheetCell* c){return c&&!c->formula.empty()?c->formula:valueText(c);}
void invalidate(){auto& s=state();jobs().cancel();s.pending=false;++s.revision;}
void menu(Screen screen,int selected=0){auto& s=state();s.screen=screen;s.selection=selected;s.first=0;}
void syncEpoch(){auto& s=state();if(s.epoch!=session().spreadsheetEpoch){clear();s.epoch=session().spreadsheetEpoch;}}
void beginEdit(const std::string& source=""){
  auto& s=state();s.editing=true;s.editor.setMathInput(false);s.editor.begin(source);
}
bool inputKey(K key){return (key>=K::Digit0&&key<=K::Scientific)||key==K::Shift||key==K::Subtract||key==K::Add||key==K::Multiply||key==K::Divide||key==K::X||key==K::Fraction||key==K::Root||key==K::Power||key==K::Square||key==K::Log||key==K::Ans||key==K::Sin||key==K::Cos||key==K::Tan||key==K::Open||key==K::Close;}
void type(K key){
  auto& e=state().editor;
  const auto previous=e.text();const auto cursor=e.cursor();e.press(key);
  if(spreadsheetInputBytes(e.text())>kSheetInput){e.begin(previous);e.setCursor(cursor);e.notice="Input limit: 49 bytes";}
}
std::vector<MenuItem> rows(){
  auto& s=state();
  if(s.screen==Screen::Tools)return {{0,"Fill Formula"},{1,"Fill Value"},{2,"Edit Cell"},{3,"Available Memory"},{4,"Cut & Paste"},{5,"Copy & Paste"},{6,"Delete All"},{7,"Recalculate"},{8,"Auto Calc",true},{9,"Show Cell",true}};
  if(s.screen==Screen::Catalog){
    std::vector<MenuItem> list={{0,"Spreadsheet",true}};
    for(size_t i=0;i<catalogData().size();++i)if(static_cast<int>(i)!=verifyGroupIndex())list.push_back({static_cast<int>(i)+1,catalogData()[i].name,true});
    return list;
  }
  if(s.screen==Screen::Commands)return {{0,"Grab"},{1,"="},{2,"$"},{3,":"},{4,"Min"},{5,"Max"},{6,"Mean"},{7,"Sum"}};
  if(s.screen==Screen::Category){std::vector<MenuItem> list;const auto& entries=catalogData()[s.group].entries;for(size_t i=0;i<entries.size();++i)list.push_back({static_cast<int>(i),entries[i].label});return list;}
  if(s.screen==Screen::AutoCalc)return {{0,"On",false,s.autoCalc?UiKit::Mark::kRadioOn:UiKit::Mark::kRadioOff},{1,"Off",false,s.autoCalc?UiKit::Mark::kRadioOff:UiKit::Mark::kRadioOn}};
  if(s.screen==Screen::ShowCell)return {{0,"Formula",false,s.showFormula?UiKit::Mark::kRadioOn:UiKit::Mark::kRadioOff},{1,"Value",false,s.showFormula?UiKit::Mark::kRadioOff:UiKit::Mark::kRadioOn}};
  return {};
}
void error(Error e){auto& s=state();s.errorOrigin=s.screen;s.error=e;s.screen=Screen::Error;}
void submit(SpreadsheetOperation op,const std::string& expression="",const SpreadsheetRange* range=nullptr){
  auto& s=state();if(jobs().running())return;
  auto task=std::make_shared<SpreadsheetTask>();task->operation=op;task->input=s.data;task->expression=expression;task->autoCalc=s.autoCalc;
  task->range=range?*range:SpreadsheetRange{s.cursor.row,s.cursor.col,s.cursor.row,s.cursor.col};
  task->sourceRow=s.savedCursor.row;task->sourceCol=s.savedCursor.col;
  Job& job=prepareJob(AppId::Spreadsheet,0,expression);job.spreadsheet=task;job.ownerRevision=s.revision;
  s.pending=jobs().start(job)==JobService::Start::Started;job.spreadsheet.reset();
  if(!s.pending)error(Error::Stack);
}
void openCatalog(){auto& s=state();s.origin=s.screen;menu(Screen::Catalog);s.editor.shifted=false;}
void insert(const std::string& text,bool templ=false){
  auto& s=state();if(!s.editing)beginEdit();
  const auto before=s.editor.text();const auto cursor=s.editor.cursor();
  if(templ)s.editor.insertTemplate(text);else s.editor.insert(text);
  if(spreadsheetInputBytes(s.editor.text())>kSheetInput){s.editor.begin(before);s.editor.setCursor(cursor);s.editor.notice="Input limit: 49 bytes";}
}
void shared(K key){
  auto& s=state();ResultSummary summary;const auto* c=cell();const ResultSummary* value=nullptr;
  if(c&&c->occupied&&c->error==Error::None){summary.value=Value::approximate(c->value);value=&summary;}
  CalculateApp::openShared(AppId::Spreadsheet,key,value);s.shared=true;s.editor.shifted=false;
}
void fill(bool formula){auto& s=state();s.fillFormula=formula;s.fillExpression="";s.fillRange=spreadsheetReference(s.cursor.row,s.cursor.col);s.fillRange+=':'+s.fillRange;s.fillSelection=0;s.editing=false;menu(Screen::Fill);}
}
void clear(){auto& s=state();if(s.pending)jobs().cancel();s.pending=false;++s.revision;s.data.reset();s.cursor={};s.editing=false;s.editor.begin();s.shared=false;menu(Screen::Grid);}
void enter(){auto& s=state();syncEpoch();s.active=true;s.shared=false;s.editing=false;menu(Screen::Grid);}
void pause(){auto& s=state();invalidate();s.active=false;s.editing=false;s.editor.begin();s.shared=false;menu(Screen::Grid);}
bool service(){
  // This sheet deliberately survives another app's launch (fx-880BTG manual).
  auto& s=state();if(s.epoch==session().spreadsheetEpoch)return false;
  syncEpoch();return true;
}
void press(K key){
  auto& s=state();syncEpoch();
  if(s.shared){
    CalculateApp::press(key);s.shared=CalculateApp::sharedActive();
    if(!s.shared){const auto token=CalculateApp::sharedInput();if(!token.empty())insert(token);}
    return;
  }
  if(s.screen==Screen::Error){if(key==K::Okay||key==K::Return||key==K::Clear){s.screen=s.errorOrigin;s.error=Error::None;}return;}
  if(key==K::Interrupted){invalidate();s.editor.press(key);s.editing=false;menu(Screen::Grid);return;}
  if(jobs().running()){
    if(key==K::Return||key==K::Clear){invalidate();if(key==K::Clear)s.editing=false;}
    return;
  }
  if(s.screen==Screen::Memory){if(key==K::Okay||key==K::Execute||key==K::Return||key==K::Clear)menu(Screen::Grid);return;}
  if(s.screen==Screen::Paste||s.screen==Screen::Grab){
    if(moveGrid(s.cursor,policy,key))return;
    if(key==K::Return||key==K::Clear){if(s.screen==Screen::Grab){s.cursor=s.savedCursor;menu(s.origin);}else menu(Screen::Grid);return;}
    if(key==K::Okay||key==K::Execute){
      if(s.screen==Screen::Grab){const auto name=spreadsheetReference(s.cursor.row,s.cursor.col);s.cursor=s.savedCursor;insert(name);menu(s.origin);}
      else submit(s.cut?SpreadsheetOperation::Cut:SpreadsheetOperation::Copy);
    }
    return;
  }
  if(s.screen==Screen::Grid||s.screen==Screen::Fill){
    if(key==K::Catalog){openCatalog();return;}
    if(key==K::Settings||key==K::Variable||key==K::Function){shared(key);return;}
    if(s.editing){
      if(key==K::Return){s.editing=false;return;}
      if(key==K::Execute||key==K::Okay){
        if(s.editor.text().empty())return;
        if(s.screen==Screen::Fill){
          if(s.fillSelection==0)s.fillExpression=s.editor.text();
          else {SpreadsheetRange r;if(!spreadsheetRange(s.editor.text(),r)){error(Error::Range);return;}s.fillRange=s.editor.text();}
          s.editing=false;s.fillSelection=std::min(2,s.fillSelection+1);
        }else submit(SpreadsheetOperation::Set,s.editor.text());
        return;
      }
      type(key);return;
    }
    if(s.screen==Screen::Fill){
      if(key==K::Return||key==K::Clear){menu(Screen::Grid);return;}
      if(key==K::Up)s.fillSelection=std::max(0,s.fillSelection-1);
      else if(key==K::Down)s.fillSelection=std::min(2,s.fillSelection+1);
      else if(key==K::Okay||key==K::Execute){
        if(s.fillSelection<2)beginEdit(s.fillSelection==0?s.fillExpression:s.fillRange);
        else{SpreadsheetRange r;if(!spreadsheetRange(s.fillRange,r)){error(Error::Range);return;}submit(s.fillFormula?SpreadsheetOperation::FillFormula:SpreadsheetOperation::FillValue,s.fillExpression,&r);}
      }else if(s.fillSelection<2&&inputKey(key)){beginEdit();type(key);}
      return;
    }
    if(moveGrid(s.cursor,policy,key))return;
    if(key==K::Tools){menu(Screen::Tools);return;}
    if(key==K::Delete){submit(SpreadsheetOperation::Erase);return;}
    if(key==K::Okay){beginEdit(sourceText(cell()));return;}
    if(inputKey(key)){beginEdit();type(key);}
    return;
  }
  if(key==K::Return||key==K::Clear){
    if(s.screen==Screen::Commands||s.screen==Screen::Category)menu(Screen::Catalog);
    else if(s.screen==Screen::Catalog)menu(s.origin);
    else if(s.screen==Screen::Tools)menu(Screen::Grid);
    else menu(Screen::Tools);
    return;
  }
  const auto list=rows();if(list.empty())return;
  if(key==K::Up)s.selection=(s.selection+list.size()-1)%list.size();
  else if(key==K::Down)s.selection=(s.selection+1)%list.size();
  else if(key==K::PageUp)s.selection=std::max(0,s.selection-4);
  else if(key==K::PageDown)s.selection=std::min(static_cast<int>(list.size())-1,s.selection+4);
  else if(key==K::Okay||key==K::Execute||key==K::Right){
    const int action=list[s.selection].action;
    if(key==K::Right&&!list[s.selection].submenu)return;
    switch(s.screen){
      case Screen::Tools:
        if(action<2)fill(action==0);
        else if(action==2){menu(Screen::Grid);beginEdit(sourceText(cell()));}
        else if(action==3)menu(Screen::Memory);
        else if(action==4||action==5){s.savedCursor=s.cursor;s.cut=action==4;menu(Screen::Paste);}
        else if(action==6){menu(Screen::Grid);submit(SpreadsheetOperation::Clear);}
        else if(action==7){menu(Screen::Grid);submit(SpreadsheetOperation::Recalculate);}
        else if(action==8)menu(Screen::AutoCalc,s.autoCalc?0:1);
        else menu(Screen::ShowCell,s.showFormula?0:1);
        break;
      case Screen::Catalog:
        if(action==0)menu(Screen::Commands);else{s.group=action-1;menu(Screen::Category);}break;
      case Screen::Commands:
        if(action==0){if(!s.editing)beginEdit();s.savedCursor=s.cursor;menu(Screen::Grab);}
        else{static const char* tokens[]={"","=","$",":","Min(","Max(","Mean(","Sum("};insert(tokens[action]);menu(s.origin);}break;
      case Screen::Category:insert(catalogData()[s.group].entries[action].expression,true);menu(s.origin);break;
      case Screen::AutoCalc:s.autoCalc=action==0;menu(Screen::Grid);break;
      case Screen::ShowCell:s.showFormula=action==0;menu(Screen::Grid);break;
      default:break;
    }
  }
  const auto next=rows();s.first=UiKit::ensureVisible(s.first,s.selection,next.size(),s.screen==Screen::AutoCalc?3:4);
}
void adopt(const Job& job,Result& result){
  auto& s=state();if(s.shared){CalculateApp::adopt(job,result);return;}
  if(!s.active||!s.pending||session().app!=AppId::Spreadsheet||job.ownerRevision!=s.revision||s.epoch!=session().spreadsheetEpoch||!job.spreadsheet)return;
  s.pending=false;
  if(result.error==Error::Cancelled)return;
  if(result.error!=Error::None){error(result.error);return;}
  if(!result.spreadsheet)return;
  s.data=result.spreadsheet;++s.revision;
  const auto op=job.spreadsheet->operation;
  if(op==SpreadsheetOperation::Copy&&!s.cut){menu(Screen::Paste);return;}
  s.editing=false;s.editor.begin();menu(Screen::Grid);
  if(op==SpreadsheetOperation::Set){++s.cursor.row;clampGrid(s.cursor,policy);}
}
bool caretVisible(){auto& s=state();return s.shared?CalculateApp::caretVisible():s.editing&&s.screen!=Screen::Error&&!jobs().running();}
std::vector<uint8_t> render(bool caretOn){
  auto& s=state();if(s.shared)return CalculateApp::render(caretOn);
  if(s.screen==Screen::Error)return renderErrorDialog(s.error);
  if(jobs().running())return Busy::renderBusy();
  if(s.screen==Screen::Memory)return renderSpreadsheetMemory(kSheetMemory-(s.data?spreadsheetUsedBytes(*s.data):0));
  if(s.screen==Screen::Fill)return renderSpreadsheetFill(s.fillFormula,s.fillExpression,s.fillRange,s.fillSelection,s.editing?&s.editor:nullptr,caretOn);
  if(s.screen==Screen::AutoCalc)return renderSpreadsheetChoices("Auto Calculation?",rows(),s.selection);
  if(s.screen==Screen::ShowCell)return renderSpreadsheetChoices("",rows(),s.selection);
  if(s.screen!=Screen::Grid&&s.screen!=Screen::Grab&&s.screen!=Screen::Paste)return renderMenuList(rows(),s.selection,s.first,false);
  SpreadsheetView view;view.cursor=s.cursor;view.caretOn=caretOn;
  for(int row=s.cursor.firstRow;row<s.cursor.firstRow+4;++row)for(int col=s.cursor.firstCol;col<s.cursor.firstCol+4;++col)
    if(!s.data)view.cells.push_back("");
    else{const auto& c=s.data->cells[row*kSheetCols+col];view.cells.push_back(!c.occupied?"":c.error!=Error::None?"ERROR":spreadsheetCellText(c.value,session().settings));}
  if(s.screen==Screen::Paste){view.footer="Paste:[OK]";view.paste=true;}
  else if(s.editing){view.editor=&s.editor;}
  else view.footer=s.showFormula?sourceText(cell()):valueText(cell());
  return renderSpreadsheet(view);
}
PanelIcons::State icons(){
  auto& s=state();if(s.shared)return CalculateApp::icons();IconView v;
  if(s.screen==Screen::Grid||s.screen==Screen::Grab||s.screen==Screen::Fill){v.calculation=true;v.io=IO::LineDecimal;v.angle=session().settings.angle;v.number=session().settings.number;v.shifted=s.editing&&s.editor.shifted;}
  return desiredIcons(v);
}
size_t stateBytes(){return sizeof(State);}
size_t heapBytes(){auto& s=state();size_t n=s.data?sizeof(SpreadsheetData):0;if(s.data)for(const auto& c:s.data->cells)n+=c.formula.capacity();return n;}
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot(){
  auto& s=state();if(s.shared)return CalculateApp::snapshot();CalculatorApp::Snapshot out;
  const char* names[]={"Spreadsheet","SpreadsheetTools","SpreadsheetCatalog","SpreadsheetCommands","SpreadsheetCategory","SpreadsheetFill","SpreadsheetAutoCalc","SpreadsheetShowCell","SpreadsheetMemory","SpreadsheetGrab","SpreadsheetPaste","ErrorDialog"};
  out.screen=names[static_cast<unsigned>(s.screen)];out.gridRow=s.cursor.row;out.gridCol=s.cursor.col;out.rows=kSheetRows;out.cols=kSheetCols;out.selection=s.screen==Screen::Fill?s.fillSelection:s.selection;out.first=s.first;out.editing=s.editing;out.text=s.editing?s.editor.text():sourceText(cell());
  out.caret=lastRender().caret;out.caretRect=lastRender().caretRect;
  for(const auto& r:rows())out.menuLabels.push_back(r.label);
  for(int i=0;i<kSheetCells;++i)out.cells.push_back(s.data?valueText(&s.data->cells[i]):"");
  if(s.screen==Screen::Error){out.errorTitle=errorText(s.error);out.errorOrigin=names[static_cast<unsigned>(s.errorOrigin)];}
  return out;
}
#endif
} } // namespace Calculator::SpreadsheetApp
