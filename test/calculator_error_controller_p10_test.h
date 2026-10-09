#include "calculator/apps/calculator_calculate.h"
void p10ErrorControllerChecks(){
  using namespace Calculator;
  int checks=0;
  auto require=[&](bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(std::string("P10: ")+label);};
  auto keys=[&](std::initializer_list<K> list){for(K k:list){CalculatorApp::press(k);drain();}};
  auto snap=[&]{return CalculatorApp::snapshot(frame);};
  auto& session=calculatorSession();
  CalculatorPlatform::fakeMillis=0;
  CalculatorApp::openHome();service();keys({K::Okay,K::Clear});
  session.settings=Settings{};
  keys({K::Digit7,K::Execute});
  const auto history=session.history;
  const auto ans=session.memory.ans;
  for(K dismiss:{K::Okay,K::Clear,K::Return}){
    keys({K::Clear,K::Digit1,K::Divide,K::Digit0});
    const auto original=snap();keys({K::Execute});const auto dialog=snap();
    require(dialog.screen=="ErrorDialog"&&dialog.errorTitle=="Math ERROR"&&dialog.errorOrigin=="Entry","Math modal/title/origin");
    require(dialog.text==original.text&&dialog.text=="1/0"&&!dialog.hasResult&&!dialog.caret&&dialog.menuScreen.empty(),"source preserved, no editor/result/menu");
    require(dialog.icons==PanelIcons::State{}&&dialog.errorCursor==3,"Math cursor and icons");
    for(K ignored:{K::Digit3,K::Delete,K::Left,K::Right,K::Up,K::Down,K::Shift,K::Execute,K::Catalog,K::Settings,K::Tools,K::Variable,K::Function,K::Format}){
      keys({ignored});require(snap().text==dialog.text&&snap().frame==dialog.frame&&snap().cursor==dialog.cursor,"modal ignores editing/navigation");
    }
    CalculatorPlatform::fakeMillis=500;service();
    require(frame==dialog.frame,"modal has no caret blink");
    keys({dismiss});const auto restored=snap();
    require(restored.screen=="Entry"&&restored.text==original.text&&restored.cursor==3&&restored.caret,"OK/AC/BACK restores source/end and immediate caret");
    require(restored.icons.mathio&&restored.icons.deg,"editor icons restore");
    require(session.history==history&&session.memory.ans.re.number==ans.re.number,"failed result does not change history or Ans");
    CalculatorPlatform::fakeMillis=0;service();
  }
  keys({K::Clear,K::Digit1,K::Multiply,K::Multiply,K::Digit2,K::Execute});
  require(snap().screen=="ErrorDialog"&&snap().errorTitle=="Syntax ERROR"&&snap().text=="1**2"&&snap().errorCursor==2,"C59 Syntax modal");
  keys({K::Return});require(snap().cursor==2&&snap().text=="1**2","C59 before offending token");
  keys({K::Digit3});require(snap().text=="1*3*2","C59 corrective typing");
  // Same document normalization for exact stops, UTF-8, hidden syntax, EOF.
  for(const std::string source:{std::string("1**2"),std::string("frac(1,2)"),std::string("sin(2"),std::string(u8"1×π")}){
    EditDocument doc;doc.rebuild(source);
    for(size_t p=0;p<=source.size()+2;++p){
      const auto cursor=CalculateApp::errorReturnCursor(source,Error::Syntax,p);
      require(doc.valid(cursor),"normalized source stop");
      require(cursor==source.size()||(static_cast<unsigned char>(source[cursor])&0xc0)!=0x80,"UTF-8 boundary");
      if(doc.valid(p)&&(p==source.size()||(static_cast<unsigned char>(source[p])&0xc0)!=0x80))require(cursor==p,"exact legal offset retained");
    }
    require(CalculateApp::errorReturnCursor(source,Error::Math,0)==source.size(),"Math uses source end");
  }
  for(K dismiss:{K::Okay,K::Clear,K::Return}){
    CalculatorApp::openHome();service();keys({K::Okay,K::Clear,K::Variable,K::Right,K::Digit1,K::Multiply,K::Multiply,K::Digit2});
    const auto store=snap();const auto value=session.memory.variables[1];keys({K::Execute});
    require(snap().screen=="ErrorDialog"&&snap().errorOrigin=="Store"&&snap().text==store.text,"Store modal preserves owner/source");
    keys({dismiss});require(snap().screen=="Store"&&snap().text=="1**2"&&snap().cursor==2&&snap().fieldRole==store.fieldRole,"Store normalized return");
    require(session.memory.variables[1].re.number==value.re.number,"failed Store does not commit");
    keys({K::Digit3,K::Execute});require(session.memory.variables[1].re.number==6,"corrected Store commits to same B owner");

    CalculatorApp::openHome();service();keys({K::Okay,K::Clear,K::Function,K::Down,K::Down,K::Down,K::Okay,K::Clear,K::Digit1,K::Multiply,K::Multiply,K::Digit2});
    const auto definition=session.memory.g;keys({K::Execute});
    require(snap().screen=="ErrorDialog"&&snap().errorOrigin=="Define"&&snap().text=="1**2","Define g modal");
    keys({dismiss});require(snap().screen=="Define"&&snap().cursor==2&&session.memory.g==definition,"Define return and no premature commit");
    keys({K::Digit3,K::Execute});require(session.memory.g=="1*3*2","corrected Define preserves g owner");

    CalculatorApp::openHome();service();keys({K::Okay,K::Clear,K::X,K::Add,K::Digit1,K::Tools,K::Okay});
    require(snap().screen=="Calc","CALC prompt opened");
    keys({K::Digit1,K::Divide,K::Digit0,K::Execute});
    require(snap().screen=="ErrorDialog"&&snap().errorOrigin=="Calc"&&snap().text=="1/0","CALC field modal");
    keys({dismiss});require(snap().screen=="Calc"&&snap().text=="1/0"&&snap().cursor==3,"CALC same field/end return");
    keys({K::Clear,K::Digit3,K::Execute});
    // P13: the corrected last value only commits here; the final EXE is still
    // required, and only it evaluates the parent through the ordinary path.
    require(snap().screen=="Calc"&&snap().calcPhase=="AwaitFinalExecute"&&!snap().hasResult,"CALC corrected value commits without auto-run");
    keys({K::Execute});
    require(snap().screen=="Result"&&snap().text=="x+1"&&snap().resultText=="4"&&session.memory.variables[6].re.number==3,"CALC preserves parent source and x owner");
  }
  // Session revision changing under a deferred job invalidates its adoption.
  CalculatorApp::openHome();service();keys({K::Okay,K::Clear,K::Digit1,K::Divide,K::Digit0});
  CalculatorApp::press(K::Execute);service();++session.revision;drain();
  require(snap().screen=="Entry"&&!snap().hasResult,"stale revision cannot adopt error");
  // A cancelled failed job cannot replace Home, a later menu, or new result.
  CalculatorApp::press(K::Execute);service();CalculatorApp::press(K::Home);service();drain();
  require(CalculatorApp::isHome(),"stale error cannot replace Home");
  keys({K::Okay,K::Clear,K::Digit9,K::Settings});const auto menu=snap();
  service();require(snap().screen=="Settings"&&snap().frame==menu.frame,"stale error cannot replace menu");
  keys({K::Clear,K::Execute});require(snap().screen=="Result"&&snap().resultText=="9","newer result retained");
  keys({K::Clear,K::Digit1,K::Divide,K::Digit0,K::Execute,K::Home});
  require(CalculatorApp::isHome(),"modal HOME keeps global handling");
  std::cout<<"P10 error controller: "<<checks<<" checks passed\n";
}
