#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/ui/calculator_home.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace Calculator;
using K=ExpressionInput::Key;
int main(int argc,char** argv){
  assert(argc==2);
  // Selected/top pairs for the three physical captures; the last one shows
  // Math Box alone on the last row, its other two cells empty.
  const unsigned selected[]={0,6,9},top[]={0,2,3};
  for(int i=0;i<3;++i){HomeMenu menu;menu.selected=selected[i];menu.top=top[i];auto pixels=menu.pixels();
    std::ifstream in(std::string(argv[1])+"/home-"+std::to_string(i+1)+".bin",std::ios::binary);
    std::vector<uint8_t> expected((std::istreambuf_iterator<char>(in)),{});
    assert(expected.size()==pixels.size());size_t diff=0;
    for(unsigned y=0;y<63;++y)for(unsigned x=0;x<192;++x){
      const unsigned slot=(top[i]+y/32)*3+x/64;
      const bool badge=slot<kHomeItemCount&&x%64>=52&&x%64<59&&y%32>=1&&y%32<8;
      const unsigned offset=y*192+x,shift=6-2*(offset%4);
      if(!badge&&((expected[offset/4]>>shift)&3)!=((pixels[offset/4]>>shift)&3))++diff;
    }
    if(diff){std::cerr<<"Capture "<<i+1<<": "<<diff<<" differing bytes\n";return 1;}
  }
  {HomeMenu menu;const auto frame=menu.pixels();
   const uint8_t expected[]={0x1c,0x36,0x67,0x77,0x77,0x22,0x1c};
   for(unsigned y=0;y<7;++y)for(unsigned x=0;x<7;++x){
      const unsigned pixel=(y+1)*192+52+x;const unsigned shade=(frame[pixel/4]>>(6-2*(pixel%4)))&3;
     assert(shade==((expected[y]&(1u<<(6-x)))?0u:3u));
   }
   for(K key:{K::Digit1,K::Digit0,K::Add,K::Subtract,K::Multiply,K::Divide}){menu.press(key);assert(menu.selected==static_cast<unsigned>(shortcutIndex(key)));}
  }
  HomeMenu home;home.press(K::PageDown);assert(home.selected==6&&home.top==1);
  home.press(K::Down);home.press(K::Down);assert(home.selected==12&&home.top==3);
  // The last row holds Math Box (12) alone; its other slots do not exist, so
  // Right stops at 12 and Down has nothing below either.
  home.press(K::Right);assert(home.selected==12&&home.top==3);
  home.press(K::Down);assert(home.selected==12);
  home.press(K::PageDown);assert(home.selected==12);
  home.press(K::Left);assert(home.selected==11);
  home.press(K::Down);assert(home.selected==11);
  home.press(K::Up);assert(home.selected==8&&home.top==2);
  // Template editing is covered by calculator_render_test.cpp, which links
  // the document and layout the editor now navigates with.
  std::cout<<"Home: all three captures preserved outside shortcut badges. Menu navigation passed.\n";
}
