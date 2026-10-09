#include "calculator/ui/calculator_shortcuts.h"
#include "calculator/ui/calculator_get_started.h"
#include "calculator/ui/calculator_render.h"
#include "calculator/math/calculator_basen.h"
#include "calculator/assets/calculator_busy.h"
#include "calculator/math/calculator_complex.h"
#include "calculator/assets/calculator_distribution_glyphs.h"
#include "calculator/assets/calculator_error_assets.h"
#include "calculator/math/calculator_linear.h"
#include "calculator/ui/calculator_metrics.h"
#include "calculator/ui/calculator_presentation.h"
#include "ui/canvas_text.h"
#include "system/psram_scratch.h"
#include "ui/canvas_gray.h"
#include "ui/ui_kit.h"
#include <Adafruit_GFX.h>
#include <algorithm>
#include <cctype>

namespace Calculator {
namespace {
namespace M=MathMetrics;
// Phase 5: the etched icon strip carries D/R/G, SHIFT, FIX/SCI, Verify and
// history. No bitmap row is reserved for them; entry uses the full 63 rows
// except a genuine transient notice.
constexpr int16_t kNoticeTop=54;
// M-04 geometry is independent of M-03's physical menu-font gate.
// Retain the existing body face (7 ink rows; reference value ink is 9).
constexpr int16_t kGridTop=0;
constexpr int16_t kGridRowPitch=13;
constexpr int16_t kGridColumnPitch=97;
constexpr int16_t kGridCellWidth=91;
constexpr int16_t kGridCellHeight=11;
constexpr int16_t kGridTextX=2,kGridTextY=2;
constexpr UiKit::ListStyle kCalculatorListStyle{15,187,false};
RenderInfo info;
// Reused between frames: storage grows to the largest expression once.
EditDocument inputDocument,resultDocument;
MathLayout inputLayout,resultLayout;
// One reused 2-bpp destination; legacy chrome remains a temporary mono surface.
// PSRAM, not .bss (system/psram_scratch.h); built on the first frame.
GrayCanvas& gray(){
  static GrayCanvas& canvas=PsramScratch::object<GrayCanvas>("calculator_gray");
  return canvas;
}
std::vector<uint8_t> packed(GFXcanvas1& c,UiKit::ThumbRect thumb={}){
  gray().blit1bpp(c,3);
  if(thumb.width)gray().fillRect(thumb.x,thumb.y,thumb.width,thumb.height,2);
  const auto& raw=gray().packed();
  auto rle=CanvasText::packBitsRle(raw.data(),raw.size());
  CanvasText::captureMarqueeFrame(raw.data(),raw.size(),rle,true);
  return rle;
}
// Exact s35-c1/c2 raster rows (research/evidence/sequences), not icon segments;
// the reference arrow.bmp is the same 8x7 right arrow.
constexpr uint8_t leftArrow[]={0x03,0x0f,0x3f,0xff,0x3f,0x0f,0x03};
constexpr uint8_t rightArrow[]={0xc0,0xf0,0xfc,0xff,0xfc,0xf0,0xc0};
// With an arrow shown the content is clipped to [kArrowClipLeft, kArrowClipRight).
constexpr int16_t kArrowClipLeft=10,kArrowClipRight=182;
// Scroll arrows sit centred on the line's digits (rows baseline-9..-3): rows
// 3..9 for an ordinary line at the top, lower with the digits when a tall
// template pushes the baseline down. Always inside `region`.
int16_t arrowTop(int16_t baseline,const MathLayout::Rect& region){
  return std::max<int16_t>(region.y,std::min<int16_t>(baseline-9,region.y+region.height-7));
}
MathLayout::Rect paintInput(const MathLayout& layout,const EditDocument& doc,size_t cursor,
                           int16_t dx,int16_t dy,MathLayout::Rect region,bool editing){
  const auto ink=layout.ink();
  const bool left=ink.width>0 && ink.x+dx<region.x;
  const bool right=ink.width>0 && ink.x+ink.width+dx>region.x+region.width;
  auto clip=region;
  if(left){clip.width-=kArrowClipLeft-clip.x;clip.x=kArrowClipLeft;}
  if(right)clip.width=std::min<int16_t>(clip.width,kArrowClipRight-clip.x);
  layout.paint(gray(),dx,dy,clip,{editing?doc.slotField(cursor):EditDocument::kNone,3,2});
  const int16_t arrowRows=std::min<int16_t>(7,std::max<int16_t>(0,region.height-3));
  if(left)gray().bits(leftArrow,2,arrowTop(dy,region),8,arrowRows,3);
  if(right)gray().bits(rightArrow,183,arrowTop(dy,region),8,arrowRows,3);
  return clip;
}
// Native Execute action mark (Table Range emulator capture): a solid 10x10
// disc at x=1..10, rows top+3..top+12 of a 16 px form row, with a play
// triangle cut out of it. Drawn in `ink`, so it inverts with the band.
void executeMark(GFXcanvas1& c,int top,uint16_t ink){
  static constexpr uint8_t disc[10][2]={{4,7},{2,9},{2,9},{1,10},{1,10},{1,10},{1,10},{2,9},{2,9},{4,7}};
  static constexpr uint8_t play[10]={0,0,1,2,3,3,2,1,0,0};
  for(int dy=0;dy<10;++dy){
    c.drawFastHLine(disc[dy][0],top+3+dy,disc[dy][1]-disc[dy][0]+1,ink);
    if(play[dy])c.drawFastHLine(5,top+3+dy,play[dy],1-ink);
  }
}
// Native 5x7 grid face (hardware captures of Statistics, Table, Matrix and
// Vector): cells, headers, row numbers and names in every data grid. It is
// not Calculate's Script face, whose digits stand nine rows. Fixed 6 px
// advance, ink rows top..top+6, lowercase on the bottom five rows with no
// descenders. Five bits per row, MSB = left column; index 0..94 is ' '..'~',
// 95 is the italic x of the Table headers (kItalicX).
constexpr char kItalicX='\x01';
constexpr int16_t kGridAdvance=6,kGridInk=7;
constexpr uint8_t kGridGlyphs[98][kGridInk]={
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},  // space
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04},  // !
  {0x0a,0x0a,0x0a,0x00,0x00,0x00,0x00},  // "
  {0x0a,0x0a,0x1f,0x0a,0x1f,0x0a,0x0a},  // #
  {0x04,0x0f,0x14,0x0e,0x05,0x1e,0x04},  // $
  {0x18,0x19,0x02,0x04,0x08,0x13,0x03},  // %
  {0x08,0x14,0x14,0x08,0x15,0x12,0x0d},  // &
  {0x06,0x06,0x04,0x08,0x00,0x00,0x00},  // '
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02},  // (
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08},  // )
  {0x04,0x15,0x0e,0x1f,0x0e,0x15,0x04},  // *
  {0x00,0x04,0x04,0x1f,0x04,0x04,0x00},  // +
  {0x00,0x00,0x00,0x00,0x06,0x06,0x04},  // ,
  {0x00,0x00,0x00,0x0e,0x00,0x00,0x00},  // -
  {0x00,0x00,0x00,0x00,0x00,0x00,0x08},  // .
  {0x00,0x01,0x02,0x04,0x08,0x10,0x00},  // /
  {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},  // 0
  {0x04,0x0c,0x04,0x04,0x04,0x04,0x04},  // 1
  {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},  // 2
  {0x0e,0x11,0x01,0x06,0x01,0x11,0x0e},  // 3
  {0x02,0x06,0x0a,0x0a,0x12,0x1f,0x02},  // 4
  {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e},  // 5
  {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e},  // 6
  {0x1f,0x01,0x02,0x02,0x04,0x04,0x04},  // 7
  {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},  // 8
  {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e},  // 9
  {0x00,0x00,0x04,0x00,0x00,0x04,0x00},  // :
  {0x00,0x00,0x04,0x00,0x04,0x04,0x08},  // ;
  {0x01,0x02,0x04,0x08,0x04,0x02,0x01},  // <
  {0x00,0x00,0x1f,0x00,0x1f,0x00,0x00},  // =
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08},  // >
  {0x0e,0x11,0x01,0x06,0x04,0x00,0x04},  // ?
  {0x0e,0x11,0x15,0x17,0x16,0x10,0x0f},  // @
  {0x04,0x0a,0x11,0x11,0x1f,0x11,0x11},  // A
  {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},  // B
  {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},  // C
  {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},  // D
  {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},  // E
  {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},  // F
  {0x0f,0x11,0x10,0x10,0x13,0x11,0x0f},  // G
  {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},  // H
  {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},  // I
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0c},  // J
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11},  // K
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},  // L
  {0x11,0x1b,0x1b,0x15,0x15,0x15,0x11},  // M
  {0x11,0x11,0x19,0x15,0x13,0x11,0x11},  // N
  {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},  // O
  {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},  // P
  {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},  // Q
  {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},  // R
  {0x0e,0x11,0x10,0x0e,0x01,0x11,0x0e},  // S
  {0x1f,0x15,0x04,0x04,0x04,0x04,0x04},  // T
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},  // U
  {0x11,0x11,0x11,0x0a,0x0a,0x04,0x04},  // V
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0a},  // W
  {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},  // X
  {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},  // Y
  {0x1f,0x01,0x02,0x0e,0x08,0x10,0x1f},  // Z
  {0x0f,0x08,0x08,0x08,0x08,0x08,0x0f},  // [
  {0x00,0x10,0x08,0x04,0x02,0x01,0x00},  // backslash
  {0x0f,0x01,0x01,0x01,0x01,0x01,0x0f},  // ]
  {0x04,0x0a,0x11,0x00,0x00,0x00,0x00},  // ^
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1f},  // _
  {0x0c,0x0c,0x04,0x02,0x00,0x00,0x00},  // `
  {0x00,0x00,0x0c,0x02,0x0e,0x12,0x0f},  // a
  {0x10,0x10,0x16,0x19,0x11,0x19,0x16},  // b
  {0x00,0x00,0x0e,0x10,0x10,0x10,0x0e},  // c
  {0x01,0x01,0x0d,0x13,0x11,0x13,0x0d},  // d
  {0x00,0x00,0x0e,0x11,0x1f,0x10,0x0e},  // e
  {0x03,0x04,0x0e,0x04,0x04,0x04,0x04},  // f
  {0x00,0x00,0x0f,0x11,0x0f,0x01,0x0e},  // g
  {0x10,0x10,0x16,0x19,0x11,0x11,0x11},  // h
  {0x04,0x00,0x0c,0x04,0x04,0x04,0x0e},  // i
  {0x00,0x02,0x00,0x06,0x02,0x02,0x18},  // j
  {0x10,0x10,0x12,0x14,0x18,0x14,0x12},  // k
  {0x0c,0x04,0x04,0x04,0x04,0x04,0x0e},  // l
  {0x00,0x00,0x1a,0x15,0x15,0x15,0x15},  // m
  {0x00,0x00,0x16,0x19,0x11,0x11,0x11},  // n
  {0x00,0x00,0x0e,0x11,0x11,0x11,0x0e},  // o
  {0x00,0x00,0x1e,0x11,0x1e,0x10,0x10},  // p
  {0x00,0x00,0x0f,0x11,0x11,0x0f,0x01},  // q
  {0x00,0x00,0x17,0x18,0x10,0x10,0x10},  // r
  {0x00,0x00,0x0f,0x10,0x0e,0x01,0x1e},  // s
  {0x08,0x08,0x1e,0x08,0x08,0x09,0x06},  // t
  {0x00,0x00,0x11,0x11,0x11,0x13,0x0d},  // u
  {0x00,0x00,0x11,0x11,0x11,0x0a,0x04},  // v
  {0x00,0x00,0x11,0x11,0x15,0x15,0x0a},  // w
  {0x00,0x00,0x11,0x0a,0x04,0x0a,0x11},  // x
  {0x00,0x00,0x11,0x11,0x0a,0x04,0x18},  // y
  {0x00,0x00,0x1f,0x02,0x04,0x08,0x1f},  // z
  {0x02,0x04,0x04,0x08,0x04,0x04,0x02},  // {
  {0x04,0x04,0x04,0x00,0x04,0x04,0x04},  // |
  {0x08,0x04,0x04,0x02,0x04,0x04,0x08},  // }
  {0x08,0x15,0x02,0x00,0x00,0x00,0x00},  // ~
  {0x00,0x00,0x09,0x16,0x04,0x0d,0x12},  // italic x
  {0x03,0x0c,0x10,0x0c,0x03,0x00,0x1f},  // \x03 less-or-equal
  {0x18,0x06,0x01,0x06,0x18,0x00,0x1f},  // \x04 greater-or-equal
};
int16_t gridWidth(const std::string& text){
  return text.empty()?0:static_cast<int16_t>(text.size()*kGridAdvance-1);
}
void gridText(GFXcanvas1& c,const std::string& text,int16_t x,int16_t top,uint16_t ink){
  for(const char ch:text){
    const int index=ch==kItalicX?95:ch=='\x03'?96:ch=='\x04'?97:(ch>=' '&&ch<='~'?ch-' ':-1);
    if(index>=0)
      for(int16_t row=0;row<kGridInk;++row)for(int16_t col=0;col<5;++col)
        if(kGridGlyphs[index][row]&(0x10>>col))c.drawPixel(x+col,top+row,ink);
    x+=kGridAdvance;
  }
}
// Right-aligned so the last glyph cell ends on `right` (inclusive). A value
// too wide for [left,right] loses its tail rather than touching a neighbor.
void gridRight(GFXcanvas1& c,std::string text,int16_t left,int16_t right,int16_t top,uint16_t ink){
  while(!text.empty()&&gridWidth(text)>right-left+1)text.pop_back();
  gridText(c,text,static_cast<int16_t>(right+1-gridWidth(text)),top,ink);
}
// Math-face text placed by its ink, not its pen: the ink's right edge lands
// on `right` (inclusive), clipped to [left,right]. Returns the ink's left
// column (right+1 when nothing inked).
int16_t inkRight(GFXcanvas1& c,const std::string& text,MathFont::Style style,
                 int16_t left,int16_t right,int16_t baseline,uint16_t ink=1){
  constexpr int16_t kBase=13;
  const int16_t advance=labelAdvance(text.c_str(),style);
  GFXcanvas1 scratch(static_cast<int16_t>(std::max<int>(8,advance+8)),16);scratch.fillScreen(0);
  paintLabel(scratch,text.c_str(),2,kBase,style);
  int16_t inkLeft=scratch.width(),inkEnd=-1;
  for(int16_t y=0;y<scratch.height();++y)for(int16_t x=0;x<scratch.width();++x)
    if(scratch.getPixel(x,y)){inkLeft=std::min(inkLeft,x);inkEnd=std::max(inkEnd,x);}
  if(inkEnd<0)return static_cast<int16_t>(right+1);
  const int16_t dx=static_cast<int16_t>(right-inkEnd);
  for(int16_t y=0;y<scratch.height();++y)for(int16_t x=inkLeft;x<=inkEnd;++x)
    if(scratch.getPixel(x,y)&&x+dx>=left)c.drawPixel(x+dx,y-kBase+baseline,ink);
  return static_cast<int16_t>(std::max<int>(left,inkLeft+dx));
}
// Native answer band (Matrix, Vector, Ratio, Statistics, Table captures): the
// selected value in the Normal face, ink ending at x=190 on rows 49..60. A
// draft ends in Calculate's insertion cursor: a 2 px bar over the face's
// 12 rows (50..61), never an underline.
constexpr int16_t kFooterRight=190,kFooterBaseline=61,kFooterCaret=2,kFooterCaretTop=50,kFooterCaretHeight=12;
void footerValue(GFXcanvas1& c,const std::string& text,bool caret=false){
  if(!caret){inkRight(c,text,MathFont::Style::Normal,0,kFooterRight,kFooterBaseline);return;}
  const int16_t caretX=static_cast<int16_t>(kFooterRight+1-kFooterCaret);
  inkRight(c,text,MathFont::Style::Normal,0,static_cast<int16_t>(caretX-2),kFooterBaseline);
  c.fillRect(caretX,kFooterCaretTop,kFooterCaret,kFooterCaretHeight,1);
}
// Apps hand their cell draft over as text ending in a '_' insertion cursor.
void footerText(GFXcanvas1& c,const std::string& text){
  const bool draft=!text.empty()&&text.back()=='_';
  footerValue(c,draft?text.substr(0,text.size()-1):text,draft);
}
void replaceAll(std::string& s,const std::string& from,const std::string& to){
  for(size_t at=s.find(from);at!=std::string::npos;at=s.find(from,at+to.size()))s.replace(at,from.size(),to);
}
// One labeled editable field (Store/Define/CALC): `label` at the field
// origin with the editor's math or linear text after it, sharing one caret
// and viewport inside `region`. Writes info.input/caret exactly like
// renderCalculation; the caller owns status text and the notice band. `doc`
// and `layout` are caller-provided reuse storage (CALC needs two at once).
void paintLabeledField(GFXcanvas1& c,const std::string& label,const MathEditor& editor,
                       const Settings& s,bool caret,Viewport& view,
                       const MathLayout::Rect& region,EditDocument& doc,MathLayout& layout,bool caretOn){
  if(s.io==IO::MathMath||s.io==IO::MathDecimal){
    // Casio-face label on the math baseline: the same glyphs the expression
    // uses, never the 6x8 built-in face (which shrank "A=" next to math).
    const int16_t labelWidth=static_cast<int16_t>(labelAdvance(label.c_str(),MathFont::Style::Normal)+4);
    doc.rebuild(editor.text());layout.layout(doc);
    const MathLayout::Box& ib=layout.box();
    info.input=region;
    const size_t cursor=doc.normalize(editor.cursor());
    if(!caret)view={0,0};
    auto dx=[&]{return static_cast<int16_t>(M::kMarginLeft+labelWidth-view.x);};
    auto dy=[&]{return static_cast<int16_t>(region.y+M::kMarginTop+ib.ascent-view.y);};
    if(caret){
      const MathLayout::Rect k=layout.caret(cursor);
      const int16_t right=192-M::kMarginRight,top=region.y,low=region.y+region.height;
      const int16_t labelEnd=static_cast<int16_t>(M::kMarginLeft+labelWidth);
      if(k.x+k.width+dx()>right)view.x+=k.x+k.width+dx()-right;
      const auto ink=layout.ink();
      if(cursor==doc.source().size() && ink.x+ink.width+dx()>192)
        view.x+=ink.x+ink.width+dx()-192;
      if(k.x+dx()<labelEnd)view.x+=k.x+dx()-labelEnd;
      if(k.y+k.height+dy()>low)view.y+=k.y+k.height+dy()-low;
      if(k.y+dy()<top)view.y+=k.y+dy()-top;
      view.x=std::max<int16_t>(0,view.x);view.y=std::max<int16_t>(0,view.y);
      if(labelWidth+std::max<int>(ib.width,ink.x+ink.width)+M::kMarginLeft<=right &&
         labelWidth+k.x+k.width+M::kMarginLeft<=right)view.x=0;
    }
    // Label in the expression face, sitting on the math baseline.
    c.setTextColor(1);
    paintLabel(c,label.c_str(),M::kMarginLeft,static_cast<int16_t>(dy()),MathFont::Style::Normal);
    const auto clip=paintInput(layout,doc,cursor,dx(),dy(),info.input,caret);
    if(clip.x>info.input.x)c.fillRect(info.input.x,info.input.y,clip.x-info.input.x,info.input.height,0);
    // The viewport follows `caret`; the blink phase only hides the paint.
    if(caret&&caretOn){
      MathLayout::Rect k=layout.caret(cursor);k.x+=dx();k.y+=dy();
      const int16_t y0=std::max(k.y,info.input.y),y1=std::min<int16_t>(k.y+k.height,info.input.y+info.input.height);
      const int16_t x0=std::max(k.x,clip.x),x1=std::min<int16_t>(k.x+k.width,clip.x+clip.width);
      if(y1>y0 && x1>x0)gray().fillRect(x0,y0,x1-x0,y1-y0,3);
      const uint16_t field=doc.slotField(cursor);
      info.caret=true;info.caretRect=k;info.style=layout.slot(cursor).style;info.role=doc.field(field).role;
      info.caretKind=editor.caretKind();
    }
  }else{
    // P13 L-09: Linear fields go through the document-driven presenter, not
    // editor.text() directly. Confirmed kinds emit their calculator token;
    // pending structures keep the verbatim fallback, so this stays
    // pixel-identical until probe evidence confirms exact token forms.
    c.setFont(nullptr);
    doc.rebuild(editor.text());
    const LinearPresenter::Output linear = LinearPresenter::present(doc);
    const std::string text = label + linear.display;
    const size_t cursor =
        label.size() + LinearPresenter::displayColumnForSource(linear, editor.cursor());
    info.input=region;
    size_t first=cursor>29?cursor-29:0;
    c.setTextColor(1);c.setCursor(region.x,static_cast<int16_t>(region.y+1));
    c.print(text.substr(first,31).c_str());
    if(caret&&caretOn)c.drawFastVLine(static_cast<int16_t>(region.x+(cursor-first)*6),region.y,10,1);
  }
}
// One signed exact term: [sign] k*pi/d, k*sqrt(r)/d, n/d, w r/d.
std::string presentTerm(std::string t){
  std::string sign;
  if(!t.empty()&&(t[0]=='-'||t[0]=='+')){sign=t.substr(0,1);t.erase(0,1);}
  replaceAll(t,"*pi","pi");replaceAll(t,"*sqrt(","sqrt(");
  const size_t space=t.find(' '),slash=t.find('/');
  if(space!=std::string::npos&&slash!=std::string::npos&&space<slash)
    return sign+"mixed("+t.substr(0,space)+","+t.substr(space+1,slash-space-1)+","+t.substr(slash+1)+")";
  if(slash!=std::string::npos)return sign+"frac("+t.substr(0,slash)+","+t.substr(slash+1)+")";
  return sign+t;
}
}  // namespace

