// User's hardware photos establish composition, not an exact raster oracle.
void tableEquationRenderChecks(const std::string& dir) {
  DataGridView grid;grid.headers={"a","b","c"};grid.cells={"0","0","0","0","0","0"};
  const auto selected=decode(renderDataGrid(grid));
  grid.cursor.col=1;
  const auto plain=decode(renderDataGrid(grid));
  bool contained=true;
  for(int y=0;y<10;++y)for(int x=16;x<74;++x)
    contained &= selected.at(x,y)==plain.at(x,y);
  check("Equation selected digit stays inside its cell",contained);
  writePbm(dir+"/equation-coefficients.pbm",selected);
  MathEditor editor;editor.begin("1");grid.cursor.col=0;grid.editor=&editor;
  const auto typing=decode(renderDataGrid(grid));
  grid.caretOn=false;const auto blink=decode(renderDataGrid(grid));
  check("Equation entry cursor blinks",typing.bits!=blink.bits);
  writePbm(dir+"/equation-entry.pbm",typing);
  editor.begin("1234567890123456789012345678901234567890");editor.setCursor(0);
  decode(renderDataGrid(grid));
  check("Equation long draft cursor remains visible at start",lastRender().caretRect.x>=0);
  // Casio Equation captures: expression rows, left system brace and formula picker.
  for(int degree=2;degree<=4;++degree){
    writePbm(dir+"/equation-degree-menu-"+std::to_string(degree)+".pbm",decode(renderPolynomialMenu(degree-2)));
    DataGridView equation;equation.equation=true;equation.polynomial=true;
    equation.headers.resize(degree+1);equation.cells.resize(degree+1,"0");equation.footer="0";
    const auto frame=decode(renderDataGrid(equation));
    check("Polynomial first coefficient selected",frame.at(10,13)&&!frame.at(48,13));
    writePbm(dir+"/equation-polynomial-"+std::to_string(degree)+".pbm",frame);
    equation.cursor.col=degree;
    const auto last=decode(renderDataGrid(equation));
    check("Polynomial final coefficient stays visible",last.at(10+(degree%3)*64,13+(degree/3)*12));
    writePbm(dir+"/equation-polynomial-last-"+std::to_string(degree)+".pbm",last);
  }
  // Casio Inequality captures: relation picker per degree, then one expression
  // row closed by the relation; the draft uses Calculate's bar caret.
  for(int degree=2;degree<=4;++degree){
    writePbm(dir+"/inequality-relation-menu-"+std::to_string(degree)+".pbm",decode(renderPolynomialMenu(0,degree)));
    DataGridView row;row.equation=true;row.polynomial=true;row.relation=degree==4?"<0":degree==3?"\x04" "0":"\x03" "0";
    row.headers.resize(degree+1);row.cells.resize(degree+1,"0");row.footer="0";
    const auto frame=decode(renderDataGrid(row));
    check("Inequality relation closes the expression row",frame.at(10,13)&&!frame.at(48,13));
    writePbm(dir+"/inequality-coefficients-"+std::to_string(degree)+".pbm",frame);
    MathEditor draft;draft.begin("12");row.editor=&draft;
    writePbm(dir+"/inequality-draft-"+std::to_string(degree)+".pbm",decode(renderDataGrid(row)));
    check("Coefficient draft is typed at the left",lastRender().caretRect.x<40);
    check("Coefficient draft caret is Calculate's bar",lastRender().caretRect.width==2&&lastRender().caretRect.height==12);
  }
  {
    Settings math;math.io=IO::MathMath;int16_t scroll=0;
    // Every answer screen puts its highest text pixel (header, superscript,
    // or the x= label when there is no header) exactly on row 3 (a 3 px top margin).
    auto topRow=[](const Frame& f){
      for(int y=0;y<63;++y)for(int x=0;x<192;++x)if(f.at(x,y))return y;
      return 63;
    };
    const auto inequality=decode(renderEquationResult("x\x03" "a, b\x03" "x","",
        std::string("x<=")+"\x01" "-2" "\x02"+", "+"\x01" "0" "\x02"+"<=x",math,scroll));
    check("Inequality header sits 3 px below the top edge",topRow(inequality)==3);
    writePbm(dir+"/inequality-result.pbm",inequality);
    const auto min=decode(renderEquationResult("Min of y="+polynomialFormula(2),"x","\x01" "-1" "\x02",math,scroll));
    check("Min header sits 3 px below the top edge",topRow(min)==3);
    writePbm(dir+"/equation-result-min.pbm",min);
    const auto simul=decode(renderEquationResult("","x","\x01" "0" "\x02",math,scroll));
    check("Simul label sits 3 px below the top edge",topRow(simul)==3);
    writePbm(dir+"/equation-result-simul.pbm",simul);
    const auto solverRoot=decode(renderEquationResult("","x","\x01" "2" "\x02",math,scroll,true));
    check("Solver root label sits 3 px below the top edge",topRow(solverRoot)==3);
    writePbm(dir+"/equation-result-solver.pbm",solverRoot);
    const auto cubic=decode(renderEquationResult("Max of y="+polynomialFormula(3),"x","\x01" "-1" "\x02",math,scroll));
    check("Two-line header sits 3 px below the top edge",topRow(cubic)==3);
    writePbm(dir+"/equation-result-cubic-max.pbm",cubic);
    const auto menu=decode(renderPolynomialMenu(2));
    bool rowTopClear=true;for(int x=10;x<188;++x)rowTopClear&=!menu.at(x,0);
    check("Formula menu superscript stays inside the band, badge drawn",rowTopClear&&menu.at(1,6));
    writePbm(dir+"/equation-degree-menu-badges.pbm",menu);
    const std::string longValue="-0.1746854043+1.546868887i";
    const auto head=decode(renderEquationResult(polynomialFormula(3)+"=0","x_2",longValue,math,scroll));
    check("Long answer starts at its head with a right arrow",scroll==0&&head.at(190,55));
    writePbm(dir+"/equation-result-long.pbm",head);
    scroll=1000;
    const auto tail=decode(renderEquationResult(polynomialFormula(3)+"=0","x_2",longValue,math,scroll));
    check("Long answer scrolls to its tail with a left arrow",scroll>0&&scroll<1000&&tail.at(2,55));
    writePbm(dir+"/equation-result-long-end.pbm",tail);
  }
  for(int unknowns=2;unknowns<=4;++unknowns){
    DataGridView equation;equation.equation=true;equation.headers.resize(unknowns+1);
    equation.cells.resize(unknowns*(unknowns+1),"0");equation.footer="0";
    const auto frame=decode(renderDataGrid(equation));
    check("Simultaneous brace remains outside selection",frame.at(6,1)&&frame.at(10,1)&&!frame.at(48,1));
    writePbm(dir+"/equation-simul-"+std::to_string(unknowns)+".pbm",frame);
    equation.cursor.row=unknowns-1;equation.cursor.col=unknowns;
    const auto last=decode(renderDataGrid(equation));
    check("Simultaneous RHS is reachable",last.at(138,1+(unknowns-1)*12));
    writePbm(dir+"/equation-simul-last-"+std::to_string(unknowns)+".pbm",last);
  }
  editor.begin("1");
  Viewport view;
  std::vector<DistributionField> fields={{"Start","1"},{"End","5"},{"Step","1"}};
  // Emulator capture: fixed "Table Range" heading, three scrolling rows.
  const auto range=decode(renderDistributionForm("Table Range",fields,1,0,nullptr,view));
  check("Table range heading stays plain above selected End",!range.at(180,5) && !range.at(180,20) && range.at(180,36));
  writePbm(dir+"/table-range.pbm",range);
  const auto editing=decode(renderDistributionForm("Table Range",fields,1,0,&editor,view));
  check("Table range edit stays in End row",lastRender().caretRect.y==32);
  writePbm(dir+"/table-range-edit.pbm",editing);
  const auto run=decode(renderDistributionForm("Table Range",fields,3,0,nullptr,view));
  // Selected Execute: a paper disc on the band with a black play triangle.
  check("Table range scrolls Execute into the last row",run.at(180,50) && !run.at(180,36));
  check("Execute mark is a solid disc with a play cut",run.at(0,56) && !run.at(1,56) && run.at(5,56)
        && run.at(7,56) && !run.at(8,56) && !run.at(10,56) && run.at(11,56) && !run.at(5,51));
  writePbm(dir+"/table-range-execute.pbm",run);
  grid={};grid.headers={"x","f(x)","g(x)"};grid.cells={"","",""};
  grid.table=true;grid.editor=&editor;
  const auto table=decode(renderDataGrid(grid));
  check("Table uses narrow columns and gray rules",table.level(19,40)==2 && table.level(145,40)==2);
  check("Table draft stays below grid",lastRender().caretRect.y==51);
  writePbm(dir+"/table-entry.pbm",table);
  grid.editor=nullptr;grid.footer="f(x)/g(x):None";
  const auto undefined=decode(renderDataGrid(grid));
  const auto* slash=MathFont::find('/',MathFont::Style::Normal);
  const int slashX=1+labelAdvance("f(x)",MathFont::Style::Normal)+slash->xOffset;
  bool literalSlash=true;
  for(int y=0;y<slash->height;++y)for(int x=0;x<slash->width;++x)
    literalSlash &= undefined.at(slashX+x,61+slash->yOffset+y)==MathFont::pixel(*slash,x,y);
  check("Table undefined footer uses literal slash",literalSlash);
  editor.begin("0");
  const auto seed=decode(renderSolverSeed("x",editor,0));
  const auto execute=decode(renderSolverSeed("x",editor,1));
  check("Solver form selects target then Execute",seed.at(180,35) && !seed.at(180,51)
        && !execute.at(180,35) && execute.at(180,51));
  Viewport seedView;
  editor.begin("6");
  const auto inlineSeed=decode(renderSolverSeed("x",editor,0,&seedView,true));
  check("Solver seed cursor stays on X row",lastRender().caretRect.y==32);
  const auto inlineBlink=decode(renderSolverSeed("x",editor,0,&seedView,false));
  check("Solver seed cursor blinks inline",inlineSeed.bits!=inlineBlink.bits);
  writePbm(dir+"/solver-seed-edit.pbm",inlineSeed);
  int16_t scroll0=0;
  editor.begin("9x");
  const auto result=decode(renderSolverResult(editor,"x","0","0",scroll0));
  check("Solver combines root and residual",lastRender().presented=="x=0; L-R=0");
  writePbm(dir+"/solver-result.pbm",result);
  editor.begin("x^2-2=0");
  const auto radical=decode(renderSolverResult(editor,"x","sqrt(2)","0",scroll0));
  writePbm(dir+"/solver-result-exact.pbm",radical);
  // Casio capture: 2x^2+3x+2=9 answers -3/4+sqrt(65)/4, a fraction taller than a text row.
  editor.begin("2x^2+3x+2=9");
  const auto tall=decode(renderSolverResult(editor,"x","-3/4+sqrt(65)/4","0",scroll0));
  bool rootAboveRow=false;for(int x=40;x<192;++x)rootAboveRow|=tall.at(x,30);
  check("Tall solver root rises above its text row",rootAboveRow);
  writePbm(dir+"/solver-result-tall.pbm",tall);
  editor.begin("123456789012345678901234567890");
  decode(renderSolverSeed("x",editor,0,&seedView,true));
  check("Solver long seed caret remains inside row",lastRender().caretRect.x<188 && lastRender().caretRect.y==32);
  editor.setCursor(0);
  decode(renderSolverSeed("x",editor,0,&seedView,true));
  check("Solver seed scroll returns to start",seedView.x==0);
}
