// Included in the render harness: assertions are about pixels, not constants.
void p9MenuExpectations(){
  static_assert(SettingAction::InputOutput==0 && SettingAction::Angle==1 && SettingAction::Number==2 &&
      SettingAction::Digits==3 && SettingAction::Engineer==4 && SettingAction::Fraction==5 &&
      SettingAction::DecimalMark==6 && SettingAction::Separators==7 && SettingAction::FormatMenu==8 &&
      SettingAction::ScientificKey==9 && SettingAction::Reset==10 && SettingAction::Complex==11 &&
      SettingAction::CalcSettingsGroup==12 && SettingAction::SystemSettingsGroup==13 &&
      SettingAction::GetStarted==14,"Stable setting IDs");
  const int before=checks;
  for(int selected=0;selected<4;++selected){
    std::vector<MenuItem> rows(4);
    const auto f=decode(renderMenuList(rows,selected,0));
    bool exact=true;
    // fitToWidth's legacy empty-label fallback paints an ellipsis. Exclude
    // its text columns; every bar edge and separator remains measured.
    for(int y=0;y<63;++y)for(int x=0;x<192;++x)if(x<2 || x>=40)
      exact &= f.level(x,y)==((x<188 && y>=16*selected && y<16*selected+15)?3:0);
    check("P9 exact 188x15 selected bar row "+std::to_string(selected),exact);
  }
  // Window positions include a wrapped last->first selection.
  for(const auto pos:std::vector<std::pair<int,int>>{{0,0},{4,3},{8,5},{0,5}}){
    std::vector<MenuItem> rows(9);
    const auto f=decode(renderMenuList(rows,pos.first,pos.second));
    const int first=UiKit::ensureVisible(pos.second,pos.first,9,4);
    const auto r=UiKit::scrollbarThumb(0,64,first,4,9);
    bool exact=r.x==190 && r.width==2;
    for(int y=0;y<63;++y)for(int x=188;x<192;++x)
      exact &= f.level(x,y)==((x>=190 && y>=r.y && y<r.y+r.height)?2:0);
    check("P9 grey thumb and two blank columns "+std::to_string(pos.first),exact);
  }
  const auto first=UiKit::scrollbarThumb(0,64,0,4,9);
  const auto middle=UiKit::scrollbarThumb(0,64,3,4,9);
  const auto last=UiKit::scrollbarThumb(0,64,5,4,9);
  check("P9 unchanged scrollbar window math",first.y==0 && first.height==28 && middle.y==21 && last.y==36 && last.height==28);
  check("P9 fitting list has no thumb",UiKit::scrollbarThumb(0,64,0,4,4).width==0);
  for(int selected=0;selected<9;++selected){
    // Exercise blank ink, not undefined empty-String font bounds: Adafruit
    // getTextBounds(String("")) leaves its output arguments untouched, and
    // fitToWidth's legacy caller does not initialize them. A space measures
    // normally and paints nothing; all full-frame geometry assertions stay.
    std::array<std::string,9> empty;empty.fill(" ");
    const auto f=decode(renderVariableGrid(empty,selected));
    bool exact=true;const int left=(selected%2)*97,top=(selected/2)*13;
    for(int y=0;y<63;++y)for(int x=0;x<192;++x)
      exact &= f.level(x,y)==((x>=left && x<left+91 && y>=top && y<top+11)?3:0);
    check("P9 VARIABLE exact cell "+std::to_string(selected),exact);
  }
  const auto grid=decode(renderVariableGrid(zeroGridLabels(),-1));
  const auto ink=inkBounds(grid,0,10,0,90),other=inkBounds(grid,0,10,97,187);
  check("P9 VARIABLE retained text bounds",ink.left==2 && ink.right==18 && ink.top==2 && ink.bottom==8 &&
      other.left==99 && other.right==115 && other.top==2 && other.bottom==8);
  std::cout<<"P9 VARIABLE A=0 ink: "<<ink.left<<","<<ink.top<<".."<<ink.right<<","<<ink.bottom
      <<"; B=0 ink: "<<other.left<<","<<other.top<<".."<<other.right<<","<<other.bottom<<" (7 rows; font difference retained)\n";
  // All four active row positions, including the bottom clipped panel edge.
  for(int selected=0;selected<4;++selected){
    CanvasText::invalidateMarquee();
    std::vector<MenuItem> rows(9);for(auto& row:rows)row.label=std::string(60,'9');
    const auto raw=renderMenuList(rows,selected,0);const auto original=decode(raw);
    CanvasText::acceptMarqueeFrame(raw,0);std::vector<uint8_t> animated;
    check("P9 long selected row animates",CanvasText::nextMarqueeFrame(1080,animated));
    if(animated.empty())continue;
    const auto f=decode(animated);bool outside=true,polarity=true;
    for(int y=0;y<63;++y)for(int x=0;x<192;++x){
      const bool viewport=x>=2 && x<184 && y>=selected*16 && y<selected*16+15;
      if(!viewport)outside &= f.level(x,y)==original.level(x,y);
      else polarity &= f.level(x,y)==0 || f.level(x,y)==3;
    }
    check("P9 marquee preserves separator/grey thumb/other rows",outside && polarity);
  }
  // fitToWidth("") determinism: Adafruit's String getTextBounds leaves its
  // outputs untouched for empty input, so the helper must return "" without
  // reading uninitialized memory. Production-reachable via an empty
  // UiKit::hintBar line; verified twice for determinism.
  {
    GFXcanvas1 c(192, 63);
    c.fillScreen(0);
    c.setFont(nullptr);
    c.setTextSize(1);
    c.setTextWrap(false);
    const String a = CanvasText::fitToWidth(c, String(""), 188);
    const String b = CanvasText::fitToWidth(c, String(""), 188);
    check("P9 fitToWidth empty returns empty deterministically", a.length() == 0 && b.length() == 0);
    check("P9 fitToWidth blank space stays blank", CanvasText::fitToWidth(c, String(" "), 188) == String(" "));
  }
  CanvasText::invalidateMarquee();
  std::cout<<"P9 focused render: "<<checks-before<<" checks\n";
}
