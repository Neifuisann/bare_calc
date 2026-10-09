// Calculator render/state harness: the production MathEditor, EditDocument,
// MathLayout, renderer, UiKit, CanvasText and Adafruit GFX compiled on the
// host (see test/render_fakes).
//
//   render.exe capture <dir>   write the named fixtures as P1 PBMs, refusing
//                              to overwrite any file that already exists
//   render.exe check <dir>     run every expectation, write current renders
//                              into <dir> for review
//
// check() takes an optional known-failure flag for defects a later phase of
// documented calculator rendering fixes; a known failure that starts passing
// fails the run so its marker goes in the same change as the fix. Phase 2
// cleared every Phase 0 marker.
#include "calculator/ui/calculator_document.h"
#include "calculator/ui/calculator_editor.h"
#include "calculator/math/calculator_engine.h"
#include "calculator/math/calculator_eng_symbols.h"
#include "calculator/ui/calculator_icons.h"
#include "calculator/ui/calculator_layout.h"
#include "calculator/math/calculator_linear.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/ui/calculator_metrics.h"
#include "calculator/ui/calculator_presentation.h"
#include "calculator/ui/calculator_render.h"
#include "ui/canvas_text.h"
#include "ui/canvas_gray.h"
#include "display/panel_icons.h"
#include "ui/ui_kit.h"

#include <Adafruit_GFX.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace Calculator;
using K = ExpressionInput::Key;
using Role = EditDocument::Role;
using Style = MathFont::Style;

namespace {
int failures = 0, knownFailures = 0, checks = 0;
void check(const std::string& name, bool ok, bool knownFailure = false) {
  ++checks;
  if (ok && knownFailure) {std::cout << "XPASS " << name << " (remove the known-failure marker)\n"; ++failures;}
  else if (!ok && knownFailure) {std::cout << "XFAIL " << name << "\n"; ++knownFailures;}
  else if (!ok) {std::cout << "FAIL  " << name << "\n"; ++failures;}
}

struct Frame {
  std::vector<uint8_t> bits;
  std::vector<uint8_t> levels;
  uint8_t level(int x,int y) const {return levels.empty() ? (at(x,y)?3:0) : levels[size_t(y)*192+x];}
  bool at(int x, int y) const {
    if (x < 0 || y < 0 || x >= 192 || y >= 63) return false;
    const size_t i = static_cast<size_t>(y) * 192 + x;
    return bits[i >> 3] & (0x80 >> (i & 7));
  }
  bool operator==(const Frame& o) const {return bits == o.bits;}
};
// Inverse of CanvasText::packBitsRle; goldens compare decoded pixels.
Frame decode(const std::vector<uint8_t>& rle) {
  Frame f;
  for (size_t i = 0; i < rle.size();) {
    const uint8_t control = rle[i++];
    if (control < 128) {f.bits.insert(f.bits.end(), rle.begin() + i, rle.begin() + i + control + 1); i += control + 1;}
    else {f.bits.insert(f.bits.end(), control - 126, rle[i]); ++i;}
  }
  if (f.bits.size() == 3024) {
    const auto raw=f.bits;f.bits.assign(1512,0);f.levels.resize(192*63);
    for(size_t p=0;p<f.levels.size();++p){
      f.levels[p]=(raw[p/4]>>(6-2*(p%4)))&3;
      if(f.levels[p])f.bits[p/8]|=0x80>>(p%8);
    }
  } else if (f.bits.size() != 1512) {std::cerr << "invalid decoded frame size\n"; std::exit(2);}
  return f;
}
bool exists(const std::string& path) {return std::ifstream(path).good();}
void writePbm(const std::string& path, const Frame& f) {
  std::ofstream out(path, std::ios::binary);
  out << "P1\n192 63\n";
  for (int y = 0; y < 63; ++y) {for (int x = 0; x < 192; ++x) out << (f.at(x, y) ? '1' : '0'); out << '\n';}
  std::ofstream pgm(path.substr(0,path.size()-4)+".pgm",std::ios::binary);
  pgm << "P5\n192 63\n255\n";
  for(int y=0;y<63;++y)for(int x=0;x<192;++x)pgm.put(static_cast<char>(255-85*f.level(x,y)));
}

struct Rect {int left = 0, top = 0, right = -1, bottom = -1; bool empty() const {return right < left;}};
Rect inkBounds(const Frame& f, int top, int bottom, int left = 0, int right = 191) {
  Rect r{192, 63, -1, -1};
  for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) if (f.at(x, y)) {
    r.left = std::min(r.left, x); r.right = std::max(r.right, x); r.top = std::min(r.top, y); r.bottom = std::max(r.bottom, y);
  }
  return r;
}

MathEditor typed(std::initializer_list<K> keys, const std::string& start = "") {
  MathEditor e; e.begin(start);
  for (K k : keys) e.press(k);
  return e;
}
MathEditor at(const std::string& source, size_t cursor) {MathEditor e; e.begin(source); e.setCursor(cursor); return e;}
// What CalculatorApp::insert() does for a CATALOG template.
void catalogInsert(MathEditor& e, const std::string& text) {e.insertTemplate(text);}

Settings math;
Frame render(const MathEditor& e, const Result* result = nullptr, bool busy = false, const Settings& s = math) {
  Viewport view;
  return decode(renderCalculation(e, s, result, Format::Standard, busy, false, view));
}
Result evaluated(const std::string& expression) {
  return evaluate(Request{expression, math, Memory{}, false, 1});
}
std::array<std::string, 9> zeroGridLabels() {
  std::array<std::string, 9> labels;
  for (int i = 0; i < 9; ++i) labels[static_cast<size_t>(i)] = std::string(1, variableName(i)) + "=0";
  return labels;
}

// The painted caret, in screen pixels. The renderer reports its rectangle;
// the frame must actually carry ink over it.
Rect caret(const MathEditor& e) {
  const Frame f = render(e);
  const auto& info = lastRender();
  if (!info.caret) return {};
  const auto k = info.caretRect;
  Rect r{k.x, k.y, k.x + k.width - 1, k.y + k.height - 1};
  for (int y = r.top; y <= r.bottom; ++y) for (int x = std::max(0, r.left); x <= r.right; ++x)
    if (!f.at(x, y)) return {};
  return r;
}
// Expression ink without the caret: render with the caret, then exclude the
// caret rectangle the renderer reports (a busy render now shows the modal
// spinner, so it can no longer stand in as a caret-less expression view).
Rect inkBoundsExcept(const Frame& f, int top, int bottom, int left, int right,
                     int skipLeft, int skipTop, int skipRight, int skipBottom) {
  Rect r{192, 63, -1, -1};
  for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
    if (!f.at(x, y)) continue;
    if (x >= skipLeft && x <= skipRight && y >= skipTop && y <= skipBottom) continue;
    r.left = std::min(r.left, x); r.right = std::max(r.right, x); r.top = std::min(r.top, y); r.bottom = std::max(r.bottom, y);
  }
  return r;
}
Rect expressionInk(const MathEditor& e) {
  const Frame f = render(e);
  const auto k = lastRender().caretRect;
  return inkBoundsExcept(f, 0, 53, 0, 191, k.x, k.y, k.x + k.width - 1, k.y + k.height - 1);
}
Rect firstGlyphInk(const MathEditor& e) {
  const Rect all = expressionInk(e);
  const Frame f = render(e);
  const auto k = lastRender().caretRect;
  return inkBoundsExcept(f, 0, 53, all.left, all.left + 3, k.x, k.y, k.x + k.width - 1, k.y + k.height - 1);
}

struct Fixture {std::string name; std::function<Frame()> frame;};
std::vector<Fixture> fixtures() {
  auto entry = [](std::initializer_list<K> keys) {return [keys] {return render(typed(keys));};};
  auto catalog = [](std::string text) {return [text] {MathEditor e; e.begin(); catalogInsert(e, text); return render(e);};};
  auto result = [](std::string expression) {return [expression] {MathEditor e; e.begin(expression); auto r = evaluated(expression); return render(e, &r);};};
  auto menu = [](std::vector<MenuItem> rows, int selected, int first = 0) {
    return [=] { return decode(renderMenuList(rows, selected, first)); };
  };
  return {
    {"calc-selected-variable", [] {MathEditor e;e.begin();Viewport v;return decode(renderCalcPrompt("9x",math,"x=",e,math,false,v,false));}},
    {"calc-selected-execute", [] {MathEditor e;e.begin();Viewport v;return decode(renderCalcPrompt("9x",math,"x=",e,math,false,v,false,true));}},
    {"exact-radian-sqrt", [] {MathEditor e;e.begin("2sin(frac(pi,4))");Request q;q.expression=e.text();q.settings.angle=Angle::Radian;auto r=evaluate(q);Viewport v;return decode(renderCalculation(e,q.settings,&r,Format::Standard,false,false,v));}},
    // P15 review artifacts, not target raster goldens. Legacy solution paths
    // remain intact; the ledgers separately track missing physical evidence.
    {"p15-complex", [] {MathEditor e;e.begin("2+3i");Result r;r.complex=true;r.value=Value(2);r.imag=Value(3);return render(e,&r);}},
    {"p15-basen", [] {MathEditor e;e.begin("15*37");ResultSummary r;r.value=Value(555);Viewport v;return decode(renderBaseN(e,16,&r,false,v,false));}},
    {"p15-matrix", [] {AggregateView v;v.name="MatAns";v.rows=v.cols=2;v.cells={"2","1","1","1"};return decode(renderAggregate(v));}},
    {"p15-vector", [] {AggregateView v;v.name="VctAns";v.rows=2;v.cols=1;v.cells={"4","6"};return decode(renderAggregate(v));}},
    {"p15-equation", [] {return decode(renderSolution("x1",std::string("\x01")+"1"+"\x02",math));}},
    {"p15-inequality", [] {return decode(renderSolution("",std::string("x<=")+"\x01"+"-3"+"\x02"+", "+"\x01"+"1"+"\x02"+"<=x",math));}},
    {"entry-empty", entry({})},
    {"entry-123", entry({K::Digit1, K::Digit2, K::Digit3})},
    {"entry-power-exponent-3", entry({K::Digit9, K::Power, K::Digit3})},
    {"entry-power-exponent-32", entry({K::Digit9, K::Power, K::Digit3, K::Digit2})},
    {"entry-power-exited", entry({K::Digit9, K::Power, K::Digit3, K::Digit2, K::Right})},
    {"entry-power-nested", entry({K::Digit2, K::Power, K::Digit3, K::Power, K::Digit4})},
    {"entry-power-after-2x", entry({K::Digit2, K::X, K::Power, K::Digit3})},
    {"entry-fraction-numerator", entry({K::Fraction, K::Digit1})},
    {"entry-fraction-denominator", entry({K::Fraction, K::Digit1, K::Down, K::Digit2})},
    {"entry-sqrt", entry({K::Root, K::Digit2})},
    {"entry-root-index", entry({K::Shift, K::Root, K::Digit3, K::Down, K::Digit8})},
    {"entry-integral-empty", catalog("integral(,,)")},
    {"entry-derivative-empty", catalog("diff(,)")},
    {"entry-sum-empty", catalog("sum(,,)")},
    {"entry-long", [] {MathEditor e; e.begin("123456789012345678901234567890+1"); return render(e);}},
    {"result-one-third", result("1/3")},
    {"result-sqrt-2", result("sqrt(2)")},
    {"result-pi-half", result("pi/2")},
    {"result-scientific", result("2^40*1000")},
    {"result-factored-pi", result("pi*(5sqrt(5)-1)/12")},
    {"result-factored-e", result("e*(sqrt(2)+1)")},
    {"result-factored-one", result("sqrt(5)*pi/12")},
    {"menu-catalog-root", menu(catalogRootRows(false), 0)},
    {"menu-catalog-category", menu(catalogCategoryRows(0), 0)},
    {"menu-tools", menu(toolRows(false), 0)},
    {"menu-format", [] { auto r = evaluated("1/3"); return decode(renderMenuList(formatRows(&r, true, math), 0, 0)); }},
    {"menu-functions", menu(functionRows(), 0)},
    {"menu-variables", [] { return decode(renderVariableGrid(zeroGridLabels(), 0)); }},
    {"menu-variables-last", [] { return decode(renderVariableGrid(zeroGridLabels(), 8)); }},
    {"menu-variable-actions", menu(variableActionRows(true), 0)},
    {"menu-notice", [] { return decode(renderDialog("Calculate a value first")); }},
    {"menu-reset", menu(resetRows(), 0)},
    {"menu-reset-confirm", [] { return decode(renderConfirm(resetQuestion(ResetAction::All), {{ConfirmAction::Yes, "Yes"}, {ConfirmAction::No, "No"}}, 2, 0)); }},
    {"menu-settings-root", [] { return decode(renderMenuList(settingsRootRows(math), 0, 0)); }},
    {"menu-settings-angle", [] { return decode(renderMenuList(settingsOptionRows(SettingAction::Angle, math), 0, 0)); }},
    {"field-store", [] {MathEditor e; e.begin("0"); Viewport v; return decode(renderAssignment("A=", e, math, false, v));}},
    {"field-define", [] {MathEditor e; e.begin("x+1"); Viewport v; return decode(renderAssignment("f(x)=", e, math, false, v));}},
    {"field-calc", [] {MathEditor e; e.begin("5"); Viewport v; Settings line = math; line.io = IO::LineDecimal; return decode(renderCalcPrompt("2*x+1", line, "x=", e, line, false, v));}},
    {"field-calc-math", [] {MathEditor e; e.begin("5"); Viewport v; return decode(renderCalcPrompt("2*x+1", math, "x=", e, math, false, v));}},
    // P13 A-10 split-style gate: natural parent under a Linear value field.
    {"field-calc-split", [] {MathEditor e; e.begin("5"); Viewport v; Settings line = math; line.io = IO::LineDecimal; return decode(renderCalcPrompt("2*x+1", math, "x=", e, line, false, v));}},
    // Phase 2 additions: every natural template and a filled integral.
    {"entry-integral-filled", [] {return render(at("integral(x,0,1)", 10));}},
    {"entry-derivative-filled", [] {return render(at("diff(x^(2),3)", 13));}},
    {"entry-product-filled", [] {return render(at("product(x,1,5)", 9));}},
    {"entry-log-base", [] {return render(at("log(2,8)", 7));}},
    {"entry-mixed", [] {return render(at("mixed(1,2,3)", 11));}},
    {"entry-abs", [] {return render(at("abs(-3)", 6));}},
    {"entry-nested-fraction", [] {return render(at("frac(1,frac(2,3))", 15));}},
    {"result-mixed", result("7/3")},
    {"entry-convert", [] {return render(at("convert(5,0)", 12));}},
  };
}

// ---------------------------------------------------------------- editor --
void wrapExpectations() {
  // P2: Power appends a postfix script; the base is the preceding atom at
  // layout/evaluation time. Only the no-atom fallback keeps pow(,).
  const char* wraps[][2] = {
    {"9", "9^()"}, {"123", "123^()"}, {"2x", "2x^()"}, {"AB", "AB^()"}, {"1E-3", "1E-3^()"},
    {"sin(x)", "sin(x)^()"}, {"(2+3)", "(2+3)^()"}, {"pow(9,3)", "pow(9,3)^()"}, {"-2", "-2^()"},
    {"(-2)", "(-2)^()"}, {"3!", "3!^()"}, {"x^(2)", "x^(2)^()"}, {"2+", "2+pow(,)"}, {"Ans", "Ans^()"},
    {"2pi", "2pi^()"}, {"1.5", "1.5^()"}, {"1E", "1E^()"},
  };
  for (const auto& w : wraps) {
    MathEditor e; e.begin(w[0]); e.press(K::Power);
    check(std::string("wrap ") + w[0] + " -> " + w[1] + " (got " + e.text() + ")", e.text() == w[1]);
  }
  {MathEditor e = typed({K::Digit2, K::X, K::Power, K::Digit3});
   check("2x^3 serializes a power of x only (" + e.text() + ")", e.text() == "2x^(3)");
   const Parsed p = parse(e.text());
   check("2x^3 is what the evaluator reads", p.error == Error::None && p.root->kind == Node::Kind::Binary);}
  {MathEditor e = typed({K::Digit2, K::Square}); check("x^2 is one finished power (" + e.text() + ")", e.text() == "2^(2)" && e.cursor() == 5);}
  {MathEditor e = typed({K::Square}); check("x^2 with nothing before fills the base first", e.text() == "pow(,2)" && e.cursor() == 4);}
  {MathEditor e = typed({K::Digit2, K::Shift, K::Power}); check("x^-1 (" + e.text() + ")", e.text() == "2^(-1)" && e.cursor() == 6);}
  {MathEditor e = typed({K::Digit2, K::Fraction}); check("fraction wraps into the numerator, cursor in the denominator", e.text() == "frac(2,)" && e.cursor() == 7);}
}

