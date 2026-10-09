#include "calculator/apps/calculator_spreadsheet.h"
#include "system/psram_allocator.h"
#include <algorithm>
#include <bitset>
#include <cctype>

namespace Calculator {
namespace {
struct Reference {
  int row = -1, col = -1;
  bool fixedRow = false, fixedCol = false;
  size_t end = 0;
};
bool letter(char ch) { return std::isalpha(static_cast<unsigned char>(ch)); }
bool digit(char ch) { return ch >= '0' && ch <= '9'; }
// Token boundaries keep scientific exponents (1E10) and function names intact.
bool reference(const std::string& s, size_t at, Reference& out) {
  if (at >= s.size() || (at && (letter(s[at-1]) || s[at-1] == '$'))) return false;
  Reference r; size_t p = at;
  if (s[p] == '$') { r.fixedCol = true; if (++p == s.size()) return false; }
  if (!((s[p] >= 'A' && s[p] <= 'E') || s[p] == '?')) return false;
  // E after a decimal number is the engine's exponent token, not column E.
  if (s[p] == 'E' && at && (digit(s[at-1]) || s[at-1] == '.')) return false;
  r.col = s[p] == '?' ? -1 : s[p] - 'A'; ++p;
  if (p < s.size() && s[p] == '$') { r.fixedRow = true; ++p; }
  if (p == s.size() || (!digit(s[p]) && s[p] != '?')) return false;
  if (s[p] == '?') ++p;
  else {
    unsigned n = 0;
    while (p < s.size() && digit(s[p])) { n = std::min(1000u, n*10 + s[p]-'0'); ++p; }
    if (n >= 1 && n <= kSheetRows) r.row = n-1;
  }
  out = r; out.end = p; return true;
}
bool valid(const Reference& r) { return r.row >= 0 && r.col >= 0; }
int index(int row, int col) { return row*kSheetCols+col; }
void spaces(const std::string& s, size_t& p) { while (p<s.size() && s[p]==' ') ++p; }
bool rangeAt(const std::string& s, size_t& p, SpreadsheetRange& out) {
  Reference a,b; spaces(s,p);
  if (!reference(s,p,a) || !valid(a)) return false;
  p = a.end; spaces(s,p);
  if (p==s.size() || s[p++]!=':') return false;
  spaces(s,p); if (!reference(s,p,b) || !valid(b)) return false;
  p=b.end; spaces(s,p);
  if (a.row>b.row || a.col>b.col) return false;
  out={a.row,a.col,b.row,b.col}; return true;
}
bool aggregateAt(const std::string& s, size_t at, std::string& name, SpreadsheetRange& range, size_t& end) {
  if (at && letter(s[at-1])) return false;
  for (const char* command : {"Min", "Max", "Mean", "Sum"}) {
    const std::string prefix=std::string(command)+"(";
    if (s.compare(at,prefix.size(),prefix)) continue;
    size_t p=at+prefix.size();
    if (!rangeAt(s,p,range)) return false;
    if (p<s.size() && s[p]==')') ++p;
    else if (p!=s.size()) return false; // Native EXE closes a trailing call.
    name=command; end=p; return true;
  }
  return false;
}
struct Dependencies { std::bitset<kSheetCells> cells; };
Dependencies dependencies(const std::string& s) {
  Dependencies out;
  for (size_t p=0;p<s.size();) {
    SpreadsheetRange range; size_t end; std::string name; Reference r;
    if (aggregateAt(s,p,name,range,end)) {
      for (int row=range.row;row<=range.endRow;++row)
        for (int col=range.col;col<=range.endCol;++col) out.cells[index(row,col)]=true;
      p=end;
    } else if (reference(s,p,r)) {
      if (valid(r)) out.cells[index(r.row,r.col)]=true;
      p=r.end;
    } else ++p;
  }
  return out;
}
struct Evaluation { Decimal value; Error error=Error::None; };
Evaluation expression(const std::string& source, const SpreadsheetData& sheet, const Request& request,
                      const std::function<bool()>& cancelled, const std::function<void()>& cooperate) {
  if(source.empty())return {{},Error::Syntax};
  std::string text;
  for (size_t p=source[0]=='='?1:0;p<source.size();) {
    SpreadsheetRange range; size_t end; std::string name; Reference r;
    if (aggregateAt(source,p,name,range,end)) {
      Decimal value; int count=0;
      for (int row=range.row;row<=range.endRow;++row) for (int col=range.col;col<=range.endCol;++col) {
        const auto& cell=sheet.cells[index(row,col)];
        if (cell.error!=Error::None) return {{},cell.error};
        const Decimal v=cell.occupied?cell.value:Decimal(0);
        if (name=="Min") { if (!count || v<value) value=v; }
        else if (name=="Max") { if (!count || v>value) value=v; }
        else value=value+v;
        ++count;
      }
      if (name=="Mean") value=value/Decimal(count);
      text+='(';text+=value.str(40);text+=')';p=end;
    } else if (reference(source,p,r)) {
      if (!valid(r)) return {{},Error::Range};
      const auto& cell=sheet.cells[index(r.row,r.col)];
      if (cell.error!=Error::None) return {{},cell.error};
      text+='(';text+=cell.occupied?cell.value.str(40):"0";text+=')';p=r.end;
    } else text+=source[p++];
  }
  // Spreadsheet constants/formulas read shared variables/functions but never
  // write Ans, PreAns, history or global memory through an embedded store.
  Request r=request; r.expression=text; r.verify=false;
  const auto result=evaluate(r,cancelled,cooperate);
  const auto rounded=result.value.number.rounded(10);
  return {rounded,result.error==Error::None&&rounded.abs()>=Decimal("1E100")?Error::Math:result.error};
}
// Resolve formulas iteratively: no cell-recursion on the device stack.
// Once no node is ready, the remaining subgraph contains a circular reference.
Error recalculate(SpreadsheetData& sheet, const Request& request,
                  const std::function<bool()>& cancelled, const std::function<void()>& cooperate) {
  std::vector<Dependencies,PsramMemory::Allocator<Dependencies>> deps(kSheetCells);
  std::array<bool,kSheetCells> done{}; int remaining=0;
  for (int i=0;i<kSheetCells;++i) {
    auto& c=sheet.cells[i]; done[i]=c.formula.empty();
    if (!done[i]) { deps[i]=dependencies(c.formula); c.error=Error::None; ++remaining; }
  }
  while (remaining) {
    bool progress=false;
    for (int i=0;i<kSheetCells;++i) if (!done[i]) {
      if (cancelled && cancelled()) return Error::Cancelled;
      if (cooperate) cooperate();
      bool ready=true;
      for (int j=0;j<kSheetCells;++j) if (deps[i].cells[j]&&!done[j]) { ready=false;break; }
      if (!ready) continue;
      auto& c=sheet.cells[i]; const auto v=expression(c.formula,sheet,request,cancelled,cooperate);
      c.value=v.value;c.error=v.error;done[i]=true;--remaining;progress=true;
    }
    if (!progress) {
      for (int i=0;i<kSheetCells;++i) if (!done[i]) sheet.cells[i].error=Error::Circular;
      return Error::Circular;
    }
  }
  return Error::None;
}
}
std::string spreadsheetReference(int row,int col) { return std::string(1,static_cast<char>('A'+col))+std::to_string(row+1); }
bool spreadsheetRange(const std::string& source, SpreadsheetRange& out) {
  size_t p=0;return rangeAt(source,p,out)&&p==source.size();
}
size_t spreadsheetInputBytes(const std::string& text) {
  size_t bytes=0;
  for (size_t p=0;p<text.size();) {
    if (text[p]==' ') {++p;continue;}
    if (letter(text[p])) {
      size_t end=p;while(end<text.size()&&letter(text[end]))++end;
      if (end<text.size()&&text[end]=='(') { ++bytes;p=end+1;continue; }
    }
    ++bytes;
    // One display symbol, irrespective of its UTF-8 encoding.
    ++p;while(p<text.size()&&(static_cast<unsigned char>(text[p])&0xc0)==0x80)++p;
  }
  return bytes;
}
size_t spreadsheetUsedBytes(const SpreadsheetData& data) {
  size_t n=0;for(const auto& c:data.cells)if(c.occupied)n+=c.formula.empty()?14:15+spreadsheetInputBytes(c.formula);return n;
}
std::string spreadsheetCellText(const Decimal& value,const Settings& settings){
  const auto scalar=Value::approximate(value);
  const auto full=formatValue(scalar,settings,Format::Decimal);
  if(full.size()<=6)return full;
  // Six significant decimal digits where they fit, then a bounded scientific
  // readout. Never clip away an exponent and change the apparent magnitude.
  auto compact=value.str(6);
  if(settings.comma)std::replace(compact.begin(),compact.end(),'.',',');
  if(compact.size()<=6)return compact;
  Settings sci=settings;sci.number=NumberFormat::Sci;sci.engineer=false;
  for(int digits=4;digits>=1;--digits){
    sci.digits=digits;compact=formatValue(scalar,sci,Format::Decimal);
    compact.erase(std::remove(compact.begin(),compact.end(),'+'),compact.end());
    if(compact.size()<=7)return compact;
  }
  return compact;
}
std::string relocateSpreadsheetFormula(const std::string& source,int rows,int cols) {
  std::string text;
  for(size_t p=0;p<source.size();) {
    Reference r;
    if (!reference(source,p,r)) {text+=source[p++];continue;}
    const int row=r.row<0?-1:r.row+(r.fixedRow?0:rows), col=r.col<0?-1:r.col+(r.fixedCol?0:cols);
    if(r.fixedCol)text+='$';text+=col>=0&&col<kSheetCols?static_cast<char>('A'+col):'?';
    if(r.fixedRow)text+='$';text+=row>=0&&row<kSheetRows?std::to_string(row+1):"?";
    p=r.end;
  }
  return text;
}
Result evaluateSpreadsheet(const SpreadsheetTask& task,const Request& request,
                            const std::function<bool()>& cancelled,const std::function<void()>& cooperate) {
  Result result;result.memory=request.memory;
  auto sheet=std::allocate_shared<SpreadsheetData>(PsramMemory::Allocator<SpreadsheetData>());
  if(task.input)*sheet=*task.input;
  const auto& range=task.range;
  if(range.row<0||range.col<0||range.endRow>=kSheetRows||range.endCol>=kSheetCols||range.endRow<range.row||range.endCol<range.col) {result.error=Error::Range;return result;}
  if(task.sourceRow<0||task.sourceRow>=kSheetRows||task.sourceCol<0||task.sourceCol>=kSheetCols){result.error=Error::Range;return result;}
  const int dest=index(range.row,range.col),src=index(task.sourceRow,task.sourceCol);
  const auto op=task.operation;
  if(op==SpreadsheetOperation::Clear)for(auto& c:sheet->cells)c=SpreadsheetCell{};
  else if(op==SpreadsheetOperation::Erase)sheet->cells[dest]=SpreadsheetCell{};
  else if(op==SpreadsheetOperation::Copy||op==SpreadsheetOperation::Cut) {
    auto cell=sheet->cells[src];
    if(op==SpreadsheetOperation::Copy&&!cell.formula.empty())cell.formula=relocateSpreadsheetFormula(cell.formula,range.row-task.sourceRow,range.col-task.sourceCol);
    if(op==SpreadsheetOperation::Cut&&src!=dest)sheet->cells[src]=SpreadsheetCell{};
    sheet->cells[dest]=std::move(cell);
  } else if(op==SpreadsheetOperation::Set||op==SpreadsheetOperation::FillFormula||op==SpreadsheetOperation::FillValue) {
    std::string source=task.expression;
    if(source.empty()){result.error=Error::Syntax;return result;}
    if(op==SpreadsheetOperation::FillFormula&&source[0]!='=')source='='+source;
    if(op==SpreadsheetOperation::FillValue&&source[0]=='=')source.erase(0,1);
    if(source.empty()){result.error=Error::Syntax;return result;}
    if(spreadsheetInputBytes(source)>kSheetInput){result.error=Error::Memory;return result;}
    // Fill Value evaluates each adjusted expression against the preceding
    // committed sheet, then freezes its result just like individual entries.
    for(int row=range.row;row<=range.endRow;++row)for(int col=range.col;col<=range.endCol;++col) {
      if(cancelled&&cancelled()){result.error=Error::Cancelled;return result;}
      if(cooperate)cooperate();
      const auto adjusted=relocateSpreadsheetFormula(source,row-range.row,col-range.col);
      if(spreadsheetInputBytes(adjusted)>kSheetInput){result.error=Error::Memory;return result;}
      auto& c=sheet->cells[index(row,col)];
      if(adjusted[0]=='='){c=SpreadsheetCell{};c.occupied=true;c.formula=adjusted;}
      else {
        const auto v=expression(adjusted,*sheet,request,cancelled,cooperate);
        if(v.error!=Error::None){result.error=v.error;return result;}
        c=SpreadsheetCell{};c.occupied=true;c.value=v.value;
      }
    }
  }
  if(spreadsheetUsedBytes(*sheet)>kSheetMemory){result.error=Error::Memory;return result;}
  for(const auto& c:sheet->cells)if(!c.formula.empty()&&spreadsheetInputBytes(c.formula)>kSheetInput){result.error=Error::Memory;return result;}
  const bool inserting=op==SpreadsheetOperation::Set||op==SpreadsheetOperation::FillFormula||op==SpreadsheetOperation::Copy||op==SpreadsheetOperation::Cut;
  // Validate every newly inserted formula even with Auto Calc off. Existing
  // formulas keep their cached values until the explicit Recalculate action.
  if(task.autoCalc||op==SpreadsheetOperation::Recalculate) {
    const auto e=recalculate(*sheet,request,cancelled,cooperate);
    if(e==Error::Cancelled||e==Error::Circular){result.error=e;return result;}
  } else if(inserting) {
    for(int row=range.row;row<=range.endRow;++row)for(int col=range.col;col<=range.endCol;++col) {
      auto& c=sheet->cells[index(row,col)];if(c.formula.empty())continue;
      auto v=expression(c.formula,*sheet,request,cancelled,cooperate);c.value=v.value;c.error=v.error;
    }
    // Cycle checking is independent of cached evaluation.
    auto check=std::allocate_shared<SpreadsheetData>(PsramMemory::Allocator<SpreadsheetData>(),*sheet);
    const auto e=recalculate(*check,request,cancelled,cooperate);
    if(e==Error::Circular||e==Error::Cancelled){result.error=e;return result;}
  }
  if(op==SpreadsheetOperation::Set||op==SpreadsheetOperation::FillFormula) {
    for(int row=range.row;row<=range.endRow;++row)for(int col=range.col;col<=range.endCol;++col)
      if(sheet->cells[index(row,col)].error!=Error::None){result.error=sheet->cells[index(row,col)].error;return result;}
  }
  result.spreadsheet=std::move(sheet);return result;
}
} // namespace Calculator
