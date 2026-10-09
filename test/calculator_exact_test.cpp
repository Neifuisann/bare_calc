// Exact arithmetic: BigInt, Rational and the ExactNumber normal form.
// Decimal references are published constants (OEIS / NIST digits), never
// computed by this code, so approximation is checked against an oracle.
#include "calculator/math/calculator_exact.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace Calculator;
namespace {
int checks = 0;
void require(bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << what << "\n";
    std::exit(1);
  }
}
ExactNumber X(const std::string& text) { return ExactNumber::parse(text); }
// `text` evaluates exactly to the canonical form `canonical`, which parses
// back to the identical value.
void same(const std::string& text, const std::string& canonical) {
  const ExactNumber x = X(text);
  require(x.exact(), text + " is exact");
  require(x.str() == canonical, text + " -> " + x.str() + ", expected " + canonical);
  require(X(x.str()) == x, text + ": parse(str()) round-trips");
}
void status(const std::string& text, ExactStatus expected) {
  require(X(text).status() == expected, text + " status " + std::to_string(static_cast<int>(X(text).status())));
}
void approx(const std::string& text, unsigned digits, const std::string& expected) {
  const std::string got = X(text).approximate(digits);
  require(got == expected, text + " ~ " + got + ", expected " + expected);
}
BigInt big(const char* digits) {
  BigInt b;
  require(BigInt::parse(digits, b), std::string("parse ") + digits);
  return b;
}

void bigIntegers() {
  const BigInt a = big("123456789012345678901234567890"), b = big("987654321098765432109876543210");
  require((a + b).str() == "1111111110111111111011111111100", "big add");
  require((b - a).str() == "864197532086419753208641975320", "big subtract");
  require((a - b).str() == "-864197532086419753208641975320", "big negative subtract");
  require((a * b).str() == "121932631137021795226185032733622923332237463801111263526900", "big multiply");
  require((b / a).str() == "8" && (b % a).str() == "9000000000900000000090", "big divide");
  require((-b / a).str() == "-8" && (-b % a).str() == "-9000000000900000000090", "truncating signed divide");
  require(BigInt(2).pow(200).str() == "1606938044258990275541962092341162602522202993782792835301376", "2^200");
  // Knuth D's rare add-back branch: divisor with a high top limb.
  const BigInt u = big("340282366920938463463374607431768211455"), v = big("18446744073709551617");
  require((u / v).str() == "18446744073709551615" && (u % v).zero(), "(2^128-1)/(2^64+1)");
  require(BigInt::gcd(big("1071508607186267320948425049060001810561404811705533607443750388370351051124936122493198378815695858127594672917553146825187145285692314043598457757469857480393456777482423098542107460506237114187795418215304647498358194126739876755916554394607706291457119647768654216766042983165262438683720566806937"), BigInt(3)).str() == "1", "gcd of a 1000-bit value");
  // int64 boundaries promote and demote exactly.
  const BigInt maxi(INT64_MAX), mini(INT64_MIN);
  require((maxi + BigInt(1)).str() == "9223372036854775808" && !(maxi + BigInt(1)).fitsInt64(), "INT64_MAX+1 promotes");
  require(((maxi + BigInt(1)) - BigInt(1)).fitsInt64(), "and demotes back");
  require((-mini).str() == "9223372036854775808" && (mini / BigInt(-1)).str() == "9223372036854775808", "-INT64_MIN");
  require((mini * BigInt(-1) + mini).zero(), "INT64_MIN round trip");
  for (const BigInt& value : {BigInt(1), BigInt(-1), mini, a, -a}) {
    require(value.shifted(INT64_MIN).zero(), "extreme right shift truncates to zero");
    require(value.shifted(-4294967296LL).zero(), "right shift beyond 32-bit size_t truncates to zero");
    require(value.shifted(-static_cast<int64_t>(value.bits())).zero(), "right shift by bit length is zero");
    require(value.shifted(1 - static_cast<int64_t>(value.bits())) == BigInt(value.sign()), "right shift below bit length preserves top bit");
  }
  BigInt r;
  require(big("1000000000000000000000000000000000000000000000000000000000000").exactRoot(3, r) && r.str() == "100000000000000000000", "cube root of 10^60");
  require(!big("1000000000000000000000000000000000000000000000000000000000001").exactRoot(3, r), "10^60+1 is no cube");
  require(big("99999999999999999999").root(2).str() == "9999999999", "floor sqrt");
  require(big("-12345678901234567890123").shifted(-40).str() == "-11228329550" && BigInt(3).shifted(70).str() == "3541774862152233910272", "shifts");
  require(big("+42").str() == "42" && !BigInt::parse("4x2", r) && !BigInt::parse("-", r), "parse");

  // 100! through ExactNumber: no overflow anywhere below kMaxBits.
  ExactNumber f = ExactNumber::integer(1);
  for (int i = 2; i <= 100; ++i) f = f * ExactNumber::integer(i);
  require(f.str() == "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000", "100!");
  require((f / (f / ExactNumber::integer(100))).str() == "100", "100!/99! = 100");
  same("2^100-2^100+1", "1");
  same("(2^64+1)*(2^64-1)", "340282366920938463463374607431768211455");
  same("10^30/10^28", "100");
  status("2^100000", ExactStatus::Inexact);  // past kMaxBits: no exact claim, no error
  status("2^4095", ExactStatus::Exact);      // 4096 bits: the largest allowed
  status("2^4096", ExactStatus::Inexact);
  status("3^2000/2^3000", ExactStatus::Exact);
  same("(2^3700+1)/2^3700-1", "1/" + BigInt(2).pow(3700).str());
}

