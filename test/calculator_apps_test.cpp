#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/ui/calculator_get_started.h"
#include "ui/canvas_text.h"
#include <fstream>
#include <cstdlib>
// Shared app layer: section 18 retention reducer, grid cursor and cell drafts,
// the persistent record, and the whole overlay app driven through its public
// boundary with the platform fake (inline or parked worker, settable clock,
// in-memory NVS).
#include "calculator/runtime/calculator_app.h"
#include "calculator/apps/calculator_calculate.h"
#include "calculator/runtime/calculator_job.h"
#include "calculator/runtime/calculator_storage.h"
#include "calculator/ui/calculator_grid.h"
#include "calculator_history_p11_test.h"
#include "calculator_platform_fake.h"
#include "calculator/ui/calculator_render.h"
#include "calculator/runtime/calculator_session.h"
#include "calculator/runtime/calculator_store.h"
#include <cassert>
#include <iostream>
using namespace Calculator;
using K = ExpressionInput::Key;
using R = TransitionReason;
namespace CP = CalculatorPlatform;

CalculatorSession loaded() {
  CalculatorSession s;
  s.settings.angle = Angle::Radian;
  s.memory.ans = Value(7);
  s.memory.preAns = Value(6);
  s.memory.variables[0] = Value(5);
  s.memory.f = "x";
  s.memory.g = "x+1";
  s.verify = true;
  s.history = {HistoryEntry{"1+1", ResultSummary{}}};
  return s;
}
bool scalarsKept(const CalculatorSession& s) {
  return s.memory.ans.re.number == 7 && s.memory.variables[0].re.number == 5;
}
CalculatorSession after(R reason) {
  auto s = loaded();
  applyTransition(s, reason);
  return s;
}

void retention() {
  auto s = after(R::HomeOpen);
  assert(s.history.empty() && scalarsKept(s) && s.memory.preAns.number == 6 && s.verify &&
         s.memory.f == "x" && s.settings.angle == Angle::Radian);

  s = after(R::OtherAppLaunch);
  assert(s.history.empty() && scalarsKept(s) && s.memory.preAns.number == 0 && !s.verify &&
         s.memory.g == "x+1");

  s = after(R::InputStyleChanged);
  assert(s.history.empty() && s.memory.f.empty() && s.memory.g.empty() && s.verify && scalarsKept(s));
  s = after(R::OutputStyleChanged);
  assert(s.history.empty() && s.memory.f == "x" && s.verify);

  s = after(R::VariableMemoryReset);
  assert(s.memory.ans.re.number == 0 && s.memory.preAns.number == 0 && s.memory.variables[0].re.number == 0);
  assert(s.memory.f == "x" && s.memory.g == "x+1" && s.verify && s.history.size() == 1 &&
         s.settings.angle == Angle::Radian);

  s = after(R::SettingsDataReset);
  assert(scalarsKept(s) && s.memory.preAns.number == 6 && s.memory.f.empty() && !s.verify &&
         s.history.empty() && s.settings.angle == Angle::Degree);

  s = after(R::InitializeAll);
  assert(s.memory.ans.re.number == 0 && s.memory.variables[0].re.number == 0 && s.memory.f.empty() &&
         !s.verify && s.history.empty() && s.settings.angle == Angle::Degree);

  s = loaded();
  launchApp(s, AppId::Calculate);  // reopening the running app retains everything
  assert(s.history.size() == 1 && s.verify && s.memory.preAns.number == 6);
  launchApp(s, AppId::Complex);
  assert(s.app == AppId::Complex && s.history.empty() && !s.verify && s.memory.preAns.number == 0 &&
         scalarsKept(s));

  assert(appAtHomeIndex(0) == AppId::Calculate && appAtHomeIndex(7) == AppId::Complex &&
         appAtHomeIndex(12) == AppId::MathBox);
  assert(!inputStyleChanged(IO::MathMath, IO::MathDecimal));
  assert(!inputStyleChanged(IO::LineDecimal, IO::LineLine));
  assert(inputStyleChanged(IO::MathDecimal, IO::LineLine));
}

void grid() {
  const GridPolicy policy{10, 3, 4, 2};
  GridCursor c;
  assert(moveGrid(c, policy, K::Left) && moveGrid(c, policy, K::Up) && c.row == 0 && c.col == 0);
  moveGrid(c, policy, K::Right);
  moveGrid(c, policy, K::Right);
  moveGrid(c, policy, K::Right);
  assert(c.col == 2 && c.firstCol == 1);
  for (int i = 0; i < 5; ++i) moveGrid(c, policy, K::Down);
  assert(c.row == 5 && c.firstRow == 2);
  moveGrid(c, policy, K::PageDown);
  assert(c.row == 9 && c.firstRow == 6 && c.col == 2);
  moveGrid(c, policy, K::PageDown);
  assert(c.row == 9 && c.firstRow == 6);
  moveGrid(c, policy, K::PageUp);
  assert(c.row == 5 && c.firstRow == 5);
  assert(!moveGrid(c, policy, K::Digit1) && c.row == 5);

  clampGrid(c, GridPolicy{3, 3, 4, 2});  // rows deleted below the cursor
  assert(c.row == 2 && c.firstRow == 0 && c.col == 2);
  clampGrid(c, GridPolicy{});
  assert(c.row == 0 && c.col == 0 && c.firstRow == 0 && c.firstCol == 0);
  assert(moveGrid(c, GridPolicy{}, K::Down) && c.row == 0);

  CellEdit edit;
  assert(!edit.stale(9));
  edit.begin({2, 1}, 5);
  edit.editor.press(K::Digit3);
  assert(edit.active && edit.editor.text() == "3" && edit.target.row == 2 && edit.target.col == 1);
  assert(!edit.stale(5) && edit.stale(6));
  edit.abort();
  assert(!edit.active && edit.editor.text().empty() && edit.target.row == -1 && !edit.stale(6));
}

// Inverse of CanvasText::packBitsRle, as in calculator_render_test.cpp.
// Returns the 2-bpp level (0-3) so grey (2) stays distinguishable from black
// (3); inkIn() treats any nonzero level as ink for presence-only checks.
int pixel(const std::vector<uint8_t>& rle, int x, int y) {
  std::vector<uint8_t> bits;
  for (size_t i = 0; i < rle.size();) {
    const uint8_t control = rle[i++];
    if (control < 128) {bits.insert(bits.end(), rle.begin() + i, rle.begin() + i + control + 1); i += control + 1;}
    else {bits.insert(bits.end(), control - 126, rle[i]); ++i;}
  }
  assert(bits.size() == 3024);
  const size_t at = static_cast<size_t>(y) * 192 + x;
  return (bits[at >> 2] >> (6-2*(at&3))) & 3;
}
bool inkIn(const std::vector<uint8_t>& rle, int left, int top, int right, int bottom) {
  for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) if (pixel(rle, x, y) != 0) return true;
  return false;
}