void postfixPowerExpectations() {
  // P2 acceptance: every newly keyed power-like form uses postfix scripts,
  // stays on a valid document stop, and evaluates through binary ^.
  auto valid = [](const MathEditor& e) {
    EditDocument d; d.rebuild(e.text()); return d.valid(e.cursor());
  };
  {MathEditor e = typed({K::Digit9, K::Power});
   check("power after an atom is ^() inside (" + e.text() + ")", e.text() == "9^()" && e.cursor() == 3 && valid(e));}
  {MathEditor e = typed({K::Open, K::Digit1, K::Add, K::Digit2, K::Close, K::Power});
   check("power after a compound atom appends (" + e.text() + ")", e.text() == "(1+2)^()" && valid(e));}
  {MathEditor e = typed({K::Digit9, K::Square});
   check("square is ^(2) after (" + e.text() + ")", e.text() == "9^(2)" && e.cursor() == 5 && valid(e));}
  {MathEditor e = typed({K::Digit9, K::Shift, K::Power});
   check("shift-power is ^(-1) after (" + e.text() + ")", e.text() == "9^(-1)" && e.cursor() == 6 && valid(e));}
  {MathEditor e = typed({K::Digit2, K::Scientific});
   check("scientific is *10^() inside (" + e.text() + ")", e.text() == "2*10^()" && e.cursor() == 6 && valid(e));}
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Power});
   check("nested power nests scripts (" + e.text() + ")", e.text() == "9^(3^())" && valid(e));}
  {MathEditor e = typed({K::Fraction, K::Digit1, K::Down, K::Digit2, K::Right, K::Power, K::Digit3});
   check("power after a fraction appends (" + e.text() + ")", e.text() == "frac(1,2)^(3)" && valid(e));}
  {MathEditor e = typed({K::Power});
   check("power on an empty line keeps the pow fallback (" + e.text() + ")", e.text() == "pow(,)" && valid(e));}
  {MathEditor e = typed({K::Shift, K::Power});
   check("shift-power on an empty line keeps its fallback (" + e.text() + ")", e.text() == "pow(,-1)" && valid(e));}
  // Legacy forms still parse, evaluate and lay out.
  {MathEditor e; e.begin("pow(9,3)"); e.setCursor(8);
   check("legacy pow parses and ends after", e.cursor() == 8 && valid(e));
   check("legacy pow evaluates", evaluated("pow(9,3)").value.number == 729);}
  {MathEditor e; e.begin("pow(9,3)+9^(3)");
   check("mixed legacy/new evaluates identically", evaluated(e.text()).value.number == 1458);}
  // START insertion joins the base (E-17 correction).
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3});
   e.press(K::PageUp); e.press(K::Digit2);
   check("START 2 joins the base (" + e.text() + ")", e.text() == "29^(3)" && valid(e));
   check("29^(3) is 24389", evaluated(e.text()).value.number == 24389);}
  {MathEditor e; e.begin("2x^(3)"); Request q; q.expression = e.text(); q.memory.variables[6] = Value(2);
   check("2x^(3) with x=2 is 2*x^3", evaluate(q).value.number == 16);}
  {MathEditor e; e.begin("(1/2)^(3)");
   check("(1/2)^(3) is 1/8", evaluated(e.text()).value.number == Decimal("0.125"));}
}

void shiftParenExpectations() {
  // Shift+( types = and Shift+) types a comma.
  {MathEditor e; e.press(K::Shift); e.press(K::Open);
   check("shift+( types = (" + e.text() + ")", e.text() == "=");}
  {MathEditor e; e.begin("1+2"); e.press(K::Shift); e.press(K::Open);
   check("shift+( appends = (" + e.text() + ")", e.text() == "1+2=");}
  {MathEditor e; e.press(K::Shift); e.press(K::Close);
   check("shift+) types , (" + e.text() + ")", e.text() == ",");}
  // Inside a natural template Shift+) advances past the hidden comma instead
  // of inserting one the serializer would reject.
  {MathEditor e; e.begin("integral(,,)"); e.setCursor(9);
   e.press(K::Shift); e.press(K::Close);
   check("shift+) advances a template field (cursor " + std::to_string(e.cursor()) + ")",
         e.text() == "integral(,,)" && e.cursor() == 10);}
  {MathEditor e; e.begin("integral(,,)"); e.setCursor(11);
   e.press(K::Shift); e.press(K::Close);
   check("shift+) in the last field exits the template (cursor " + std::to_string(e.cursor()) + ")",
         e.text() == "integral(,,)" && e.cursor() == 12);}
}

void navigationExpectations() {
  // Postfix scripts have no base field: Left from the exponent start lands
  // before the script, then walks the base atom, then exits to the root.
  {MathEditor e = typed({K::Digit9, K::Power}); e.press(K::Left); check("Left from exponent start reaches before the script", e.cursor() == 1);
   e.press(K::Left); check("Left walks the base", e.cursor() == 0);
   e.press(K::Left); check("Left at the root start stays (cursor " + std::to_string(e.cursor()) + ")", e.cursor() == 0);
   e.press(K::Right); check("Right from the root start moves past the base", e.cursor() == 1);}
  // Section 6.9, logical side of the reported failure.
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3});
   check("9^3 cursor before the close", e.text() == "9^(3)" && e.cursor() == 4);
   e.press(K::Digit2); check("then 2 extends the exponent", e.text() == "9^(32)" && e.cursor() == 5);
   e.press(K::Right); check("Right leaves the power, source unchanged", e.text() == "9^(32)" && e.cursor() == 6);
   e.press(K::Left); check("Left re-enters the exponent end", e.cursor() == 5);}
  // Nested powers exit one structure per Right.
  {MathEditor e = typed({K::Digit2, K::Power, K::Digit3, K::Power, K::Digit4});
   check("nested power source (" + e.text() + ")", e.text() == "2^(3^(4))" && e.cursor() == 7);
   e.press(K::Right); check("first Right: after the inner power, still in the outer exponent", e.cursor() == 8);
   e.press(K::Right); check("second Right: after the outer power", e.cursor() == 9);}
  // Up/Down between numerator and denominator keep the horizontal position.
  {MathEditor e = typed({K::Digit2, K::Fraction, K::Digit3});
   e.press(K::Up); check("Up from the denominator end lands at the numerator end (" + std::to_string(e.cursor()) + ")", e.cursor() == 6);
   e.press(K::Up); check("Up from the numerator stays in the numerator", e.cursor() == 6);
   e.press(K::Down); check("Down returns to the denominator end", e.cursor() == 8);}
  {MathEditor e = at("frac(123,4)", 6);  // numerator, after '1'
   e.press(K::Down); check("Down picks the nearest denominator stop by x (" + std::to_string(e.cursor()) + ")", e.cursor() == 9 || e.cursor() == 10);}
  // P3/E-02/E-03: neither a postfix script nor a legacy prefix power is a
  // vertical pair. Up/Down ascend straight through them: a no-op at root
  // level, an ancestor field when nested.
  {MathEditor e = at("pow(12,3)", 6); e.press(K::Up); check("Up from a legacy base is a no-op", e.cursor() == 6);
   e.press(K::Down); check("Down stays in the legacy base", e.cursor() == 6);}
  {MathEditor e = at("pow(12,3)", 8); e.press(K::Down); check("Down from a legacy exponent is a no-op", e.cursor() == 8);
   e.press(K::Up); check("Up stays in the legacy exponent", e.cursor() == 8);}
  // Legacy pow() stays fully navigable: every stop reachable, all valid.
  {MathEditor e; e.begin("pow(12,3)"); e.setCursor(0);
   EditDocument d; d.rebuild(e.text());
   bool ok = true;
   for (size_t o = 0; o <= e.text().size(); ++o) if (d.valid(o)) { e.setCursor(o); ok &= e.cursor() == o; }
   e.setCursor(0);
   for (size_t o = 0; o < e.text().size() && ok; ++o) { e.press(K::Right); ok &= d.valid(e.cursor()); }
   for (size_t o = e.text().size(); o > 0 && ok; --o) { e.press(K::Left); ok &= d.valid(e.cursor()); }
   check("legacy pow() walks end to end on valid stops", ok);}
  // Integral fields: limits sit left of the body, so Right from either limit
  // enters the body and Right from the body exits (Up/Down own the vertical
  // moves), like a fraction's numerator/denominator pair.
  {MathEditor e; e.begin(); catalogInsert(e, "integral(,,)");
   check("integral opens in its body", e.text() == "integral(,,)" && e.cursor() == 9);
   e.press(K::Right); check("Right: body -> after", e.cursor() == 12);
   e.setCursor(9); e.press(K::Down); check("Down: body -> lower", e.cursor() == 10);
   e.press(K::Right); check("Right: lower -> body", e.cursor() == 9);
   e.press(K::Down); e.press(K::Up); check("Up: lower -> upper", e.cursor() == 11);
   e.press(K::Right); check("Right: upper -> body", e.cursor() == 9);
   e.setCursor(10); e.press(K::Left); check("Left: lower -> before", e.cursor() == 0);
   e.setCursor(11); e.press(K::Left); check("Left: upper -> before", e.cursor() == 0);
   e.setCursor(9); e.press(K::Up); check("Up: body -> upper", e.cursor() == 11);
   e.press(K::Down); check("Down: upper -> lower", e.cursor() == 10);
   // Limits are drawn left of the body: Left from after the integral enters
   // the body end, not the upper limit (stored last).
   e.setCursor(9); e.press(K::X); e.press(K::Right); check("Right: x body -> after", e.cursor() == 13);
   e.press(K::Left); check("Left: after -> body end", e.cursor() == 10);}
  // '(' and ')' inside a template field type one paren each, never a pair or
  // a jump; an unmatched one stays loose and never breaks the template.
  {const std::string O(1, '\x05'), C(1, '\x06');
   auto integral = [](const MathEditor& e) {
     EditDocument d; d.rebuild(e.text());
     return d.item(d.field(d.root()).first).kind == EditDocument::Kind::Integral;
   };
   MathEditor e; e.begin(); catalogInsert(e, "integral(,,)"); e.press(K::Open);
   check("'(' in a body is one loose paren", e.text() == "integral(" + O + ",,)" && e.cursor() == 10 && integral(e));
   e.press(K::X); e.press(K::Close);
   check("')' pairs it into a group (" + e.text() + ")", e.text() == "integral((x),,)" && e.cursor() == 12 && integral(e));
   e.press(K::Close);
   check("an extra ')' stays loose", e.text() == "integral((x)" + C + ",,)" && e.cursor() == 13 && integral(e));
   e.press(K::Delete); e.press(K::Delete);
   check("deleting a ')' leaves its '(' loose", e.text() == "integral(" + O + "x,,)" && e.cursor() == 11 && integral(e));
   e.press(K::Delete); e.press(K::Delete);
   check("deleting the last loose paren empties the body", e.text() == "integral(,,)" && integral(e));
   e.press(K::Open); e.press(K::Close); e.press(K::Open);
   check("'() (' keeps the template", e.text() == "integral(()" + O + ",,)" && integral(e));
   e.press(K::Close); e.press(K::Close);
   check("'() () )' keeps the template", e.text() == "integral(()()" + C + ",,)" && integral(e));
   e.begin(); catalogInsert(e, "integral(,,)"); e.press(K::Sin); e.press(K::X);
   check("sin keeps its auto-closed call", e.text() == "integral(sin(x),,)" && integral(e));
   e.press(K::Right); e.press(K::Delete);
   check("deleting a call's ')' loosens its '('", e.text() == "integral(sin" + O + "x,,)" && integral(e));
   const Parsed open = parse("integral(" + O + "x+1,0,1)");
   check("loose '(' closes at its field end", open.error == Error::None && open.root &&
         open.root->kind == Node::Kind::Call && open.root->children.size() == 3);
   const Parsed call = parse("integral(sin" + O + "x,0,1)");
   check("loose call takes one argument", call.error == Error::None && call.root && call.root->children.size() == 3 &&
         call.root->children[0]->kind == Node::Kind::Call && call.root->children[0]->text == "sin");
   check("loose ')' is a Syntax ERROR", parse("integral(x" + C + ",0,1)").error == Error::Syntax);}
  // Missing close: the end belongs to the open field; Right writes the ')'.
  {MathEditor e; e.begin("pow(9,3");
   check("open template: end is in the exponent", e.cursor() == 7);
   e.press(K::Digit4); check("typing lands in the exponent", e.text() == "pow(9,34");
   e.press(K::Right); check("Right materializes the close (" + e.text() + ")", e.text() == "pow(9,34)" && e.cursor() == 9);
   e.undo(); check("that close is its own undo step", e.text() == "pow(9,34" && e.cursor() == 8);}
  // Close key inside a template leaves it; in a group it types ')'.
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Close}); check("')' in an exponent is a loose paren", e.text() == "9^(3\x06)" && e.cursor() == 5);}
  {MathEditor e = typed({K::Open, K::Digit2, K::Close}); check("')' after '(' closes the group", e.text() == "(2)" && e.cursor() == 3);}
  // Engine error positions inside hidden syntax normalize to a visible stop.
  {MathEditor e; e.begin("pow(9,3)"); e.setCursor(2); check("offset in 'pow' goes to the base", e.cursor() == 4);
   e.begin("Ans+1"); e.setCursor(1); check("offset inside Ans goes to its start", e.cursor() == 0);}
  // Middle insertion.
  {MathEditor e = typed({K::Digit1, K::Digit2, K::Digit3, K::Left, K::Left, K::Digit9});
   check("middle insertion (" + e.text() + ")", e.text() == "1923" && e.cursor() == 2);}
  // Function names are atomic; function keys insert open calls (P5/E-16).
  {MathEditor e = typed({K::Sin, K::Digit5}); check("sin opens its argument", e.text() == "sin(5" && e.cursor() == 5);
   e.press(K::Left); e.press(K::Left); check("Left from the argument start jumps over 'sin('", e.cursor() == 0);}
  // Multi-argument call: each comma boundary has a field end and a next start.
  {MathEditor e = at("gcd(12,8)", 6); e.press(K::Right); check("Right across a comma", e.cursor() == 7);
   e.press(K::Left); check("Left back across it", e.cursor() == 6);}
}

void p3TraversalExpectations() {
  // P3: arrows land where the ClassWiz reference lands. Horizontal order is
  // reading order; Up/Down own the vertical field pairs; the renderer stays
  // passive (x matching only via MathLayout::nearest).
  auto valid = [](const MathEditor& e) {
    EditDocument d; d.rebuild(e.text()); return d.valid(e.cursor());
  };
  // 1. Down in a root exponent is a no-op; typing extends the exponent (C06).
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3});
   e.press(K::Down); check("Down in 9^3 is a no-op", e.text() == "9^(3)" && e.cursor() == 4 && valid(e));
   e.press(K::Digit2); check("then 2 extends the exponent", e.text() == "9^(32)" && valid(e));}
  // 2. Up from ordinary root text near a Script is a no-op (C16 shape).
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Left, K::Left});
   e.press(K::Up); check("Up from 9-after-3 is a no-op", e.text() == "9^(3)" && e.cursor() == 1 && valid(e));
   e.press(K::Digit2); check("then 2 joins the base", e.text() == "92^(3)" && valid(e));}
  // 3. Down from an exponent nested in a numerator ascends to the fraction
  // denominator (C25), generically through the Script owner.
  {MathEditor e = typed({K::Fraction, K::Digit9, K::Power, K::Digit3});
   e.press(K::Down);
   EditDocument d; d.rebuild(e.text());
   check("Down from the nested exponent reaches the denominator",
         d.slotField(e.cursor()) != EditDocument::kNone &&
         d.field(d.slotField(e.cursor())).role == Role::Denominator && valid(e));
   e.press(K::Digit2); check("then 2 lands in the denominator", e.text() == "frac(9^(3),2)" && valid(e));}
  // 4/5. Root Right/Left wrap with the main-editor policy (C14/C15).
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Right, K::Digit2});
   e.wrapHorizontal = true;
   e.press(K::Right); check("Right at the root end wraps to the root start", e.cursor() == 0 && valid(e));}
  {MathEditor e; e.begin("12"); e.setCursor(0); e.wrapHorizontal = true;
   e.press(K::Left); check("Left at the root start wraps to the root end", e.cursor() == 2 && valid(e));
   e.press(K::Digit3); check("then 3 appends", e.text() == "123" && valid(e));}
  // 6. Secondary field editors keep today's stay-put behavior.
  {MathEditor e; e.begin("12"); e.setCursor(0);
   e.press(K::Left); check("field editor Left at start stays", e.cursor() == 0);
   e.setCursor(2); e.press(K::Right); check("field editor Right at end stays", e.cursor() == 2);
   // begin()/setCursor() never flip the policy behind the controller.
   e.wrapHorizontal = true; e.begin("12"); e.setCursor(0);
   e.press(K::Left); check("begin() preserves wrapHorizontal", e.cursor() == 2);}
  // 7. Right at a numerator end leaves the whole fraction (C17).
  {MathEditor e = typed({K::Fraction, K::Digit1});
   e.press(K::Right);
   EditDocument d; d.rebuild(e.text());
   check("Right leaves the fraction to the root",
         d.slotField(e.cursor()) != EditDocument::kNone &&
         d.field(d.slotField(e.cursor())).role == Role::Root && valid(e));
   e.press(K::Digit2); check("then 2 follows the fraction", e.text() == "frac(1,)2" && valid(e));}
  // 8. Down still enters the denominator (C18 shape).
  {MathEditor e = typed({K::Fraction, K::Digit1});
   e.press(K::Down); e.press(K::Digit2);
   check("Down enters the denominator", e.text() == "frac(1,2)" && valid(e));}
  // 9. Left at a denominator start leaves to before the fraction (C22/C23).
  {MathEditor e = typed({K::Fraction, K::Digit1, K::Down});
   e.press(K::Left); check("Left from an empty denominator start is before the fraction", e.cursor() == 0 && valid(e));
   e.press(K::Digit3); check("then 3 precedes the fraction", e.text() == "3frac(1,)" && valid(e));}
  {MathEditor e = typed({K::Fraction, K::Digit1, K::Down, K::Digit2, K::Left, K::Left});
   e.press(K::Left); check("Left from a filled denominator start is before the fraction", e.cursor() == 0 && valid(e));
   e.press(K::Digit3); check("then 3 precedes the filled fraction", e.text() == "3frac(1,2)" && valid(e));}
  // 10. Up from the denominator returns to the numerator with x-nearest
  // behavior (C26 shape keeps passing through the same path).
  {MathEditor e = typed({K::Fraction, K::Digit1, K::Down, K::Digit2, K::Up});
   EditDocument d; d.rebuild(e.text());
   check("Up returns to the numerator",
         d.field(d.slotField(e.cursor())).role == Role::Numerator && valid(e));
   e.press(K::Digit3); check("then 3 extends the numerator", e.text() == "frac(13,2)" && valid(e));}
  // 11. Nested fractions: horizontal exits stay local to each level.
  {MathEditor e; e.begin("frac(1,frac(2,))"); e.setCursor(13);  // inner numerator end
   e.press(K::Right);
   EditDocument d; d.rebuild(e.text());
   check("Right at an inner numerator end leaves the inner fraction",
         e.text() == "frac(1,frac(2,))" && d.field(d.slotField(e.cursor())).role == Role::Denominator && valid(e));}
  // 12. Script inside a fraction: Down climbs past the script to the
  // denominator; Right at the exponent end steps out one level at a time.
  {MathEditor e; e.begin("frac(9^(3),)"); e.setCursor(9);  // exponent end
   e.press(K::Down);
   EditDocument d; d.rebuild(e.text());
   check("Down climbs past the script to the denominator",
         d.field(d.slotField(e.cursor())).role == Role::Denominator && valid(e));}
  // 13. Fraction inside a Script: Down in the inner exponent is a no-op at
  // the script level and finds no fraction sibling either.
  {MathEditor e; e.begin("9^(frac(1,))"); e.setCursor(9);  // inner numerator end
   e.press(K::Down);
   EditDocument d; d.rebuild(e.text());
   check("Down enters the inner denominator",
         d.field(d.slotField(e.cursor())).role == Role::Denominator && valid(e));}
  {MathEditor e; e.begin("9^(frac(1,2))"); e.setCursor(11);  // inner denominator end
   const size_t before = e.cursor();
   e.press(K::Down); check("Down with no sibling field is a no-op", e.cursor() == before && valid(e));
   e.press(K::Up);
   EditDocument dUp; dUp.rebuild(e.text());
   check("Up still owns denominator->numerator inside a Script",
         dUp.field(dUp.slotField(e.cursor())).role == Role::Numerator && valid(e));}
  // 14. Sweep: every arrow from every stop of representative sources stays a
  // legal document stop, with the main-editor policy on and off.
  const char* sweep[] = {"", "12", "9^(3)", "9^(32)", "2^(3^(4))", "pow(9,3)", "pow(12,34)+5",
                         "frac(1,)", "frac(1,2)", "frac(9^(3),2)", "9^(frac(1,2))", "frac(1,frac(2,3))",
                         "mixed(1,2,3)", "sqrt(2)", "root(3,8)", "log(2,8)", "2+3*4", "sin(5)",
                         "integral(x,0,1)", "9^(3", "frac(1,"};
  for (bool wrap : {false, true}) {
    for (const char* s : sweep) {
      EditDocument d; d.rebuild(s);
      for (size_t o = 0; o <= std::string(s).size(); ++o) {
        if (!d.valid(o)) continue;
        for (int arrow = 0; arrow < 4; ++arrow) {
          MathEditor e; e.begin(s); e.setCursor(o); e.wrapHorizontal = wrap;
          if (arrow == 0) e.press(K::Left);
          else if (arrow == 1) e.press(K::Right);
          else if (arrow == 2) e.press(K::Up);
          else e.press(K::Down);
          EditDocument after; after.rebuild(e.text());
          if (!after.valid(e.cursor())) {
            check(std::string("arrow ") + std::to_string(arrow) + " from " + std::to_string(o) +
                  " in \"" + s + "\" stays valid (wrap=" + (wrap ? "on" : "off") + ")", false);
            goto nextSource;
          }
        }
      }
      nextSource:;
    }
  }
  check("every arrow from every stop stays a valid document stop", true);
}

