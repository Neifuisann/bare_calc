// Renderer-only P10 metrics; bit-perfect vs the Casio 880BTG ERROR-MENU.bmp
// (see tools/assets/bake_error_menu.py). Deliberately outside the 45 frozen P9 fixtures.
void p10ErrorRenderChecks(const std::string& dir){
  const int before=checks;
  for(Error error:{Error::Math,Error::Syntax}){
    const auto f=decode(renderErrorDialog(error));
    writePbm(dir+(error==Error::Math?"/p10-math-error.pbm":"/p10-syntax-error.pbm"),f);
    check("P10 no caret/input/result geometry",!lastRender().caret&&lastRender().input.width==0&&lastRender().result.width==0);
    bool band=true,outside=true,titleIcon=true;int whiteLabel=0,titleInk=0,grayCount=0;
    for(int y=0;y<63;++y)for(int x=0;x<192;++x){
      const auto level=f.level(x,y);
      if(y>=48&&y<=62){
        if(x>=188)outside &= level==0;
        else if(x>=2&&x<46&&y>=49&&y<=60){band &= level==0||level==3;whiteLabel += level==0;}
        else band &= level==3;
      }
      if(y==0||y==47)outside &= level==0;
      if(y>=1&&y<=20){
        if(level){titleIcon &= x>=2&&x<164;if(level==3)++titleInk;}
      }
      if(y>=21&&y<=41){
        if(level==1)++grayCount;
        if(level&&(x<85||x>106))titleIcon=false;
        if(level>3)titleIcon=false;
      }
      if(y>=42&&y<=47)outside &= level==0;
    }
    check("P10 Back band x0..187 y48..62 with white Back label",band&&whiteLabel>100);
    check("P10 outside-band rows/columns blank",outside);
    check("P10 title + warning present and bounded",titleIcon&&titleInk>200&&grayCount>100);
    bool symmetry=true;
    for(int y=21;y<=41;++y)for(int x=85;x<=106;++x)symmetry &= f.level(x,y)==f.level(191-x,y);
    check("P10 warning symmetric",symmetry&&f.level(95,40)==3&&f.level(95,41)==3&&f.level(85,40)==0);
    check("P10 warning punctuation",f.level(95,30)==3&&f.level(96,30)==3&&f.level(95,35)==1&&f.level(95,36)==3);
    std::vector<uint8_t> animated;
    check("P10 no marquee animation",!CanvasText::nextMarqueeFrame(10000,animated));
  }
  std::cout<<"P10 error renderer: "<<checks-before<<" checks\n";
}