namespace Busy {
// Modal takeover while a job runs with no notice of its own: blank screen
// with the baked CW spinner centered. No caret, expression, or notice band;
// info resets so lastRender() never reports a stale caret. Packed directly
// like renderErrorDialog (no marquee capture: there is no scrolling row).
std::vector<uint8_t> renderBusy(){
  gray().clear();info=RenderInfo{};
  gray().bits(kBlack,kX,kY,kW,kH,3);
  gray().bits(kDark,kX,kY,kW,kH,2);
  gray().bits(kLight,kX,kY,kW,kH,1);
  const auto& raw=gray().packed();
  return CanvasText::packBitsRle(raw.data(),raw.size());
}
}  // namespace Busy

const RenderInfo& lastRender(){return info;}

// Narrow legacy adapter for Equation/Inequality solution spans
// (renderSolution) and Complex solution text: formatValue(Standard) grammar
// becomes template source. Solution spans are single Standard values, so the
// pair ("; "), sexagesimal d/m/s (explicit Sexagesimal only), recurring "(..)"
// (explicit Recurring only) and prime "^" (explicit Prime only) grammars are
// unreachable here and were deleted. Kept: scientific E, exact terms.
std::string presentResult(const std::string& in){
  if(in.empty())return in;
  // Scientific: mantissa E[+-]exponent, in the P2 postfix script form so the
  // result lays out through the same Kind::Script path as keyed input.
  const size_t e=in.find('E');
  if(e!=std::string::npos&&e+1<in.size()&&(in[e+1]=='+'||in[e+1]=='-'||std::isdigit(static_cast<unsigned char>(in[e+1])))){
    std::string exponent=in.substr(e+1);if(exponent[0]=='+')exponent.erase(0,1);
    return in.substr(0,e)+"*10^("+exponent+")";
  }
  // Exact values: one or two terms, split at a top-level sign.
  std::string out;size_t start=0;int depth=0;
  for(size_t i=1;i<in.size();++i){
    if(in[i]=='(')++depth;else if(in[i]==')')--depth;
    else if(!depth&&(in[i]=='+'||in[i]=='-')){out+=presentTerm(in.substr(start,i-start));start=i;}
  }
  return out+presentTerm(in.substr(start));
}

