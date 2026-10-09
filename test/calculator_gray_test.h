// Included inside the render harness namespace: same production pipeline and
// decoded level accessor, with the retained monochrome painter as P7 oracle.
void p8GrayExpectations() {
  static GrayCanvas g;
  g.clear();g.set(0,0,0);g.set(1,0,1);g.set(2,0,2);g.set(3,0,3);
  g.set(4,0,2);g.set(191,0,1);g.set(0,1,3);g.set(191,62,2);
  check("P8 raw packing/Home bit order",g.packed().size()==3024 && g.packed()[0]==0x1b &&
        g.packed()[1]==0x80 && g.packed()[47]==1 && g.packed()[48]==0xc0 && g.packed()[3023]==2);
  for(uint8_t level=0;level<4;++level){g.clear(level);bool ok=true;
    for(auto b:g.packed())ok &= b==level*0x55;
    check("P8 clear level "+std::to_string(level),ok);}
  g.clear();g.set(-1,0,3);g.set(192,0,3);g.set(0,-1,3);g.set(0,63,3);g.set(0,0,4);
  bool empty=true;for(auto b:g.packed())empty &= b==0;
  check("P8 set clips and rejects invalid level",empty);
  g.fillRect(-2,-2,4,4,2);g.fillRect(191,62,32767,32767,3);g.fillRect(0,0,-1,4,3);
  int twos=0,threes=0;for(int y=0;y<63;++y)for(int x=0;x<192;++x){twos+=g.pixel(x,y)==2;threes+=g.pixel(x,y)==3;}
  check("P8 fillRect clips without coordinate overflow",twos==4 && threes==1);
  g.clear();const uint8_t bitmap[]={0x81,0x80,0x40,0x00};g.bits(bitmap,-1,0,9,2,2);
  check("P8 padded bitmap and negative clipping",g.pixel(6,0)==2 && g.pixel(7,0)==2 && g.pixel(0,1)==2 && g.pixel(0,0)==0);
  GFXcanvas1 mono(192,63);mono.fillScreen(0);mono.drawPixel(0,0,1);mono.drawPixel(191,62,1);
  g.clear(2);g.blit1bpp(mono,3);
  check("P8 transparent monochrome promotion",g.pixel(0,0)==3 && g.pixel(191,62)==3 && g.pixel(1,0)==2);
  // Entire Home frames reconstructed through set(), against physical golden
  // bytes already verified by calculator_home_test (no Home production edits).
  for(int n=1;n<=3;++n){
    std::ifstream file("test/calculator_references/home-"+std::to_string(n)+".bin",std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),{});g.clear();
    if(raw.size()==3024)for(size_t p=0;p<12096;++p)g.set(p%192,p/192,(raw[p/4]>>(6-2*(p%4)))&3);
    check("P8 Home packing frame "+std::to_string(n),raw.size()==3024 && std::equal(raw.begin(),raw.end(),g.packed().begin()));
  }
  // No equal adjacent bytes: exactly 24 literal control bytes for 3024 raw.
  g.clear();for(size_t p=0;p<12096;++p){const uint8_t b=(p/4)%251;g.set(p%192,p/192,(b>>(6-2*(p%4)))&3);}
  const auto compressed=CanvasText::packBitsRle(g.packed().data(),3024);const auto decoded=decode(compressed);
  bool exact=compressed.size()==3048;
  for(int y=0;y<63;++y)for(int x=0;x<192;++x)exact &= decoded.level(x,y)==g.pixel(x,y);
  check("P8 worst literal PackBits capacity and round trip",exact);
  // Alternating singleton/repeat-pair defeats the literal-only bound:
  // 3024 input bytes become 4032 bytes. The compressor's vector must grow.
  for(size_t p=0;p<12096;++p){const size_t b=p/4;const uint8_t v=b%3==0?0x1b:0xe4;
    g.set(p%192,p/192,(v>>(6-2*(p%4)))&3);}
  const auto mixed=CanvasText::packBitsRle(g.packed().data(),3024);const auto unpacked=decode(mixed);
  exact=mixed.size()==4032;
  for(int y=0;y<63;++y)for(int x=0;x<192;++x)exact &= unpacked.level(x,y)==g.pixel(x,y);
  check("P8 adversarial mixed PackBits expands safely to 4032",exact);

  const char* sources[]={"","9","9^(3)","frac(1,2)","frac(1,)","sqrt(9)","sqrt(9^(3))",
    "frac(9^(3),2)","frac(1,frac(2,3))","root(3,8)","(frac(1,2))","sum(x^(2),1,10)","integral(x,0,1)"};
  for(const char* source:sources){
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);const int dy=l.box().ascent;
    mono.fillScreen(0);l.paint(mono,0,dy,{0,0,192,63});
    for(uint16_t field=0;field<d.fields();++field){
      g.clear();l.paint(g,0,dy,{0,0,192,63},{field,3,2});bool mask=true,levels=true;
      for(int y=0;y<63;++y)for(int x=0;x<192;++x){
        const auto p=g.pixel(x,y);mask &= bool(p)==mono.getPixel(x,y);levels &= p!=1 && (field!=0 || p!=2);
      }
      check(std::string("P8 P7 mask invariant ")+source+" field "+std::to_string(field),mask);
      check(std::string("P8 no leakage/root all black ")+source+" field "+std::to_string(field),levels);
    }
  }
  // Independent ownership oracle for separated fields: every nonzero pixel
  // inside the field's frozen ink rect is active, all outside is inactive.
  // The nested numerator/radicand rectangles include their Script descendants.
  for(const char* source:{"9^(3)","frac(1,2)","sqrt(9)","frac(9^(3),2)","sqrt(9^(3))"}){
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);const int dy=l.box().ascent;
    for(uint16_t f=1;f<d.fields();++f){
      g.clear();l.paint(g,0,dy,{0,0,192,63},{f,3,2});const auto r=l.fieldInk(f);
      bool ok=true;int active=0,inactive=0;
      for(int y=0;y<63;++y)for(int x=0;x<192;++x)if(g.pixel(x,y)){
        const bool inside=x>=r.x && x<r.x+r.width && y>=r.y+dy && y<r.y+dy+r.height;
        ok &= g.pixel(x,y)==(inside?3:2);active+=inside;inactive+=!inside;
      }
      check(std::string("P8 structural subtree/decorations ")+source+" field "+std::to_string(f),ok && active>0 && inactive>0);
    }
  }
  auto exponent=typed({K::Digit9,K::Power,K::Digit3});
  const auto frame=render(exponent);const auto caret=lastRender().caretRect;
  bool caretBlack=true;for(int y=caret.y;y<caret.y+caret.height;++y)for(int x=caret.x;x<caret.x+caret.width;++x)caretBlack &= frame.level(x,y)==3;
  check("P8 C01 caret black/P7 geometry",caretBlack && caret.width==3 && caret.height==9 && caret.y==0);
  exponent.press(K::Right);const auto root=render(exponent);bool allBlack=true;for(auto p:root.levels)allBlack &= p!=2 && p!=1;
  check("P8 C03 root all-black clears prior grey",allBlack);
  auto insert=at("9",0);const auto insertFrame=render(insert);insert.press(K::Shift);insert.press(K::Delete);
  const auto armed=render(insert);check("P8 InsArmed kind/same bar",lastRender().caretKind==MathEditor::CaretKind::InsArmed && armed==insertFrame);
  Viewport view;renderCalculation(insert,math,nullptr,Format::Standard,false,false,view,"",false);
  check("P8 InsArmed survives blink off",lastRender().caretKind==MathEditor::CaretKind::InsArmed);
  auto overwrite=at("9",0);overwrite.setMathInput(false);const auto old=render(overwrite);
  overwrite.press(K::Shift);overwrite.press(K::Delete);const auto over=render(overwrite);
  check("P8 Overwrite kind/same P7 bar",lastRender().caretKind==MathEditor::CaretKind::Overwrite && old==over);

  const uint8_t left[]={3,15,63,255,63,15,3},right[]={192,240,252,255,252,240,192};
  auto arrows=[&](size_t cursor,int initial,bool wantLeft,bool wantRight){
    auto e=at(std::string(40,'9'),cursor);Viewport v;v.x=initial;
    const auto f=decode(renderCalculation(e,math,nullptr,Format::Standard,false,false,v,"",false));
    bool ok=true;
    for(int y=0;y<63;++y)for(int x=0;x<192;++x){
      if((wantLeft && x<10)||(wantRight && x>=182)){
        bool ink=false;
        if(y>=3 && y<=9 && x>=2 && x<=9 && wantLeft)ink=left[y-3]&(0x80>>(x-2));
        if(y>=3 && y<=9 && x>=183 && x<=190 && wantRight)ink=right[y-3]&(0x80>>(x-183));
        ok &= f.level(x,y)==(ink?3:0);
      }
    }
    // Unreserved sides retain the P7 math mask (and have no raster arrow).
    EditDocument d;d.rebuild(e.text());MathLayout l;l.layout(d);mono.fillScreen(0);
    l.paint(mono,MathMetrics::kMarginLeft-v.x,l.box().ascent,{0,0,192,63});
    for(int y=0;y<63;++y)for(int x=wantLeft?10:0;x<(wantRight?182:192);++x)ok &= f.at(x,y)==mono.getPixel(x,y);
    check("P8 arrow start/middle/end exact bands "+std::to_string(cursor),ok);
  };
  arrows(0,0,false,true);arrows(20,130,true,true);arrows(40,0,true,false);
  // Boundary is actual ink, not advance width. A right-aligned short line
  // with one extra pixel beyond the panel must show the arrow in paintInput.
  for(const char* source:{"9", "99999999999999999"}){
    auto e=at(source,0);Viewport v;const auto f=decode(renderCalculation(e,math,nullptr,Format::Standard,false,false,v,"",false));
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);mono.fillScreen(0);l.paint(mono,MathMetrics::kMarginLeft,l.box().ascent,{0,0,192,63});
    bool ok=true;for(int y=0;y<63;++y)for(int x=0;x<192;++x)ok &= f.at(x,y)==mono.getPixel(x,y);
    check("P8 short/fitting no arrows",ok);
  }
  for(const auto& suffix:{std::string("^(i)"),std::string("^(3)")}){
    const std::string source=std::string(17,'9')+suffix;
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);const auto ink=l.ink();
    const int edge=ink.x+ink.width+MathMetrics::kMarginLeft;
    check("P8 exact fitting/one-pixel-overflow setup",edge==(suffix=="^(i)"?192:193));
    auto e=at(source,0);Viewport v;const auto f=decode(renderCalculation(e,math,nullptr,Format::Standard,false,false,v,"",false));
    bool exact=true;
    mono.fillScreen(0);l.paint(mono,MathMetrics::kMarginLeft,l.box().ascent,{0,0,192,63});
    // The arrow follows the digit line (rows baseline-9..-3, CasioEmuX):
    // the raised exponent pushes this line's baseline below row 12.
    const int arrowTop=l.box().ascent-9;
    for(int y=0;y<63;++y)for(int x=182;x<192;++x){
      const bool arrow=y>=arrowTop && y<=arrowTop+6 && x>=183 && x<=190 && (right[y-arrowTop]&(0x80>>(x-183)));
      exact &= f.at(x,y)==(edge==193?arrow:mono.getPixel(x,y));
    }
    check("P8 exact fitting/one-pixel-overflow raster",exact && v.x==0);
  }
  // Marquee must stay white-on-black after mono menu promotion.
  CanvasText::invalidateMarquee();
  const auto menu=renderMenuList({MenuItem{0,std::string(60,'9')}},0,0);
  CanvasText::acceptMarqueeFrame(menu,0);std::vector<uint8_t> moved;
  check("P8 promoted menu marquee available",CanvasText::nextMarqueeFrame(1080,moved));
  if(!moved.empty()){
    const auto f=decode(moved);bool levels=true;for(auto p:f.levels)levels &= p!=1 && p!=2;
    CanvasText::invalidateMarquee();mono.fillScreen(0);
    const std::vector<UiKit::Row> rows={{String(std::string(60,'9').c_str()),false,UiKit::Mark::kNone,'1'}};
    const UiKit::Window window{0};UiKit::list(mono,rows,0,0,kMenuVisibleRows,false,&window,UiKit::ListStyle{15,187,false});
    const auto original=CanvasText::packCanvas(mono,192,63,1512);
    CanvasText::acceptMarqueeFrame(original,0);std::vector<uint8_t> expected;
    const bool animated=CanvasText::nextMarqueeFrame(1080,expected);
    check("P8 marquee polarity preserved with P9 list geometry",levels && animated && f==decode(expected));
  }
  CanvasText::invalidateMarquee();
  for(const char* source:{"","9^(3)","frac(1,2)","99999999999999999999"}){
    auto e=at(source,std::string(source)=="9^(3)"?4:std::strlen(source));Viewport v;const auto rle=renderCalculation(e,math,nullptr,Format::Standard,false,false,v);
    EditDocument d;d.rebuild(source);MathLayout l;l.layout(d);mono.fillScreen(0);
    l.paint(mono,MathMetrics::kMarginLeft-v.x,l.box().ascent-v.y,{0,0,192,63});
    const auto k=lastRender().caretRect;if(lastRender().caret)mono.fillRect(k.x,k.y,k.width,k.height,1);
    std::cout<<"P8 PackBits "<<source<<": "<<rle.size()<<" bytes; P7 mono "<<CanvasText::packBitsRle(mono.getBuffer(),1512).size()<<"\n";
  }
  const auto menuMask=decode(menu).bits;
  std::cout<<"P8 PackBits menu: "<<menu.size()<<" bytes; P7 mono "<<CanvasText::packBitsRle(menuMask.data(),menuMask.size()).size()<<"\n";
  auto e=at("1/3",3);auto r=evaluated("1/3");Viewport v;
  const auto resultFrame=renderCalculation(e,math,&r,Format::Standard,false,false,v);const auto resultMask=decode(resultFrame).bits;
  std::cout<<"P8 PackBits result: "<<resultFrame.size()<<" bytes; P7 mono "<<CanvasText::packBitsRle(resultMask.data(),resultMask.size()).size()<<"\n";
}
