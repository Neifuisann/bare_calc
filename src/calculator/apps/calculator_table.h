#pragma once
#include "calculator/apps/calculator_calculate.h"
namespace Calculator {
// One real scalar per cell, no Memory/Result or natural-layout tree per row.
struct TableRow {
  Value x,f,g;
  bool hasX=false,hasF=false,hasG=false;
  Error fError=Error::None,gError=Error::None;
};
struct TableData { std::vector<TableRow> rows; };
enum class TableOperation : uint8_t { Generate, Recalculate, EditX, Verify };
struct TableTask {
  TableOperation operation=TableOperation::Generate;
  uint8_t type=0,columns=3;
  std::array<Value,3> range;
  std::shared_ptr<const TableData> input;
  int row=0,col=0;
  bool verify=false;
};
size_t tableCapacity(uint8_t type);
bool composedFunction(const std::string& source);
Result evaluateTable(const TableTask&,const Request&,const std::function<bool()>& cancelled={},const std::function<void()>& cooperate={});
namespace TableApp {
void enter();
void pause();
void press(ExpressionInput::Key key);
void adopt(const Job&,Result&);
// Poll only semantic revisions; never reactive to arbitrary variable/settings changes.
bool service();
bool caretVisible();
std::vector<uint8_t> render(bool caretOn);
PanelIcons::State icons();
size_t stateBytes();
size_t heapBytes();
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
const TableData* data();
bool needsRecalculate();
#endif
}
}
