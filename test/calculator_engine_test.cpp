#include "calculator/math/calculator_complex.h"
#include "calculator/math/calculator_aggregate.h"
#include "calculator/math/calculator_basen.h"
#include "calculator/math/calculator_polynomial.h"
#include "calculator/math/calculator_engine.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace Calculator;
Result calc(const std::string& expression) {
  Request q;q.expression=expression;return evaluate(q);
}
void equals(const std::string& expression,const char* expected,const char* tolerance="1E-9") {
  auto r=calc(expression);
  if(r.error!=Error::None || (r.value.number-Decimal(expected)).abs()>Decimal(tolerance)) {
    std::cerr<<expression<<" -> "<<r.value.number.str()<<" "<<errorText(r.error)<<" expected "<<expected<<"\n";std::abort();
  }
}
Result complexCalc(const std::string& expression,bool verify=false,Settings settings=Settings{}) {
  Request q;q.expression=expression;q.verify=verify;q.settings=settings;q.memory.preAns=Value(7);return evaluateComplex(q);
}
void expectText(const std::string& expression,const std::string& expected,Settings settings=Settings{},Format format=Format::Standard) {
  const Result r=complexCalc(expression,false,settings);
  const std::string got=r.error!=Error::None?errorText(r.error):formatComplex(r.value,r.imag,settings,format);
  if(got!=expected){std::cerr<<expression<<" -> "<<got<<" expected "<<expected<<"\n";std::abort();}
}
void complexExamples() {
  // Manual pp.131-134.
  expectText("(1+i)^4+(1-i)^2","-4-2i");
  expectText("2@45","sqrt(2)+sqrt(2)i");
  expectText("sqrt(2)+sqrt(2)i","2@45",Settings{},Format::Polar);
  expectText("conjg(2+3i)","2-3i");
  expectText("abs(1+i)","sqrt(2)");
  expectText("arg(1+i)","45");
  expectText("rep(2+3i)","2");
  expectText("imp(2+3i)","3");
  Result r=complexCalc("i^2=-1",true);assert(r.error==Error::None&&r.verified&&r.truth&&r.memory.ans.re.number==1);
  // Every quadrant and both axes; the principal argument is -180 < theta <= 180.
  expectText("arg(-1+i)","135");expectText("arg(-1-i)","-135");expectText("arg(1-i)","-45");
  expectText("arg(-1)","180");expectText("arg(-i)","-90");expectText("arg(sqrt(3)+i)","30");
  assert(complexCalc("arg(0)").error==Error::Math);
  Settings radians;radians.angle=Angle::Radian;
  expectText("arg(i)","pi/2",radians);
  Settings polar;polar.complexResult=ComplexResult::Polar;
  expectText("1+i","sqrt(2)@45",polar);
  expectText("1+i","1+i",polar,Format::Rectangular);
  expectText("3","3",polar);
  expectText("1/(2i)","-1/2i");
  expectText("i^-1","-i");
  expectText("2i*3i","-6");
  expectText("sin(30)+i","1/2+i");  // real subtrees keep Calculate's exact rules
  // A zero imaginary part is real; PreAns is never touched.
  r=complexCalc("i*i");assert(!r.complex&&!r.memory.ans.complex&&r.memory.ans.re.number==-1&&r.memory.preAns.number==7);
  r=complexCalc("1+2i");assert(r.complex&&r.memory.ans.complex&&r.memory.ans.im.number==2&&r.memory.preAns.number==7);
  // No complex trig, roots or ordering; exponent and division bounds.
  assert(complexCalc("sin(i)").error==Error::Math);
  assert(complexCalc("i^0.5").error==Error::Math);
  assert(complexCalc("i<2",true).error==Error::Math);
  assert(complexCalc("(1+i)^10000000000").error==Error::Math);
  assert(complexCalc("1/0i").error==Error::Math);
  // A complex variable: harmless in Calculate until referenced, then Math ERROR there.
  Request q;q.memory.variables[0]=ScalarValue::of(Value(1),Value(2));
  q.expression="B+1";assert(evaluate(q).error==Error::None);
  q.expression="3+A";r=evaluate(q);assert(r.error==Error::Math&&r.position==2);
  q.expression="A*i";r=evaluateComplex(q);assert(r.error==Error::None&&r.value.number==-2&&r.imag.number==1);
  q.memory.f="x^2";q.expression="f(i)";r=evaluateComplex(q);assert(r.error==Error::None&&!r.complex&&r.value.number==-1);
  q.expression="(1+i)^9999999999";assert(evaluateComplex(q,[]{return true;}).error==Error::Cancelled);
}
// Base-N manual examples (pp.134-137) and the signed 32-bit boundaries.
Result baseN(const char* expression, uint8_t base, int32_t a = 0) {
  Request q;
  q.expression = expression;
  q.memory.variables[0] = Value(a);
  return evaluateBaseN(q, base);
}
int32_t wordOf(const Result& r) {
  int64_t n = 0;
  assert(r.error == Error::None && r.value.number.toInt(n));
  return static_cast<int32_t>(n);
}
void baseNExamples() {
  assert(formatBaseN(wordOf(baseN("11+1", 2)), 2) == "00000000000000000000000000000100");
  assert(formatBaseN(wordOf(baseN("1F+1", 16)), 16) == "00000020");
  assert(wordOf(baseN("$10+#10+&10+|10", 10)) == 36);
  assert(wordOf(baseN("15*37", 10)) == 555 && formatBaseN(555, 16) == "0000022B");
  assert(formatBaseN(555, 8) == "00000001053");
  assert(formatBaseN(wordOf(baseN("1010 and 1100", 2)), 2).substr(28) == "1000");
  assert(formatBaseN(wordOf(baseN("not(1010)", 2)), 2) == "11111111111111111111111111110101");
  assert(wordOf(baseN("5 or 2 and 3", 10)) == 7);  // and binds tighter
  assert(wordOf(baseN("#FFFFFFFF", 10)) == -1 && wordOf(baseN("-2147483648", 10)) == -2147483647 - 1);
  assert(baseN("2147483647+1", 10).error == Error::Math);
  assert(baseN("neg(#80000000)", 10).error == Error::Math);
  assert(baseN("2/0", 10).error == Error::Math && wordOf(baseN("-7/2", 10)) == -3);
  const Result bad = baseN("12", 2);
  assert(bad.error == Error::Syntax && bad.position == 1);
  assert(baseN("A+1", 10).error == Error::Syntax);          // A is a digit, not the variable
  assert(wordOf(baseN("var(A)+A", 16, 5)) == 15);           // variable A plus hex digit A
  assert(baseN("var(A)", 10).error == Error::None);
  Request q;
  q.expression = "var(A)";
  q.memory.variables[0] = Value(Decimal("2.5"));
  assert(evaluateBaseN(q, 10).error == Error::Math);
  // Unary signs must share the parser's 32-level stack bound.
  q.expression = std::string(32, '+') + "1";
  assert(wordOf(evaluateBaseN(q, 10)) == 1);
  q.expression = std::string(33, '+') + "1";
  const Result deep = evaluateBaseN(q, 10);
  assert(deep.error == Error::Stack && deep.position == 33);
  q.expression = std::string(10000, '-') + "1";
  assert(evaluateBaseN(q, 10).error == Error::Stack);
  q.expression = "----1";
  unsigned polls = 0;
  assert(evaluateBaseN(q, 10, [&] { return ++polls == 3; }).error == Error::Cancelled);
  assert(wordOf(baseN("not( 1 )", 10)) == -2);
  assert(wordOf(baseN("neg( 7 )", 10)) == -7);
  assert(wordOf(baseN("not( neg( 1 ) )", 10)) == 0);
  assert(wordOf(baseN("not( 0", 10)) == -1);  // implicit closing parenthesis
  assert(baseN("neg( )", 10).error == Error::Syntax);
}