void fractions() {
  same("1/3+1/7", "10/21");
  same("-1/3-1/6", "-1/2");
  same("(-3)/(-9)", "1/3");
  same("3/(-9)", "-1/3");
  same("-(-2/4)", "1/2");
  same("1/2-1/2", "0");
  same("0/5", "0");
  same("(-2/3)^3", "-8/27");
  same("(-2/3)^-3", "-27/8");
  same("(2/3)^0", "1");
  same("0.1+0.2", "3/10");
  same("1.5E-3", "3/2000");
  same("-2E+4", "-20000");
  same("123456789/987654321", "13717421/109739369");
  same("1/(2^70)*2^70", "1");
  require(ExactNumber::fraction(6, -4).str() == "-3/2", "fraction(6,-4)");
  require(ExactNumber::fromLiteral("12.50").str() == "25/2" && ExactNumber::fromLiteral(".5").str() == "1/2", "literals");
  require(!ExactNumber::fromLiteral("1..2").exact() && !ExactNumber::fromLiteral("E5").exact(), "bad literals");
}

void radicals() {
  same("sqrt(8)+sqrt(18)", "5*sqrt(2)");
  same("sqrt(8)", "2*sqrt(2)");
  same("sqrt(12)*sqrt(3)", "6");
  same("sqrt(2)*sqrt(3)", "sqrt(6)");
  same("sqrt(2)/sqrt(8)", "1/2");
  same("1/sqrt(2)", "1/2*sqrt(2)");
  same("sqrt(1/3)", "1/3*sqrt(3)");
  same("sqrt(50)-5*sqrt(2)", "0");
  same("sqrt(72)", "6*sqrt(2)");
  same("root(3,54)", "3*root(3,2)");
  same("root(3,-54)", "-3*root(3,2)");
  same("root(4,4)", "sqrt(2)");
  same("root(6,8)", "sqrt(2)");
  same("root(4,36)", "sqrt(6)");
  same("sqrt(2)*root(3,2)", "root(6,32)");
  same("8^(1/3)", "2");
  same("4^(3/2)", "8");
  same("(-8)^(1/3)", "-2");
  same("(-8)^(2/3)", "4");
  same("27^(-2/3)", "1/9");
  same("2^(1/2)", "sqrt(2)");
  same("(1+sqrt(2))^2", "3+2*sqrt(2)");
  same("(1+sqrt(2))*(1-sqrt(2))", "-1");
  same("1/(1+sqrt(2))", "-1+sqrt(2)");
  same("1/(sqrt(2)+sqrt(3))", "-sqrt(2)+sqrt(3)");
  same("1/(1+sqrt(2)+sqrt(3))", "1/2+1/4*sqrt(2)-1/4*sqrt(6)");
  same("(sqrt(2)+sqrt(3))/(sqrt(2)+sqrt(3))", "1");
  same("1/(1+root(3,2))", "1/3-1/3*root(3,2)+1/3*root(3,4)");
  same("(2+sqrt(3))^5*(2-sqrt(3))^5", "1");
  // A large unextracted square (1009 > trial limit) still merges and cancels.
  same("sqrt(1009^2*2)", "1009*sqrt(2)");  // a whole cofactor square is still found
  same("sqrt(1009^2*2026)-1009*sqrt(2026)", "0");  // 1009^2*1013: found only by merging
  same("sqrt(1009^2*2026)", "sqrt(2062632106)");
  status("sqrt(-4)", ExactStatus::NotReal);
  status("root(4,-16)", ExactStatus::NotReal);
  status("(-8)^(1/2)", ExactStatus::NotReal);
  status("(1-sqrt(3))^(1/2)", ExactStatus::NotReal);
}

