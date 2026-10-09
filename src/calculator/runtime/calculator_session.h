#pragma once
#include "calculator/math/calculator_aggregate.h"
#include "calculator/math/calculator_engine.h"
#include <cstdint>
#include <string>
#include <vector>

// Calculator state shared by every app, and the single place that decides what
// survives an app switch, reset or setting change
// (docs/ARCHITECTURE.md). Screen state -- an editor
// draft, a visible result, a menu stack -- stays with the app that owns it.
namespace Calculator {

// Home cell order: the value is the HomeMenu index (calculator_home.h), so
// the four exact Home captures keep defining the dispatch.
enum class AppId : uint8_t {
  Calculate, Statistics, Distribution, Spreadsheet, Table, Equation, Inequality,
  Complex, BaseN, Matrix, Vector, Ratio, MathBox
};
inline AppId appAtHomeIndex(unsigned index) { return static_cast<AppId>(index); }

// Only events that clear something. Showing Home over a paused app and
// suspending the calculator retain everything, so they are not
// transitions. Native ON is not scanned (plan O-10) and has no event yet.
enum class TransitionReason : uint8_t {
  HomeOpen,             // replay history only (manual p.40)
  OtherAppLaunch,       // PreAns, Verify, history, MatAns/VctAns, Base-N base
  InputStyleChanged,    // Math <-> Line: f/g and history (manual p.77)
  OutputStyleChanged,   // same input style: history only
  VariableMemoryReset,  // A-F/x/y/z, Ans, PreAns; keeps settings and f/g
  SettingsDataReset,    // settings, f/g, Verify, history, aggregates; keeps scalars
  InitializeAll,        // every calculator default
  TableTypeChanged,    // Table data only; settings and definitions retained
  NativeOn,            // manual ON; input hardware currently exposes no ON key
};

// A history entry's compact answer, as displayed. Browsing shows it; only EXE
// re-evaluates. The execution format travels with it (SHIFT+EXE decimal
// replays as shown); later FORMAT-menu views stay current-result state.
struct ResultSummary {
  Value value;
  Value second;
  Value imag;
  bool pair = false;
  bool complex = false;
  bool verified = false;
  bool truth = false;
  Error error = Error::None;
  size_t position = 0;  // source byte offset, as in Result; never an edit cursor
  Format displayedFormat = Format::Standard;  // execution format (P12 R-01)
  // A CAS answer (Result::formula): natural-display source, shared, never
  // re-evaluated; `value` is then a placeholder.
  std::shared_ptr<const std::string> formula;
};
// Never embeds Memory/Job/editor state (~2.3 KB each, ~74 KB over 32 entries);
// the asserts are the tripwire.
static_assert(sizeof(ResultSummary) < sizeof(Result),
              "ResultSummary must stay compact and never contain a Memory snapshot");
static_assert(sizeof(ResultSummary) <= 512, "ResultSummary budget: values plus flags only");
struct HistoryEntry {
  std::string source;
  ResultSummary answer;
};
// View-sensitive by design (Standard vs SHIFT+EXE Decimal differ). Tests only:
// pushHistory never dedupes.
inline bool operator==(const ResultSummary& a, const ResultSummary& b) {
  return a.value == b.value && a.second == b.second && a.imag == b.imag &&
         a.pair == b.pair && a.complex == b.complex && a.verified == b.verified &&
         a.truth == b.truth && a.error == b.error && a.position == b.position &&
         a.displayedFormat == b.displayedFormat &&
         (a.formula ? b.formula && *a.formula == *b.formula : !b.formula);
}
inline bool operator==(const HistoryEntry& a, const HistoryEntry& b) {
  return a.source == b.source && a.answer == b.answer;
}
// The 8 KB byte cap is the controlling limit; it counts retained memory (inline
// storage, source, dynamic Value payload, spare capacity). The count is only a
// sanity limit.
constexpr size_t kHistoryMaxBytes = 8192;
constexpr size_t kHistoryMaxEntries = 32;
size_t historyValueHeap(const Value& value);
size_t historyEntryBytes(const HistoryEntry& entry);
size_t historyPayloadBytes(const std::vector<HistoryEntry>& history);
// Build the retained summary from a successful Result (copies values only).
ResultSummary toSummary(const Result& result);
ResultSummary toSummary(const Result& result, Format displayedFormat);
// Evicts oldest-first until the entry fits; an entry over the whole budget is
// DROPPED (truncating source would corrupt recall; the live Result still shows).
bool pushHistory(std::vector<HistoryEntry>& history, HistoryEntry entry);

// Distribution parameters, retained by name across screens until Settings&Data
// or Initialize All (880-MAN). Never persisted. Defaults provisional (O-2):
// 0, except p/Area = 0.5 and sigma = 1.
struct DistributionParameters {
  Decimal x, n, mu, lower, upper, lambda;
  Decimal p{Decimal("0.5")}, sigma{Decimal(1)}, area{Decimal("0.5")};
};

struct CalculatorSession {
  Settings settings;
  Memory memory;
  bool verify = false;
  // Calculate replay: HistoryEntry in execution order, bounded by the 8 KB cap.
  std::vector<HistoryEntry> history;
  AppId app = AppId::Calculate;
  // MatA-D/MatAns and VctA-D/VctAns; the answers clear on another app's launch.
  Aggregates aggregates;
  // Base-N's current base: kept while Base-N is paused, Decimal after another
  // app (plan O-6 provisional). Not persisted.
  uint8_t base = 10;
  // Bumped by every change the store must save (settings, memory, f/g); the
  // app persists once it has been stable for the debounce window.
  uint32_t revision = 0;
  // Retained, never persisted. Epochs invalidate controller-owned rows without
  // coupling the session to rendering.
  uint8_t tableType = 0; // dual, f, g
  std::array<Value,3> tableRange{{Value(1),Value(5),Value(1)}};
  uint32_t tableEpoch = 0;
  uint32_t ratioEpoch = 0;
  DistributionParameters distParams;
  uint32_t statEpoch = 0;
  uint32_t distEpoch = 0;
  // fx-880BTG retains Spreadsheet across app switches; ON/reset/power clears.
  uint32_t spreadsheetEpoch = 0;
};

// The one session the overlay runs on.
CalculatorSession& calculatorSession();

void applyTransition(CalculatorSession& session, TransitionReason reason);
// Home commit of `app`: reopening the running app retains everything,
// launching a different one applies OtherAppLaunch first.
void launchApp(CalculatorSession& session, AppId app);
bool inputStyleChanged(IO before, IO after);

}  // namespace Calculator
