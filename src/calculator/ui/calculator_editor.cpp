#include "calculator/ui/calculator_editor.h"
#include "calculator/ui/calculator_document.h"
#include "calculator/ui/calculator_layout.h"
#include "calculator/math/calculator_loose_paren.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>
namespace Calculator {
namespace {
using Kind=EditDocument::Kind;
constexpr uint16_t kNone=EditDocument::kNone;
// One scratch document/layout for every editor: editing runs on the UI loop
// only, one key at a time, and each use rebuilds from the editor's own text.
EditDocument& document(const std::string& text){static EditDocument d;d.rebuild(text);return d;}
// Field `f` sits (at any depth) inside a natural template, whose serialized
// ',' and ')' a real unmatched paren would swallow (see LooseParen).
bool natural(const EditDocument& d,uint16_t f){
  for(uint16_t owner=d.field(f).owner;owner!=kNone;owner=d.field(d.item(owner).field).owner)
    if(d.hidden(owner))return true;
  return false;
}
// A group or one-argument call whose parens canonicalize() may re-pair.
// Multi-argument calls keep theirs: loosening them would free their commas.
bool pairable(const EditDocument::Item& it){
  return it.kind==Kind::Group||(it.kind==Kind::Call&&it.fieldCount==1);
}
// Deleting one paren of a pair inside a template field: first turn both into
// loose parens in place (same length, so every offset holds), so the partner
// survives as plain text instead of re-parsing the template around it.
void loosen(const EditDocument& d,uint16_t i,std::string& text){
  const EditDocument::Item& it=d.item(i);
  if(!pairable(it)||!natural(d,it.field))return;
  text[it.kind==Kind::Group?it.begin:it.nameEnd]=LooseParen::kOpen;
  if(it.closed)text[it.end-1]=LooseParen::kClose;
}
// P4/E-10: only wrappers whose removal-with-splice is reference-covered.
// Fraction and legacy Power concatenate every field in order; a postfix
// Script keeps its exponent after "^(" and ")" go away. Every other template
// (Mixed, Log, Root, Integral, Sum, Product, ...) keeps the old
// previous-field-end move; its splice stays pending, never inferred.
bool spliceOnDelete(Kind kind){return kind==Kind::Fraction||kind==Kind::Power||kind==Kind::Script;}
// P4/E-08: structural "nothing logically to the left": the root start, or
// the start of a first field whose owner itself starts where nothing is
// left (C28: a numerator start inside a fraction sitting at the root start).
bool nothingLeftOf(const EditDocument& d,size_t offset){
  const uint16_t f=d.slotField(offset);
  if(f==kNone)return false;
  const EditDocument::Field& field=d.field(f);
  if(field.owner==kNone)return offset==0;
  return field.index==0&&nothingLeftOf(d,d.item(field.owner).begin);
}
// P5/E-11: INS-eligible template keys. Target evidence covers unshifted Root
// only: C40 plus the manual's 1+7/6-becomes-sqrt-argument case. Shifted Root
// (nth-root) and every other structural key stay cancelling keys until
// target evidence arrives (pending C96/C100); the set extends here, with its
// reference test, not across switch branches.
bool isInsTemplateKey(ExpressionInput::Key key,bool shift){
  return key==ExpressionInput::Key::Root&&!shift;
}
}
void MathEditor::begin(const std::string& value){text_=value.size()>kLimit?value.substr(0,kLimit):value;cursor_=text_.size();shifted=false;review=false;insArmed_=false;notice.clear();undo_=Snapshot{};settle();}
void MathEditor::settle(){cursor_=document(text_).normalize(cursor_);}
void MathEditor::canonicalize(){
  // Re-pair every paren inside each template field (a natural field, or one
  // argument of a multi-argument call inside one) as the calculator would
  // read the keystrokes: a matched pair is the real "()" -- a group, or a
  // call after a name -- and an unmatched one the loose byte. Groups and
  // one-argument calls are walked through; templates and multi-argument
  // calls are their own fields. Every rewrite is one byte for one byte, so
  // the cursor offset holds (settle() re-anchors it).
  if(text_.find_first_of(std::string("()")+LooseParen::kOpen+LooseParen::kClose)==std::string::npos)return;
  const EditDocument& d=document(text_);
  std::string out=text_;
  std::vector<bool> seen(text_.size(),false);
  struct Paren{uint16_t at;bool open;};
  std::vector<Paren> parens;std::vector<uint16_t> heads;std::vector<size_t> open;
  for(uint16_t f=0;f<d.fields();++f){
    const uint16_t owner=d.field(f).owner;
    if(owner==kNone||!(d.hidden(owner)||d.item(owner).kind==Kind::Call)||pairable(d.item(owner))||!natural(d,f))continue;
    parens.clear();heads.assign(1,d.field(f).first);
    while(!heads.empty()){  // explicit stack: nesting never grows the loop task's stack
      uint16_t i=heads.back();heads.pop_back();
      for(;i!=kNone;i=d.item(i).next){
        const EditDocument::Item& it=d.item(i);
        if(it.kind==Kind::Glyph&&LooseParen::is(text_[it.begin]))parens.push_back({it.begin,text_[it.begin]==LooseParen::kOpen});
        else if(pairable(it)){
          parens.push_back({it.kind==Kind::Group?it.begin:it.nameEnd,true});
          if(it.closed)parens.push_back({static_cast<uint16_t>(it.end-1),false});
          heads.push_back(d.field(it.firstField).first);
        }
      }
    }
    std::sort(parens.begin(),parens.end(),[](const Paren& a,const Paren& b){return a.at<b.at;});
    open.clear();
    for(size_t k=0;k<parens.size();++k){
      const Paren& p=parens[k];
      seen[p.at]=true;out[p.at]=p.open?LooseParen::kOpen:LooseParen::kClose;
      if(p.open)open.push_back(k);
      else if(!open.empty()){out[parens[open.back()].at]='(';out[p.at]=')';open.pop_back();}
    }
  }
  // Outside every template field (a splice can carry one there) an unmatched
  // paren is ordinary text again: the root has no separators to swallow.
  for(size_t o=0;o<out.size();++o)if(LooseParen::is(out[o])&&!seen[o])out[o]=LooseParen::ascii(out[o]);
  text_.swap(out);
}
void MathEditor::setCursor(size_t offset){cursor_=offset;settle();}
void MathEditor::undo(){if(!undo_.valid)return;std::swap(text_,undo_.text);std::swap(cursor_,undo_.cursor);insArmed_=false;settle();}
bool MathEditor::replace(size_t from,size_t to,const std::string& value){
  if(text_.size()-(to-from)+value.size()>kLimit){notice="Input limit";return false;}
  text_.replace(from,to-from,value);return true;
}
void MathEditor::insert(const std::string& value){
  Edit t(*this);
  const EditDocument& d=document(text_);
  const EditDocument::Field& field=d.field(d.slotField(cursor_));
  // An open call inside a template must not consume the enclosing field
  // separators. Keep its closing delimiters in the source and leave the caret
  // inside, including when nested in an ordinary call. (The '(' and ')' keys
  // insert loose parens there instead; see paren().)
  if(natural(d,d.slotField(cursor_))){
    int depth=0;
    for(char c:value){if(c=='(')++depth;else if(c==')')--depth;}
    if(depth>0){
      const std::string before=text_;
      insert(value+std::string(static_cast<size_t>(depth),')'));
      if(text_!=before){cursor_-=static_cast<size_t>(depth);settle();}
      return;
    }
  }
  // A ')' or ',' at nesting depth 0, typed straight into a hidden template
  // field, would end that field in the serialized text; the Close key types
  // a loose paren instead (paren()). Balanced insertions such as "sin()" are fine.
  if(field.owner!=kNone&&d.hidden(field.owner)){
    int depth=0;
    for(char c:value){if(c=='(')++depth;else if(c==')'&&depth)--depth;else if(c==')'||(c==','&&!depth)){notice="Not available here";return;}}
  }
  size_t to=cursor_;
  if(overwrite&&value.size()==1){const uint16_t next=d.itemStartingAt(cursor_,d.slotField(cursor_));if(next!=kNone&&d.item(next).kind==Kind::Glyph)to=d.item(next).end;}
  // The evaluator lexes a letter-led name through every following letter and
  // digit, so Ans then 7 would serialize as the unknown name "Ans7" (and pi
  // before 2 as "pi2"). Keep adjacent tokens apart with an explicit product,
  // which is what the keypresses meant.
  const auto word=[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';};
  const uint16_t previous=d.itemEndingAt(cursor_,d.slotField(cursor_));
  const bool before=!value.empty()&&word(value[0])&&previous!=kNone&&d.item(previous).kind==Kind::Name&&
                    std::isalpha(static_cast<unsigned char>(text_[d.item(previous).begin]));
  // The inserted text ends in a name (not a digit run or a single-letter
  // variable) that would run on into a following letter or digit.
  size_t tail=value.size();while(tail&&word(value[tail-1]))--tail;
  while(tail<value.size()&&std::isdigit(static_cast<unsigned char>(value[tail])))++tail;
  const char lead=tail<value.size()?value[tail]:'\0';
  const bool single=value.size()-tail==1&&((lead>='A'&&lead<='F')||lead=='x'||lead=='y'||lead=='z');
  const bool after=std::isalpha(static_cast<unsigned char>(lead))&&!single&&to<text_.size()&&word(text_[to]);
  if(!replace(cursor_,to,(before?"*":"")+value+(after?"*":"")))return;
  cursor_+=(before?1:0)+value.size();settle();
}
void MathEditor::insertTemplate(const std::string& value){
  Edit t(*this);
  const size_t start=cursor_;const std::string before=text_;insert(value);
  if(text_==before)return;
  const EditDocument& d=document(text_);
  for(size_t o=start;o<=start+value.size();++o){
    const uint16_t f=d.slotField(o);
    if(f!=kNone&&d.field(f).begin==d.field(f).end){cursor_=o;return;}
  }
}
void MathEditor::wrap(const std::string& function,unsigned fields){
  Edit t(*this);
  const size_t start=document(text_).operandStart(cursor_);
  const std::string operand=text_.substr(start,cursor_-start);
  if(!replace(start,cursor_,function+"("+operand+std::string(fields-1,',')+")"))return;
  cursor_=start+function.size()+1+operand.size()+(!operand.empty()&&fields>1?1:0);settle();
}
void MathEditor::wrapFilled(const std::string& function,const std::string& filler){
  // Fraction-adjacent legacy helper (x^2/x^-1 used it before P2). Power keys
  // now go through insertScript(); this stays for any prefix template that
  // still binds its base at key time.
  const size_t start=document(text_).operandStart(cursor_);
  const std::string value=function+"("+text_.substr(start,cursor_-start)+","+filler+")";
  const bool empty=start==cursor_;
  if(!replace(start,cursor_,value))return;
  cursor_=empty?start+function.size()+1:start+value.size();settle();
}
void MathEditor::insertScript(const std::string& exponent,bool cursorInside){
  // Postfix power (P2/E-17): the base is whatever atom precedes the cursor at
  // layout/evaluation time, exactly like the calculator's postfix power token.
  // Nothing is wrapped at key time, so there is no base-start stop and typing
  // before the power later joins the base (START 2 on 9^3 gives 29^3).
  // With no atom before the cursor the authentic behavior is UNKNOWN; keep
  // the legacy pow(,) fallback so an empty base slot still exists. That path
  // is covered by an explicit pending reference case, never by inference.
  Edit t(*this);
  if(document(text_).operandStart(cursor_)==cursor_){
    if(exponent.empty()){wrap("pow",2);return;}
    wrapFilled("pow",exponent);return;
  }
  const std::string script="^("+exponent+")";
  const size_t at=cursor_;
  const std::string before=text_;
  insert(script);
  if(text_==before)return;
  // insert() leaves the cursor after the new script (a valid stop) and never
  // injects '*' around '^'/parentheses; move inside when the exponent is
  // still to fill (ordinary Power). settle() normalizes to the owning field.
  cursor_=cursorInside?at+2:at+script.size();settle();
}
void MathEditor::wrapWith(const std::string& prefix,const std::string& suffix){
  Edit t(*this);
  const size_t start=document(text_).operandStart(cursor_);
  const std::string value=prefix+text_.substr(start,cursor_-start)+suffix;
  if(!replace(start,cursor_,value))return;
  cursor_=start+value.size();settle();
}
void MathEditor::wrapFollowing(const std::string& function,unsigned fields){
  // P5/E-11: INS consumption. [from, operandEnd(from)) is one complete
  // following operand (a number with its E-tail, a variable, a name, one
  // whole structure, plus postfix modifiers) or nothing. The template owns
  // that span; the cursor lands in its first empty field when one exists
  // (an nth-root index), otherwise at the wrapped operand's start so the
  // next digit extends it (C40: 2 lands before 9, giving 29^3).
  Edit t(*this);
  insArmed_=false;
  const size_t from=cursor_;
  const size_t to=document(text_).operandEnd(cursor_);
  if(to==from){insertTemplate(function+"("+(fields>1?",":"")+")");return;}
  const std::string operand=text_.substr(from,to-from);
  const std::string value=fields>1?function+"(,"+operand+")":function+"("+operand+")";
  if(!replace(from,to,value))return;
  const EditDocument& d=document(text_);
  for(size_t o=from;o<=from+value.size();++o){
    const uint16_t fl=d.slotField(o);
    if(fl!=kNone&&d.field(fl).begin==d.field(fl).end){cursor_=o;settle();return;}
  }
  cursor_=from+function.size()+1;settle();
}
void MathEditor::left(){
  const EditDocument& d=document(text_);
  const size_t next=d.left(cursor_);
  // P3/E-05: wrap only at the root field's first stop. A no-op inside a
  // nested field (no horizontal target there) stays put, never wraps the
  // whole expression.
  if(next==cursor_&&wrapHorizontal){
    const uint16_t f=d.slotField(cursor_);
    if(f!=kNone&&d.field(f).owner==kNone&&cursor_==d.field(d.root()).begin){cursor_=d.normalize(text_.size());return;}
  }
  cursor_=next;
}
void MathEditor::right(){
  const EditDocument& d=document(text_);
  uint16_t open=kNone;const size_t next=d.right(cursor_,&open);
  if(open==kNone){
    // P3/E-04: wrap only at the root field's final stop; see left().
    if(next==cursor_&&wrapHorizontal){
      const uint16_t f=d.slotField(cursor_);
      if(f!=kNone&&d.field(f).owner==kNone&&cursor_==d.field(d.root()).end){cursor_=0;return;}
    }
    cursor_=next;return;
  }
  // Leaving a template the source never closed writes its ')' -- as its own
  // undoable edit -- rather than drawing a parent-level caret while typing
  // would still land in the child.
  if(replace(text_.size(),text_.size(),")"))cursor_=text_.size();
}
void MathEditor::paren(bool open){
  // '(' and ')' type exactly one paren, never a pair and never a jump out of
  // a template. Inside a template field it goes in loose and canonicalize()
  // pairs it with a partner in that field; elsewhere it is the real one (a
  // root ')' closes the group or call before it).
  const EditDocument& d=document(text_);
  if(natural(d,d.slotField(cursor_)))insert(std::string(1,open?LooseParen::kOpen:LooseParen::kClose));
  else insert(open?"(":")");
}
void MathEditor::vertical(bool up){
  const EditDocument& d=document(text_);
  uint16_t target=d.vertical(d.slotField(cursor_),up);
  if(target==kNone)return;
  // The target may itself contain a nested fraction: enter its bottom box
  // when moving up, its top box when moving down, so the caret lands inside
  // the middle box rather than on the container's left edge.
  target=d.verticalEnter(target,up);
  static MathLayout layout;layout.layout(d);
  cursor_=layout.nearest(target,layout.slot(cursor_).x);
}
void MathEditor::backspace(){
  const EditDocument& d=document(text_);
  const uint16_t f=d.slotField(cursor_);
  const EditDocument::Field& field=d.field(f);
  if(cursor_==field.begin){
    // P4/E-08: DEL with nothing logically to the left deletes forward (C27:
    // root start removes the next item instead of doing nothing).
    if(field.owner==kNone){if(cursor_==0)deleteForward(f);return;}
    const EditDocument::Item& owner=d.item(field.owner);
    if(!d.hidden(field.owner)){
      // Calls and groups show their parentheses and commas, so each one is
      // an ordinary character to delete. A call's name goes with its '('
      // (one token), and an empty closed call such as "cos(▌)" goes whole
      // rather than stranding its ')'.
      const size_t from=field.index?field.begin-1:owner.begin;
      const size_t to=!field.index&&owner.kind==Kind::Call&&owner.closed&&d.allEmpty(field.owner)?owner.end:field.begin;
      if(to!=owner.end)loosen(d,field.owner,text_);  // its ')' stays, as a loose paren
      replace(from,to,"");cursor_=from;return;
    }
    if(d.allEmpty(field.owner)){replace(owner.begin,owner.end,"");cursor_=owner.begin;return;}
    // P4/E-10: DEL at the start of a later field removes the wrapper and
    // splices every field's contents in order (C31: "1/2" at the denominator
    // start becomes "1▌2"). Pending kinds keep the previous-field-end move.
    if(field.index&&spliceOnDelete(owner.kind)){splice(field.owner,field.index);return;}
    if(field.index){cursor_=d.field(d.fieldAt(field.owner,field.index-1)).end;return;}
    // P4/C32: a non-empty postfix Script owns only its Exponent field, so
    // the index>0 gate above cannot see it. DEL at the exponent start drops
    // "^(" and ")" around the preserved exponent ("9^(▌3)" -> "9▌3").
    if(owner.kind==Kind::Script){splice(field.owner,0);return;}
    if(owner.kind==Kind::Sqrt||owner.kind==Kind::Abs){
      // Unwrap: the radicand or argument stays, as one undoable edit.
      const std::string content=text_.substr(field.begin,field.end-field.begin);
      replace(owner.begin,owner.end,content);cursor_=owner.begin;return;
    }
    // P4/E-08: the structure itself has nothing before it (C28: a numerator
    // start inside a fraction at the root start), so delete this field's
    // first item instead of stepping out (C29 keeps the step-out: "2+▌1/□"
    // moves before the fraction and deletes nothing).
    if(nothingLeftOf(d,owner.begin)){deleteForward(f);return;}
    cursor_=owner.begin;return;  // never merge fields silently
  }
  const uint16_t p=d.itemEndingAt(cursor_,f);
  const EditDocument::Item& item=d.item(p);
  if(d.atomic(p)){replace(item.begin,item.end,"");cursor_=item.begin;return;}
  if(!d.hidden(p)){loosen(d,p,text_);replace(item.end-1,item.end,"");cursor_=item.end-1;return;}  // the visible ')'
  if(d.allEmpty(p)){replace(item.begin,item.end,"");cursor_=item.begin;return;}
  // P4/E-09: DEL immediately after a structure enters its last field and
  // deletes that field's last item in the same action (C33: after "1/2" one
  // DEL yields "1/□" with the cursor in the denominator). Bounded: the call
  // below either erases (terminating) or moves strictly inside the structure,
  // and it runs only when the last field holds something to delete. The last
  // field's range is captured up front so no document reference is used after
  // the recursive call mutates the source (backspace() rebuilds on entry).
  const uint16_t last = d.lastField(p);
  const size_t lastBegin = d.field(last).begin, lastEnd = d.field(last).end;
  cursor_ = lastEnd;
  if (lastEnd != lastBegin) backspace();
}
void MathEditor::deleteForward(uint16_t f){
  // Erases the logical item starting at the cursor; the cursor itself stays
  // at its insertion point (forward delete, not backspace). Hidden template
  // syntax is never cut byte-wise: an empty wrapper goes whole, a complete
  // one is descended into. settle() re-anchors the untouched cursor.
  const EditDocument& d=document(text_);
  uint16_t field=f;
  size_t pos=cursor_;
  for(;;){
    const uint16_t n=d.itemStartingAt(pos,field);
    if(n==kNone)return;  // nothing to the right (e.g. an empty first field)
    const EditDocument::Item& item=d.item(n);
    if(d.atomic(n)){replace(item.begin,item.end,"");settle();return;}
    // A call's name and '(' are one key's token ("cos(" is one keypress), so
    // forward DEL takes them together, as backward DEL at the argument start
    // does; an empty call goes whole. Never letter by letter ("cos(" -> "os("
    // -> "s(" left a different, unknown call behind). Groups show one
    // ordinary '(' and lose just that.
    if(item.kind==Kind::Call){
      const bool whole=d.allEmpty(n)&&item.closed;
      if(!whole)loosen(d,n,text_);
      replace(item.begin,whole?item.end:item.nameEnd+1,"");settle();return;
    }
    if(!d.hidden(n)){loosen(d,n,text_);replace(item.begin,item.begin+1,"");settle();return;}
    if(d.allEmpty(n)){replace(item.begin,item.end,"");settle();return;}
    field=d.item(n).firstField;
    pos=d.field(field).begin;
  }
}
void MathEditor::splice(uint16_t owner,uint8_t atIndex){
  // One mutation: the wrapper [begin,end) becomes the concatenation of its
  // fields' contents in reference order. All ranges and bytes are captured
  // before replace() invalidates the document; the cursor lands at the start
  // of the atIndex field's preserved content. settle() normalizes the stop.
  const EditDocument& d=document(text_);
  const size_t begin=d.item(owner).begin,end=d.item(owner).end;
  std::string kept;
  size_t boundary=0;
  uint8_t index=0;
  for(uint16_t f=d.item(owner).firstField;f!=kNone;f=d.field(f).next,++index){
    if(index==atIndex)boundary=kept.size();
    const EditDocument::Field& field=d.field(f);
    kept+=text_.substr(field.begin,field.end-field.begin);
  }
  if(index<=atIndex)boundary=kept.size();
  if(!replace(begin,end,kept))return;
  cursor_=begin+boundary;
  settle();
}
void MathEditor::press(ExpressionInput::Key key){
  using K=ExpressionInput::Key;notice.clear();
  if(key==K::Interrupted){shifted=false;review=true;insArmed_=false;return;}
  if(key==K::Shift){shifted=!shifted;return;}
  const bool shift=shifted;shifted=false;
  // SHIFT+AC is not undo: it switches the device off, and UserInput consumes
  // the AC before any editor sees it. Undo lives in TOOLS.
  // P5/E-11: an armed INS survives only to an eligible template key. Shift
  // itself never reaches here, so every other key — even one rejected below —
  // disarms it. Base-N has no template keys, so INS never survives there
  // either. Menu opens disarm through disarmIns() in the controller.
  const bool consumeIns=insArmed_&&!baseNInput&&isInsTemplateKey(key,shift);
  if(!consumeIns)insArmed_=false;
  // P5/E-13: one outer transaction per logical operation. Nested public
  // calls (insertTemplate->insert, insertScript->insert/wrap) share it; only
  // a real source change publishes undo_, so cursor moves and rejected edits
  // never fork or destroy the previous undo state.
  Edit t(*this);
  if(baseNInput){
    const bool digit=key>=K::Digit0&&key<=K::Digit9;
    if(digit&&shift&&(key==K::Digit0||key>=K::Digit7))return;
    if(!digit&&key!=K::Add&&key!=K::Subtract&&key!=K::Multiply&&key!=K::Divide&&key!=K::Open&&key!=K::Close&&
       key!=K::Delete&&key!=K::Clear&&key!=K::Left&&key!=K::Right&&key!=K::PageUp&&key!=K::PageDown&&key!=K::Ans)return;
    if(shift&&(key==K::Open||key==K::Close||key==K::Ans||key==K::Multiply||key==K::Divide))return;
  }
  if(key>=K::Digit0&&key<=K::Digit9){
    const int digit=static_cast<int>(key)-static_cast<int>(K::Digit0);
    static const char* shiftedDigits[]={"x","D","E","F","A","B","C","pi","e",nullptr};
    if(shift){if(shiftedDigits[digit])insert(shiftedDigits[digit]);else if(complexInput)insert("i");else notice="Math ERROR: real values only";}
    else insert(std::string(1,static_cast<char>('0'+digit)));
  }else switch(key){
    case K::Left:
      if(shift){
        // P5/E-12: inside Mixed/Integral/Sum/Product jump before the
        // innermost such template; elsewhere keep source start (UNKNOWN).
        const EditDocument& d=document(text_);
        const uint16_t owner=d.jumpEnclosing(cursor_);
        cursor_=owner!=kNone?d.item(owner).begin:0;
      }else left();
      break;
    case K::Right:
      if(shift){
        const EditDocument& d=document(text_);
        const uint16_t owner=d.jumpEnclosing(cursor_);
        cursor_=owner!=kNone?d.item(owner).end:text_.size();
      }else right();
      break;
    case K::Up:vertical(true);break;case K::Down:vertical(false);break;
    case K::PageUp:cursor_=0;break;case K::PageDown:cursor_=text_.size();break;
    case K::Delete:
      // P5/E-11: Math input arms INS (no source change, no undo step);
      // Linear input keeps the overwrite toggle. Base-N has no template
      // keys, so INS cannot be consumed there and keeps the legacy toggle.
      if(shift){if(mathInput_&&!baseNInput)insArmed_=true;else overwrite=!overwrite;}
      else backspace();
      break;
    case K::Clear:text_.clear();cursor_=0;break;
    case K::Add:insert("+");break;case K::Subtract:insert("-");break;
    case K::Multiply:if(shift)insertTemplate("abs()");else insert("*");break;
    case K::Divide:insert(shift?"%":"/");break;
    case K::Dot:insert(shift?"y":".");break;
    case K::Open:if(shift)insert("=");else paren(true);break;
    case K::Close:
      if(!shift){paren(false);break;}
      // Shift+) types a comma: inside a natural template it advances to the
      // next field (past the hidden comma); elsewhere it inserts one.
      {const EditDocument& d=document(text_);
       const uint16_t slot=d.slotField(cursor_);
       const EditDocument::Field& field=d.field(slot);
       if(field.owner!=kNone&&d.hidden(field.owner)){
         const uint16_t next=d.fieldAt(field.owner,static_cast<uint8_t>(field.index+1));
         cursor_=next!=kNone?d.field(next).end:d.item(field.owner).end;
         break;
       }
       insert(",");}break;
    case K::Fraction:wrap(shift?"mixed":"frac",shift?3:2);break;
    case K::Root:
      if(consumeIns)wrapFollowing(shift?"root":"sqrt",shift?2:1);
      else insertTemplate(shift?"root(,)":"sqrt()");
      break;
    case K::Square:if(shift)insertTemplate("log(");else insertScript("2",false);break;
    case K::Power:if(shift)insertScript("-1",false);else insertScript("",true);break;
    // P5/E-16: ordinary call functions insert open (unclosed). Close types
    // the missing ')'; DEL walks the open structure with the P4 rules.
    case K::Log:insertTemplate(shift?"ln(":"log(,)");break;
    case K::Sin:insertTemplate(shift?"asin(":"sin(");break;
    case K::Cos:insertTemplate(shift?"acos(":"cos(");break;
    case K::Tan:insertTemplate(shift?"atan(":"tan(");break;
    case K::X:insert("x");break;case K::Ans:insert(shift?"PreAns":"Ans");break;
    // P12 E-15: Power inserts canonical *10^(). Sci Notat keeps the same
    // fallback until a reference case resolves it (never invented).
    case K::Scientific:if(shift)insert("z");else insertTemplate("*10^()");(void)scientificPower_;break;
    default:break;
  }
}
}
