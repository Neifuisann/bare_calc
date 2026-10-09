// P13 render-TU checks (included in calculator_render_test.cpp INSIDE its
// anonymous namespace, like the P9/P10 headers): the evidence-gated Linear
// presenter unit pins plus the CALC parent-natural / field-linear split.
// calculator_linear.h is included at the including file's global scope; every
// reference here is ::Calculator-qualified so it never nests. No exact
// fx-880 Linear token is asserted anywhere: PENDING structures keep the
// verbatim fallback by construction.
void p13LinearPresenterChecks() {
  using Kind = ::Calculator::EditDocument::Kind;
  namespace Linear = ::Calculator::LinearPresenter;
  // Evidence statuses: only already-calculator-facing spellings are confirmed.
  check("P13 script power is a confirmed linear token",
        Linear::statusForKind(Kind::Script) == Linear::TokenStatus::Confirmed);
  check("P13 groups/calls/names stay confirmed",
        Linear::statusForKind(Kind::Group) == Linear::TokenStatus::Confirmed &&
            Linear::statusForKind(Kind::Call) == Linear::TokenStatus::Confirmed &&
            Linear::statusForKind(Kind::Name) == Linear::TokenStatus::Confirmed &&
            Linear::statusForKind(Kind::Glyph) == Linear::TokenStatus::Confirmed);
  for (Kind pending :
       {Kind::Power, Kind::Fraction, Kind::Mixed, Kind::Sqrt, Kind::Root, Kind::Abs, Kind::Log,
        Kind::Diff, Kind::Integral, Kind::Sum, Kind::Product, Kind::Constant, Kind::Convert,
        Kind::Recurring}) {
    check("P13 structural kind stays evidence-gated",
          Linear::statusForKind(pending) == Linear::TokenStatus::Pending);
  }
  auto present = [](const char* source) {
    ::Calculator::EditDocument doc;
    doc.rebuild(source);
    return Linear::present(doc);
  };
  // Confirmed runs: verbatim display, identity mapping, no pending flag.
  {
    const auto out = present("3A+B");
    check("P13 plain expression presents verbatim", out.display == "3A+B" && !out.hasPending &&
                                                       out.pendingCount == 0);
    check("P13 identity mapping", out.map.size() == 4 && out.map[0] == 0 && out.map[3] == 3);
    check("P13 cursor columns", Linear::displayColumnForSource(out, 0) == 0 &&
                                    Linear::displayColumnForSource(out, 2) == 2 &&
                                    Linear::displayColumnForSource(out, 4) == 4);
    check("P13 column inverse", Linear::sourceForDisplayColumn(out, 1) == 1 &&
                                    Linear::sourceForDisplayColumn(out, 4) == 4);
  }
  {
    const auto out = present("9^(3)+sin(1)");
    check("P13 script/call present verbatim without pending",
          out.display == "9^(3)+sin(1)" && !out.hasPending);
    for (size_t i = 0; i < out.map.size(); ++i)
      if (out.map[i] != i) {
        check("P13 script/call mapping identity", false);
        break;
      }
    check("P13 script/call mapping identity", out.map.size() == out.display.size());
  }
  // Pending structures: verbatim fallback preserved, flagged, mapping valid.
  for (const char* source : {"frac(1,2)", "pow(9,3)", "sqrt(2)", "mixed(1,2,3)", "root(3,8)",
                             "log(2,8)", "abs(-3)", "diff(x,3)", "integral(x,0,1)", "sum(x,1,5)",
                             "product(x,1,5)", "constant(0)", "convert(5,0)", "recur(3)"}) {
    const std::string want(source);
    const auto out = present(source);
    bool mapped = out.display == want && out.hasPending && out.pendingCount >= 1 &&
                  out.map.size() == out.display.size();
    for (size_t i = 0; mapped && i < out.map.size(); ++i)
      if (out.map[i] >= want.size()) mapped = false;
    check(std::string("P13 pending fallback keeps ") + want, mapped);
    check(std::string("P13 pending cursor reaches end for ") + want,
          Linear::displayColumnForSource(out, want.size()) == out.display.size());
  }
  {
    const auto out = present("");
    check("P13 empty presents empty", out.display.empty() && out.map.empty() && !out.hasPending);
  }
  // Cursor mapping is mandatory: every display byte maps to a real source
  // offset in non-decreasing order, so caret and viewport stay on stops.
  {
    const auto out = present("2*x+1");
    bool ordered = true;
    for (size_t i = 1; i < out.map.size(); ++i)
      if (out.map[i] < out.map[i - 1]) ordered = false;
    check("P13 mapping is non-decreasing", ordered);
  }
}
void p13CalcSplitRenderChecks() {
  {
    MathEditor field;field.begin("");Viewport view;
    const Frame prompt=decode(renderCalcPrompt("9x",math,"x=",field,math,false,view));
    check("CALC selected row fills bottom right",prompt.level(180,48)==3&&prompt.level(180,62)==3);
    check("CALC leaves middle blank",prompt.level(180,32)==0);
    const Frame execute=decode(renderCalcPrompt("9x",math,"x=",field,math,false,view,true,true));
    check("CALC Execute differs from variable row",!(execute==prompt));
    check("CALC Execute has no caret",!lastRender().caret);
  }
  namespace Linear = ::Calculator::LinearPresenter;
  Settings line = math;
  line.io = IO::LineDecimal;
  // Split styles: a natural structured parent stays structured while the
  // value field is Linear. No canonical parent tokens leak into the field.
  {
    MathEditor field;
    field.begin("5");
    Viewport v;
    const Frame c = decode(renderCalcPrompt("frac(1,2)+A", math, "A=", field, line, false, v));
    check("P13 split keeps the natural parent above", !inkBounds(c, 0, 30).empty());
    check("P13 split keeps the linear field below", !inkBounds(c, 31, 53).empty());
  }
  // The prompt caret belongs only to the value field. Linear fields carry no
  // RenderInfo caret by P8 design, so diff the blink phases: caret pixels
  // must exist and must all sit in the field band.
  {
    MathEditor onField, offField;
    onField.begin("5");
    offField.begin("5");
    Viewport v1, v2;
    const Frame on =
        decode(renderCalcPrompt("frac(1,2)+A", math, "A=", onField, line, false, v1, true));
    const Frame off =
        decode(renderCalcPrompt("frac(1,2)+A", math, "A=", offField, line, false, v2, false));
    bool anyCaret = false, onlyField = true;
    for (int y = 0; y < 63; ++y)
      for (int x = 0; x < 192; ++x)
        if (on.level(x, y) != off.level(x, y)) {
          anyCaret = true;
          if (y < 31) onlyField = false;
        }
    check("P13 split caret lives only in the field", anyCaret && onlyField);
  }
  // Linear parent path (global Line I/O): document-driven bytes, same pixels
  // as the legacy direct print for unconfirmed structures.
  {
    MathEditor field;
    field.begin("5");
    Viewport v, v2;
    const Frame a = decode(renderCalcPrompt("frac(1,2)+A", line, "A=", field, line, false, v));
    MathEditor field2;
    field2.begin("5");
    const Frame b = decode(renderCalcPrompt("2*x+1", line, "x=", field2, line, false, v2));
    check("P13 linear parent still paints", !inkBounds(a, 0, 30).empty() && !inkBounds(b, 0, 30).empty());
  }
  // Ordinary Linear entry goes through the presenter without visual change:
  // confirmed forms show no canonical leaks.
  {
    MathEditor e;
    e.begin("9^(3)+sin(1)");
    e.setMathInput(false);
    Viewport v;
    const Frame f = decode(renderResultSummary(e, line, nullptr, Format::Standard, false, false, v,
                                               "", true, nullptr));
    check("P13 linear entry still draws", !inkBounds(f, 0, 15).empty());
    EditDocument doc;
    doc.rebuild("9^(3)+sin(1)");
    const auto out = Linear::present(doc);
    check("P13 confirmed linear forms leak no canonical source",
          !out.hasPending && out.display.find("frac(") == std::string::npos &&
              out.display.find("pow(") == std::string::npos &&
              out.display.find("sqrt(") == std::string::npos);
  }
}