void nestedRadicals() {
  same("sqrt(3+2*sqrt(2))", "1+sqrt(2)");
  same("sqrt(5-2*sqrt(6))", "-sqrt(2)+sqrt(3)");
  same("sqrt(4+2*sqrt(3))", "1+sqrt(3)");
  same("sqrt(2+sqrt(3))", "1/2*sqrt(2)+1/2*sqrt(6)");
  same("root(4,17+12*sqrt(2))", "1+sqrt(2)");
  // Not denestable: a nested radical atom, still exact and canonical.
  same("sqrt(1+sqrt(2))", "sqrt(1+sqrt(2))");
  same("sqrt(1+sqrt(2))^2", "1+sqrt(2)");
  same("sqrt(2+2*sqrt(2))", "sqrt(2)*sqrt(1+sqrt(2))");
  same("sqrt(1+sqrt(2))*sqrt(1+sqrt(2))*sqrt(1+sqrt(2))", "sqrt(1+sqrt(2))+sqrt(2)*sqrt(1+sqrt(2))");
  same("1/sqrt(1+sqrt(2))", "-sqrt(1+sqrt(2))+sqrt(2)*sqrt(1+sqrt(2))");
  same("sqrt(1+sqrt(2))-sqrt(1+sqrt(2))", "0");
  same("root(3,1+sqrt(2))^3", "1+sqrt(2)");
  approx("sqrt(1+sqrt(2))", 30, "1.55377397403003730734415895306");
  // Nested radicals can be dependent: sqrt(5-sqrt(5)) is
  // (sqrt(10)+sqrt(2))/2 * sqrt(5-2sqrt(5)). Commensurable atoms merge onto
  // one, so equal values meet and a zero sum is zero (cases from NumWorks
  // Poincare's simplification tests; each was a nonzero sum before).
  same("(sqrt(10)-sqrt(2))*sqrt(5-sqrt(5))-4*sqrt(5-2*sqrt(5))", "0");
  same("sqrt(5-sqrt(5))-(sqrt(10)+sqrt(2))/2*sqrt(5-2*sqrt(5))", "0");
  same("1/sqrt(sqrt(17)-4)-sqrt(4+sqrt(17))", "0");
  same("sqrt(2+sqrt(2))*sqrt(2-sqrt(2))", "sqrt(2)");
  same("sqrt(2+sqrt(2))+sqrt(2-sqrt(2))", "2*sqrt(2-sqrt(2))+sqrt(2)*sqrt(2-sqrt(2))");
  same("sqrt(1+sqrt(2))+sqrt(3+sqrt(2))", "sqrt(1+sqrt(2))+sqrt(3+sqrt(2))");  // independent: both stay
  // A perfect square of a transcendental sum has its root; a sum of two
  // surds denests after pulling one out.
  same("sqrt(pi^2+2*pi+1)", "1+pi");
  same("sqrt((pi-4)^2)", "4-pi");
  same("sqrt(1/4+pi+pi^2)", "1/2+pi");
  same("sqrt(e^2+2*e*ln(2)+ln(2)^2)", "e+ln(2)");
  same("sqrt(pi^2+1)", "sqrt(1+pi^2)");
  same("sqrt(57*sqrt(17)+68*sqrt(10))", "2*root(4,1700)+root(4,4913)");
}

