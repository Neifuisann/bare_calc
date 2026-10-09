#include "calculator/runtime/calculator_session.h"
#include "calculator/runtime/calculator_storage.h"

namespace Calculator {
namespace {
// Undefining is enough (rows == 0), and needs no 6 KB temporary on the loop stack.
void clearAggregates(CalculatorSession& session) {
  for (auto& m : session.aggregates.matrices) m.rows = m.cols = 0;
  for (auto& v : session.aggregates.vectors) v.rows = v.cols = 0;
  session.base = 10;
}
}  // namespace

CalculatorSession& calculatorSession() {
  static auto& session = CalculatorStorage::makeState<CalculatorSession>();
  return session;
}

// Decimal is inline; the exact form owns heap only past int64 rationals
// (big integers, radicals, constants), counted in full even when shared.
size_t historyValueHeap(const Value& value) { return value.symbolic.heapBytes(); }

size_t historyEntryBytes(const HistoryEntry& entry) {
  size_t bytes = sizeof(HistoryEntry);
  // Conservative portable rule: count the retained characters, never the
  // allocator's capacity or a hardcoded small-string threshold. Buffered
  // sources overcount by a few bytes rather than risk undercounting on a
  // standard-library layout change.
  if (!entry.source.empty()) bytes += entry.source.size() + 1;
  bytes += historyValueHeap(entry.answer.value);
  bytes += historyValueHeap(entry.answer.second);
  bytes += historyValueHeap(entry.answer.imag);
  // A CAS formula: its characters plus the shared control block and string.
  if (entry.answer.formula) bytes += entry.answer.formula->size() + 1 + sizeof(std::string) + 32;
  return bytes;
}

size_t historyPayloadBytes(const std::vector<HistoryEntry>& history) {
  size_t bytes = 0;
  if (history.capacity() > history.size())
    bytes += (history.capacity() - history.size()) * sizeof(HistoryEntry);
  for (const auto& entry : history) bytes += historyEntryBytes(entry);
  return bytes;
}

ResultSummary toSummary(const Result& result) { return toSummary(result, Format::Standard); }

ResultSummary toSummary(const Result& result, Format displayedFormat) {
  ResultSummary summary;
  summary.value = result.value;
  summary.second = result.second;
  summary.imag = result.imag;
  summary.pair = result.pair;
  summary.complex = result.complex;
  summary.verified = result.verified;
  summary.truth = result.truth;
  summary.error = result.error;
  summary.position = result.position;
  summary.displayedFormat = displayedFormat;
  summary.formula = result.formula;
  return summary;
}

bool pushHistory(std::vector<HistoryEntry>& history, HistoryEntry entry) {
  if (historyEntryBytes(entry) > kHistoryMaxBytes) return false;  // DROP, never truncate
  while (!history.empty()) {
    const bool countBound = history.size() >= kHistoryMaxEntries;
    // Prospective payload after the push: it consumes one spare slot when one
    // exists, so only unspared growth adds storage. Exact, no guard band.
    size_t grown = historyPayloadBytes(history) + historyEntryBytes(entry);
    if (history.capacity() > history.size()) grown -= sizeof(HistoryEntry);
    if (!countBound && grown <= kHistoryMaxBytes) break;
    history.erase(history.begin());  // oldest-first eviction
  }
  // Removing the final old entry does not release the vector's spare slots.
  // A large new answer can fit by itself while that retained capacity would
  // push it over the byte budget. Drop only that otherwise unusable storage.
  if (history.empty() && history.capacity() &&
      historyPayloadBytes(history) - sizeof(HistoryEntry) + historyEntryBytes(entry) > kHistoryMaxBytes)
    std::vector<HistoryEntry>().swap(history);
  // Linear growth only: geometric overshoot would quietly hold several KB past
  // the accounted payload. Pushes happen once per EXE, so this stays cheap.
  if (history.capacity() < history.size() + 1) history.reserve(history.size() + 1);
  history.push_back(std::move(entry));
  return true;
}

void applyTransition(CalculatorSession& session, TransitionReason reason) {
  Memory& memory = session.memory;
  if(reason==TransitionReason::SettingsDataReset||reason==TransitionReason::InitializeAll||reason==TransitionReason::NativeOn)++session.spreadsheetEpoch;
  if(reason==TransitionReason::OtherAppLaunch||reason==TransitionReason::SettingsDataReset||reason==TransitionReason::InitializeAll){
    ++session.tableEpoch;++session.ratioEpoch;
    // P17: another app clears Statistics data and Distribution lists
    // (provisional D for Statistics; 880-MAN for Distribution lists).
    // Named distribution parameters survive an app switch (880-MAN).
    ++session.statEpoch;++session.distEpoch;
    session.tableRange={{Value(1),Value(5),Value(1)}};
    if(reason!=TransitionReason::OtherAppLaunch){
      session.tableType=0;
      session.distParams=DistributionParameters{};
    }
  }
  if(reason==TransitionReason::InputStyleChanged||reason==TransitionReason::OutputStyleChanged){
    ++session.tableEpoch;
    if(session.app==AppId::Table){memory.f.clear();memory.g.clear();}
  }
  switch (reason) {
    case TransitionReason::TableTypeChanged:
      ++session.tableEpoch;return;
    case TransitionReason::NativeOn:
      ++session.ratioEpoch;
      // P17 provisional: LogicalOn clears Statistics data, preserves the
      // Distribution list (O-1/O-2 PENDING).
      ++session.statEpoch;
      if(session.app==AppId::Table){++session.tableEpoch;memory.f.clear();memory.g.clear();session.verify=false;}
      return;
    case TransitionReason::HomeOpen:
      break;
    case TransitionReason::OtherAppLaunch:
      // PreAns belongs to Calculate; another app never inherits or advances it.
      memory.preAns = Value(0);
      session.verify = false;
      session.aggregates.matrices[kAnswerSlot].rows = 0;
      session.aggregates.vectors[kAnswerSlot].rows = 0;
      session.base = 10;
      break;
    case TransitionReason::InputStyleChanged:
      memory.f.clear();
      memory.g.clear();
      break;
    case TransitionReason::OutputStyleChanged:
      break;
    // Resets go field by field: this runs on the loop task's 8 KB stack, and
    // a Memory{} or variables{} temporary is ~2 KB each.
    case TransitionReason::VariableMemoryReset:
      memory.ans = Value(0);
      memory.preAns = Value(0);
      for (auto& variable : memory.variables) variable = ScalarValue{};
      return;  // history and Verify are not variable memory (capacity retained)
    case TransitionReason::SettingsDataReset:
      session.settings = Settings{};
      memory.f.clear();
      memory.g.clear();
      session.verify = false;
      clearAggregates(session);
      break;
    case TransitionReason::InitializeAll:
      session.settings = Settings{};
      memory.ans = Value(0);
      memory.preAns = Value(0);
      for (auto& variable : memory.variables) variable = ScalarValue{};
      memory.f.clear();
      memory.g.clear();
      session.verify = false;
      clearAggregates(session);
      break;
  }
  session.history.clear();
  // Transitions are infrequent menu actions, never keypresses: release the
  // peak vector capacity so clearing observably drops retained memory.
  session.history.shrink_to_fit();
}

void launchApp(CalculatorSession& session, AppId app) {
  if (app == session.app) return;
  applyTransition(session, TransitionReason::OtherAppLaunch);
  session.app = app;
  ++session.revision;  // PreAns is persisted
}

bool inputStyleChanged(IO before, IO after) {
  return (before <= IO::MathDecimal) != (after <= IO::MathDecimal);
}

}  // namespace Calculator