void deletionExpectations() {
  // P4/E-10: DEL at a legacy power exponent start splices base and exponent.
  {MathEditor e = at("pow(9,3)", 6); e.press(K::Delete);
   check("DEL at a legacy exponent start splices pow (" + e.text() + ")", e.text() == "93" && e.cursor() == 1);}
  // P4/E-10: DEL at a filled denominator start splices the fraction (C31).
  {MathEditor e = at("frac(1,2)", 7); e.press(K::Delete);
   check("DEL at a filled denominator start splices the fraction (" + e.text() + ")", e.text() == "12" && e.cursor() == 1);}
  {MathEditor e = typed({K::Fraction}); e.press(K::Delete); check("DEL in an empty template removes it whole", e.text().empty() && e.cursor() == 0);}
  {MathEditor e = at("2+sqrt()", 8); e.press(K::Delete); check("DEL after an empty template removes it whole", e.text() == "2+" && e.cursor() == 2);}
  {MathEditor e = at("sqrt(2)", 5); e.press(K::Delete); check("DEL at a filled radicand start unwraps it (" + e.text() + ")", e.text() == "2" && e.cursor() == 0);}
  // P4/E-09: DEL after a filled power enters it and deletes the last item.
  {MathEditor e = at("pow(9,3)", 8); e.press(K::Delete); check("DEL after a filled power deletes its last exponent item", e.text() == "pow(9,)" && e.cursor() == 6);}
  {MathEditor e = at("(2)", 3); e.press(K::Delete); check("DEL after a group removes its visible ')'", e.text() == "(2" && e.cursor() == 2);}
  {MathEditor e = at("sin(5)", 4); e.press(K::Delete); check("DEL at a call's argument start removes 'sin('", e.text() == "5)" && e.cursor() == 0);}
  {MathEditor e = at("Ans", 3); e.press(K::Delete); check("DEL removes a name whole", e.text().empty());}
  // Undo: one step per operation, a second undo redoes.
  {MathEditor e = typed({K::Digit2, K::Square}); e.undo(); check("undo x^2 in one step (" + e.text() + ")", e.text() == "2" && e.cursor() == 1);
   e.undo(); check("second undo redoes it", e.text() == "2^(2)" && e.cursor() == 5);}
  // SHIFT+AC is the device's switch-off (UserInput consumes the AC); reaching
  // the editor anyway it is a plain AC, never an undo.
  {MathEditor e = typed({K::Digit1, K::Digit2, K::Shift, K::Clear}); check("SHIFT+AC is not undo", e.text().empty());}
  // Inserting ')' or ',' straight into a hidden field is refused.
  {MathEditor e = typed({K::Digit9, K::Power}); e.insert(","); check("',' in an exponent is refused", e.text() == "9^()" && !e.notice.empty());
   e.insertTemplate("sin()"); check("a balanced template in an exponent is fine (" + e.text() + ")", e.text() == "9^(sin())" && e.cursor() == 7);}
  // Unit conversion wraps the operand with an atomic unit.
  {MathEditor e; e.begin("5"); e.wrapWith("convert(", ",3)"); check("conversion wraps and ends after", e.text() == "convert(5,3)" && e.cursor() == 12);
   e.press(K::Left); check("Left from after a conversion enters its operand", e.cursor() == 9);}
  {MathEditor e; e.begin(); e.press(K::Interrupted); check("interruption sets review", e.review && !e.shifted);}
  // Adjacent tokens never merge into one evaluator name.
  const char* separated[][4] = {
    {"Ans", "3", "7", "Ans*7"}, {"pi", "2", "2", "pi*2"}, {"e", "1", "x", "e*x"}, {"AB", "2", "7", "AB7"},
    {"2", "1", "pi", "2pi"}, {"2", "0", "pi", "pi*2"}, {"x", "1", "2", "x2"}, {"", "0", "sin()", "sin()"},
  };
  for (const auto& c : separated) {
    MathEditor e; e.begin(c[0]); e.setCursor(std::stoul(c[1])); e.insert(c[2]);
    check(std::string("insert ") + c[2] + " into " + c[0] + " -> " + c[3] + " (got " + e.text() + ")", e.text() == c[3]);
    check(std::string("evaluator reads ") + c[3], parse(e.text()).error == Error::None);
  }
  {MathEditor e; e.begin("2"); e.setCursor(0); e.insert("pi"); check("cursor after an inserted name, before its separator", e.cursor() == 2);}
}

void p4StructuralDeletionExpectations() {
  // P4 acceptance: structural DEL matches the ClassWiz reference (E-08/E-09/
  // E-10). Every branch ends on a valid EditDocument stop with parseable
  // source; cursor placement is asserted, not just text. Reference case IDs
  // in comments; UNKNOWN-behavior pins are marked pending, never claimed.
  auto valid = [](const MathEditor& e) {
    EditDocument d; d.rebuild(e.text()); return d.valid(e.cursor());
  };
  auto role = [](const MathEditor& e) {
    EditDocument d; d.rebuild(e.text());
    const uint16_t f = d.slotField(e.cursor());
    return f == EditDocument::kNone ? Role::Root : d.field(f).role;
  };
  // 1. Root-start forward DEL (C27): "12" cursor 0 DEL -> "2", then 3 -> "32".
  {MathEditor e; e.begin("12"); e.setCursor(0); e.press(K::Delete);
   check("root-start DEL deletes forward (" + e.text() + ")", e.text() == "2" && e.cursor() == 0 && valid(e));
   e.press(K::Digit3);
   check("then 3 gives 32", e.text() == "32" && valid(e));}
  // 2. Forward DEL inside a first field (C28): numerator kept, then 3 fills it.
  {MathEditor e = at("frac(1,)", 5); e.press(K::Delete);
   check("numerator-start DEL keeps the fraction (" + e.text() + ")", e.text() == "frac(,)" && e.cursor() == 5 && role(e) == Role::Numerator && valid(e));
   e.press(K::Digit3);
   check("then 3 fills the numerator", e.text() == "frac(3,)" && valid(e));}
  // 3. DEL immediately after a populated fraction, one key (C33/E-09).
  {MathEditor e = at("frac(1,2)", 9); e.press(K::Delete);
   check("DEL after 1/2 deletes the 2 in one key (" + e.text() + ")", e.text() == "frac(1,)" && role(e) == Role::Denominator && valid(e));}
  // 4. DEL immediately after sqrt, then again (C35/E-09).
  {MathEditor e = at("sqrt(9)", 7); e.press(K::Delete);
   check("DEL after sqrt(9) empties the radicand (" + e.text() + ")", e.text() == "sqrt()" && role(e) == Role::Radicand && valid(e));
   e.press(K::Delete);
   check("second DEL removes the empty sqrt", e.text().empty() && e.cursor() == 0 && valid(e));}
  // 5. Denominator-start splice with continuation (C31/E-10).
  {MathEditor e = at("frac(1,2)", 7); e.press(K::Delete);
   check("denominator-start DEL splices 1/2 (" + e.text() + ")", e.text() == "12" && e.cursor() == 1 && valid(e));
   e.press(K::Digit3);
   check("then 3 gives 132", e.text() == "132" && valid(e));}
  // Empty-denominator splice (C33 second DEL): frac(1,) at denom start -> "1".
  {MathEditor e = at("frac(1,)", 7); e.press(K::Delete);
   check("empty-denominator DEL unwraps to 1 (" + e.text() + ")", e.text() == "1" && e.cursor() == 1 && valid(e));}
  // 6. Script exponent-start splice with continuation (C32/E-10).
  {MathEditor e = at("9^(3)", 3); e.press(K::Delete);
   check("exponent-start DEL splices the script (" + e.text() + ")", e.text() == "93" && e.cursor() == 1 && valid(e));
   e.press(K::Digit2);
   check("then 2 gives 923", e.text() == "923" && valid(e));}
  // 7. Empty Script deletion still removes the wrapper (C34, no regression).
  {MathEditor e = at("9^()", 3); e.press(K::Delete);
   check("DEL in an empty exponent removes the script (" + e.text() + ")", e.text() == "9" && e.cursor() == 1 && valid(e));
   e.press(K::Delete);
   check("second DEL removes the 9", e.text().empty() && valid(e));}
  // 8. Sqrt unwrap keeps the radicand (C30 shape, no regression).
  {MathEditor e = typed({K::Root, K::Digit9, K::Left});
   e.press(K::Delete); e.press(K::Digit2);
   check("C30 shape gives 29 (" + e.text() + ")", e.text() == "29" && valid(e));}
  // 9. Visible closing parenthesis deletes singly (C37, no regression).
  {MathEditor e = at("(1+2)", 5); e.press(K::Delete);
   check("DEL after (1+2) drops ) (" + e.text() + ")", e.text() == "(1+2" && e.cursor() == 4 && valid(e));
   e.press(K::Delete);
   check("second DEL drops 2", e.text() == "(1+" && valid(e));}
  // 10. Nested first-field forward DEL (pending reference: pin the safe
  // behavior, do not claim compatibility).
  {MathEditor e = at("frac(frac(1,2),3)", 10); e.press(K::Delete);
   check("nested numerator-start DEL deletes inward, stays valid (" + e.text() + ")",
         e.text() == "frac(frac(,2),3)" && e.cursor() == 10 && valid(e));}
  // 12. Undo still returns to the pre-key snapshot (current P4 mechanism).
  {MathEditor e = at("frac(1,2)", 7); e.press(K::Delete);
   e.undo();
   check("undo restores the spliced fraction (" + e.text() + ")", e.text() == "frac(1,2)" && e.cursor() == 7 && valid(e));}
  // 13. Legacy pow(...) deletion cannot strand the cursor.
  {MathEditor e = at("pow(9,3)", 4); e.press(K::Delete);
   check("DEL at a legacy base start forward-deletes (" + e.text() + ")", e.text() == "pow(,3)" && e.cursor() == 4 && valid(e));}
  {MathEditor e = at("pow(,3)", 4); e.press(K::Delete);
   check("DEL at an empty legacy base start is a valid no-op (" + e.text() + ")", e.text() == "pow(,3)" && e.cursor() == 4 && valid(e));}
  // 14. Deletion around postfix ^() never recreates a hidden base stop.
  {MathEditor e = at("9^(3)", 3); e.press(K::Delete);
   EditDocument d; d.rebuild(e.text());
   bool scriptGone = true;
   for (size_t i = 0; i < d.items(); ++i) if (d.item(i).kind == EditDocument::Kind::Script) scriptGone = false;
   check("spliced 93 has no Script item (" + e.text() + ")", scriptGone && e.text() == "93" && valid(e));}
  // (11) Every check above also asserts cursor validity via valid(e).
}

// ------------------------------------------------------------- layout --
void caretExpectations() {
  // Section 6.9, the reported failure: 9, Power, 3 inserts into the exponent,
  // so the caret must sit right after the raised 3, above the base baseline.
  for (auto keys : {std::vector<K>{K::Digit9, K::Power, K::Digit3}, std::vector<K>{K::Digit9, K::Power, K::Digit3, K::Digit2}}) {
    MathEditor e; e.begin(); for (K k : keys) e.press(k);
    const Rect ink = expressionInk(e), base = firstGlyphInk(e), c = caret(e);
    const std::string where = " (caret x" + std::to_string(c.left) + " rows " + std::to_string(c.top) + ".." + std::to_string(c.bottom) +
        ", exponent ink right " + std::to_string(ink.right) + ", base bottom " + std::to_string(base.bottom) + ")";
    check("exponent caret is raised" + where, !c.empty() && c.bottom < base.bottom - 2);
    check("exponent caret follows the last digit" + where, !c.empty() && c.left >= ink.right - 1 && c.left <= ink.right + 3);
    check("exponent caret is the script caret", lastRender().style == Style::Script && lastRender().role == Role::Exponent &&
          c.bottom - c.top + 1 == 9);
    MathEditor probe = e; probe.press(K::Digit4);
    check("next digit goes into the exponent (" + probe.text() + ")", probe.text().find("4)") != std::string::npos);
  }
  // After Right the caret spans the owning root field, including its script.
  MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Right});
  const Rect c = caret(e), base = firstGlyphInk(e);
  check("exited caret spans the parent field (caret bottom " + std::to_string(c.bottom) + ", base bottom " + std::to_string(base.bottom) + ")",
        !c.empty() && c.bottom == base.bottom + 1 && lastRender().style == Style::Normal);
  MathEditor probe = e; probe.press(K::Digit4);
  check("after Right the next digit follows the power (" + probe.text() + ")", probe.text() == "9^(3)4");
  // Fraction and radical ends stay inside their field.
  {EditDocument d; MathLayout l; d.rebuild("frac(12,3)"); l.layout(d);
   const int bar = -6;
   check("numerator end caret above the bar", l.caret(7).y + l.caret(7).height <= bar);
   check("denominator end caret below the bar", l.caret(9).y > bar);
   d.rebuild("sqrt(22)"); l.layout(d);
   check("radicand end stays under the radical", l.slot(7).baseline == l.slot(5).baseline && l.slot(7).x < l.box().width && l.slot(8).x >= l.slot(7).x);}
  // Ordinary normal field: 3x13.
  {MathEditor n = typed({K::Digit1}); const Rect k = caret(n);
   check("normal caret is 3x13", k.right - k.left + 1 == 3 && k.bottom - k.top + 1 == 13);}
}

void p6ParenExpectations() {
  // The three P6 regressions are real passing assertions in P7. Keep them:
  // single-line delimiters use contained ink, independent of caret minima.
  EditDocument d;
  MathLayout l;
  d.rebuild("1+2");
  l.layout(d);
  const auto plain = l.box();
  d.rebuild("(1+2)");
  l.layout(d);
  const auto paren = l.box();
  check("P7: single-row (1+2) uses variant 0 (ascent unchanged)", paren.ascent == plain.ascent);
  check("P7: line ascent unchanged by parens", paren.ascent == plain.ascent && paren.descent == plain.descent);
  d.rebuild("2^(3)");
  l.layout(d);
  const int baseWidth = l.box().width;
  d.rebuild("2^((3))");
  l.layout(d);
  const int scriptParenWidth = l.box().width;
  const auto* scriptOpen = MathFont::find('(', Style::Script);
  const int scriptParenPair = scriptOpen ? 2 * scriptOpen->advance : 12;
  check("P7: 2^((3)) uses the script paren", scriptParenWidth - baseWidth == scriptParenPair);
}

