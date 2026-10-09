#pragma once
// Native safety regressions. These are not external target-fidelity passes.
#include "calculator/runtime/calculator_store.h"
#include <stdexcept>
inline void p15AppControllerChecks() {
  using namespace Calculator;
  using Key=ExpressionInput::Key;
  unsigned checks=0;
  auto require=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(std::string("P15: ")+why);};
  auto press=[&](Key k){CalculatorApp::press(k);drain();};
  auto launch=[&](AppId app){
    CalculatorApp::openHome();service();
    for(int n=0;n<14;++n)CalculatorApp::press(Key::Left);
    for(int n=0;n<static_cast<int>(app);++n)CalculatorApp::press(Key::Right);
    press(Key::Okay);
  };
  auto snap=[&](){return CalculatorApp::snapshot(frame);};
  auto& s=calculatorSession();
  // Manual indicator semantics: settings own icons, FORMAT is only a view.
  for(auto mode:{ComplexResult::Rectangular,ComplexResult::Polar}){
    s.settings.complexResult=mode;launch(AppId::Complex);press(Key::Clear);
    auto icons=CalculatorApp::desiredIcons();
    require(icons.cplxI==(mode==ComplexResult::Rectangular)&&icons.angle==(mode==ComplexResult::Polar),"Complex entry setting icons");
    for(auto k:{Key::Digit2,Key::Add,Key::Digit3,Key::Shift,Key::Digit9,Key::Execute})press(k);
    const auto re=s.memory.ans.re.number,im=s.memory.ans.im.number;
    press(Key::Shift);press(Key::Format);
    require(snap().screen=="Format","Complex FORMAT menu");
    PanelIcons::State zero;require(CalculatorApp::desiredIcons()==zero,"Complex menu icons clear");
    press(Key::Down);press(Key::Down);if(mode==ComplexResult::Rectangular)press(Key::Down);press(Key::Execute);
    icons=CalculatorApp::desiredIcons();
    require(icons.cplxI==(mode==ComplexResult::Rectangular)&&icons.angle==(mode==ComplexResult::Polar),"FORMAT does not change setting icons");
    require(s.memory.ans.re.number==re&&s.memory.ans.im.number==im,"FORMAT preserves complex value");
    press(Key::Clear);press(Key::Digit1);press(Key::Divide);press(Key::Digit0);press(Key::Execute);
    require(snap().screen=="ErrorDialog"&&CalculatorApp::desiredIcons()==zero,"Complex error clears icons");
    press(Key::Return);
  }
  s.settings.complexResult=ComplexResult::Rectangular;
  launch(AppId::Calculate);launch(AppId::Equation);
  for(auto k:{Key::Down,Key::Okay,Key::Okay,Key::Digit1,Key::Execute,Key::Digit0,Key::Execute,Key::Digit1,Key::Execute,Key::Execute})press(k);
  const auto complexSolutionFrame=frame;
  for(auto k:{Key::Tools,Key::Okay,Key::Down,Key::Okay,Key::Clear})press(k);
  require(snap().resultText=="No Real Roots","Complex Roots Off filters existing solution");
  for(auto k:{Key::Tools,Key::Okay,Key::Up,Key::Okay,Key::Clear})press(k);
  require(frame==complexSolutionFrame&&!calculatorJobs().running(),"Complex Roots On restores identical solution without a job");
  // Every app's queued job is cancelled on departure; late worker completion
  // cannot overwrite another app. Relaunch then commit a newer value.
  for(auto app:{AppId::Complex,AppId::BaseN,AppId::Matrix,AppId::Vector,AppId::Equation,AppId::Inequality}){
    launch(AppId::Calculate);launch(app);
    const bool aggregate=app==AppId::Matrix||app==AppId::Vector;
    const bool equation=app==AppId::Equation||app==AppId::Inequality;
    auto enterField=[&](){
      if(aggregate){
        press(Key::Tools);press(Key::Okay);
        if(snap().screen=="Action")press(Key::Okay);
        press(Key::Down);if(app==AppId::Matrix)press(Key::Down);press(Key::Okay);
      }else if(equation){
        if(app==AppId::Equation){press(Key::Down);press(Key::Okay);}
        press(Key::Okay);if(app==AppId::Inequality)press(Key::Okay);
      }else press(Key::Clear);
    };
    enterField();press(Key::Digit7);
    CalculatorApp::press(Key::Execute);service();
    require(calculatorJobs().running(),"app worker queued");
    CalculatorApp::openHome();service();
    // Pick Calculate while the cancelled worker still exists.
    for(int n=0;n<14;++n)CalculatorApp::press(Key::Left);
    CalculatorApp::press(Key::Okay);service();
    const auto before=snap();
    require(CalculatorPlatform::runPendingWorker(),"late worker exists");drain();
    require(s.app==AppId::Calculate&&snap().screen==before.screen&&snap().text==before.text,"late result rejected after app switch");
    launch(app);enterField();press(Key::Digit9);press(Key::Execute);
    require(!calculatorJobs().running(),"newer app result adopted");
    if(aggregate||equation)require(!snap().cells.empty()&&snap().cells[0]=="9","newer field wins");
    else require(s.memory.ans.re.number==9,"newer scalar wins");
    if(aggregate||equation)require(s.history.empty(),"aggregate/equation does not add Calculate history");
    // Error leaves the cell uncommitted; BACK aborts its draft.
    if(aggregate||equation){
      press(Key::Digit1);press(Key::Divide);press(Key::Digit0);press(Key::Execute);
      if(equation){
        // Equation/Inequality: the modal ERROR screen, then back to the draft.
        require(snap().screen=="ErrorDialog"&&snap().errorTitle=="Math ERROR","cell error is a screen");
        press(Key::Return);
      }
      require(snap().editing&&(equation||snap().notice=="Math ERROR"),"cell error retains draft");
      press(Key::Return);require(!snap().editing,"BACK aborts cell draft");
    }
  }
  // Submenu cancellation must not allocate or overwrite an aggregate.
  for(auto app:{AppId::Matrix,AppId::Vector}){
    launch(AppId::Calculate);launch(app);
    // Find the app's aggregate group by label (catalog groups get added), then
    // insert its first entry (MatA/VctA).
    press(Key::Catalog);
    const std::string group=app==AppId::Matrix?"Matrix":"Vector";
    const auto rows=snap().menuLabels;
    const auto at=std::find(rows.begin(),rows.end(),group);
    require(at!=rows.end(),"aggregate catalog group listed");
    for(auto n=at-rows.begin();n>0;--n)press(Key::Down);
    press(Key::Okay);press(Key::Okay);
    require(snap().text==(app==AppId::Matrix?"MatA":"VctA"),"aggregate variable inserted");
    CalculatorApp::press(Key::Execute);service();require(calculatorJobs().running(),"aggregate result job queued");
    CalculatorApp::openHome();service();
    require(CalculatorPlatform::runPendingWorker(),"late aggregate worker exists");drain();
    require(CalculatorApp::isHome(),"late aggregate result cannot leave Home");
    press(Key::Okay);press(Key::Execute);
    require(snap().screen=="Answer"&&snap().cells[0]=="9","new aggregate result wins after resume");
    require(s.history.empty(),"aggregate answer never enters Calculate history");
  }
  // Solve jobs, separately from the cell jobs above (Equation/Inequality).
  for(auto app:{AppId::Equation,AppId::Inequality}){
    launch(AppId::Calculate);launch(app);
    if(app==AppId::Equation){press(Key::Down);press(Key::Okay);}
    press(Key::Okay);if(app==AppId::Inequality)press(Key::Okay);
    for(auto k:{Key::Digit1,Key::Execute,Key::Digit0,Key::Execute,Key::Subtract,Key::Digit1,Key::Execute})press(k);
    CalculatorApp::press(Key::Execute);service();require(calculatorJobs().running(),"solution job queued");
    CalculatorApp::openHome();service();
    require(CalculatorPlatform::runPendingWorker(),"late solution worker exists");drain();
    require(CalculatorApp::isHome(),"late solution cannot leave Home");
    press(Key::Okay);press(Key::Execute);
    require(snap().screen=="Solutions","newer solve succeeds after same-app resume");
    const auto solutionFrame=frame;press(Key::Format);
    require(frame==solutionFrame,"legacy solution FORMAT stays unchanged (source-only)");
  }
  for(auto app:{AppId::Matrix,AppId::Vector}){
    launch(app);press(Key::Tools);press(Key::Okay);
    if(snap().screen=="Action")press(Key::Okay);
    press(Key::Okay);require(snap().screen=="Dimension","dimension chooser opens");
    press(Key::Down);press(Key::Return);require(snap().screen=="Size","dimension BACK restores size form");
    press(Key::Okay);press(Key::Down);press(Key::Okay);
    require(snap().screen=="Size","dimension selection returns to owning form");
  }
  // Retention/reset reducer matrix, independent of controller state.
  for(auto app:{AppId::Complex,AppId::BaseN,AppId::Matrix,AppId::Vector,AppId::Equation,AppId::Inequality}){
    CalculatorSession copy;copy.app=app;copy.base=16;
    copy.aggregates.matrices[0].rows=copy.aggregates.matrices[0].cols=2;
    copy.aggregates.vectors[0].rows=2;copy.aggregates.vectors[0].cols=1;
    copy.aggregates.matrices[kAnswerSlot]=copy.aggregates.matrices[0];
    copy.aggregates.vectors[kAnswerSlot]=copy.aggregates.vectors[0];
    applyTransition(copy,TransitionReason::HomeOpen);
    require(copy.base==16&&copy.aggregates.matrices[kAnswerSlot].defined(),"Home retains app-specific memory");
    applyTransition(copy,TransitionReason::OutputStyleChanged);
    require(copy.base==16&&copy.aggregates.vectors[0].defined(),"output settings retain aggregates");
    applyTransition(copy,TransitionReason::InputStyleChanged);
    require(copy.aggregates.matrices[0].defined(),"input settings retain aggregates");
    applyTransition(copy,TransitionReason::OtherAppLaunch);
    require(copy.base==10&&!copy.aggregates.matrices[kAnswerSlot].defined()&&!copy.aggregates.vectors[kAnswerSlot].defined()&&copy.aggregates.matrices[0].defined()&&copy.aggregates.vectors[0].defined(),"other-app retention policy");
    applyTransition(copy,TransitionReason::SettingsDataReset);
    require(!copy.aggregates.matrices[0].defined()&&!copy.aggregates.vectors[0].defined(),"settings-data clears named values");
    applyTransition(copy,TransitionReason::InitializeAll);require(copy.base==10,"initialize default base");
  }
  launch(AppId::Calculate);press(Key::Clear);press(Key::Return);
  require(!CalculatorApp::isHome(),"C65 remains open; P14 routing frozen");
  std::cout<<"P15 native safety: "<<checks<<" checks passed (six apps, late jobs, new field state, icons, dimensions, retention, C65).\n";
}
