#include "calculator/math/calculator_engine.h"
#include "calculator/ui/calculator_render.h"
#include "calculator/runtime/calculator_session.h"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// P11 history: source + compact answer, 8 KB cap with oldest-first eviction
// and DROP of single oversize entries, HistoryView browsing, empty-line
// recall after any clear, long-result scroll viewports, history icons, and
// A-13 BACK-at-end.
//
// The unit part runs in every app-test binary. The controller part needs the
// detached CALCULATOR_REFERENCE_TEST snapshot, like the P10 header, and runs
// in reference.exe --self-test.
void p11HistoryUnitChecks() {
  using namespace Calculator;
  int checks = 0;
  auto require = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(std::string("P11unit: ") + label);
  };
  // Measured host (mingw64) values are printed for the report; the relations
  // below are the portable guards. Memory ~2.3 KB must never ride per entry.
  require(sizeof(ResultSummary) < sizeof(Result), "summary stays smaller than Result");
  require(sizeof(ResultSummary) <= 512, "summary budget is values plus flags");
  require(sizeof(Memory) >= 2000, "Memory snapshot cost documents the no-copy rule");
  require(historyValueHeap(Value(0)) == 0, "Value owns no heap payload today");
  std::cout << "P11 sizes: Result=" << sizeof(Result) << " Summary=" << sizeof(ResultSummary)
            << " Memory=" << sizeof(Memory) << " Entry=" << sizeof(HistoryEntry) << "\n";
  // toSummary keeps exactly what the current renderer reads, nothing else.
  {
    Request request{"1+1", Settings{}, Memory{}, false, 1};
    const Result result = evaluate(request);
    require(result.error == Error::None, "eval 1+1");
    const ResultSummary summary = toSummary(result);
    require(summary.error == Error::None && !summary.pair && !summary.complex && !summary.verified,
            "summary flags");
    require(summary.value.number == Decimal(2), "summary value");
    require(summary.position == result.position, "summary position carried");
    require(summaryText(summary, Settings{}, Format::Standard) == "2", "summary text round-trip");
    require(summary == toSummary(result), "summary equality");
    require(!(summary == ResultSummary{}), "summary inequality");
    HistoryEntry kept{"1+1", summary};
    require(kept == HistoryEntry{"1+1", summary}, "entry equality");
  }
  // Accounting, DROP policy, oldest-first eviction, clearing.
  {
    std::vector<HistoryEntry> history;
    require(historyPayloadBytes(history) == 0, "empty payload is zero");
    HistoryEntry small{"1+1", ResultSummary{}};
    require(pushHistory(history, std::move(small)), "small entry retained");
    require(history.size() == 1 && historyPayloadBytes(history) <= kHistoryMaxBytes, "payload bounded");
    const size_t before = history.size();
    HistoryEntry big{std::string(9000, '9'), ResultSummary{}};
    require(!pushHistory(history, std::move(big)), "oversize entry is DROPped, never truncated");
    require(history.size() == before, "DROP retains nothing");
    for (int i = 0; i < 60; ++i) {
      HistoryEntry entry{"expr" + std::to_string(i), ResultSummary{}};
      require(pushHistory(history, std::move(entry)), "bounded entry fits");
      require(historyPayloadBytes(history) <= kHistoryMaxBytes, "cap holds under pressure");
      require(history.size() <= kHistoryMaxEntries, "count sanity holds");
    }
    require(history.back().source == "expr59", "newest retained");
    require(history.front().source != "expr0", "oldest evicted first");
    for (size_t i = 1; i < history.size(); ++i) {
      const int prev = std::stoi(history[i - 1].source.substr(4));
      const int next = std::stoi(history[i].source.substr(4));
      require(next == prev + 1, "eviction preserves order");
    }
    // Eviction keeps vector capacity. A large, individually valid answer
    // must shed that spare storage as well as the old entries to fit.
    const std::string largeSource(kHistoryMaxBytes - sizeof(HistoryEntry) - 1, '9');
    require(pushHistory(history, HistoryEntry{largeSource, ResultSummary{}}), "large entry replaces small entries");
    require(history.size() == 1 && history.back().source == largeSource, "large entry retained alone");
    require(historyPayloadBytes(history) <= kHistoryMaxBytes, "spare capacity cannot exceed history cap");
    std::cout << "P11 history: cap=" << kHistoryMaxBytes << " payload=" << historyPayloadBytes(history)
              << " retained=" << history.size() << " capacity=" << history.capacity() << "\n";
    history.clear();
    history.shrink_to_fit();
    require(historyPayloadBytes(history) == 0 && history.capacity() == 0, "clearing drops memory");
  }
  std::cout << "P11 history unit: " << checks << " checks passed\n";
}

