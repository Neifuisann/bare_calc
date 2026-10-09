#include "calculator/ui/calculator_menu.h"

#include <algorithm>
#include <array>

#include "calculator/math/calculator_complex.h"
#include "calculator/assets/calculator_constants.h"
#include "calculator/math/calculator_conversions.h"
#include "calculator/math/calculator_eng_symbols.h"
#include "calculator/ui/calculator_presentation.h"

namespace Calculator {
namespace {

void collectGroups(bool constants, std::vector<std::string>& groups) {
  const size_t count = constants ? sizeof(kConstants) / sizeof(kConstants[0])
                                 : sizeof(kConversions) / sizeof(kConversions[0]);
  for (size_t i = 0; i < count; ++i) {
    const std::string category =
        constants ? kConstants[i].group : kConversions[i].group;
    if (std::find(groups.begin(), groups.end(), category) == groups.end())
      groups.push_back(category);
  }
}

}  // namespace

const std::vector<CatalogGroup>& catalogData() {
  static const std::vector<CatalogGroup> data = {
      {"Func Analysis",
       {{"Derivative(d/dx)", "diff(,)"},
        {"Integration", "integral(,,)"},
        {"Summation", "sum(,,)"},
        {"Product", "product(,,)"},
        {"Remainder", "rem(,)"},
        {"Logarithm", "log(,)"},
        {"Natural Logarithm", "ln()"}}},
      {"Probability",
       {{"%", "%"},
        {"Factorial(!)", "!"},
        {"Permutation(P)", "P"},
        {"Combination(C)", "C"},
        {"Random Number", "rand()"},
        {"Random Integer", "randint(,)"}}},
      {"Numeric Calc",
       {{"GCD", "gcd(,)"},
        {"LCM", "lcm(,)"},
        {"Absolute Value", "abs()"},
        {"Recurring Decimal", "repeat(,)"},
        {"Integer Part", "int()"},
        {"Round Off", "rnd()"},
        {"Largest Integer", "floor()"}}},
      {"Angle/Coord/Sexa",
       {{"Degrees", "deg()"},
        {"Radians", "rad()"},
        {"Gradians", "grad()"},
        {"Rect to Polar", "pol(,)"},
        {"Polar to Rect", "rec(,)"},
        {"Degrees Minutes Seconds", "dms(,,)"}}},
      {"Hyperbolic/Trig",
       {{"sinh", "sinh()"},
        {"cosh", "cosh()"},
        {"tanh", "tanh()"},
        {"asinh", "asinh()"},
        {"acosh", "acosh()"},
        {"atanh", "atanh()"},
        {"sin", "sin()"},
        {"cos", "cos()"},
        {"tan", "tan()"},
        {"asin", "asin()"},
        {"acos", "acos()"},
        {"atan", "atan()"}}},
      // Postfix symbol bytes (calculator_eng_symbols.h), drawn as m u n ...
      {"Engineer Symbol",
       {{"Milli", "\x10"},
        {"Micro", "\x11"},
        {"Nano", "\x12"},
        {"Pico", "\x13"},
        {"Femto", "\x14"},
        {"Kilo", "\x15"},
        {"Mega", "\x16"},
        {"Giga", "\x17"},
        {"Tera", "\x18"},
        {"Peta", "\x19"},
        {"Exa", "\x1A"}}},
      {"Other",
       {{"Ans", "Ans"},
        {"PreAns", "PreAns"},
        {"Pi", "pi"},
        {"e", "e"},
        {"Reciprocal", "^(-1)"},
        {"Square", "^(2)"},
        {"Cube", "^(3)"},
        {"Negative", "-"},
        {"Comma", ","}}},
      {"Verify",
       {{"=", "="},
        {"!=", "!="},
        {"<", "<"},
        {">", ">"},
        {"<=", "<="},
        {">=", ">="}}},
      // Menu rows are ASCII ClassWiz15: the native angle glyph cannot render
      // here, so the polar operator row is named instead (plan O-8).
      {"Complex",
       {{"i", "i"},
        {"Polar Angle", "@"},
        {"Conjugate", "conjg()"},
        {"Argument", "arg()"},
        {"Real Part", "rep()"},
        {"Imaginary Part", "imp()"}}},
      // Base-N: A-F are digits here; prefixes and operators are stored as the
      // symbols calculator_basen.h defines.
      {"Hex Value", {{"A", "A"}, {"B", "B"}, {"C", "C"}, {"D", "D"}, {"E", "E"}, {"F", "F"}}},
      {"Base Prefix", {{"Decimal(d)", "$"}, {"Hexadecimal(h)", "#"}, {"Binary(b)", "&"}, {"Octal(o)", "|"}}},
      {"Logic Operation",
       {{"Neg", "neg()"}, {"Not", "not()"}, {"and", " and "}, {"or", " or "}, {"xor", " xor "}, {"xnor", " xnor "}}},
      {"Matrix",
       {{"MatA", "MatA"}, {"MatB", "MatB"}, {"MatC", "MatC"}, {"MatD", "MatD"}, {"MatAns", "MatAns"},
        {"Determinant", "det()"}, {"Transposition", "trn()"}, {"Identity", "identity()"}, {"Absolute Value", "abs()"}}},
      {"Vector",
       {{"VctA", "VctA"}, {"VctB", "VctB"}, {"VctC", "VctC"}, {"VctD", "VctD"}, {"VctAns", "VctAns"},
        {"Dot Product", "'"}, {"Angle", "angle(,)"}, {"Unit Vector", "unitv()"}, {"Absolute Value", "abs()"}}},
      // Calculate only: the CAS commands (calculator_cas.h, docs/calculator/CAS.md).
      {"Symbolic",
       {{"Simplify", "simplify()"},
        {"Expand", "expand()"},
        {"Factor", "factor()"},
        {"Derivative", "deriv()"},
        {"Antiderivative", "integrate()"},
        {"Partial Fractions", "partfrac()"},
        {"Solve", "solve()"},
        {"Substitute", "subst(,,)"}}},
  };
  return data;
}

// Display aliases keep semantic labels/action IDs stable. Every fixed choice
// fits the chrome font alongside the small shortcut circle and its arrow.
std::string menuDisplayLabel(const std::string& label){
  struct Alias{const char* full;const char* shortName;};
  static const Alias aliases[]={
    {"Angle/Coord/Sexa","Angle/Coord"},{"Hyperbolic/Trig","Hyperb/Trig"},
    {"Engineer Symbol","Eng Symbols"},{"Unit Conversions","Unit Convert"},
    {"Derivative(d/dx)","d/dx"},{"Natural Logarithm","Natural Log"},
    {"Degrees Minutes Seconds","Deg/Min/Sec"},{"Recurring Decimal","Recurring Dec"},
    {"Rectangular Coord","Rect Coord"},{"Improper Fraction","Improper Frac"},
    {"Variable Memory","Variable Mem"},{"Settings & Data","Settings/Data"},
    {"System Settings","System Setup"},{"Fraction Result","Fraction Form"},
    {"Digit Separator","Digit Separat."},{"Polar (r, theta)","Polar (r,t)"},
    {"Logic Operation","Logic Ops"},{"Electromagnetic","Electromag"},
    {"Atomic & Nuclear","Atomic/Nuclear"},{"Physico-Chemical","Phys/Chem"},
    {"Mean/Var/Dev...","Mean/Var/Dev"},{"Min/Max/Quartile","Min/Max/Quart"},
    {"Select Reg Type","Regression"},{"Statistics Calc","Stats Calc"},
    {"Freq Descending","Freq Descend"},{"Define f(x)/g(x)","Define f/g"},
    {"Delete f/g Column","Delete f/g Col"},{"Largest Integer","Largest Int"},
    {"Partial Fractions","Partial Frac"}
  };
  for(const auto& alias:aliases)if(label==alias.full)return alias.shortName;
  return label;
}
std::string quickCatalogLabel(const CatalogEntry& entry){
  const std::string expression=entry.expression;
  if(expression=="diff(,)")return "d/dx";
  if(expression=="integral(,,)")return "Integral";
  if(expression=="product(,,)")return "Product";
  if(expression=="sum(,,)")return "Sum";
  if(expression=="randint(,)")return "RanInt";
  if(expression=="repeat(,)")return "Repeat";
  if(expression=="integrate()")return "AntiDiff";  // the CAS antiderivative
  if(expression.size()==1&&EngSymbol::is(expression[0]))return std::string(1,EngSymbol::ascii(expression[0]));
  if(expression.size()>8){const auto paren=expression.find('(');if(paren!=std::string::npos)return expression.substr(0,paren);}
  return expression;
}

int verifyGroupIndex() {
  const auto& data = catalogData();
  for (size_t i = 0; i < data.size(); ++i)
    if (data[i].name == "Verify") return static_cast<int>(i);
  return -1;
}

std::vector<MenuItem> catalogRootRows(bool verifyMode, AppId app) {
  const auto& data = catalogData();
  const bool complex = app == AppId::Complex, baseN = app == AppId::BaseN;
  std::vector<MenuItem> rows;
  int other = -1;
  for (size_t i = 0; i < data.size(); ++i) {
    const std::string& name = data[i].name;
    const bool baseNGroup = name == "Hex Value" || name == "Base Prefix" || name == "Logic Operation";
    if (baseN != baseNGroup) continue;  // Base-N shows its three groups and nothing else
    if (name == "Other") { other = static_cast<int>(i); continue; }  // always the last row
    if (!verifyMode && name == "Verify") continue;
    if (name == "Complex" && !complex) continue;
    if (name == "Matrix" && app != AppId::Matrix) continue;
    if (name == "Vector" && app != AppId::Vector) continue;
    if (name == "Func Analysis" && complex) continue;
    if (name == "Symbolic" && app != AppId::Calculate) continue;
    // Action is the data index, so hiding Verify never renumbers siblings.
    rows.push_back({static_cast<int>(i), data[i].name, true});
  }
  if (baseN) return rows;
  rows.push_back({CatalogRootAction::SciConstants, "Sci Constants", true});
  rows.push_back({CatalogRootAction::UnitConversions, "Unit Conversions", true});
  if (other >= 0) rows.push_back({other, data[static_cast<size_t>(other)].name, true});
  return rows;
}

std::vector<MenuItem> catalogCategoryRows(int group) {
  const auto& data = catalogData();
  std::vector<MenuItem> rows;
  if (group < 0 || group >= static_cast<int>(data.size())) return rows;
  for (size_t i = 0; i < data[group].entries.size(); ++i)
    rows.push_back({static_cast<int>(i), data[group].entries[i].label, false});
  return rows;
}

std::vector<MenuItem> constantsGroupRows() {
  std::vector<std::string> groups;
  collectGroups(true, groups);
  std::vector<MenuItem> rows;
  for (size_t i = 0; i < groups.size(); ++i)
    rows.push_back({static_cast<int>(i), groups[i], true});
  return rows;
}

std::vector<MenuItem> constantsItemRows(int group, std::vector<size_t>& indices) {
  indices.clear();
  std::vector<std::string> groups;
  collectGroups(true, groups);
  std::vector<MenuItem> rows;
  if (group < 0 || group >= static_cast<int>(groups.size())) return rows;
  const size_t count = sizeof(kConstants) / sizeof(kConstants[0]);
  for (size_t i = 0; i < count; ++i) {
    if (std::string(kConstants[i].group) != groups[static_cast<size_t>(group)])
      continue;
    indices.push_back(i);
    // Action is the position inside this group's item list; indices[] maps it
    // back to the kConstants data index for insertion.
    rows.push_back({static_cast<int>(rows.size()), kConstants[i].label, false});
  }
  return rows;
}

std::vector<MenuItem> conversionGroupRows() {
  std::vector<std::string> groups;
  collectGroups(false, groups);
  std::vector<MenuItem> rows;
  for (size_t i = 0; i < groups.size(); ++i)
    rows.push_back({static_cast<int>(i), groups[i], true});
  return rows;
}

std::vector<MenuItem> conversionItemRows(int group, std::vector<size_t>& indices) {
  indices.clear();
  std::vector<std::string> groups;
  collectGroups(false, groups);
  std::vector<MenuItem> rows;
  if (group < 0 || group >= static_cast<int>(groups.size())) return rows;
  const size_t count = sizeof(kConversions) / sizeof(kConversions[0]);
  for (size_t i = 0; i < count; ++i) {
    if (std::string(kConversions[i].group) != groups[static_cast<size_t>(group)])
      continue;
    indices.push_back(i);
    rows.push_back({static_cast<int>(rows.size()), kConversions[i].label, false});
  }
  return rows;
}

std::vector<MenuItem> toolRows(bool verifyMode, bool calc) {
  // Reference order from casio-tools.jpg and tools-menu.bmp: CALC, Undo,
  // Verify ON/OFF. Actions are stable; only the Verify label reflects state.
  std::vector<MenuItem> rows;
  if (calc) rows.push_back({ToolAction::Calc, "CALC", false});
  rows.push_back({ToolAction::Undo, "Undo", false});
  rows.push_back({ToolAction::VerifyToggle, verifyMode ? "Verify OFF" : "Verify ON", false});
  return rows;
}

std::vector<MenuItem> functionRows() {
  return {
      {FunctionAction::F, "f(x)", false},
      {FunctionAction::G, "g(x)", false},
      {FunctionAction::DefineF, "Define f(x)", false},
      {FunctionAction::DefineG, "Define g(x)", false},
  };
}

bool canStoreResult(const Result* result, bool hasResult) {
  // A CAS formula's `value` is a placeholder: only the numeric roots a
  // solve() answer carries (Result::solutions) are storable.
  return hasResult && result != nullptr && result->error == Error::None &&
         (!result->formula || (result->solutions && !result->solutions->empty()));
}

// P12 R-05: single capability source. The fx-880 availability model lives in
// Presentation::capabilitiesForResult (Standard-view-kind gated); this adapter
// only maps it onto stable FORMAT row actions so menu and renderer agree.
namespace {
bool available(Format format, const Presentation::Capabilities& c) {
  switch (format) {
    case Format::Standard:
      return c.standard;
    case Format::Decimal:
      return c.decimal;
    case Format::Engineering:
      return c.engineering;
    case Format::Sexagesimal:
      return c.sexagesimal;
    case Format::Mixed:
      return c.mixed;
    case Format::Improper:
      return c.improper;
    case Format::Recurring:
      return c.recurringDecimal;
    case Format::Prime:
      return c.primeFactor;
    case Format::Rectangular:
      return c.rectangular;
    case Format::Polar:
      return c.polar;
  }
  return false;
}

// `c` null: no usable result, Standard only.
std::vector<MenuItem> rowsFor(const Presentation::Capabilities* c) {
  // P12 R-05 §26: fx-880 manual order, not alphabetical nor legacy order.
  const std::array<std::pair<Format, const char*>, 10> all = {{
      {Format::Standard, "Standard"},
      {Format::Decimal, "Decimal"},
      {Format::Prime, "Prime Factor"},
      {Format::Recurring, "Recurring Decimal"},
      {Format::Rectangular, "Rectangular Coord"},
      {Format::Polar, "Polar Coord"},
      {Format::Improper, "Improper Fraction"},
      {Format::Mixed, "Mixed Number"},
      {Format::Engineering, "ENG Notation"},
      {Format::Sexagesimal, "Sexagesimal"},
  }};
  std::vector<MenuItem> rows;
  for (const auto& entry : all) {
    if (c ? !available(entry.first, *c) : entry.first != Format::Standard) continue;
    rows.push_back({static_cast<int>(entry.first), entry.second, false});
  }
  if (rows.empty()) rows.push_back({static_cast<int>(Format::Standard), "Standard", false});
  return rows;
}
}  // namespace

bool formatAvailable(Format format, const Result* result, bool hasResult,
                     const Settings& settings) {
  if (!hasResult || result == nullptr) return format == Format::Standard;
  return available(format, Presentation::capabilitiesForResult(*result, settings));
}

std::vector<MenuItem> formatRows(const Result* result, bool hasResult,
                                 const Settings& settings) {
  if (!hasResult || result == nullptr) return rowsFor(nullptr);
  const Presentation::Capabilities c = Presentation::capabilitiesForResult(*result, settings);
  return rowsFor(&c);
}

std::vector<MenuItem> formatRows(const ResultSummary& summary, const Settings& settings) {
  const Presentation::Capabilities c = Presentation::capabilitiesForSummary(summary, settings);
  return rowsFor(&c);
}

std::vector<MenuItem> variableActionRows(bool canStore) {
  std::vector<MenuItem> rows;
  rows.push_back({VariableAction::Recall, "Recall", false});
  if (canStore) rows.push_back({VariableAction::Store, "Store result", false});
  rows.push_back({VariableAction::Edit, "Edit", false});
  return rows;
}

std::vector<MenuItem> resetRows() {
  return {
      {ResetAction::Variables, "Variable Memory", true},
      {ResetAction::SettingsData, "Settings & Data", true},
      {ResetAction::All, "Initialize All", true},
      {ResetAction::Cancel, "Cancel", false},
  };
}

std::string resetQuestion(int resetAction) {
  switch (resetAction) {
    case ResetAction::Variables:
      return "Clear variables?";
    case ResetAction::SettingsData:
      return "Reset settings?";
    case ResetAction::All:
      return "Initialize all?";
    default:
      return "Reset?";
  }
}

std::vector<MenuItem> settingsRootRows(const Settings&) {
  return {{SettingAction::CalcSettingsGroup, "Calc Settings", true},
          {SettingAction::SystemSettingsGroup, "System Settings", true},
          {SettingAction::Reset, "Reset", true},
          {SettingAction::GetStarted, "Get Started", true}};
}

std::vector<MenuItem> calcSettingsRows(const Settings&) {
  // Bare labels only: the option pages carry the radio state, and inline
  // values truncated into "..." on the 192 px rows.
  return {
      {SettingAction::InputOutput, "Input/Output", true},
      {SettingAction::Angle, "Angle Unit", true},
      {SettingAction::Number, "Number Format", true},
      {SettingAction::Engineer, "Engineer Symbol", true},
      {SettingAction::Fraction, "Fraction Result", true},
      {SettingAction::Complex, "Complex Result", true},
      {SettingAction::DecimalMark, "Decimal Mark", true},
      {SettingAction::Separators, "Digit Separator", true},
      {SettingAction::FormatMenu, "Format Menu", true},
      {SettingAction::ScientificKey, "Scientific Key", true},
  };
}

std::vector<MenuItem> settingsOptionRows(int setting, const Settings& settings) {
  const auto radio = [](bool on) {
    return on ? UiKit::Mark::kRadioOn : UiKit::Mark::kRadioOff;
  };
  switch (setting) {
    case SettingAction::InputOutput: {
      const std::array<const char*, 4> names = {"MathI/MathO", "MathI/DecimalO", "LineI/LineO", "LineI/DecimalO"};
      std::vector<MenuItem> rows;
      for (int i = 0; i < 4; ++i)
        rows.push_back({i, names[static_cast<size_t>(i)], false, radio(static_cast<int>(settings.io) == i)});
      return rows;
    }
    case SettingAction::Angle: {
      const std::array<const char*, 3> names = {"Degree", "Radian", "Gradian"};
      std::vector<MenuItem> rows;
      for (int i = 0; i < 3; ++i)
        rows.push_back({i, names[static_cast<size_t>(i)], false, radio(static_cast<int>(settings.angle) == i)});
      return rows;
    }
    case SettingAction::Number: {
      const std::array<const char*, 4> names = {"Norm 1", "Norm 2", "Fix", "Sci"};
      std::vector<MenuItem> rows;
      for (int i = 0; i < 4; ++i)
        rows.push_back({i, names[static_cast<size_t>(i)], false, radio(static_cast<int>(settings.number) == i)});
      return rows;
    }
    case SettingAction::Digits: {
      // Sci uses 1..10 significant digits; the other modes use 0..9 decimal
      // places, matching the historical cycling behavior.
      const bool sci = settings.number == NumberFormat::Sci;
      std::vector<MenuItem> rows;
      for (int i = 0; i < 10; ++i) {
        const int value = sci ? i + 1 : i;
        rows.push_back({value, std::to_string(value), false, radio(settings.digits == value)});
      }
      return rows;
    }
    case SettingAction::Engineer:
      return {{1, "On", false, radio(settings.engineer)}, {0, "Off", false, radio(!settings.engineer)}};
    case SettingAction::Complex:
      // Native labels are a+bi and r-angle-theta; ClassWiz15 rows are ASCII (O-8).
      return {{0, "a+bi", false, radio(settings.complexResult == ComplexResult::Rectangular)},
              {1, "Polar (r, theta)", false, radio(settings.complexResult == ComplexResult::Polar)}};
    case SettingAction::Fraction:
      return {{1, "Mixed", false, radio(settings.mixed)}, {0, "Improper", false, radio(!settings.mixed)}};
    case SettingAction::DecimalMark:
      return {{1, "Comma", false, radio(settings.comma)}, {0, "Dot", false, radio(!settings.comma)}};
    case SettingAction::Separators:
      return {{1, "On", false, radio(settings.separators)}, {0, "Off", false, radio(!settings.separators)}};
    case SettingAction::FormatMenu:
      return {{1, "Menu", false, radio(settings.formatMenu)}, {0, "Decimal toggle", false, radio(!settings.formatMenu)}};
    case SettingAction::ScientificKey:
      return {{1, "Power", false, radio(settings.scientificPower)}, {0, "Sci Notat", false, radio(!settings.scientificPower)}};
    default:
      return {};
  }
}

std::string settingsQuestion(int setting) {
  switch (setting) {
    case SettingAction::InputOutput:
      return "Input/Output?";
    case SettingAction::Angle:
      return "Angle Unit?";
    case SettingAction::Number:
      return "Number Format?";
    case SettingAction::Digits:
      return "Digits?";
    case SettingAction::Engineer:
      return "Engineer Symbol?";
    case SettingAction::Fraction:
      return "Fraction Result?";
    case SettingAction::Complex:
      return "Complex Result?";
    case SettingAction::DecimalMark:
      return "Decimal Mark?";
    case SettingAction::Separators:
      return "Digit Separator?";
    default:
      return "";
  }
}

bool applySettingOption(int setting, int choice, Settings& settings, bool& clearHistory) {
  clearHistory = false;
  switch (setting) {
    case SettingAction::InputOutput:
      if (choice < 0 || choice > 3) return false;
      settings.io = static_cast<IO>(choice);
      clearHistory = true;
      return true;
    case SettingAction::Angle:
      if (choice < 0 || choice > 2) return false;
      settings.angle = static_cast<Angle>(choice);
      return true;
    case SettingAction::Number:
      if (choice < 0 || choice > 3) return false;
      settings.number = static_cast<NumberFormat>(choice);
      if (settings.number == NumberFormat::Sci && settings.digits == 0) settings.digits = 1;
      return true;
    case SettingAction::Digits: {
      const bool sci = settings.number == NumberFormat::Sci;
      const int lo = sci ? 1 : 0, hi = sci ? 10 : 9;
      if (choice < lo || choice > hi) return false;
      settings.digits = static_cast<uint8_t>(choice);
      return true;
    }
    case SettingAction::Engineer:
      if (choice != 0 && choice != 1) return false;
      settings.engineer = choice == 1;
      return true;
    case SettingAction::Complex:
      if (choice != 0 && choice != 1) return false;
      settings.complexResult = choice == 1 ? ComplexResult::Polar : ComplexResult::Rectangular;
      return true;
    case SettingAction::Fraction:
      if (choice != 0 && choice != 1) return false;
      settings.mixed = choice == 1;
      return true;
    case SettingAction::DecimalMark:
      if (choice != 0 && choice != 1) return false;
      settings.comma = choice == 1;
      return true;
    case SettingAction::Separators:
      if (choice != 0 && choice != 1) return false;
      settings.separators = choice == 1;
      return true;
    case SettingAction::FormatMenu:
      if (choice != 0 && choice != 1) return false;
      settings.formatMenu = choice == 1;
      return true;
    case SettingAction::ScientificKey:
      if (choice != 0 && choice != 1) return false;
      settings.scientificPower = choice == 1;
      return true;
    default:
      return false;
  }
}

char variableName(int index) {
  if (index >= 0 && index < 6) return static_cast<char>('A' + index);
  if (index >= 6 && index < kVariableCount) return static_cast<char>('x' + index - 6);
  return '?';
}

int variableRow(int index) {
  if (index < 0 || index >= kVariableCount) return -1;
  return index / kVariableColumns;
}

int variableColumn(int index) {
  if (index < 0 || index >= kVariableCount) return -1;
  return index % kVariableColumns;
}

int variableIndexAt(int row, int col) {
  if (row < 0 || row >= kVariableRows || col < 0 || col >= kVariableColumns) return -1;
  const int index = row * kVariableColumns + col;
  return index < kVariableCount ? index : -1;
}

int moveVariableSelection(int current, GridMove move) {
  if (current < 0 || current >= kVariableCount) return 0;
  int row = variableRow(current), col = variableColumn(current);
  switch (move) {
    case GridMove::Left:
      if (col > 0) --col;
      break;
    case GridMove::Right:
      if (col + 1 < kVariableColumns && variableIndexAt(row, col + 1) >= 0) ++col;
      break;
    case GridMove::Up:
      if (row > 0) --row;
      break;
    case GridMove::Down:
      if (row + 1 < kVariableRows && variableIndexAt(row + 1, col) >= 0) ++row;
      break;
  }
  const int next = variableIndexAt(row, col);
  return next >= 0 ? next : current;
}

std::string valueLabel(const std::string& name, const ScalarValue& value, const Settings& settings) {
  Settings norm1 = settings;
  norm1.number = NumberFormat::Norm1;
  // The grid's built-in face has no angle sign, so complex values show a+bi.
  return name + "=" +
         (value.complex ? formatComplex(value.re, value.im, norm1, Format::Rectangular) : formatValue(value.re, norm1, Format::Decimal));
}

std::string variableLabel(int index, const ScalarValue& value, const Settings& settings) {
  return valueLabel(std::string(1, variableName(index)), value, settings);
}

bool MenuStack::push(const MenuContext& context) {  if (contexts_.size() >= kMenuStackMax) return false;
  contexts_.push_back(context);
  return true;
}

void MenuStack::pop() {
  if (!contexts_.empty()) contexts_.pop_back();
}

void MenuStack::clear() { contexts_.clear(); }

std::vector<CatalogEntry> quickCatalogEntries(bool verifyMode,AppId app){
  std::vector<CatalogEntry> out;
  for(const auto& row:catalogRootRows(verifyMode,app)){
    if(row.action<0||static_cast<size_t>(row.action)>=catalogData().size())continue;
    for(const auto& entry:catalogData()[row.action].entries)out.push_back(entry);
  }
  return out;
}

}  // namespace Calculator