void p7GeometryExpectations() {
  using R = MathLayout::Rect;
  auto rect = [&](const std::string& name, R r, int x, int y, int w, int h) {
    check(name + " got (" + std::to_string(r.x) + "," + std::to_string(r.y) + "," +
          std::to_string(r.width) + "," + std::to_string(r.height) + ")",
          r.x == x && r.y == y && r.width == w && r.height == h);
  };
  auto screenCaret = [&](const char* source, size_t offset, int x, int y, int w, int h) {
    MathEditor e = at(source, offset); render(e);
    rect(std::string("P7 caret ") + source, lastRender().caretRect, x,y,w,h);
  };
  screenCaret("",0,0,0,3,13);
  screenCaret("9",1,11,0,3,13);
  screenCaret("9^()",3,11,0,3,9);
  screenCaret("9^(3)",4,17,0,3,9);
  screenCaret("9^(32)",5,23,0,3,9);
  screenCaret("9^(3)",5,19,0,3,17);
  screenCaret("frac(1,)",8,15,0,3,30);
  auto noCaret = [&](const char* source) { auto e=at(source,std::strlen(source)); Viewport v;
    return decode(renderCalculation(e,math,nullptr,Format::Standard,false,false,v,"",false)); };
  auto rasterRect = [&](const std::string& name, const Frame& f, int top, int bottom, int left, int right, int x,int y,int w,int h) {
    const Rect r=inkBounds(f,top,bottom,left,right);
    rect(name,{int16_t(r.left),int16_t(r.top),int16_t(r.right-r.left+1),int16_t(r.bottom-r.top+1)},x,y,w,h);
  };
  rasterRect("P7 digit origin",noCaret("9"),0,20,0,10,2,0,8,12);
  const Frame power=noCaret("9^(3)");
  rasterRect("P7 base ink",power,0,30,0,10,2,4,8,12);
  rasterRect("P7 exponent ink",power,0,30,11,17,12,0,5,9);
  const Frame fraction=noCaret("frac(1,)");
  rasterRect("P7 fraction rule",fraction,14,14,0,20,1,14,14,1);
  rasterRect("P7 denominator box",fraction,17,35,0,20,3,17,10,13);
  for (int y : {12,13,15,16}) check("P7 blank fraction gap " + std::to_string(y), inkBounds(fraction,y,y,0,20).empty());
  for(const char* source:{"(frac(1,2))","(sqrt(frac(1,2)))"}) {
    const auto f=noCaret(source);const auto left=inkBounds(f,0,62,0,10);
    check(std::string("P7 tall delimiter variant ")+source,!left.empty() && left.bottom-left.top+1>MathFont::find('(',Style::Normal)->height);
  }
  auto contains = [](R a,R b) { return !b.width || !b.height || (a.width && a.height && a.x<=b.x && a.y<=b.y && a.x+a.width>=b.x+b.width && a.y+a.height>=b.y+b.height); };
  const char* sources[]={"","9","9^(3)","9^(32)","2^(3^(4))","(1+2)","((1))","2^((3))",
    "frac(1,)","frac(1,2)","frac(9^(3),2)","(frac(1,2))","sqrt(9)","sqrt(9^(3))","root(3,x)",
    "frac(1,2)^(3)","sqrt(9)^(3)","(sqrt(frac(1,2)))","sin(1","sin(1)","2^(sin(3))",
    "integral(x,0,1)","sum(x,1,5)","diff(x,3)","log(2,8)","convert(5,0)","constant(2)","abs(9)",
    "12345678901234567890^(32)","12345678901234567890frac(1,2)","frac(12,3)","frac(1,23)","frac(frac(1,2),3)"};
  for(const char* source:sources) {
    MathEditor e=at(source,std::strlen(source)); const auto cursor=e.cursor();
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);
    check(std::string("P7 source/cursor immutable ")+source,d.source()==source && e.cursor()==cursor);
    check(std::string("P7 root ink union ")+source,contains(l.fieldInk(0),l.ink()));
    for(uint16_t i=0;i<d.items();++i) {
      const auto& it=d.item(i);
      // Hidden constant/conversion fields are not painted and have empty ink.
      check("P7 field contains item ink",contains(l.fieldInk(it.field),l.itemInk(i)));
      if(it.kind==EditDocument::Kind::Glyph && l.itemInk(i).width && source[it.begin]>='0' && source[it.begin]<='9') {
        const auto sl=l.slot(it.begin);const auto* g=MathFont::find(source[it.begin],sl.style);
        check("P7 digit ink lies in item",g && contains(l.itemInk(i),{int16_t(sl.x+g->inkLeft()),int16_t(sl.baseline+g->yOffset),g->width,g->height}));
        check("P7 pen progresses by advance, not ink",g && l.slot(it.end).x-sl.x==g->advance);
      }
      for(uint16_t f=it.firstField;f!=EditDocument::kNone;f=d.field(f).next)
        check("P7 item contains child ink",contains(l.itemInk(i),l.fieldInk(f)));
      if(it.kind==EditDocument::Kind::Fraction) {
        const auto n=l.fieldInk(d.fieldAt(i,0)),den=l.fieldInk(d.fieldAt(i,1));
        check("P7 fraction rule separates actual ink by 2/2",
          den.y-(n.y+n.height)==MathMetrics::kFractionNumeratorInkGap+
            MathMetrics::kFractionRuleThickness+MathMetrics::kFractionDenominatorInkGap);
      }
      if(it.kind==EditDocument::Kind::Script) {
        const auto a=l.slot(it.begin),b=l.slot(d.field(it.firstField).begin);
        check("P7 Script attaches at preceding advance",a.x==b.x);
      }
    }
    for(size_t o=0;o<=d.source().size();++o) if(d.valid(o)) {
      const auto f=d.slotField(o);const auto b=l.fieldBox(f);const auto k=l.caret(o);const auto sl=l.slot(o);
      const auto& m=sl.style==Style::Normal?MathMetrics::kNormal:MathMetrics::kScript;
      // Caret follows its line, never the whole field union: it is at least
      // the style minima and at most the owning field's height. The exact
      // neighbour rule (digit shrinks, tall structure stays tall) is pinned
      // by the dedicated nested-fraction checks below.
      const int full=std::max<int>(b.ascent,m.caretMinAscent)+std::max<int>(b.descent,m.caretMinDescent);
      const int min= int(m.caretMinAscent)+int(m.caretMinDescent);
      check("P7 caret within its field",k.y<=sl.baseline-m.caretMinAscent &&
        k.y+k.height>=sl.baseline+m.caretMinDescent &&
        k.height>=min && k.height<=full);
    }
    { // Casio fx-880BTG: stacking fractions grows the caret, typing a digit
      // beside the tall stack shrinks it back to the digit/line height.
      EditDocument dd; MathLayout ll;
      dd.rebuild("frac(1,frac(2,3)9)"); ll.layout(dd);
      const size_t after9=dd.field(dd.slotField(dd.source().size()-1)).end-1;
      // after9 lands on the '9' end inside the outer denominator; the field
      // union is tall from the sibling inner fraction, the caret is not.
      size_t off=dd.source().size()+1;
      for(size_t o=0;o<=dd.source().size();++o)
        if(dd.valid(o)&&dd.slotField(o)==dd.slotField(after9)&&dd.itemEndingAt(o,dd.slotField(o))!=EditDocument::kNone) {
          const auto it=dd.item(dd.itemEndingAt(o,dd.slotField(o)));
          if(dd.source().compare(it.begin,it.end-it.begin,"9")==0) off=o;
        }
      if(off<=dd.source().size()) {
        const auto b=ll.fieldBox(dd.slotField(off)); const auto k=ll.caret(off);
        check("nested fraction caret shrinks after 9",
          k.height==MathMetrics::kNormal.caretMinAscent+MathMetrics::kNormal.caretMinDescent &&
          b.ascent+b.descent>k.height);
      } else check("nested fraction caret shrinks after 9",false);
      // After the whole tall fraction the caret stays tall.
      dd.rebuild("frac(1,2)"); ll.layout(dd);
      const auto end=ll.caret(dd.source().size());
      check("caret after a tall fraction stays tall",
        end.height>MathMetrics::kNormal.caretMinAscent+MathMetrics::kNormal.caretMinDescent);
    }
    // Paint all the geometry on a larger canvas: raster bounds must equal the
    // measured union (no fake caret/minimum ink; all decorations included).
    GFXcanvas1 canvas(1024,1024);canvas.fillScreen(0);l.paint(canvas,400,400,{0,0,1024,1024});
    R painted{};int x0=1024,y0=1024,x1=-1,y1=-1;
    for(int y=0;y<1024;++y)for(int x=0;x<1024;++x)if(canvas.getPixel(x,y)) {x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
    if(x1>=x0)painted={int16_t(x0-400),int16_t(y0-400),int16_t(x1-x0+1),int16_t(y1-y0+1)};
    const auto ink=l.ink();rect(std::string("P7 exact painted union ")+source,painted,ink.x,ink.y,ink.width,ink.height);
  }
  { // Normal placeholders read 2px (outer outline plus one inset round);
    // small script cells stay 1px. Outer size is unchanged, so this only
    // adds ink inside the old normal outline.
    const std::pair<const char*, int> cases[] = {{"frac(,)", 2}, {"9^()", 1}, {"integral(,,)", 3}, {"sum(,,)", 3}};
      for (const auto& c : cases) {
        EditDocument dd; MathLayout ll; dd.rebuild(c.first); ll.layout(dd);
        GFXcanvas1 canvas(256, 256); canvas.fillScreen(0);
        const int16_t dx = 100, dy = 100;
        ll.paint(canvas, dx, dy, {0, 0, 256, 256});
        int boxes = 0; bool ok = true;
        for (uint16_t f = 1; f < dd.fields(); ++f) {
          if (dd.field(f).first != EditDocument::kNone) continue;
          const size_t o = dd.field(f).begin;
          if (!dd.valid(o) || dd.slotField(o) != f) continue;
          const auto sl = ll.slot(o);
          const bool script = sl.style == Style::Script;
          const int w = script ? 6 : 10, h = script ? 9 : 13;
          const int x = sl.x + dx, y = sl.baseline - (script ? 9 : 12) + dy;
          ++boxes;
          for (int i = 0; i < w; ++i) ok &= canvas.getPixel(x + i, y) && canvas.getPixel(x + i, y + h - 1);
          for (int i = 0; i < h; ++i) ok &= canvas.getPixel(x, y + i) && canvas.getPixel(x + w - 1, y + i);
          if (!script) {
            for (int i = 1; i < w - 1; ++i) ok &= canvas.getPixel(x + i, y + 1) && canvas.getPixel(x + i, y + h - 2);
            for (int i = 1; i < h - 1; ++i) ok &= canvas.getPixel(x + 1, y + i) && canvas.getPixel(x + w - 2, y + i);
          } else {
            for (int i = 1; i < w - 1; ++i) ok &= !canvas.getPixel(x + i, y + 1) && !canvas.getPixel(x + i, y + h - 2);
            for (int i = 1; i < h - 1; ++i) ok &= !canvas.getPixel(x + 1, y + i) && !canvas.getPixel(x + w - 2, y + i);
          }
        }
        check(std::string("placeholder width ") + c.first, ok && boxes == c.second);
      }
  }
  for(const char* source:{"1/3","9","729","sqrt(2)"}) {
    auto e=at(source,std::strlen(source));auto result=evaluated(source);const auto f=render(e,&result);const auto info=lastRender();
    const auto ink=inkBounds(f,info.result.y,62);
    check(std::string("P7 result anchors ")+source,ink.right==191 && ink.bottom==60);
    if(std::string(source)=="1/3")rasterRect("P7 result fraction rule",f,46,46,170,191,178,46,14,1);
  }
  for(const char* label:{"A=","f(x)="}) {
    auto e=at("9^(3)",4);Viewport v;renderAssignment(label,e,math,false,v);
    check("P7 assignment script caret",lastRender().caretRect.height==9 && lastRender().caretRect.y==0);
  }
  {auto e=at("9",1);Viewport v;renderCalcPrompt("9^(3)",math,"x=",e,math,false,v);
   rect("CALC selected-row caret",lastRender().caretRect,lastRender().caretRect.x,49,1,13);}
  {const auto f=decode(renderSolution("x","\x01" "1/3" "\x02",math));const auto b=inkBounds(f,17,62);
   check("P7 solution anchors",b.right==191 && b.bottom==60);}
  // '|' in canonical source is the Base-N octal prefix. Exercise the actual
  // vertical-bar font glyph through the label path, and Abs through layout.
  {const auto* g=MathFont::find('|',Style::Normal);GFXcanvas1 c(192,63);c.fillScreen(0);
   const int pen=192-g->inkRight();paintLabel(c,"|",pen,20,Style::Normal);
   check("P7 bar overhang independent of advance",g->inkLeft()==-1 && g->width!=g->advance);
   bool right=false;for(int y=0;y<63;++y)right|=c.getPixel(191,y);
   check("P7 right-edge bar visible",right);}
  for(const char* suffix:{"9","9^(32)","frac(1,2)","abs(9)"}) {
    std::string source="12345678901234567890";source+=suffix;auto e=at(source,source.size());Viewport v;
    const auto f=decode(renderCalculation(e,math,nullptr,Format::Standard,false,false,v,"",false));
    const int scroll=v.x;renderCalculation(e,math,nullptr,Format::Standard,false,false,v);
    const auto k=lastRender().caretRect;
    check("P7 edge viewport preserves blink geometry",v.x==scroll && v.x>0 && k.x>=0 && k.x+k.width<=192);
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);const auto ink=l.ink();
    check("P7 rightmost ink fits viewport",ink.x+ink.width+MathMetrics::kMarginLeft-v.x<=192);
    check("P7 rightmost visible item paints",!inkBounds(f,0,62,170,191).empty());
  }
}

// Invariant sweep (plan 13.3): for every stop, the next printable typed there
// lands in the field the caret was drawn in, on the caret's baseline and in
// its style, at the caret's x within that field. (Absolute x can move: a
// centred denominator or limit re-centres as it widens.)
void slotInvariant() {
  const char* sources[] = {"", "123", "9^(3)", "2^(3^(4))", "pow(9,3)", "frac(1,2)", "mixed(1,2,3)", "sqrt(22)", "root(3,8)",
                           "abs(-3)", "log(2,8)", "diff(x,3)", "integral(x,0,1)", "sum(x,1,5)", "product(x,1,5)", "sin(x)+cos(30)",
                           "gcd(12,8)", "(2+3)*4", "2x^(2)+1", "9^(3", "pow(9,3", "frac(1,", "Ans+pi*e", "convert(5,3)", "constant(2)+1",
                           "1E-3", "frac(sqrt(2),x^(2))", "sum(x^(2),1,10)"};
  for (const char* s : sources) {
    EditDocument d; MathLayout l; d.rebuild(s); l.layout(d);
    const std::string source = s;
    for (size_t o = 0; o <= source.size(); ++o) {
      if (!d.valid(o)) continue;
      const auto slot = l.slot(o);
      const uint16_t field = d.slotField(o);
      const int relative = slot.x - l.slot(d.field(field).begin).x;
      const std::string where = std::string(" in \"") + s + "\" at " + std::to_string(o);
      MathEditor e; e.begin(source); e.setCursor(o);
      check("a stop survives setCursor" + where, e.cursor() == o);
      e.insert("7");
      EditDocument d2; MathLayout l2; d2.rebuild(e.text()); l2.layout(d2);
      const uint16_t f2 = d2.slotField(e.cursor());
      const uint16_t typed7 = f2 == EditDocument::kNone ? EditDocument::kNone : d2.itemEndingAt(e.cursor(), f2);
      if (typed7 == EditDocument::kNone || e.text()[d2.item(typed7).begin] != '7') {check("the typed digit ends at the cursor" + where + " (" + e.text() + ")", false); continue;}
      const auto &before = d.field(field), &after = d2.field(f2);
      check("insertion lands in the caret's field" + where, before.role == after.role && before.begin == after.begin);
      const auto start = l2.slot(o);
      check("insertion starts on the caret's baseline and style" + where, start.baseline == slot.baseline && start.style == slot.style);
      check("insertion starts at the caret's x within its field" + where, start.x - l2.slot(after.begin).x == relative);
    }
    // Layout is idempotent and painting does not move anything.
    MathLayout again; again.layout(d);
    bool same = true;
    for (size_t o = 0; o <= source.size(); ++o) if (d.valid(o)) same &= again.slot(o).x == l.slot(o).x && again.slot(o).baseline == l.slot(o).baseline;
    check(std::string("layout repeats identically: ") + s, same && again.box().width == l.box().width);
  }
  // Rendering twice gives the same frame and caret.
  MathEditor e = at("frac(sqrt(2),x^(2))", 12);
  const Frame a = render(e); const auto k1 = lastRender().caretRect;
  const Frame b = render(e); const auto k2 = lastRender().caretRect;
  check("render is repeatable", a == b && k1.x == k2.x && k1.y == k2.y);
}