std::vector<uint8_t> renderGetStarted(){return CanvasText::packBitsRle(kGetStarted,sizeof(kGetStarted));}
std::vector<uint8_t> renderQuickCatalog(const std::vector<CatalogEntry>& entries,int selected){
  gray().clear();GFXcanvas1 c(192,63);c.fillScreen(0);c.setFont(nullptr);c.setTextWrap(false);
  const int first=selected/14*14;
  for(int i=first;i<static_cast<int>(entries.size())&&i<first+14;++i){
    const int cell=i-first,x=cell%3*63,y=cell/3*12;
    const bool active=i==selected;c.fillRect(x,y,62,11,active?1:0);c.setTextColor(active?0:1);c.setCursor(x+9,y+2);
    const std::string label=quickCatalogLabel(entries[i]);
    for(unsigned by=0;by<7;++by)for(unsigned bx=0;bx<7;++bx)
      if(shortcutCircleRow(by)&(1u<<(6-bx)))c.drawPixel(x+bx,y+2+by,shortcutDigitPixel(shortcutLabel(i),bx,by)?(active?1:0):(active?0:1));
    c.print(label.c_str());
  }
  return packed(c,UiKit::scrollbarThumb(0,60,first,14,static_cast<int>(entries.size())));
}
std::vector<uint8_t> renderMenuList(const std::vector<MenuItem>& items, int selected, int first,bool shortcuts) {
  gray().clear();
  GFXcanvas1 c(192, 63);
  c.fillScreen(0);
  std::vector<UiKit::Row> rows;
  rows.reserve(items.size());
  for (size_t i=0;i<items.size();++i) {const auto& item=items[i];
    rows.push_back(UiKit::Row{String(menuDisplayLabel(item.label).c_str()),item.submenu,item.mark,
      static_cast<char>(shortcuts&&item.action>=0?shortcutLabel(i):0)});
  }
  // Bare list: top=0, four large rows, no title or rule. The caller owns the
  // window so painting, paging and the scrollbar share one first row.
  const UiKit::Window window{first};
  UiKit::list(c, rows, selected, 0, kMenuVisibleRows, false, &window,kCalculatorListStyle);
  const int start=UiKit::ensureVisible(first,selected,static_cast<int>(rows.size()),kMenuVisibleRows);
  return packed(c,UiKit::scrollbarThumb(0,kMenuVisibleRows*UiKit::kRowPitch,start,kMenuVisibleRows,static_cast<int>(rows.size())));
}

std::vector<uint8_t> renderConfirm(const std::string& question, const std::vector<MenuItem>& options,
                                   int selected, int first) {
  gray().clear();
  GFXcanvas1 c(192, 63);
  c.fillScreen(0);
  // Info-leading composition: the question is row zero (never selected) and
  // the options follow. The caller passes selected past the info row.
  std::vector<UiKit::Row> rows;
  rows.reserve(options.size() + 1);
  rows.push_back(UiKit::Row{String(question.c_str()), false, UiKit::Mark::kNone});
  for (size_t i=0;i<options.size();++i) {const auto& item=options[i];
    rows.push_back(UiKit::Row{String(menuDisplayLabel(item.label).c_str()),false,item.mark,shortcutLabel(i)});
  }
  const UiKit::Window window{first};
  UiKit::list(c, rows, selected, 0, kMenuVisibleRows, true, &window,kCalculatorListStyle);
  const int start=UiKit::ensureVisible(first,selected,static_cast<int>(rows.size()),kMenuVisibleRows);
  return packed(c,UiKit::scrollbarThumb(0,kMenuVisibleRows*UiKit::kRowPitch,start,kMenuVisibleRows,static_cast<int>(rows.size())));
}

std::vector<uint8_t> renderDialog(const std::string& message) {
  gray().clear();
  GFXcanvas1 c(192, 63);
  c.fillScreen(0);
  c.setTextColor(1);
  c.setTextWrap(false);
  c.setFont(nullptr);
  // Word-wrap the message in the body font and center it vertically. No
  // selection bar, no scrollbar, no actionable row: Return dismisses. Long
  // messages show from the head so the cause is never sliced away.
  const auto lines = CanvasText::wrapText(c, String(message.c_str()), 188);
  const size_t visible = std::min(lines.size(), static_cast<size_t>(5));
  const size_t start = 0;
  int16_t y = static_cast<int16_t>(20 - (visible > 1 ? (visible - 1) * 4 : 0));
  if (y < 4) y = 4;
  for (size_t i = 0; i < visible; ++i) {
    const String fitted = CanvasText::fitToWidth(c, lines[start + i], 188);
    int16_t bx, by;
    uint16_t bw, bh;
    c.getTextBounds(fitted, 0, 0, &bx, &by, &bw, &bh);
    c.setCursor(static_cast<int16_t>((192 - static_cast<int>(bw)) / 2), y);
    c.print(fitted);
    y += 10;
    if (y > 52) break;
  }
  return packed(c);
}

std::vector<uint8_t> renderErrorDialog(Error error) {
  // Bit-perfect vs the Casio 880BTG ERROR-MENU.bmp (see
  // tools/bake_error_menu.py) except the warning fill, deliberately level 1
  // (lightest gray) instead of the capture's level 2: title 12px chrome at (2,1), warning 22x21 at
  // (85,21) with black outline/"!" + lightest-gray fill (level 1), selection band x0..187
  // y48..62 at level 3 with white "Back" 44x12 at (2,49). x188..191 stay
  // paper; y0,13..20,42..47 stay paper. "Syntax ERROR" shares the validated
  // style (generated "Math ERROR" is 0 diff vs the reference).
  namespace Art = ErrorArt;
  info = RenderInfo{};
  CanvasText::invalidateMarquee();
  gray().clear();
  gray().setTextWrap(false);
  const uint8_t* title = Art::kErrMath;
  int16_t titleW = 107, titleH = 12;
  switch (error) {
    case Error::Syntax: title = Art::kErrSyntaxERROR; titleW = 129; titleH = 13; break;
    case Error::Math: title = Art::kErrMath; titleW = 107; titleH = 12; break;
    case Error::Argument: title = Art::kErrArgumentERROR; titleW = 151; titleH = 15; break;
    case Error::Stack: title = Art::kErrStackERROR; titleW = 118; titleH = 12; break;
    case Error::Circular: title = Art::kErrCircularERROR; titleW = 151; titleH = 12; break;
    case Error::NotDefined: title = Art::kErrNotDefined; titleW = 118; titleH = 12; break;
    case Error::NoOperator: title = Art::kErrNoOperator; titleW = 118; titleH = 13; break;
    case Error::TimeOut: title = Art::kErrTimeOut; titleW = 84; titleH = 12; break;
    case Error::Cancelled: title = Art::kErrCancelled; titleW = 96; titleH = 12; break;
    case Error::Dimension: title = Art::kErrDimensionERROR; titleW = 162; titleH = 13; break;
    case Error::Variable: title = Art::kErrVariableERROR; titleW = 151; titleH = 12; break;
    case Error::CannotSolve: title = Art::kErrCannotSolve; titleW = 129; titleH = 12; break;
    case Error::Range: title = Art::kErrRangeERROR; titleW = 118; titleH = 15; break;
    case Error::Memory: title = Art::kErrMemoryERROR; titleW = 129; titleH = 13; break;
    case Error::None: break;
  }
  gray().bits(title, 2, 1, titleW, titleH, 3);
  gray().bits(Art::kErrWarnGray, 85, 21, 22, 21, 1);
  gray().bits(Art::kErrWarnBlack, 85, 21, 22, 21, 3);
  gray().fillRect(0, 48, 188, 15, 3);
  gray().bits(Art::kErrBack, 2, 49, 44, 12, 0);
  gray().setTextColor(3);
  const auto& raw = gray().packed();
  return CanvasText::packBitsRle(raw.data(), raw.size());
}

std::vector<uint8_t> renderVariableGrid(const std::array<std::string,9>& labels,int selected){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  c.setFont(nullptr);c.setTextSize(1);c.setTextWrap(false);
  info=RenderInfo{};
  for(int i=0;i<9;++i){
    const int16_t x=static_cast<int16_t>((i%2)*kGridColumnPitch);
    const int16_t y=static_cast<int16_t>(kGridTop+(i/2)*kGridRowPitch);
    const bool active=(i==selected);
    if(active)c.fillRect(x,y,kGridCellWidth,kGridCellHeight,1);
    c.setTextColor(active?0:1);
    c.setCursor(x+kGridTextX,y+kGridTextY);
    // Truncated inside the cell: a long value must never reach the neighbor.
    c.print(CanvasText::fitToWidth(c,String(labels[static_cast<size_t>(i)].c_str()),kGridCellWidth-2*kGridTextX));
  }
  c.setTextColor(1);
  return packed(c);
}

namespace {
// Native grid geometry (Statistics/Table captures): headers on rows 1..7, four
// value rows at a 10 px pitch from row 10, each inverted over its eight rows
// (the glyph plus one). Row numbers end at x=17; the grey column rules run
// rows 0..47 every 42 px from x=19 and each cell is the 36 px 22..57 band.
constexpr int16_t kDataHeaderTop=1,kDataTop=10,kDataPitch=10,kDataBand=8,kDataRows=4;
constexpr int16_t kDataNumberRight=17,kDataRule=19,kDataColumn=42,kDataCell=36,kDataCellLeft=22,kDataHeight=48;
// Header centered between the column's rules: the 41 px interior.
void gridHeader(GFXcanvas1& c,const std::string& title,int16_t interiorLeft,int16_t interiorWidth){
  gridText(c,title,static_cast<int16_t>(interiorLeft+(interiorWidth-gridWidth(title))/2),kDataHeaderTop,1);
}
}  // namespace

std::string polynomialFormula(int degree){
  std::string out;
  for(int term=0;term<=degree;++term){
    if(term)out+='+';
    out+=static_cast<char>('a'+term);
    const int power=degree-term;
    if(power)out+='x';
    if(power>1){out+='^';out+=static_cast<char>('0'+power);}
  }
  return out;
}

namespace {
// Normal-face label where ^d and _d draw a Script super- or subscript digit.
int16_t paintFormula(GFXcanvas1& c,const std::string& text,int16_t x,int16_t baseline,uint16_t ink){
  for(size_t i=0;i<text.size();++i){
    const char ch=text[i];
    if((ch=='^'||ch=='_')&&i+1<text.size()){
      const char digit[]={text[++i],0};
      x=paintLabel(c,digit,x,static_cast<int16_t>(baseline+(ch=='^'?-4:3)),MathFont::Style::Script,ink);
      continue;
    }
    if(ch==' '){x+=4;continue;}  // captions set words closer than the Normal space
    const char one[]={ch,0};
    x=paintLabel(c,one,x,baseline,MathFont::Style::Normal,ink);
  }
  return x;
}
}  // namespace

// Formula menus (Casio Polynomial/Inequality captures): degree picker, or with
// `relationDegree` the four relations of that degree. Plain inverse band.
std::vector<uint8_t> renderPolynomialMenu(int selected,int relationDegree){
  gray().clear();info=RenderInfo{};
  GFXcanvas1 c(192,63);c.fillScreen(0);
  static const char* const kRelations[]={">0","<0","\x04" "0","\x03" "0"};
  const int count=relationDegree?4:3;
  for(int row=0;row<count;++row){
    const int top=row*15,baseline=top+14;
    const uint16_t ink=row==selected?0:1;
    if(row==selected)c.fillRect(0,top,188,15,1);
    // The same quick-select badge as every calculator list (UiKit rows).
    for(unsigned by=0;by<7;++by)for(unsigned bx=0;bx<7;++bx)
      if(shortcutCircleRow(by)&(1u<<(6-bx)))
        c.drawPixel(1+bx,top+4+by,shortcutDigitPixel(shortcutLabel(row),bx,by)?1-ink:ink);
    // Quartic relations elide c,d as a dot run so the badge still fits (native).
    const std::string formula=relationDegree==4?"ax^4+bx^3+'''+e":polynomialFormula(relationDegree);
    const std::string text=relationDegree?formula+kRelations[row]:polynomialFormula(row+2);
    paintFormula(c,text,11,static_cast<int16_t>(baseline),ink);
  }
  return packed(c);
}

