#include "calculator/runtime/calculator_storage.h"
#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/runtime/calculator_app.h"
#include "calculator/apps/calculator_calculate.h"
#include "calculator/apps/calculator_distribution.h"
#include "calculator/apps/calculator_equation.h"
#include "calculator/apps/calculator_ratio.h"
#include "calculator/apps/calculator_statistics.h"
#include "calculator/apps/calculator_spreadsheet.h"
#include "calculator/apps/calculator_table.h"
#include "calculator/ui/calculator_home.h"
#include "calculator/runtime/calculator_clock.h"
#include "calculator/apps/calculator_mathbox.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/runtime/calculator_job.h"
#include "calculator/runtime/calculator_platform.h"
#include "calculator/ui/calculator_render.h"
#include "calculator/runtime/calculator_session.h"
#include "calculator/runtime/calculator_store.h"
#include "ui/canvas_text.h"

// The overlay shell: Home, the not-yet-implemented notice, the shared session,
// the single job service and debounced persistence. Everything an app draws
// or does lives in its controller (calculator_calculate.* today).
namespace CalculatorApp {
namespace {
using namespace Calculator;
using K=ExpressionInput::Key;
// MathBox is the shell's accessory screen (calculator_mathbox.h), not a session app.
enum class Screen : uint8_t {Home,Notice,App,MathBox};
Screen screen=Screen::Home;
bool opened=false,dirty=true,loaded=false;
// True once an app has been entered since the last fresh Home entry.
// Home-on-Home resumes that paused app; without one Home stays up.
bool hasPausedApp=false;
// Quick calculate (openQuick): the calculator's resting face (boot,
// power-on), Calculate straight from the shared session. Cleared by Home.
bool quick=false;
// Home was pressed over quick calculate: Home-on-Home resumes it as quick.
// Cleared by launching another app and by suspend().
bool quickPaused=false;
// Math Box was the last thing open: Home-on-Home goes back to it. Cleared by
// a fresh entry and by launching an app.
bool mathBoxPaused=false;
// Where the Setting key's list returns: Home, or Math Box over which it opened.
Screen settingsReturn=Screen::Home;
// Countdown alert modal (Calculator::countdown()): minutes left, 0 = time up,
// -1 = none. It sits over whatever screen is up and any key closes it.
int alertMinutes=-1;
HomeMenu home;
std::string notice;
CalculatorSession& session() { return calculatorSession(); }
JobService& jobs() { return calculatorJobs(); }
// Caret blink phase (~1 Hz full cycle): visibility only, never geometry.
// Flips mark the frame dirty so the loop presents the new phase.
bool caretBlinkOn=true,caretBlinking=false;
constexpr uint32_t kCaretBlinkMs=500;
// Persistence waits until the session has been unchanged this long, so a run
// of edits costs one NVS write.
constexpr uint32_t kPersistDelayMs=2000;
uint32_t savedRevision=0,seenRevision=0,changedAt=0;
static_assert(static_cast<unsigned>(AppId::Calculate)==kCalculateIndex&&static_cast<unsigned>(AppId::MathBox)==kMathBoxIndex);

bool equationApp(AppId app){return app==AppId::Equation||app==AppId::Inequality;}
// Apps with a local implementation; the others still show the notice.
bool localApp(AppId app){
  return app==AppId::Ratio||app==AppId::Table||app==AppId::Calculate||app==AppId::Complex||app==AppId::BaseN||app==AppId::Matrix||app==AppId::Vector||app==AppId::Statistics||app==AppId::Spreadsheet||app==AppId::Distribution||equationApp(app);
}
void persist(){
  if(session().revision==savedRevision)return;
  // A failed write keeps the revision unsaved and retries after the delay.
  if(saveStoredState(session()))savedRevision=session().revision;
  else changedAt=CalculatorPlatform::millis();
}
void load(){
  if(loaded)return;loaded=true;
  loadStoredState(session());
  savedRevision=seenRevision=session().revision;
}
// Moves a pending countdown alert into the modal (a newer one replaces one
// still showing: the latest milestone is the one that matters).
bool adoptAlert(){
  Countdown& run=countdown();
  run.service(CalculatorPlatform::millis());
  if(run.pendingAlert()<0)return false;
  alertMinutes=run.pendingAlert();run.clearAlert();
  CalculatorPlatform::logf("[CLOCK] Countdown alert: %d min left.\n",alertMinutes);
  return true;
}
}  // namespace

bool active(){return opened;}
#ifdef CALCULATOR_REFERENCE_TEST
Snapshot snapshot(const std::vector<uint8_t>& lastFrame){
  Snapshot out;
  if(opened&&screen==Screen::App&&session().app==AppId::Ratio)out=RatioApp::snapshot();
  else if(opened&&screen==Screen::App&&session().app==AppId::Table)out=TableApp::snapshot();
  else if(opened&&screen==Screen::App&&session().app==AppId::Statistics)out=StatisticsApp::snapshot();
  else if(opened&&screen==Screen::App&&session().app==AppId::Spreadsheet)out=SpreadsheetApp::snapshot();
  else if(opened&&screen==Screen::App&&session().app==AppId::Distribution)out=DistributionApp::snapshot();
  else if(opened&&screen==Screen::App&&!equationApp(session().app))out=CalculateApp::snapshot();
  else if(opened&&screen==Screen::App)out=EquationApp::snapshot();
  else if(opened&&screen==Screen::MathBox)out.screen=MathBoxApp::screenName();
  else out.screen=!opened?"Suspended":screen==Screen::Home?"Home":screen==Screen::Notice?"Notice":"UnsupportedApp";
  if(opened&&alertMinutes>=0)out.screen="Alert";
  if(screen==Screen::Notice)out.notice=notice;
  out.icons=desiredIcons();out.frame=lastFrame;out.busy=jobs().running();
  out.appId=static_cast<int>(session().app);out.base=session().base;
  return out;
}
#endif
bool homeSettings=false;
bool isHome(){return opened&&screen==Screen::Home;}
bool takeSystemSetup(){
  if(!CalculateApp::takeSystemSetupRequest())return false;
  persist();return true;
}
void redraw(){if(opened)dirty=true;}
static void enterHomeMenu(bool fresh){
  load();jobs().cancel();CalculateApp::pause();EquationApp::pause();
  if(session().app==AppId::Ratio)RatioApp::pause(true);
  if(session().app==AppId::Table)TableApp::pause();
  if(session().app==AppId::Statistics)StatisticsApp::pause();if(session().app==AppId::Spreadsheet)SpreadsheetApp::pause();
  if(session().app==AppId::Distribution)DistributionApp::pause();
  applyTransition(session(),TransitionReason::HomeOpen);
  if(screen==Screen::MathBox)mathBoxPaused=true;
  if(quick)quickPaused=true;
  opened=true;screen=Screen::Home;dirty=true;quick=false;
  // A fresh entry starts with no paused app, so Home-on-Home keeps the menu
  // up. Toggling out of a running app preserves the resume target.
  if(fresh){hasPausedApp=false;mathBoxPaused=false;quickPaused=false;}
}
void openHome(){
  enterHomeMenu(true);
}
void showHome(){
  enterHomeMenu(false);
}
// No HomeOpen transition, which is what clears the replay history: the last
// work is still there, as it is when Home toggles back into Calculate. Another
// app paused underneath is left exactly as Home > Calculate would leave it.
// The Home cursor follows quick calculate to the Calculate cell.
static void openQuickAs(unsigned cell){
  load();jobs().cancel();QuickOverlay::close();homeSettings=false;
  launchApp(session(),AppId::Calculate);
  CalculateApp::enter(AppId::Calculate);
  opened=true;screen=Screen::App;dirty=true;hasPausedApp=true;quick=true;quickPaused=false;mathBoxPaused=false;
  // The Home menu's cursor follows how it was opened, not whichever cell an
  // earlier session left it on; the window scrolls as HomeMenu::press would.
  home.selected=cell;home.top=cell/3?cell/3-1:0;
}
void openQuick(){openQuickAs(kCalculateIndex);}
bool alertDue(){
  if(alertMinutes>=0)return true;
  Countdown& run=countdown();run.service(CalculatorPlatform::millis());
  return run.pendingAlert()>=0;
}
bool alertActive(){return opened&&alertMinutes>=0;}
bool countdownRunning(){return countdown().running();}
bool countdownBlocksAutoPowerOff(){return countdown().running()||countdown().pendingAlert()>=0||alertMinutes>=0;}
uint32_t countdownWakeMs(){return countdown().msUntilNextAlert(CalculatorPlatform::millis());}
// Re-enter the paused app without a launch transition: the same dispatch as
// picking the running app on the Home menu, so drafts and datasets survive.
// Statistics/Table/Distribution enter() with no relaunch arg; Ratio/Equation
// take relaunch=false explicitly.
static void resumeApp(){
  const AppId app=session().app;
  if(app==AppId::Ratio)RatioApp::enter(false);
  else if(app==AppId::Table)TableApp::enter();
  else if(app==AppId::Statistics)StatisticsApp::enter();
  else if(app==AppId::Spreadsheet)SpreadsheetApp::enter();else if(app==AppId::Distribution)DistributionApp::enter();
  else if(equationApp(app))EquationApp::enter(app,false);
  else CalculateApp::enter(app);
  screen=Screen::App;dirty=true;
}
void suspend(){QuickOverlay::close();homeSettings=false;quick=false;quickPaused=false;alertMinutes=-1;mathBoxPaused=false;jobs().cancel();CalculateApp::pause();EquationApp::pause();if(session().app==AppId::Ratio)RatioApp::pause();if(session().app==AppId::Table)TableApp::pause();if(session().app==AppId::Statistics)StatisticsApp::pause();if(session().app==AppId::Spreadsheet)SpreadsheetApp::pause();if(session().app==AppId::Distribution)DistributionApp::pause();persist();opened=false;dirty=false;hasPausedApp=false;}
void press(K key){
  if(!opened)return;dirty=true;
  // The countdown alert takes every key (Home included) and only closes: the
  // key that dismisses it never reaches the screen underneath. A keypad
  // discontinuity is not a press.
  if(alertMinutes>=0){
    if(key==K::Interrupted)return;
    alertMinutes=-1;
    return;
  }
  if(homeSettings){
    if(key==K::Home){homeSettings=false;screen=Screen::Home;return;}
    CalculateApp::press(key);if(!CalculateApp::sharedActive()){homeSettings=false;screen=settingsReturn;}return;
  }
  if(key==K::Home){
    QuickOverlay::close();
    // Home toggles between the last app and its menu: from an app (or the
    // notice) it shows the menu, from the menu it resumes the paused app.
    // With no app visited since the last fresh entry the menu stays up.
    // Quick calculate resumes as quick.
    if(screen==Screen::Home){
      if(mathBoxPaused){MathBoxApp::enter(false);screen=Screen::MathBox;mathBoxPaused=false;return;}
      if(hasPausedApp&&quickPaused){openQuickAs(kCalculateIndex);return;}
      if(hasPausedApp&&localApp(session().app)){resumeApp();return;}
      return;
    }
    showHome();return;
  }
  if(QuickOverlay::active()){QuickOverlay::press(key);return;}
  if(key==K::Interrupted){if(session().app==AppId::Ratio)RatioApp::press(key);else if(session().app==AppId::Table)TableApp::press(key);else if(session().app==AppId::Statistics)StatisticsApp::press(key);else if(session().app==AppId::Spreadsheet)SpreadsheetApp::press(key);else if(session().app==AppId::Distribution)DistributionApp::press(key);else if(equationApp(session().app))EquationApp::interrupt();else CalculateApp::interrupt();jobs().cancel();return;}
  if(screen==Screen::MathBox){
    // Setting opens the shared Settings list as it does over Home (System
    // Setup, Use Casio MCU); Math Box itself has nothing to configure.
    if(key==K::Settings){CalculateApp::openHomeSettings();homeSettings=true;settingsReturn=Screen::MathBox;return;}
    MathBoxApp::press(key);return;
  }
  if(screen==Screen::Home){
    if(key==K::Settings){CalculateApp::openHomeSettings();homeSettings=true;settingsReturn=Screen::Home;return;}
    // Return on the Home menu is a no-op.
    if(key==K::Return)return;
    // Every app in localApp() has an implementation; any other Casio cell
    // shows the not-implemented notice.
    const int shortcut=shortcutIndex(key);
    if(shortcut>=0){home.press(key);key=K::Okay;}
    if(key==K::Okay||key==K::Execute){
      const AppId app=appAtHomeIndex(home.selected);
      if(localApp(app)){
        const bool relaunch=session().app!=app;
        launchApp(session(),app);
        if(app==AppId::Ratio)RatioApp::enter(relaunch);else if(app==AppId::Table)TableApp::enter();else if(app==AppId::Statistics)StatisticsApp::enter();else if(app==AppId::Spreadsheet)SpreadsheetApp::enter();else if(app==AppId::Distribution)DistributionApp::enter();else if(equationApp(app))EquationApp::enter(app,relaunch);else CalculateApp::enter(app);
        screen=Screen::App;hasPausedApp=true;mathBoxPaused=false;quickPaused=false;
      }
      // Math Box: no launchApp -- it is not a session app, so the paused
      // app keeps PreAns/Verify and Home-on-Home still reaches it later.
      else if(app==AppId::MathBox){MathBoxApp::enter(true);screen=Screen::MathBox;}
      else{notice="Not implemented yet";screen=Screen::Notice;}
    }else home.press(key);
    return;
  }
  if(screen==Screen::Notice){if(key==K::Return||key==K::Okay||key==K::Execute)screen=Screen::Home;return;}
  if(session().app==AppId::Ratio)RatioApp::press(key);else if(session().app==AppId::Table)TableApp::press(key);else if(session().app==AppId::Statistics)StatisticsApp::press(key);else if(session().app==AppId::Spreadsheet)SpreadsheetApp::press(key);else if(session().app==AppId::Distribution)DistributionApp::press(key);else if(equationApp(session().app))EquationApp::press(key);else CalculateApp::press(key);
}
bool service(std::vector<uint8_t>& frame){
  const bool wasRunning=jobs().running();
  // Boot-lifetime PSRAM: each holds a whole Memory (~2.3 KB with typed
  // scalars). Ordinary locals overflow the 8 KB loop stack; inline statics
  // unnecessarily consume the internal RAM the worker stack needs.
  static auto& job = CalculatorStorage::makeState<Job>();
  static auto& result = CalculatorStorage::makeState<Result>();
  if(jobs().take(job,result)){if(job.app==AppId::Ratio)RatioApp::adopt(job,result);else if(job.app==AppId::Table)TableApp::adopt(job,result);else if(job.app==AppId::Statistics)StatisticsApp::adopt(job,result);else if(job.app==AppId::Spreadsheet)SpreadsheetApp::adopt(job,result);else if(job.app==AppId::Distribution)DistributionApp::adopt(job,result);else if(equationApp(job.app))EquationApp::adopt(job,result);else CalculateApp::adopt(job,result);}
  job.table.reset();result.table.reset(); // controller owns adopted rows; shell retains no Table heap
  job.statistics.reset();result.statistics.reset(); // controller owns adopted summaries; shell retains none
  job.spreadsheet.reset();result.spreadsheet.reset();
  job.distribution.reset();result.distribution.reset(); // controller owns adopted rows; shell retains none
  // The countdown runs whatever is on screen; its alert goes over all of it.
  if(opened&&adoptAlert())dirty=true;
  if(opened&&screen==Screen::MathBox&&!homeSettings&&MathBoxApp::service())dirty=true;
  if(TableApp::service())dirty=true;
  if(StatisticsApp::service())dirty=true;
  if(SpreadsheetApp::service())dirty=true;
  if(DistributionApp::service())dirty=true;
  // Any job that just ended -- published or cancelled -- changes the busy
  // presentation (caret, modal spinner).
  if(wasRunning&&!jobs().running())dirty=true;
  const uint32_t now=CalculatorPlatform::millis();
  if(session().revision!=seenRevision){seenRevision=session().revision;changedAt=now;}
  if(session().revision!=savedRevision&&now-changedAt>=kPersistDelayMs)persist();
  {
    const bool blinkable=opened&&alertMinutes<0&&screen==Screen::App&&(session().app==AppId::Ratio?RatioApp::caretVisible():session().app==AppId::Table?TableApp::caretVisible():session().app==AppId::Statistics?StatisticsApp::caretVisible():session().app==AppId::Spreadsheet?SpreadsheetApp::caretVisible():session().app==AppId::Distribution?DistributionApp::caretVisible():equationApp(session().app)?EquationApp::caretVisible():CalculateApp::caretVisible());
    caretBlinking=blinkable;
    const bool phase=(now/kCaretBlinkMs)%2==0;
    if(blinkable){if(phase!=caretBlinkOn){caretBlinkOn=phase;dirty=true;}}
    else if(!caretBlinkOn){caretBlinkOn=true;dirty=true;}
  }
  if(!opened||!dirty)return false;dirty=false;
  if(alertMinutes>=0){frame=MathBoxApp::renderAlert(alertMinutes);}
  else if(QuickOverlay::active()){frame=QuickOverlay::render();}
  else if(homeSettings){frame=CalculateApp::render(false);}
  else if(screen==Screen::Home){auto raw=home.pixels();frame=CanvasText::packBitsRle(raw.data(),raw.size());}
  else if(screen==Screen::Notice)frame=renderDialog(notice);
  else if(screen==Screen::MathBox)frame=MathBoxApp::render();
  else frame=session().app==AppId::Ratio?RatioApp::render(caretBlinkOn):session().app==AppId::Table?TableApp::render(caretBlinkOn):session().app==AppId::Statistics?StatisticsApp::render(caretBlinkOn):session().app==AppId::Spreadsheet?SpreadsheetApp::render(caretBlinkOn):session().app==AppId::Distribution?DistributionApp::render(caretBlinkOn):equationApp(session().app)?EquationApp::render(caretBlinkOn):CalculateApp::render(caretBlinkOn);
  return true;
}
uint32_t sleepBudgetMs(uint32_t maxMs){
  if(!opened||dirty||jobs().running())return 0;
  if(!caretBlinking)return maxMs;
  // +2: wake just past the edge, so the next service() already sees the flip.
  const uint32_t toEdge=kCaretBlinkMs-CalculatorPlatform::millis()%kCaretBlinkMs+2;
  return toEdge<maxMs?toEdge:maxMs;
}
PanelIcons::State desiredIcons(){
  if(!opened||alertMinutes>=0||homeSettings||QuickOverlay::active()||screen!=Screen::App)return Calculator::desiredIcons(IconView{});
  return session().app==AppId::Ratio?RatioApp::icons():session().app==AppId::Table?TableApp::icons():session().app==AppId::Statistics?StatisticsApp::icons():session().app==AppId::Spreadsheet?SpreadsheetApp::icons():session().app==AppId::Distribution?DistributionApp::icons():equationApp(session().app)?EquationApp::icons():CalculateApp::icons();
}
}  // namespace CalculatorApp
