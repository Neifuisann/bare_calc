#include "calculator/runtime/calculator_storage.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/apps/calculator_calculate.h"
#include "calculator/math/calculator_basen.h"
#include "calculator/apps/calculator_matrix.h"
#include "calculator/ui/calculator_editor.h"
#include "calculator/math/calculator_engine.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/runtime/calculator_platform.h"
#include "calculator/ui/calculator_render.h"
#include "ui/ui_kit.h"
#include <algorithm>
#include <array>
#include <functional>
#include <memory>

namespace Calculator {
namespace CalculateApp {
namespace {
using K=ExpressionInput::Key;
enum class Screen {Entry,Catalog,Tools,Settings,SettingsOption,Format,Variables,VariableAction,Functions,Store,Define,Calc,Reset,ResetConfirm,Constants,Conversions,
  Aggregate,CalcSettings,SystemSettings,ErrorDialog,HistoryView,QuickCatalog,GetStarted,StorePick};  // Append screens; preserve existing IDs.
Screen screen=Screen::Entry;
AppId activeApp=AppId::Calculate;
bool hasResult=false;
bool systemSetupRequested=false;  // Settings > System Setup, until the host takes it
// HistoryView: read-only stored source + answer, no caret, no recompute. Any
// empty entry line with history browses (UP = newest with answer, LEFT/RIGHT
// recall its source); clearing to empty restores that, so no pristine flag.
bool historyReturnEmpty=false;  // HistoryView entered from an empty line: DOWN at newest returns there
int16_t resultScrollX=0,historyScrollX=0;  // A-07 result viewports; never the editor viewport
// Deterministic scroll step for overflowing answers; the exact physical
// cadence is unconfirmed (PHYSICAL_LONG_RESULT_PENDING), so only the
// clamped range (summaryMaxScroll) is asserted, never the step.
constexpr int16_t kResultScrollStep=95;
MathEditor editor,fieldEditor;
Viewport entryView,fieldView,historyEntryView;
// State every app shares; what survives Home, launches, resets and IO changes
// is decided only by applyTransition(). The aliases keep this code as it was.
CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }
Settings& settings() { return session().settings; }
Memory& memory() { return session().memory; }
bool& verify() { return session().verify; }
std::vector<HistoryEntry>& history() { return session().history; }
Result& result() {
  static auto& value = CalculatorStorage::makeState<Result>();
  return value;
}
Format resultFormat=Format::Standard;
size_t selection=0;int group=-1,variable=0,definition=0;
// Caller-owned scroll window for the visible menu: painting, page keys and
// the UiKit scrollbar share this one first row (see UiKit::Window).
int menuFirst=0;
// Settings option page (a SettingAction) and pending ResetConfirm action.
int settingOption=-1,resetPending=-1;
MenuStack menuParents;
 // P13 A-10: explicit CALC state machine. The parent expression source is
 // captured at launch and stays immutable for the whole run; prompt editing
 // uses only fieldEditor (always Linear), never the main editor.
 // state | event                  | next               | side effect
 // Inactive | TOOLS CALC (vars)    | Prompting idx 0    | capture parent, blank field
 // Inactive | TOOLS CALC (no vars) | Entry              | ordinary execute(), no context
 // Prompting| value EXE            | Prompting idx+1    | commit var, blank next field
 // Prompting| last value EXE       | AwaitFinalExecute  | commit var, NO evaluation yet
 // Await    | final EXE            | Running (Entry)    | exactly one parent job
 // Await    | edit committed value | Prompting last idx | value stays visible for correction
 // Running  | success              | Result (Entry)     | normal P12 result/history/Ans
 // Running  | failure              | Entry              | ordinary error, context retired
 // Running  | Clear/Return         | Await (Calc)       | cancel, back to prompts
 // Result   | EXE                  | Prompting idx 0    | sole repeat path, blank fields
 // Result   | ordinary exit        | Inactive           | AC/edit/browse/Home/switch retire
enum class CalcPhase : uint8_t {
  Inactive,
  Prompting,
  AwaitFinalExecute,
  Running,
  Result,
};
// Job ownership: prompt values evaluate the field text; the final evaluation
// runs the immutable parent source through the ordinary Entry adoption path.
enum class CalcJobKind : uint8_t { None, PromptValue, FinalExpression };
struct CalcContext {
  std::string parentSource;
  std::vector<int> variables;  // first-occurrence discovery order, deduped
  size_t index = 0;
  CalcPhase phase = CalcPhase::Inactive;
  uint32_t generation = 0;  // bumped per launch and per repeat; stale jobs die on it
  bool hasLastResult = false;
};
CalcContext calc;
// Never carries a Result/Memory snapshot: the context is source text plus
// small indices, so repeats cost bytes, not kilobytes.
static_assert(sizeof(CalcContext) <= 512, "CalcContext budget: parent source plus indices only");
void retireCalc();        // defined with the other CALC helpers below
void syncCalcFieldStyle();  // defined with the other CALC helpers below
void syncInputStyle();  // defined with the other input-style helpers below
size_t historyCursor=0;  // HistoryView selection; newest after each adoption
// FORMAT opened on a HistoryView entry: its rows come from that stored
// answer, the choice becomes the entry's displayed format, and closing goes
// back to the entry instead of the entry line.
bool formatHistory=false;
std::vector<MenuItem> formatMenuRows(){
  if(formatHistory&&historyCursor<history().size())return formatRows(history()[historyCursor].answer,settings());
  return formatRows(hasResult?&result():nullptr,hasResult,settings());
}
// Compact view of the live Result for the shared result/history renderer.
// Built once at adoption; browsing never recomputes from source.
ResultSummary currentSummary;
// Show HistoryView entry i (expression + stored answer, no caret).
void showHistoryIndex(size_t i){historyCursor=i;historyScrollX=0;historyEntryView=Viewport{};}
void enterHistoryView(size_t i,bool fromEmpty){
  retireCalc();  // Browsing is an ordinary exit: no CALC repeat afterwards.
  showHistoryIndex(i);historyReturnEmpty=fromEmpty;screen=Screen::HistoryView;
}
// Read-only view, or empty-line recall, -> editing: load the source exactly
// once, the stored answer disappears, the caret appears, the selection ends.
// LEFT/BACK end, RIGHT start.
void enterEditFromView(const std::string& source,bool atEnd){
  retireCalc();
  editor.begin(source);editor.setCursor(atEnd?source.size():0);
  hasResult=false;historyReturnEmpty=false;screen=Screen::Entry;
  resultScrollX=0;historyScrollX=0;
}
// AC on a result, in HistoryView, or over recalled text: ordinary clear back
// to the empty line. Browsability follows from emptiness + history alone.
void clearEntry(){
  retireCalc();
  editor.begin();hasResult=false;screen=Screen::Entry;
  historyReturnEmpty=false;
  resultScrollX=0;historyScrollX=0;entryView=Viewport{};historyEntryView=Viewport{};
}
// Storing a solve() answer: its exact numeric roots ride on the live Result
// (Cas::evaluate), the formula stays what the screen shows. One root stores
// directly; several open StorePick, a list of "x1=..." rows, for `storeTarget`.
// `storePickFromMenu`: opened by VARIABLE > Store, so it returns to the grid.
int storeTarget=0;
bool storePickFromMenu=false;
const std::vector<Solution>* storableRoots(){
  if(!hasResult||result().error!=Error::None||!result().formula||!result().solutions||result().solutions->empty())return nullptr;
  return result().solutions.get();
}
std::vector<MenuItem> storePickRows(){
  std::vector<MenuItem> out;
  if(const auto* roots=storableRoots())
    for(size_t i=0;i<roots->size();++i)out.push_back({static_cast<int>(i),valueLabel((*roots)[i].label,(*roots)[i].value,settings()),false});
  return out;
}
// "Stored in A" in the result's notice band; cleared by the next key only if
// it is still the store's own notice (never a Cancelled/memory notice).
bool storeNoticeShown=false;
void storeNotice(bool on){
  const std::string text=std::string("Stored in ")+variableName(storeTarget);
  if(on){editor.notice=text;storeNoticeShown=true;return;}
  if(storeNoticeShown&&editor.notice==text)editor.notice.clear();
  storeNoticeShown=false;
}
std::vector<size_t> tableIndices;
int tableGroup=-1;
// Which editor owns the open menu: Main for entry-side menus, otherwise the
// Store/Define/Calc field the menu was opened from. Catalog/function
// insertion and menu closing follow it back to the right editor.
MenuOrigin menuOrigin=MenuOrigin::Main;
bool menuOpenedByUser=false;
Screen fieldMenuReturn=Screen::Entry;
int defineMenuSelection=2;  // Functions row that opened Define; restored on save/cancel
// Field drafts parked while Define screens opened from field menus are up.
// A stack: Define itself opens Catalog/Functions, so a nested Define parks
// another draft. Bounded; deeper nesting drops the oldest draft.
struct FieldStash{std::string text;size_t cursor=0;Screen screen=Screen::Entry;int variable=0,definition=0;size_t calcIndex=0;uint32_t calcGeneration=0;CalcPhase calcPhase=CalcPhase::Inactive;};
std::vector<FieldStash> fieldStash;
constexpr size_t kFieldStashMax=4;
// Source stays in its original editor throughout a modal. These bounded
// owners supplement JobService's generation/cancellation gate.
struct EvaluationOwner {
  Screen origin=Screen::Entry;
  AppId app=AppId::Calculate;
  uint32_t revision=0;
  int variable=0,definition=0;
  size_t calcIndex=0;
  uint32_t calcGeneration=0;
  CalcJobKind calcKind=CalcJobKind::None;
};
EvaluationOwner pendingOwner;
bool pendingEvaluation=false,revealErrorCaret=false;
struct ErrorState {
  EvaluationOwner owner;
  Error code=Error::None;
  const char* title=""; // Central errorText mapping; never translated by the evaluator.
  size_t cursor=0;
} errorDialog;
EvaluationOwner evaluationOwner(Screen origin,CalcJobKind kind=CalcJobKind::None){
  return {origin,activeApp,session().revision,variable,definition,calc.index,calc.generation,kind};
}
bool ownsCurrent(const EvaluationOwner& owner){
  if(owner.app!=activeApp||owner.revision!=session().revision||
     owner.variable!=variable||owner.definition!=definition)
    return false;
  if(owner.calcKind==CalcJobKind::None) return owner.calcIndex==calc.index;
  // CALC-owned jobs die with their generation lineage; prompt values pin the
  // index they were submitted from, the final pins the parent source instead.
  if(owner.calcGeneration!=calc.generation) return false;
  if(owner.calcKind==CalcJobKind::PromptValue) return owner.calcIndex==calc.index;
  return true;
}
bool fieldScreen(Screen s){return s==Screen::Store||s==Screen::Define||s==Screen::Calc;}
// P13: ordinary exits retire the CALC repeat context, so the next EXE on an
// ordinary result re-executes instead of reopening prompts.
void retireCalc(){
  calc.phase=CalcPhase::Inactive;
  calc.hasLastResult=false;
  calc.parentSource.clear();
  calc.variables.clear();
  calc.index=0;
  ++calc.generation;
}
// CALC value fields are always Linear ([880-MAN]), independent of the global
// Input/Output that still governs the parent expression and the icons.
void syncCalcFieldStyle(){ fieldEditor.setMathInput(false); }
// P10 field-error ownership: a CALC prompt failure keeps the exact submitting
// job lineage (generation+index) so dismissal restores the same field.
EvaluationOwner errorOwnerFor(Screen origin){
  if(origin==Screen::Calc&&pendingEvaluation) return pendingOwner;
  return evaluationOwner(origin);
}
void showError(Error code,size_t position,Screen origin){
  MathEditor& source=fieldScreen(origin)?fieldEditor:editor;
  // Domain/Math errors return to the end (C58). Syntax and other typed
  // locations use source/document stops, never geometric nearest().
  errorDialog={errorOwnerFor(origin),code,errorText(code),
               errorReturnCursor(source.text(),code,position)};
  source.notice.clear();
  screen=Screen::ErrorDialog;
}
void dismissError(){
  if(!ownsCurrent(errorDialog.owner))return; // Never restore over a newer owner.
  screen=errorDialog.owner.origin;
  if(screen==Screen::Calc)syncCalcFieldStyle();
  MathEditor& source=fieldScreen(screen)?fieldEditor:editor;
  source.setCursor(errorDialog.cursor);
  revealErrorCaret=true;
  errorDialog.code=Error::None;
}
void changed(){++session().revision;}
void stashField(){
  if(fieldStash.size()>=kFieldStashMax)fieldStash.erase(fieldStash.begin());
  fieldStash.push_back({fieldEditor.text(),fieldEditor.cursor(),fieldMenuReturn,variable,definition,calc.index,calc.generation,calc.phase});
}
bool stashedField(){return !fieldStash.empty();}
void unstashField(){
  const FieldStash top=fieldStash.back();fieldStash.pop_back();
  fieldEditor.begin(top.text);fieldEditor.setCursor(top.cursor);
  screen=top.screen;variable=top.variable;definition=top.definition;calc.index=top.calcIndex;
  if(top.screen==Screen::Calc&&top.calcGeneration==calc.generation){
    calc.phase=top.calcPhase;syncCalcFieldStyle();
  }
  fieldView=Viewport{};
}
Screen originScreen(MenuOrigin origin){
  switch(origin){
    case MenuOrigin::Store:return Screen::Store;
    case MenuOrigin::Define:return Screen::Define;
    case MenuOrigin::Calc:return Screen::Calc;
    default:return Screen::Entry;
  }
}
// The menu stack is empty: return to whatever opened the menu.
void closeMenuToCaller(){
  if(formatHistory){
    formatHistory=false;menuOpenedByUser=false;menuOrigin=MenuOrigin::Main;menuParents.clear();
    screen=history().empty()?Screen::Entry:Screen::HistoryView;
    return;
  }
  screen=menuOrigin==MenuOrigin::Main?Screen::Entry:fieldMenuReturn;
  // Field callers themselves return to VARIABLE/FUNCTION. Keep their user
  // menu origin alive when a nested Catalog/Function menu closes.
  menuOpenedByUser=menuOrigin!=MenuOrigin::Main;
  menuOrigin=MenuOrigin::Main;menuParents.clear();
}
// Encode the current menu level for the parent stack: the screen plus its
// subpage (catalog group, table group, setting id, or pending reset).
MenuContext currentContext() {
  MenuContext context;
  context.selected=static_cast<int>(selection);
  context.first=menuFirst;
  context.app=activeApp;
  context.target.origin=menuOrigin;
  context.openedByUser=menuOpenedByUser;
  switch (screen) {
    case Screen::Catalog: context.screen=0; context.subpage=group; break;
    case Screen::Constants: context.screen=1; context.subpage=tableGroup; break;
    case Screen::Conversions: context.screen=2; context.subpage=tableGroup; break;
    case Screen::Tools: context.screen=3; context.subpage=-1; break;
    case Screen::Settings: context.screen=4; context.subpage=-1; break;
    case Screen::SettingsOption: context.screen=5; context.subpage=settingOption; break;
    case Screen::Format: context.screen=6; context.subpage=-1; break;
    case Screen::Variables: context.screen=7; context.subpage=-1; break;
    case Screen::VariableAction: context.screen=8; context.subpage=variable; break;
    case Screen::Functions: context.screen=9; context.subpage=-1; break;
    case Screen::Reset: context.screen=10; context.subpage=-1; break;
    case Screen::ResetConfirm: context.screen=11; context.subpage=resetPending; break;
    case Screen::CalcSettings: context.screen=12; break;
    case Screen::SystemSettings: context.screen=13; break;
    default: context.screen=-1; context.subpage=-1; break;
  }
  return context;
}
void restoreContext(const MenuContext& context) {
  // A parent recorded for another app never restores into this one.
  if(context.app!=activeApp){screen=Screen::Entry;menuParents.clear();menuOrigin=MenuOrigin::Main;return;}
  selection=context.selected < 0 ? 0 : static_cast<size_t>(context.selected);
  menuFirst=context.first < 0 ? 0 : context.first;
  menuOrigin=context.target.origin;
  menuOpenedByUser=context.openedByUser;
  if(menuOrigin!=MenuOrigin::Main)fieldMenuReturn=originScreen(menuOrigin);
  switch (context.screen) {
    case 0: screen=Screen::Catalog; group=context.subpage; break;
    case 1: screen=Screen::Constants; tableGroup=context.subpage; break;
    case 2: screen=Screen::Conversions; tableGroup=context.subpage; break;
    case 3: screen=Screen::Tools; break;
    case 4: screen=Screen::Settings; break;
    case 5: screen=Screen::SettingsOption; settingOption=context.subpage; break;
    case 6: screen=Screen::Format; break;
    case 7: screen=Screen::Variables; variable=static_cast<int>(selection); break;
    case 8: screen=Screen::VariableAction; variable=context.subpage; break;
    case 9: screen=Screen::Functions; break;
    case 10: screen=Screen::Reset; break;
    case 11: screen=Screen::ResetConfirm; resetPending=context.subpage; break;
    case 12: screen=Screen::CalcSettings; break;
    case 13: screen=Screen::SystemSettings; break;
    default: screen=Screen::Entry; break;
  }
}
void cancel(){pendingEvaluation=false;jobs().cancel();}
bool aggregateApp(){return activeApp==AppId::Matrix||activeApp==AppId::Vector;}
// Filled in place by prepareJob: a Job holds a whole Memory, too big for the
// loop task's 8 KB stack.
void startJob(const std::string& expression, bool verifying, Screen destination,
              Format execFormat=Format::Standard, CalcJobKind kind=CalcJobKind::None) {
  Job& job=prepareJob(activeApp,static_cast<int>(destination),expression);
  job.request.verify=verifying;
  job.execFormat=execFormat;
  // The worker reads a snapshot, never slots the UI may be editing meanwhile.
  if(aggregateApp())job.aggregates=std::make_shared<Aggregates>(session().aggregates);
  const auto started=jobs().start(job);
  if(started==JobService::Start::Started){pendingOwner=evaluationOwner(destination,kind);pendingEvaluation=true;}
  if(started==JobService::Start::NoMemory){editor.notice="Not enough memory";fieldEditor.notice=editor.notice;}
}
// What a CALC prompt evaluates: the field text, or -- for a blank field --
// the variable itself, so EXE/Down accept its stored value unchanged. adopt()
// compares against this same string; comparing against the raw (blank) field
// would drop every accepted stored value as stale.
std::string calcPromptSource(){
  if(!fieldEditor.text().empty()||calc.index>=calc.variables.size())return fieldEditor.text();
  return std::string(1,variableName(calc.variables[calc.index]));
}
// Submit the CALC field: a prompt value evaluates the field text, while the
// final EXE evaluates the immutable parent source exactly once.
void submitCalcField(){
  if(calc.phase==CalcPhase::AwaitFinalExecute){
    startJob(calc.parentSource,verify(),Screen::Entry,Format::Standard,CalcJobKind::FinalExpression);
    if(!pendingEvaluation) return;  // NoMemory notice is already set; stay on the prompts.
    hasResult=false;editor.notice.clear();
    resultScrollX=0;historyScrollX=0;
    calc.phase=CalcPhase::Running;
    screen=Screen::Entry;syncInputStyle();
    return;
  }
  if(calc.phase!=CalcPhase::Prompting||calc.index>=calc.variables.size()){
    retireCalc();syncInputStyle();screen=Screen::Entry;return;  // corrupt state: never index OOB
  }
  startJob(calcPromptSource(),false,Screen::Calc,Format::Standard,CalcJobKind::PromptValue);
}
void execute(){
  if(jobs().running()){editor.notice="Finishing cancellation...";return;}
  editor.disarmIns();
  if(editor.review){editor.review=false;editor.notice="Check input; EXE calculates";return;}
  // P12 R-01: SHIFT+EXE requests Decimal presentation. One-shot policy:
  // capture and consume SHIFT here (S lamp off), even on an empty line that
  // cannot start a job, so a stray SHIFT never stays armed. Never mutate
  // source, never sticky. The exact Value/history math is unchanged; only
  // execFormat differs.
  const bool shiftDecimal=editor.shifted;
  editor.shifted=false;
  if(editor.text().empty())return;
  hasResult=false;editor.notice.clear();
  startJob(editor.text(),verify(),Screen::Entry,shiftDecimal?Format::Decimal:Format::Standard);
}
void menu(Screen s){editor.disarmIns();fieldEditor.disarmIns();formatHistory=false;screen=s;selection=0;menuFirst=0;settingOption=-1;resetPending=-1;menuParents.clear();menuOrigin=MenuOrigin::Main;menuOpenedByUser=true;}
// Catalog/Functions opened from a Store/Define/Calc field: insertion targets
// the field editor and closing the menu restores the field screen.
void openFieldMenu(Screen s){
  menuOpenedByUser=true;
  editor.disarmIns();fieldEditor.disarmIns();
  fieldMenuReturn=screen;
  menuOrigin=screen==Screen::Store?MenuOrigin::Store:screen==Screen::Define?MenuOrigin::Define:MenuOrigin::Calc;
  screen=s;selection=0;menuFirst=0;settingOption=-1;resetPending=-1;group=-1;tableGroup=-1;menuParents.clear();
}
// Semantic rows for the visible menu. Dispatch always uses MenuItem::action;
// the selection index is only a position inside this filtered vector.
std::vector<MenuItem> items(){
  switch(screen){
    case Screen::QuickCatalog:{
      std::vector<MenuItem> out;const auto entries=quickCatalogEntries(verify(),activeApp);
      for(size_t i=0;i<entries.size();++i)out.push_back({static_cast<int>(i),entries[i].label,false});
      return out;
    }
    case Screen::Catalog:
      if(group<0)return catalogRootRows(verify(),activeApp);
      return catalogCategoryRows(group);
    case Screen::Constants:
      if(tableGroup<0)return constantsGroupRows();
      return constantsItemRows(tableGroup,tableIndices);
    case Screen::Conversions:
      if(tableGroup<0)return conversionGroupRows();
      return conversionItemRows(tableGroup,tableIndices);
    case Screen::Tools:return toolRows(verify(),activeApp==AppId::Calculate);
    case Screen::Format:return formatMenuRows();
    case Screen::Settings:return settingsRootRows(settings());
    case Screen::CalcSettings:return calcSettingsRows(settings());
    case Screen::SettingsOption:return settingsOptionRows(settingOption,settings());
    // Variables renders as a grid, never as rows; VariableAction stays a list.
    case Screen::VariableAction:return variableActionRows(canStoreResult(hasResult?&result():nullptr,hasResult));
    case Screen::Functions:return functionRows();
    case Screen::Reset:return resetRows();
    case Screen::ResetConfirm:return {{ConfirmAction::Yes,"Yes",false},{ConfirmAction::No,"No",false}};
    case Screen::StorePick:return storePickRows();
    default:return {};
  }
}
void syncWindow(size_t count){
  if(count==0){selection=0;menuFirst=0;return;}
  if(selection>=count)selection=count-1;
  menuFirst=UiKit::ensureVisible(menuFirst,static_cast<int>(selection),static_cast<int>(count),kMenuVisibleRows);
}
// Push the current level and enter a child list with a fresh window. When
// the bounded stack is full the navigation is dropped and the current screen
// stays unchanged, per the MenuStack contract.
void pushChild(Screen child,int subpage,int selected=0){
  if(!menuParents.push(currentContext()))return;
  if(child==Screen::Catalog)group=subpage;
  else if(child==Screen::Constants||child==Screen::Conversions)tableGroup=subpage;
  else if(child==Screen::SettingsOption)settingOption=subpage;
  else if(child==Screen::ResetConfirm)resetPending=subpage;
  else if(child==Screen::VariableAction)variable=subpage;
  screen=child;selection=static_cast<size_t>(selected<0?0:selected);menuFirst=0;
  syncWindow(items().size());
}
// Pop one menu level, restoring the parent selection and window. Returns
// false when there was no parent (the caller closes to the entry view).
bool popParent(){
  if(menuParents.empty())return false;
  const MenuContext parent=menuParents.top();menuParents.pop();
  restoreContext(parent);
  if(screen==Screen::Variables){
    // The grid is not a scrolling list: keep the restored cell, clamped, and
    // never let the list window logic reset it to zero.
    if(selection>=static_cast<size_t>(kVariableCount))selection=kVariableCount-1;
    variable=static_cast<int>(selection);menuFirst=0;
  }else syncWindow(items().size());
  return true;
}
// Find the option row holding the current setting value, for opening an
// option page with its radio and selection already in agreement.
int currentOptionSelection(const std::vector<MenuItem>& options,int setting){
  int current=-1;
  switch(setting){
    case SettingAction::InputOutput:current=static_cast<int>(settings().io);break;
    case SettingAction::Angle:current=static_cast<int>(settings().angle);break;
    case SettingAction::Number:current=static_cast<int>(settings().number);break;
    case SettingAction::Digits:current=settings().digits;break;
    case SettingAction::Engineer:current=settings().engineer?1:0;break;
    case SettingAction::Complex:current=static_cast<int>(settings().complexResult);break;
    case SettingAction::Fraction:current=settings().mixed?1:0;break;
    case SettingAction::DecimalMark:current=settings().comma?1:0;break;
    case SettingAction::Separators:current=settings().separators?1:0;break;
    case SettingAction::FormatMenu:current=settings().formatMenu?1:0;break;
    case SettingAction::ScientificKey:current=settings().scientificPower?1:0;break;
    default:break;
  }
  for(size_t i=0;i<options.size();++i)if(options[i].action==current)return static_cast<int>(i);
  return 0;
}
void insertExpression(const char* value,MathEditor& ed){
  // E-16: single-argument calls insert open like the function keys (Close types
  // the ')'). Multi-field calls, scripts, tokens, structural templates and the
  // hidden abs() template keep their form; rand() takes no argument.
  const std::string text=value;
  const size_t n=text.size();
  if(text!="rand()"&&text!="abs()"&&n>2&&text.compare(n-2,2,"()")==0&&
     text.find(',')==std::string::npos&&text.find('^')==std::string::npos){
    ed.insertTemplate(text.substr(0,n-1));return;
  }
  if(text=="rand()")ed.insert(text);else ed.insertTemplate(text);
}
void insert(const char* value){
  const std::string text=value;
  if(menuOrigin==MenuOrigin::Main){
    if(hasResult){editor.begin();hasResult=false;}
    // rand() takes no argument; every other template starts in its first field.
    insertExpression(value,editor);
    menuParents.clear();screen=Screen::Entry;
  }else{
    // Field origin: the field draft is preserved; only the menu closes.
    insertExpression(value,fieldEditor);
    screen=fieldMenuReturn;menuOrigin=MenuOrigin::Main;menuParents.clear();
    // A menu insertion into the committed last CALC value reopens that
    // prompt for correction instead of diverging from the committed values.
    if(screen==Screen::Calc&&calc.phase==CalcPhase::AwaitFinalExecute&&
       calc.index==calc.variables.size()&&calc.index>0){
      calc.phase=CalcPhase::Prompting;--calc.index;
    }
  }
}
void doReset(int action){
  applyTransition(session(),action==ResetAction::Variables?TransitionReason::VariableMemoryReset
                  :action==ResetAction::SettingsData?TransitionReason::SettingsDataReset:TransitionReason::InitializeAll);
  retireCalc();
  if(action!=ResetAction::Variables){editor.begin();hasResult=false;}
  historyReturnEmpty=false;resultScrollX=0;historyScrollX=0;
  changed();
}
void accept(){
  const auto list=items();
  if(list.empty()||selection>=list.size())return;
  const int action=list[selection].action;
  switch(screen){
    case Screen::QuickCatalog:insert(quickCatalogEntries(verify(),activeApp)[action].expression);break;
    case Screen::Catalog:
      if(group<0){
        if(action==CatalogRootAction::SciConstants){tableGroup=-1;pushChild(Screen::Constants,-1);}
        else if(action==CatalogRootAction::UnitConversions){tableGroup=-1;pushChild(Screen::Conversions,-1);}
        else pushChild(Screen::Catalog,action);
      }else insert(catalogData()[static_cast<size_t>(group)].entries[static_cast<size_t>(action)].expression);
      break;
    case Screen::Constants:case Screen::Conversions:
      if(tableGroup<0)pushChild(screen,action);
      else if(action>=0&&static_cast<size_t>(action)<tableIndices.size()){
        const auto index=tableIndices[static_cast<size_t>(action)];
        if(screen==Screen::Constants){
          const std::string value="constant("+std::to_string(index)+")";
          if(menuOrigin==MenuOrigin::Main){if(hasResult){editor.begin();hasResult=false;}editor.insert(value);screen=Screen::Entry;}
          else{fieldEditor.insert(value);screen=fieldMenuReturn;}
        }else{
          if(menuOrigin==MenuOrigin::Main){if(hasResult){editor.begin("Ans");hasResult=false;}editor.wrapWith("convert(",","+std::to_string(index)+")");screen=Screen::Entry;}
          else{fieldEditor.wrapWith("convert(",","+std::to_string(index)+")");screen=fieldMenuReturn;}
        }
        menuParents.clear();menuOrigin=MenuOrigin::Main;
      }break;
    case Screen::Tools:
      if(action==ToolAction::Calc){
        const std::string parent=editor.text();
        const auto parsed=parse(parent);
        if(parsed.error!=Error::None){menuParents.clear();showError(parsed.error,parsed.position,Screen::Entry);break;}
        // First-occurrence discovery order, deduplicated: 3A+B prompts A then B.
        std::vector<int> found;
        std::function<void(const Expression&)> visit=[&](const Expression& n){if(!n)return;if(n->kind==Node::Kind::Symbol){int i=variableIndex(n->text);if(i>=0&&std::find(found.begin(),found.end(),i)==found.end())found.push_back(i);}for(auto& child:n->children)visit(child);};visit(parsed.root);
        if(found.empty()){menuParents.clear();screen=Screen::Entry;execute();}
        else{
          calc.parentSource=parent;
          calc.variables=std::move(found);
          calc.index=0;
          calc.phase=CalcPhase::Prompting;
          ++calc.generation;
          calc.hasLastResult=false;
          fieldEditor.begin();fieldView=Viewport{};syncCalcFieldStyle();
          menuParents.clear();screen=Screen::Calc;
        }
      }else if(action==ToolAction::VerifyToggle){verify()=!verify();history().clear();history().shrink_to_fit();retireCalc();editor.begin();hasResult=false;menuParents.clear();screen=Screen::Entry;}
      else{editor.undo();hasResult=false;menuParents.clear();screen=Screen::Entry;}break;
    case Screen::Format:
      if(formatHistory){
        // The viewed entry keeps the chosen view, like the live result does.
        if(historyCursor<history().size())history()[historyCursor].answer.displayedFormat=static_cast<Format>(action);
        historyScrollX=0;closeMenuToCaller();break;
      }
      resultFormat=static_cast<Format>(action);resultScrollX=0;historyScrollX=0;menuParents.clear();screen=Screen::Entry;break;
    case Screen::Settings:
      if(action==SettingAction::Reset){pushChild(Screen::Reset,-1);}
      else if(action==SettingAction::CalcSettingsGroup)pushChild(Screen::CalcSettings,-1);
      // System Setup is the device's page, not the calculator's: the host
      // shows it over this list (CalculatorApp::takeSystemSetup) and hands
      // back to it, so every app shares one page.
      else if(action==SettingAction::SystemSettingsGroup)systemSetupRequested=true;
      else if(action==SettingAction::GetStarted)pushChild(Screen::GetStarted,-1);
      break;
    case Screen::CalcSettings:
      {
        const auto options=settingsOptionRows(action,settings());
        pushChild(Screen::SettingsOption,action,currentOptionSelection(options,action));
      }break;
    case Screen::SettingsOption:{
      bool clearHistory=false;const IO before=settings().io;
      if(applySettingOption(settingOption,action,settings(),clearHistory)){
        if(clearHistory){
          // f/g survive an output-only change (MathMath<->MathDecimal); Math<->Line clears them.
          applyTransition(session(),inputStyleChanged(before,settings().io)?TransitionReason::InputStyleChanged:TransitionReason::OutputStyleChanged);
          retireCalc();editor.begin();hasResult=false;
        }
        syncInputStyle();
        changed();
      }
      // OK applies the highlighted value and returns to the root, whose
      // labels now show the new choice.
      if(popParent())break;
      screen=Screen::Settings;break;}
    case Screen::VariableAction:
      // Base-N recalls a distinct reference token: its A-F are hex digits.
      if(action==VariableAction::Recall){const std::string name(1,variableName(variable));insert((activeApp==AppId::BaseN?"var("+name+")":name).c_str());}
      else if(action==VariableAction::Store){
        const auto* roots=storableRoots();
        if(roots&&roots->size()>1){storeTarget=variable;storePickFromMenu=true;pushChild(Screen::StorePick,-1);break;}
        if(canStoreResult(hasResult?&result():nullptr,hasResult)){memory().variables[variable]=roots?roots->front().value:result().scalar();changed();}
        // Return to the same variable cell the action was opened from.
        if(popParent())break;
        screen=Screen::Variables;selection=static_cast<size_t>(variable);menuFirst=0;}
      else{fieldEditor.begin();fieldView=Viewport{};menuParents.clear();screen=Screen::Store;}break;
    case Screen::Functions:
      if(action==FunctionAction::F)insert("f()");
      else if(action==FunctionAction::G)insert("g()");
      else{
        const int want=action==FunctionAction::DefineF?0:1;
        if(menuOrigin!=MenuOrigin::Main){
          // Park the field draft; Define save/cancel restores it verbatim.
          stashField();
        }else defineMenuSelection=static_cast<int>(selection);
        definition=want;fieldEditor.begin(definition?memory().g:memory().f);fieldView=Viewport{};
        menuParents.clear();menuOrigin=MenuOrigin::Main;screen=Screen::Define;
      }break;
    case Screen::Reset:
      if(action==ResetAction::Cancel){if(popParent())break;menuParents.clear();screen=Screen::Entry;}
      else pushChild(Screen::ResetConfirm,action,1);
      break;
    case Screen::ResetConfirm:
      if(action==ConfirmAction::Yes){
        doReset(resetPending);
        // A reset clears the state the parent labels read; return to the
        // Settings root when it is still there, else fall back to Entry.
        if(popParent()&&popParent())break;
        menuParents.clear();screen=Screen::Entry;break;
      }
      // No dismisses the question and returns to the Reset options.
      if(popParent())break;
      screen=Screen::Reset;break;
    case Screen::StorePick:{
      const auto* roots=storableRoots();
      if(roots&&static_cast<size_t>(action)<roots->size()){memory().variables[storeTarget]=(*roots)[action].value;changed();}
      // From VARIABLE > Store: back to the variable's cell, as a direct Store
      // returns. From SHIFT+letter: back to the result, with a confirmation.
      if(storePickFromMenu){
        variable=storeTarget;
        if(popParent()&&screen==Screen::VariableAction&&popParent())break;
        menuParents.clear();screen=Screen::Variables;selection=static_cast<size_t>(variable);menuFirst=0;break;
      }
      menuParents.clear();screen=Screen::Entry;storeNotice(true);break;}
    default:break;
  }
}
// P5/E-11: the editor only needs Math-vs-Linear policy, never the full
// Input/Output setting. Synced on entry and whenever settings apply.
// Overwrite is Linear-only transient state: entering Math (or re-syncing)
// clears it so it can neither leak into Math inserts nor linger in the
// reported caret kind.
void syncInputStyle(){
  const bool math=settings().io==IO::MathMath||settings().io==IO::MathDecimal;
  editor.setMathInput(math);fieldEditor.setMathInput(math);
  editor.setScientificPower(settings().scientificPower);fieldEditor.setScientificPower(settings().scientificPower);
  editor.overwrite=false;fieldEditor.overwrite=false;
}
}  // namespace

size_t errorReturnCursor(const std::string& source,Error error,size_t position){
  EditDocument doc;doc.rebuild(source);
  position=error==Error::Math?source.size():std::min(position,source.size());
  // The canonical editor grammar is ASCII, but imported UTF-8 must not return
  // into continuation bytes even if the document regards each byte as a glyph.
  auto continuation=[&](size_t p){return p<source.size()&&(static_cast<unsigned char>(source[p])&0xc0)==0x80;};
  while(position&&continuation(position))--position;
  position=doc.normalize(position);
  while(continuation(position)||!doc.valid(position))++position;
  return position;
}
void enter(AppId app){
  // P3/E-04/E-05: root horizontal wrap belongs to the main entry editor only.
  // The Store/Define/CALC field editor keeps the default until a reference
  // case covers it.
  editor.wrapHorizontal=true;
  fieldEditor.wrapHorizontal=false;
  syncInputStyle();
  if(app!=activeApp){
    editor.begin();hasResult=false;entryView=Viewport{};activeApp=app;
    // A switch drops view state before pause(): pause() would otherwise fold a
    // stale HistoryView back into a result of the previous app.
    historyReturnEmpty=false;resultScrollX=0;historyScrollX=0;
    historyEntryView=Viewport{};screen=Screen::Entry;
  }
  // The Store/Define fields take i too: a complex value is stored like any other.
  editor.complexInput=fieldEditor.complexInput=app==AppId::Complex;
  editor.baseNInput=fieldEditor.baseNInput=app==AppId::BaseN;
  // (Re)opening the app is an ordinary exit for CALC: the prompt view never
  // survives it, and no repeat context leaks into the fresh entry.
  retireCalc();syncInputStyle();
  pause();screen=Screen::Entry;
}
void pause(){
  pendingEvaluation=false;revealErrorCaret=false;errorDialog.code=Error::None;
  editor.disarmIns();fieldEditor.disarmIns();
  // A suspended CALC final/result must not dangle: jobs are cancelled above,
  // so Running can never complete, and the repeat context does not cross a
  // Home exit or suspend. Mid-prompt fields keep their screen and context.
  if(calc.phase==CalcPhase::Running||calc.phase==CalcPhase::Result){
    retireCalc();
    if(screen==Screen::Entry)syncInputStyle();
  }
  // HistoryView is a view, not a draft: fold it back to the entry state it
  // came from so reopening shows entry/result, never a dangling selection.
  if(screen==Screen::Format&&formatHistory){formatHistory=false;screen=Screen::HistoryView;}
  if(screen==Screen::HistoryView){
    if(historyReturnEmpty){editor.begin();hasResult=false;}
    else hasResult=true;
    screen=Screen::Entry;historyReturnEmpty=false;historyScrollX=0;
  }
  menuParents.clear();menuOrigin=MenuOrigin::Main;menuOpenedByUser=false;fieldStash.clear();
  group=-1;tableGroup=-1;settingOption=-1;resetPending=-1;selection=0;menuFirst=0;
}
void interrupt(){editor.press(K::Interrupted);fieldEditor.press(K::Interrupted);}
void openShared(AppId app,K key,const ResultSummary* value){
  enter(app);editor.begin();hasResult=value!=nullptr;
  if(value){result().value=value->value;result().imag=value->imag;result().complex=value->complex;
    result().error=value->error;result().verified=false;result().pair=false;currentSummary=*value;
    // A retained CAS answer must not turn the handed value into its roots.
    result().formula.reset();result().solutions.reset();}
  press(key);
}
void openHomeSettings(){menu(Screen::Settings);}
bool takeSystemSetupRequest(){const bool value=systemSetupRequested;systemSetupRequested=false;return value;}
bool sharedActive(){return screen!=Screen::Entry;}
const std::string& sharedInput(){return editor.text();}
void press(K key){
  // SHIFT+x has no function in this build: it only clears SHIFT.
  if((screen==Screen::Entry||screen==Screen::Store||screen==Screen::Define||screen==Screen::Calc)&&key==K::X){
    MathEditor& input=screen==Screen::Entry?editor:fieldEditor;
    if(input.shifted){input.shifted=false;input.disarmIns();return;}
  }
  if(screen==Screen::ErrorDialog){
    if(key==K::Okay||key==K::Clear||key==K::Return)dismissError();
    return; // Hidden source is immutable; HOME remains owned by the shell.
  }
  if(screen==Screen::Aggregate){
    editor.disarmIns();fieldEditor.disarmIns();
    if(jobs().running()){if(key==K::Clear||key==K::Return)cancel();return;}
    const AggregateApp::Outcome o=AggregateApp::press(key);
    using Kind=AggregateApp::Outcome::Kind;
    if(o.kind==Kind::Evaluate)startJob(o.text,false,Screen::Aggregate);
    else if(o.kind==Kind::Close)screen=Screen::Entry;
    else if(o.kind==Kind::Clear){editor.begin();hasResult=false;screen=Screen::Entry;}
    else if(o.kind==Kind::Continue){editor.begin(o.text);hasResult=false;screen=Screen::Entry;editor.shifted=o.shift;editor.press(o.key);}
    return;
  }
  if(screen==Screen::Store||screen==Screen::Define||screen==Screen::Calc){
    if(jobs().running()){if(key==K::Clear||key==K::Return){cancel();fieldEditor.notice="Cancelled";}return;}
    if(screen==Screen::Calc&&key==K::Up&&calc.phase==CalcPhase::AwaitFinalExecute&&calc.index>0){
      --calc.index;calc.phase=CalcPhase::Prompting;
      fieldEditor.begin();fieldView=Viewport{};return;
    }
    if(screen==Screen::Calc&&key==K::Down&&calc.phase==CalcPhase::Prompting){
      submitCalcField();return;
    }
    if(key==K::Catalog){const bool quick=fieldEditor.shifted;fieldEditor.shifted=false;openFieldMenu(quick?Screen::QuickCatalog:Screen::Catalog);return;}
    if(key==K::Function){if(activeApp!=AppId::BaseN)openFieldMenu(Screen::Functions);return;}
    if(key==K::Return){
      // Cancel restores the caller: the variable grid cell, the parked field,
      // the Functions option, or the entry view for a CALC sequence. CALC
      // Return is an ordinary exit (never P14 Home): the repeat context dies.
      fieldEditor.disarmIns();
      if(screen==Screen::Store){screen=Screen::Variables;selection=static_cast<size_t>(variable);menuFirst=0;return;}
      if(screen==Screen::Define){
        if(stashedField()){unstashField();return;}
        screen=Screen::Functions;selection=static_cast<size_t>(defineMenuSelection);menuFirst=0;syncWindow(items().size());return;
      }
      retireCalc();syncInputStyle();
      screen=Screen::Entry;return;
    }
    if(key==K::Execute||key==K::Okay){
      fieldEditor.disarmIns();
      if(fieldEditor.review){fieldEditor.review=false;fieldEditor.notice="Check input; EXE confirms";return;}
      if(screen==Screen::Define){
        auto parsed=parse(fieldEditor.text());if(parsed.error!=Error::None){showError(parsed.error,parsed.position,Screen::Define);return;}
        (definition?memory().g:memory().f)=fieldEditor.text();changed();
        if(stashedField()){unstashField();return;}
        screen=Screen::Functions;selection=static_cast<size_t>(defineMenuSelection);menuFirst=0;syncWindow(items().size());return;
      }
      if(screen==Screen::Calc){submitCalcField();return;}
      startJob(fieldEditor.text(),false,screen);return;
    }
    if(screen==Screen::Calc){
      const std::string before=fieldEditor.text();
      fieldEditor.press(key);
      // Editing the committed last value reopens its prompt for correction;
      // the value stays visible and is recommitted by its EXE. EXE itself
      // takes the submit path above, never this one.
      if(calc.phase==CalcPhase::AwaitFinalExecute&&fieldEditor.text()!=before&&
         calc.index==calc.variables.size()&&calc.index>0){
        calc.phase=CalcPhase::Prompting;--calc.index;
      }
      return;
    }
    fieldEditor.press(key);return;
  }
  if(screen==Screen::Variables){
    if(key==K::Clear){if(menuOpenedByUser)closeMenuToCaller();return;}
    // Nine-cell grid, not a scrolling list: column-aware arrows, OK opens the
    // cell actions, Return leaves, and a digit/dot/minus starts a replacement
    // entry for the selected cell (manual page 44).
    if(key==K::Return){if(!popParent())closeMenuToCaller();return;}
    if(key==K::Left||key==K::Right||key==K::Up||key==K::Down){
      const GridMove move=key==K::Left?GridMove::Left:key==K::Right?GridMove::Right:key==K::Up?GridMove::Up:GridMove::Down;
      selection=static_cast<size_t>(moveVariableSelection(static_cast<int>(selection),move));
      variable=static_cast<int>(selection);return;
    }
    if(key==K::Okay||key==K::Execute){variable=static_cast<int>(selection);pushChild(Screen::VariableAction,variable);return;}
    if((key>=K::Digit0&&key<=K::Digit9)||key==K::Dot||key==K::Subtract){
      variable=static_cast<int>(selection);
      fieldEditor.begin();fieldEditor.press(key);fieldView=Viewport{};
      menuParents.clear();screen=Screen::Store;return;
    }
    return;
  }
  if(screen==Screen::HistoryView){
    // Read-only result-like view: Up/Down browse without editing, Left/Right
    // scroll an overflowing answer first and otherwise edit at end/start,
    // BACK edits at end, EXE re-executes through the normal job path, AC goes
    // to the pristine line. Unconfirmed keys are safe no-ops.
    if(jobs().running()){if(key==K::Clear||key==K::Return){cancel();editor.notice="Cancelled";}return;}
    if(history().empty()||historyCursor>=history().size()){clearEntry();return;}
    const size_t count=history().size();
    // SHIFT arms the S lamp here too, so SHIFT+EXE (Decimal) and SHIFT+FORMAT
    // work on the viewed entry exactly as on a live result. Navigation keys
    // spend it; a typed key carries it into the fresh line (SHIFT 7 = pi).
    if(key==K::Shift){editor.disarmIns();editor.shifted=!editor.shifted;return;}
    const bool shift=editor.shifted;
    if(key==K::Up||key==K::Down||key==K::Left||key==K::Right||key==K::Return||key==K::Clear||
       key==K::PageUp||key==K::PageDown||key==K::Interrupted)editor.shifted=false;
    if(key==K::Interrupted||key==K::PageUp||key==K::PageDown)return;
    if(key==K::Up){editor.disarmIns();if(historyCursor>0)showHistoryIndex(historyCursor-1);return;}
    if(key==K::Down){
      editor.disarmIns();
      if(historyCursor+1<count)showHistoryIndex(historyCursor+1);
      else if(historyReturnEmpty)clearEntry();
      else{screen=Screen::Entry;hasResult=true;historyReturnEmpty=false;resultScrollX=0;}
      return;
    }
    if(key==K::Left||key==K::Right){
      editor.disarmIns();
      // A-07: an overflowing answer scrolls first and never edits. BACK stays
      // the explicit edit-at-end route; the step is deterministic, its exact
      // physical cadence pending (PHYSICAL_LONG_RESULT_PENDING).
      const int16_t max=activeApp!=AppId::BaseN
          ?summaryMaxScroll(history()[historyCursor].answer,settings(),
                            history()[historyCursor].answer.displayedFormat):0;
      if(max>0){
        historyScrollX=key==K::Right?std::min<int16_t>(max,historyScrollX+kResultScrollStep)
                                    :std::max<int16_t>(0,historyScrollX-kResultScrollStep);
        return;
      }
      enterEditFromView(history()[historyCursor].source,key==K::Left);return;
    }
    if(key==K::Return){editor.disarmIns();enterEditFromView(history()[historyCursor].source,true);return;}
    if(key==K::Execute||key==K::Okay){
      // C56: re-execute through the normal job path, never a redraw of the
      // stored answer. The editor takes the historical source so adoption's
      // source guard still holds; success appends a new entry normally.
      editor.disarmIns();
      const std::string source=history()[historyCursor].source;
      screen=Screen::Entry;hasResult=false;historyReturnEmpty=false;
      editor.begin(source);editor.shifted=shift;execute();return;  // SHIFT+EXE: Decimal
    }
    if(key==K::Clear){editor.disarmIns();clearEntry();return;}
    if(key==K::Format){
      // FORMAT acts on the viewed entry in place (no re-execution, no new
      // history entry): the toggle flips its Standard/Decimal view, the menu
      // offers what its stored answer can show. Base-N cycles the base, which
      // the entry is drawn in as well.
      editor.disarmIns();editor.shifted=false;
      if(activeApp==AppId::BaseN){session().base=nextBase(session().base);return;}
      HistoryEntry& entry=history()[historyCursor];
      if(settings().formatMenu!=shift){
        if(formatRows(entry.answer,settings()).size()<=1)return;
        menu(Screen::Format);formatHistory=true;
        return;
      }
      entry.answer.displayedFormat=entry.answer.displayedFormat==Format::Decimal?Format::Standard:Format::Decimal;
      historyScrollX=0;return;
    }
    // Any other key starts a fresh line, as on a live result: + - x / (and
    // the power keys) continue from Ans, everything else types into a blank
    // line or opens its menu over one.
    editor.disarmIns();retireCalc();
    screen=Screen::Entry;hasResult=false;historyReturnEmpty=false;
    resultScrollX=0;historyScrollX=0;entryView=Viewport{};
    editor.begin();editor.shifted=shift;
    if(key==K::Add||key==K::Subtract||key==K::Multiply||key==K::Divide||key==K::Power||key==K::Square)editor.insert("Ans");
    press(key);return;
  }
  if(screen!=Screen::Entry){
    if(key==K::Clear){if(menuOpenedByUser)closeMenuToCaller();return;}
    // One-column menu navigation: Up/Down move, Right opens a submenu, Left
    // returns to the parent, Return pops one level (or closes to the caller),
    // OK activates, and paging steps by the visible four rows.
    if(key==K::Return){if(!popParent())closeMenuToCaller();return;}
    auto list=items();if(list.empty())return;
    syncWindow(list.size());
    if(screen==Screen::QuickCatalog){
      if(key==K::Down)selection=std::min(list.size()-1,selection+3);
      else if(key==K::Up)selection=selection>=3?selection-3:0;
      else if(key==K::Right)selection=std::min(list.size()-1,selection+1);
      else if(key==K::Left)selection=selection?selection-1:0;
      else if(key==K::PageDown)selection=std::min(list.size()-1,(selection/14+1)*14);
      else if(key==K::PageUp)selection=selection>=14?(selection/14-1)*14:0;
      else if(key==K::Okay||key==K::Execute)accept();
      else {int target=static_cast<int>(selection);if(menuShortcut(key,target,list.size())){selection=target;accept();}}
      return;
    }
    int quick=static_cast<int>(selection);
    if(menuShortcut(key,quick,list.size())){selection=static_cast<size_t>(quick);accept();return;}
    if(key==K::Down){selection=(selection+1)%list.size();syncWindow(list.size());return;}
    if(key==K::Up){selection=(selection+list.size()-1)%list.size();syncWindow(list.size());return;}
    if(key==K::Right){
      if(selection<list.size()&&list[selection].submenu)accept();
      return;}
    if(key==K::Left){
      if(!menuParents.empty())popParent();
      else closeMenuToCaller();
      return;}
    if(key==K::PageDown)selection=std::min(list.size()-1,selection+static_cast<size_t>(kMenuVisibleRows));
    else if(key==K::PageUp)selection=selection>=static_cast<size_t>(kMenuVisibleRows)?selection-kMenuVisibleRows:0;
    else if(key==K::Okay||key==K::Execute){accept();return;}
    else return;
    syncWindow(list.size());return;
  }
  if(jobs().running()) {
    if(key==K::Clear||key==K::Return){
      cancel();editor.notice="Cancelled";
      // A cancelled CALC final returns to the committed prompts; the repeat
      // lineage survives the cancellation.
      if(pendingOwner.calcKind==CalcJobKind::FinalExpression&&calc.phase==CalcPhase::Running){
        calc.phase=CalcPhase::AwaitFinalExecute;
        screen=Screen::Calc;syncCalcFieldStyle();
      }
    }
    return;
  }
  // Return on the entry screen never leaves Calculate: with a result it
  // edits the source at its end (A-13: Result.position is the P10 error
  // contract, never a success edit cursor), otherwise it does nothing, so
  // an idle press cannot silently change screens.
  // A store confirmation lasts one key.
  if(hasResult&&key!=K::Shift)storeNotice(false);
  // SHIFT then a letter key on a solve() answer stores a root into that
  // variable (SHIFT 4 = A, as the editor types it); several roots ask which.
  // Only here does SHIFT+letter not start a fresh line holding the letter.
  if(hasResult&&editor.shifted&&shiftedVariableIndex(key)>=0&&storableRoots()){
    editor.shifted=false;editor.disarmIns();
    storeTarget=shiftedVariableIndex(key);
    const auto* roots=storableRoots();
    if(roots->size()==1){memory().variables[storeTarget]=roots->front().value;changed();storeNotice(true);return;}
    menu(Screen::StorePick);storePickFromMenu=false;syncWindow(roots->size());return;
  }
  if(key==K::Return){editor.disarmIns();if(hasResult)enterEditFromView(editor.text(),true);return;}
  if(key==K::Execute||key==K::Okay){
    // EXE on a CALC result is the sole repeat path: reopen the value prompts
    // for the same immutable parent, starting blank at the first variable.
    // The visible result is abandoned (history keeps its entry); the next
    // final EXE will adopt anew.
    if(calc.phase==CalcPhase::Result&&calc.hasLastResult&&hasResult&&!calc.variables.empty()){
      calc.index=0;calc.phase=CalcPhase::Prompting;++calc.generation;
      calc.hasLastResult=false;hasResult=false;
      fieldEditor.begin();fieldView=Viewport{};syncCalcFieldStyle();
      screen=Screen::Calc;return;
    }
    execute();return;
  }
  if(key==K::Settings){menu(Screen::Settings);return;}
  if(key==K::Catalog){const bool quick=editor.shifted;editor.shifted=false;group=-1;menu(quick?Screen::QuickCatalog:Screen::Catalog);return;}
  // Matrix/Vector TOOLS is the named-slot list; Base-N has no TOOLS.
  // P5: the Base-N no-op still cancels transient INS like any other key.
  if(key==K::Tools){if(aggregateApp()){AggregateApp::openList(activeApp);screen=Screen::Aggregate;}else if(activeApp!=AppId::BaseN)menu(Screen::Tools);else{editor.disarmIns();fieldEditor.disarmIns();}return;}
  if(key==K::Variable){menu(Screen::Variables);if(variable<0||variable>=kVariableCount)variable=0;selection=static_cast<size_t>(variable);return;}
  if(key==K::Function){if(activeApp!=AppId::BaseN)menu(Screen::Functions);else{editor.disarmIns();fieldEditor.disarmIns();}return;}
  // Base-N FORMAT cycles the base; the retained result is shown in the new one.
  // P12 R-05: FORMAT with no applicable alternative does not open (preserve
  // screen/result, no empty menu). The toggle path (formatMenu==shifted)
  // always applies since Decimal is universal.
  if(key==K::Format){if(activeApp==AppId::BaseN){session().base=nextBase(session().base);editor.shifted=false;editor.disarmIns();return;}if(!hasResult){editor.disarmIns();editor.shifted=false;return;}const bool wantMenu=settings().formatMenu!=editor.shifted;editor.shifted=false;editor.disarmIns();if(wantMenu){if(formatRows(hasResult?&result():nullptr,hasResult,settings()).size()<=1)return;menu(Screen::Format);}else{resultFormat=resultFormat==Format::Decimal?Format::Standard:Format::Decimal;resultScrollX=0;}return;}
  // A-01: Up/Down browse history as stored expression plus stored answer,
  // without entering editing. From the live (newest) result UP steps to the
  // older entry; DOWN at newest is no movement. History clamps, never wraps.
  if(hasResult&&(key==K::Up||key==K::Down)&&!history().empty()){
    editor.disarmIns();
    if(key==K::Up){
      // The live result is the newest entry unless its insert was DROPped as
      // oversize (unreachable via the 256-char editor, but cheap to get
      // right): only step past the back when it actually is the live one.
      const bool liveIsNewest = history().back().source == editor.text();
      enterHistoryView(liveIsNewest&&history().size()>=2?history().size()-2:history().size()-1,false);
    }
    return;
  }
  // A-02/A-03: an empty line with history browses, however it became empty.
  // UP = newest entry with answer, LEFT/RIGHT recall its source, DOWN/Return do
  // nothing; C57 (AC after recall, recall again) needs no flag. With text
  // showing, arrows stay editor navigation.
  if(editor.text().empty()&&!hasResult&&!history().empty()){
    if(key==K::Up){editor.disarmIns();enterHistoryView(history().size()-1,true);return;}
    if(key==K::Down||key==K::Return){editor.disarmIns();return;}
    if(key==K::Shift){editor.press(key);return;}
    if(key==K::Left||key==K::Right){
      editor.disarmIns();enterEditFromView(history().back().source,key==K::Left);return;
    }
  }
  // C47 short results edit at end/start; A-07 long results scroll their
  // viewport first and never edit on Left/Right. AC reaps to pristine; any
  // other key starts a fresh expression (Ans-inserting operators included).
  if(hasResult && key!=K::Shift){
    editor.disarmIns();
    if(key==K::Left||key==K::Right){
      const int16_t max=activeApp!=AppId::BaseN
          ?summaryMaxScroll(currentSummary,settings(),resultFormat):0;
      if(max>0){
        resultScrollX=key==K::Right?std::min<int16_t>(max,resultScrollX+kResultScrollStep)
                                   :std::max<int16_t>(0,resultScrollX-kResultScrollStep);
        return;
      }
      enterEditFromView(editor.text(),key==K::Left);return;
    }
    if(key==K::Clear){clearEntry();return;}
    bool shift=editor.shifted;editor.begin();editor.shifted=shift;
    if(key==K::Add||key==K::Subtract||key==K::Multiply||key==K::Divide||key==K::Power||key==K::Square)editor.insert("Ans");
    retireCalc();hasResult=false;resultScrollX=0;
  }
  editor.press(key);
  // Retention owns clearing: a history view must never outlive usable history.
  if(history().empty()&&screen==Screen::HistoryView)screen=Screen::Entry;
  else if(screen==Screen::HistoryView&&historyCursor>=history().size())historyCursor=history().size()-1;
}
void adopt(const Job& job,Result& output){
  const Screen destination=static_cast<Screen>(job.destination);
  if(!pendingEvaluation)return;
  pendingEvaluation=false;
  if(job.app!=activeApp||destination!=screen||pendingOwner.origin!=destination||!ownsCurrent(pendingOwner))return;
  const bool calcPromptJob=destination==Screen::Calc&&pendingOwner.calcKind==CalcJobKind::PromptValue;
  if(destination!=Screen::Aggregate&&job.request.expression!=
     (calcPromptJob?calcPromptSource():fieldScreen(destination)?fieldEditor.text():editor.text()))return;
  if(destination==Screen::Aggregate){
    if(output.error!=Error::None)AggregateApp::cellError(output.error);else AggregateApp::commitCell(output.value);
    return;
  }
  if(destination!=Screen::Entry){
    if(output.error!=Error::None){showError(output.error,output.position,destination);return;}
    const bool calcPrompt = destination==Screen::Calc&&pendingOwner.calcKind==CalcJobKind::PromptValue;
    if(destination==Screen::Calc&&!calcPrompt){retireCalc();syncInputStyle();screen=Screen::Entry;return;}
    if(calcPrompt&&(calc.phase!=CalcPhase::Prompting||calc.generation!=pendingOwner.calcGeneration||
                    calc.index>=calc.variables.size()))
      return;  // stale prompt value: never store, never advance
    // CALC values commit to calculator variable memory (established behavior);
    // only the final parent evaluation may update Ans/history through the
    // ordinary result path below. Prompt accepts touch neither.
    memory().variables[calcPrompt?calc.variables[calc.index]:variable]=output.scalar();changed();
    if(calcPrompt){
      if(++calc.index<calc.variables.size()){fieldEditor.begin();fieldView=Viewport{};}
      // The last value only commits here: evaluation waits for the explicit
      // final EXE (AwaitFinalExecute). The value stays visible; it is not
      // calculated yet, and no history entry is created for any prompt.
      else calc.phase=CalcPhase::AwaitFinalExecute;
      return;
    }
    screen=Screen::Variables;selection=static_cast<size_t>(variable);menuFirst=0;
    return;
  }
  // P13: a CALC final runs the immutable parent through this ordinary Entry
  // path so Ans/history stay on the P12 semantics. Stale finals (repeat
  // started, context retired, source changed) adopt nothing.
  const bool calcFinal = pendingOwner.calcKind==CalcJobKind::FinalExpression;
  if(calcFinal&&(calc.phase!=CalcPhase::Running||calc.generation!=pendingOwner.calcGeneration||
                 job.request.expression!=calc.parentSource))
    return;
  if(output.error==Error::None&&output.aggregate){
    // A matrix/vector outcome replaces MatAns/VctAns and shows its grid; no replay.
    (activeApp==AppId::Vector?session().aggregates.vectors:session().aggregates.matrices)[kAnswerSlot]=*output.aggregate;
    changed();hasResult=false;AggregateApp::openAnswer(activeApp);screen=Screen::Aggregate;return;
  }
  // Matrix/Vector retain their independently owned error presentation.
  if(output.error!=Error::None&&!aggregateApp()){
    // P10 failures never enter history. A failed CALC final also retires the
    // repeat context: the ordinary Entry error owns the screen afterwards.
    if(calcFinal)retireCalc();
    hasResult=false;showError(output.error,output.position,Screen::Entry);return;
  }
  // Successful EXE: adopt, refresh Ans/memory, store the compact {source,
  // answer} entry (oversize DROPs; the live Result still shows). The execution
  // format comes from the job (SHIFT+EXE, R-01) and travels with the entry;
  // later FORMAT views stay current-result state.
  result()=std::move(output);hasResult=true;historyReturnEmpty=false;
  if(screen==Screen::HistoryView)screen=Screen::Entry;
  resultFormat=job.execFormat;resultScrollX=0;historyScrollX=0;
  currentSummary=toSummary(result(),job.execFormat);
  if(result().error==Error::None){
    memory()=result().memory;changed();
    if(!aggregateApp()){  // Matrix and Vector have no replay history
      HistoryEntry entry{job.request.expression,currentSummary};
      pushHistory(history(),std::move(entry));  // 8 KB cap, oldest-first eviction
      historyCursor=history().empty()?0:history().size()-1;
    }
    // A successful CALC final keeps its repeat context: EXE on this result
    // reopens the prompts (the sole repeat path). The typed P12 presentation
    // above is the only result rendering; CALC adds no formatter of its own.
    if(calcFinal){calc.phase=CalcPhase::Result;calc.hasLastResult=true;}
  }else editor.setCursor(result().position);
}
// Caret blink: Menus, results and busy screens have no caret and never wake
// for the blink.
bool caretVisible(){
  if(screen==Screen::Aggregate)return false;  // its cell draft shows a static cursor
  if(screen==Screen::HistoryView)return false;  // read-only view: no caret, like a result
  return (screen==Screen::Entry||screen==Screen::Store||screen==Screen::Define||screen==Screen::Calc)
      &&!jobs().running()&&!(screen==Screen::Entry&&hasResult);
}
#ifdef CALCULATOR_REFERENCE_TEST
// Host-only observation: caret kinds come from lastRender(), so callers must
// service()/drain() after any kind-changing key before calling snapshot().
CalculatorApp::Snapshot snapshot(){
  if(screen==Screen::Aggregate)return AggregateApp::snapshot();
  CalculatorApp::Snapshot out;
  static const char* names[]={"Entry","Catalog","Tools","Settings","SettingsOption","Format","Variables","VariableAction","Functions","Store","Define","Calc","Reset","ResetConfirm","Constants","Conversions","Aggregate","CalcSettings","SystemSettings","ErrorDialog","HistoryView","QuickCatalog","GetStarted","StorePick"};
  out.screen=names[static_cast<unsigned>(screen)];
  if(screen==Screen::Entry&&hasResult)out.screen="Result";
  // HistoryView shows the stored source plus the stored answer, like a result.
  // Clamp defensively: retention owns clearing and the controller clamps on
  // every key, so an empty history here only means a torn-down session.
  const HistoryEntry* viewed=nullptr;
  if(screen==Screen::HistoryView&&!history().empty()){
    if(historyCursor>=history().size())historyCursor=history().size()-1;
    viewed=&history()[historyCursor];
  }
  const bool field=fieldScreen(screen)||(screen==Screen::ErrorDialog&&fieldScreen(errorDialog.owner.origin));
  const bool math=screen==Screen::Entry||field||viewed;
  const MathEditor& e=field?fieldEditor:editor;
  if(screen==Screen::ErrorDialog){out.errorTitle=errorDialog.title;out.errorCursor=errorDialog.cursor;out.errorOrigin=names[static_cast<unsigned>(errorDialog.owner.origin)];}
  out.text=viewed?viewed->source:e.text();
  out.cursor=viewed?viewed->source.size():e.cursor();
  out.notice=e.notice;
  EditDocument doc;doc.rebuild(out.text);
  static const char* roles[]={"Root","Group","Argument","Base","Exponent","Numerator","Denominator","Whole","Radicand","Index","Body","Lower","Upper","Point","Operand"};
  out.fieldRole=roles[static_cast<unsigned>(doc.field(doc.slotField(doc.normalize(out.cursor))).role)];
  out.hasResult=hasResult||viewed;out.historyCursor=historyCursor;
  out.selection=selection;out.first=menuFirst;
  // P13 CALC observation: explicit phase/index/parent, never inferred from
  // source text. CalcResult marks the repeat-capable Entry result.
  static const char* calcPhases[]={"Inactive","Prompting","AwaitFinalExecute","Running","Result"};
  out.calcPhase=calcPhases[static_cast<unsigned>(calc.phase)];
  out.calcIndex=calc.index;out.calcCount=calc.variables.size();
  out.calcParent=calc.parentSource;
  out.calcResult=screen==Screen::Entry&&hasResult&&calc.phase==CalcPhase::Result&&calc.hasLastResult;
  out.caret=math&&lastRender().caret&&caretVisible();
  if(out.caret)out.caretRect=lastRender().caretRect;
  // P8: observe the kind delivered to rendering; exact alternate raster
  // shapes remain pending capture. Menus/results stay Hidden as before.
  if(!out.caret)out.caretKind="Hidden";
  else if(lastRender().caretKind==MathEditor::CaretKind::InsArmed)out.caretKind="InsArmed";
  else if(lastRender().caretKind==MathEditor::CaretKind::Overwrite)out.caretKind="Overwrite";
  else out.caretKind="Insert";
  if(math)out.logicalCaretKind=lastRender().caretKind==MathEditor::CaretKind::InsArmed?"InsArmed":
      lastRender().caretKind==MathEditor::CaretKind::Overwrite?"Overwrite":"Insert";
  // P12: HistoryView reproduces its execution format; live uses current view.
  if(viewed)out.resultText=summaryText(viewed->answer,settings(),viewed->answer.displayedFormat);
  else if(hasResult)out.resultText=summaryText(currentSummary,settings(),resultFormat);
  if(activeApp==AppId::BaseN&&hasResult)out.resultText=lastRender().presented;
  if(!math&&screen!=Screen::ErrorDialog){
    out.menuScreen=out.screen;
    // These builders are pure; items() also rebuilds table indices for some
    // menus, so never call it from the observation accessor.
    std::vector<MenuItem> rows;
    if(screen==Screen::Catalog)rows=group<0?catalogRootRows(verify(),activeApp):catalogCategoryRows(group);
    if(screen==Screen::Functions)rows=functionRows();
    if(screen==Screen::Settings)rows=settingsRootRows(settings());
    if(screen==Screen::CalcSettings)rows=calcSettingsRows(settings());
    if(screen==Screen::SettingsOption)rows=settingsOptionRows(settingOption,settings());
    if(screen==Screen::Tools)rows=toolRows(verify(),activeApp==AppId::Calculate);
    if(screen==Screen::Format)rows=formatMenuRows();
    if(screen==Screen::StorePick)rows=storePickRows();
    for(const auto& row:rows)out.menuLabels.push_back(row.label);
  }
  return out;
}
#endif
std::vector<uint8_t> render(bool caretOn){
  if(screen==Screen::GetStarted)return renderGetStarted();
  if(screen==Screen::QuickCatalog)return renderQuickCatalog(quickCatalogEntries(verify(),activeApp),static_cast<int>(selection));
  if(screen==Screen::ErrorDialog)return renderErrorDialog(errorDialog.code);
  if(revealErrorCaret){caretOn=true;revealErrorCaret=false;}
  const bool busy=jobs().running();
  if(screen==Screen::Aggregate)return AggregateApp::render();
  // HistoryView reuses the ordinary result/history render path with the stored
  // source and stored answer: same expression geometry, same answer anchoring,
  // no caret. A local editor carries the historical source; the live editor
  // is untouched until an explicit edit transition.
  if(screen==Screen::HistoryView){
    if(history().empty()){screen=Screen::Entry;}
    else{
      if(historyCursor>=history().size())historyCursor=history().size()-1;
      const HistoryEntry& entry=history()[historyCursor];
      MathEditor viewed;viewed.begin(entry.source);
      if(activeApp==AppId::BaseN)return renderBaseN(viewed,session().base,&entry.answer,busy,historyEntryView,false);
      // P12: stored execution format, not the live FORMAT view.
      return renderResultSummary(viewed,settings(),&entry.answer,entry.answer.displayedFormat,busy,verify(),historyEntryView,"",false,&historyScrollX);
    }
  }
  if(screen==Screen::Entry&&activeApp==AppId::BaseN)return renderBaseN(editor,session().base,hasResult?&currentSummary:nullptr,busy,entryView,caretOn);
  if(screen==Screen::Entry)return renderResultSummary(editor,settings(),hasResult?&currentSummary:nullptr,resultFormat,busy,verify(),entryView,"",caretOn,&resultScrollX);
  if(screen==Screen::Variables){
    std::array<std::string,9> labels;
    for(int i=0;i<9;++i)labels[static_cast<size_t>(i)]=variableLabel(i,memory().variables[i],settings());
    return renderVariableGrid(labels,static_cast<int>(selection));
  }
  if(screen==Screen::Store)return renderAssignment(std::string(1,variableName(variable))+"=",fieldEditor,settings(),busy,fieldView,caretOn);
  if(screen==Screen::Define)return renderAssignment(definition?"g(x)=":"f(x)=",fieldEditor,settings(),busy,fieldView,caretOn);
  if(screen==Screen::Calc){
    // Corrupt CALC state can never index OOB: fall back to the entry view
    // instead of painting a prompt for no variable. The parent renders in
    // the global Input/Output (natural when Math); only the value field is
    // forced Linear. AwaitFinalExecute selects the bottom Execute action.
    const bool awaiting=calc.phase==CalcPhase::AwaitFinalExecute;
    const bool prompting=calc.phase==CalcPhase::Prompting&&calc.index<calc.variables.size();
    if(calc.variables.empty()||(!prompting&&!awaiting)){
      retireCalc();syncInputStyle();screen=Screen::Entry;
      return renderResultSummary(editor,settings(),hasResult?&currentSummary:nullptr,resultFormat,busy,verify(),entryView,"",caretOn,&resultScrollX);
    }
    const size_t labelIdx=awaiting?calc.variables.size()-1:calc.index;
    Settings fieldSettings=settings();fieldSettings.io=IO::LineDecimal;
    return renderCalcPrompt(calc.parentSource,settings(),
                            std::string(1,variableName(calc.variables[labelIdx]))+"=",
                            fieldEditor,fieldSettings,busy,fieldView,caretOn,awaiting,
                            formatValue(memory().variables[calc.variables[labelIdx]].re,fieldSettings));
  }
  if(screen==Screen::StorePick){
    // "Store in A" over the roots, painted like the Reset question: the info
    // row shifts the selection by one in the owned window.
    const auto options=items();
    if(!options.empty()){
      if(selection>=options.size())selection=options.size()-1;
      menuFirst=UiKit::ensureVisible(menuFirst,static_cast<int>(selection)+1,static_cast<int>(options.size())+1,kMenuVisibleRows);
      return renderConfirm(std::string("Store in ")+variableName(storeTarget),options,static_cast<int>(selection)+1,menuFirst);
    }
  }
  if(screen==Screen::ResetConfirm){
    const auto options=items();
    // The painted list is the question plus the options, so the window is
    // owned against that full size with the selection offset past the info
    // row. The confirm is short enough that first stays zero in practice.
    if(selection>=options.size())selection=options.size()-1;
    menuFirst=UiKit::ensureVisible(menuFirst,static_cast<int>(selection)+1,static_cast<int>(options.size())+1,kMenuVisibleRows);
    return renderConfirm(resetQuestion(resetPending),options,static_cast<int>(selection)+1,menuFirst);
  }
  // Bare calculator lists: no title, no rule, four large rows. The window
  // is caller-owned so a filtered list never loses its scroll position.
  const auto list=items();
  syncWindow(list.size());
  return renderMenuList(list,static_cast<int>(selection),menuFirst);
}
PanelIcons::State icons(){
  IconView view;
  if(screen==Screen::Entry||screen==Screen::HistoryView||(screen==Screen::Aggregate&&AggregateApp::gridShown())){
    view.calculation=true;
    view.angle=settings().angle;
    view.io=settings().io;
    view.number=settings().number;
    view.engineer=settings().engineer;
    view.verify=verify();
    view.shifted=editor.shifted;
    // I-06/I-01: arrows mirror press()'s history-browse precondition, so a lit
    // arrow always means Up/Down browses: an empty line with history lights UP;
    // non-empty editing lights neither. Pinned asymmetries: a sole entry keeps
    // UP lit (C44/C54-2), and DOWN from newest returns to the live result or
    // empty line (C53/C54) while unlit. Segments stay off; raster arrows (I-04).
    if(screen==Screen::HistoryView&&!history().empty()){
      if(historyCursor>=history().size())historyCursor=history().size()-1;
      // I-06 for 2+ entries: newest UP-only, oldest DOWN-only, middle both.
      // A sole entry keeps the single-result replay indicator (C44/C54-2);
      // UP there stays clamped with no movement.
      view.historyPrev=historyCursor>0||history().size()==1;
      view.historyNext=historyCursor+1<history().size();
    }else if(hasResult){
      view.historyPrev=!history().empty();
      view.historyNext=false;
    }else if(editor.text().empty()&&!history().empty()){
      view.historyPrev=true;
      view.historyNext=false;
    }
    view.complexApp=activeApp==AppId::Complex;
    view.complexResult=settings().complexResult;
    view.baseN=activeApp==AppId::BaseN;
    view.aggregate=aggregateApp();
  }else if(screen==Screen::Store||screen==Screen::Define||screen==Screen::Calc){
    view.field=true;
    // Parent expression's IO, not CALC's forced-linear field IO: a math
    // parent keeps MATHIO lit under a linear prompt.
    view.angle=settings().angle;
    view.io=settings().io;
    view.number=settings().number;
    view.engineer=settings().engineer;
    view.verify=verify();
    view.shifted=fieldEditor.shifted;
    view.complexApp=activeApp==AppId::Complex;
    view.complexResult=settings().complexResult;
    view.baseN=activeApp==AppId::BaseN;
    view.aggregate=aggregateApp();
  }
  auto state=desiredIcons(view);
  if(screen==Screen::Calc){
    state.arrUp=calc.phase==CalcPhase::AwaitFinalExecute;
    state.arrDown=calc.phase==CalcPhase::Prompting;
  }
  return state;
}
}  // namespace CalculateApp
}  // namespace Calculator
