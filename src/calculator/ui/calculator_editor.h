#pragma once
#include "input/expression_editor.h"
#include <string>
namespace Calculator {
// Math input as serialized template text (what the evaluator, history and f/g
// store) plus a cursor that is always an EditDocument stop. All structural
// decisions read the document: arrows walk stops, Power appends a postfix
// script bound to the preceding atom, DEL follows structural rules (forward
// delete with nothing to the left, enter-and-delete after a structure, wrapper
// splice at a later field's start), Up/Down pick the nearest stop by drawn x.
// SHIFT+DEL arms INS in Math input (Linear toggles overwrite); armed INS makes
// the next Root key wrap the following operand. SHIFT+Left/Right jump across
// the innermost Mixed/Integral/Sum/Product. Call functions insert unclosed.
// Every public mutator is one outer edit transaction (one undo step). Pixels
// stay in MathLayout.
class MathEditor {
 public:
  static constexpr size_t kLimit=256;
  // Priority InsArmed > Overwrite > Insert (Math INS and Linear overwrite never
  // overlap by policy).
  enum class CaretKind : uint8_t { Insert, Overwrite, InsArmed };
  bool shifted=false,overwrite=false,review=false;
  // Complex app profile: SHIFT 9 types i. Everywhere else it reports real-only.
  bool complexInput=false;
  // Base-N profile: digits, SHIFT 1-6 hex digits, + - x /, parentheses, Ans and
  // editing keys. Everything else (functions, fractions, dot, i) does nothing.
  bool baseNInput=false;
  // Horizontal wrap policy (E-04/E-05): only the main Calculate editor sets it.
  // Policy, not edit state: begin(), setCursor() and undo never clear it.
  bool wrapHorizontal=false;
  // Math vs Linear input semantics (E-11), synced from the Input/Output setting.
  // Configuration: begin(), setCursor() and undo never change it.
  void setMathInput(bool math){mathInput_=math;}
  bool mathInput() const {return mathInput_;}
  // E-15: Power inserts canonical *10^(); Sci Notat keeps the same fallback
  // until a reference case resolves its token form.
  void setScientificPower(bool power){scientificPower_=power;}
  bool scientificPower() const {return scientificPower_;}
  // Transient INS arming (E-11): consumed by the next eligible template key,
  // cleared by anything else (menus call disarmIns()). Never persisted or undone.
  bool insArmed() const {return insArmed_;}
  void disarmIns(){insArmed_=false;}
  CaretKind caretKind() const {
    return insArmed_?CaretKind::InsArmed:overwrite?CaretKind::Overwrite:CaretKind::Insert;
  }
  // Outer-transaction depth, for tests: zero after every complete operation.
  int editDepth() const {return editDepth_;}
  bool hasUndo() const {return undo_.valid;}
  std::string notice;
  void begin(const std::string& value="");
  const std::string& text() const {return text_;}
  size_t cursor() const {return cursor_;}
  // Normalized to the nearest stop: evaluator error positions can point into
  // hidden template syntax.
  void setCursor(size_t offset);
  void press(ExpressionInput::Key key);
  // Tokens at the cursor; the cursor ends after them.
  void insert(const std::string& value);
  // A CATALOG/key template such as "integral(,,)": the cursor enters its
  // first empty field, or ends after it when it has none.
  void insertTemplate(const std::string& value);
  // function(operand,...) around the atom before the cursor; the cursor goes
  // to the first field still to fill.
  void wrap(const std::string& function,unsigned fields=1);
  // Postfix "^(" + exponent + ")"; its base is the preceding atom at layout/
  // evaluation time. With no atom the legacy pow(,) fallback is kept.
  // cursorInside: cursor in the exponent (Power), else after it (x^2, x^-1).
  void insertScript(const std::string& exponent,bool cursorInside);
  // prefix+operand+suffix around that atom, cursor after it (unit conversion).
  void wrapWith(const std::string& prefix,const std::string& suffix);
  // Template around the operand FOLLOWING the cursor, consuming INS (E-11).
  // Cursor at the operand's start (an empty first field wins); nothing after
  // the cursor degrades to an empty template insertion.
  void wrapFollowing(const std::string& function,unsigned fields=1);
  // One step per operation; a second undo redoes (manual's TOOLS > Undo).
  void undo();
 private:
  std::string text_;
  size_t cursor_=0;
  struct Snapshot {std::string text;size_t cursor=0;bool valid=false;} undo_;
  bool mathInput_=true;
  bool scientificPower_=true;
  bool insArmed_=false;
  // Only the outermost Edit publishes undo_, and only if the text changed.
  int editDepth_=0;
  struct Edit {
    MathEditor& e;Snapshot before;bool outer;
    explicit Edit(MathEditor& m):e(m),before{m.text_,m.cursor_,true},outer(m.editDepth_==0){++m.editDepth_;}
    ~Edit(){--e.editDepth_;if(outer)e.canonicalize();e.settle();if(outer&&e.text_!=before.text)e.undo_=std::move(before);}
  };
  bool replace(size_t from,size_t to,const std::string& value);
  void wrapFilled(const std::string& function,const std::string& filler);
  void settle();
  void left();
  void right();
  void vertical(bool up);
  void backspace();
  // E-08: erase the item at/after the cursor when nothing deletable is to its
  // left; the cursor stays put.
  void deleteForward(uint16_t field);
  // E-10: remove a wrapper keeping its fields' contents in order; the cursor
  // lands where field `atIndex`'s content starts.
  void splice(uint16_t owner,uint8_t atIndex);
  // '(' / ')' keys: exactly one paren (loose inside a template field).
  void paren(bool open);
  // Matched parens real, unmatched loose, per template field (LooseParen).
  void canonicalize();
};
}
