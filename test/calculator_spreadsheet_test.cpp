#include "calculator/apps/calculator_spreadsheet.h"
#include "calculator_platform_fake.h"
#include "calculator/runtime/calculator_storage.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Calculator;
using K=ExpressionInput::Key;
namespace {
int checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
using Op=SpreadsheetOperation;
struct Sheet {
  Request request;
  std::shared_ptr<const SpreadsheetData> data;
  Result run(Op op,std::string text="",SpreadsheetRange range={},bool autoCalc=true,int sr=0,int sc=0){
    SpreadsheetTask t;t.operation=op;t.expression=text;t.range=range;t.autoCalc=autoCalc;t.input=data;t.sourceRow=sr;t.sourceCol=sc;
    auto r=evaluateSpreadsheet(t,request);if(r.spreadsheet)data=r.spreadsheet;return r;
  }
  Result set(int row,int col,std::string text,bool calc=true){return run(Op::Set,text,{row,col,row,col},calc);}
  const SpreadsheetCell& at(int row,int col){return data->cells[row*5+col];}
};
void mathematics(){
  Sheet s;
  require(s.set(0,0,"7*5").error==Error::None,"manual constant 1");
  require(s.set(1,0,"7*6").error==Error::None,"manual constant 2");
  s.set(2,0,"A2+7");s.set(0,1,"=A1+7");
  require(s.at(0,0).value==Decimal(35)&&s.at(1,0).value==Decimal(42)&&s.at(2,0).value==Decimal(49)&&s.at(0,1).value==Decimal(42),"Casio example 1");
  require(s.at(2,0).formula.empty()&&s.at(0,1).formula=="=A1+7","constants freeze, formulas retained");
  s.set(3,0,"=Sum(A1:A3)");require(s.at(3,0).value==Decimal(126),"manual range sum");
  s.run(Op::FillFormula,"2A1-3",{0,1,2,1});
  require(s.at(0,1).value==Decimal(67)&&s.at(1,1).value==Decimal(81)&&s.at(2,1).value==Decimal(95),"manual Fill Formula");
  require(s.at(2,1).formula=="=2A3-3","fill adjusts relative references");
  s.run(Op::FillValue,"B1*3",{0,2,2,2});
  require(s.at(0,2).value==Decimal(201)&&s.at(1,2).value==Decimal(243)&&s.at(2,2).value==Decimal(285),"manual Fill Value");
  s.set(0,0,"10");require(s.at(0,1).value==Decimal(17)&&s.at(0,2).value==Decimal(201)&&s.at(2,0).value==Decimal(49),"auto calc updates formulas only");
  s.set(0,0,"20",false);require(s.at(0,1).value==Decimal(17),"Auto Calc off retains cache");
  s.run(Op::Recalculate);require(s.at(0,1).value==Decimal(37),"explicit recalculation");
  s.set(0,0,"A1+1");require(s.at(0,0).value==Decimal(21),"constant can read its previous cell");
  s.set(0,3,"=Min(A1:C3)");require(s.at(0,3).value==Decimal(21),"rectangle Min");
  s.set(1,3,"=Max(A1:C3)");require(s.at(1,3).value==Decimal(285),"rectangle Max");
  s.set(2,3,"=Mean(A1:A3)");require(s.at(2,3).value==Decimal("37.33333333"),"10-digit Mean");
  require(relocateSpreadsheetFormula("=$A1+A$2+$B$3",2,1)=="=$A3+B$2+$B$3","all absolute reference types");
  require(relocateSpreadsheetFormula("=2A1+1E10",1,1)=="=2B2+1E10","implicit product and scientific exponent");
  s.set(0,4,"=$A1+A$2+$B$3");s.run(Op::Copy,"",{2,3,2,3},true,0,4);
  require(s.at(2,3).formula=="=$A3+?$2+$B$3"&&s.at(2,3).error==Error::Range,"out of bounds copied reference becomes ? and ERROR");
  s.run(Op::Cut,"",{3,4,3,4},true,0,4);
  require(!s.at(0,4).occupied&&s.at(3,4).formula=="=$A1+A$2+$B$3","cut preserves reference spelling");
  require(s.set(4,4,"=E5").error==Error::Circular,"self reference rejected transactionally");
  require(!s.at(4,4).occupied,"failed formula leaves prior sheet intact");
  s.set(5,0,"=A7");require(s.set(6,0,"=A6",false).error==Error::Circular,"indirect cycle rejected with Auto Calc off");
  require(s.set(7,0,"=1/0").error==Error::Math,"formula math error");
  require(s.set(7,0,"=2+").error==Error::Syntax,"formula syntax error");
  s.set(7,0,"12345678915");require(s.at(7,0).value==Decimal("12345678920"),"manual 11 digits round to 10");
  require(spreadsheetCellText(Decimal("12345678920"),Settings{}).find("E10")!=std::string::npos,"large cell preserves scientific exponent");
  require(spreadsheetCellText(Decimal("-1E-99"),Settings{})=="-1E-99","negative tiny cell keeps sign and exponent");
  require(s.set(7,0,"9.9999999998E99").error==Error::Math,"storage rounding cannot exceed calculator range");
  s.request.memory.variables[0]=Value(2);s.request.memory.f="x^2";s.set(8,0,"=f(A)+sin(30)");
  require(s.at(8,0).value==Decimal("4.5"),"shared functions, variables and angle");
  auto r=s.run(Op::Clear);require(spreadsheetUsedBytes(*r.spreadsheet)==0,"Delete All restores memory");
  r=s.run(Op::FillValue,"1",{0,0,33,4});require(r.error==Error::None&&spreadsheetUsedBytes(*s.data)==2380,"170 constants fill 2380 bytes exactly");
  require(s.set(34,0,"2").error==Error::Memory&&!s.at(34,0).occupied,"memory overflow is atomic");
  s.run(Op::Erase,"",{0,0,0,0});require(spreadsheetUsedBytes(*s.data)==2366,"DEL restores 14 bytes");
  require(spreadsheetInputBytes("=Sum(A1:A3)")==8,"command consumes one token byte");
  require(s.set(0,0,"="+std::string(49,'1')).error==Error::Memory,"49-token input limit");
  SpreadsheetRange range;
  require(spreadsheetRange("A1:E45",range)&&range.endRow==44&&range.endCol==4,"full allowed range");
  for(const auto bad:{"A0:A1","A1:F1","A1:E46","B2:A1","A1","A1:A1+2"})require(!spreadsheetRange(bad,range),"invalid range rejected");
  SpreadsheetTask t;t.operation=Op::FillFormula;t.range={0,0,44,4};t.expression="1";
  r=evaluateSpreadsheet(t,s.request,[]{return true;});require(r.error==Error::Cancelled&&!r.spreadsheet,"cancellation never publishes partial fill");
}
std::string outDir;
std::vector<uint8_t> frame;
void save(const std::string& name){
  std::vector<uint8_t> raw;
  for(size_t p=0;p<frame.size();){const auto n=frame[p++];if(n<128){raw.insert(raw.end(),frame.begin()+p,frame.begin()+p+n+1);p+=n+1;}else raw.insert(raw.end(),n-126,frame[p++]);}
  require(raw.size()==3024,"192x63 four-gray panel frame");if(outDir.empty())return;
  std::ofstream pgm(outDir+"/spreadsheet-"+name+".pgm",std::ios::binary);pgm<<"P5\n192 63\n255\n";
  for(size_t p=0;p<192*63;++p)pgm.put(static_cast<char>(255-85*((raw[p/4]>>(6-2*(p%4)))&3)));
}
void drain(){CalculatorApp::service(frame);}
void press(K k){CalculatorApp::press(k);drain();}
void keys(std::initializer_list<K> list){for(auto k:list)press(k);}
CalculatorApp::Snapshot snap(){return CalculatorApp::snapshot(frame);}
void launch(K shortcut){CalculatorApp::openHome();press(shortcut);}
void tool(int row){press(K::Tools);for(int i=0;i<row;++i)press(K::Down);press(K::Okay);}
void ui(){
  CalculatorPlatform::fakeMillis=0;SpreadsheetApp::clear();launch(K::Digit4);
  require(snap().screen=="Spreadsheet"&&snap().rows==45&&snap().cols==5,"Home opens real Spreadsheet app");save("empty");
  press(K::Tools);save("tools-top");for(int i=0;i<6;++i)press(K::Down);save("tools-middle");for(int i=0;i<3;++i)press(K::Down);save("tools-bottom");press(K::Return);
  press(K::Catalog);require(snap().screen=="SpreadsheetCatalog","CATALOG opens categories");save("catalog");press(K::Okay);require(snap().screen=="SpreadsheetCommands","Spreadsheet commands open");save("commands-top");for(int i=0;i<6;++i)press(K::Down);save("commands-mean");press(K::Down);save("commands-sum");keys({K::Return,K::Return});
  tool(3);require(snap().screen=="SpreadsheetMemory","Available Memory");save("memory");press(K::Return);
  keys({K::Digit1,K::Execute,K::Digit2,K::Execute});require(snap().cells[0]=="1"&&snap().cells[5]=="2"&&snap().gridRow==2,"entry commits and moves down");
  keys({K::Up,K::Right,K::Shift,K::Open,K::Shift,K::Digit4,K::Digit2,K::Add,K::Digit7,K::Execute});
  require(snap().cells[6]=="9","formula keys and row reference");
  press(K::Up);save("formula");press(K::Okay);require(snap().editing&&snap().text=="=A2+7","OK edits retained formula");save("editing");press(K::Return);
  keys({K::Up,K::Right});tool(5);require(snap().screen=="SpreadsheetPaste","copy paste standby");save("paste");press(K::Okay);require(snap().screen=="SpreadsheetPaste","copy stays armed");press(K::Down);save("paste-down");press(K::Return);
  tool(1);save("fill-value");press(K::Okay);press(K::Digit3);save("fill-value-edit");press(K::Execute);press(K::Okay);save("fill-range-edit");press(K::Execute);press(K::Okay);
  require(snap().screen=="Spreadsheet"&&snap().cells[7]=="3","Fill Value commits range");
  tool(8);save("auto-calc");press(K::Down);press(K::Okay);tool(9);save("show-cell");press(K::Return);press(K::Return);
  // Relative Fill Formula is entered through the same two inline fields as
  // the supplied Fill Value screen. Confirm publishes both formula and value.
  tool(0);keys({K::Okay,K::Digit2,K::Shift,K::Digit4,K::Digit2,K::Subtract,K::Digit3,K::Execute,K::Down,K::Okay});
  require(snap().cells[7]=="1","Fill Formula UI evaluates relative expression");
  // Grab lands in the original cell editor, never in the selected reference cell.
  keys({K::Shift,K::Open,K::Catalog,K::Okay,K::Okay,K::Left,K::Left,K::Okay,K::Add,K::Digit1,K::Execute});
  require(snap().cells[7]=="3"&&snap().gridRow==2,"Grab selects A2 and returns editor to C2");
  launch(K::Digit1);launch(K::Digit4);require(snap().cells[0]=="1","fx-880BTG sheet survives app changes");
  applyTransition(calculatorSession(),TransitionReason::NativeOn);drain();require(snap().cells[0].empty(),"native ON clears sheet");
  keys({K::Shift,K::Open,K::Shift,K::Digit4,K::Digit1,K::Execute});require(snap().errorTitle=="Circular ERROR","Circular ERROR modal");save("circular-error");press(K::Return);require(snap().editing&&snap().text=="=A1","error returns original editable source");keys({K::Return,K::Clear});
  CalculatorPlatform::fakeDeferWorker=true;keys({K::Digit9,K::Execute});require(calculatorJobs().running(),"spreadsheet uses worker");
  press(K::Home);CalculatorPlatform::runPendingWorker();drain();press(K::Home);require(snap().cells[0].empty(),"Home cancels late commit");
  keys({K::Digit9,K::Execute,K::Clear});CalculatorPlatform::runPendingWorker();drain();require(snap().cells[0].empty(),"AC cancels late commit");CalculatorPlatform::fakeDeferWorker=false;
  keys({K::Digit4,K::Execute,K::Up});press(K::Okay);save("caret-on");CalculatorPlatform::fakeMillis=500;drain();save("caret-off");
  require(snap().editing&&!snap().caret,"caret blink preserves editing");press(K::Return);
  for(int i=0;i<50;++i)press(K::Down);for(int i=0;i<6;++i)press(K::Right);
  require(snap().gridRow==44&&snap().gridCol==4,"E45 navigation clamps safely");save("e45");
  SpreadsheetApp::clear();drain();require(snap().cells[0].empty(),"power-off clear");
  // Clearing a dormant sheet must not cancel another app's worker.
  launch(K::Digit1);CalculatorPlatform::fakeDeferWorker=true;keys({K::Clear,K::Digit8,K::Execute});SpreadsheetApp::clear();
  CalculatorPlatform::runPendingWorker();drain();require(snap().resultText=="8","sheet lifetime does not cancel Calculate");CalculatorPlatform::fakeDeferWorker=false;
}
}
int main(int argc,char** argv){
  try{assert(CalculatorStorage::usage().internal==0);if(argc>1)outDir=argv[1];mathematics();ui();std::cout<<"Spreadsheet: "<<checks<<" checks passed; manual examples, references, memory, cancellation and UI frames.\n";}
  catch(const std::exception& e){std::cerr<<"FAIL Spreadsheet: "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
