#include "calculator/apps/calculator_quick.h"
#include "calculator/ui/calculator_menu.h"
#include "calculator/ui/calculator_render.h"
#include "calculator/ui/calculator_shortcuts.h"
#include <algorithm>
namespace Calculator { namespace QuickOverlay {
namespace { MathEditor* target=nullptr;std::vector<CatalogEntry> entries;int selected=0; }
bool active(){return target!=nullptr;}
void close(){target=nullptr;entries.clear();selected=0;}
bool open(MathEditor& editor,ExpressionInput::Key key,AppId app){
  using K=ExpressionInput::Key;
  if(!editor.shifted||(key!=K::Catalog&&key!=K::X))return false;
  close();editor.shifted=false;editor.disarmIns();
  // SHIFT+x has no function in this build: it only clears SHIFT.
  if(key==K::X)return true;
  entries=quickCatalogEntries(calculatorSession().verify,app);
  if(entries.empty()){editor.notice="Enter an expression first";return true;}
  target=&editor;return true;
}
void press(ExpressionInput::Key key){
  using K=ExpressionInput::Key;if(!target)return;
  if(key==K::Return||key==K::Clear||key==K::Interrupted){close();return;}
  const int count=static_cast<int>(entries.size());
  if(key==K::Down)selected=std::min(count-1,selected+3);
  else if(key==K::Up)selected=std::max(0,selected-3);
  else if(key==K::Right)selected=std::min(count-1,selected+1);
  else if(key==K::Left)selected=std::max(0,selected-1);
  else if(key==K::PageDown)selected=std::min(count-1,(selected/14+1)*14);
  else if(key==K::PageUp)selected=std::max(0,(selected/14-1)*14);
  else if(menuShortcut(key,selected,entries.size())||key==K::Okay||key==K::Execute){target->insertTemplate(entries[selected].expression);close();}
}
std::vector<uint8_t> render(){return renderQuickCatalog(entries,selected);}
} }