namespace {
void sheetSmall(const std::string& text,int x,int top,uint8_t ink){
  GFXcanvas1 mask(192,7);mask.fillScreen(0);gridText(mask,text,x,0,1);
  gray().bits(mask.getBuffer(),0,top,192,7,ink);
}
// Native linear footer in the chrome font; clipping and cursor are local to
// this band so a long formula cannot overwrite the grid or its neighbors.
void sheetLine(const std::string& text,size_t cursor,bool editing,bool caretOn,
               int x,int top,int width,uint16_t ink=1){
  GFXcanvas1 line(width,15);line.fillScreen(0);line.setFont(UiKit::chromeFont());line.setTextWrap(false);line.setTextColor(ink);
  int16_t bx,by;uint16_t w,h;
  line.getTextBounds(text.substr(0,cursor).c_str(),0,12,&bx,&by,&w,&h);
  const int caretX=cursor?bx+w+1:0;
  line.getTextBounds(text.c_str(),0,12,&bx,&by,&w,&h);
  const int offset=editing?std::max(0,caretX-width+3):std::max(0,int(bx+w)-width);
  line.setCursor(-offset,12);line.print(text.c_str());
  if(editing&&caretOn){
    line.fillRect(caretX-offset,1,2,12,ink);
    info.caret=true;info.caretRect={static_cast<int16_t>(x+caretX-offset),static_cast<int16_t>(top+1),2,12};
  }
  gray().bits(line.getBuffer(),x,top,width,15,3);
}
}
std::vector<uint8_t> renderSpreadsheet(const SpreadsheetView& view){
  gray().clear();info=RenderInfo{};CanvasText::invalidateMarquee();
  // Header and row gutters are contiguous inversions, including the corner
  // of the selected column/row; rules stay medium gray (reference palette).
  for(int col=0;col<4;++col){
    const int left=20+42*col;
    gray().fillRect(left,0,41,9,col+view.cursor.firstCol==view.cursor.col?3:2);
    sheetSmall(std::string(1,'A'+col+view.cursor.firstCol),left+19,1,0);
  }
  gray().fillRect(188,0,4,9,2);
  gray().fillRect(19,0,1,48,2);
  gray().fillRect(0,8,20,1,2);
  for(int row=0;row<4;++row){
    const int top=9+10*row;
    gray().fillRect(0,top,20,9,row+view.cursor.firstRow==view.cursor.row?3:2);
    const auto number=std::to_string(row+view.cursor.firstRow+1);
    sheetSmall(number,18-gridWidth(number),top+1,0);
    for(int col=0;col<4;++col){
      const int left=20+42*col;const bool selected=row+view.cursor.firstRow==view.cursor.row&&col+view.cursor.firstCol==view.cursor.col;
      if(selected)gray().fillRect(left,top,41,9,3);
      auto text=view.cells.size()>static_cast<size_t>(row*4+col)?view.cells[row*4+col]:"";
      // Six ordinary digits, or seven characters when an exponent/sign needs
      // the full 41 px width. Compact formatting retains numeric magnitude.
      if(text.size()>7)text=text.substr(0,7);
      sheetSmall(text,left+(text.size()==7?41:39)-gridWidth(text),top+1,selected?0:3);
    }
    if(row<3){gray().fillRect(0,top+9,20,1,1);gray().fillRect(20,top+9,172,1,2);}
  }
  for(int col=0;col<4;++col){gray().fillRect(61+42*col,0,1,9,1);gray().fillRect(61+42*col,9,1,39,2);}
  if(view.editor)sheetLine(view.editor->text(),view.editor->cursor(),true,view.caretOn,1,48,190);
  else if(!view.footer.empty())sheetLine(view.footer,0,false,false,view.paste?12:1,48,view.paste?179:190);
  if(view.paste){gray().drawRect(1,49,8,10,3);gray().fillRect(3,51,4,6,3);}
  const auto& raw=gray().packed();return CanvasText::packBitsRle(raw.data(),raw.size());
}
std::vector<uint8_t> renderSpreadsheetFill(bool formula,const std::string& expression,
    const std::string& range,int selected,const MathEditor* editor,bool caretOn){
  gray().clear();info=RenderInfo{};
  GFXcanvas1 c(192,63);c.fillScreen(0);c.setFont(UiKit::chromeFont());c.setTextWrap(false);
  c.setTextColor(1);c.setCursor(2,12);c.print(formula?"Fill Formula":"Fill Value");
  const char* labels[]={formula?"Form":"Value","Range","Confirm"};
  for(int row=0;row<3;++row){
    const int top=16+16*row;const bool active=row==selected;
    if(active)c.fillRect(0,top,188,15,1);
    c.setTextColor(active?0:1);c.setCursor(row==2?13:2,top+12);c.print(labels[row]);
    if(row<2){c.print(" :");if(!editor||!active)c.print((row==0?expression:range).c_str());}
    else executeMark(c,top,active?0:1);
  }
  gray().blit1bpp(c,3);
  if(expression.empty()){
    for(int y=48;y<63;++y)for(int x=0;x<188;++x)if(gray().pixel(x,y)==3)gray().set(x,y,2);
  }
  if(editor&&selected<2){
    const int top=16+16*selected,left=selected==0?(formula?68:79):79;
    // The editor is white on the selected black band, including its caret.
    GFXcanvas1 line(187-left,15);line.fillScreen(1);line.setFont(UiKit::chromeFont());line.setTextWrap(false);line.setTextColor(0);
    int16_t bx,by;uint16_t w,h;line.getTextBounds(editor->text().substr(0,editor->cursor()).c_str(),0,12,&bx,&by,&w,&h);
    const int caretX=editor->cursor()?bx+w+1:0,offset=std::max(0,caretX-line.width()+3);
    line.setCursor(-offset,12);line.print(editor->text().c_str());
    if(caretOn){line.fillRect(caretX-offset,1,2,12,0);info.caret=true;info.caretRect={static_cast<int16_t>(left+caretX-offset),static_cast<int16_t>(top+1),2,12};}
    gray().fillRect(left,top,line.width(),15,0);gray().bits(line.getBuffer(),left,top,line.width(),15,3);
  }
  const auto& raw=gray().packed();return CanvasText::packBitsRle(raw.data(),raw.size());
}
std::vector<uint8_t> renderSpreadsheetMemory(size_t bytes){
  gray().clear();info=RenderInfo{};GFXcanvas1 c(192,63);c.fillScreen(0);c.setFont(UiKit::chromeFont());c.setTextColor(1);c.setCursor(2,12);c.print((std::to_string(bytes)+" Bytes Free").c_str());return packed(c);
}
std::vector<uint8_t> renderSpreadsheetChoices(const std::string& question,const std::vector<MenuItem>& choices,int selected){
  gray().clear();info=RenderInfo{};CanvasText::invalidateMarquee();
  GFXcanvas1 c(192,63);c.fillScreen(0);c.setFont(UiKit::chromeFont());c.setTextWrap(false);c.setTextColor(1);
  const int start=question.empty()?0:16;
  if(start){c.setCursor(1,12);c.print(question.c_str());}
  // Native 10x10 radio outline/center, transcribed from supplied capture #9.
  constexpr uint16_t outline[]={0x0f0,0x30c,0x204,0x402,0x402,0x402,0x402,0x204,0x30c,0x0f0};
  constexpr uint16_t center[]={0,0,0x060,0x0f0,0x1f8,0x1f8,0x0f0,0x060,0,0};
  for(size_t row=0;row<choices.size();++row){
    const int top=start+16*row;const bool active=static_cast<int>(row)==selected;const uint16_t ink=active?0:1;
    if(active)c.fillRect(0,top,188,15,1);
    const bool filled=choices[row].mark==UiKit::Mark::kRadioOn;
    for(int y=0;y<10;++y)for(int x=0;x<12;++x)if((outline[y]|(filled?center[y]:0))&(1<<(11-x)))c.drawPixel(x,top+3+y,ink);
    c.setTextColor(ink);c.setCursor(11,top+12);c.print(choices[row].label.c_str());
  }
  return packed(c);
}
std::vector<uint8_t> renderDataGrid(const DataGridView& view){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};
  if(!view.context.empty() || view.statistics || view.table) {
    CanvasText::invalidateMarquee();
    // CW Distribution: x/P at the left, type name in the right context
    // region. Four numbered slots remain visible even for an empty list.
    const int columns=(view.statistics||view.table)?static_cast<int>(view.headers.size()):2;
    if(columns<1)return packed(c);
    const int rows=static_cast<int>(view.cells.size()/columns);
    const int first=std::max(0,view.cursor.firstRow);
    for(int col=0;col<columns;++col) {
      std::string label=view.headers[static_cast<size_t>(col)];
      // Table headers set x in the italic variable face; Statistics keeps
      // its upright column names.
      if(view.table)std::replace(label.begin(),label.end(),'x',kItalicX);
      gridHeader(c,label,static_cast<int16_t>(kDataRule+1+col*kDataColumn),kDataColumn-1);
    }
    const int capacity=view.statistics?160:view.table&&columns==3?30:45;
    for(int line=0;line<kDataRows && first+line<capacity;++line) {
      const int row=first+line;
      const int16_t top=static_cast<int16_t>(kDataTop+line*kDataPitch);
      gridRight(c,std::to_string(row+1),0,kDataNumberRight,top,1);
      for(int col=0;col<columns;++col) {
        const int16_t left=static_cast<int16_t>(kDataCellLeft+col*kDataColumn),right=static_cast<int16_t>(left+kDataCell-1);
        const bool active=row==view.cursor.row && col==view.cursor.col;
        if(active)c.fillRect(left,top,kDataCell,kDataBand,1);
        if(row<rows)gridRight(c,view.cells[static_cast<size_t>(row*columns+col)],left,right,top,active?0:1);
      }
    }
    const size_t split=view.context.rfind(' ');
    paintLabel(c,view.context.substr(0,split).c_str(),106,12,MathFont::Style::Normal);
    if(split!=std::string::npos)
      paintLabel(c,view.context.substr(split+1).c_str(),106,28,MathFont::Style::Normal);
    if(view.editor) {
      inputDocument.rebuild(view.editor->text());
      const auto linear=LinearPresenter::present(inputDocument);
      const auto column=LinearPresenter::displayColumnForSource(linear,view.editor->cursor());
      const int caretX=labelAdvance(linear.display.substr(0,column).c_str(),MathFont::Style::Normal);
      const int offset=std::max(0,caretX-189);
      GFXcanvas1 entry(192,12);entry.fillScreen(0);
      paintLabel(entry,linear.display.c_str(),-offset,12,MathFont::Style::Normal);
      if(view.caretOn)entry.fillRect(caretX-offset,0,2,12,1);
      c.drawBitmap(0,51,entry.getBuffer(),192,12,1);
      info.input={0,51,192,12};info.caret=view.caretOn;
      info.caretRect={static_cast<int16_t>(caretX-offset),51,2,12};
      info.caretKind=view.editor->caretKind();
    } else if(view.table && view.footer.find(":None")!=std::string::npos) {
      // This is a function-name separator, not the expression '/' token
      // (paintLabel deliberately converts that token to division).
      int16_t x=1;  // capture: ink from x=2 on rows 49..61
      for(char ch:view.footer){
        if(ch=='/'){
          const auto* slash=MathFont::find('/',MathFont::Style::Normal);
          MathFont::draw(c,*slash,x,kFooterBaseline,1);x+=slash->advance;
        }else if(ch==')'){
          // Captured ')' ink sits 1 px left of the face's; its advance is kept.
          const char text[]={ch,0};
          paintLabel(c,text,static_cast<int16_t>(x-1),kFooterBaseline,MathFont::Style::Normal);
          x=static_cast<int16_t>(x+labelAdvance(text,MathFont::Style::Normal));
        }else {const char text[]={ch,0};x=paintLabel(c,text,x,kFooterBaseline,MathFont::Style::Normal);}
      }
    } else if(!view.footer.empty())footerText(c,view.footer);
    // Column rules in the native grid use the intermediate LCD shade.
    gray().blit1bpp(c,3);
    for(int col=0;col<=columns;++col)gray().drawFastVLine(kDataRule+col*kDataColumn,0,kDataHeight,2);
    const auto thumb=UiKit::scrollbarThumb(0,kDataHeight,first,kDataRows,rows);
    if(thumb.width)gray().fillRect(thumb.x,thumb.y,thumb.width,thumb.height,2);
    const auto& raw=gray().packed();
    return CanvasText::packBitsRle(raw.data(),raw.size());
  }
  if(view.equation){
    const int columns=static_cast<int>(view.headers.size());
    const int rows=columns?static_cast<int>(view.cells.size())/columns:0;
    const int degree=columns-1;
    auto power=[&](int x,int top,int exponent){
      // Tiny raised digits keep the coefficient rows at the captured 12px pitch.
      const uint8_t digits[3][5]={{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1}};
      if(exponent<2||exponent>4)return;
      for(int y=0;y<5;++y)for(int dx=0;dx<3;++dx)
        if(digits[exponent-2][y]&(4>>dx))c.drawPixel(x+dx,top+y,1);
    };
    if(view.polynomial){
      int x=0;
      for(int term=0;term<columns;++term){
        if(term){gridText(c,"+",x,2,1);x+=6;}
        gridText(c,std::string(1,static_cast<char>('a'+term)),x,2,1);x+=6;
        if(degree-term){gridText(c,"x",x,2,1);x+=6;}
        if(degree-term>1){power(x,0,degree-term);x+=4;}
      }
      gridText(c,view.relation,x,2,1);
    }else if(rows){
      // Left system brace spans all equation rows; no row numbers or rules.
      // Drawn with 2 px strokes, as heavy as the native brace.
      const int bottom=rows*12-3,mid=(bottom+1)/2;
      for(int dx=0;dx<2;++dx){
        c.drawPixel(6+dx,1,1);c.drawFastVLine(4+dx,2,mid-3,1);
        c.drawPixel(3+dx,mid-1,1);c.drawPixel(2+dx,mid,1);c.drawPixel(3+dx,mid+1,1);
        c.drawFastVLine(4+dx,mid+2,bottom-mid-2,1);c.drawPixel(6+dx,bottom,1);
      }
      c.drawPixel(5,1,1);c.drawPixel(5,bottom,1);
    }
    const int first=view.polynomial?0:std::max(0,view.cursor.col-2);
    for(int row=0;row<rows;++row)for(int col=first;col<columns;++col){
      if(!view.polynomial&&col-first>=3)break;
      const int slot=view.polynomial?col%3:col-first;
      const int x=10+slot*64,top=view.polynomial?14+(col/3)*12:2+row*12;
      const bool selected=row==view.cursor.row&&col==view.cursor.col;
      if(selected)c.fillRect(x,top-1,38,9,1);
      gridRight(c,view.cells[static_cast<size_t>(row*columns+col)],x,x+37,top,selected?0:1);
      if(view.polynomial){
        const int exponent=degree-col;
        if(exponent){gridText(c,"x",x+39,top,1);power(x+45,top-2,exponent);}
        if(col+1==columns)gridText(c,view.relation,x+39,top,1);
        if(col+1<columns){
          if(slot<2)gridText(c,"+",x+55,top,1);
          else gridText(c,"+",2,top+12,1);
        }
      }else if(col<degree){
        gridText(c,std::string(1,"xyzt"[col]),x+39,top,1);
        gridText(c,col==degree-1?"=":"+",x+55,top,1);
      }
    }
  }else{
  // Equation/Inequality coefficients: the same face and row geometry, with
  // the columns sharing the width right of a 16 px row-number gutter.
  constexpr int16_t kGutter=16;
  const int16_t columns=static_cast<int16_t>(std::max<size_t>(view.headers.size(),1));
  const int rows=static_cast<int>(view.cells.size()/static_cast<size_t>(columns));
  const bool scroll=rows>kDataRows;
  const int16_t right=192-(scroll?UiKit::kScrollbarWidth+1:0);
  const int16_t width=static_cast<int16_t>((right-kGutter)/columns);
  for(int16_t col=0;col<columns&&col<static_cast<int16_t>(view.headers.size());++col)
    gridHeader(c,view.headers[static_cast<size_t>(col)],static_cast<int16_t>(kGutter+col*width),width);
  const int first=std::max(0,std::min(view.cursor.firstRow,std::max(0,rows-kDataRows)));
  for(int line=0;line<kDataRows&&first+line<rows;++line){
    const int row=first+line;
    const int16_t top=static_cast<int16_t>(kDataTop+line*kDataPitch);
    gridRight(c,std::to_string(row+1),0,kGutter-3,top,1);
    for(int16_t col=0;col<columns;++col){
      const int16_t x=static_cast<int16_t>(kGutter+col*width);
      const bool selected=row==view.cursor.row&&col==view.cursor.col;
      if(selected)c.fillRect(x,top,width,kDataBand,1);
      gridRight(c,view.cells[static_cast<size_t>(row*columns+col)],static_cast<int16_t>(x+1),static_cast<int16_t>(x+width-4),top,selected?0:1);
    }
  }
  if(scroll)UiKit::scrollbar(c,kDataTop,kDataRows*kDataPitch,first,kDataRows,rows);
  }
  if(view.editor && view.editor->notice.empty()) {
    inputDocument.rebuild(view.editor->text());
    const auto linear=LinearPresenter::present(inputDocument);
    const auto column=LinearPresenter::displayColumnForSource(linear,view.editor->cursor());
    const auto style=MathFont::Style::Normal;
    const int width=labelAdvance(linear.display.c_str(),style);
    const int cursor=labelAdvance(linear.display.substr(0,column).c_str(),style);
    // The coefficient draft is typed at the left of the answer band with
    // Calculate's bar cursor (native); the committed value shows at the
    // right. A long draft scrolls to keep the cursor visible.
    (void)width;
    const int origin=std::min(1,kFooterRight-kFooterCaret-cursor);
    paintLabel(c,linear.display.c_str(),origin,kFooterBaseline,style);
    if(view.caretOn)c.fillRect(origin+cursor,kFooterCaretTop,kFooterCaret,kFooterCaretHeight,1);
    info.input={0,49,192,14};info.caret=view.caretOn;
    info.caretRect={static_cast<int16_t>(origin+cursor),kFooterCaretTop,kFooterCaret,kFooterCaretHeight};
    info.caretKind=view.editor->caretKind();
  } else if(!view.footer.empty())footerText(c,view.footer);
  return packed(c);
}