void constants() {
  same("pi", "pi");
  same("pi+pi", "2*pi");
  same("pi/2+pi/3", "5/6*pi");
  same("pi-pi", "0");
  same("pi*pi", "pi^2");
  same("pi/pi", "1");
  same("1/pi", "pi^(-1)");
  same("sqrt(pi^2)", "pi");
  same("pi^(1/2)*pi^(1/2)", "pi");
  same("2*pi*sqrt(3)", "2*sqrt(3)*pi");
  same("e*e", "e^2");
  same("sqrt(e)", "e^(1/2)");
  same("e^2/e", "e");
  same("(pi+pi*sqrt(2))/pi", "1+sqrt(2)");
  same("1/(pi+pi*sqrt(2))", "-pi^(-1)+sqrt(2)*pi^(-1)");
  same("pi+e+1", "1+pi+e");
  same("(pi+1)^2", "1+2*pi+pi^2");
  same("(pi+1)^2-pi^2-2*pi", "1");
  // Symbols never become decimals to be compared: 355/113 is not pi.
  require(!(X("355/113") == X("pi")) && !(X("355/113-pi")).zero(), "355/113 != pi");
  // A transcendental denominator of several terms has no exact inverse.
  status("1/(1+pi)", ExactStatus::Inexact);
  status("2^pi", ExactStatus::Inexact);
  status("e^pi", ExactStatus::Inexact);
}

void logarithms() {
  same("ln(1)", "0");
  same("ln(e)", "1");
  same("ln(e^3)", "3");
  same("ln(sqrt(e))", "1/2");
  same("exp(ln(3))", "3");
  same("e^(ln(3))", "3");
  same("ln(3)", "ln(3)");
  same("ln(4)", "2*ln(2)");
  same("ln(6)-ln(2)-ln(3)", "0");
  same("ln(8)/ln(2)", "3");
  same("ln(100)/ln(10)", "2");
  same("ln(1000)/ln(10)", "3");
  same("ln(1/8)", "-3*ln(2)");
  same("ln(2/3)", "ln(2)-ln(3)");
  same("ln(sqrt(2))", "1/2*ln(2)");
  same("ln(2*pi)", "ln(2)+ln(pi)");
  same("exp(2*ln(3)+1)", "9*e");
  same("exp(ln(2)/2)", "sqrt(2)");
  same("ln(1+sqrt(2))+ln(-1+sqrt(2))", "0");
  same("ln(-1+sqrt(2))", "-ln(1+sqrt(2))");
  same("ln(ln(3))", "ln(ln(3))");
  same("ln(ln(2))", "-ln(ln(2)^(-1))");
  same("ln(1000003^2)", "2*ln(1000003)");
  same("ln(3)^2/ln(3)", "ln(3)");
  status("ln(0)", ExactStatus::NotReal);
  status("ln(-1)", ExactStatus::NotReal);
  status("ln(1-sqrt(2))", ExactStatus::NotReal);
  require(X("100").log(X("10")).str() == "2" && X("2").log(X("4")).str() == "1/2", "log(base)");
  status("ln(2)/ln(1)", ExactStatus::DivisionByZero);
}

