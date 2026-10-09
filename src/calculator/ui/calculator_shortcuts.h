#pragma once
#include "input/expression_editor.h"
#include <cstddef>
namespace Calculator {
// The same order is shown on Home, lists and the quick catalog. Long lists
// repeat in banks of fourteen; the selected row determines the active bank.
inline int shortcutIndex(ExpressionInput::Key key) {
  using K=ExpressionInput::Key;
  if(key>=K::Digit1&&key<=K::Digit9)return static_cast<int>(key)-static_cast<int>(K::Digit1);
  if(key==K::Digit0)return 9;
  if(key==K::Add)return 10;if(key==K::Subtract)return 11;
  if(key==K::Multiply)return 12;if(key==K::Divide)return 13;
  return -1;
}
inline uint8_t shortcutCircleRow(unsigned y){
  static const uint8_t rows[]={28,62,127,127,127,62,28};return y<7?rows[y]:0;
}
inline bool shortcutDigitPixel(char label,unsigned x,unsigned y){
  const char* labels="1234567890+-*/";unsigned index=0;
  while(index<14&&labels[index]!=label)++index;
  if(index==14||x<2||x>4||y<1||y>5)return false;
  static const uint16_t glyphs[]={0x2c97,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf,0x7b6f,0x5d0,0x1c0,0xaa8,0x12a4};
  return (glyphs[index]>>(14-((y-1)*3+x-2)))&1;
}
inline char shortcutLabel(size_t index){return "1234567890+-*/"[index%14];}
// Row badge `n` (shortcutIndex) in the bank holding `selected`, or -1 when that
// bank has no such row.
inline int shortcutTarget(int selected,int n,size_t count){
  if(n<0)return -1;
  const int target=(selected<0?0:selected/14)*14+n;
  return static_cast<size_t>(target)<count?target:-1;
}
// SHIFT+key on an answer screen stores into the variable the editor would
// type for that key (calculator_editor.cpp: SHIFT 4 5 6 1 2 3 0 . x10 =
// A B C D E F x y z), so the keycap legend means the same thing in both
// places. Returns the Memory::variables index, or -1 (pi, e and the rest).
inline int shiftedVariableIndex(ExpressionInput::Key key){
  using K=ExpressionInput::Key;
  switch(key){
    case K::Digit4:return 0;case K::Digit5:return 1;case K::Digit6:return 2;
    case K::Digit1:return 3;case K::Digit2:return 4;case K::Digit3:return 5;
    case K::Digit0:return 6;case K::Dot:return 7;case K::Scientific:return 8;
    default:return -1;
  }
}
inline bool menuShortcut(ExpressionInput::Key& key,int& selected,size_t count){
  const int target=shortcutTarget(selected,shortcutIndex(key),count);
  if(target<0)return false;
  selected=target;key=ExpressionInput::Key::Okay;return true;
}
}
