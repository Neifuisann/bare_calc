#include "calculator/apps/calculator_calculate.h"
// P13 A-10 controller acceptance (runs in reference.exe --self-test):
// APP-CALC-01 exact sequence, AwaitFinalExecute gating, repeat, stale guards,
// error routing, and the P14 freeze. Phase is read from the explicit snapshot
// fields, never inferred from source text.
void p13CalcControllerChecks() {
  using namespace Calculator;
  int checks = 0;
  auto require = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(std::string("P13: ") + label);
  };
  auto keys = [&](std::initializer_list<K> list) {
    for (K k : list) {
      CalculatorApp::press(k);
      drain();
    }
  };
  auto snap = [&] { return CalculatorApp::snapshot(frame); };
  auto& session = calculatorSession();
  auto homeToCalculate = [&] {
    CalculatorApp::openHome();
    service();
    for (int i = 0; i < 14; ++i) {
      CalculatorApp::press(K::Left);
      drain();
    }
    for (int i = 0; i < 5; ++i) {
      CalculatorApp::press(K::Up);
      drain();
    }
    keys({K::Okay, K::Clear});
  };
  // Fresh deterministic start: defaults (Math I/O), empty history/answer.
  homeToCalculate();
  session.settings = Settings{};
  homeToCalculate();
  const auto ansBefore = session.memory.ans;
  // APP-CALC-01: 3 SHIFT 4 + SHIFT 5 -> 3A+B, TOOLS CALC.
  keys({K::Digit3, K::Shift, K::Digit4, K::Add, K::Shift, K::Digit5});
  require(snap().text == "3A+B", "parent typed as 3A+B");
  keys({K::Tools, K::Okay});
  {
    const auto s = snap();
    require(s.screen == "Calc" && s.calcPhase == "Prompting" && s.calcIndex == 0 && s.calcCount == 2,
            "CALC opens prompting A of 2");
    require(s.calcParent == "3A+B" && s.text.empty() && !s.hasResult, "parent captured, blank field");
  }
  const size_t historyBefore = session.history.size();
  require(snap().icons.arrDown&&!snap().icons.arrUp,"CALC prompt shows down arrow");
  // A=5 EXE moves to B; prompt accepts touch neither history nor Ans.
  keys({K::Digit5, K::Execute});
  {
    const auto s = snap();
    require(s.screen == "Calc" && s.calcPhase == "Prompting" && s.calcIndex == 1, "A commits, B prompt");
    require(session.memory.variables[0].re.number == 5, "A stored");
    require(session.history.size() == historyBefore && session.memory.ans.re.number == ansBefore.re.number,
            "prompt accept creates no result/history/Ans");
  }
  // B=10 EXE must NOT auto-run: explicit AwaitFinalExecute, still no result.
  keys({K::Digit1, K::Digit0, K::Execute});
  {
    const auto s = snap();
    require(s.screen == "Calc" && s.calcPhase == "AwaitFinalExecute", "last value only commits");
    require(session.memory.variables[1].re.number == 10, "B stored");
    require(!s.hasResult && !s.calcResult && session.history.size() == historyBefore &&
                session.memory.ans.re.number == ansBefore.re.number,
            "no calculation before the final EXE");
  }
  // Final EXE submits exactly one calculation: 25 through the P12 path.
  require(snap().icons.arrUp&&!snap().icons.arrDown,"CALC Execute shows up arrow");
  keys({K::Up});
  require(snap().calcPhase=="Prompting"&&snap().text.empty(),"Up edits stored last value");
  keys({K::Down});
  require(snap().calcPhase=="AwaitFinalExecute"&&session.memory.variables[1].re.number==10,
          "Down accepts displayed stored value without changing it");
  keys({K::Execute});
  {
    const auto s = snap();
    require(s.screen == "Result" && s.calcResult && s.calcPhase == "Result", "CALC result state");
    require(s.resultText == "25" && s.text == "3A+B", "25 for the immutable parent");
    require(session.memory.ans.re.number == 25 && session.history.size() == historyBefore + 1,
            "final updates Ans and pushes exactly one history entry");
  }
  // EXE on the CALC result reopens the prompts for another run.
  keys({K::Execute});
  {
    const auto s = snap();
    require(s.screen == "Calc" && s.calcPhase == "Prompting" && s.calcIndex == 0, "prompts reopen");
    require(s.calcParent == "3A+B" && s.text.empty() && !s.hasResult, "same parent, blank fields, no prefill");
  }
  // Repeat with changed values proves recalculation: A=2, B=4 -> 10.
  keys({K::Digit2, K::Execute, K::Digit4, K::Execute});
  require(snap().calcPhase == "AwaitFinalExecute", "repeat commits without auto-run");
  keys({K::Execute});
  require(snap().resultText == "10" && session.memory.ans.re.number == 10, "repeat recalculates to 10");
  require(session.history.size() == historyBefore + 2, "one entry per final EXE, no duplicates");
  // Editing the committed last value reopens its prompt; EXE recommits, EXE calculates.
  keys({K::Execute});  // reopen
  keys({K::Digit7, K::Execute, K::Digit8, K::Execute});
  require(snap().calcPhase == "AwaitFinalExecute", "second run awaits final EXE");
  keys({K::Digit9});  // correct B=8 to B=89
  require(snap().calcPhase == "Prompting" && snap().calcIndex == 1 && snap().text == "89",
          "edit reopens the last prompt");
  keys({K::Execute});
  require(snap().calcPhase == "AwaitFinalExecute", "recommit returns to await");
  keys({K::Execute});
  require(snap().resultText == "110", "3*7+89 evaluates after correction");
  // Stale prompt job: defer, cancel, late completion adopts nothing. The
  // first Return only cancels the running job (prompt kept); the second is
  // the ordinary CALC exit to Entry, never Home.
  keys({K::Execute});  // reopen prompts
  require(snap().calcPhase == "Prompting" && !snap().hasResult, "reopen abandons the visible result");
  CalculatorApp::press(K::Digit9);
  service();
  CalculatorApp::press(K::Execute);
  service();  // deferred prompt job now running
  require(Calculator::calculatorJobs().running(), "prompt job deferred");
  keys({K::Return});
  require(snap().screen == "Calc" && snap().calcPhase == "Prompting" && snap().text == "9",
          "cancel keeps the prompt and its draft");
  keys({K::Return});
  require(!CalculatorApp::isHome() && snap().screen == "Entry", "CALC Return exits to Entry, not Home");
  CalculatorPlatform::runPendingWorker();
  drain();
  require(session.memory.variables[0].re.number == 7, "cancelled prompt value never commits");
  require(snap().calcPhase == "Inactive", "exit retires the run");
  // Stale final across cancel: deferred final, Clear, late completion ignored.
  homeToCalculate();
  keys({K::Digit3, K::Shift, K::Digit4, K::Add, K::Shift, K::Digit5, K::Tools, K::Okay});
  keys({K::Digit2, K::Execute, K::Digit4, K::Execute});
  CalculatorApp::press(K::Execute);
  service();
  require(Calculator::calculatorJobs().running(), "final job deferred");
  keys({K::Clear});  // cancel back to the committed prompts
  require(snap().screen == "Calc" && snap().calcPhase == "AwaitFinalExecute", "final cancel restores prompts");
  CalculatorPlatform::runPendingWorker();
  drain();
  require(session.memory.ans.re.number == 110, "cancelled final never publishes");
  keys({K::Execute});
  require(snap().resultText == "10", "resubmitted final still evaluates");
  // CALC field errors restore the same field (P10 ownership under P13).
  keys({K::Execute});  // reopen
  keys({K::Digit1, K::Divide, K::Digit0, K::Execute});
  require(snap().screen == "ErrorDialog" && snap().errorOrigin == "Calc", "prompt value error owns Calc");
  keys({K::Return});
  require(snap().screen == "Calc" && snap().text == "1/0", "dismiss restores the same field");
  keys({K::Return});  // abandon this run to Entry
  // Final-evaluation error owns the ordinary Entry (no CALC result to repeat).
  keys({K::Clear, K::Shift, K::Digit4, K::Divide, K::Digit0, K::Tools, K::Okay});
  keys({K::Digit5, K::Execute});
  require(snap().calcPhase == "AwaitFinalExecute", "error-run awaits final EXE");
  const size_t historyAtError = session.history.size();
  keys({K::Execute});
  require(snap().screen == "ErrorDialog" && snap().errorOrigin == "Entry", "failed final owns Entry");
  require(session.history.size() == historyAtError && snap().calcPhase == "Inactive",
          "failures push no history and retire CALC");
  keys({K::Return});
  require(snap().screen == "Entry", "failed final dismisses to Entry");
  // P14 freeze: BACK on empty Calculate never opens Home here.
  keys({K::Clear, K::Return});
  require(snap().screen == "Entry" && !CalculatorApp::isHome(), "C65 stays pre-P14");
  // Parent-natural gate at controller level: structured parent keeps MATHIO,
  // the prompt band paints below the parent band (caret geometry is pinned
  // pixel-exact in the render suite).
  keys({K::Clear, K::Fraction, K::Digit1, K::Down, K::Digit2, K::Right, K::Add, K::Shift,
        K::Digit4, K::Tools, K::Okay});
  {
    const auto s = snap();
    require(s.screen == "Calc" && s.calcParent == "frac(1,2)+A", "structured parent captured");
    require(s.icons.mathio, "math parent keeps MATHIO under the linear prompt");
    const auto p = pixels(frame);
    bool parentInk = false, fieldInk = false;
    for (int y = 0; y < 30 && !parentInk; ++y)
      for (int x = 0; x < 192; ++x)
        if (p[static_cast<size_t>(y * 192 + x)]) {
          parentInk = true;
          break;
        }
    for (int y = 31; y < 54 && !fieldInk; ++y)
      for (int x = 0; x < 192; ++x)
        if (p[static_cast<size_t>(y * 192 + x)]) {
          fieldInk = true;
          break;
        }
    require(parentInk && fieldInk, "parent above, prompt field below");
  }
  keys({K::Return});
  // CALC with no variables never prompts: ordinary direct evaluation.
  keys({K::Clear, K::Digit1, K::Add, K::Digit2, K::Tools, K::Okay});
  require(snap().screen == "Result" && snap().resultText == "3" && snap().calcPhase == "Inactive",
          "varless CALC executes directly with no context");
  // Repeated references dedupe to one prompt in first-occurrence order.
  keys({K::Clear, K::Shift, K::Digit5, K::Add, K::Shift, K::Digit5, K::Add, K::Shift, K::Digit4,
        K::Tools, K::Okay});
  require(snap().screen == "Calc" && snap().calcCount == 2 && snap().calcParent == "B+B+A",
          "B+B+A dedupes to B then A");
  keys({K::Digit1, K::Execute});
  require(snap().calcIndex == 1 && session.memory.variables[1].re.number == 1, "B commits first");
  keys({K::Digit2, K::Execute, K::Execute});
  require(snap().resultText == "4", "1+1+2 evaluates after final EXE");
  keys({K::Return, K::Clear});
  std::cout << "P13 CALC controller: " << checks << " checks passed\n";
}