std::vector<uint8_t> renderDistributionForm(const std::string& title,
    const std::vector<DistributionField>& fields, int selected, int first,
    const MathEditor* editor, Viewport& viewport, bool caretOn) {
  gray().clear();
  info=RenderInfo{};
  CanvasText::invalidateMarquee();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  const auto style=MathFont::Style::Normal;
  paintLabel(c,title.c_str(),2,13,style);
  const int total=static_cast<int>(fields.size())+1;
  const int start=UiKit::ensureVisible(first,selected,total,kDistributionFormRows);
  for(int line=0;line<kDistributionFormRows && start+line<total;++line) {
    const int row=start+line,top=16+line*16,baseline=top+13;
    const bool active=row==selected;
    const bool editing=active && editor && row<static_cast<int>(fields.size());
    const uint16_t ink=active && !editing?0:1;
    if(active && !editing)c.fillRect(0,top,188,15,1);
    if(row==static_cast<int>(fields.size())) {
      executeMark(c,top,ink);
      paintLabel(c,"Execute",12,baseline,style,ink);
      continue;
    }
    const auto& field=fields[static_cast<size_t>(row)];
    if(field.label=="mu")DistributionGlyphs::drawMu(c,12,baseline,ink);
    else if(field.label=="sigma")DistributionGlyphs::drawSigma(c,12,baseline,ink);
    else if(field.label=="lambda")DistributionGlyphs::drawLambda(c,12,baseline,ink);
    else paintLabel(c,field.label.c_str(),12,baseline,style,ink);
    paintLabel(c,":",66,baseline,style,ink);
    constexpr int valueX=77,valueWidth=111;
    // A separate clipped value band prevents long input painting over labels,
    // adjacent rows, or the scrollbar. The draft stays in the selected row.
    GFXcanvas1 value(valueWidth,15);value.fillScreen(0);
    std::string text=field.value;
    int cursorX=0;
    if(editing) {
      inputDocument.rebuild(editor->text());
      const auto linear=LinearPresenter::present(inputDocument);
      text=linear.display;
      const size_t column=LinearPresenter::displayColumnForSource(linear,editor->cursor());
      cursorX=labelAdvance(text.substr(0,column).c_str(),style);
      if(cursorX-viewport.x>valueWidth-2)viewport.x=cursorX-valueWidth+2;
      if(cursorX<viewport.x)viewport.x=cursorX;
      viewport.x=std::max<int16_t>(0,viewport.x);
      info.input={valueX,static_cast<int16_t>(top),valueWidth,15};
      info.caret=caretOn;
      info.caretRect={static_cast<int16_t>(valueX+cursorX-viewport.x),static_cast<int16_t>(top),2,15};
      info.caretKind=editor->caretKind();
    }
    paintLabel(value,text.c_str(),editing?-viewport.x:0,13,style);
    if(editing && caretOn)value.fillRect(cursorX-viewport.x,0,2,15,1);
    for(int y=0;y<15;++y)for(int x=0;x<valueWidth;++x)
      if(value.getPixel(x,y))c.drawPixel(valueX+x,top+y,ink);
  }
  const auto thumb=UiKit::scrollbarThumb(16,47,start,kDistributionFormRows,total);
  return packed(c,thumb);
}

namespace {
// Six 10 px small-script rows fill 1..61 with no header or gutter.
constexpr int kResultRows = 6, kResultTop = 1, kResultPitch = 10;
void statisticsLabel(GFXcanvas1& c,const std::string& name,int baseline,uint16_t ink) {
  const auto style=MathFont::Style::Script;
  int x=36;
  std::string tail=name;
  if(name=="xbar" || name=="ybar") {
    paintLabel(c,name=="xbar"?"x":"y",x,baseline,style,ink);
    c.drawFastHLine(x,baseline-9,5,ink);
    return;
  }
  if(name.compare(0,3,"sum")==0) {
    // Compact capital sigma, with the same seven-row body as the labels.
    c.drawFastHLine(x,baseline-8,6,ink);c.drawFastHLine(x,baseline-2,6,ink);
    c.drawLine(x,baseline-8,x+3,baseline-5,ink);
    c.drawLine(x+3,baseline-5,x,baseline-2,ink);
    x+=7;tail=name.substr(3);
  } else if(name.compare(0,3,"pop")==0 || name.compare(0,3,"smp")==0) {
    const bool population=name.compare(0,3,"pop")==0;
    if(population)DistributionGlyphs::drawSigma(c,x,baseline,ink);
    else paintLabel(c,"s",x,baseline,style,ink);
    x+=population?11:6;
    tail=name.substr(name.size()-1);
    if(name.find("Var")!=std::string::npos)tail="2"+tail;
  }
  for(char ch:tail) {
    if(ch>='2' && ch<='4' && name!="Q3") {
      const uint8_t two[]={0xe0,0x20,0xe0,0x80,0xe0};
      const uint8_t three[]={0xe0,0x20,0xe0,0x20,0xe0};
      const uint8_t four[]={0xa0,0xa0,0xe0,0x20,0x20};
      c.drawBitmap(x,baseline-9,ch=='2'?two:ch=='3'?three:four,3,5,ink);x+=4;
    } else {
      if(ch=='X')ch='x';else if(ch=='Y')ch='y';
      const char text[]={ch,0};x=paintLabel(c,text,x,baseline,style,ink);
    }
  }
}
}  // namespace

std::vector<uint8_t> renderResultList(const std::vector<MenuItem>& items, int selected, int first) {
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};
  const int rows=static_cast<int>(items.size());
  const bool scroll=rows>kResultRows;
  const int16_t right=static_cast<int16_t>(192-(scroll?UiKit::kScrollbarWidth+1:0));
  const int start=std::max(0,std::min(first,std::max(0,rows-kResultRows)));
  for(int line=0;line<kResultRows&&start+line<rows;++line){
    const int row=start+line;
    const int16_t top=static_cast<int16_t>(kResultTop+line*kResultPitch);
    const int16_t baseline=static_cast<int16_t>(top+kResultPitch-1);
    const bool active=(row==selected);
    if(active)c.fillRect(0,top,right,kResultPitch,1);
    // Left-origin `label=value` (CW capture); a value too wide loses its
    // tail rather than touching the scrollbar.
    const std::string& label=items[static_cast<size_t>(row)].label;
    const auto equal=label.find('=');
    if(equal!=std::string::npos) {
      statisticsLabel(c,label.substr(0,equal),baseline,active?0:1);
      paintLabel(c,"=",78,baseline,MathFont::Style::Script,active?0:1);
    }
    std::string shown=equal==std::string::npos?label:label.substr(equal+1);
    int16_t width=labelAdvance(shown.c_str(),MathFont::Style::Script);
    const int valueX=equal==std::string::npos?2:84;
    while(!shown.empty()&&width>right-valueX){shown.pop_back();width=labelAdvance(shown.c_str(),MathFont::Style::Script);}
    paintLabel(c,shown.c_str(),valueX,baseline,MathFont::Style::Script,active?0:1);
  }
  if(scroll)UiKit::scrollbar(c,kResultTop,kResultRows*kResultPitch,start,kResultRows,rows);
  return packed(c);
}