void viewportExpectations() {
  // A long expression scrolls so the caret stays inside the panel, and the
  // caret and glyphs share the translation.
  MathEditor e; e.begin();
  Viewport view;
  for (int i = 0; i < 30; ++i) {
    e.press(K::Digit1);
    decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, view));
    const auto k = lastRender().caretRect;
    if (k.x < 0 || k.x + k.width > 192) {check("caret stays on screen while typing (step " + std::to_string(i) + ")", false); break;}
  }
  check("long input scrolled", view.x > 0);
  const int16_t scrolled = view.x;
  const int16_t column = lastRender().caretRect.x;
  e.press(K::Left);
  decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, view));
  check("Left on a line hidden to the left rolls it under the caret",
        view.x == scrolled - MathMetrics::kNormalDigitAdvance && lastRender().caretRect.x == column);
  e.press(K::PageUp);
  decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, view));
  check("start of input scrolls back to zero", view.x == 0 && lastRender().caretRect.x >= 0);
  // Tall nesting keeps the active field visible.
  MathEditor tall = at("frac(frac(frac(1,2),3),frac(4,frac(5,6)))", 37);
  Viewport v2;
  decode(renderCalculation(tall, math, nullptr, Format::Standard, false, false, v2));
  const auto k = lastRender().caretRect;
  check("caret of a tall expression is inside the input region", k.y >= lastRender().input.y && k.y + k.height <= lastRender().input.y + lastRender().input.height);
  // Blink-off hides the caret paint but keeps the scroll: an idle long
  // expression must not jump back to the origin every 500 ms.
  {
    MathEditor e; e.begin("123456789012345678901234567890+1");
    Viewport v;
    const Frame on = decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v, "", true));
    check("blink fixture scrolled", v.x > 0);
    const int16_t scrolled = v.x;
    const auto lit = lastRender().caretRect;
    const Frame off = decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v, "", false));
    check("blink-off keeps the scroll", v.x == scrolled);
    check("blink-off paints no caret", !lastRender().caret);
    bool sameOutsideCaret = true;
    for (int y = 0; y < 63 && sameOutsideCaret; ++y)
      for (int x = 0; x < 192; ++x) {
        const bool inCaret = x >= lit.x && x < lit.x + lit.width && y >= lit.y && y < lit.y + lit.height;
        if (!inCaret && on.at(x, y) != off.at(x, y)) {sameOutsideCaret = false; break;}
      }
    check("blink-off only removes the caret ink", sameOutsideCaret);
    decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v, "", true));
    check("blink-on restores the caret at the same scroll", lastRender().caret && v.x == scrolled);
  }
}

// 2026-09-25 Casio parity fixes: whole-token DEL, base parentheses around an
// empty argument, sin^-1, engineering symbols, CATALOG order, and the
// CasioEmuX scroll model (caret as far right as one digit of room allows).
void casioParityExpectations() {
  // DEL never splits a function name.
  {MathEditor e = at("cos(5)", 0); e.press(K::Delete); check("forward DEL takes 'cos(' whole (" + e.text() + ")", e.text() == "5)" && e.cursor() == 0);}
  {MathEditor e = at("cos()", 0); e.press(K::Delete); check("forward DEL removes an empty call whole (" + e.text() + ")", e.text().empty());}
  {MathEditor e = typed({K::Cos}); e.setCursor(0); e.press(K::Delete); check("forward DEL removes an open 'cos(' whole (" + e.text() + ")", e.text().empty());}
  {MathEditor e = typed({K::Shift, K::Sin, K::Digit1}); e.setCursor(0); e.press(K::Delete); check("forward DEL removes 'asin(' whole (" + e.text() + ")", e.text() == "1");}
  {MathEditor e = at("cos()", 4); e.press(K::Delete); check("DEL in an empty closed call removes it whole (" + e.text() + ")", e.text().empty() && e.cursor() == 0);}
  // An empty argument keeps the 12-row pair a digit gets.
  for (const char* source : {"cos(", "cos()", "(", "cos(5)"}) {
    EditDocument d; MathLayout l; d.rebuild(source); l.layout(d);
    check(std::string("base parentheses around ") + source,
          l.box().ascent == MathMetrics::kNormal.ascent && l.box().descent == MathMetrics::kNormal.descent);
  }
  {EditDocument d; MathLayout l; d.rebuild("cos("); l.layout(d); const int16_t open = l.slot(4).x;
   d.rebuild("cos(5"); l.layout(d);
   check("typing into cos( keeps its '(' (argument does not move)", l.slot(4).x == open);}
  // Inverse trig draws as sin^-1: the name plus an 11-column raised -1.
  for (const char* name : {"sin", "cos", "tan"}) {
    EditDocument d; MathLayout l; d.rebuild(std::string("a") + name + "(5)"); l.layout(d); const int inverse = l.box().width;
    const auto ink = l.ink();
    d.rebuild(std::string(name) + "(5)"); l.layout(d);
    check(std::string("a") + name + " draws as " + name + "^-1", inverse - l.box().width == 11 && ink.y == l.ink().y);
  }
  // Engineering symbols: one atomic postfix x10^n byte.
  check("5k is 5000", evaluated("5\x15").value.number == 5000);
  check("2+3m is 2.003", evaluated("2+3\x10").value.number == Decimal("2.003"));
  check("(5k)^2 binds the symbol first", evaluated("5\x15^(2)").value.number == Decimal("25000000"));
  check("1p is exact", evaluated("1\x13").value.symbolic.exact());
  check("a leading symbol is a syntax error", evaluated("\x15").error == Error::Syntax);
  {MathEditor e = at("5\x15", 2); e.press(K::Delete); check("DEL removes an engineering symbol whole", e.text() == "5");}
  {MathEditor e = at("5\x15", 2); e.press(K::Square); check("x^2 takes 5k as its base (" + e.text() + ")", e.text() == "5\x15^(2)");}
  {EditDocument d; MathLayout l; d.rebuild("5\x15"); l.layout(d);
   check("k draws as the baked symbol, not '?'", l.box().width == MathMetrics::kNormalDigitAdvance + 11 && l.ink().height == 12);
   check("Linear shows k", LinearPresenter::present(d).display == "5k");}
  {bool symbols = true; for (const auto& group : catalogData()) if (group.name == "Engineer Symbol")
     for (const auto& entry : group.entries) symbols = symbols && std::strlen(entry.expression) == 1 && EngSymbol::is(entry.expression[0]);
   check("CATALOG Engineer Symbol inserts symbol bytes", symbols);}
  // CATALOG: Other is the last root row.
  for (AppId app : {AppId::Calculate, AppId::Complex, AppId::Matrix}) {
    const auto rows = catalogRootRows(false, app);
    check("Other is the last CATALOG row", !rows.empty() && rows.back().label == "Other");
  }
  // Scrolling: the 17th digit rolls the line, leaving one digit of room.
  {MathEditor e = at(std::string(16, '9'), 16); Viewport v;
   decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v));
   check("16 digits fit unscrolled", v.x == 0);
   e.press(K::Digit9); decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v));
   const auto k = lastRender().caretRect;
   check("the 17th digit rolls the line", v.x > 0 && k.x + 1 + MathMetrics::kNormalDigitAdvance <= 191);}
  // 20 digits then Left: the caret holds its column while the line rolls
  // (CasioEmuX: 179 at the end, 170 once the right arrow appears).
  {MathEditor e = at("01234567890123456789", 20); Viewport v;
   decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v));
   const int16_t end = lastRender().caretRect.x;
   e.press(K::Left); decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v));
   check("Left from the end keeps the caret column", lastRender().caretRect.x == end);
   e.press(K::Left); decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v));
   const int16_t pinned = lastRender().caretRect.x;
   check("with more text on the right the caret keeps a digit before the arrow", pinned + 1 + MathMetrics::kNormalDigitAdvance <= 182);
   e.press(K::Left); decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v));
   check("Left x3 rolls the line, not the caret", lastRender().caretRect.x == pinned && v.x > 0);}
  // A template entered at the right edge is never cut by the right arrow.
  for (const char* tail : {"sqrt()", "integral(,,)", "log(,)"}) {
    MathEditor e; e.begin(std::string(16, '9')); e.insertTemplate(tail); Viewport v;
    const Frame f = decode(renderCalculation(e, math, nullptr, Format::Standard, false, false, v, "", false));
    EditDocument d; d.rebuild(e.text()); MathLayout l; l.layout(d);
    const auto box = l.fieldInk(d.slotField(e.cursor()));
    const int right = box.x + box.width + MathMetrics::kMarginLeft - v.x;
    const bool arrow = l.ink().x + l.ink().width + MathMetrics::kMarginLeft - v.x > 192;
    check(std::string("active box right of 16 digits stays clear of the arrow: ") + tail, right <= (arrow ? 182 : 192));
    (void)f;
  }
}

void resultExpectations() {
  const char* cases[][2] = {
    {"1/3", "frac(1,3)"}, {"-1/3", "-frac(1,3)"}, {"2 1/3", "mixed(2,1,3)"}, {"-2 1/3", "-mixed(2,1,3)"},
    {"2*pi/3", "frac(2pi,3)"}, {"pi", "pi"}, {"-pi/2", "-frac(pi,2)"}, {"3*sqrt(2)", "3sqrt(2)"},
    {"sqrt(3)/2", "frac(sqrt(3),2)"}, {"1/2+sqrt(3)/2", "frac(1,2)+frac(sqrt(3),2)"}, {"1-sqrt(2)", "1-sqrt(2)"},
    {"1.099511628E+12", "1.099511628*10^(12)"}, {"5E-5", "5*10^(-5)"},
    // P12 closure: the legacy solution-span adapter no longer handles pair
    // ("; "), sexagesimal d/m/s, recurring "(..)" or prime "^" grammars;
    // those views travel the typed path (R-04).
    {"True", "True"}, {"42", "42"},
  };
  for (const auto& c : cases) check(std::string("present ") + c[0] + " -> " + c[1] + " (got " + presentResult(c[0]) + ")", presentResult(c[0]) == c[1]);
  // A fitting result is right-aligned; the result region is below the input.
  MathEditor e; e.begin("1/3"); Result r = evaluated("1/3");
  const Frame f = render(e, &r);
  const auto info = lastRender();
  const Rect ink = inkBounds(f, info.result.y, info.result.y + info.result.height - 1);
  check("fraction result is laid out as a fraction", info.presented == "frac(1,3)");
  check("result is right-aligned", !ink.empty() && ink.right >= 185);
  check("input sits above the result", info.input.y + info.input.height <= info.result.y);
  check("no caret while a result shows", !info.caret);
}

void menuExpectations() {
  // Phase 3: stable action IDs survive filtering and reordering.
  {
    const auto tools = toolRows(false);
    check("TOOLS reference order CALC/Undo/Verify", tools.size() == 3 && tools[0].label == "CALC" &&
              tools[1].label == "Undo" && tools[2].label == "Verify ON",
          false);
    check("TOOLS actions stable", tools[0].action == ToolAction::Calc && tools[1].action == ToolAction::Undo &&
                                      tools[2].action == ToolAction::VerifyToggle);
    const auto on = toolRows(true);
    check("TOOLS Verify label follows state, action does not",
          on[2].label == "Verify OFF" && on[2].action == ToolAction::VerifyToggle);
  }
  {
    const auto root = catalogRootRows(false);
    const auto& data = catalogData();
    const int verify = verifyGroupIndex();
    check("catalog root hides Verify without renumbering",
          // Verify, Complex, the three Base-N groups, Matrix and Vector hidden.
          root.size() == data.size() - 7 + 2 && verify >= 0);
    const auto baseN = catalogRootRows(false, AppId::BaseN);
    check("Base-N catalog is its three groups", baseN.size() == 3 && baseN[0].label == "Hex Value");
    bool matrixGroup = false;
    for (const auto& row : catalogRootRows(false, AppId::Matrix)) matrixGroup = matrixGroup || row.label == "Matrix";
    check("Matrix catalog has its group", matrixGroup);
    for (const auto& row : root) {
      if (row.action == CatalogRootAction::SciConstants || row.action == CatalogRootAction::UnitConversions) {
        check("catalog table row opens a child", row.submenu);
        continue;
      }
      check("catalog action is its data index (" + row.label + ")",
            row.action >= 0 && row.action < static_cast<int>(data.size()) && data[static_cast<size_t>(row.action)].name == row.label && row.submenu);
    }
    const auto withVerify = catalogRootRows(true);
    check("catalog root shows Verify in verify mode", withVerify.size() == data.size() - 6 + 2);
    bool complexGroup = false, analysis = false;
    for (const auto& row : catalogRootRows(false, AppId::Complex)) {
      complexGroup = complexGroup || row.label == "Complex";
      analysis = analysis || row.label == "Func Analysis";
    }
    check("Complex CATALOG adds its category and drops Func Analysis", complexGroup && !analysis);
    check("Complex TOOLS has no CALC", toolRows(false, false).size() == 2 && toolRows(false, false)[0].action == ToolAction::Undo);
    for (const auto& group : data)
      check("catalog category fits with shortcut and arrow ("+group.name+")",menuDisplayLabel(group.name).size()<=14);
    const auto category = catalogCategoryRows(0);
    check("catalog category rows carry entry indices",
          !category.empty() && category[0].action == 0 && !category[0].submenu &&
              catalogData()[0].entries[0].label == category[0].label);
  }
  {
    // FORMAT capabilities depend on the retained result.
    Result third = evaluated("1/3"), twelve = evaluated("12"), root2 = evaluated("sqrt(2)");
    const auto rational = formatRows(&third, true, math);
    auto has = [&](const std::vector<MenuItem>& rows, Format f) {
      for (const auto& r : rows) if (r.action == static_cast<int>(f)) return true;
      return false;
    };
    check("1/3 offers mixed/improper/recurring but not prime",
          has(rational, Format::Mixed) && has(rational, Format::Recurring) && !has(rational, Format::Prime));
    const auto integer = formatRows(&twelve, true, math);
    check("12 offers prime but not mixed", has(integer, Format::Prime) && !has(integer, Format::Mixed));
    const auto irrational = formatRows(&root2, true, math);
    check("sqrt(2) offers neither mixed nor prime",
          !has(irrational, Format::Mixed) && !has(irrational, Format::Prime) && has(irrational, Format::Standard));
    Result bad;
    bad.error = Error::Syntax;
    check("error result offers Standard only", formatRows(&bad, true, math).size() == 1);
    check("no result offers Standard only", formatRows(nullptr, false, math).size() == 1);
    check("real results never offer coordinate forms", !has(rational, Format::Rectangular) && !has(integer, Format::Polar));
    Result z = evaluated("1");
    z.complex = true;
    z.imag = Value(1);
    const auto complexRows = formatRows(&z, true, math);
    check("complex result offers Standard/Decimal/Rectangular/Polar",
          complexRows.size() == 4 && has(complexRows, Format::Rectangular) && has(complexRows, Format::Polar) &&
              !has(complexRows, Format::Prime));
    // Actions are Format values, so reordering rows cannot retarget them.
    for (const auto& r : rational) check("FORMAT action is its Format value", r.action >= 0 && r.action < 8);
  }
  {
    // Store appears only when a value is available; Recall/Edit keep IDs.
    const auto with = variableActionRows(true), without = variableActionRows(false);
    check("variable actions with result", with.size() == 3 && with[1].action == VariableAction::Store);
    check("variable actions without result omit Store",
          without.size() == 2 && without[0].action == VariableAction::Recall && without[1].action == VariableAction::Edit);
    Result good = evaluated("1+1");
    check("canStore needs a clean result", canStoreResult(&good, true) && !canStoreResult(&good, false));
    Result bad;
    bad.error = Error::Math;
    check("canStore refuses errors", !canStoreResult(&bad, true));
  }
  {
    // Settings: building rows never mutates; OK applies via explicit options.
    // The root carries eight bare labels (values live on the option pages as
    // radio state, so nothing truncates into "..." on the 192 px rows).
    Settings before = math;
    const auto hierarchy = settingsRootRows(math);
    check("P9 Settings hierarchy",hierarchy.size()==4 && hierarchy[0].label=="Calc Settings" &&
        hierarchy[1].label=="System Settings" && hierarchy[2].label=="Reset" &&
        hierarchy[0].action==12 && hierarchy[1].action==13 && hierarchy[2].action==10 &&
        hierarchy[0].submenu && hierarchy[1].submenu && hierarchy[2].submenu &&
        hierarchy[3].label=="Get Started" && hierarchy[3].action==14 && hierarchy[3].submenu);
    const auto root = calcSettingsRows(math);
    const char* bare[] = {"Input/Output", "Angle Unit", "Number Format", "Engineer Symbol",
                          "Fraction Result", "Complex Result", "Decimal Mark", "Digit Separator"};
    check("Calc Settings preserves eight rows plus existing key preferences", root.size() == 10 &&
        root[8].action==8 && root[9].action==9);
    for (size_t i = 0; i < root.size() && i < 8; ++i)
      check(std::string("settings root row ") + std::to_string(i) + " is " + bare[i],
            root[i].label == bare[i] && root[i].submenu);
    const int actions[] = {SettingAction::InputOutput, SettingAction::Angle, SettingAction::Number,
                           SettingAction::Engineer, SettingAction::Fraction, SettingAction::Complex,
                           SettingAction::DecimalMark, SettingAction::Separators};
    for (size_t i = 0; i < root.size() && i < 8; ++i)
      check("settings root action is stable", root[i].action == actions[i]);
    check("settings unchanged by row construction", math.io == before.io && math.angle == before.angle);
    const auto angle = settingsOptionRows(SettingAction::Angle, math);
    check("angle options carry radio state",
          angle.size() == 3 && angle[0].mark == UiKit::Mark::kRadioOn && angle[1].mark == UiKit::Mark::kRadioOff);
    const auto complex = settingsOptionRows(SettingAction::Complex, math);
    check("complex options carry radio state (a+bi default)",
          complex.size() == 2 && complex[0].mark == UiKit::Mark::kRadioOn && complex[1].mark == UiKit::Mark::kRadioOff);
    const auto digits = settingsOptionRows(SettingAction::Digits, math);
    check("digits page has ten choices", digits.size() == 10);
    Settings probe = math;
    bool clear = false;
    check("applying the highlighted angle works",
          applySettingOption(SettingAction::Angle, 1, probe, clear) && probe.angle == Angle::Radian && !clear);
    check("applying IO requests a history clear",
          applySettingOption(SettingAction::InputOutput, 2, probe, clear) && clear);
    check("applying complex stores the preference",
          applySettingOption(SettingAction::Complex, 1, probe, clear) && probe.complexResult == ComplexResult::Polar && !clear);
    check("out-of-range choice is refused", !applySettingOption(SettingAction::Angle, 9, probe, clear));
    check("complex refuses out-of-range", !applySettingOption(SettingAction::Complex, 2, probe, clear));
  }
  {
    // Reset asks before acting; confirmation carries Yes/No.
    const auto reset = resetRows();
    check("reset root has four rows, first three open confirm",
          reset.size() == 4 && reset[0].submenu && reset[1].submenu && reset[2].submenu && !reset[3].submenu);
    check("reset actions stable",
          reset[0].action == ResetAction::Variables && reset[3].action == ResetAction::Cancel);
    check("reset questions name the scope", !resetQuestion(ResetAction::All).empty());
  }
  {
    // Parent stack: bounded, restores selection and window.
    MenuStack stack;
    MenuContext a;
    a.screen = 0;
    a.subpage = -1;
    a.selected = 2;
    a.first = 1;
    check("stack pushes", stack.push(a) && stack.size() == 1 && stack.top().selected == 2);
    for (size_t i = 1; i < kMenuStackMax; ++i) { MenuContext c; c.screen = 3; if (!stack.push(c)) break; }
    check("stack is bounded", stack.size() == kMenuStackMax && !stack.push(a));
    stack.pop();
    check("stack pops", stack.size() + 1 == kMenuStackMax);
    stack.clear();
    check("stack clears", stack.empty());
  }
  {
    // Caller-owned windows: clamp, keep the selection visible, step by the
    // visible four rows rather than the old five-item paging.
    check("clamp keeps short lists at zero", UiKit::clampFirst(4, 3, kMenuVisibleRows) == 0);
    check("clamp holds the tail", UiKit::clampFirst(99, 11, kMenuVisibleRows) == 7);
    check("ensureVisible pulls just far enough",
          UiKit::ensureVisible(0, 5, 11, kMenuVisibleRows) == 2 && UiKit::ensureVisible(2, 2, 11, kMenuVisibleRows) == 2);
    check("bare lists page by four", kMenuVisibleRows == 4);
    // Owned window vs historical derived window, through UiKit directly:
    // selection 2 of 9 rows derives first=2, so an explicit first=2 must
    // paint the identical frame; an explicit first=4 for selection 5 must
    // differ from the derived first=5.
    {
      std::vector<UiKit::Row> uiRows;
      for (int i = 0; i < 9; ++i)
        uiRows.push_back(UiKit::Row{String(("Row " + std::to_string(i)).c_str()), false, UiKit::Mark::kNone});
      auto paint = [&](int selected, const UiKit::Window* window) {
        GFXcanvas1 canvas(192, 63);
        canvas.fillScreen(0);
        UiKit::list(canvas, uiRows, selected, 0, kMenuVisibleRows, false, window);
        return decode(CanvasText::packCanvas(canvas, 192, 63, 1512));
      };
      const UiKit::Window owned{2}, scrolled{4};
      check("owned window matches derived window when they agree", paint(2, nullptr) == paint(2, &owned));
      check("window ownership scrolls", !(paint(5, nullptr) == paint(5, &scrolled)));
    }
    // renderMenuList honors the first it is given.
    std::vector<MenuItem> nine;
    for (int i = 0; i < 9; ++i) nine.push_back({i, "Row " + std::to_string(i)});
    check("menu list paints the owned window",
          decode(renderMenuList(nine, 5, 4)) == decode(renderMenuList(nine, 5, 4)) &&
              !(decode(renderMenuList(nine, 5, 0)) == decode(renderMenuList(nine, 5, 4))));
  }
  {
    // Bare geometry: no title or rule above the first row. The old private
    // list drew its title at y=0 and a rule at y=9; a bare menu must leave
    // row 0 for content (ink may start anywhere in the first 16px band, but
    // no full-width rule may sit at y=9).
    const Frame tools = decode(renderMenuList(toolRows(false), 0, 0));
    bool rule = true;
    for (int x = 0; x < 192; ++x)
      if (!tools.at(x, 9)) rule = false;
    check("bare menus carry no title rule", !rule);
    // Notice is a dialog, not a selectable list: it must not carry a
    // selection bar (a full-width inverted row).
    const Frame notice = decode(renderDialog("Calculate a value first"));
    bool bar = false;
    for (int y = 0; y < 63 && !bar; ++y) {
      bool full = true;
      for (int x = 0; x < 188; x += 7)
        if (!notice.at(x, y)) full = false;
      if (full) bar = true;
    }
    check("notice dialog has no selection bar", !bar);
  }
}

