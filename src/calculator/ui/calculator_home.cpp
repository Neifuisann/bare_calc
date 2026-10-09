#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/ui/calculator_home.h"
#include "calculator/assets/calculator_home_assets.h"
#include <algorithm>
#include <cstring>
namespace Calculator {
void HomeMenu::press(ExpressionInput::Key key) {
  using K=ExpressionInput::Key;
  const int quick=shortcutIndex(key);if(quick>=0)selected=static_cast<unsigned>(quick);
  switch(key) {
    case K::Left: if(selected) --selected; break;
    case K::Right: if(selected<kMathBoxIndex) ++selected; break;
    case K::Up: if(selected>=3)selected-=3;break;
    case K::Down: if(selected+3<kHomeItemCount)selected+=3;break;
    case K::PageUp:selected=selected>=6?selected-6:0;break;
    case K::PageDown:selected=std::min(kMathBoxIndex,selected+6);break;
    default:break;
  }
  const unsigned row=selected/3;
  if(row<top)top=row;
  if(row>top+1)top=row-1;
  top=std::min(top,3u);
}
std::array<uint8_t,3024> HomeMenu::pixels() const {
  std::array<uint8_t,3024> raw{};
  std::memcpy(raw.data(),kHomeRows[top],1488);
  std::memcpy(raw.data()+32*48,kHomeRows[top+1],1488);
  for(unsigned slot=top*3;slot<kHomeItemCount&&slot<(top+2)*3;++slot){
    const unsigned bx=slot%3*64+52,by=(slot/3-top)*32+1;
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<7;++x){
      const bool ink=(shortcutCircleRow(y)&(1u<<(6-x)))&&!shortcutDigitPixel(shortcutLabel(slot),x,y);
      const unsigned off=(by+y)*192+bx+x,shift=6-2*(off%4);
      raw[off/4]=static_cast<uint8_t>((raw[off/4]&~(3u<<shift))|((ink?3u:0u)<<shift));
    }
  }
  const unsigned x0=selected%3*64,y0=(selected/3-top)*32;
  for(unsigned y=0;y<31;++y)for(unsigned x=0;x<60;++x) {
    if((y==0||y==30)&&(x==0||x==59))continue;
    const unsigned offset=(y0+y)*192+x0+x;
    raw[offset/4]^=3u<<(6-2*(offset%4));
  }
  for(unsigned y=0;y<63;++y) {
    const uint8_t shade=top==0?kHomeScroll[0][y]:top==2?kHomeScroll[1][y]:top==3?kHomeScroll[2][y]:(y>=12&&y<37?2:0);
    raw[y*48+47]|=static_cast<uint8_t>(shade*5);
  }
  return raw;
}
}