// Matrix and Vector manual examples (pp.137-150).
Aggregate grid(int rows, int cols, std::initializer_list<int> values) {
  Aggregate a;
  a.rows = static_cast<uint8_t>(rows);
  a.cols = static_cast<uint8_t>(cols);
  int i = 0;
  for (int v : values) { a.at(i / cols, i % cols) = Decimal(v); ++i; }
  return a;
}
bool same(const Result& r, int rows, int cols, std::initializer_list<const char*> values) {
  if (r.error != Error::None || !r.aggregate || r.aggregate->rows != rows || r.aggregate->cols != cols) return false;
  int i = 0;
  for (const char* v : values) {
    if ((r.aggregate->at(i / cols, i % cols) - Decimal(v)).abs() > Decimal("1E-9")) return false;
    ++i;
  }
  return true;
}
void aggregateExamples() {
  Aggregates store;
  store.matrices[0] = grid(2, 2, {2, 1, 1, 1});
  store.matrices[1] = grid(2, 2, {2, -1, -1, 2});
  store.matrices[2] = grid(2, 3, {1, 0, -1, 0, -1, 1});
  store.matrices[3] = grid(3, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9});
  auto m = [&](const char* e) { Request q; q.expression = e; return evaluateAggregate(q, store, false); };
  assert(same(m("MatA*MatB"), 2, 2, {"3", "0", "1", "1"}));
  store.matrices[1] = grid(2, 2, {2, 3, 2, 1});
  assert(same(m("MatA+MatB"), 2, 2, {"4", "4", "3", "2"}));
  assert(same(m("pow(MatA,2)"), 2, 2, {"5", "3", "3", "2"}));
  assert(same(m("pow(MatA,3)"), 2, 2, {"13", "8", "8", "5"}));
  assert(same(m("pow(MatA,-1)"), 2, 2, {"1", "-1", "-1", "2"}));
  Result det = m("det(MatA)");
  assert(det.error == Error::None && !det.aggregate && det.value.number == 1 && det.memory.ans.re.number == 1);
  assert(same(m("trn(MatC)"), 3, 2, {"1", "0", "0", "-1", "-1", "1"}));
  assert(same(m("abs(MatC)"), 2, 3, {"1", "0", "1", "0", "1", "1"}));
  assert(same(m("identity(2)+MatA"), 2, 2, {"3", "1", "1", "2"}));
  assert(same(m("2MatA"), 2, 2, {"4", "2", "2", "2"}));
  // Engineering symbols scale a matrix operand like a scalar factor.
  assert(same(m("MatA\x15"), 2, 2, {"2000", "1000", "1000", "1000"}));
  assert(m("det(MatC)").error == Error::Dimension && m("MatA+MatC").error == Error::Dimension);
  assert(m("pow(MatD,-1)").error == Error::Math && m("MatAns").error == Error::NotDefined);
  assert(m("MatA+1").error == Error::Math);

  // Aggregate operations can return ordinary scalars. Their surrounding
  // operators/functions must retain Calculate's exact arithmetic and domains.
  store.matrices[0] = grid(2, 2, {2, 0, 0, 2});
  assert(m("det(MatA)^2").error == Error::None && m("det(MatA)^2").value.number == 16);
  assert(m("pow(det(MatA),3)").value.number == 64);
  assert(m("det(MatA)!").value.number == 24);
  assert(m("det(MatA)%").value.symbolic == ExactNumber::fraction(1, 25));
  assert(m("root(3,det(MatA)+4)").value.number == 2);
  assert(m("sqrt(det(MatA))").value.number == 2);
  const Result radical = m("abs(-sqrt(det(MatA)+1))");
  assert(radical.error == Error::None && radical.value.symbolic == ExactNumber::integer(5).root(2));
  assert(m("sqrt(-det(MatA))").error == Error::Math);
  assert(m("1/(det(MatA)-4)").error == Error::Math);
  assert(m("sin(MatA)").error == Error::Math);
  Request nested;
  nested.expression = "f(det(MatA))";
  nested.memory.f = "x+A+Ans";
  nested.memory.variables[0] = Value(7);
  nested.memory.ans = Value(9);
  const Result function = evaluateAggregate(nested, store, false);
  assert(function.error == Error::None && function.value.number == 20);
  assert(function.memory.variables[0].re.number == 7);

  store.vectors[0] = grid(2, 1, {1, 2});
  store.vectors[1] = grid(2, 1, {3, 4});
  store.vectors[2] = grid(3, 1, {2, -1, 2});
  auto v = [&](const char* e) { Request q; q.expression = e; return evaluateAggregate(q, store, true); };
  assert(same(v("VctA+VctB"), 2, 1, {"4", "6"}));
  assert(v("VctA'VctB").value.number == 11);
  assert(same(v("VctA*VctB"), 3, 1, {"0", "0", "-2"}));  // 2D cross is 3D
  assert((v("angle(VctA,VctB)").value.number - Decimal("10.30484646876602")).abs() < Decimal("1E-12"));
  assert(same(v("unitv(VctB)"), 2, 1, {"0.6", "0.8"}));
  assert(v("abs(VctC)").value.number == 3);
  assert(v("(VctA'VctB)^2").value.number == 121);
  assert(v("sqrt(VctA'VctB)").value.symbolic == ExactNumber::integer(11).root(2));
  assert(v("VctA+VctC").error == Error::Dimension);
}