#ifdef CALCULATOR_REFERENCE_TEST
void p11HistoryControllerChecks() {
  using namespace Calculator;
  int checks = 0;
  auto require = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(std::string("P11: ") + label);
  };
  auto keys = [&](std::initializer_list<K> list) {
    for (K k : list) {
      CalculatorApp::press(k);
      drain();
    }
  };
  auto snap = [&] { return CalculatorApp::snapshot(frame); };
  auto& session = calculatorSession();
  // Cold start: Home, clamped navigation to a known cell, launch, clear. The
  // HomeOpen transition itself clears replay history.
  auto cold = [&](bool complex = false) {
    CalculatorApp::openHome();
    service();
    session.settings = Settings{};
    session.verify = false;
    for (int i = 0; i < 5; ++i) {
      CalculatorApp::press(K::Up);
      service();
    }
    for (int i = 0; i < 3; ++i) {
      CalculatorApp::press(K::Left);
      service();
    }
    if (complex) keys({K::Down, K::Down, K::Right});
    keys({K::Okay, K::Clear});
    session.history.clear();
    session.history.shrink_to_fit();
  };
  CalculatorPlatform::fakeMillis = 0;
  // C53: 1 EXE 2 EXE UP shows 1...1 with no caret; UP clamps; DOWN shows 2...2.
  cold();
  keys({K::Digit1, K::Execute});
  require(snap().screen == "Result" && snap().resultText == "1" && snap().hasResult, "C53 live result 1");
  keys({K::Digit2, K::Execute});
  keys({K::Up});
  {
    const auto s = snap();
    require(s.screen == "HistoryView", "C53 UP enters HistoryView, never editing");
    require(s.text == "1" && s.resultText == "1", "C53 expression plus stored answer");
    require(!s.caret && s.caretKind == "Hidden", "C53 no caret while browsing");
    require(s.hasResult, "C53 history view reports a result");
    require(s.historyCursor == 0, "C53 oldest index");
    require(!s.icons.arrUp && s.icons.arrDown, "C53 oldest is DOWN-only");
  }
  const auto oldestFrame = snap().frame;
  keys({K::Up});
  require(snap().historyCursor == 0 && snap().frame == oldestFrame, "C53 UP clamps at oldest");
  keys({K::Down});
  {
    const auto s = snap();
    require(s.screen == "HistoryView" && s.text == "2" && s.resultText == "2", "C53 DOWN steps to newest history");
    require(!s.caret, "C53 newest history has no caret");
    require(s.icons.arrUp && !s.icons.arrDown, "C53 newest is UP-only");
  }
  keys({K::Down});
  {
    const auto s = snap();
    require(s.screen == "Result" && s.text == "2" && s.resultText == "2", "C53 DOWN at newest returns to live result");
  }
  // C55: UP LEFT 3 edits the recalled source at its end.
  keys({K::Up, K::Left});
  {
    const auto s = snap();
    require(s.screen == "Entry" && s.text == "1" && s.cursor == 1, "C55 LEFT edits history source at end");
    require(s.caret && !s.hasResult, "C55 answer gone, caret back");
  }
  keys({K::Digit3});
  require(snap().text == "13", "C55 typing gives 13, never 31");
  // C56: UP EXE re-executes the historical source through the normal path.
  cold();
  keys({K::Digit1, K::Execute, K::Clear, K::Digit2, K::Execute, K::Up, K::Execute});
  {
    const auto s = snap();
    require(s.screen == "Result" && s.resultText == "1" && !s.caret, "C56 re-executes 1 = 1");
    require(session.history.size() == 3, "C56 re-execution appends one entry");
  }
  // C54: 1 EXE AC UP browses 1...1 from the empty line; DOWN returns there.
  cold();
  keys({K::Digit1, K::Execute});
  require(snap().icons.arrUp && !snap().icons.arrDown, "C44 single result advertises UP");
  keys({K::Clear});
  {
    const auto s = snap();
    require(s.screen == "Entry" && s.text.empty(), "C54 AC gives an empty line");
    require(s.icons.arrUp && !s.icons.arrDown, "I-01 UP lit after AC while history exists");
  }
  keys({K::Up});
  {
    const auto s = snap();
    require(s.screen == "HistoryView" && s.text == "1" && s.resultText == "1", "C54 UP shows 1...1");
    require(!s.caret, "C54 no caret in HistoryView");
    require(s.icons.arrUp && !s.icons.arrDown, "C54 sole entry keeps the replay indicator");
  }
  keys({K::Down});
  {
    const auto s = snap();
    require(s.screen == "Entry" && s.text.empty(), "C54 DOWN at newest returns to the empty line");
    require(s.icons.arrUp, "C54 empty line keeps UP");
  }
  // Field case: typing after a result, then AC, must not lock history away.
  // Any empty line with history browses, however it became empty.
  cold();
  keys({K::Digit1, K::Execute, K::Digit2});
  require(snap().screen == "Entry" && snap().text == "2", "typing after a result starts a new edit");
  require(!snap().icons.arrUp && !snap().icons.arrDown, "non-empty editing lights no arrow");
  keys({K::Clear});
  require(snap().text.empty() && snap().icons.arrUp && !snap().icons.arrDown, "AC restores the browsable empty line");
  keys({K::Up});
  {
    const auto s = snap();
    require(s.screen == "HistoryView" && s.text == "1" && s.resultText == "1", "UP after AC shows 1");
    require(!s.caret, "no caret while browsing");
  }
  keys({K::Down});
  require(snap().screen == "Entry" && snap().text.empty(), "DOWN returns to the empty line");
  // C57: AC LEFT recalls at end, AC RIGHT recalls at start. Clearing back to
  // the empty line restores browsability by itself, so the second AC+recall
  // needs no extra flag.
  cold();
  keys({K::Digit1, K::Divide, K::Digit3, K::Execute, K::Clear, K::Left});
  {
    const auto s = snap();
    require(s.screen == "Entry" && s.text == "1/3" && s.cursor == 3, "C57 LEFT recalls 1/3 at end");
    require(s.caret && !s.hasResult, "C57 recall is editing, no answer");
  }
  keys({K::Left});
  require(snap().text == "1/3" && snap().cursor == 2, "second LEFT steps the cursor, never re-recalls");
  keys({K::Clear, K::Right});
  {
    const auto s = snap();
    require(s.screen == "Entry" && s.text == "1/3" && s.cursor == 0, "C57 RIGHT recalls 1/3 at start");
  }
  // A-13/C48: BACK on a successful result edits at the source end, and typing
  // extends it. Result.position stays the P10 error contract.
  cold();
  keys({K::Digit1, K::Execute, K::Return});
  require(snap().screen == "Entry" && snap().text == "1" && snap().cursor == 1, "C48 BACK edits at end");
  keys({K::Digit2});
  require(snap().text == "12", "C48 1 EXE BACK 2 gives 12");
  // C47 short results keep LEFT=end / RIGHT=start.
  cold();
  keys({K::Digit1, K::Divide, K::Digit3, K::Execute, K::Left});
  require(snap().text == "1/3" && snap().cursor == 3, "C47 LEFT edits short result at end");
  keys({K::Execute, K::Right});
  require(snap().text == "1/3" && snap().cursor == 0, "C47 RIGHT edits short result at start");
  // I-06 middle of three lights both; boundaries clamp with newest/oldest-only.
  cold();
  keys({K::Digit1, K::Execute, K::Clear, K::Digit2, K::Execute, K::Clear, K::Digit3, K::Execute});
  keys({K::Up});
  require(snap().icons.arrUp && snap().icons.arrDown, "I-06 middle lights both");
  require(snap().text == "2" && snap().resultText == "2", "I-06 middle shows 2...2");
  keys({K::Up});
  require(!snap().icons.arrUp && snap().icons.arrDown, "I-06 oldest is DOWN-only");
  keys({K::Down, K::Down});
  require(snap().screen == "HistoryView" && snap().text == "3" && snap().icons.arrUp && !snap().icons.arrDown,
          "I-06 newest is UP-only");
  keys({K::Down});
  require(snap().screen == "Result" && snap().text == "3", "I-06 DOWN at newest returns to live result");
  keys({K::Down});
  require(snap().screen == "Result" && snap().text == "3", "I-06 DOWN on live newest is no movement");
  // P10 failures never enter history; Ans and history survive dismissal.
  cold();
  keys({K::Digit1, K::Execute});
  const size_t kept = session.history.size();
  keys({K::Clear, K::Digit1, K::Divide, K::Digit0, K::Execute});
  require(snap().screen == "ErrorDialog", "P10 modal still owns failures");
  require(snap().icons == PanelIcons::State{}, "P10 modal clears icons");
  require(session.history.size() == kept && session.history.back().source == "1", "failure adds no history");
  keys({K::Clear});
  require(snap().screen == "Entry" && snap().text == "1/0" && snap().cursor == 3, "P10 AC dismissal intact");
  require(session.memory.ans.re.number == 1, "failure keeps Ans");
  // Ans continuation still starts from a result.
  keys({K::Clear, K::Digit2, K::Execute, K::Add, K::Digit2});
  require(snap().text == "Ans+2", "Ans continuation intact");
  // No history, no arrows.
  session.history.clear();
  session.history.shrink_to_fit();
  service();
  require(!snap().icons.arrUp && !snap().icons.arrDown, "no history lights nothing");
  // A-07 long result in Complex: arrows scroll the viewport, never edit.
  cold(true);
  require(session.app == AppId::Complex, "long-result app is Complex");
  // NOTE: RIGHT after each exponent exits its field (typing stays inside it).
  keys({K::Digit2, K::Power, K::Digit1, K::Digit0, K::Digit0, K::Right, K::Add, K::Digit2, K::Power,
        K::Digit1, K::Digit0, K::Digit0, K::Right, K::Multiply, K::Shift, K::Digit9, K::Execute});
  {
    const auto s = snap();
    require(s.screen == "Result" && s.hasResult && !s.caret, "A-07 long result shows");
    require(!s.icons.arrLeft && !s.icons.arrRight, "A-07 physical segments stay off");
  }
  const auto scrolled0 = snap().frame;
  keys({K::Right});
  require(snap().screen == "Result" && !snap().caret && snap().hasResult, "A-07 RIGHT scrolls, never edits");
  require(snap().frame != scrolled0, "A-07 RIGHT moves the viewport");
  require(!snap().icons.arrLeft && !snap().icons.arrRight, "A-07 scrolled physical segments stay off");
  keys({K::Right, K::Right});
  const auto scrolledEnd = snap().frame;
  keys({K::Right});
  require(snap().frame == scrolledEnd && snap().screen == "Result", "A-07 scroll clamps at the end, stays scroll-only");
  keys({K::Left});
  require(snap().frame != scrolledEnd, "A-07 LEFT scrolls back");
  keys({K::Return});
  {
    const auto s = snap();
    require(s.screen == "Entry" && !s.hasResult && s.caret, "A-07 BACK edits the long source");
    require(s.cursor == s.text.size(), "A-07 BACK lands at the source end");
  }
  keys({K::Execute, K::Clear});
  require(snap().text.empty() && snap().icons.arrUp, "A-07 AC keeps empty-line history access");
  keys({K::Up});
  require(snap().screen == "HistoryView" && !snap().caret, "A-07 UP browses the long answer");
  const auto history0 = snap().frame;
  keys({K::Right});
  require(snap().screen == "HistoryView" && !snap().caret, "A-07 RIGHT scrolls history, never edits");
  require(snap().frame != history0, "A-07 history viewport moves");
  keys({K::Clear});
  require(snap().screen == "Entry" && snap().text.empty(), "A-07 AC from HistoryView gives the empty line");
  // Separate history-state fuzz (own digest; never mixed with the P4 editor
  // stress digest). Random controller keys over a bounded container assert a
  // valid index, the memory cap, no dangling view, and deterministic state.
  cold();
  uint64_t state = 0x12345678u;
  auto next = [&] {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state >> 33;
  };
  const K pool[] = {K::Execute, K::Clear, K::Up,     K::Down,  K::Left,   K::Right,  K::Return,
                    K::Digit0, K::Digit1, K::Digit2, K::Add,   K::Subtract, K::Dot};
  uint64_t digest = 1469598103934665603ull;
  auto mix = [&](const CalculatorApp::Snapshot& s) {
    digest ^= static_cast<uint64_t>(s.screen.size() + s.text.size() * 1315423911u + s.cursor * 97 +
                                    s.historyCursor * 13 + session.history.size() * 17 +
                                    (s.hasResult ? 31 : 0) + (s.icons.arrUp ? 7 : 0) + (s.icons.arrDown ? 5 : 0));
    digest *= 1099511628211ull;
  };
  for (int step = 0; step < 3000; ++step) {
    CalculatorApp::press(pool[next() % (sizeof(pool) / sizeof(pool[0]))]);
    drain();
    const auto s = snap();
    require(s.screen == "Entry" || s.screen == "Result" || s.screen == "HistoryView" ||
                s.screen == "ErrorDialog",
            "fuzz stays in a known screen");
    require(session.history.empty() || s.historyCursor < session.history.size(), "fuzz keeps a valid cursor");
    require(historyPayloadBytes(session.history) <= kHistoryMaxBytes, "fuzz respects the memory cap");
    if (s.screen == "HistoryView")
      require(!session.history.empty() && s.historyCursor < session.history.size(), "fuzz keeps a valid view");
    if (s.icons.arrUp || s.icons.arrDown || s.screen == "HistoryView")
      require(!session.history.empty(), "fuzz arrows imply usable history");
    if (step % 500 == 0) mix(s);
  }
  mix(snap());
  std::cout << "P11 history fuzz: 3000 steps, digest=" << std::hex << digest << std::dec << "\n";
  std::cout << "P11 history controller: " << checks << " checks passed\n";
}
#endif