namespace {
// Bottom notice band only; status lives on the etched strip (Phase 5).
void noticeBand(GFXcanvas1& c,const std::string& note){
  if(note.empty())return;
  c.setFont(nullptr);
  c.fillRect(0,kNoticeTop,192,63-kNoticeTop,0);c.setCursor(0,kNoticeTop+1);c.print(note.substr(0,32).c_str());
}
}  // namespace

std::vector<uint8_t> renderAssignment(const std::string& label,const MathEditor& editor,
                                      const Settings& s,bool busy,Viewport& view,bool caretOn){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};info.caretKind=editor.caretKind();
  // Busy with no notice of its own is the modal CW spinner (blank screen,
  // centered); a vanished caret alone would read as a hang. Explicit notices
  // (Cancelled, Not enough memory) keep the notice band instead.
  if(busy&&editor.notice.empty())return Busy::renderBusy();
  const std::string busyNote=editor.notice;
  const int16_t bottom=busyNote.empty()?63:kNoticeTop;
  paintLabeledField(c,label,editor,s,!busy,view,
                    {0,0,192,bottom},
                    inputDocument,inputLayout,caretOn);
  noticeBand(c,busyNote);
  return packed(c);
}

std::vector<uint8_t> renderCalcPrompt(const std::string& parent, const Settings& parentSettings,
                                      const std::string& label, const MathEditor& editor,
                                      const Settings& fieldSettings,
                                      bool busy,Viewport& view,bool caretOn,
                                      bool executeRow,const std::string& storedValue){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};info.caretKind=editor.caretKind();
  // Modal spinner while busy with no notice (see renderAssignment).
  if(busy&&editor.notice.empty())return Busy::renderBusy();
  const std::string busyNote=editor.notice;
  const int16_t bottom=busyNote.empty()?63:kNoticeTop;
  // Retained parent expression above: no caret, no scrolling, clipped to its
  // band so a tall parent can never push the active field off the screen.
  // P13 A-10: the parent renders in parentSettings (its natural Math display
  // when the global Input/Output is Math); only the value field is Linear.
  const int16_t kFieldTop=bottom-15;
  const int16_t kParentBottom=kFieldTop;
  if(parentSettings.io==IO::MathMath||parentSettings.io==IO::MathDecimal){
    resultDocument.rebuild(parent);resultLayout.layout(resultDocument);
    const MathLayout::Box& pb=resultLayout.box();
    resultLayout.paint(gray(),M::kMarginLeft,
                       static_cast<int16_t>(M::kMarginTop+pb.ascent),
                       {0,0,192,kParentBottom});
  }else{
    // Linear parent: document-driven display bytes (pending structures keep
    // the verbatim fallback), never raw canonical search/replace.
    resultDocument.rebuild(parent);
    const LinearPresenter::Output linear = LinearPresenter::present(resultDocument);
    c.setFont(nullptr);c.setTextColor(1);c.setCursor(0,2);
    c.print(linear.display.substr(0,31).c_str());
  }
  // CALC uses a selected bottom row, not the Store/Define assignment field.
  // The blank draft displays the stored value until typing replaces it.
  (void)fieldSettings;
  const auto style=MathFont::Style::Normal;
  inputDocument.rebuild(editor.text());
  const auto linear=LinearPresenter::present(inputDocument);
  const std::string value=editor.text().empty()?storedValue:linear.display;
  const std::string text=executeRow?"Execute":label+value;
  const int16_t cursorX=static_cast<int16_t>(labelAdvance(
      (label+linear.display.substr(0,LinearPresenter::displayColumnForSource(linear,editor.cursor()))).c_str(),style));
  view.x=executeRow?0:std::max<int16_t>(0,cursorX-183);
  c.fillRect(0,kFieldTop,188,15,1);
  paintLabel(c,text.c_str(),executeRow?12:-view.x,bottom-2,style,0);
  if(executeRow)c.drawCircle(5,kFieldTop+7,3,0);
  info.input={0,kFieldTop,188,15};
  if(!executeRow&&!editor.text().empty()&&!busy){
    info.caret=true;info.caretRect={static_cast<int16_t>(cursorX-view.x),kFieldTop+1,1,13};
    if(caretOn)c.drawFastVLine(info.caretRect.x,info.caretRect.y,13,0);
  }
  c.fillRect(188,kFieldTop,4,15,0);
  noticeBand(c,busyNote);
  return packed(c);
}

std::string summaryText(const ResultSummary& r,const Settings& s,Format f){
  if(r.error!=Error::None)return errorText(r.error);
  if(r.verified)return r.truth?"True":"False";
  // A CAS formula is already display source; Line I/O shows its linear form.
  if(r.formula){EditDocument d;d.rebuild(*r.formula);return LinearPresenter::present(d).display;}
  // P12 typed path: semantic runs -> linear text (R-02 separators, R-03
  // distinct marks as UTF-8, locale Point). No pretty-string reparse.
  const Presentation::Output pres=Presentation::presentSummary(r,s,f);
  // Verified/error handled above; empty presentation means legacy text-only
  // truth (kept as Text runs in future). Fall back to engine text only when
  // the typed path yields nothing (should not happen for numeric results).
  if(Presentation::empty(pres)){
    if(r.complex)return formatComplex(r.value,r.imag,s,f);
    std::string output=formatValue(r.value,s,f);
    if(r.pair)output+="; "+formatValue(r.second,s,f);
    return output;
  }
  return Presentation::toLinearText(pres,s);
}

// Exactly the string the result region lays out: typed Presentation ->
// canonical template source -> EditDocument/MathLayout. No heuristic
// fraction/sqrt/pi/E reparse (R-04). Legacy presentResult() remains only for
// Equation/Complex solution pages (renderSolution) until P15.
std::string presentedSummary(const ResultSummary& r,const Settings& s,Format f){
  if(r.error!=Error::None)return summaryText(r,s,f);
  if(r.verified)return summaryText(r,s,f);
  if(r.formula)return *r.formula;  // CAS: natural-display source, in every format
  // Typed presentations are never empty for numeric results, so the legacy
  // string parser is unreachable on the normal Calculate path.
  return Presentation::toTemplateSource(Presentation::presentSummary(r,s,f),s);
}

int16_t summaryMaxScroll(const ResultSummary& r,const Settings& s,Format f){
  if(s.io!=IO::MathMath&&s.io!=IO::MathDecimal)return 0;
  if(r.error!=Error::None)return 0;
  EditDocument measured;measured.rebuild(presentedSummary(r,s,f));
  MathLayout placed;placed.layout(measured);
  const int need=placed.ink().x+placed.ink().width;
  return need>191?static_cast<int16_t>(need-190):0;
}

std::vector<uint8_t> renderResultSummary(const MathEditor& editor,const Settings& s,const ResultSummary* r,Format f,bool busy,bool verify,
                                       Viewport& view,const std::string& caption,bool caretOn,int16_t* resultScroll){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);c.setTextColor(1);c.setTextWrap(false);
  info=RenderInfo{};info.caretKind=editor.caretKind();
  (void)verify;  // Verify lives on the etched CHECK glyph (Phase 5), not in pixels.
  // Busy with neither notice nor caption is the modal CW spinner, not the
  // expression with a hidden caret: a vanished caret alone reads as hung.
  std::string busyNote=editor.notice.empty()?caption:editor.notice;
  if(busy&&busyNote.empty())return Busy::renderBusy();
  const std::string& note=busyNote;
  const int16_t bottom=note.empty()?63:kNoticeTop;
  // `caret` owns the viewport (a scrolled expression must stay scrolled even
  // while the blink hides the paint); `showCaret` adds the blink phase.
  const bool caret=!r&&!busy;
  const bool showCaret=caret&&caretOn;
  std::string output;
  if(r)output=summaryText(*r,s,f);
  if(s.io==IO::MathMath||s.io==IO::MathDecimal){
    int16_t inputBottom=bottom;
    if(r){
      info.presented=presentedSummary(*r,s,f);
      resultDocument.rebuild(info.presented);resultLayout.layout(resultDocument);
      const MathLayout::Rect& ri=resultLayout.ink();
      const int16_t inkBottom=std::min<int16_t>(61,bottom);
      const int16_t top=std::max<int16_t>(0,inkBottom-ri.height);
      info.result={0,top,192,static_cast<int16_t>(bottom-top)};
      inputBottom=std::max<int16_t>(0,top-M::kResultGap);
      // A-07: an answer wider than its viewport scrolls (scroll-only) instead
      // of entering editing. The fit path stays byte-identical to the legacy
      // right-aligned paint; only an overflowing non-error answer with scroll
      // state clips with P8 raster arrows. Exact target arrow geometry stays
      // PHYSICAL_LONG_RESULT_PENDING; physical segments stay off (I-04).
      // summaryMaxScroll() is the same range the controller uses for key
      // precedence, so paint and keys can never disagree.
      const int16_t maxScroll=resultScroll?summaryMaxScroll(*r,s,f):0;
      if(maxScroll<=0){
        // Right-aligned when it fits; a longer one shows its start. Errors read
        // from the left.
        const int16_t x=r->error!=Error::None?M::kMarginLeft:std::max<int16_t>(M::kMarginLeft,192-ri.x-ri.width);
        resultLayout.paint(gray(),x,inkBottom-ri.y-ri.height,info.result);
      }else{
        int16_t scroll=*resultScroll;
        if(scroll<0)scroll=0;
        if(scroll>maxScroll)scroll=maxScroll;
        *resultScroll=scroll;
        const int16_t x=static_cast<int16_t>(M::kMarginLeft-scroll);
        auto clip=info.result;
        const bool left=scroll>0,right=scroll<maxScroll;
        if(left){clip.width-=kArrowClipLeft-clip.x;clip.x=kArrowClipLeft;}
        if(right)clip.width=std::min<int16_t>(clip.width,kArrowClipRight-clip.x);
        const int16_t baseline=static_cast<int16_t>(inkBottom-ri.y-ri.height);
        resultLayout.paint(gray(),x,baseline,clip);
        const int16_t arrowRows=std::min<int16_t>(7,std::max<int16_t>(0,info.result.height-3));
        if(left)gray().bits(leftArrow,2,arrowTop(baseline,info.result),8,arrowRows,3);
        if(right)gray().bits(rightArrow,183,arrowTop(baseline,info.result),8,arrowRows,3);
      }
    }
    inputDocument.rebuild(editor.text());inputLayout.layout(inputDocument);
    const MathLayout::Box& ib=inputLayout.box();
    info.input={0,0,192,inputBottom};
    const size_t cursor=inputDocument.normalize(editor.cursor());
    // Caret and expression share one translation. It moves only when the
    // whole caret rectangle would leave the viewport.
    if(!caret){
      // Result/history view has no caret to follow: top-anchoring clips the
      // denominator when a tall fraction-power input shares the screen with
      // its answer. Bottom-anchor the input box to inputBottom instead so the
      // fraction bar and denominator stay visible; excess clips at the top.
      // Up/Down keeps browsing history (no input scrolling after EXE).
      view.x=0;
      const int16_t need=static_cast<int16_t>(M::kMarginTop+ib.ascent+ib.descent);
      view.y=need>inputBottom?static_cast<int16_t>(need-inputBottom):0;
    }
    auto dx=[&]{return static_cast<int16_t>(M::kMarginLeft-view.x);};
    auto dy=[&]{return static_cast<int16_t>(M::kMarginTop+ib.ascent-view.y);};
    if(caret){
      const MathLayout::Rect k=inputLayout.caret(cursor);
      const int16_t right=192-M::kMarginRight,top=info.input.y,low=info.input.y+info.input.height;
      const auto ink=inputLayout.ink();
      const int16_t inkRight=static_cast<int16_t>(std::max<int>(ib.width,ink.x+ink.width));
      // Casio (CasioEmuX captures): the caret sits as far right as one digit
      // cell of room allows, and the line scrolls only as much as that needs.
      // Typing the 17th digit rolls the line; while text hides on the left,
      // Left/Right roll the line under a caret that keeps its column, so the
      // characters before it stay visible. The room ends at the right arrow
      // when more follows, so an empty box right of the caret (a radicand, an
      // integrand) is never cut by the arrow. Characters are clipped at the
      // arrows, not snapped to whole cells, as on the calculator.
      const int16_t slotX=inputLayout.slot(cursor).x;
      const int16_t room=M::kNormalDigitAdvance;
      auto scrollFor=[&](int16_t limit){return static_cast<int16_t>(std::max(0,slotX+M::kMarginLeft+room-limit));};
      view.x=scrollFor(right);
      if(inkRight+M::kMarginLeft-view.x>192)view.x=scrollFor(kArrowClipRight);
      if(cursor==inputDocument.source().size() && ink.x+ink.width+dx()>192)
        view.x+=ink.x+ink.width+dx()-192;
      if(k.x+k.width+dx()>right)view.x+=k.x+k.width+dx()-right;
      if(k.y+k.height+dy()>low)view.y+=k.y+k.height+dy()-low;
      if(k.y+dy()<top)view.y+=k.y+dy()-top;
      view.x=std::max<int16_t>(0,view.x);view.y=std::max<int16_t>(0,view.y);
    }
    const auto clip=paintInput(inputLayout,inputDocument,cursor,dx(),dy(),info.input,caret);
    if(showCaret){
      MathLayout::Rect k=inputLayout.caret(cursor);k.x+=dx();k.y+=dy();
      const int16_t y0=std::max(k.y,info.input.y),y1=std::min<int16_t>(k.y+k.height,info.input.y+info.input.height);
      const int16_t x0=std::max(k.x,clip.x),x1=std::min<int16_t>(k.x+k.width,clip.x+clip.width);
      if(y1>y0 && x1>x0)gray().fillRect(x0,y0,x1-x0,y1-y0,3);
      const uint16_t field=inputDocument.slotField(cursor);
      info.caret=true;info.caretRect=k;info.style=inputLayout.slot(cursor).style;info.role=inputDocument.field(field).role;
      info.caretKind=editor.caretKind();
    }
  }else{
    // Linear fallback preserves P7 geometry: the caret is drawn on the mono
    // canvas only and deliberately leaves info.caret unset, so snapshots
    // report caret=false/Hidden here while logicalCaretKind still tracks.
    // P13 L-09: the input line goes through the document-driven presenter
    // (pending structures keep the verbatim fallback); the result region
    // below stays on the P12 typed path and is untouched here.
    inputDocument.rebuild(editor.text());
    const LinearPresenter::Output linear = LinearPresenter::present(inputDocument);
    const std::string& text=linear.display;
    const size_t cursor=LinearPresenter::displayColumnForSource(linear,editor.cursor());
    size_t first=cursor>29?cursor-29:0;c.setCursor(0,2);c.print(text.substr(first,31).c_str());
    if(showCaret)c.drawFastVLine(static_cast<int16_t>((cursor-first)*6),1,10,1);
    if(r){
      c.fillRect(0,43,192,20,0);
      // The built-in face has no angle sign, so a complex result is laid out in
      // the ClassWiz face -- through the layout, whose operator spacing keeps the
      // + bar off the next digit -- right-aligned like math output.
      if(r->error==Error::None&&r->complex){
        info.presented=output;resultDocument.rebuild(output);resultLayout.layout(resultDocument);
        const auto ri=resultLayout.ink();
        resultLayout.paint(gray(),std::max<int16_t>(M::kMarginLeft,192-ri.x-ri.width),std::min<int16_t>(61,bottom)-ri.y-ri.height,{0,43,192,static_cast<int16_t>(bottom-43)});
      }
      else{c.setCursor(0,46);c.print(output.c_str());}
    }
  }
  if(!note.empty()){c.fillRect(0,kNoticeTop,192,63-kNoticeTop,0);c.setCursor(0,kNoticeTop+1);c.print(note.substr(0,32).c_str());}
  return packed(c);
}