void mixed() {
  same("(1+sqrt(2))*(pi+e)", "pi+sqrt(2)*pi+e+sqrt(2)*e");
  same("(sqrt(2)+pi)^2-2*sqrt(2)*pi", "2+pi^2");
  same("sqrt(3)/2*2/sqrt(3)", "1");
  same("(3+sqrt(5))/2-(1+sqrt(5))/2", "1");
  same("((1+sqrt(5))/2)^2-(1+sqrt(5))/2-1", "0");  // the golden ratio's equation, exactly
  same("-(pi-sqrt(2))", "sqrt(2)-pi");
  same("ln(2)*pi/(2*pi)", "1/2*ln(2)");
  same("sqrt(12)/(2*sqrt(3))+1/2", "3/2");
  // Sign of structurally nonzero values, from certified approximation.
  require(X("pi-355/113").sign() < 0 && X("e-2718/1000").sign() > 0, "signs near rationals");
  require(X("sqrt(2)+sqrt(3)-sqrt(10)").sign() < 0, "sqrt(2)+sqrt(3) (3.146) < sqrt(10) (3.162)");
  require(X("-pi").abs().str() == "pi" && X("1-sqrt(2)").abs().str() == "-1+sqrt(2)", "abs");
  bool certain = false;
  require(ExactNumber::compare(X("sqrt(2)"), X("1414213562373095/1000000000000000"), &certain) > 0 && certain, "compare");
  require(ExactNumber::compare(X("sqrt(8)"), X("2*sqrt(2)"), &certain) == 0 && certain, "equal compares 0");
  Rational q;
  require(X("5/6*pi").piMultiple(q) && q == Rational::of(5, 6) && X("0").piMultiple(q) && q.zero(), "piMultiple");
  require(!X("pi^2").piMultiple(q) && !X("sqrt(2)*pi").piMultiple(q), "not pi multiples");
  std::vector<ExactNumber::SimpleTerm> terms;
  require(X("1/2+sqrt(3)/2").simpleTerms(terms) && terms.size() == 2 && terms[1].radicand == BigInt(3), "simple terms");
  require(!X("root(3,2)").simpleTerms(terms) && !X("sqrt(2)*pi").simpleTerms(terms) && !X("ln(2)").simpleTerms(terms), "not simple");
  require(X("7").simpleTerms(terms) && terms.size() == 1, "rational simple term");
}

void divisionByZero() {
  status("1/0", ExactStatus::DivisionByZero);
  status("0/0", ExactStatus::DivisionByZero);
  status("1/(sqrt(2)*sqrt(2)-2)", ExactStatus::DivisionByZero);   // zero found structurally, not by 1E-40
  status("1/(pi-pi)", ExactStatus::DivisionByZero);
  status("1/(ln(6)-ln(2)-ln(3))", ExactStatus::DivisionByZero);
  status("0^0", ExactStatus::DivisionByZero);
  status("0^(-2)", ExactStatus::DivisionByZero);
  status("(1/0)+1", ExactStatus::DivisionByZero);  // errors propagate
  status("sqrt(1/0)", ExactStatus::DivisionByZero);
  require(ExactNumber::fraction(1, 0).status() == ExactStatus::DivisionByZero, "fraction(1,0)");
  require(!ExactNumber().exact() && !(ExactNumber() + ExactNumber::integer(1)).exact(), "Inexact propagates");
  require((ExactNumber() + ExactNumber::fraction(1, 0)).status() == ExactStatus::DivisionByZero, "certain errors win");
  same("0^3", "0");
  status("(", ExactStatus::Inexact);
  status("foo(2)", ExactStatus::Inexact);
}