void dataGrid() {
  DataGridView view;
  view.headers = {"x", "Freq"};
  for (int row = 1; row <= 6; ++row) view.cells.insert(view.cells.end(), {std::to_string(row), "1"});
  view.cursor.row = 4;
  view.cursor.col = 1;
  view.cursor.firstRow = 2;
  view.footer = "1";
  const auto f = renderDataGrid(view);
  // Rows 3..6 shown; row 5 (third line) Freq cell inverted, its x cell not.
  const int width = (192 - 16 - 3) / 2, top = 10 + 2 * 10;
  assert(pixel(f, 16 + width + 1, top) && !pixel(f, 16 + 1, top));
  assert(!pixel(f, 16 + 1, 0) && inkIn(f, 16, 0, 191, 10));  // upright header
  assert(inkIn(f, 0, 11, 15, 20));                             // row numbers
  assert(inkIn(f, 189, 11, 191, 50));                          // scrollbar past four rows
  assert(inkIn(f, 0, 52, 191, 62));                            // selected value
  view.cells.resize(8);                                        // four rows: no scrollbar
  assert(!inkIn(renderDataGrid(view), 189, 11, 191, 48));  // footer value starts on row 49
}

// A v3 record as the pre-exact firmware wrote it: values carried at most two
// int64 display terms. Ans = 1+sqrt(2), x = 3/4, everything else zero.
std::vector<uint8_t> legacyV3Record() {
  std::vector<uint8_t> p;
  auto u8=[&](uint8_t v){p.push_back(v);};
  auto i64=[&](int64_t v){for(int i=0;i<8;++i)u8(static_cast<uint8_t>(static_cast<uint64_t>(v)>>(8*i)));};
  auto text=[&](const std::string& t){u8(static_cast<uint8_t>(t.size()));u8(0);p.insert(p.end(),t.begin(),t.end());};
  auto term=[&](bool valid,int64_t n,int64_t d,int64_t r){u8(valid?1:0);if(valid){i64(n);i64(d);i64(r);}};
  auto value=[&](const std::string& digits,bool exact,int64_t n,int64_t d,int64_t r,bool extra,int64_t en,int64_t ed,int64_t er){
    text(digits);term(exact,n,d,r);term(extra,en,ed,er);};
  for(uint8_t b:{0,0,0,3,0,32})u8(b);                                                   // settings
  u8(0);value("2.414213562373095048801688724209698078570",true,1,1,1,true,1,1,2);       // Ans
  value("0",true,0,1,1,false,0,0,0);                                                    // PreAns
  for(int i=0;i<9;++i){u8(0);if(i==6)value("0.75",true,3,4,1,false,0,0,0);else value("0",true,0,1,1,false,0,0,0);}
  text("");text("");                                                                    // f, g
  for(int i=0;i<10;++i){u8(0);u8(0);}                                                   // aggregates
  uint32_t crc=0xFFFFFFFFu;
  for(uint8_t b:p){crc^=b;for(int k=0;k<8;++k)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
  crc=~crc;
  std::vector<uint8_t> r{'C','A','L','C',3,static_cast<uint8_t>(p.size()),static_cast<uint8_t>(p.size()>>8)};
  for(int i=0;i<4;++i)r.push_back(static_cast<uint8_t>(crc>>(8*i)));
  r.insert(r.end(),p.begin(),p.end());
  return r;
}
Value evaluated(const std::string& expression) {
  return evaluate(Request{expression, Settings{}, Memory{}, false, 1}).value;
}

void storeRecords() {
  Settings settings;
  settings.angle = Angle::Gradian;
  settings.complexResult = ComplexResult::Polar;
  settings.number = NumberFormat::Fix;
  settings.digits = 4;
  settings.engineer = true;
  Memory memory;
  memory.ans = evaluated("1+sqrt(2)");  // two exact terms
  memory.preAns = Value(-3);
  memory.variables[2] = ScalarValue::of(Value(5), evaluated("sqrt(3)/2"));
  memory.f = "x^2+1";
  memory.g = "f(x)";
  const auto record = encodeV2(settings, memory);
  Settings s;
  Memory m;
  assert(decodeV2(record, s, m));
  assert(s.angle == Angle::Gradian && s.complexResult == ComplexResult::Polar && s.number == NumberFormat::Fix &&
         s.digits == 4 && s.engineer && s.scientificPower && !s.comma);
  assert(m.ans.re.exact().valid && m.ans.re.extra().valid && m.ans.re.extra().radicand == 2 &&
         m.ans.re.number == memory.ans.re.number);
  assert(m.variables[2].complex && m.variables[2].re.number == 5 && m.variables[2].im.exact().radicand == 3 &&
         m.variables[2].im.exact().denominator == 2);
  assert(m.preAns.number == -3 && m.f == "x^2+1" && m.g == "f(x)" && !m.variables[0].complex);
  // Any flipped byte or truncation rejects the whole record and leaves the outputs alone.
  for (size_t i = 0; i < record.size(); ++i) {
    auto bad = record;
    bad[i] ^= 0x20;
    Settings untouched;
    assert(!decodeV2(bad, untouched, m) && untouched.angle == Angle::Degree);
  }
  assert(!decodeV2(std::vector<uint8_t>(record.begin(), record.end() - 1), s, m));
  // v4 keeps every exact form, including those no display term can hold.
  {
    Memory exact;
    exact.ans = evaluated("ln(3)");
    exact.variables[0] = evaluated("2^200");
    exact.variables[1] = evaluated("sqrt(1+sqrt(2))");
    exact.variables[2] = ScalarValue::of(evaluated("pi/6"), evaluated("-sqrt(3)/2"));
    Settings es;
    Memory em;
    assert(decodeV2(encodeV2(settings, exact), es, em));
    assert(em.ans.re.symbolic.str() == "ln(3)" && em.ans.re.number == exact.ans.re.number);
    assert(em.variables[0].re.symbolic == exact.variables[0].re.symbolic && em.variables[0].re.symbolic.str().size() == 61);
    assert(em.variables[1].re.symbolic.str() == "sqrt(1+sqrt(2))");
    assert(em.variables[2].complex && em.variables[2].re.symbolic.str() == "1/6*pi" &&
           em.variables[2].im.symbolic.str() == "-1/2*sqrt(3)");
    // An exact form past the text limit is stored as its decimal alone.
    exact.ans = Value::of(ExactNumber::parse("(2^3700+1)/2^3700"));  // ~2230 characters, value ~1
    assert(exact.ans.re.isExact() && exact.ans.re.symbolic.str().size() > 1024);
    assert(decodeV2(encodeV2(settings, exact), es, em) && !em.ans.re.isExact() && em.ans.re.number == exact.ans.re.number);
  }
  // A v3 record (int64 display terms) still loads, converted to exact forms.
  {
    Settings ls;
    Memory lm;
    assert(decodeV2(legacyV3Record(), ls, lm));
    assert(lm.ans.re.symbolic.str() == "1+sqrt(2)" && lm.ans.re.extra().radicand == 2);
    assert(lm.variables[6].re.symbolic.str() == "3/4" && lm.variables[6].re.number == Decimal("0.75"));
    assert(lm.preAns.symbolic.zero() && ls.digits == 3);
  }
  // v1 JSON migrates; its complex bool becomes the Complex Result setting.
  const std::string v1 = R"({"version":1,"angle":1,"io":2,"number":0,"digits":3,"complex":true,)"
                         R"("ans":{"number":"0.5","n":1,"d":2,"r":1},"vars":[{"number":"7"}],"f":"x+1","g":""})";
  assert(decodeV1(v1, s, m) && s.angle == Angle::Radian && s.io == IO::LineLine &&
         s.complexResult == ComplexResult::Polar);
  assert(m.ans.re.exact().denominator == 2 && m.variables[0].re.number == 7 && m.f == "x+1");
  // Loading prefers v2 and falls back to v1.
  CP::fakeNvs().clear();
  CP::saveString("calc_v1", "state", v1);
  CalculatorSession session;
  loadStoredState(session);
  assert(session.settings.angle == Angle::Radian);
  CP::saveBlob("calc_v2", "state", record);
  loadStoredState(session);
  assert(session.settings.angle == Angle::Gradian);
  CP::fakeNvs().clear();
}

std::vector<uint8_t> frame;
void pump() { CalculatorApp::service(frame); }
void keys(std::initializer_list<K> list) {
  for (K k : list) {
    CalculatorApp::press(k);
    pump();
  }
}

void appFlow() {
  CalculatorSession& s = calculatorSession();
  CP::fakeNvs().clear();
  CalculatorApp::openHome();
  pump();
  assert(CalculatorApp::isHome() && CalculatorApp::desiredIcons() == PanelIcons::State{});

  keys({K::Execute, K::Digit1, K::Add, K::Digit2, K::Execute});  // Calculate is the first cell
  assert(s.memory.ans.re.number == 3 && lastRender().presented == "3" && s.history.size() == 1);

  // A job cancelled by Home finishes later and never publishes.
  CP::fakeDeferWorker = true;
  keys({K::Multiply, K::Digit5, K::Execute});
  CalculatorApp::press(K::Home);
  assert(CP::runPendingWorker());
  pump();
  assert(s.memory.ans.re.number == 3 && s.history.empty() && CalculatorApp::isHome());

  // Reopening keeps the draft; AC cancels a running job the same way.
  CP::fakeDeferWorker = false;
  keys({K::Execute, K::Execute});
  assert(s.memory.ans.re.number == 15);
  CP::fakeDeferWorker = true;
  keys({K::Execute, K::Clear});
  assert(CP::runPendingWorker());
  pump();
  assert(s.memory.ans.re.number == 15);
  CP::fakeDeferWorker = false;

  // CATALOG opened from a Store field returns to that field, not the entry.
  keys({K::Variable, K::Digit7});
  const std::vector<uint8_t> field = frame;
  assert(CalculatorApp::desiredIcons().deg);
  keys({K::Catalog});
  assert(frame != field && !CalculatorApp::desiredIcons().deg);
  keys({K::Return});
  assert(frame == field && CalculatorApp::desiredIcons().deg);
  keys({K::Execute});
  assert(s.memory.variables[0].re.number == 7);

  // Persistence waits for a quiet period, then writes one record that loads back.
  assert(CP::fakeNvs().empty());
  CP::fakeMillis = 2500;
  pump();
  std::vector<uint8_t> record;
  Settings settings;
  Memory memory;
  assert(CP::loadBlob("calc_v2", "state", record) && decodeV2(record, settings, memory) &&
         memory.variables[0].re.number == 7 && memory.ans.re.number == 15);

  // Complex (Home cell 7): i from SHIFT 9, the shared Ans, PreAns untouched, i icon lit.
  keys({K::Return, K::Home, K::Down, K::Down, K::Right, K::Execute});
  assert(s.app == AppId::Complex && s.memory.preAns.number == 0);
  s.memory.preAns = Value(9);
  keys({K::Digit1, K::Add, K::Shift, K::Digit9, K::Execute});
  assert(s.memory.ans.complex && s.memory.ans.re.number == 1 && s.memory.ans.im.number == 1);
  assert(s.memory.preAns.number == 9 && lastRender().presented == "1+i");
  assert(CalculatorApp::desiredIcons().cplxI && !CalculatorApp::desiredIcons().angle);

  // Back in Calculate the complex Ans is Math ERROR where referenced, and stays stored.
  keys({K::Home, K::Left, K::Up, K::Up, K::Execute});
  assert(s.app == AppId::Calculate && s.memory.preAns.number == 0 && !CalculatorApp::desiredIcons().cplxI);
  keys({K::Clear, K::Ans, K::Add, K::Digit1, K::Execute});
  assert(frame == renderErrorDialog(Error::Math) && !lastRender().caret &&
         CalculatorApp::desiredIcons() == PanelIcons::State{} && s.memory.ans.complex);
  CP::fakeMillis = 5000;
  pump();
  assert(CP::loadBlob("calc_v2", "state", record) && decodeV2(record, settings, memory) && memory.ans.complex);

  // Complex Store: the field takes i and a complex Ans and keeps both parts.
  keys({K::Home, K::Down, K::Down, K::Right, K::Execute});
  assert(s.app == AppId::Complex);
  keys({K::Digit3, K::Add, K::Shift, K::Digit9, K::Execute});
  keys({K::Variable, K::Right, K::Digit2, K::Shift, K::Digit9, K::Execute});
  assert(s.memory.variables[1].complex && s.memory.variables[1].im.number == 2 && s.memory.variables[1].re.number == 0);
  keys({K::Left, K::Digit0, K::Clear, K::Ans, K::Execute, K::Return});
  assert(s.memory.variables[0].complex && s.memory.variables[0].re.number == 3 && s.memory.variables[0].im.number == 1);

  // Replay walks past the previous entry: two Ups reach the first calculation.
  keys({K::Clear, K::Digit1, K::Add, K::Digit1, K::Execute, K::Clear, K::Digit2, K::Add, K::Digit2, K::Execute});
  keys({K::Up, K::Up});
  assert(CalculatorApp::desiredIcons().arrDown && !CalculatorApp::desiredIcons().arrUp);
  keys({K::Execute});
  assert(s.memory.ans.complex && s.memory.ans.re.number == 3 && s.memory.ans.im.number == 1);

  // Line output lays a complex result out with its operators.
  keys({K::Settings, K::Okay, K::Okay, K::Down, K::Down, K::Okay, K::Return, K::Return});
  assert(s.settings.io == IO::LineLine);
  keys({K::Clear, K::Digit1, K::Subtract, K::Digit2, K::Shift, K::Digit9, K::Execute});
  assert(lastRender().presented == "1-2i" && inkIn(frame, 0, 43, 191, 62));
}

void repeat(K key, int count) { for (int i = 0; i < count; ++i) keys({key}); }

// P13: the CALC value field is a selected (inked) bottom row. A blank draft
// shows the stored value with no caret; once typing starts the caret blinks
// ink-off inside that row. Toggle the blink phase and diff the two frames:
// any change must stay in the field band, and exists only while typing.
bool calcPromptCaretInField(bool typing = false) {
  CP::fakeMillis = 0;
  pump();
  const auto on = frame;
  CP::fakeMillis = 500;
  pump();
  const auto off = frame;
  CP::fakeMillis = 0;
  pump();
  bool anyBlink = false, onlyField = true;
  for (int y = 0; y < 63; ++y)
    for (int x = 0; x < 192; ++x)
      if (pixel(on, x, y) != pixel(off, x, y)) {
        anyBlink = true;
        if (y < 31) onlyField = false;
      }
  return anyBlink == typing && onlyField && inkIn(on, 0, 48, 187, 62);
}

// P13 A-10: APP-CALC-01 through the public overlay boundary. 3A+B, CALC,
// A=5 EXE, B=10 EXE (no auto-run), final EXE -> 25, EXE reopens prompts,
// changed values recalculate. Prompt accepts never touch history/Ans; each
// final EXE pushes exactly one entry.
void calcFlow() {
  CalculatorSession& s = calculatorSession();
  CalculatorApp::openHome();
  pump();
  CP::fakeMillis = 0;  // pin the caret blink on for the caret-band asserts below
  s.settings = Settings{};
  for (int i = 0; i < 14; ++i) keys({K::Left});
  for (int i = 0; i < 5; ++i) keys({K::Up});
  keys({K::Okay, K::Clear});  // Calculate, empty entry, synced editors
  assert(s.history.empty());
  const auto ansBefore = s.memory.ans;
  keys({K::Digit3, K::Shift, K::Digit4, K::Add, K::Shift, K::Digit5});
  keys({K::Tools, K::Okay});
  assert(calcPromptCaretInField());  // prompting A
  assert(s.history.empty());
  keys({K::Digit5});
  assert(calcPromptCaretInField(true));  // typing replaces the stored value
  keys({K::Execute});
  assert(s.memory.variables[0].re.number == 5);
  assert(calcPromptCaretInField());  // prompting B
  assert(s.history.empty() && s.memory.ans.re.number == ansBefore.re.number);
  keys({K::Digit1, K::Digit0, K::Execute});
  assert(s.memory.variables[1].re.number == 10);
  assert(s.history.empty() && s.memory.ans.re.number == ansBefore.re.number);
  assert(calcPromptCaretInField());  // AwaitFinalExecute: committed, not calculated
  keys({K::Execute});
  assert(s.memory.ans.re.number == 25 && lastRender().presented == "25");
  assert(s.history.size() == 1 && !lastRender().caret);
  keys({K::Execute});  // EXE on the CALC result reopens the prompts
  assert(calcPromptCaretInField());
  assert(s.history.size() == 1);
  keys({K::Digit2, K::Execute, K::Digit4, K::Execute});
  assert(calcPromptCaretInField() && s.history.size() == 1);  // repeat awaits final
  keys({K::Execute});
  assert(s.memory.ans.re.number == 10 && lastRender().presented == "10");
  assert(s.history.size() == 2);
  // CALC Return/AC never open Home (P14 owns BACK->Home, frozen here).
  keys({K::Execute});  // reopen
  keys({K::Return});
  assert(!CalculatorApp::isHome());
  keys({K::Tools, K::Okay, K::Clear});
  assert(!CalculatorApp::isHome() && calcPromptCaretInField());
  keys({K::Return});
  assert(!CalculatorApp::isHome());
  std::cout << "P13 CALC: APP-CALC-01, AwaitFinalExecute gating, repeat and no-Home exits passed.\n";
}

// Base-N, Matrix, Vector, Equation and Inequality through physical keys.
void numericApps() {
  CalculatorSession& s = calculatorSession();
  // Base-N (cell 8, right of Complex): 15x37 in Decimal, FORMAT shows it in Hex,
  // and in Hex SHIFT 3 is the digit F.
  keys({K::Home, K::Right, K::Execute});
  assert(s.app == AppId::BaseN && s.base == 10);
  keys({K::Clear, K::Digit1, K::Digit5, K::Multiply, K::Digit3, K::Digit7, K::Execute});
  assert(lastRender().presented == "555" && s.memory.ans.re.number == 555);
  const PanelIcons::State baseIcons = CalculatorApp::desiredIcons();
  assert(!baseIcons.mathio && !baseIcons.deg);
  keys({K::Format});
  assert(s.base == 16 && lastRender().presented == "0000022B");
  keys({K::Clear, K::Digit1, K::Shift, K::Digit3, K::Add, K::Digit1, K::Execute});
  assert(lastRender().presented == "00000020");

  // Matrix (cell 9): TOOLS > MatA > Confirm 2x2, cells 2 1 1 1, then MatA^-1.
  keys({K::Home, K::Right, K::Execute});
  assert(s.app == AppId::Matrix && s.base == 10);
  keys({K::Tools, K::Execute});
  {const auto picker=frame;keys({K::Clear});assert(frame==picker && s.app==AppId::Matrix);}
  keys({K::Down, K::Down, K::Execute});
  keys({K::Digit2, K::Execute, K::Digit1, K::Execute, K::Digit1, K::Execute, K::Digit1, K::Execute});
  const Aggregate& a = s.aggregates.matrices[0];
  assert(a.rows == 2 && a.cols == 2 && a.at(0, 0) == Decimal(2) && a.at(0, 1) == Decimal(1) && a.at(1, 1) == Decimal(1));
  const int matrixGroup = 6;  // Func Analysis .. Engineer Symbol, then Matrix (Other is last)
  keys({K::Return, K::Clear, K::Catalog});
  repeat(K::Down, matrixGroup);
  keys({K::Execute, K::Execute, K::Shift, K::Power, K::Execute});
  const Aggregate& answer = s.aggregates.matrices[kAnswerSlot];
  assert(answer.rows == 2 && answer.at(0, 0) == Decimal(1) && answer.at(0, 1) == Decimal(-1) && answer.at(1, 1) == Decimal(2));
  // + on the answer continues with MatAns+; add MatA.
  keys({K::Add, K::Catalog});
  repeat(K::Down, matrixGroup);
  keys({K::Execute, K::Execute, K::Execute});
  assert(answer.at(0, 0) == Decimal(3) && answer.at(0, 1) == Decimal(0) && answer.at(1, 1) == Decimal(3));
  // Named slots and the answer persist.
  CP::fakeMillis += 5000;
  pump();
  {
    std::vector<uint8_t> record;
    Settings settings;
    Memory memory;
    Aggregates stored;
    assert(CP::loadBlob("calc_v2", "state", record) && decodeV2(record, settings, memory, &stored));
    assert(stored.matrices[0].rows == 2 && stored.matrices[kAnswerSlot].at(1, 1) == Decimal(3));
  }
  keys({K::Clear});

  // Vector (cell 10): launching another app clears MatAns but keeps MatA.
  keys({K::Home, K::Right, K::Execute});
  assert(s.app == AppId::Vector && !s.aggregates.matrices[kAnswerSlot].defined() && s.aggregates.matrices[0].defined());
  keys({K::Tools, K::Execute});
  {const auto picker=frame;keys({K::Clear});assert(frame==picker && s.app==AppId::Vector);}
  keys({K::Down, K::Execute, K::Digit1, K::Execute, K::Digit2, K::Execute, K::Return});
  keys({K::Tools, K::Down, K::Execute, K::Down, K::Execute, K::Digit3, K::Execute, K::Digit4, K::Execute, K::Return});
  keys({K::Clear, K::Catalog});
  repeat(K::Down, matrixGroup);  // Vector sits where Matrix did
  keys({K::Execute, K::Execute, K::Multiply, K::Catalog});
  repeat(K::Down, matrixGroup);
  keys({K::Execute, K::Down, K::Execute, K::Execute});
  const Aggregate& cross = s.aggregates.vectors[kAnswerSlot];
  assert(cross.rows == 3 && cross.cols == 1 && cross.at(0, 0).zero() && cross.at(2, 0) == Decimal(-2));
  keys({K::Clear});

  // Equation (cell 5): Polynomial degree 2, 1 2 -2. Output is Line (set above).
  keys({K::Home, K::Up, K::Left, K::Left, K::Execute});
  assert(s.app == AppId::Equation);
  {const auto picker=frame;keys({K::Clear});assert(frame==picker && s.app==AppId::Equation);}
  keys({K::Down, K::Execute, K::Execute});
  keys({K::Digit1, K::Execute, K::Digit2, K::Execute, K::Subtract, K::Digit2, K::Execute, K::Execute});
  assert(lastRender().presented.find("0.7320508076") != std::string::npos);
  keys({K::Down});
  assert(lastRender().presented.find("-2.732050808") != std::string::npos && CalculatorApp::desiredIcons().arrUp);
  keys({K::Down, K::Down, K::Down});  // x-Min, y-Min, then back to the coefficients
  keys({K::Execute});
  assert(lastRender().presented.find("0.7320508076") != std::string::npos);

  // Inequality (cell 6): x^2+2x-3 >= 0.
  keys({K::Home, K::Right, K::Execute});
  assert(s.app == AppId::Inequality);
  keys({K::Execute, K::Down, K::Down, K::Execute});
  keys({K::Digit1, K::Execute, K::Digit2, K::Execute, K::Subtract, K::Digit3, K::Execute, K::Execute});
  {
    std::string shown;
    for (char c : lastRender().presented) if (c != '\x01' && c != '\x02') shown += c;
    assert(shown == "x<=-3, 1<=x");
  }

  // Solver back in Equation (a relaunch: it starts at the mode menu): x^2-2=0 from 1.
  keys({K::Home, K::Left, K::Execute, K::Down, K::Down, K::Execute});
  keys({K::X, K::Square, K::Subtract, K::Digit2, K::Shift, K::Open, K::Digit0, K::Execute});
  // The initial-value form opens with Execute selected; Up reaches the field.
  assert(!lastRender().caret);
  keys({K::Up, K::Clear, K::Digit1, K::Execute});
  // Committing the initial value selects Execute; solving is a separate action.
  assert(lastRender().presented.empty() && !lastRender().caret);
  keys({K::Execute});
  assert((s.memory.variables[6].re.number - Decimal(2).sqrt()).abs() < Decimal("1E-20"));
  assert(s.memory.variables[6].re.isExact());
  // x1, x2: an exclusive answer screen, one page at a time.
  assert(!lastRender().presented.empty() && CalculatorApp::desiredIcons().arrDown);
  // SHIFT 6 stores the shown root, x1 = -sqrt(2), into C without leaving the page.
  keys({K::Shift, K::Digit6});
  assert(s.memory.variables[2].re.number == -s.memory.variables[6].re.number && CalculatorApp::desiredIcons().arrDown);
  keys({K::Right});  // the answer fits: Right pages like Down
  assert(!lastRender().presented.empty());
  keys({K::Okay});
  assert(lastRender().presented.empty());
  keys({K::Okay, K::Up, K::Clear, K::Digit6});
  assert(lastRender().caret && lastRender().caretRect.y == 32);
  keys({K::Return, K::Return});
  for(K dismiss:{K::Okay,K::Clear,K::Return}){
    keys({K::Clear,K::X,K::Multiply,K::Multiply,K::Digit2,K::Execute});
    assert(frame==renderErrorDialog(Error::Syntax));
    assert(!lastRender().caret && CalculatorApp::desiredIcons()==PanelIcons::State{});
    const auto dialog=frame;
    keys({K::Digit9,K::Delete,K::Left,K::Right,K::Execute,K::Catalog});
    assert(frame==dialog);
    keys({dismiss,K::Digit3,K::Execute}); // x*3*2 after restoring parser offset 2
    assert(frame!=dialog); // valid equation reaches initial value form
    keys({K::Return,K::Clear,K::Digit2,K::Add,K::Digit2,K::Execute});
    assert(frame==renderErrorDialog(Error::Variable));
    keys({dismiss,K::Add,K::X,K::Execute}); // constant expression retained, append unknown
    assert(frame!=renderErrorDialog(Error::Variable));
    keys({K::Up,K::Clear,K::Digit1,K::Multiply,K::Multiply,K::Digit2,K::Execute,K::Execute});
    assert(frame==renderErrorDialog(Error::Syntax)); // seed evaluation owns this error
    keys({dismiss});
    assert(lastRender().caret && lastRender().caretRect.y==32);
    keys({K::Digit3,K::Execute,K::Execute}); // corrected seed: 1*3*2
    assert(lastRender().presented=="-4"); // x+4: the one root
    keys({K::Okay});
  }
  // CATALOG opens over the equation and inserts into it (integral, derivative...).
  const auto equation=frame;
  keys({K::Catalog});
  assert(frame!=equation);
  const auto catalog=frame;
  keys({K::Execute});
  assert(frame!=catalog); // the category's entries
  keys({K::Execute});
  assert(frame!=catalog && frame!=equation); // inserted: back at the edited equation
  const auto inserted=frame;
  keys({K::Digit1});  // typing lands in the equation editor again, not in a menu
  assert(frame!=inserted);
}

// Storing solver answers: SHIFT then a letter key (SHIFT 4 = A, as the editor
// types it) and VARIABLE > Store, on every Equation kind and on Calculate's
// solve(). Restores memory and settings so later flows see what they expect.
void openApp(unsigned index) {
  CalculatorApp::openHome();
  pump();
  repeat(K::Left, 14);
  repeat(K::Right, static_cast<int>(index));
  keys({K::Okay});
}
// CALCULATOR_STORE_REVIEW=<dir>: the current frame as <dir>/store-<name>.pgm
// for tools/render/render_sheet.py (picker, confirmations).
void reviewFrame(const char* name) {
  const char* output = std::getenv("CALCULATOR_STORE_REVIEW");
  if (!output) return;
  std::vector<uint8_t> raw;
  for (size_t i = 0; i < frame.size();) {
    const uint8_t n = frame[i++];
    if (n < 128) { raw.insert(raw.end(), frame.begin() + i, frame.begin() + i + n + 1); i += n + 1; }
    else raw.insert(raw.end(), n - 126, frame[i++]);
  }
  assert(raw.size() == 3024);
  std::ofstream image(std::string(output) + "/store-" + name + ".pgm", std::ios::binary);
  image << "P5\n192 63\n255\n";
  for (size_t i = 0; i < 192 * 63; ++i) image.put(static_cast<char>(255 - 85 * ((raw[i / 4] >> (6 - 2 * (i % 4))) & 3)));
}
// CATALOG > Symbolic > `entry` in Calculate, the call left open for its argument.
void symbolicCommand(int entry) {
  int symbolic = -1;
  const auto groups = catalogRootRows(false, AppId::Calculate);
  for (size_t i = 0; i < groups.size(); ++i) if (groups[i].label == "Symbolic") symbolic = static_cast<int>(i);
  assert(symbolic >= 0);
  keys({K::Clear, K::Catalog});
  repeat(K::Down, symbolic);
  keys({K::Execute});
  repeat(K::Down, entry);
  keys({K::Execute});
}
void storeAnswers() {
  CalculatorSession& s = calculatorSession();
  const Memory savedMemory = s.memory;
  const Settings savedSettings = s.settings;
  s.settings = Settings{};
  for (auto& v : s.memory.variables) v = ScalarValue(Value(0));
  auto var = [&](int i) { return s.memory.variables[static_cast<size_t>(i)]; };

  // A CAS formula's value is a placeholder: storable only through solve() roots.
  {
    Result shown;
    shown.formula = std::make_shared<const std::string>("2x");
    assert(!canStoreResult(&shown, true));
    shown.solutions = std::make_shared<const std::vector<Solution>>(1);
    assert(canStoreResult(&shown, true));
  }

  // Calculate, solve(x^2=4): two roots, so SHIFT 4 asks which (x1=-2, x2=2).
  openApp(0);
  assert(s.app == AppId::Calculate);
  constexpr int kSolve = 6, kDerivative = 3;  // rows of the Symbolic group
  symbolicCommand(kSolve);
  keys({K::X, K::Square, K::Shift, K::Open, K::Digit4, K::Close, K::Execute});
  const auto result = frame;
  reviewFrame("solve-result");
  keys({K::Shift, K::Digit4});
  assert(frame != result && var(0).re.number == 0);  // the picker, nothing stored yet
  reviewFrame("solve-picker");
  keys({K::Down, K::Execute});
  assert(var(0).re.number == 2);
  reviewFrame("solve-stored");
  // Back on the result, with the confirmation in the notice band.
  assert(frame != result);
  // VARIABLE > A > Store result asks the same question; x1 = -2 goes to A.
  keys({K::Variable});
  repeat(K::Up, 5);
  keys({K::Left, K::Okay, K::Down, K::Okay, K::Okay});
  assert(var(0).re.number == -2);
  keys({K::Return});  // the grid closes back to the result
  assert(var(0).re.number == -2);

  // One root stores at once: solve(2x=6), SHIFT 5 -> B = 3, persisted.
  symbolicCommand(kSolve);
  keys({K::Digit2, K::X, K::Shift, K::Open, K::Digit6, K::Close, K::Execute});
  uint32_t revision = s.revision;
  keys({K::Shift, K::Digit5});
  assert(var(1).re.number == 3 && s.revision != revision);
  // SHIFT . and SHIFT x10 are y and z, as in the editor.
  keys({K::Shift, K::Dot});
  assert(var(7).re.number == 3);
  keys({K::Shift, K::Scientific});
  assert(var(8).re.number == 3);

  // A formula without roots keeps SHIFT+letter's old meaning: a fresh line.
  symbolicCommand(kDerivative);
  keys({K::X, K::Square, K::Close, K::Execute, K::Shift, K::Digit4});
  assert(var(0).re.number == -2);

  // Equation (cell 5, a relaunch: the mode menu). Polynomial x^2-5x+6=0.
  openApp(5);
  assert(s.app == AppId::Equation);
  keys({K::Down, K::Execute, K::Execute});
  keys({K::Digit1, K::Execute, K::Subtract, K::Digit5, K::Execute, K::Digit6, K::Execute, K::Execute});
  {
    const auto page = frame;
    revision = s.revision;
    reviewFrame("poly-page");
    keys({K::Shift, K::Digit4});
    assert(frame != page && s.revision != revision);  // "Stored in A" replaces the header
    reviewFrame("poly-stored");
  }
  keys({K::Down, K::Shift, K::Digit5});
  const Decimal a = var(0).re.number, b = var(1).re.number;
  assert(((a == 2 && b == 3) || (a == 3 && b == 2)));
  // VARIABLE > Store now works on a Polynomial answer too: x2 into A.
  keys({K::Variable});
  repeat(K::Up, 5);
  keys({K::Left, K::Okay, K::Down, K::Okay, K::Return});
  assert(var(0).re.number == b);
  // A vertex page is a value as well: x-Min into D (SHIFT 1).
  keys({K::Down, K::Shift, K::Digit1});
  assert(var(3).re.number == Decimal("2.5"));

  // x^2+1=0: complex roots keep their imaginary part (SHIFT 6 -> C). AC
  // leaves the answers (Return would step back a page); the cursor is on c.
  keys({K::Clear, K::Left, K::Left, K::Digit1, K::Execute, K::Digit0, K::Execute, K::Digit1, K::Execute, K::Execute});
  keys({K::Shift, K::Digit6});
  assert(var(2).complex && var(2).re.number == 0 && var(2).im.number.abs() == 1);
  // ...and through VARIABLE > Store as well (into A).
  keys({K::Variable});
  repeat(K::Up, 5);
  keys({K::Left, K::Okay, K::Down, K::Okay, K::Return});
  assert(var(0).complex && var(0).im.number.abs() == 1);

  // Simul Equation x+y=3, x-y=1: x into A, y into B.
  keys({K::Return, K::Return, K::Return, K::Up, K::Execute, K::Execute});
  keys({K::Digit1, K::Execute, K::Digit1, K::Execute, K::Digit3, K::Execute,
        K::Digit1, K::Execute, K::Subtract, K::Digit1, K::Execute, K::Digit1, K::Execute, K::Execute});
  keys({K::Shift, K::Digit4, K::Down, K::Shift, K::Digit5});
  reviewFrame("simul-stored");
  assert(!var(0).complex && var(0).re.number == 2 && var(1).re.number == 1);

  // Inequality answers are intervals: SHIFT 4 refuses and stores nothing.
  openApp(6);
  assert(s.app == AppId::Inequality);
  keys({K::Execute, K::Execute});
  keys({K::Digit1, K::Execute, K::Digit0, K::Execute, K::Subtract, K::Digit1, K::Execute, K::Execute});
  {
    const auto page = frame;
    revision = s.revision;
    keys({K::Shift, K::Digit4});
    assert(var(0).re.number == 2 && s.revision == revision && frame != page);  // "Cannot store"
    reviewFrame("inequality-refused");
    keys({K::Variable});
    assert(s.app == AppId::Inequality && var(0).re.number == 2);
  }

  keys({K::Home});
  s.memory = savedMemory;
  s.settings = savedSettings;
  std::cout << "Store answers: SHIFT+letter and VARIABLE > Store on solve(), Simul, Polynomial, complex roots; Inequality refuses.\n";
}

// Home toggles between the last app and its menu: App -> Home -> the same
// app, with drafts retained. A menu shown with no app visited yet stays up.
void homeToggle() {
  CalculatorSession& s = calculatorSession();
  CP::fakeNvs().clear();
  s.settings = Settings{};  // Math IO: results report through presented
  CalculatorApp::openHome();
  pump();
  assert(CalculatorApp::isHome());
  CalculatorApp::press(K::Home);
  assert(CalculatorApp::isHome() && CalculatorApp::active());
  // Enter Calculate, leave a draft, and toggle out and back.
  CalculatorApp::openHome();
  pump();
  assert(CalculatorApp::isHome());
  repeat(K::Left, 14);
  keys({K::Okay});
  assert(!CalculatorApp::isHome() && s.app == AppId::Calculate);
  keys({K::Clear, K::Digit1, K::Digit2});
  keys({K::Home});
  assert(CalculatorApp::isHome());
  keys({K::Home});
  assert(!CalculatorApp::isHome() && s.app == AppId::Calculate);
  // The draft survived the round trip: evaluating it gives 12.
  keys({K::Execute});
  assert(s.memory.ans.re.number == 12 && lastRender().presented == "12");
  // The same round trip for Statistics, the reported case.
  keys({K::Home});
  assert(CalculatorApp::isHome());
  repeat(K::Left, 14);
  keys({K::Right, K::Okay});
  assert(s.app == AppId::Statistics);
  keys({K::Home, K::Home});
  assert(!CalculatorApp::isHome() && s.app == AppId::Statistics);
  std::cout << "Home toggle: fresh menu stays, Calculate/Statistics resume passed.\n";
}

// Quick calculate (boot, power-on): straight into Calculate with the last
// work showing, Calculate's own Settings, and Home turning it into an
// ordinary session. Nothing in it leaves the calculator.
void quickCalculate() {
  CalculatorSession& s = calculatorSession();
  CP::fakeNvs().clear();
  s.settings = Settings{};
  CalculatorApp::openHome();
  pump();
  repeat(K::Left, 14);
  keys({K::Okay, K::Clear, K::Digit2, K::Add, K::Digit3, K::Execute});
  assert(s.app == AppId::Calculate && lastRender().presented == "5");
  // Quick calculate opens Calculate from another app too.
  keys({K::Home});
  repeat(K::Left, 14);
  keys({K::Right, K::Okay});
  assert(s.app == AppId::Statistics);
  CalculatorApp::openQuick();
  pump();
  assert(CalculatorApp::active() && !CalculatorApp::isHome() && s.app == AppId::Calculate);
  keys({K::Clear, K::Digit1, K::Add, K::Digit1, K::Execute});
  assert(s.memory.ans.re.number == 2 && s.history.size() == 1);
  // Settings is Calculate's own page, as under Home; Return closes it.
  keys({K::Settings});
  assert(Calculator::CalculateApp::sharedActive());
  keys({K::Return});
  assert(!Calculator::CalculateApp::sharedActive());
  keys({K::Catalog});
  assert(Calculator::CalculateApp::sharedActive());
  keys({K::Return});
  assert(!Calculator::CalculateApp::sharedActive() && CalculatorApp::active());
  // Power-off and power-on: the next quick calculate finds the same result
  // and history.
  CalculatorApp::suspend();
  assert(!CalculatorApp::active());
  CalculatorApp::openQuick();
  pump();
  assert(lastRender().presented == "2" && s.history.size() == 1);
  // Home and Home again resume quick calculate; Return and SHIFT+Return are
  // Calculate's own and never leave it.
  keys({K::Home});
  assert(CalculatorApp::isHome());
  keys({K::Home});
  assert(!CalculatorApp::isHome() && s.app == AppId::Calculate);
  keys({K::Return});
  assert(CalculatorApp::active() && !CalculatorApp::isHome());
  keys({K::Shift, K::Return});
  assert(CalculatorApp::active() && !CalculatorApp::isHome() && s.app == AppId::Calculate);
  // Home > Calculate picked on the menu ends quick mode as an ordinary launch.
  keys({K::Home, K::Okay});
  assert(CalculatorApp::active() && !CalculatorApp::isHome() && s.app == AppId::Calculate);
  std::cout << "Quick calculate: last work kept across power-off, Calculate's own Settings, "
               "Home-Home resumes it, Calculate ends it passed.\n";
}

void shortcutFlows(){
  CP::fakeDeferWorker=false;CalculatorApp::openHome();keys({K::Digit1});
  auto& s=calculatorSession();s.settings=Settings{};
  keys({K::Clear,K::Catalog,K::Digit1,K::Digit2,K::Sin,K::X,K::Close});
  // The catalog template has an empty pair of limits and a nested sine.
  keys({K::Home,K::Settings,K::Digit4});
  std::vector<uint8_t> frame;CalculatorApp::press(K::Right);assert(CalculatorApp::service(frame));
  assert(frame==CanvasText::packBitsRle(kGetStarted,sizeof(kGetStarted)));
  keys({K::Return,K::Return,K::Digit1,K::Clear,K::Shift,K::Catalog,K::Digit2,K::Digit1});
  keys({K::Clear,K::Shift,K::Multiply,K::Subtract,K::Digit3,K::Right,K::Execute});
  assert(s.memory.ans.re.number==3);
  keys({K::Shift,K::X,K::Return});assert(s.memory.ans.re.number==3);  // SHIFT+x is inert
  keys({K::Clear,K::Digit5,K::Digit0,K::Shift,K::Divide,K::Execute});assert(s.memory.ans.re.number==Decimal("0.5"));
  // Shared field overlay must insert into its owner and keep cancellation inert.
  MathEditor e;e.begin("7");e.press(K::Shift);assert(QuickOverlay::open(e,K::Catalog,AppId::Calculate));
  QuickOverlay::press(K::Digit2);assert(e.text()=="7integral(,,)"&&!QuickOverlay::active());
  e.press(K::Shift);QuickOverlay::open(e,K::Catalog,AppId::Calculate);QuickOverlay::press(K::Return);assert(e.text()=="7integral(,,)");
  // SHIFT+x has no function: it only clears SHIFT, in fields and in Calculate.
  e.press(K::Shift);assert(QuickOverlay::open(e,K::X,AppId::Calculate)&&!QuickOverlay::active()&&!e.shifted&&e.text()=="7integral(,,)");
  std::cout<<"Shortcut flows, nested templates, shared picker and inert SHIFT+x passed.\n";
}

bool preparationReady = false;
bool prepareTestJob() { return preparationReady; }
void deferredPreparedJob() {
  auto& service = calculatorJobs();
  service.setPreparationHook(prepareTestJob);
  Job request, completed;
  Result result;
  request.request.expression = "2+3";
  assert(service.start(request) == JobService::Start::Started);
  assert(service.running() && !service.take(completed, result));
  service.cancel();
  service.discardCancelled(); // Home can leave the app before take() runs.
  assert(!service.running());
  preparationReady = false;
  assert(service.start(request) == JobService::Start::Started);
  assert(!service.take(completed, result));
  preparationReady = true;
  assert(service.take(completed, result));
  assert(result.error == Error::None && result.value.number == Decimal(5));
  service.setPreparationHook(nullptr);
}

// A history entry answers FORMAT and SHIFT+EXE directly, and any other key
// starts a fresh line (+ - x / continue from Ans), as a live result does.
void historyKeys() {
  CalculatorSession& s = calculatorSession();
  CP::fakeDeferWorker = false;
  s.settings = Settings{};
  CalculatorApp::openHome();
  pump();
  repeat(K::Left, 14);
  keys({K::Okay, K::Clear});
  s.history.clear();
  keys({K::Digit1, K::Divide, K::Digit3, K::Execute, K::Digit5, K::Execute});
  assert(s.history.size() == 2 && lastRender().presented == "5");
  keys({K::Up});  // the older entry, 1/3
  assert(lastRender().presented == "frac(1,3)");
  keys({K::Format});  // toggle in place: no job, no new entry
  assert(lastRender().presented == "0.3333333333" && s.history.size() == 2 &&
         s.history[0].answer.displayedFormat == Format::Decimal);
  keys({K::Format});
  assert(lastRender().presented == "frac(1,3)" && s.history[0].answer.displayedFormat == Format::Standard);
  // SHIFT+FORMAT opens the menu for the entry; Return goes back to it.
  const std::vector<uint8_t> entryFrame = frame;
  keys({K::Shift, K::Format});
  assert(frame != entryFrame);  // the FORMAT menu
  keys({K::Return});
  assert(frame == entryFrame && lastRender().presented == "frac(1,3)" && s.history.size() == 2);
  // The menu's choice becomes the entry's view.
  keys({K::Shift, K::Format, K::Down, K::Okay});
  assert(lastRender().presented == "0.3333333333" && s.history[0].answer.displayedFormat == Format::Decimal);
  keys({K::Format});
  assert(lastRender().presented == "frac(1,3)");
  // SHIFT+EXE re-executes the entry as a Decimal result.
  keys({K::Shift, K::Execute});
  assert(lastRender().presented == "0.3333333333" && s.history.size() == 3 &&
         s.history.back().answer.displayedFormat == Format::Decimal);
  // A digit on a history entry types into a fresh line.
  keys({K::Up, K::Digit7, K::Execute});
  assert(lastRender().presented == "7" && s.history.back().source == "7");
  // + on a history entry continues from Ans (7): Ans+1.
  keys({K::Up, K::Add, K::Digit1, K::Execute});
  assert(s.history.back().source == "Ans+1" && lastRender().presented == "8");
  std::cout << "History entry: FORMAT toggle/menu in place, SHIFT+EXE Decimal, fresh line and Ans operators passed.\n";
}

int main(int argc,char** argv) {
  // Arduino adds PSRAM after global constructors. Every controller must defer
  // its session/job/result allocation until runtime, including when linked but
  // never opened. A global reference to calculatorSession() fails this check.
  assert(CalculatorStorage::usage().psram == 0 && CalculatorStorage::usage().internal == 0);
  deferredPreparedJob();
  if(argc==2&&std::string(argv[1])=="--shortcuts"){shortcutFlows();return 0;}
  retention();
  grid();
  dataGrid();
  storeRecords();
  p11HistoryUnitChecks();
  appFlow();
  numericApps();
  storeAnswers();
  homeToggle();
  quickCalculate();
  historyKeys();  // before calcFlow: its known failure aborts the run
  calcFlow();
  shortcutFlows();
  std::cout << "Calculator apps: retention, grid, cell drafts, data grid render, v2 record + v1 migration, "
               "stale/cancelled jobs, field menu return, persistence, the Complex flow and Base-N/Matrix/Vector/Equation/Inequality keys passed.\n";
}