// Legacy Result entry: converts to the shared summary view with no scroll
// state (start-clipped, no arrows), so existing callers and goldens are
// pixel-identical. The Calculate controller renders live/history answers
// through renderResultSummary directly with its scroll viewport.
std::vector<uint8_t> renderCalculation(const MathEditor& editor,const Settings& s,const Result* r,Format f,bool busy,bool verify,
                                       Viewport& view,const std::string& caption,bool caretOn){
  ResultSummary owned;const ResultSummary* summary=nullptr;
  if(r){owned=toSummary(*r);summary=&owned;}
  return renderResultSummary(editor,s,summary,f,busy,verify,view,caption,caretOn,nullptr);
}

std::vector<uint8_t> renderBaseN(const MathEditor& editor,uint8_t base,const ResultSummary* r,bool busy,Viewport& view,bool caretOn){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);c.setTextColor(1);c.setTextWrap(false);
  info=RenderInfo{};info.caretKind=editor.caretKind();
  const bool value=r&&r->error==Error::None,binary=value&&base==2;
  // Modal spinner while busy with no notice (see renderAssignment); checked
  // before painting so the spinner never pays for an expression layout.
  std::string note=editor.notice;
  if(busy&&note.empty())return Busy::renderBusy();
  // Math layout for the prefix/digit glyphs whatever the stored IO setting.
  paintLabeledField(c,"",editor,Settings{},!r&&!busy,view,{0,0,192,static_cast<int16_t>(binary?32:44)},
                    inputDocument,inputLayout,caretOn);
  if(!note.empty()){noticeBand(c,note);return packed(c);}
  const char* name=base==16?"Hexadecimal":base==2?"Binary":base==8?"Octal":"Decimal";
  constexpr MathFont::Style kNormal=MathFont::Style::Normal,kScript=MathFont::Style::Script;
  if(!r){
    if(editor.text().empty()){paintLabel(c,name,0,50,kScript);paintLabel(c,"Press [FORMAT] to change format",0,61,kScript);}
    return packed(c);
  }
  if(!value){info.presented=errorText(r->error);paintLabel(c,info.presented.c_str(),M::kMarginLeft,60,kNormal);return packed(c);}
  int64_t n=0;r->value.number.toInt(n);
  info.presented=formatBaseN(static_cast<int32_t>(n),base);
  auto right=[&](const std::string& t,int16_t baseline){
    paintLabel(c,t.c_str(),static_cast<int16_t>(192-M::kMarginRight-labelAdvance(t.c_str(),kNormal)),baseline,kNormal);
  };
  if(binary){right(info.presented.substr(0,16),46);right(info.presented.substr(16),60);}
  else right(info.presented,60);
  paintLabel(c,name,0,61,kScript);
  return packed(c);
}

std::vector<uint8_t> renderAggregate(const AggregateView& v){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};
  // Native MatA/VctA editor (hardware captures): `name=` in the grid face at
  // the top left, then the cells on the data grid's 42 px columns and 10 px
  // rows between square brackets that span exactly the rows used.
  constexpr int16_t kBracketLeft=17;
  const int rows=std::max(v.rows,1),cols=std::max(v.cols,1);
  gridText(c,v.name+"=",1,kDataHeaderTop,1);
  const int16_t bottom=static_cast<int16_t>(kDataTop+rows*kDataPitch-3);
  const int16_t bracketRight=static_cast<int16_t>(kDataCellLeft+(cols-1)*kDataColumn+kDataCell+2);
  for(const int16_t x:{kBracketLeft,bracketRight}){
    // Four-pixel serifs two rows deep on two-pixel stems, open to the cells.
    const int16_t stem=x==kBracketLeft?x:static_cast<int16_t>(x+2);
    c.fillRect(stem,kDataTop,2,bottom-kDataTop+1,1);
    c.fillRect(x,kDataTop,4,2,1);c.fillRect(x,static_cast<int16_t>(bottom-1),4,2,1);
  }
  for(int r=0;r<v.rows;++r)for(int k=0;k<v.cols;++k){
    const int16_t x=static_cast<int16_t>(kDataCellLeft+k*kDataColumn),y=static_cast<int16_t>(kDataTop+r*kDataPitch);
    const bool selected=r==v.cursor.row&&k==v.cursor.col;
    if(selected)c.fillRect(x,y,kDataCell,kDataBand,1);
    const size_t at=static_cast<size_t>(r*v.cols+k);
    if(at<v.cells.size())gridRight(c,v.cells[at],x,static_cast<int16_t>(x+kDataCell-1),y,selected?0:1);
  }
  footerText(c,v.footer);
  return packed(c);
}

std::vector<uint8_t> renderSizeForm(const std::string& title,const std::vector<SizeField>& fields,int selected){
  gray().clear();info=RenderInfo{};
  CanvasText::invalidateMarquee();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  c.setFont(UiKit::chromeFont());c.setTextWrap(false);
  // Chrome text placed by its ink's left column, as measured on the captures.
  // The ClassWiz15 boxes carry blank columns, so the ink is found by drawing.
  auto text=[&](const std::string& s,int16_t inkLeft,int16_t top,uint16_t ink){
    const int16_t baseline=static_cast<int16_t>(UiKit::kRowTopPad+UiKit::kChromeAscent);
    GFXcanvas1 scratch(192,UiKit::kRowPitch);scratch.fillScreen(0);
    scratch.setFont(UiKit::chromeFont());scratch.setTextWrap(false);scratch.setTextColor(1);
    scratch.setCursor(8,baseline);scratch.print(s.c_str());
    int16_t first=scratch.width();
    for(int16_t y=0;y<scratch.height();++y)for(int16_t x=0;x<first;++x)if(scratch.getPixel(x,y))first=x;
    c.setTextColor(ink);
    c.setCursor(static_cast<int16_t>(inkLeft+8-first),static_cast<int16_t>(top+baseline));
    c.print(s.c_str());
  };
  constexpr int16_t kLabelX=2,kColonX=137,kValueX=145,kArrowX=178,kConfirmX=13,kBandWidth=188,kBandRows=15;
  text(title,kLabelX,0,1);
  const int total=static_cast<int>(fields.size())+1;
  for(int row=0;row<total&&row<3;++row){
    const int16_t top=static_cast<int16_t>((row+1)*UiKit::kRowPitch);
    const bool active=row==selected;
    const uint16_t ink=active?0:1;
    if(active)c.fillRect(0,top,kBandWidth,kBandRows,1);
    if(row==static_cast<int>(fields.size())){
      executeMark(c,top,ink);
      text("Confirm",kConfirmX,top,ink);
      continue;
    }
    const auto& field=fields[static_cast<size_t>(row)];
    text(field.label,kLabelX,top,ink);
    // The native colon is two 3x3 dots, aligned in one column for every row.
    c.fillRect(kColonX,top+3,3,3,ink);c.fillRect(kColonX,top+9,3,3,ink);
    text(std::to_string(field.value),kValueX,top,ink);
  }
  c.setFont(nullptr);
  gray().blit1bpp(c,3);
  // The value arrow is grey: dark on paper, light on the selection band.
  for(int row=0;row<static_cast<int>(fields.size())&&row<2;++row){
    const int16_t top=static_cast<int16_t>((row+1)*UiKit::kRowPitch+4);
    for(int16_t y=0;y<7;++y){
      const int16_t width=static_cast<int16_t>(8-2*std::abs(3-y));
      gray().fillRect(kArrowX,top+y,width,1,row==selected?1:2);
    }
  }
  const auto& raw=gray().packed();
  return CanvasText::packBitsRle(raw.data(),raw.size());
}

std::vector<uint8_t> renderRatio(const RatioView& view){
  gray().clear();info=RenderInfo{};
  CanvasText::invalidateMarquee();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  constexpr auto kScript=MathFont::Style::Script;
  // Hardware capture: four 36 px slots whose right edges are 44/86/140/182,
  // Script values on rows 20..28, a ten-row inversion and a grey underline on
  // row 30 under each editable slot. ':' follows slots one and three, '='
  // sits between the pairs, and X takes the slot of the unknown.
  constexpr int16_t kRight[4]={44,86,140,182};
  constexpr int16_t kSlot=36,kTop=20,kBaseline=29,kUnderline=30;
  const int unknown=view.unknownLast?3:2;
  auto glyph=[&](char ch,int16_t inkLeft){
    const auto* g=MathFont::find(static_cast<uint8_t>(ch),kScript);
    if(g)MathFont::draw(c,*g,static_cast<int16_t>(inkLeft-g->inkLeft()),kBaseline,1);
  };
  glyph(':',static_cast<int16_t>(kRight[0]+3));
  glyph('=',94);
  glyph(':',static_cast<int16_t>(kRight[2]+3));
  inkRight(c,"X",kScript,static_cast<int16_t>(kRight[unknown]-kSlot+1),kRight[unknown],kBaseline);
  int field=0;
  for(int slot=0;slot<4;++slot){
    if(slot==unknown)continue;
    const int16_t left=static_cast<int16_t>(kRight[slot]-kSlot+1);
    const bool active=field==view.selected;
    if(active)c.fillRect(left,kTop,kSlot,kBaseline-kTop+1,1);
    inkRight(c,view.cells[static_cast<size_t>(field)],kScript,left,kRight[slot],kBaseline,active?0:1);
    ++field;
  }
  footerValue(c,view.footer,view.draft);
  gray().blit1bpp(c,3);
  for(int slot=0;slot<4;++slot)
    if(slot!=unknown)gray().fillRect(static_cast<int16_t>(kRight[slot]-kSlot+1),kUnderline,kSlot,1,2);
  const auto& raw=gray().packed();
  return CanvasText::packBitsRle(raw.data(),raw.size());
}

