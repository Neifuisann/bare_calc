// P1: real shell/controller/editor/layout, deferred fake worker, no ROM code.
#include "calculator/runtime/calculator_app.h"
#include "calculator_platform_fake.h"
#include "calculator/runtime/calculator_job.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using K=ExpressionInput::Key;
namespace {
std::vector<uint8_t> frame;
K key(const std::string& name){
  static const std::map<std::string,K> keys={
    {"AC",K::Clear},{"OK",K::Okay},{"EXE",K::Execute},{"SHIFT",K::Shift},{"DEL",K::Delete},
    {"BACK",K::Return},{"HOME",K::Home},{"SETTINGS",K::Settings},{"TOOLS",K::Tools},
    {"CATALOG",K::Catalog},{"VARIABLE",K::Variable},{"FUNCTION",K::Function},{"FORMAT",K::Format},
    {"UP",K::Up},{"DOWN",K::Down},{"LEFT",K::Left},{"RIGHT",K::Right},{"PAGE_UP",K::PageUp},
    {"PAGE_DOWN",K::PageDown},{"FRACTION",K::Fraction},{"POWER",K::Power},{"SQUARE",K::Square},
    {"ROOT",K::Root},{"TIMES_TEN_POWER",K::Scientific},{"LOG",K::Log},{"SIN",K::Sin},{"COS",K::Cos},
    {"TAN",K::Tan},{"OPEN",K::Open},{"CLOSE",K::Close},{"X",K::X},{"DOT",K::Dot},{"ANS",K::Ans},
    {"ADD",K::Add},{"SUBTRACT",K::Subtract},{"MULTIPLY",K::Multiply},{"DIVIDE",K::Divide}};
  if(name.size()==1&&name[0]>='0'&&name[0]<='9')return static_cast<K>(static_cast<int>(K::Digit0)+name[0]-'0');
  auto it=keys.find(name);if(it==keys.end())throw std::runtime_error("Unknown semantic key: "+name);return it->second;
}
void service(){CalculatorApp::service(frame);}
void drain(){
  // Publish on the worker, adopt only through the real loop entrypoint.
  for(unsigned n=0;n<16;++n){
    service();
    if(!Calculator::calculatorJobs().running())return;
    if(!CalculatorPlatform::runPendingWorker())throw std::runtime_error("Job has no fake worker");
  }
  throw std::runtime_error("Job completion bound exceeded");
}
std::vector<uint8_t> pixels(const std::vector<uint8_t>& rle){
  std::vector<uint8_t> raw;
  for(size_t i=0;i<rle.size();){
    unsigned control=rle[i++];
    unsigned count=control<128?control+1:control-126;
    if(i+(control<128?count:1)>rle.size())throw std::runtime_error("Truncated frame");
    if(control<128){raw.insert(raw.end(),rle.begin()+i,rle.begin()+i+count);i+=count;}
    else raw.insert(raw.end(),count,rle[i++]);
    if(raw.size()>3024)throw std::runtime_error("Oversized frame");
  }
  if(raw.size()!=1512&&raw.size()!=3024)throw std::runtime_error("Invalid frame size");
  std::vector<uint8_t> out(192*63);
  for(size_t p=0;p<out.size();++p)out[p]=raw.size()==1512?((raw[p/8]>>(7-p%8))&1)*3:(raw[p/4]>>(6-2*(p%4)))&3;
  return out;
}
void rect(JsonArray a,const Calculator::MathLayout::Rect& r){a.add(r.x);a.add(r.y);a.add(r.width);a.add(r.height);}
JsonDocument observe(){
  const auto a=CalculatorApp::snapshot(frame);
  const auto b=CalculatorApp::snapshot(frame);
  if(a.text!=b.text||a.cursor!=b.cursor||a.frame!=b.frame||a.icons!=b.icons||a.screen!=b.screen||a.busy!=b.busy)
    throw std::runtime_error("Snapshot changed state");
  JsonDocument d;
  d["logical"]["source"]=a.text;d["logical"]["cursor"]=a.cursor;d["logical"]["fieldRole"]=a.fieldRole;
  d["logical"]["caretKind"]=a.logicalCaretKind;
  d["screen"]["id"]=a.screen;d["menu"]["id"]=a.menuScreen;d["menu"]["selection"]=a.selection;d["menu"]["first"]=a.first;
  d["menu"]["visibleRows"]=Calculator::kMenuVisibleRows;
  d["menu"]["lastSelected"]=!a.menuLabels.empty()&&a.selection+1==a.menuLabels.size();
  auto labels=d["menu"]["labels"].to<JsonArray>();for(auto& x:a.menuLabels)labels.add(x);
  auto sortedLabels=a.menuLabels;std::sort(sortedLabels.begin(),sortedLabels.end());
  auto labelSet=d["menu"]["labelSet"].to<JsonArray>();for(const auto& x:sortedLabels)labelSet.add(x);
  d["result"]["text"]=a.resultText;d["result"]["hasResult"]=a.hasResult;
  d["history"]["cursor"]=a.historyCursor;d["notice"]=a.notice;
  d["error"]["title"]=a.errorTitle;d["error"]["origin"]=a.errorOrigin;d["error"]["cursor"]=a.errorCursor;
  d["caret"]["visible"]=a.caret;d["caret"]["kind"]=a.caretKind;rect(d["caret"]["rect"].to<JsonArray>(),a.caretRect);
  d["caret"]["x"]=a.caretRect.x;d["caret"]["width"]=a.caretRect.width;d["caret"]["height"]=a.caretRect.height;
  d["icons"]["MATH"]=a.icons.mathio;d["icons"]["D"]=a.icons.deg;d["icons"]["S"]=a.icons.shift;
  d["icons"]["i"]=a.icons.cplxI;d["icons"]["angle"]=a.icons.angle;
  d["icons"]["R"]=a.icons.rad;d["icons"]["G"]=a.icons.gra;
  d["icons"]["FIX"]=a.icons.fix;d["icons"]["SCI"]=a.icons.sci;
  d["icons"]["E"]=a.icons.e;d["icons"]["VERIFY"]=a.icons.check;
  d["app"]["id"]=a.appId;d["app"]["base"]=a.base;
  d["app"]["historySize"]=Calculator::calculatorSession().history.size();
  d["grid"]["row"]=a.gridRow;d["grid"]["col"]=a.gridCol;
  d["grid"]["rows"]=a.rows;d["grid"]["cols"]=a.cols;d["grid"]["editing"]=a.editing;
  auto cells=d["grid"]["cells"].to<JsonArray>();for(const auto& v:a.cells)cells.add(v);
  d["result"]["page"]=a.page;
  d["icons"]["UP"]=a.icons.arrUp;d["icons"]["DOWN"]=a.icons.arrDown;
  d["icons"]["LEFT"]=a.icons.arrLeft;d["icons"]["RIGHT"]=a.icons.arrRight;
  PanelIcons::State zero, icon=a.icons;icon.sun=false;d["icons"]["allOff"]=icon==zero;
  return d;
}
// Strict recursive subset comparison. Unknown fields cannot silently pass.
bool compare(JsonVariantConst expected,JsonVariantConst actual,std::string path,std::string& why){
  if(actual.isNull())throw std::runtime_error("Unimplemented observation field: "+path);
  if(expected.is<JsonObjectConst>()){
    for(auto kv:expected.as<JsonObjectConst>())
      if(!compare(kv.value(),actual[kv.key()],path+"."+kv.key().c_str(),why))return false;
    return true;
  }
  if(expected.is<JsonArrayConst>()){
    auto e=expected.as<JsonArrayConst>();auto a=actual.as<JsonArrayConst>();
    if(e.size()==a.size()){bool ok=true;for(size_t i=0;i<e.size();++i)if(!compare(e[i],a[i],path+"["+std::to_string(i)+"]",why)){ok=false;break;}if(ok)return true;}
  }else if(!actual.isNull()&&expected==actual)return true;
  std::string e,a;serializeJson(expected,e);serializeJson(actual,a);why=path+": expected "+e+", got "+a;return false;
}
JsonDocument measure(JsonVariantConst spec,const std::vector<uint8_t>& p){
  JsonDocument d;auto r=spec["region"].as<JsonArrayConst>();
  if(r.size()!=4)throw std::runtime_error("Raster region needs x,y,w,h");
  int x=r[0],y=r[1],w=r[2],h=r[3];
  if(x<0||y<0||w<1||h<1||x+w>192||y+h>63)throw std::runtime_error("Raster region outside panel");
  int left=192,top=63,right=-1,bottom=-1,count=0;std::set<int> levels;
  const auto s=CalculatorApp::snapshot(frame);
  for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i){
    if((spec["maskCaret"]|false)&&s.caret&&i>=s.caretRect.x&&i<s.caretRect.x+s.caretRect.width&&j>=s.caretRect.y&&j<s.caretRect.y+s.caretRect.height)continue;
    int v=p[j*192+i];levels.insert(v);
    if(v&&(spec["level"].isNull()||v==spec["level"].as<int>())){left=std::min(left,i);right=std::max(right,i);top=std::min(top,j);bottom=std::max(bottom,j);++count;}
  }
  rect(d["bbox"].to<JsonArray>(),{static_cast<int16_t>(count?left:0),static_cast<int16_t>(count?top:0),static_cast<int16_t>(count?right-left+1:0),static_cast<int16_t>(count?bottom-top+1:0)});
  d["width"]=count?right-left+1:0;d["height"]=count?bottom-top+1:0;d["count"]=count;
  // Either intermediate panel level is grey (inactive fields paint level 1).
  d["hasGrey"]=levels.count(1)!=0||levels.count(2)!=0;
  if(spec["fractionGaps"]|false){
    // Find the longest horizontal ink run, then count completely empty rows
    // around it. This tests gaps independently of origin and rule width.
    int longest=0,rule=-1;std::vector<bool> occupied(h,false);
    for(int j=y;j<y+h;++j){int run=0;for(int i=x;i<x+w;++i){
      const bool masked=(spec["maskCaret"]|false)&&s.caret&&i>=s.caretRect.x&&i<s.caretRect.x+s.caretRect.width&&j>=s.caretRect.y&&j<s.caretRect.y+s.caretRect.height;
      if(p[j*192+i]&&!masked){occupied[j-y]=true;if(++run>longest){longest=run;rule=j-y;}}else run=0;
    }}
    if(rule<0)throw std::runtime_error("No fraction rule found");
    int above=0,below=0;
    for(int j=rule-1;j>=0&&!occupied[j];--j)++above;
    for(int j=rule+1;j<h&&!occupied[j];++j)++below;
    d["blankAboveRule"]=above;d["blankBelowRule"]=below;
  }
  return d;
}
}
#include "calculator_menu_controller_p9_test.h"
#include "calculator_error_controller_p10_test.h"
#include "calculator_history_p11_test.h"
#include "calculator_calc_controller_p13_test.h"
#include "calculator_apps_p15_test.h"
#include "calculator_apps_p16_test.h"
#include "calculator_apps_p17_test.h"
int main(int argc,char** argv){
  try{
    if(argc==2&&std::string(argv[1])=="--self-test"){
      CalculatorPlatform::fakeDeferWorker=true;
      CalculatorApp::openHome();service();CalculatorApp::press(K::Okay);service();
      CalculatorApp::press(K::Digit1);service();CalculatorApp::press(K::Execute);service();
      const auto queued=CalculatorApp::snapshot(frame);
      if(!queued.busy||queued.hasResult)throw std::runtime_error("Worker was not deferred");
      if(!CalculatorPlatform::runPendingWorker())throw std::runtime_error("No worker queued");
      if(CalculatorApp::snapshot(frame).hasResult)throw std::runtime_error("Completion adopted outside service");
      drain();const auto completed=CalculatorApp::snapshot(frame);
      if(completed.busy||!completed.hasResult||completed.resultText!="1")throw std::runtime_error("Completion not adopted");
      auto copy=completed;copy.text="mutated";copy.frame.clear();
      if(CalculatorApp::snapshot(frame).text!="1"||CalculatorApp::snapshot(frame).frame.empty())throw std::runtime_error("Snapshot aliases live state");
      if(CalculatorApp::service(frame))throw std::runtime_error("Snapshot dirtied rendering");
      CalculatorApp::press(K::Digit2);CalculatorApp::press(K::Execute);service();
      CalculatorApp::openHome();service();
      if(!CalculatorPlatform::runPendingWorker())throw std::runtime_error("Cancelled worker missing");
      drain();if(!CalculatorApp::isHome())throw std::runtime_error("Stale completion changed Home");
      CalculatorApp::press(K::Okay);drain();CalculatorApp::press(K::Clear);drain();
      CalculatorApp::press(K::Digit9);drain();
      if(CalculatorApp::snapshot(frame).caretKind!="Insert")throw std::runtime_error("Insert kind lost in snapshot");
      CalculatorApp::press(K::Shift);drain();CalculatorApp::press(K::Delete);drain();
      if(CalculatorApp::snapshot(frame).caretKind!="InsArmed")throw std::runtime_error("InsArmed kind lost in snapshot");
      if(CalculatorApp::snapshot(frame).logicalCaretKind!="InsArmed")throw std::runtime_error("Logical InsArmed lost");
      CalculatorPlatform::fakeMillis+=500;service();
      if(CalculatorApp::snapshot(frame).logicalCaretKind!="InsArmed")throw std::runtime_error("Blink lost logical kind");
      CalculatorApp::openHome();service();
      Calculator::calculatorSession().settings.io=Calculator::IO::LineLine;
      CalculatorApp::press(K::Okay);drain();CalculatorApp::press(K::Clear);drain();
      CalculatorApp::press(K::Digit9);drain();CalculatorApp::press(K::Shift);drain();CalculatorApp::press(K::Delete);drain();
      if(CalculatorApp::snapshot(frame).logicalCaretKind!="Overwrite")throw std::runtime_error("Logical Overwrite lost");
      std::cout<<"P8 snapshot: Insert/InsArmed/Overwrite renderer kinds, including blink-off, passed.\n";
      p9ControllerChecks();
      p10ErrorControllerChecks();
      p11HistoryUnitChecks();
      p11HistoryControllerChecks();
      p13CalcControllerChecks();
      p15AppControllerChecks();
      p16AppControllerChecks();
      p17AppControllerChecks();
      std::cout<<"Reference adapter: deferred completion, service-only adoption, detached snapshots, no dirty side effects, cancelled completion passed.\n";
      return 0;
    }
    if(argc!=3 && argc!=4)throw std::runtime_error("usage: reference.exe cases.json case-id [capture-directory] (one cold process per case)");
    std::ifstream input(argv[1]);JsonDocument cases;
    auto error=deserializeJson(cases,input);if(error)throw std::runtime_error(error.c_str());
    JsonVariantConst c;for(auto row:cases["cases"].as<JsonArrayConst>())if(row["id"]==argv[2])c=row;
    if(c.isNull())throw std::runtime_error("Case not found");
    CalculatorPlatform::fakeNvs().clear();CalculatorPlatform::fakeMillis=0;CalculatorPlatform::fakeDeferWorker=true;
    // P12 C50: FORMAT behavior is setting-aware (FORMAT Key=Menu vs Decimal
    // toggle). Cases pin a non-default choice explicitly in initial.settings;
    // everything else cold-starts on firmware defaults.
    if(c["initial"]["settings"]["formatMenu"].is<const char*>()){
      const std::string formatMenu=c["initial"]["settings"]["formatMenu"].as<std::string>();
      if(formatMenu!="Menu"&&formatMenu!="Decimal toggle")throw std::runtime_error("Unknown formatMenu setting");
      Calculator::calculatorSession().settings.formatMenu=(formatMenu=="Menu");
    }
    if(c["initial"]["settings"]["complexResult"].is<const char*>()){
      const std::string mode=c["initial"]["settings"]["complexResult"].as<std::string>();
      if(mode!="Rectangular"&&mode!="Polar")throw std::runtime_error("Unknown Complex Result setting");
      Calculator::calculatorSession().settings.complexResult=mode=="Polar"?Calculator::ComplexResult::Polar:Calculator::ComplexResult::Rectangular;
    }
    CalculatorApp::openHome();service();
    const std::string initialApp=c["initial"]["app"]|"Calculate";
    const std::map<std::string,int> apps={{"Calculate",0},{"Statistics",1},{"Distribution",2},{"Table",4},{"Equation",5},{"Inequality",6},{"Complex",7},{"Base-N",8},{"Matrix",9},{"Vector",10},{"Ratio",11}};
    auto app=apps.find(initialApp);if(app==apps.end())throw std::runtime_error("Unsupported initial app");
    for(int n=0;n<app->second;++n)CalculatorApp::press(K::Right);
    CalculatorApp::press(K::Okay);drain();
    std::map<std::string,JsonDocument> captures;
    std::map<std::string,std::vector<uint8_t>> rasters;
    for(auto step:c["keys"].as<JsonArrayConst>()){
      if(step.is<const char*>()){CalculatorApp::press(key(step.as<std::string>()));drain();}
      else if(step["capture"].is<const char*>()){
        std::string name=step["capture"].as<std::string>();if(captures.count(name))throw std::runtime_error("Duplicate capture");
        captures[name]=observe();rasters[name]=pixels(frame);
        if(argc==4) {
          const auto dir=std::filesystem::path(argv[3]);
          std::filesystem::create_directories(dir);
          std::ofstream pgm(dir/(std::string(argv[2])+"-"+name+".pgm"),std::ios::binary);
          pgm<<"P5\n192 63\n255\n";
          for(uint8_t level:rasters[name])pgm.put(static_cast<char>(255-level*85));
          if(!pgm)throw std::runtime_error("Could not write native capture");
        }
        if(CalculatorApp::isHome()){
          auto path=std::filesystem::path(argv[1]).parent_path().parent_path()/"calculator_references"/"home-1.bin";
          std::ifstream physical(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(physical)),{});
          if(bytes.size()!=3024)throw std::runtime_error("Missing physical Home reference");
          // Shortcut badges are the firmware's own addition to the physical
          // Home (calculator_home_test.cpp masks the same 7x7 corner per item).
          bool equal=true;for(size_t p=0;p<rasters[name].size();++p){
            const size_t x=p%192,y=p/192;
            if(x%64>=52&&x%64<59&&y%32>=1&&y%32<8)continue;
            if(rasters[name][p]!=((bytes[p/4]>>(6-2*(p%4)))&3))equal=false;
          }
          captures[name]["physicalHomeMatch"]=equal;
        }
        // Geometry is measured now, not using the final state's caret mask.
        for(auto check:c["checks"].as<JsonArrayConst>())if(check["capture"]==name&&check["measure"].is<JsonObjectConst>()){
          if(check["measure"]["caretOff"]|false){
            CalculatorPlatform::fakeMillis=500;service();
            captures[name]["measurements"][check["id"].as<std::string>()].set(measure(check["measure"],pixels(frame)));
            CalculatorPlatform::fakeMillis=0;service();
          }else captures[name]["measurements"][check["id"].as<std::string>()].set(measure(check["measure"],rasters[name]));
        }
      }else throw std::runtime_error("Unknown replay step");
    }
    int pass=0,gap=0,pending=0,fail=0;
    for(auto check:c["checks"].as<JsonArrayConst>()){
      const std::string id=check["id"].as<std::string>();
      if(check["native"]=="pending"){++pending;continue;}
      std::string name=check["capture"].as<std::string>();if(!captures.count(name))throw std::runtime_error("Missing capture: "+name);
      JsonVariantConst actual=captures[name].as<JsonVariantConst>();
      if(check["measure"].is<JsonObjectConst>())actual=actual["measurements"][id];
      JsonDocument difference;
      if(check["delta"].is<JsonObjectConst>()){
        const std::string before=check["delta"]["capture"].as<std::string>();
        if(!captures.count(before))throw std::runtime_error("Missing delta capture");
        JsonVariantConst a=actual,b=captures[before].as<JsonVariantConst>();
        for(auto part:check["delta"]["path"].as<JsonArrayConst>()){a=a[part.as<std::string>()];b=b[part.as<std::string>()];}
        if(!a.is<int>()||!b.is<int>())throw std::runtime_error("Non-numeric delta");
        difference["value"]=a.as<int>()-b.as<int>();actual=difference.as<JsonVariantConst>();
      }
      std::string why;bool matches=compare(check["expect"],actual,"",why);
      bool known=check["native"].is<JsonObjectConst>();
      if(matches&&!known)++pass;
      else if(!matches&&known){++gap;std::cout<<"GAP "<<id<<" "<<check["native"]["gap"].as<std::string>()<<" "<<why<<"\n";}
      else {++fail;std::cout<<"FAIL "<<id<<" "<<(matches?"known gap unexpectedly passes; update ledger":why)<<"\n";}
    }
    std::cout<<argv[2]<<": "<<pass<<" pass, "<<gap<<" known gaps, "<<pending<<" pending, "<<fail<<" failures\n";
    return fail?1:0;
  }catch(const std::exception& e){std::cerr<<"HARNESS ERROR: "<<e.what()<<"\n";return 2;}
}