// Equation and Inequality (pp.119-130).
std::vector<Solution> solved(EquationKind kind, int size, std::initializer_list<const char*> c,
                             Relation relation = Relation::Greater, Error expected = Error::None) {
  EquationTask task;
  task.kind = kind;
  task.size = static_cast<uint8_t>(size);
  task.relation = relation;
  size_t i = 0;
  for (const char* v : c) task.c[i++] = calc(v).value;
  const Result r = solveEquation(task, Request{});
  assert(r.error == expected);
  return r.solutions ? *r.solutions : std::vector<Solution>{};
}
std::string text(const ScalarValue& v) { return v.complex ? formatComplex(v.re, v.im, Settings{}, Format::Standard, nullptr) : formatValue(v.re, Settings{}); }
void equationExamples() {
  auto s = solved(EquationKind::Simul, 3, {"1", "-1", "1", "2", "1", "1", "-1", "0", "-1", "1", "1", "4"});
  assert(s.size() == 3 && text(s[0].value) == "1" && text(s[1].value) == "2" && text(s[2].value) == "3" && s[2].label == "z");
  s = solved(EquationKind::Simul, 2, {"1", "3", "1", "2", "-4", "1"});  // x+3y=1, 2x-4y=1
  assert(text(s[0].value) == "7/10" && text(s[1].value) == "1/10");
  assert(solved(EquationKind::Simul, 2, {"1", "1", "1", "2", "2", "2"})[0].text == "Infinite Solution");
  assert(solved(EquationKind::Simul, 2, {"1", "1", "1", "2", "2", "3"})[0].text == "No Solution");

  s = solved(EquationKind::Polynomial, 2, {"1", "2", "-2"});
  assert(s.size() == 4 && text(s[0].value) == "-1+sqrt(3)" && text(s[1].value) == "-1-sqrt(3)");
  assert(s[2].label == "x-Min" && text(s[2].value) == "-1" && text(s[3].value) == "-3");
  s = solved(EquationKind::Polynomial, 2, {"2", "3", "4"});
  assert(s[0].value.complex && text(s[0].value) == "-3/4+sqrt(23)/4i");
  s = solved(EquationKind::Polynomial, 3, {"1", "-6", "11", "-6"});
  assert(s.size() == 7 && text(s[0].value) == "3" && text(s[1].value) == "2" && text(s[2].value) == "1");
  assert(s[3].label == "x-Max" && text(s[3].value) == "2-sqrt(3)/3");
  s = solved(EquationKind::Polynomial, 4, {"1", "0", "-5", "0", "4"});
  assert(s.size() == 4 && text(s[0].value) == "2" && text(s[3].value) == "-2");
  s = solved(EquationKind::Polynomial, 4, {"1", "-3", "3", "-3", "2"});  // (x^2+1)(x-1)(x-2)
  assert(s.size() == 4 && text(s[0].value) == "2" && text(s[1].value) == "1" && s[2].value.complex);
  s = solved(EquationKind::Polynomial, 3, {"1", "-2", "1", "0"});  // x(x-1)^2: repeated root listed once
  assert(s.size() == 2 + 4 && text(s[0].value) == "1" && text(s[1].value) == "0");
  solved(EquationKind::Polynomial, 2, {"0", "1", "1"}, Relation::Greater, Error::Math);

  auto set = [](const std::vector<Solution>& p) {
    std::string out;
    for (char ch : p.at(0).text) if (ch != '\x01' && ch != '\x02') out += ch;
    return out;
  };
  assert(set(solved(EquationKind::Inequality, 2, {"1", "2", "-3"}, Relation::GreaterEqual)) == "x<=-3, 1<=x");
  assert(set(solved(EquationKind::Inequality, 2, {"1", "2", "-3"}, Relation::Less)) == "-3<x<1");
  assert(set(solved(EquationKind::Inequality, 2, {"1", "0", "0"}, Relation::Less)) == "No Solution");
  assert(set(solved(EquationKind::Inequality, 2, {"1", "0", "0"}, Relation::GreaterEqual)) == "All Real Numbers");
  assert(set(solved(EquationKind::Inequality, 2, {"1", "-2", "1"}, Relation::Greater)) == "x<1, 1<x");
  assert(set(solved(EquationKind::Inequality, 2, {"1", "-2", "1"}, Relation::LessEqual)) == "x=1");

  EquationTask task;
  task.kind = EquationKind::Solver;
  task.seed = "1";
  Request q;
  q.expression = "x^2-B/2=0";
  q.memory.variables[1] = Value(4);
  Result r = solveEquation(task, q);
  // A parameter (B) keeps the CAS from naming the roots, so both come from
  // the numeric search: x1, x2.
  assert(r.error == Error::None && r.solutions->size() == 2 && r.solutions->at(0).label == "x1" &&
         r.solutions->at(1).label == "x2");
  assert((r.solutions->at(0).value.re.number + Decimal(2).sqrt()).abs() < Decimal("1E-20"));
  assert((r.solutions->at(1).value.re.number - Decimal(2).sqrt()).abs() < Decimal("1E-20"));
  assert((r.memory.variables[6].re.number - Decimal(2).sqrt()).abs() < Decimal("1E-20"));  // nearest the seed
  task.seed = "-1";
  r = solveEquation(task, q);
  assert(r.memory.variables[6].re.number.negative());
  q.expression = "2+2=4";
  assert(solveEquation(task, q).error == Error::Variable);
  q.expression = "x^2+1=0";
  assert(solveEquation(task, q).error == Error::CannotSolve);
  task.seed = "1E-20";
  q.expression = "x^2+1E-50=0";
  assert(solveEquation(task, q).error == Error::CannotSolve);
  q.expression = "x^2-1E-50=0";
  r = solveEquation(task, q);
  assert(r.error == Error::None);
  assert((r.memory.variables[6].re.number / Decimal("1E-25") - 1).abs() < Decimal("1E-20"));
  task.seed = "-1E-20";
  r = solveEquation(task, q);
  assert(r.error == Error::None);
  assert((r.memory.variables[6].re.number / Decimal("-1E-25") - 1).abs() < Decimal("1E-20"));
  task.seed = "1";
  for (const char* expression : {"x=0", "x^2=0", "x^3=0", "x^4=0"}) {
    q.expression = expression;
    r = solveEquation(task, q);
    assert(r.error == Error::None && r.memory.variables[6].re.number.abs() < Decimal("1E-24"));
  }
  q.expression = "(x-2)^2=0";
  r = solveEquation(task, q);
  assert(r.error == Error::None && (r.memory.variables[6].re.number - 2).abs() < Decimal("1E-18"));
  q.expression = "x^2-2=0";
  assert(solveEquation(task, q, [] { return true; }).error == Error::Cancelled);
  for (const char* seed : {"1", "-1"}) {
    task.seed = seed;
    r = solveEquation(task, q);
    assert(r.error == Error::None && r.memory.variables[6].re.isExact());
    assert(r.memory.variables[6].re.symbolic.str() == (seed[0]=='-' ? "-sqrt(2)" : "sqrt(2)"));
  }
  q.expression = "x^2-B/2=0";
  r = solveEquation(task, q);
  assert(r.error == Error::None && r.memory.variables[6].re.number.negative());

  // Every root, always as numbers: exact where the CAS names them ...
  task.seed = "0";
  q.expression = "x^2-5x+6=0";
  r = solveEquation(task, q);
  assert(r.error == Error::None && r.solutions->size() == 2 && text(r.solutions->at(0).value) == "2" &&
         text(r.solutions->at(1).value) == "3");
  // ... and numeric where solve() would stay unevaluated (a cubic with no
  // rational root, a transcendental equation).
  q.expression = "x^3-3x+1=0";
  task.seed = "1";
  r = solveEquation(task, q);
  assert(r.error == Error::None && r.solutions->size() == 3 && r.solutions->at(2).label == "x3");
  assert((r.solutions->at(0).value.re.number + Decimal("1.879385242")).abs() < Decimal("1E-8"));
  assert((r.solutions->at(1).value.re.number - Decimal("0.3472963553")).abs() < Decimal("1E-8"));
  assert((r.solutions->at(2).value.re.number - Decimal("1.532088886")).abs() < Decimal("1E-8"));
  assert((r.memory.variables[6].re.number - Decimal("1.532088886")).abs() < Decimal("1E-8"));
  q.expression = "x=cos(x)";
  q.settings.angle = Angle::Radian;
  task.seed = "0";
  r = solveEquation(task, q);
  assert(r.error == Error::None && r.solutions->size() == 1 && r.solutions->at(0).label == "x");
  assert((r.solutions->at(0).value.re.number - Decimal("0.7390851332")).abs() < Decimal("1E-8"));
}