std::vector<uint8_t> renderSolverEntry(const MathEditor& editor,const Settings& s,
                                     bool busy,Viewport& view,bool caretOn){
  auto frame=renderCalculation(editor,s,nullptr,Format::Standard,busy,false,view,"",caretOn);
  if(busy||!editor.text().empty()||!editor.notice.empty())return frame;
  GFXcanvas1 c(192,63);c.fillScreen(0);
  const auto style=MathFont::Style::Normal;
  paintLabel(c,"Input Equation",192-labelAdvance("Input Equation",style),61,style);
  return packed(c);
}

std::vector<uint8_t> renderSolverSeed(const std::string& label,const MathEditor& editor,int selected,Viewport* viewport,bool caretOn){
  gray().clear();info=RenderInfo{};
  GFXcanvas1 c(192,63);c.fillScreen(0);
  const auto style=MathFont::Style::Normal;
  paintLabel(c,"Enter Initial",0,13,style);
  paintLabel(c,"Value",0,29,style);
  if(!viewport)c.fillRect(0,32+selected*16,188,15,1);
  if(viewport){
    const std::string prefix=label+"=";
    const int valueX=labelAdvance(prefix.c_str(),style),width=188-valueX;
    paintLabel(c,prefix.c_str(),0,45,style);
    inputDocument.rebuild(editor.text());
    const auto linear=LinearPresenter::present(inputDocument);
    const size_t column=LinearPresenter::displayColumnForSource(linear,editor.cursor());
    const int cursorX=labelAdvance(linear.display.substr(0,column).c_str(),style);
    if(cursorX-viewport->x>width-2)viewport->x=cursorX-width+2;
    if(cursorX<viewport->x)viewport->x=cursorX;
    viewport->x=std::max<int16_t>(0,viewport->x);
    GFXcanvas1 value(width,15);value.fillScreen(0);
    paintLabel(value,linear.display.c_str(),-viewport->x,13,style);
    if(caretOn)value.fillRect(cursorX-viewport->x,0,2,15,1);
    for(int y=0;y<15;++y)for(int x=0;x<width;++x)
      if(value.getPixel(x,y))c.drawPixel(valueX+x,32+y,1);
    info.input={static_cast<int16_t>(valueX),32,static_cast<int16_t>(width),15};
    info.caret=caretOn;
    info.caretRect={static_cast<int16_t>(valueX+cursorX-viewport->x),32,2,15};
    info.caretKind=editor.caretKind();
  }else paintLabel(c,(label+"="+editor.text()).c_str(),0,45,style,selected==0?0:1);
  const uint16_t ink=selected==1&&!viewport?0:1;
  executeMark(c,48,ink);
  paintLabel(c,editor.notice.empty()?"Execute":editor.notice.c_str(),12,61,style,ink);
  return packed(c);
}

std::vector<uint8_t> renderSolverResult(const MathEditor& equation,const std::string& label,
                                      const std::string& root,const std::string& residual,int16_t& scroll){
  gray().clear();info=RenderInfo{};
  GFXcanvas1 c(192,63);c.fillScreen(0);
  inputDocument.rebuild(equation.text());inputLayout.layout(inputDocument);
  inputLayout.paint(gray(),0,inputLayout.box().ascent,{0,0,192,32});
  const auto equationInk=inputLayout.ink();
  const int16_t equationBottom=static_cast<int16_t>(inputLayout.box().ascent+equationInk.y+equationInk.height);
  const auto style=MathFont::Style::Normal;
  const std::string values[]={root,residual};
  const std::string labels[]={label+"=","L-R="};
  for(int row=0;row<2;++row){
    const int16_t baseline=static_cast<int16_t>(47+row*14);
    paintLabel(c,labels[row].c_str(),0,baseline,style);
    resultDocument.rebuild(presentResult(values[row]));resultLayout.layout(resultDocument);
    const auto ink=resultLayout.ink();
    if(row){resultLayout.paint(gray(),192-ink.x-ink.width,baseline,{30,49,162,14});continue;}
    // The root may be a tall fraction/radical: it rises toward the equation
    // (never below row 48) instead of being clipped to one text row, and a
    // root wider than its band scrolls with Left/Right behind arrows.
    const int16_t top=std::max<int16_t>(17,static_cast<int16_t>(equationBottom+1));
    const int16_t rootBaseline=std::min<int16_t>(baseline,static_cast<int16_t>(48-ink.y-ink.height));
    constexpr int16_t kBandLeft=30,kBandRight=191;
    const int16_t overflow=static_cast<int16_t>(std::max<int>(0,ink.width-(kBandRight-kBandLeft)));
    scroll=std::max<int16_t>(0,std::min<int16_t>(scroll,overflow));
    const bool left=overflow&&scroll>0,right=overflow&&scroll<overflow;
    MathLayout::Rect clip{static_cast<int16_t>(left?kBandLeft+10:kBandLeft),top,0,static_cast<int16_t>(49-top)};
    clip.width=static_cast<int16_t>((right?kArrowClipRight:192)-clip.x);
    const int16_t x=overflow?static_cast<int16_t>(kBandLeft-ink.x-scroll):static_cast<int16_t>(192-ink.x-ink.width);
    resultLayout.paint(gray(),x,rootBaseline,clip);
    if(left)gray().bits(leftArrow,kBandLeft,38,8,7,3);
    if(right)gray().bits(rightArrow,183,38,8,7,3);
  }
  info.presented=label+"="+root+"; L-R="+residual;
  info.result={0,34,192,29};
  return packed(c);
}

namespace {
// \x01..\x02 spans are formatValue() output, presented as math in Math I/O.
std::string presentSpans(const std::string& value,bool math){
  std::string text;
  for(size_t at=0;at<value.size();){
    const size_t open=value.find('\x01',at);
    if(open==std::string::npos){text+=value.substr(at);break;}
    const size_t close=value.find('\x02',open);
    const std::string span=value.substr(open+1,close==std::string::npos?std::string::npos:close-open-1);
    text+=value.substr(at,open-at)+(math?presentResult(span):span);
    if(close==std::string::npos)break;
    at=close+1;
  }
  return text;
}
}  // namespace

std::vector<uint8_t> renderEquationResult(const std::string& header,const std::string& label,
                                          const std::string& value,const Settings& s,int16_t& scroll,bool labelAtTop){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};
  const bool math=s.io==IO::MathMath||s.io==IO::MathDecimal;
  const std::string text=presentSpans(value,math);
  info.presented=text;
  c.setTextColor(1);
  // Header lines at baselines 16 and 30 (superscripts clear row 0); a caption
  // too wide for the panel breaks at its last fitting space.
  std::vector<std::string> lines;
  for(std::string rest=header;!rest.empty();){
    GFXcanvas1 probe(1,1);
    size_t cut=rest.size();
    while(paintFormula(probe,rest.substr(0,cut),1,0,0)>191){
      const size_t space=rest.rfind(' ',cut-1);
      if(space==std::string::npos||space==0)break;
      cut=space;
    }
    lines.push_back(rest.substr(0,cut));
    rest=cut<rest.size()?rest.substr(cut+1):std::string();
  }
  // The topmost text (the first header line, superscripts included, or the
  // label when there is no header) puts its highest ink on row kTopMargin:
  // measure its ink top on a probe and place the baseline from that.
  auto topBaseline=[](const std::string& text){
    constexpr int16_t kProbe=30,kTopMargin=3;
    GFXcanvas1 probe(192,63);probe.fillScreen(0);
    paintFormula(probe,text,1,kProbe,1);
    for(int16_t y=0;y<63;++y)for(int16_t x=0;x<192;++x)if(probe.getPixel(x,y))return static_cast<int16_t>(kProbe-y+kTopMargin);
    return kProbe;
  };
  constexpr int16_t kLinePitch=14,kLabelGap=12;
  const size_t shown=std::min<size_t>(lines.size(),2);
  const int16_t headBaseline=shown?topBaseline(lines[0]):0;
  for(size_t i=0;i<shown;++i)paintFormula(c,lines[i],1,static_cast<int16_t>(headBaseline+kLinePitch*i),1);
  // Label: under the header, or (Simul/Solver, no header) on the top row.
  (void)labelAtTop;
  const std::string labelText=label+"=";
  const int16_t labelBaseline=shown==0?(label.empty()?0:topBaseline(labelText))
                                      :static_cast<int16_t>(headBaseline+kLinePitch*(shown-1)+kLabelGap+(shown==1?2:0));
  if(!label.empty())paintFormula(c,labelText,1,labelBaseline,1);
  const int16_t valueTop=shown==0?0:static_cast<int16_t>(headBaseline+3+kLinePitch*(shown-1));
  resultDocument.rebuild(text);resultLayout.layout(resultDocument);
  const auto ri=resultLayout.ink();
  // Right-aligned when it fits; a longer answer starts at its head and
  // scrolls with Left/Right behind the input's arrows, never cut silently.
  constexpr int16_t kRight=191,kLeft=1;
  const int16_t overflow=static_cast<int16_t>(std::max<int>(0,ri.width-(kRight-kLeft)));
  scroll=std::max<int16_t>(0,std::min<int16_t>(scroll,overflow));
  info.resultOverflow=overflow;
  const int16_t x=overflow?static_cast<int16_t>(kLeft-ri.x-scroll):static_cast<int16_t>(kRight-ri.x-ri.width);
  const int16_t baseline=static_cast<int16_t>(61-ri.y-ri.height);
  MathLayout::Rect clip{0,valueTop,192,static_cast<int16_t>(63-valueTop)};
  const bool left=overflow&&scroll>0,right=overflow&&scroll<overflow;
  if(left){clip.width-=kArrowClipLeft;clip.x=kArrowClipLeft;}
  if(right)clip.width=static_cast<int16_t>(kArrowClipRight-clip.x);
  info.result=clip;
  resultLayout.paint(gray(),x,baseline,clip);
  constexpr int16_t arrowY=52;  // centred on the bottom line's digits
  if(left)gray().bits(leftArrow,2,arrowY,8,7,3);
  if(right)gray().bits(rightArrow,183,arrowY,8,7,3);
  return packed(c);
}

std::vector<uint8_t> renderSolution(const std::string& label,const std::string& value,const Settings& s){
  gray().clear();
  GFXcanvas1 c(192,63);c.fillScreen(0);
  info=RenderInfo{};
  const bool math=s.io==IO::MathMath||s.io==IO::MathDecimal;
  std::string text;
  for(size_t at=0;at<value.size();){
    const size_t open=value.find('\x01',at);
    if(open==std::string::npos){text+=value.substr(at);break;}
    const size_t close=value.find('\x02',open);
    const std::string span=value.substr(open+1,close==std::string::npos?std::string::npos:close-open-1);
    text+=value.substr(at,open-at)+(math?presentResult(span):span);
    if(close==std::string::npos)break;
    at=close+1;
  }
  info.presented=text;
  resultDocument.rebuild(text);resultLayout.layout(resultDocument);
  const MathLayout::Box& rb=resultLayout.box();
  if(label.empty()){
    resultLayout.paint(gray(),M::kMarginLeft,static_cast<int16_t>(M::kMarginTop+rb.ascent),{0,0,192,63});
    return packed(c);
  }
  c.setTextColor(1);
  paintLabel(c,(label+"=").c_str(),M::kMarginLeft,14,MathFont::Style::Normal);
  const auto ri=resultLayout.ink();
  const int16_t x=std::max<int16_t>(M::kMarginLeft,192-ri.x-ri.width);
  info.result={0,17,192,46};
  resultLayout.paint(gray(),x,static_cast<int16_t>(61-ri.y-ri.height),info.result);
  return packed(c);
}
}