void variableGridExpectations() {
  // Names, rows, columns and the missing bottom-right cell.
  const char* names = "ABCDEFxyz";
  for (int i = 0; i < 9; ++i)
    check(std::string("variable name ") + std::to_string(i), variableName(i) == names[i]);
  check("variableName rejects out-of-range", variableName(-1) == '?' && variableName(9) == '?');
  for (int i = 0; i < 9; ++i)
    check("row/col round-trip " + std::to_string(i), variableIndexAt(variableRow(i), variableColumn(i)) == i);
  check("bottom-right cell is missing",
        variableIndexAt(4, 1) == -1 && variableIndexAt(5, 0) == -1 && variableIndexAt(-1, 0) == -1 &&
            variableIndexAt(0, 2) == -1);
  // Movement: every cell keeps a valid selection; edges clamp, never wrap; a
  // step into the missing cell stays put, preserving the column.
  const int rightOf[9] = {1, 1, 3, 3, 5, 5, 7, 7, 8};
  const int leftOf[9] = {0, 0, 2, 2, 4, 4, 6, 6, 8};
  const int upOf[9] = {0, 1, 0, 1, 2, 3, 4, 5, 6};
  const int downOf[9] = {2, 3, 4, 5, 6, 7, 8, 7, 8};
  for (int i = 0; i < 9; ++i) {
    check("grid Right " + std::to_string(i), moveVariableSelection(i, GridMove::Right) == rightOf[i]);
    check("grid Left " + std::to_string(i), moveVariableSelection(i, GridMove::Left) == leftOf[i]);
    check("grid Up " + std::to_string(i), moveVariableSelection(i, GridMove::Up) == upOf[i]);
    check("grid Down " + std::to_string(i), moveVariableSelection(i, GridMove::Down) == downOf[i]);
  }
  check("grid clamps garbage",
        moveVariableSelection(-1, GridMove::Down) == 0 && moveVariableSelection(99, GridMove::Up) == 0);
  // Values always display in Norm 1 (manual page 44).
  {
    Settings norm2 = math, fix = math, norm1 = math;
    norm2.number = NumberFormat::Norm2;
    fix.number = NumberFormat::Fix;
    fix.digits = 0;
    norm1.number = NumberFormat::Norm1;
    const Value small{Decimal("0.0001")};
    check("variable label forces Norm 1 (" + variableLabel(6, small, norm2) + ")",
          variableLabel(6, small, norm2) == "x=" + formatValue(small, norm1));
    check("Norm 1 differs from Norm 2 here", formatValue(small, norm1) != formatValue(small, norm2));
    const Result third = evaluated("1/3");
    check("variable label shows decimals, never exact forms (" + variableLabel(0, third.value, fix) + ")",
          variableLabel(0, third.value, fix) == "A=" + formatValue(third.value, norm1, Format::Decimal) && variableLabel(0, third.value, fix).find('/') == std::string::npos);
  }
  // Grid paint: no title band, selection confined to one half-width cell.
  {
    const Frame g = decode(renderVariableGrid(zeroGridLabels(), 0));
    check("grid starts at y0 without title band",g.at(0,0) && g.at(90,0) && !g.at(91,0));
    int selectedInk = 0, neighborInk = 0;
    for (int y = 0; y < 11; ++y) {
      for (int x = 0; x < 91; ++x) selectedInk += g.at(x, y) ? 1 : 0;
      for (int x = 97; x < 192; ++x) neighborInk += g.at(x, y) ? 1 : 0;
    }
    check("selection fills its cell (" + std::to_string(selectedInk) + ")", selectedInk > 700);
    check("selection never reaches the neighbor (" + std::to_string(neighborInk) + ")", neighborInk < 300);
  }
  // A long value is truncated inside its own cell.
  {
    auto labels = zeroGridLabels();
    labels[0] = "A=123456789012345678901234567890";
    const Frame g = decode(renderVariableGrid(labels, 3));
    bool gapClean = true;
    for (int y = 0; y < 11 && gapClean; ++y)
      for (int x = 91; x < 97; ++x)
        if (g.at(x, y)) gapClean = false;
    check("long values never cross into the neighbor cell", gapClean);
  }
  // Assignment and CALC views.
  {
    MathEditor e = typed({K::Digit1, K::Digit2});
    Viewport v;
    const Frame a = decode(renderAssignment("A=", e, math, false, v));
    check("assignment shows its label", !inkBounds(a, 0, 53, 0, 20).empty());
    check("assignment caret follows the value", lastRender().caret && lastRender().style == Style::Normal);
  }
  {
    Settings line = math;
    line.io = IO::LineDecimal;
    MathEditor f;
    f.begin("5");
    Viewport v;
    const Frame c = decode(renderCalcPrompt("2*x+1", line, "x=", f, line, false, v));
    check("CALC retains the parent above", !inkBounds(c, 0, 30).empty());
    check("CALC field sits below the parent", !inkBounds(c, 31, 53).empty());
    MathEditor m = typed({K::Digit5});
    Viewport v2;
    decode(renderCalcPrompt("2*x+1", math, "x=", m, math, false, v2));
    check("CALC math caret is in the field band", lastRender().caret && lastRender().caretRect.y >= 31);
  }
}

void iconExpectations() {
  // Phase 5 section 10.2: complete calculator mapping. Menus clear every
  // calculation glyph; calculation views light exactly one angle.
  {
    IconView none;
    const auto s = desiredIcons(none);
    check("menu icons are all off", s == PanelIcons::State{});
  }
  {
    IconView entry;
    entry.calculation = true;
    const auto deg = desiredIcons(entry);
    check("entry defaults to DEG+MATHIO", deg.deg && !deg.rad && !deg.gra && deg.mathio);
    check("entry defaults clear FIX/SCI/E/Verify/history",
          !deg.fix && !deg.sci && !deg.e && !deg.check && !deg.arrUp && !deg.arrDown);
    check("entry never lights unused glyphs",
          !deg.cplxI && !deg.angle && !deg.scrollDown && !deg.arrLeft && !deg.arrRight && !deg.sun);
  }
  for (auto [angle, deg, rad, gra] :
       {std::tuple<Angle, bool, bool, bool>{Angle::Degree, true, false, false},
        {Angle::Radian, false, true, false}, {Angle::Gradian, false, false, true}}) {
    IconView v;
    v.calculation = true;
    v.angle = angle;
    const auto s = desiredIcons(v);
    check("angle exclusivity", s.deg == deg && s.rad == rad && s.gra == gra);
  }
  {
    IconView v;
    v.calculation = true;
    v.io = IO::LineLine;
    check("line input clears MATHIO", !desiredIcons(v).mathio);
    v.io = IO::MathDecimal;
    check("math-decimal keeps MATHIO", desiredIcons(v).mathio);
  }
  {
    IconView v;
    v.calculation = true;
    v.number = NumberFormat::Fix;
    v.engineer = true;
    v.verify = true;
    v.shifted = true;
    v.historyPrev = true;
    v.historyNext = true;
    const auto s = desiredIcons(v);
    check("FIX/E/Verify/SHIFT/history", s.fix && !s.sci && s.e && s.check && s.shift && s.arrUp && s.arrDown);
  }
  {
    IconView v;
    v.calculation = true;
    v.number = NumberFormat::Sci;
    const auto s = desiredIcons(v);
    check("SCI without FIX", s.sci && !s.fix);
  }
  {
    // Field: parent settings plus the field SHIFT latch; history never shows.
    IconView v;
    v.field = true;
    v.shifted = true;
    v.historyPrev = true;
    v.historyNext = true;
    const auto s = desiredIcons(v);
    check("field SHIFT on, history off", s.shift && !s.arrUp && !s.arrDown && s.deg && s.mathio);
  }
  {
    // Content/icon independence: the same expression text renders identical
    // pixels regardless of icon state; only staging differs.
    MathEditor e = typed({K::Digit1, K::Digit2, K::Digit3});
    Viewport v1, v2;
    Settings deg = math, rad = math;
    rad.angle = Angle::Radian;
    const Frame a = decode(renderCalculation(e, deg, nullptr, Format::Standard, false, false, v1));
    const Frame b = decode(renderCalculation(e, rad, nullptr, Format::Standard, false, false, v2));
    check("pixels ignore angle state (icons carry it)", a == b);
  }
}

void costExpectations() {
  // Deep single-child nesting stays linear: the old typesetter re-measured
  // every child at each level.
  std::string deep;
  for (int i = 0; i < 60; ++i) deep += "sqrt(";
  deep += "2";
  for (int i = 0; i < 60; ++i) deep += ")";
  EditDocument d; MathLayout l;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 100; ++i) {d.rebuild(deep); l.layout(d);}
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() / 100;
  check("60-deep nesting lays out in bounded time (" + std::to_string(us) + " us)", us < 5000);
  size_t radicals = 0;
  for (size_t i = 0; i < d.items(); ++i) radicals += d.item(i).kind == EditDocument::Kind::Sqrt;
  check("all 60 nested radicals parse as structures (" + std::to_string(radicals) + ")", radicals == 60);
  // A maximum-length expression.
  MathEditor e; e.begin(std::string(MathEditor::kLimit, '9')); e.press(K::Digit1);
  check("input limit holds", e.text().size() == MathEditor::kLimit && e.notice == "Input limit");
}

void lineModeExpectations() {
  Settings line = math; line.io = IO::LineLine;
  MathEditor e = typed({K::Digit9, K::Power, K::Digit3});
  const Frame f = render(e, nullptr, false, line);
  check("line mode still draws its linear text", !inkBounds(f, 0, 15).empty());
  // Phase 5: no bitmap D/R/G header. A busy empty entry shows the modal CW
  // spinner centered (24x24 at 84,19) with no caret, text, or notice band.
  MathEditor empty;
  empty.begin();
  const Frame h = render(empty, nullptr, true);
  check("entry reserves no bitmap status band", inkBounds(h, 0, 8).empty());
  const Rect spin = inkBounds(h, 0, 62);
  check("busy shows the centered spinner", spin.left == 84 && spin.top == 19 && spin.right == 107 && spin.bottom == 42);
  check("busy leaves the notice band blank", inkBounds(h, 54, 62).empty());
}

