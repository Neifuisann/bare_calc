#include "calculator/runtime/calculator_store.h"
void p9ControllerChecks(){
  int checks=0;
  auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("P9 controller check "+std::to_string(checks));};
  auto keys=[&](std::initializer_list<K> list){for(K k:list){CalculatorApp::press(k);drain();}};
  auto snapshot=[&]{return CalculatorApp::snapshot(frame);};
  auto& settings=Calculator::calculatorSession().settings;
  settings=Calculator::Settings{};
  keys({K::Clear,K::Digit1});
  for(K menu:{K::Catalog,K::Tools,K::Settings,K::Variable,K::Function}){
    const auto before=snapshot();keys({menu});
    require(snapshot().icons==PanelIcons::State{});
    keys({K::Clear});const auto after=snapshot();
    require(after.screen=="Entry" && after.text==before.text && after.cursor==before.cursor && after.frame==before.frame);
  }
  keys({K::Settings});
  require(snapshot().menuLabels==std::vector<std::string>{"Calc Settings","System Settings","Reset","Get Started"});
  keys({K::Up});require(snapshot().selection==3);
  const auto started=snapshot();keys({K::Okay});
  require(snapshot().screen=="GetStarted" && snapshot().frame!=started.frame);
  keys({K::Return});require(snapshot().screen=="Settings" && snapshot().selection==3);
  keys({K::Down});require(snapshot().selection==0);
  keys({K::Okay});require(snapshot().screen=="CalcSettings" && snapshot().menuLabels.size()==10);
  keys({K::Return});require(snapshot().screen=="Settings");
  keys({K::Return});require(snapshot().screen=="Entry" && snapshot().text=="1");
  keys({K::Settings,K::Okay,K::Okay,K::Clear});
  require(snapshot().screen=="Entry" && snapshot().text=="1");
  keys({K::Settings,K::Okay,K::Down,K::Okay,K::Down,K::Okay});
  require(settings.angle==Calculator::Angle::Radian && snapshot().screen=="CalcSettings");
  // System Setup is the device's page: OK asks the host once and the Settings
  // list stays put underneath, cursor on the row, for the hand-back.
  require(!CalculatorApp::takeSystemSetup());
  keys({K::Return,K::Down,K::Okay});
  require(snapshot().screen=="Settings" && snapshot().selection==1 && CalculatorApp::takeSystemSetup() &&
      !CalculatorApp::takeSystemSetup());
  CalculatorApp::redraw();drain();require(snapshot().screen=="Settings");
  keys({K::Digit2});require(snapshot().screen=="Settings" && CalculatorApp::takeSystemSetup());
  keys({K::Down,K::Okay});require(snapshot().screen=="Reset");
  keys({K::Okay});require(snapshot().screen=="ResetConfirm");
  keys({K::Return,K::Return});require(snapshot().screen=="Settings" && snapshot().selection==2);
  keys({K::Clear});require(snapshot().screen=="Entry" && snapshot().text=="1");
  keys({K::Settings,K::Okay});
  for(int i=0;i<8;++i)keys({K::Down});
  keys({K::Okay,K::Up,K::Okay});require(settings.formatMenu);
  keys({K::Down,K::Okay,K::Up,K::Okay});require(!settings.scientificPower);
  Calculator::Settings storedSettings;Calculator::Memory storedMemory;
  const auto record=Calculator::encodeV2(settings,Calculator::calculatorSession().memory);
  require(Calculator::decodeV2(record,storedSettings,storedMemory) && storedSettings.formatMenu &&
      !storedSettings.scientificPower && storedSettings.angle==Calculator::Angle::Radian);
  keys({K::Clear});
  settings.formatMenu=true;
  keys({K::Clear,K::Digit1,K::Divide,K::Digit3,K::Execute});const auto result=snapshot();
  keys({K::Format});require(snapshot().screen=="Format");
  keys({K::Clear});const auto restored=snapshot();
  require(restored.screen=="Result" && restored.hasResult && restored.text==result.text &&
      restored.resultText==result.resultText && restored.frame==result.frame);
  keys({K::Clear,K::Variable,K::Right});require(snapshot().selection==1);
  keys({K::Down});require(snapshot().selection==3);
  keys({K::Left});require(snapshot().selection==2);
  keys({K::Up});require(snapshot().selection==0);
  keys({K::Digit2});require(snapshot().screen=="Store" && snapshot().text=="2");
  keys({K::Catalog,K::Okay,K::Clear});require(snapshot().screen=="Store" && snapshot().text=="2");
  keys({K::Function,K::Clear});require(snapshot().screen=="Store" && snapshot().text=="2");
  keys({K::Return,K::Clear});require(snapshot().screen=="Entry");
  keys({K::Settings,K::Okay,K::Home});require(CalculatorApp::isHome());
  std::cout<<"P9 focused controller: "<<checks<<" checks passed\n";
}
