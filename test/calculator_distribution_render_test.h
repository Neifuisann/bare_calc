// Pixel checks for the CW form contract from the user's distribution photos.
void distributionRenderChecks(const std::string& dir) {
  std::vector<DistributionField> fields={{"Area","0"},{"mu","0"},{"sigma","1"}};
  Viewport view;
  const auto idle=decode(renderDistributionForm("Inverse Normal",fields,1,0,nullptr,view));
  writePbm(dir+"/distribution-inverse-idle.pbm",idle);
  check("Distribution heading never inverted",!idle.at(180,5));
  check("Distribution index one selects mu row",idle.at(180,35) && !idle.at(180,20));
  MathEditor edit;edit.setMathInput(false);edit.begin("123456");
  const auto typing=decode(renderDistributionForm("Inverse Normal",fields,1,0,&edit,view,true));
  const auto caret=lastRender().caretRect;
  writePbm(dir+"/distribution-inverse-edit.pbm",typing);
  bool surrounding=true;
  for(int y=0;y<63;++y)if(y<32 || y>=47)
    for(int x=0;x<192;++x)surrounding &= idle.level(x,y)==typing.level(x,y);
  check("Distribution inline edit preserves heading and adjacent rows",surrounding);
  check("Distribution caret belongs to mu value",caret.x>=77 && caret.x<188 && caret.y==32 && caret.height==15);
  const auto blink=decode(renderDistributionForm("Inverse Normal",fields,1,0,&edit,view,false));
  bool onlyCaret=true;int delta=0;
  for(int y=0;y<63;++y)for(int x=0;x<192;++x)if(typing.at(x,y)!=blink.at(x,y)){
    ++delta;onlyCaret &= x>=caret.x && x<caret.x+caret.width && y>=caret.y && y<caret.y+caret.height;
  }
  check("Distribution blink changes only the inline caret",onlyCaret && delta>0);
  edit.begin("12345678901234567890123456");
  decode(renderDistributionForm("Inverse Normal",fields,1,0,&edit,view,true));
  check("Distribution long draft scrolls inside value band",view.x>0 && lastRender().caretRect.x<188);
  edit.setCursor(0);
  decode(renderDistributionForm("Inverse Normal",fields,1,0,&edit,view,true));
  check("Distribution Left returns to start of value",view.x==0 && lastRender().caretRect.x==77);
  fields={{"Lower","0"},{"Upper","0"},{"mu","0"},{"sigma","1"}};
  const auto lower=decode(renderDistributionForm("Normal CD",fields,0,0,nullptr,view));
  const auto execute=decode(renderDistributionForm("Normal CD",fields,4,2,nullptr,view));
  writePbm(dir+"/distribution-normal-cd.pbm",lower);
  writePbm(dir+"/distribution-normal-cd-execute.pbm",execute);
  bool fixed=true;
  for(int y=0;y<16;++y)for(int x=0;x<192;++x)fixed &= lower.level(x,y)==execute.level(x,y);
  check("Distribution scroll keeps title fixed",fixed && execute.at(180,50));
  DataGridView grid;grid.headers={"x","P"};grid.cells={"",""};grid.context="Binomial PD";
  const auto list=decode(renderDataGrid(grid));
  writePbm(dir+"/distribution-binomial-list.pbm",list);
  check("Distribution grid reserves right context",list.level(103,40)==2 && list.level(61,40)==2);
  check("Distribution first cell has narrow selection",list.at(22,11) && list.at(57,11) && !list.at(60,11));
  grid.cells={"8",""};grid.cursor.col=1;
  const auto plain=decode(renderDataGrid(grid));
  grid.cursor.col=0;
  const auto selected=decode(renderDataGrid(grid));
  bool inverted=true;
  for(int y=10;y<18;++y)for(int x=22;x<58;++x)inverted &= plain.at(x,y)!=selected.at(x,y);
  check("Distribution selection contains the entire numeric glyph",inverted);
}