void p5EditorPolicyExpectations() {
  // P5 acceptance: one undo step per logical operation (E-13), Math INS with
  // following-operand wrap (E-11), SHIFT jumps across Mixed/Integral/Sum/
  // Product (E-12), open function calls (E-16), and the logical caret kind.
  // Every branch ends on a valid EditDocument stop with depth zero.
  using CaretKind = MathEditor::CaretKind;
  auto valid = [](const MathEditor& e) {
    EditDocument d; d.rebuild(e.text());
    return e.cursor() <= e.text().size() && d.valid(e.cursor()) &&
           e.text().size() <= MathEditor::kLimit && e.editDepth() == 0;
  };
  // 1. Direct digit edit undoes in one step.
  {MathEditor e = typed({K::Digit1, K::Digit2});
   check("digits type", e.text() == "12" && valid(e));
   e.undo();
   check("digit undo restores 1 (" + e.text() + ")", e.text() == "1" && e.cursor() == 1 && valid(e));}
  // 2. Direct template insertion undoes in one step.
  {MathEditor e; e.begin("5"); e.insertTemplate("sqrt()");
   check("direct template insert enters its field (" + e.text() + ")", e.text() == "5sqrt()" && e.cursor() == 6 && valid(e));
   e.undo();
   check("nested insertTemplate undoes whole (" + e.text() + ")", e.text() == "5" && e.cursor() == 1 && valid(e));
   e.undo();
   check("second undo redoes it", e.text() == "5sqrt()" && e.cursor() == 6 && valid(e));}
  // 3. Menu-path insertion (bypassing press) participates in the outer
  // transaction: CATALOG insert then TOOLS Undo removes the whole operation.
  {MathEditor e; e.begin("1+"); catalogInsert(e, "sqrt()");
   check("menu template insert enters its field (" + e.text() + ")", e.text() == "1+sqrt()" && e.cursor() == 7 && valid(e));
   e.undo();
   check("menu insert undoes whole in one step (" + e.text() + ")", e.text() == "1+" && e.cursor() == 2 && valid(e));}
  // 4. Menu-path ordinary insertion undoes too.
  {MathEditor e; e.begin(); e.insert("Ans");
   check("menu ordinary insert", e.text() == "Ans" && valid(e));
   e.undo();
   check("menu ordinary insert undoes", e.text().empty() && e.cursor() == 0 && valid(e));}
  // 5. Cursor-only moves publish no snapshot and keep the previous undo.
  {MathEditor e = typed({K::Digit1, K::Digit2}); e.press(K::PageUp); e.press(K::PageUp);
   check("moves keep prior undo", e.hasUndo() && valid(e));
   e.undo();
   check("undo after moves restores 1 (" + e.text() + ")", e.text() == "1" && valid(e));}
  // 6. Rejected insertion publishes nothing and keeps the previous undo.
  {MathEditor e = typed({K::Digit9, K::Power});
   e.insert(",");
   check("refused insert keeps undo (" + e.text() + ")", e.text() == "9^()" && !e.notice.empty() && e.hasUndo() && valid(e));
   e.undo();
   check("undo after refusal restores 9", e.text() == "9" && valid(e));}
  // 7. Over-limit insertion changes neither source nor undo.
  {MathEditor e; for (int i = 0; i < 256; ++i) e.press(K::Digit9);
   check("full input holds 256", e.text().size() == 256 && valid(e));
   e.insert("1");
   check("over-limit insert refused, undo kept", e.text().size() == 256 && !e.notice.empty() && e.hasUndo() && valid(e));
   e.undo();
   check("undo still restores the pre-full state", e.text().size() == 255 && valid(e));}
  // 8. P4 one-key structural DEL still undoes in one step.
  {MathEditor e = at("frac(1,2)", 9); e.press(K::Delete);
   check("E-09 DEL in one key (" + e.text() + ")", e.text() == "frac(1,)" && valid(e));
   e.undo();
   check("E-09 DEL undoes whole (" + e.text() + ")", e.text() == "frac(1,2)" && e.cursor() == 9 && valid(e));}
  // 9-11. Math SHIFT+DEL arms INS without touching source or overwrite.
  {MathEditor e; e.press(K::Shift); e.press(K::Delete);
   check("Math SHIFT+DEL arms INS", e.insArmed() && e.text().empty() && !e.overwrite && valid(e));
   check("armed caret kind is InsArmed", e.caretKind() == CaretKind::InsArmed);}
  // 12-13. C40: INS Root wraps the complete following operand (9^3, postfix
  // script included), landing where 2 extends it to 29^3.
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Right, K::PageUp, K::Shift, K::Delete});
   check("INS armed, source kept (" + e.text() + ")", e.insArmed() && e.text() == "9^(3)" && valid(e));
   e.press(K::Root);
   check("Root consumes INS around 9^3 (" + e.text() + ")", !e.insArmed() && e.text() == "sqrt(9^(3))" && e.cursor() == 5 && valid(e));
   e.press(K::Digit2);
   check("C40 gives sqrt(29^(3)) (" + e.text() + ")", e.text() == "sqrt(29^(3))" && valid(e));
   e.undo();
   check("digit undoes to the wrap (" + e.text() + ")", e.text() == "sqrt(9^(3))" && valid(e));
   e.undo();
   check("second undo redoes the digit (" + e.text() + ")", e.text() == "sqrt(29^(3))" && valid(e));}
  {MathEditor e = typed({K::Digit9, K::Power, K::Digit3, K::Right, K::PageUp, K::Shift, K::Delete});
   e.press(K::Root);
   check("INS wrap lands (" + e.text() + ")", e.text() == "sqrt(9^(3))" && e.cursor() == 5 && valid(e));
   e.undo();
   check("wrap undoes to 9^3 in one step (" + e.text() + ")", e.text() == "9^(3)" && e.cursor() == 0 && valid(e));}
  // 14. Manual 1+7/6 case: INS wraps the whole following fraction template.
  {MathEditor e = typed({K::Digit1, K::Add, K::Fraction, K::Digit7, K::Down, K::Digit6,
                         K::Left, K::Left, K::Shift, K::Delete, K::Root});
   check("INS wraps the following fraction (" + e.text() + ")", e.text() == "1+sqrt(frac(7,6))" && !e.insArmed() && valid(e));}
  // 15-16. A non-template key cancels INS; disarmIns covers menu opens.
  {MathEditor e; e.press(K::Shift); e.press(K::Delete); e.press(K::Digit2);
   check("digit cancels INS", !e.insArmed() && e.text() == "2" && e.caretKind() == CaretKind::Insert && valid(e));}
  {MathEditor e; e.press(K::Shift); e.press(K::Delete); e.disarmIns();
   check("menu-open disarm clears INS", !e.insArmed() && e.text().empty() && valid(e));}
  // Shifted Root is not INS-eligible (pending C100): it cancels arming and
  // inserts a plain nth-root template.
  {MathEditor e; e.press(K::Shift); e.press(K::Delete); e.press(K::Shift); e.press(K::Root);
   check("shifted Root cancels INS (" + e.text() + ")", !e.insArmed() && e.text() == "root(,)" && e.cursor() == 5 && valid(e));}
  // Base-N has no template keys: SHIFT+DEL keeps the legacy overwrite
  // toggle, Root is rejected, and INS never goes stale.
  {MathEditor e; e.baseNInput = true; e.press(K::Shift); e.press(K::Delete);
   check("Base-N SHIFT+DEL keeps the legacy toggle", e.overwrite && !e.insArmed() && valid(e));
   e.press(K::Root);
   check("Base-N Root leaves no stale INS", !e.insArmed() && e.text().empty() && valid(e));}
  // The hidden abs() template is not an ordinary call: menu insertion keeps
  // the closed template form.
  {MathEditor e; e.begin(); catalogInsert(e, "abs()");
   check("abs stays a template (" + e.text() + ")", e.text() == "abs()" && e.cursor() == 4 && valid(e));}
  // 17-18. Linear SHIFT+DEL still toggles overwrite; Math never does.
  {MathEditor e; e.setMathInput(false); e.press(K::Shift); e.press(K::Delete);
   check("Linear SHIFT+DEL toggles overwrite", e.overwrite && !e.insArmed() && valid(e));
   check("overwrite caret kind", e.caretKind() == CaretKind::Overwrite);}
  {MathEditor e; e.setMathInput(false); e.begin("1");
   check("begin keeps input-style policy", !e.mathInput() && valid(e));}
  // 19. SHIFT+Right inside an Integral lower limit jumps after it.
  {MathEditor e = at("integral(1,,)", 11); e.press(K::Shift); e.press(K::Right);
   check("SHIFT+Right leaves the integral (" + std::to_string(e.cursor()) + ")", e.cursor() == 13 && valid(e));}
  // 20. SHIFT+Left inside a Sum body jumps before it.
  {MathEditor e = at("sum(1,,)", 5); e.press(K::Shift); e.press(K::Left);
   check("SHIFT+Left enters before the sum", e.cursor() == 0 && e.text() == "sum(1,,)" && valid(e));}
  // 21. Nested qualifying templates choose the innermost.
  {MathEditor e = at("integral(sum(1,,),,)", 15); e.press(K::Shift); e.press(K::Right);
   check("SHIFT+Right stops after the inner sum (" + std::to_string(e.cursor()) + ")", e.cursor() == 17 && valid(e));}
  // 22. Outside the supported kinds the legacy start/end behavior stays.
  {MathEditor e = at("sqrt(1)", 6); e.press(K::Shift); e.press(K::Right);
   check("SHIFT+Right outside still goes to source end", e.cursor() == e.text().size() && valid(e));}
  // 23-24. Function keys insert open calls; Close adds the single ')'.
  {MathEditor e = typed({K::Sin});
   check("Sin inserts open sin( (" + e.text() + ")", e.text() == "sin(" && e.cursor() == 4 && valid(e));
   e.press(K::Digit3); e.press(K::Close);
   int closes = 0; for (char c : e.text()) closes += c == ')';
   check("Sin 3 Close gives one close (" + e.text() + ")", e.text() == "sin(3)" && closes == 1 && valid(e));}
  // 25. C38 DEL chain on the open representation needs no special case.
  {MathEditor e = typed({K::Sin, K::Digit3, K::Close, K::Delete});
   check("C38 DEL drops ) (" + e.text() + ")", e.text() == "sin(3" && valid(e));
   e.press(K::Delete);
   check("C38 DEL drops 3 (" + e.text() + ")", e.text() == "sin(" && valid(e));
   e.press(K::Delete);
   check("C38 DEL drops the open call", e.text().empty() && e.cursor() == 0 && valid(e));}
  // 26. Cos/Tan share the open-call mechanism.
  {MathEditor e = typed({K::Cos}); check("Cos inserts open", e.text() == "cos(" && valid(e));}
  {MathEditor e = typed({K::Tan}); check("Tan inserts open", e.text() == "tan(" && valid(e));}
  // 27. CATALOG-style ordinary insertion uses the open form at editor level.
  {MathEditor e; e.begin(); e.insertTemplate("tan(");
   check("open template insert lands in its field (" + e.text() + ")", e.text() == "tan(" && e.cursor() == 4 && valid(e));
   e.undo();
   check("open insert undoes whole", e.text().empty() && valid(e));}
  // 28. Leaving an open call with Right materializes ')' as one undo step.
  {MathEditor e = at("sin(3", 5); e.press(K::Right);
   check("Right closes the open call (" + e.text() + ")", e.text() == "sin(3)" && e.cursor() == 6 && valid(e));
   e.undo();
   check("materialized close undoes (" + e.text() + ")", e.text() == "sin(3" && valid(e));}
  // 29. Open-call key insertion undoes whole.
  {MathEditor e = typed({K::Sin});
   e.undo();
   check("Sin undoes whole", e.text().empty() && e.cursor() == 0 && valid(e));}
}
void p11HistoryRenderChecks() {
  // The summary view reproduces the Result view pixel-identically for short
  // answers: one shared path, no second history renderer.
  {
    MathEditor e;
    e.begin("1/3");
    const Result r = evaluated("1/3");
    const ResultSummary s = toSummary(r);
    Viewport v1, v2;
    int16_t scroll = 0;
    const Frame a =
        decode(renderResultSummary(e, math, &s, Format::Standard, false, false, v1, "", true, &scroll));
    const Frame b = decode(renderCalculation(e, math, &r, Format::Standard, false, false, v2));
    check("P11 summary view matches Result view (1/3)", a == b);
  }
  // Fitting answers never scroll.
  {
    const ResultSummary s = toSummary(evaluated("1/3"));
    check("P11 short answer scroll range is zero", summaryMaxScroll(s, math, Format::Standard) == 0);
  }
  // Long answer: Fix/9 digits of 123456789.5 (ink need 207 > 191).
  Settings fix;
  fix.number = NumberFormat::Fix;
  fix.digits = 9;
  const Result longResult = evaluate(Request{"123456789.5", fix, Memory{}, false, 1});
  const ResultSummary longSummary = toSummary(longResult);
  const int16_t max = summaryMaxScroll(longSummary, fix, Format::Standard);
  check("P11 long answer scroll range is positive", max > 0);
  MathEditor e;
  e.begin("123456789.5");
  // Arrow bands exclude scrolled content by construction (the clip reserves
  // them), so band ink inside the result region is the raster arrow itself.
  // The input expression above may carry its own scroll arrows; the result
  // region never overlaps it.
  auto bandInk = [](const Frame& f, int x0, int x1) {
    const auto region = lastRender().result;
    return !inkBounds(f, region.y, region.y + region.height - 1, x0, x1).empty();
  };
  {
    Viewport v;
    int16_t scroll = 0;
    const Frame start = decode(
        renderResultSummary(e, fix, &longSummary, Format::Standard, false, false, v, "", true, &scroll));
    check("P11 start shows the right raster arrow", bandInk(start, 183, 190));
  }
  {
    Viewport v;
    int16_t scroll = max;
    const Frame end = decode(
        renderResultSummary(e, fix, &longSummary, Format::Standard, false, false, v, "", true, &scroll));
    check("P11 end shows the left raster arrow", bandInk(end, 0, 9));
    check("P11 end keeps the scrolled range", scroll == max);
  }
  {
    // Offsets clamp into range instead of painting outside the viewport.
    Viewport v;
    int16_t scroll = 9999;
    const Frame clamped = decode(
        renderResultSummary(e, fix, &longSummary, Format::Standard, false, false, v, "", true, &scroll));
    Viewport w;
    int16_t exact = max;
    const Frame edge = decode(
        renderResultSummary(e, fix, &longSummary, Format::Standard, false, false, w, "", true, &exact));
    check("P11 scroll clamps at the end", scroll == max && clamped == edge);
    Viewport u;
    int16_t below = -5;
    const Frame origin = decode(
        renderResultSummary(e, fix, &longSummary, Format::Standard, false, false, u, "", true, &below));
    Viewport z;
    int16_t zero = 0;
    const Frame first = decode(
        renderResultSummary(e, fix, &longSummary, Format::Standard, false, false, z, "", true, &zero));
    check("P11 scroll clamps at the start", below == 0 && origin == first);
  }
  check("P11 source survives result scrolling", e.text() == "123456789.5");
  // Errors never scroll; Base-N keeps its summary view.
  {
    const Result bad = evaluate(Request{"1/0", math, Memory{}, false, 1});
    const ResultSummary summary = toSummary(bad);
    check("P11 error answers never scroll", summaryMaxScroll(summary, math, Format::Standard) == 0);
  }
  {
    const ResultSummary s = toSummary(evaluated("555"));
    MathEditor b;
    b.begin("555");
    Viewport v;
    decode(renderBaseN(b, 10, &s, false, v));
    check("P11 Base-N summary view keeps its value", lastRender().presented == "555");
  }
}
void p12PresentationExpectations() {
  using Presentation::Kind;
  using Presentation::toLinearText;
  using Presentation::toTemplateSource;
  auto hasKind = [](const Presentation::Output& o, Kind k) {
    for (size_t i = 1; i < o.nodes.size(); ++i)
      if (o.nodes[i].kind == k) return true;
    return false;
  };
  auto hasFormat = [](const std::vector<MenuItem>& rows, Format f) {
    for (const auto& r : rows)
      if (r.action == static_cast<int>(f)) return true;
    return false;
  };
  // R-04 typed fraction: 3/2 Standard is Fraction, Decimal is Digits/Point.
  {
    const Result r = evaluated("3/2");
    const ResultSummary s = toSummary(r);
    const auto stdPres = Presentation::presentSummary(s, math, Format::Standard);
    const auto decPres = Presentation::presentSummary(s, math, Format::Decimal);
    check("P12 3/2 Standard is typed Fraction", hasKind(stdPres, Kind::Fraction));
    check("P12 3/2 Decimal has Point and no Fraction", hasKind(decPres, Kind::Point) && !hasKind(decPres, Kind::Fraction));
    check("P12 3/2 template is frac(3,2)", toTemplateSource(stdPres, math) == "frac(3,2)");
    // Mixed view.
    const auto mixedPres = Presentation::presentSummary(s, math, Format::Mixed);
    check("P12 3/2 Mixed is typed Mixed", hasKind(mixedPres, Kind::Mixed));
  }
  // R-04 radical/pi: sqrt(2) is Radical, never Prime-capable.
  {
    const Result r = evaluated("sqrt(2)");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Standard);
    check("P12 sqrt(2) is typed Radical", hasKind(pres, Kind::Radical));
    const auto rows = formatRows(&r, true, math);
    check("P12 sqrt(2) FORMAT hides Prime Factor", !hasFormat(rows, Format::Prime));
    check("P12 sqrt(2) FORMAT hides Mixed", !hasFormat(rows, Format::Mixed));
  }
  // R-04 pi preserved.
  {
    const Result r = evaluated("pi");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Standard);
    check("P12 pi is typed Pi", hasKind(pres, Kind::Pi));
  }
  // e terms read exactly like pi terms (an exact integral's e-1, 4pi-8).
  {
    const auto pres = Presentation::presentSummary(toSummary(evaluated("e-1")), math, Format::Standard);
    check("e-1 is typed Euler", hasKind(pres, Kind::Euler));
    check("e-1 template is -1+e", toTemplateSource(pres, math) == "-1+e");
    const auto half = Presentation::presentSummary(toSummary(evaluated("3e/2")), math, Format::Standard);
    check("3e/2 template is frac(3e,2)", toTemplateSource(half, math) == "frac(3e,2)");
    const auto twoTerm = Presentation::presentSummary(toSummary(evaluated("4pi-8")), math, Format::Standard);
    check("4pi-8 template is -8+4pi", toTemplateSource(twoTerm, math) == "-8+4pi");
  }
  // A constant shared by every term is factored out (5sqrt(5)pi/12 has two
  // irrationals in one term, which the ordinary view cannot show).
  {
    auto standard = [&](const char* source) {
      return Presentation::presentSummary(toSummary(evaluated(source)), math, Format::Standard);
    };
    const auto integral = standard("pi*(5sqrt(5)-1)/12");
    check("pi(5sqrt(5)-1)/12 template", toTemplateSource(integral, math) == "frac(pi(5sqrt(5)-1),12)");
    check("pi(5sqrt(5)-1)/12 linear", toLinearText(integral, math) == "pi(5sqrt(5)-1)/12");
    check("e(1+sqrt(2)) template", toTemplateSource(standard("e*(sqrt(2)+1)"), math) == "e(1+sqrt(2))");
    check("negative factored keeps the sign outside",
          toTemplateSource(standard("-pi*(sqrt(2)+1)/3"), math) == "-frac(pi(1+sqrt(2)),3)");
    check("one factored term", toTemplateSource(standard("sqrt(5)*pi/12"), math) == "frac(sqrt(5)pi,12)");
    check("pi^2 stays decimal", !hasKind(standard("pi^2"), Kind::Pi));
    check("pi*e*sqrt(2) stays decimal", !hasKind(standard("pi*e*sqrt(2)"), Kind::Pi));
    const Result r = evaluated("pi*(5sqrt(5)-1)/12");
    const auto rows = formatRows(&r, true, math);
    check("factored FORMAT offers Decimal only",
          hasFormat(rows, Format::Decimal) && !hasFormat(rows, Format::Mixed) && !hasFormat(rows, Format::Prime) &&
              !hasFormat(rows, Format::Engineering));
    check("factored Decimal view is digits",
          !hasKind(Presentation::presentSummary(toSummary(r), math, Format::Decimal), Kind::Pi));
  }
  // R-04 scientific uses TimesTenPower, never legacy pow().
  // Fixture value from resultExpectations (2^40*1000 is scientific).
  {
    const Result r = evaluated("2^40*1000");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Standard);
    check("P12 scientific is TimesTenPower", hasKind(pres, Kind::TimesTenPower));
    const std::string tpl = toTemplateSource(pres, math);
    check("P12 scientific template has *10^(", tpl.find("*10^(") != std::string::npos);
    check("P12 scientific template has no pow(", tpl.find("pow(") == std::string::npos);
  }
  // R-04 recurring is typed where supported.
  {
    const Result r = evaluated("1/3");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Recurring);
    check("P12 1/3 Recurring is typed Recurring", hasKind(pres, Kind::Recurring));
    check("P12 1/3 Recurring template is 0.recur(3)", toTemplateSource(pres, math) == "0.recur(3)");
    check("P12 1/3 Recurring linear is 0.(3)", toLinearText(pres, math) == "0.(3)");
  }
  // R-04 prime powers stay structural (Superscript, never reparsed "^").
  {
    const Result r = evaluated("12");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Prime);
    check("P12 12 Prime template is 2^(2)*3", toTemplateSource(pres, math) == "2^(2)*3");
    check("P12 12 Prime linear is 2^2*3", toLinearText(pres, math) == "2^2*3");
  }
  // R-06 Norm1: 1/3 Decimal is 10 significant digits.
  {
    const Result r = evaluated("1/3");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Decimal);
    check("P12 C49 decimal linear is 0.3333333333", toLinearText(pres, math) == "0.3333333333");
    check("P12 C49 template decimal is 0.3333333333", toTemplateSource(pres, math) == "0.3333333333");
  }
  // R-02 separators follow decimal mark.
  {
    Result r = evaluated("rem(5,2)");
    const ResultSummary s = toSummary(r);
    Settings dot = math, comma = math;
    comma.comma = true;
    const auto dotPres = Presentation::presentSummary(s, dot, Format::Standard);
    const auto commaPres = Presentation::presentSummary(s, comma, Format::Standard);
    check("P12 dot pair uses comma", toLinearText(dotPres, dot).find(", ") != std::string::npos);
    check("P12 comma pair uses semicolon", toLinearText(commaPres, comma).find("; ") != std::string::npos);
    // Decimal mark itself.
    Value v(0);
    v.number = Decimal("1.25");
    const auto dotDec = Presentation::presentValue(v, dot, Format::Decimal);
    const auto commaDec = Presentation::presentValue(v, comma, Format::Decimal);
    check("P12 dot decimal is 1.25", toLinearText(dotDec, dot) == "1.25");
    check("P12 comma decimal is 1,25", toLinearText(commaDec, comma) == "1,25");
  }
  // R-03 sexagesimal distinct Degree/Minute/Second, never three degrees.
  {
    Value v(0);
    v.number = Decimal("12.5824388889");  // 12 + 34/60 + 56.78/3600 approx
    Settings s = math;
    const auto pres = Presentation::presentValue(v, s, Format::Sexagesimal);
    check("P12 sexagesimal has Degree", hasKind(pres, Kind::Degree));
    check("P12 sexagesimal has Minute", hasKind(pres, Kind::Minute));
    check("P12 sexagesimal has Second", hasKind(pres, Kind::Second));
    const std::string linear = toLinearText(pres, s);
    check("P12 sexagesimal linear has U+00B0", linear.find("\xC2\xB0") != std::string::npos);
    check("P12 sexagesimal linear has U+2032", linear.find("\xE2\x80\xB2") != std::string::npos);
    check("P12 sexagesimal linear has U+2033", linear.find("\xE2\x80\xB3") != std::string::npos);
    const std::string tpl = toTemplateSource(pres, s);
    size_t degrees = 0;
    for (char c : tpl)
      if (c == '\xB0') ++degrees;
    check("P12 sexagesimal template is not three degrees", degrees == 1);
  }
  // R-05 FORMAT target matrix (fx-880 availability, manual order).
  // Hard gate: 3/2 shows only Standard/Decimal/Improper/Mixed (terminating
  // fraction: no Recurring; exact family: no ENG/Sexagesimal/Prime).
  {
    const Result r = evaluated("3/2");
    const auto rows = formatRows(&r, true, math);
    std::vector<int> order;
    for (const auto& row : rows) order.push_back(row.action);
    const std::vector<int> expected = {static_cast<int>(Format::Standard), static_cast<int>(Format::Decimal),
                                       static_cast<int>(Format::Improper), static_cast<int>(Format::Mixed)};
    check("P12 3/2 FORMAT gate order", order == expected);
    check("P12 3/2 hides Recurring (terminates)", !hasFormat(rows, Format::Recurring));
    check("P12 3/2 hides ENG/Sexagesimal", !hasFormat(rows, Format::Engineering) && !hasFormat(rows, Format::Sexagesimal));
    check("P12 3/2 hides Prime", !hasFormat(rows, Format::Prime));
  }
  // Repeating fraction keeps Recurring: 1/3 offers Std/Dec/Recur/Improper/Mixed.
  {
    const Result r = evaluated("1/3");
    const auto rows = formatRows(&r, true, math);
    check("P12 1/3 offers Recurring", hasFormat(rows, Format::Recurring));
    check("P12 1/3 offers Improper+Mixed", hasFormat(rows, Format::Improper) && hasFormat(rows, Format::Mixed));
    check("P12 1/3 hides ENG/Sexagesimal", !hasFormat(rows, Format::Engineering) && !hasFormat(rows, Format::Sexagesimal));
  }
  // Irrational: Standard/Decimal only.
  {
    const Result r = evaluated("sqrt(2)");
    const auto rows = formatRows(&r, true, math);
    check("P12 sqrt(2) FORMAT is Standard/Decimal only",
          rows.size() == 2 && hasFormat(rows, Format::Standard) && hasFormat(rows, Format::Decimal));
  }
  // Non-exact decimal view owns ENG/Sexagesimal: 2^40*1000 is scientific.
  {
    const Result r = evaluated("2^40*1000");
    const auto rows = formatRows(&r, true, math);
    check("P12 decimal offers ENG", hasFormat(rows, Format::Engineering));
    check("P12 huge decimal withholds Sexagesimal", !hasFormat(rows, Format::Sexagesimal));
  }
  // Small non-exact decimal offers both ENG and Sexagesimal.
  {
    const Result r = evaluated("sin(1)");
    const auto rows = formatRows(&r, true, math);
    check("P12 sin(1) offers ENG+Sexagesimal",
          hasFormat(rows, Format::Engineering) && hasFormat(rows, Format::Sexagesimal));
    check("P12 sin(1) hides fraction rows",
          !hasFormat(rows, Format::Improper) && !hasFormat(rows, Format::Mixed) &&
              !hasFormat(rows, Format::Recurring) && !hasFormat(rows, Format::Prime));
  }
  // A polynomial integral is exact, so Standard already shows its fraction.
  {
    const Result r = evaluated("integral(9-4x,0,9/4)");
    check("P12 integral(9-4x,0,9/4) Standard is 81/8",
          toTemplateSource(Presentation::presentSummary(toSummary(r), math, Format::Standard), math) == "frac(81,8)");
  }
  // S<->D: an approximate numeric integral (1/x^2 is no polynomial) that reads
  // as a short fraction offers Improper/Mixed and converts; Standard stays decimal.
  {
    const Result r = evaluated("integral(1/x^2,0.5,4)");
    check("P12 integral 1.75 is approximate", r.error == Error::None && !r.value.isExact());
    const auto rows = formatRows(&r, true, math);
    check("P12 integral 1.75 offers Improper+Mixed",
          hasFormat(rows, Format::Improper) && hasFormat(rows, Format::Mixed));
    const ResultSummary s = toSummary(r);
    check("P12 integral 1.75 Improper is 7/4",
          toTemplateSource(Presentation::presentSummary(s, math, Format::Improper), math) == "frac(7,4)");
    check("P12 integral 1.75 Mixed is typed Mixed",
          hasKind(Presentation::presentSummary(s, math, Format::Mixed), Kind::Mixed));
    check("P12 integral 1.75 Standard stays decimal",
          !hasKind(Presentation::presentSummary(s, math, Format::Standard), Kind::Fraction));
  }
  // A sqrt term past ClassWiz's |c|,d < 100 still shows exactly while it fits
  // 10 chars: 10.125*3*sqrt(2) is 243*sqrt(2)/8, not 42.96...
  {
    const Result r = evaluated("10.125*3*sqrt(2)");
    check("P12 10.125*3*sqrt(2) is exact", r.error == Error::None && r.value.isExact());
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("P12 243sqrt(2)/8 is a radical fraction",
          hasKind(pres, Kind::Fraction) && hasKind(pres, Kind::Radical) && !hasKind(pres, Kind::Point));
  }
  // A radicand past 1000 still shows exactly while numerator and denominator
  // fit apart: 1000*sqrt(41*89*22)/(4*2760) is 25sqrt(80278)/276, not 25.66...
  {
    const Result r = evaluated("1000*sqrt(41*89*22)/(4*2760)");
    check("large radicand is exact", r.error == Error::None && r.value.isExact());
    const Exact e = r.value.exact();
    check("large radicand view is 25sqrt(80278)/276",
          e.valid && e.numerator == 25 && e.radicand == 80278 && e.denominator == 276);
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("large radicand is a radical fraction",
          hasKind(pres, Kind::Fraction) && hasKind(pres, Kind::Radical) && !hasKind(pres, Kind::Point));
    check("large radicand template",
          toTemplateSource(pres, math) == "frac(25sqrt(80278),276)");
    const auto rows = formatRows(&r, true, math);
    check("large radicand offers Decimal", hasFormat(rows, Format::Decimal));
    const auto decPres = Presentation::presentSummary(toSummary(r), math, Format::Decimal);
    check("large radicand Decimal view has point", hasKind(decPres, Kind::Point));
  }
  // Boundary cases: radicand < 1E6, numerator chars <= 10, denominator digits <= 6
  {
    const Result r = evaluated("sqrt(1001)");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("sqrt(1001) is radical, not point",
          hasKind(pres, Kind::Radical) && !hasKind(pres, Kind::Point));
  }
  {
    const Result r = evaluated("sqrt(999997)");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("sqrt(999997) 6-digit radicand is radical",
          hasKind(pres, Kind::Radical) && !hasKind(pres, Kind::Point));
  }
  {
    const Result r = evaluated("sqrt(1000003)");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("sqrt(1000003) 7-digit radicand stays decimal",
          hasKind(pres, Kind::Point) && !hasKind(pres, Kind::Radical));
  }
  {
    const Result r = evaluated("12345*sqrt(999997)");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("12345*sqrt(999997) numerator > 10 chars stays decimal",
          hasKind(pres, Kind::Point) && !hasKind(pres, Kind::Radical));
  }
  {
    const Result r = evaluated("sqrt(2)/1000000");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("sqrt(2)/1000000 denominator > 6 digits stays decimal",
          hasKind(pres, Kind::Point) && !hasKind(pres, Kind::Radical));
  }
  {
    const Result r = evaluated("1+sqrt(80278)");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("1+sqrt(80278) fits within 20 chars, shows radical",
          hasKind(pres, Kind::Radical) && !hasKind(pres, Kind::Point));
    const auto rows = formatRows(&r, true, math);
    check("1+sqrt(80278) offers Decimal", hasFormat(rows, Format::Decimal));
  }
  {
    const Result r = evaluated("12345*sqrt(80278)+12345*sqrt(80279)");
    const auto pres = Presentation::presentSummary(toSummary(r), math, Format::Standard);
    check("two terms > 20 chars total width stay decimal",
          hasKind(pres, Kind::Point));
  }
  // R-05 integer offers Prime, not Mixed.
  {
    const Result r = evaluated("12");
    const auto rows = formatRows(&r, true, math);
    check("P12 12 offers Prime", hasFormat(rows, Format::Prime));
    check("P12 12 hides Mixed", !hasFormat(rows, Format::Mixed));
    check("P12 12 hides ENG/Sexagesimal", !hasFormat(rows, Format::Engineering) && !hasFormat(rows, Format::Sexagesimal));
  }
  // R-05 complex offers Rectangular/Polar only.
  {
    Result z = evaluated("1");
    z.complex = true;
    z.imag = Value(1);
    const auto rows = formatRows(&z, true, math);
    check("P12 complex FORMAT is Standard/Decimal/Rect/Polar", rows.size() == 4 && hasFormat(rows, Format::Rectangular) &&
                                                               hasFormat(rows, Format::Polar) && !hasFormat(rows, Format::Prime));
  }
  // R-05 no alternative never opens (error result offers Standard only).
  {
    Result bad;
    bad.error = Error::Syntax;
    const auto rows = formatRows(&bad, true, math);
    check("P12 error FORMAT is Standard only", rows.size() == 1);
    check("P12 empty FORMAT is Standard only", formatRows(nullptr, false, math).size() == 1);
  }
  // P12 history stores execution format.
  {
    const Result r = evaluated("1/3");
    const ResultSummary dec = toSummary(r, Format::Decimal);
    check("P12 history keeps Decimal execution format", dec.displayedFormat == Format::Decimal);
    check("P12 history Decimal linear is 0.3333333333", summaryText(dec, math, dec.displayedFormat) == "0.3333333333");
  }
  // P12 long-result scroll uses the same typed geometry for keys and paint.
  {
    const ResultSummary s = toSummary(evaluated("1/3"));
    check("P12 typed short scroll still zero", summaryMaxScroll(s, math, Format::Standard) == 0);
  }
  // E-15 Power branch preserved; Sci Notat unresolved (same safe insertion).
  {
    MathEditor e;
    e.begin();
    e.setScientificPower(true);
    e.press(K::Scientific);
    const std::string power = e.text();
    e.begin();
    e.setScientificPower(false);
    e.press(K::Scientific);
    check("P12 Scientific Power is *10^()", power == "*10^()");
    check("P12 Sci Notat keeps safe fallback (unresolved)", e.text() == "*10^()");
  }
  // Presentation sizes for the firmware budget (flat arena: nodes carry no heap).
  {
    check("P12 Node stays compact", sizeof(Presentation::Node) <= 64);
    const Result r = evaluated("1/2+sqrt(3)/2");
    const ResultSummary s = toSummary(r);
    const auto pres = Presentation::presentSummary(s, math, Format::Standard);
    check("P12 two-term exact stays bounded", pres.nodes.size() <= 16);
  }
}
#include "calculator_gray_test.h"
#include "calculator_menu_p9_test.h"
#include "calculator_error_p10_test.h"
#include "calculator_calc_p13_test.h"
#include "calculator_distribution_render_test.h"
#include "calculator_table_equation_render_test.h"
}  // namespace

