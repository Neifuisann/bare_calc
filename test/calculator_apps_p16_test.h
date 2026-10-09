#pragma once
#include "calculator/apps/calculator_ratio.h"
#include "calculator/apps/calculator_table.h"
inline void p16AppControllerChecks(){
  using namespace Calculator;using Key=ExpressionInput::Key;unsigned checks=0;
  auto require=[&](bool v,const char* why){++checks;if(!v)throw std::runtime_error(std::string("P16: ")+why);};
  auto press=[&](Key k){CalculatorApp::press(k);drain();};
  auto snap=[&](){return CalculatorApp::snapshot(frame);};
  auto launch=[&](AppId app){CalculatorApp::openHome();service();for(int i=0;i<14;++i)CalculatorApp::press(Key::Left);for(int i=0;i<static_cast<int>(app);++i)CalculatorApp::press(Key::Right);press(Key::Okay);};
  auto& s=calculatorSession();applyTransition(s,TransitionReason::InitializeAll);
  auto type=[&](const std::string& text){for(char c:text)press(c=='-'?Key::Subtract:c=='.'?Key::Dot:c=='/'?Key::Divide:static_cast<Key>(static_cast<int>(Key::Digit0)+c-'0'));};
  auto coefficient=[&](const std::string& text){type(text);press(Key::Execute);};
  launch(AppId::Ratio);require(snap().menuLabels==std::vector<std::string>({"A:B=X:D","A:B=C:X"}),"Ratio exact menu");
  press(Key::Okay);coefficient("3");coefficient("8");coefficient("12");press(Key::Execute);
  require(snap().resultText=="9/2","Ratio exact manual example");require(s.history.empty(),"Ratio history isolated");
  press(Key::Variable);for(int i=0;i<5;++i)press(Key::Up);press(Key::Left);press(Key::Okay);press(Key::Down);press(Key::Okay);
  require(s.memory.variables[0].re.exact()==exactRational(9,2),"Ratio shared Store retains exactness");
  press(Key::Return);require(snap().screen=="RatioResult","Store returns Ratio result");
  for(auto key:{Key::Return,Key::Clear,Key::Okay}){
    press(key);require(snap().screen=="RatioCoefficients"&&snap().cells==std::vector<std::string>({"3","8","12"}),"manual result return retains coefficients");press(Key::Execute);
  }
  press(Key::Okay);press(Key::Return);press(Key::Down);press(Key::Okay);
  require(snap().cells==std::vector<std::string>({"0","0","0"}),"type change resets");
  coefficient("2");coefficient("3");coefficient("4");press(Key::Execute);require(snap().resultText=="6","second Ratio form");
  for(int form=0;form<2;++form)for(int zero=0;zero<3;++zero){
    launch(AppId::Calculate);launch(AppId::Ratio);if(form)press(Key::Down);press(Key::Okay);
    for(int i=0;i<3;++i)coefficient(i==zero?"0":"2");press(Key::Execute);
    require(snap().errorTitle=="Math ERROR","every zero coefficient errors in both types");press(Key::Return);
    require(snap().screen=="RatioCoefficients","error dismiss restores coefficients");
  }
  auto ratioReady=[&](){launch(AppId::Calculate);launch(AppId::Ratio);press(Key::Okay);coefficient("3");coefficient("8");coefficient("12");};
  for(int action=0;action<3;++action){ratioReady();CalculatorApp::press(Key::Execute);require(calculatorJobs().running(),"Ratio pending solve");
    if(action==0)CalculatorApp::openHome();else if(action==1)CalculatorApp::press(Key::Digit9);else CalculatorApp::press(Key::Return);
    drain();require(snap().screen!="RatioResult","Ratio stale result discarded");
  }
  ratioReady();press(Key::Clear);require(snap().cells==std::vector<std::string>({"0","0","0"}),"coefficient AC resets");
  ratioReady();CalculatorApp::openHome();launch(AppId::Ratio);require(snap().cells==std::vector<std::string>({"0","0","0"}),"HOME resets Ratio coefficients");
  // Real Table kernel, exact example and both capacity boundaries.
  Request request;request.memory.f="x^2+1/2";request.memory.g="x^2-1/2";
  TableTask task;task.range={{Value(-1),Value(1),realCombine(Value(1),Value(2),'/')}};
  auto generated=evaluateTable(task,request);require(generated.error==Error::None&&generated.table->rows.size()==5,"Table manual rows");
  for(int i=0;i<5;++i){const auto& row=generated.table->rows[i];const Value x=realCombine(Value(i-2),Value(2),'/');
    require(row.x.number==x.number,"Table exact x sequence");require(row.hasF&&row.f.number==x.number*x.number+Decimal("0.5"),"manual f every row");require(row.hasG&&row.g.number==x.number*x.number-Decimal("0.5"),"manual g every row");}
  for(int mode=0;mode<3;++mode){task.type=mode;const int capacity=mode?45:30;task.range={{Value(1),Value(capacity),Value(1)}};
    auto valid=evaluateTable(task,request);require(valid.error==Error::None&&valid.table->rows.size()==static_cast<size_t>(capacity),"exact capacity accepted");
    // Over-capacity Range ERROR is target-confirmed (880-MAN technical_information__errors).
    task.range[1]=Value(capacity+1);require(evaluateTable(task,request).error==Error::Range,"over capacity Range ERROR");}
  // Below pins the safe bounded implementation only: the manual names no error
  // for these inputs, so their exact target labels are PENDING (see apps-table
  // P16-TABLE-26..29). No descending range is generated: Start>End is rejected.
  task.type=0;task.range={{Value(-1),Value(1),Value(0)}};require(evaluateTable(task,request).error==Error::Range,"zero step bounded");
  task.range[2]=Value(-1);require(evaluateTable(task,request).error==Error::Range,"negative step bounded");
  task.range={{Value(2),Value(1),Value(1)}};require(evaluateTable(task,request).error==Error::Range,"reversed range bounded");
  task.range={{Value(-1),Value(1),Value(1)}};request.memory.f="sqrt(x)";request.memory.g="x";
  auto domain=evaluateTable(task,request);require(domain.error==Error::None&&domain.table->rows[0].fError==Error::Math&&domain.table->rows[0].hasG&&domain.table->rows[2].hasF,"domain errors isolated by cell");
  require(evaluateTable(task,request,[]{return true;}).error==Error::Cancelled,"Table worker cancellation");
  require(composedFunction("f(x)*2-x")&&!composedFunction("2*x+A"),"structural composed dependency detection");
  auto tools=[&](int row){press(Key::Tools);for(int i=0;i<row;++i)press(Key::Down);press(Key::Okay);};
  auto range=[&](const std::string& start,const std::string& end,const std::string& step){tools(0);coefficient(start);coefficient(end);coefficient(step);press(Key::Execute);};
  launch(AppId::Table);require(snap().screen=="Table"&&snap().cols==3&&snap().notice=="Not Defined","undefined default Table launch");
  s.memory.f="x^2+1/2";s.memory.g="x^2-1/2";++s.revision;drain();range("-1","1","0.5");
  require(snap().rows==5&&s.memory.variables[6].re.number==1,"range adoption sets End to x");
  const auto manualFrame=frame;service();require(frame==manualFrame&&pixels(frame).size()==192*63,"Table deterministic shared grid frame");
  auto table=TableApp::data();require(table&&table->rows[0].f.number==Decimal("1.5"),"UI generation evaluates actual functions");
  press(Key::Variable);for(int i=0;i<5;++i)press(Key::Up);press(Key::Left);press(Key::Okay);press(Key::Down);press(Key::Okay);require(s.memory.variables[0].re.number==-1,"Table Store selected x");press(Key::Return);
  const auto oldSecond=TableApp::data()->rows[1].f;coefficient("2");require(TableApp::data()->rows[0].f.number==Decimal("4.5")&&TableApp::data()->rows[1].f==oldSecond,"x edit only updates owning row");
  require(s.tableRange[0].number==-1&&s.tableRange[1].number==1,"x edit preserves range");
  press(Key::Down);press(Key::Add);require(TableApp::data()->rows[1].x.number==Decimal("2.5"),"plus previous x plus Step");
  press(Key::Subtract);require(TableApp::data()->rows[1].x.number==Decimal("1.5"),"minus previous x minus Step");
  press(Key::Execute);require(TableApp::data()->rows[1].x.number==Decimal("2.5"),"EXE previous x plus Step");
  s.memory.f="2*x+A";s.memory.variables[0]=Value(1);++s.revision;drain();
  const Value before=TableApp::data()->rows[0].f;s.memory.variables[0]=Value(9);++s.revision;drain();
  require(TableApp::data()->rows[0].f==before,"scalar change is non-reactive");tools(4);require(TableApp::data()->rows[0].f.number==13,"Recalculate reads current scalar");
  s.memory.f="sin(x)";++s.revision;drain();const Value degree=TableApp::data()->rows[0].f;
  s.settings.angle=Angle::Radian;++s.revision;drain();require(TableApp::data()->rows[0].f==degree,"angle change non-reactive");tools(4);require(!(TableApp::data()->rows[0].f==degree),"Recalculate uses current angle");
  s.memory.f="x+1";s.memory.g="f(x)*2-x";++s.revision;drain();tools(4);const Value composed=TableApp::data()->rows[0].g;
  s.memory.f="x+3";++s.revision;drain();require(TableApp::data()->rows[0].f.number==5&&TableApp::data()->rows[0].g==composed,"direct f updates without composed g");
  tools(4);require(TableApp::data()->rows[0].g.number==8,"composed g explicit recalc");
  s.memory.g="x+7";++s.revision;drain();require(TableApp::data()->rows[0].g.number==9,"direct g updates");
  // Shared definition editor and field error, no Table-private f/g.
  tools(1);press(Key::Okay);press(Key::Clear);press(Key::Digit7);press(Key::Execute);press(Key::Return);drain();
  require(s.memory.f=="7"&&TableApp::data()->rows[0].f.number==7,"shared Define f commits and refreshes");
  tools(5);require(s.verify&&!TableApp::data()->rows[0].hasF&&!TableApp::data()->rows[0].hasG,"Verify ON clears output columns");
  press(Key::Right);coefficient("7");require(snap().resultText=="True","shared Verify engine true");press(Key::Okay);require(snap().gridRow==2,"Verify true advances row");
  coefficient("8");require(snap().resultText=="False","shared Verify engine false");press(Key::Okay);require(snap().gridRow==2,"Verify false retains row");
  press(Key::Left);coefficient("3");tools(5);require(!s.verify&&TableApp::data()->rows[2].x.number==3&&TableApp::data()->rows[2].f.number==7,"Verify OFF recomputes retained x edits");
  const size_t oldRows=TableApp::data()->rows.size();press(Key::Delete);require(TableApp::data()->rows.size()==oldRows-1,"Delete Row");
  tools(3);press(Key::Okay);require(TableApp::data()->rows.size()==oldRows&&!TableApp::data()->rows[2].hasX,"Insert Row is blank");
  tools(3);press(Key::Down);press(Key::Okay);press(Key::Down);press(Key::Okay);require(snap().rows==0,"Delete All confirmation");
  range("1","3","1");require(s.memory.variables[6].re.number==3,"second End adoption");
  tools(2);press(Key::Down);press(Key::Okay);require(snap().rows==0&&s.tableType==1&&s.memory.f=="7"&&s.tableRange[1].number==3,"type clears rows retains definitions range");
  for(int action=0;action<4;++action){range("1","3","1");tools(0);CalculatorApp::press(Key::Execute); // Start field, not Execute: return to grid then explicit recalc below
    press(Key::Return);press(Key::Return);press(Key::Tools);for(int i=0;i<4;++i)press(Key::Down);CalculatorApp::press(Key::Okay);
    require(calculatorJobs().running(),"Table recalc pending");
    const auto previousX=s.memory.variables[6];
    if(action==0)CalculatorApp::openHome();
    else if(action==1){CalculatorApp::press(Key::Tools);CalculatorApp::press(Key::Okay);CalculatorApp::press(Key::Digit9);}
    else if(action==2){s.memory.f="x+99";++s.revision;service();}
    else{++s.tableType;applyTransition(s,TransitionReason::TableTypeChanged);service();}
    drain();require(s.memory.variables[6]==previousX,"stale Table job cannot change global x");launch(AppId::Table);
  }
  for(int action=0;action<3;++action){
    launch(AppId::Calculate);launch(AppId::Table);s.memory.f="x";s.memory.g="x+1";++s.revision;drain();
    tools(0);coefficient("1");coefficient("4");coefficient("1");
    const auto oldX=s.memory.variables[6];CalculatorApp::press(Key::Execute);require(calculatorJobs().running(),"range generation pending");
    if(action==0)CalculatorApp::openHome();
    else if(action==1){s.tableRange[1]=Value(9);}
    else{s.memory.f="x+5";++s.revision;}
    drain();require(!TableApp::data()||TableApp::data()->rows.empty(),"stale generation cannot publish rows");require(s.memory.variables[6]==oldX,"stale generation preserves x");
  }
  launch(AppId::Calculate);launch(AppId::Table);s.tableType=1;s.memory.f="x";++s.revision;drain();range("1","45","1");
  require(TableApp::heapBytes()==sizeof(TableData)+45*sizeof(TableRow),"45-row retained allocation bounded");
  tools(3);press(Key::Okay);require(snap().errorTitle=="Range ERROR"&&snap().rows==45,"Insert Row at capacity rejected (provisional label; exact target error PENDING per P16-TABLE-29)");press(Key::Return);press(Key::Return);
  for(int i=0;i<48;++i)press(Key::Down);press(Key::Delete);require(snap().rows==44&&snap().gridRow<=44,"last-row delete clamps selection");
  tools(3);press(Key::Okay);require(snap().rows==45&&TableApp::heapBytes()==sizeof(TableData)+45*sizeof(TableRow),"insert restores bounded capacity");
  launch(AppId::Calculate);require(TableApp::heapBytes()==0,"leaving Table releases active row storage");
  launch(AppId::Table);tools(1);press(Key::Okay);press(Key::Clear);press(Key::Digit1);press(Key::Add);press(Key::Execute);
  require(snap().screen=="ErrorDialog"&&snap().errorOrigin=="Define","shared Define P10 field error");press(Key::Return);require(snap().screen=="Define"&&snap().text=="1+","Define error preserves source");
  press(Key::Return);press(Key::Return);
  // Independent transition matrix, including the decoded ON event.
  for(auto reason:{TransitionReason::OtherAppLaunch,TransitionReason::NativeOn,TransitionReason::InputStyleChanged,TransitionReason::OutputStyleChanged,TransitionReason::TableTypeChanged}){
    s.app=AppId::Table;s.tableType=2;s.tableRange={{Value(-2),Value(8),Value(2)}};s.memory.f="x";s.memory.g="x+1";const auto epoch=s.tableEpoch;
    applyTransition(s,reason);require(s.tableEpoch!=epoch&&s.tableType==2,"retention clears data epoch retains type");
    require((s.memory.f.empty())==(reason==TransitionReason::NativeOn||reason==TransitionReason::InputStyleChanged||reason==TransitionReason::OutputStyleChanged),"definition retention matrix");
    require(s.tableRange[1].number==(reason==TransitionReason::OtherAppLaunch?5:8),"range retention matrix");
  }
  std::cout<<"P16 sizes: Ratio="<<RatioApp::stateBytes()<<" TableRow="<<sizeof(TableRow)<<" TableState="<<TableApp::stateBytes()<<" TableTask="<<sizeof(TableTask)<<" Job="<<sizeof(Job)<<" Result="<<sizeof(Result)<<" max row payload="<<45*sizeof(TableRow)<<"\n";
  std::cout<<"P16 native/controller: "<<checks<<" checks passed.\n";
  launch(AppId::Calculate);applyTransition(s,TransitionReason::InitializeAll);
}