// Exact arithmetic through evaluate(): `canonical` is the exact form kept in
// Ans, `shown` the ClassWiz text (fraction/radical/pi form, else decimal).
Result exactCalc(const std::string& expression,Settings settings=Settings{},const Memory& memory=Memory{}) {
  Request q;q.expression=expression;q.settings=settings;q.memory=memory;return evaluate(q);
}
void exactIs(const std::string& expression,const std::string& canonical,const std::string& shown,Settings settings=Settings{}) {
  const Result r=exactCalc(expression,settings);
  const std::string got=r.value.symbolic.str(),text=r.error==Error::None?formatValue(r.value,settings):errorText(r.error);
  if(r.error!=Error::None||!r.value.isExact()||got!=canonical||text!=shown||!(r.memory.ans.re.symbolic==r.value.symbolic)) {
    std::cerr<<expression<<" -> ["<<got<<"] shown "<<text<<", expected ["<<canonical<<"] shown "<<shown<<"\n";std::abort();
  }
}
void exactExamples() {
  // The equals operation is exact; display stays in the ClassWiz forms.
  exactIs("1/3+1/7","10/21","10/21");
  exactIs("sqrt(8)+sqrt(18)","5*sqrt(2)","5*sqrt(2)");
  exactIs("-3/4-1/4","-1","-1");
  exactIs("(-1/2)^3","-1/8","-1/8");
  exactIs("0.1+0.2","3/10","3/10");
  exactIs("pi/2+pi/3","5/6*pi","5*pi/6");
  exactIs("2pi*3/pi","6","6");
  exactIs("(1+sqrt(2))^2","3+2*sqrt(2)","3+2*sqrt(2)");
  exactIs("1/(1+sqrt(2))","-1+sqrt(2)","-1+sqrt(2)");
  exactIs("sqrt(3+2sqrt(2))","1+sqrt(2)","1+sqrt(2)");
  exactIs("8^(1/3)","2","2");
  exactIs("root(3,-27)","-3","-3");
  exactIs("log(1000)","3","3");
  exactIs("log(2,8)","3","3");
  exactIs("ln(e^2)","2","2");
  exactIs("e^(ln(5))","5","5");
  exactIs("dms(1,30,0)","3/2","3/2");
  // Exact values outside the ClassWiz forms stay exact but display as decimals.
  exactIs("ln(3)","ln(3)","1.098612289");
  exactIs("root(3,2)","root(3,2)","1.25992105");
  exactIs("pi^2","pi^2","9.869604401");
  exactIs("sqrt(1+sqrt(2))","sqrt(1+sqrt(2))","1.553773974");
  exactIs("2^100","1267650600228229401496703205376","1.2676506E+30");
  // ... and that exactness survives Ans: e^Ans after ln(3) is exactly 3.
  Memory m;m.ans=exactCalc("ln(3)").value;
  Result r=exactCalc("e^Ans",Settings{},m);assert(r.value.symbolic.str()=="3"&&formatValue(r.value,Settings{})=="3");
  m.ans=exactCalc("root(3,2)").value;r=exactCalc("Ans^3",Settings{},m);assert(r.value.symbolic.str()=="2");
  // Beyond the 40-digit decimal: the exact difference survives cancellation.
  exactIs("(10^45+1)-10^45","1","1");
  exactIs("(sqrt(2)+10^30)-10^30","sqrt(2)","sqrt(2)");
  r=exactCalc("(sqrt(2)+10^30)-10^30");assert((r.value.number-Decimal(2).sqrt()).abs()<Decimal("1E-20"));  // 23-digit Ans; decimal alone is off by 4E-10
  // Zero is structural: an exact zero divisor is Math ERROR, not 1E+40.
  assert(exactCalc("1/(sqrt(2)*sqrt(2)-2)").error==Error::Math);
  assert(exactCalc("1/(pi-pi)").error==Error::Math);
  assert(exactCalc("1/(ln(6)-ln(2)-ln(3))").error==Error::Math);
  assert(exactCalc("1/0").error==Error::Math&&exactCalc("0^0").error==Error::Math);
  assert(exactCalc("sqrt(1-sqrt(3))").error==Error::Math&&exactCalc("ln(1-sqrt(2))").error==Error::Math);
  // Verify decides equality structurally and order by certified sign.
  Request v;v.verify=true;
  for(const char* e:{"sqrt(8)=2sqrt(2)","sqrt(2)^2=2","ln(8)=3ln(2)","1/3+1/7=10/21","(1+sqrt(5))/2>1.618"}) {
    v.expression=e;r=evaluate(v);if(!r.verified||!r.truth){std::cerr<<e<<" not verified\n";std::abort();}
  }
  for(const char* e:{"pi=355/113","pi>355/113","sqrt(2)+sqrt(3)>sqrt(10)"}) {
    v.expression=e;r=evaluate(v);if(!r.verified||r.truth){std::cerr<<e<<" wrongly verified\n";std::abort();}
  }
  // Exact trigonometry in every angle unit.
  Settings rad;rad.angle=Angle::Radian;Settings grad;grad.angle=Angle::Gradian;
  exactIs("sin(pi/6)","1/2","1/2",rad);
  exactIs("cos(5pi/4)","-1/2*sqrt(2)","-sqrt(2)/2",rad);
  exactIs("tan(60)","sqrt(3)","sqrt(3)");
  exactIs("tan(30)","1/3*sqrt(3)","sqrt(3)/3");
  exactIs("sin(50)","1/2*sqrt(2)","sqrt(2)/2",grad);
  exactIs("asin(1/2)","30","30");
  exactIs("asin(1/2)","1/6*pi","pi/6",rad);
  exactIs("acos(-sqrt(3)/2)","5/6*pi","5*pi/6",rad);
  exactIs("atan(-1)","-45","-45");
  exactIs("atan(sqrt(3))","200/3","200/3",grad);
  exactIs("deg(90)","1/2*pi","pi/2",rad);
  exactIs("rad(pi/3)","60","60");
  r=exactCalc("pol(1,1)");assert(r.value.symbolic.str()=="sqrt(2)"&&r.second.symbolic.str()=="45");
  r=exactCalc("pol(-1,sqrt(3))");assert(r.value.symbolic.str()=="2"&&r.second.symbolic.str()=="120");
  r=exactCalc("rec(2,60)");assert(r.value.symbolic.str()=="1"&&r.second.symbolic.str()=="sqrt(3)");
  // The approximate command: any precision, separate from the exact answer.
  assert(approximate(exactCalc("pi").value,30)=="3.14159265358979323846264338328");
  assert(approximate(exactCalc("1/7").value,12)=="0.142857142857");
  assert(approximate(exactCalc("sqrt(8)+sqrt(18)").value,20)=="7.071067811865475244");  // ...52440|08: trailing zero trimmed
  assert(approximate(exactCalc("sin(1)",rad).value,10)=="0.8414709848");  // an inexact value: its decimal
  Settings decimal;decimal.io=IO::MathDecimal;
  assert(formatValue(exactCalc("1/3+1/7").value,decimal)=="0.4761904762");
  // Numerical calculus is approximate evaluation, except a polynomial
  // integrand on exact bounds, which integrates exactly and stays exact
  // downstream: integral(9-4x,0,9/4)*3*sqrt(2) is 243*sqrt(2)/8.
  r=exactCalc("integral(x^2,0,3)");assert(r.error==Error::None&&r.value.isExact()&&r.value.symbolic.str()=="9");
  r=exactCalc("integral(9-4x,0,9/4)");assert(r.error==Error::None&&r.value.symbolic.str()=="81/8");
  r=exactCalc("integral(9-4x,0,9/4)*3*sqrt(2)");assert(r.error==Error::None&&r.value.symbolic.str()=="243/8*sqrt(2)");
  r=exactCalc("integral(x^8-x,-1,2)");assert(r.error==Error::None&&r.value.symbolic.str()=="111/2");
  // Past those, the CAS's definite integral (when it agrees with the numeric value).
  r=exactCalc("integral(x^9,0,1)");assert(r.error==Error::None&&r.value.symbolic.str()=="1/10");  // past degree 8
  r=exactCalc("integral(sin(180x),0,9)");assert(r.error==Error::None&&r.value.symbolic.str()=="2*pi^(-1)");  // zero samples
  r=exactCalc("integral(1/x,1,2)");assert(r.error==Error::None&&r.value.symbolic.str()=="ln(2)");
  r=exactCalc("integral(4-4abs(cos(x)),0,pi)",rad);assert(r.error==Error::None&&r.value.symbolic.str()=="-8+4*pi");
  r=exactCalc("integral(e^(-x^2),0,1)");assert(r.error==Error::None&&!r.value.isExact());  // no antiderivative: numeric
  // A cusp at 0 (real cube roots of negatives): the exact form is checked
  // against a rough value, not one settled to 1E-10; a tol looser than the
  // rough one keeps the 1E-8 agreement, which 0.75000006 misses.
  r=exactCalc("integral(2(x^(2/3)-x^(1/3))/3+x^(7/3),-1,1)");assert(r.error==Error::None&&r.value.symbolic.str()=="4/5");
  r=exactCalc("integral(x^(1/3),0,1)");assert(r.error==Error::None&&r.value.symbolic.str()=="3/4");
  r=exactCalc("integral(x^(1/3),0,1,1E-6)");assert(r.error==Error::None&&!r.value.isExact());
  r=exactCalc("integral(x^(1/3),1,2)");assert(r.error==Error::None&&r.value.symbolic.str()=="-3/4+3/2*root(3,2)"&&
                                               (r.value.number-Decimal("1.1398815748423097471508")).abs()<Decimal("1E-21"));
  r=exactCalc("integral(sin(x),0,pi)",rad);assert(r.error==Error::None&&(r.value.number-2).abs()<Decimal("1E-9"));
  r=exactCalc("diff(x^3,2)");assert(r.error==Error::None&&!r.value.isExact()&&(r.value.number-12).abs()<Decimal("1E-9"));
  // Summation stays exact: 1 + 1/4 + ... + 1/100.
  r=exactCalc("sum(1/x^2,1,10)");assert(r.value.symbolic.str()=="1968329/1270080");
}
int main() {
  // Signed 64-bit conversion has an asymmetric range; failures preserve out.
  int64_t converted = 17;
  assert(Decimal("-9223372036854775808").toInt(converted));
  assert(converted == std::numeric_limits<int64_t>::min());
  assert(Decimal("9223372036854775807").toInt(converted));
  assert(converted == std::numeric_limits<int64_t>::max());
  for (const char* invalid : {"-9223372036854775809", "9223372036854775808", "1.5", "NaN", "Infinity"}) {
    converted = 17;
    assert(!Decimal(invalid).toInt(converted) && converted == 17);
  }
  {
    Request q;q.settings.angle=Angle::Radian;
    for(const char* source:{"2sin(pi/4)","2sin(pi/4+2pi)","2cos(pi/4)"}){
      q.expression=source;const auto result=evaluate(q);
      assert(result.error==Error::None);
      assert(result.value.exact().valid&&result.value.exact().numerator==1&&
             result.value.exact().denominator==1&&result.value.exact().radicand==2);
    }
  }
  // P10: source byte positions, never lookahead-after-token or a UI cursor.
  for(const auto& probe:std::vector<std::pair<std::string,size_t>>{{"1**2",2},{"1+",2},{"sin(*2)",4},{"frac(1,*)",7}}){
    const auto parsed=parse(probe.first);const auto evaluated=calc(probe.first);
    assert(parsed.error==Error::Syntax && parsed.position==probe.second);
    assert(evaluated.error==Error::Syntax && evaluated.position==probe.second);
  }
  assert(calc("1/0").error==Error::Math && calc("1/0").position==1);
  equals("6/2(1+2)","1");equals("-2^2","-4");equals("(-2)^2","4");
  equals("1E20+1-1E20","1","0");equals("sin(30)","0.5");equals("cos(60)","0.5");
  equals("tan(45)","1");equals("asin(0.5)","30");equals("acos(0.5)","60");equals("atan(1)","45");
  equals("ln(e)","1");equals("log(2,8)","3");equals("(5+3)!","40320");
  equals("10P4","5040");equals("10C4","210");
  equals("4P3","24");equals("4C3","4");equals("4C3+4P3","28");equals("4C4+4P3","25");
  equals("2*4P3","48");equals("(1+3)P(1+2)","24");equals("5C0","1");equals("5P0","1");
  equals("gcd(28,35)","7");equals("lcm(9,15)","45");equals("int(-3.5)","-3");equals("floor(-3.5)","-4");
  equals("sum(x+1,1,5)","20");equals("product(x+1,1,5)","720");
  equals("integral(ln(x),1,e)","1");equals("diff(x^3,2)","12");
  // Decimal::root (p/q powers, root()): y^n gives x back, across exponents a
  // double cannot hold (exp(ln) itself drifts to ~1E-36 out there); negative
  // bases keep their real odd roots.
  for(const char* x:{"2","0.37","2E50","3E-500","7.5E+3000"})
    for(unsigned n:{3u,5u,7u,12u}) {
      const Decimal v(x),y=v.root(n),slow=v.pow(Decimal(1)/Decimal(static_cast<int>(n)));
      assert((y.pow(Decimal(static_cast<int>(n)))/v-1).abs()<=Decimal("1E-37"));
      assert((y-slow).abs()<=slow*Decimal("1E-35"));
    }
  assert(Decimal(0).root(3).zero()&&!Decimal(-8).root(3).finite());
  equals("(-8)^(1/3)","-2");equals("(-8)^(2/3)","4");equals("(-32)^(-3/5)","-0.125");
  equals("root(3,-27)","-3");equals("1.7^(1/3)-e^(ln(1.7)/3)","0","1E-35");
  assert(calc("(-4)^(1/2)").error==Error::Math&&calc("0^(-1/3)").error==Error::Math);
  // Singular and sqrt-like ends exhaust Gauss-Kronrod's depth; tanh-sinh
  // takes over (each of these was Time Out before it).
  equals("integral(sqrt(x),0,1)","0.6666666666666666666667","1E-10");
  equals("integral(1/sqrt(x),0,1)","2","1E-15");equals("integral(1/sqrt(x),1,0)","-2","1E-15");
  equals("integral(ln(x),0,1)","-1","1E-15");equals("integral(ln(x)/sqrt(x),0,1)","-4","1E-15");
  equals("integral(1/sqrt(1-x^2),-1,1)","3.141592653589793238462643","1E-15");
  equals("integral(abs(x-1/3),0,1)","0.2777777777777777777778","1E-10");
  assert(calc("integral(1/x,0,1)").error==Error::TimeOut);  // divergent: no number
  // NumWorks Poincare's approximation cases: huge values (relative
  // tolerance), a narrow peak, a kink, far bounds (mapped), a singular
  // interior point, a nested integral.
  equals("integral(e^x,0,200)","7.2259737681257492581774E86","1E72");  // e^200-1
  equals("integral(e^(x^3),0,4)","1.312888495564E26","1E13");
  equals("integral(710*x*e^(-710*x),0,10)","0.00140845070422535211","1E-15");
  {Settings radian;radian.angle=Angle::Radian;
   const Result kink=exactCalc("integral(abs(2*sin(e^(x/4))+1),0,6)",radian);
   assert(kink.error==Error::None&&(kink.value.number-Decimal("12.573260585347")).abs()<Decimal("1E-9"));}
  equals("integral(e^(-x^2),0,100000)","0.88622692545275801365","1E-12");
  equals("integral(e^(-x^2),-100000,100000)","1.7724538509055160273","1E-12");
  equals("integral(1/(x^2+1),0,100000)","1.5707863267949","1E-12");
  equals("integral(1/x,1,1000000)","13.815510557964274104","1E-10");
  equals("integral(1/sqrt(abs(x)),-1,1)","4","1E-9");
  equals("integral(integral(x,0,x),0,1)","0.16666666666666666667","1E-15");
  equals("integral(sqrt(x^2-3)/x,sqrt(3),2)","0.093100317882891","1E-10");
  equals("root(3,-8)","-2");equals("(-8)^(1/3)","-2");
  // P2 postfix power: new ^() scripts share the binary-^ path; legacy pow()
  // stays readable, including mixed with new syntax in one expression.
  equals("29^(3)","24389");equals("(1/2)^(3)","0.125");
  {Request q;q.expression="2x^(3)";q.memory.variables[6]=Value(2);
   auto r=evaluate(q);assert(r.error==Error::None&&r.value.number==16);}
  equals("pow(9,3)","729");equals("pow(2,-1)","0.5");
  equals("pow(9,3)+9^(3)","1458");
  {Request q;q.expression="f(3)";q.memory.f="pow(x,2)";assert(evaluate(q).value.number==9);
   q.memory.f="x^(2)";assert(evaluate(q).value.number==9);}
  equals("repeat(3,1)","0.33333333333333333333333","1E-23");
  auto r=calc("sqrt(2)/3");assert(formatValue(r.value,Settings{})=="sqrt(2)/3");
  assert(formatValue(calc("1000*sqrt(41*89*22)/(4*2760)").value,Settings{})=="25*sqrt(80278)/276");
  assert(formatValue(calc("1/3").value,Settings{},Format::Recurring)=="0.(3)");
  assert(calc("1/0").error==Error::Math);assert(calc("sqrt(-1)").error==Error::Math);
  assert(calc("tan(90)").error==Error::Math);assert(calc("1..2").error==Error::Syntax);
  assert(calc("f(2)").error==Error::NotDefined);
  Request q;q.expression="f(3)";q.memory.f="x^2+1";r=evaluate(q);assert(r.value.number==10);
  q.memory.f="g(x)";q.memory.g="f(x)";assert(evaluate(q).error==Error::Circular);
  q=Request{};q.expression="3A+B";q.memory.variables[0]=Value(5);q.memory.variables[1]=Value(10);assert(evaluate(q).value.number==25);
  q.expression="0<8/9<1";q.verify=true;r=evaluate(q);assert(r.error==Error::None&&r.truth&&r.memory.ans.re.number==1);
  q.expression="5<6>4";assert(evaluate(q).error==Error::Syntax);
  q.expression="2+2";assert(evaluate(q).error==Error::NoOperator);
  q=Request{};q.expression="sum(x,1,1000000)";assert(evaluate(q,[]{return true;}).error==Error::Cancelled);
  q.expression="rem(5,2)";r=evaluate(q);assert(r.pair&&r.value.number==2&&r.second.number==1&&r.memory.variables[4].re.number==2);
  q=Request{};q.verify=true;q.expression="1<2=2";assert(evaluate(q).error==Error::Syntax);
  // sin/cos are exact at multiples of 45 degrees as well as 30.
  r=calc("sin(45)");assert(r.value.exact().valid&&r.value.exact().radicand==2&&r.value.exact().denominator==2);
  complexExamples();
  baseNExamples();
  aggregateExamples();
  equationExamples();
  exactExamples();
  std::cout<<"Calculator engine examples, precision, errors, cancellation, memory, complex, Base-N, matrix/vector, equation/inequality and exact arithmetic passed.\n";
}