void shortcutEditorChecks(const std::string& dir){
  using Kind=EditDocument::Kind;
  const std::vector<std::pair<std::string,Kind>> templates={{"integral(,,)",Kind::Integral},{"diff(,)",Kind::Diff},{"sum(,,)",Kind::Sum},{"product(,,)",Kind::Product}};
  for(const auto& t:templates)for(K key:{K::Sin,K::Cos,K::Tan,K::Open,K::Log,K::Root}){
    MathEditor e;e.begin();e.insertTemplate(t.first);e.press(key);
    EditDocument doc;doc.rebuild(e.text());
    check("nested function retains "+t.first,doc.item(doc.field(doc.root()).first).kind==t.second);
    check("nested function caret remains valid",doc.valid(e.cursor()));
    e.press(K::Digit2);e.press(K::Close);doc.rebuild(e.text());
    check("closing nested function retains outer template",doc.item(doc.field(doc.root()).first).kind==t.second);
  }
  for(const auto& group:catalogData())for(const auto& entry:group.entries)check("quick catalog fixed label fits",quickCatalogLabel(entry).size()<=8);
  MathEditor e;e.begin();e.press(K::Shift);e.press(K::Multiply);e.press(K::Subtract);e.press(K::Digit3);e.press(K::Right);
  check("Shift multiply inserts absolute",e.text()=="abs(-3)");e.press(K::Shift);e.press(K::Divide);
  check("Shift divide inserts percent",e.text()=="abs(-3)%");
  e.begin();e.insertTemplate("integral(,,)");e.press(K::Sin);e.press(K::Open);e.press(K::X);
  EditDocument d;d.rebuild(e.text());check("nested open group preserves integral",d.item(d.field(d.root()).first).kind==Kind::Integral);
  Viewport view;Settings settings;
  writePbm(dir+"/shortcut-integral.pbm",decode(renderCalculation(e,settings,nullptr,Format::Standard,false,false,view,"",true)));
  writePbm(dir+"/shortcut-quick.pbm",decode(renderQuickCatalog(quickCatalogEntries(false,AppId::Calculate),0)));
  writePbm(dir+"/shortcut-get-started.pbm",decode(renderGetStarted()));
  {
    MathEditor ed;
    ed.begin();
    const Result r = evaluated("1000*sqrt(41*89*22)/(4*2760)");
    Viewport v;
    writePbm(dir + "/result-large-radicand.pbm", decode(renderCalculation(ed, math, &r, Format::Standard, false, false, v)));
  }
  {
    MathEditor ed;
    ed.begin();
    const Result r = evaluated("1+sqrt(80278)");
    Viewport v;
    writePbm(dir + "/result-large-radicand-two-term.pbm", decode(renderCalculation(ed, math, &r, Format::Standard, false, false, v)));
  }
}

int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[1]) != "capture" && std::string(argv[1]) != "check")) {
    std::cerr << "usage: render.exe capture|check <dir>\n"; return 2;
  }
  const std::string mode = argv[1], dir = argv[2];
  if (mode == "capture") {
    for (const auto& f : fixtures()) {
      const std::string path = dir + "/" + f.name + ".pbm";
      if (exists(path)) {std::cout << "kept  " << path << "\n"; continue;}
      writePbm(path, f.frame()); std::cout << "wrote " << path << "\n";
    }
    return 0;
  }
  for (const auto& f : fixtures()) writePbm(dir + "/" + f.name + ".pbm", f.frame());
  shortcutEditorChecks(dir);
  wrapExpectations();
  postfixPowerExpectations();
  shiftParenExpectations();
  navigationExpectations();
  p3TraversalExpectations();
  deletionExpectations();
  p4StructuralDeletionExpectations();
  p5EditorPolicyExpectations();
  caretExpectations();
  p6ParenExpectations();
  p7GeometryExpectations();
  p8GrayExpectations();
  p9MenuExpectations();
  p10ErrorRenderChecks(dir);
  p11HistoryRenderChecks();
  slotInvariant();
  viewportExpectations();
  casioParityExpectations();
  resultExpectations();
  p12PresentationExpectations();
  p13LinearPresenterChecks();
  p13CalcSplitRenderChecks();
  distributionRenderChecks(dir);
  tableEquationRenderChecks(dir);
  menuExpectations();
  variableGridExpectations();
  iconExpectations();
  costExpectations();
  lineModeExpectations();
  std::cout << "calculator render: " << fixtures().size() << " fixtures rendered, " << checks << " checks, " << knownFailures
            << " known failures, " << failures << " unexpected\n";
  return failures ? 1 : 0;
}