void decimals() {
  // Published digits: pi (A000796), e (A001113), sqrt(2) (A002193),
  // ln 2 (A002162), ln 3 (A002391), golden ratio (A001622), ln(pi) (A053510).
  approx("pi", 50, "3.1415926535897932384626433832795028841971693993751");
  approx("e", 50, "2.7182818284590452353602874713526624977572470937");  // ...0936999|6 rounds up
  approx("sqrt(2)", 50, "1.4142135623730950488016887242096980785696718753769");
  approx("ln(2)", 40, "0.6931471805599453094172321214581765680755");
  approx("ln(3)", 40, "1.098612288668109691395245236922525704647");
  approx("(1+sqrt(5))/2", 40, "1.61803398874989484820458683436563811772");
  approx("ln(pi)", 30, "1.14472988584940017414342735135");
  approx("pi^(1/2)", 30, "1.77245385090551602729816748334");
  approx("1/3", 10, "0.3333333333");
  approx("2/3", 10, "0.6666666667");
  approx("-2/3", 5, "-0.66667");
  approx("1/4", 10, "0.25");
  approx("10^30/3", 5, "3.3333E+29");
  approx("1/(3*10^8)", 4, "3.333E-9");
  approx("1/1000", 3, "0.001");
  approx("9.9996", 4, "10");
  approx("0", 10, "0");
  approx("pi*10^40", 12, "3.14159265359E+40");
  approx("e^(-30)", 20, "9.3576229688401746049E-14");
  // Cancellation: the difference is ~2.6E-7 even though each term is ~3.14.
  approx("pi-355/113", 12, "-2.66764189062E-7");
  // Very close to an integer (Ramanujan-style): precision rises until certified.
  same("(1+sqrt(2))^40+(1-sqrt(2))^40", "2046573816377474");
  approx("(1+sqrt(2))^40", 35, "2046573816377473.9999999999999995114");
  const std::string pi1000 = X("pi").approximate(1000);
  require(pi1000.size() == 1001 && pi1000.compare(pi1000.size() - 25, 25, "1927876611195909216420199") == 0,
          "1000 digits of pi end ...1927876611195909216420199");
  require(X("pi").toDecimal() == Decimal("3.141592653589793238462643383279502884197"), "pi toDecimal");
  require(X("1/3").toDecimal() == Decimal(1) / Decimal(3), "1/3 toDecimal");
  require(X("sqrt(2)*sqrt(2)").toDecimal() == Decimal(2), "sqrt(2)^2 is exactly 2 in decimal");
  require(X("10^60+1").toDecimal() == Decimal("1E60"), "large integer rounds to 40 digits");
  require(!ExactNumber().toDecimal().finite() && ExactNumber().approximate(10).empty(), "inexact has no approximation");
}

void heapAndDepth() {
  require(ExactNumber::integer(5).heapBytes() == 0, "small rationals own no heap");
  require(X("sqrt(2)").heapBytes() > 0 && X("2^200").heapBytes() > 0, "symbolic/big values report heap");
  require(X("sqrt(1+sqrt(2))").depth() == 1 && X("ln(ln(3))").depth() == 1 &&
          X("sqrt(1+sqrt(1+sqrt(1+sqrt(2))))").depth() == 3, "depth");
  status("sqrt(1+sqrt(1+sqrt(1+sqrt(1+sqrt(1+sqrt(2))))))", ExactStatus::Inexact);  // depth 5 > kMaxDepth
  status("ln(ln(ln(3)))", ExactStatus::Exact);     // ln(0.094): negative but real
  status("ln(ln(ln(ln(3))))", ExactStatus::NotReal);  // ln(-2.36)
}
}  // namespace

int main() {
  bigIntegers();
  fractions();
  radicals();
  nestedRadicals();
  constants();
  logarithms();
  mixed();
  divisionByZero();
  decimals();
  heapAndDepth();
  std::cout << "Exact arithmetic: " << checks << " checks passed (BigInt, fractions, radicals, nested radicals, "
               "constants, logarithms, mixed, division by zero, decimals).\n";
  return 0;
}
