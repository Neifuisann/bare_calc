#include "input/expression_editor.h"

#include <cstring>

namespace ExpressionInput {
namespace {
using K = Key;
// Rows KPO0,KPO1,KPO2,KPO3,KPO6,G0,G1,G2,G3;
// columns KPI2,KPI3,KPI4,KPI5,KPI7,KPI8. ON is not scanned.
constexpr Key kKeys[9][6] = {
    {K::None, K::Execute, K::Format, K::Scientific, K::Dot, K::Digit0},
    {K::None, K::Subtract, K::Add, K::Digit3, K::Digit2, K::Digit1},
    {K::None, K::Divide, K::Multiply, K::Digit6, K::Digit5, K::Digit4},
    {K::None, K::Clear, K::Delete, K::Digit9, K::Digit8, K::Digit7},
    {K::Tools, K::Catalog, K::Down, K::Function, K::Variable, K::Shift},
    {K::PageUp, K::None, K::Up, K::None, K::Home, K::None},
    {K::PageDown, K::Right, K::Okay, K::Left, K::Return, K::Settings},
    {K::Log, K::Square, K::Power, K::Root, K::Fraction, K::X},
    {K::Close, K::Open, K::Tan, K::Cos, K::Sin, K::Ans},
};
constexpr const char* kCatalog[] = {
    "sqrt", "sin", "cos", "tan", "asin", "acos", "atan", "ln", "log",
    "abs", "exp", "pi", "e", "x", "y", "z", "A", "B", "C", "D", "E", "F"};
// US QWERTY, unshifted face then shifted, for Mode::Prompt. Twelve cells is
// the widest row and 192/12 = 16 px exactly, which is what fixes the cell
// width in the renderer. The trailing cell of the bottom row is a space in
// both faces: 63 rows leave no room for a spacebar of its own.
constexpr const char* kKeyboard[2][4] = {
    {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./ "},
    {"!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>? "},
};
constexpr size_t kKeyboardRows = sizeof(kKeyboard[0]) / sizeof(kKeyboard[0][0]);
}

Key matrixKey(uint8_t row, uint8_t column) {
  return row < 9 && column < 6 ? kKeys[row][column] : Key::None;
}
size_t Editor::catalogSize() { return sizeof(kCatalog) / sizeof(kCatalog[0]); }
size_t Editor::keyboardRows() { return kKeyboardRows; }
const char* Editor::keyboardRow(size_t row, bool shifted) {
  return row < kKeyboardRows ? kKeyboard[shifted ? 1 : 0][row] : "";
}
char Editor::keyboardChar() const {
  const char* row = keyboardRow(keyRow_, shifted_);
  return keyCol_ < std::strlen(row) ? row[keyCol_] : ' ';
}
// Left/Right walk the whole board rather than stopping at a row end -- with
// four arrows and no other way to reach a letter, a dead end is a key you
// cannot type. Up/Down clamp instead of wrapping the column, because the two
// bottom rows are one cell shorter than the top two.
void Editor::moveKeyboard(Key key) {
  const size_t length = std::strlen(keyboardRow(keyRow_, shifted_));
  if (key == K::Up || key == K::Down) {
    keyRow_ = key == K::Down ? (keyRow_ + 1) % kKeyboardRows
                             : (keyRow_ + kKeyboardRows - 1) % kKeyboardRows;
    const size_t target = std::strlen(keyboardRow(keyRow_, shifted_));
    if (keyCol_ >= target) keyCol_ = target - 1;
  } else if (key == K::Right) {
    if (++keyCol_ >= length) { keyCol_ = 0; keyRow_ = (keyRow_ + 1) % kKeyboardRows; }
  } else if (keyCol_) {
    --keyCol_;
  } else {
    keyRow_ = (keyRow_ + kKeyboardRows - 1) % kKeyboardRows;
    keyCol_ = std::strlen(keyboardRow(keyRow_, shifted_)) - 1;
  }
}
const char* Editor::catalogLabel(size_t index) {
  return index < catalogSize() ? kCatalog[index] : "";
}
void Editor::begin(const std::string& expression, Mode mode) {
  text_ = expression;
  cursor_ = text_.size();
  selected_ = !text_.empty();
  shifted_ = false;
  interrupted_ = false;
  catalog_ = -1;
  notice_ = "";
  sendArmed_ = false;
  mode_ = mode;
  keyRow_ = 1;  // 'q': a prompt starts with a letter far more often than a digit
  keyCol_ = 0;
}
void Editor::insert(const char* text, size_t retreat) {
  const size_t length = std::strlen(text);
  if ((selected_ ? 0 : text_.size()) + length > kMaxLength) {
    notice_ = "Limit: 256 characters";
    return;
  }
  if (selected_) { text_.clear(); cursor_ = 0; selected_ = false; }
  text_.insert(cursor_, text);
  cursor_ += length - retreat;
}
Result Editor::press(Key key) {
  notice_ = "";
  // Any key at all disarms; only the Execute branch below re-arms. That is
  // the whole confirmation: no timer, and no way to send by leaning on EXE.
  const bool sendArmed = sendArmed_;
  sendArmed_ = false;
  if (key == K::Interrupted) {
    shifted_ = false;
    interrupted_ = true;
    return Result::Edited;
  }
  if (key == K::Home) return Result::Cancel;
  if (catalog_ >= 0) {
    if (key == K::Return || key == K::Catalog || key == K::Variable) catalog_ = -1;
    else if (key == K::Down || key == K::Right) catalog_ = (catalog_ + 1) % catalogSize();
    else if (key == K::Up || key == K::Left) catalog_ = (catalog_ + catalogSize() - 1) % catalogSize();
    else if (key == K::Okay || key == K::Execute) {
      const std::string value = std::string(kCatalog[catalog_]) + (catalog_ < 11 ? "()" : "");
      insert(value.c_str(), catalog_ < 11 ? 1 : 0);
      catalog_ = -1;
    }
    return Result::Edited;
  }
  if (key == K::Return) return Result::Cancel;
  if (key == K::Shift) { shifted_ = !shifted_; return Result::Edited; }
  // Prompt mode spends the arrows and OK on the on-screen board and moves the
  // text cursor with PageUp/PageDown instead. Placed after the Shift branch on
  // purpose: SHIFT has to stay latched while the board is walked and clear only
  // when a character actually lands, and the clear below would eat it. Anything
  // not listed falls through, so every physical digit, operator and function
  // key still types exactly what it types in an expression.
  if (mode_ == Mode::Prompt) {
    switch (key) {
      case K::Left: case K::Right: case K::Up: case K::Down:
        moveKeyboard(key);
        return Result::Edited;
      case K::Okay: {
        const char cell[2] = {keyboardChar(), 0};
        insert(cell);
        shifted_ = false;
        return Result::Edited;
      }
      // Two physical spacebars: Format is where a thumb expects one, Ans is
      // the key with nothing else to do here.
      case K::Format: case K::Ans:
        insert(" ");
        shifted_ = false;
        return Result::Edited;
      case K::PageUp:
        selected_ = false;
        if (cursor_) --cursor_;
        return Result::Edited;
      case K::PageDown:
        selected_ = false;
        if (cursor_ < text_.size()) ++cursor_;
        return Result::Edited;
      case K::Execute:
        if (text_.empty()) { notice_ = "Type something first"; return Result::Edited; }
        // Sending starts a model turn that cannot be taken back, and EXE sits
        // one row above the arrows being held down to walk the board.
        if (!sendArmed) {
          sendArmed_ = true;
          notice_ = "Send? EXE again";
          return Result::Edited;
        }
        return Result::Save;
      // Setting has no menu to open from here and the panel has no room for a
      // footer, so it is the only place a key hint can live.
      case K::Settings:
        notice_ = "OK types  EXE sends  hold arrows";
        return Result::Edited;
      // The catalog overlay is expression vocabulary and would hide the board.
      case K::Catalog: case K::Function: case K::Tools: case K::Variable:
        notice_ = "Use the on-screen board";
        return Result::Edited;
      default: break;
    }
  }
  const bool shift = shifted_;
  shifted_ = false;
  if (key == K::Okay || key == K::Execute) {
    if (interrupted_) {
      interrupted_ = false;
      notice_ = "Check input; EXE saves";
      return Result::Edited;
    }
    if (text_.empty()) { notice_ = "Enter a value first"; return Result::Edited; }
    return Result::Save;
  }
  if (key == K::Left || key == K::Right || key == K::Up || key == K::Down ||
      key == K::PageUp || key == K::PageDown) {
    if (key == K::Up || key == K::PageUp || (key == K::Left && shift)) cursor_ = 0;
    else if (key == K::Down || key == K::PageDown || (key == K::Right && shift)) cursor_ = text_.size();
    else if (selected_) cursor_ = key == K::Left ? 0 : text_.size();
    else if (key == K::Left && cursor_) --cursor_;
    else if (key == K::Right && cursor_ < text_.size()) ++cursor_;
    selected_ = false;
  } else if (key == K::Delete) {
    if (shift) { selected_ = false; }  // insert mode is always active
    else if (selected_) { text_.clear(); cursor_ = 0; selected_ = false; }
    else if (cursor_) text_.erase(--cursor_, 1);
  } else if (key == K::Clear) {
    text_.clear(); cursor_ = 0; selected_ = false;
  } else if (key >= K::Digit0 && key <= K::Digit9) {
    const unsigned digit = static_cast<unsigned>(key) - static_cast<unsigned>(K::Digit0);
    const char* shifted[] = {"x", "D", "E", "F", "A", "B", "C", "pi", "e", nullptr};
    if (shift && shifted[digit]) insert(shifted[digit]);
    else if (shift) notice_ = "Complex input is unavailable";
    else { const char text[] = {static_cast<char>('0' + digit), 0}; insert(text); }
  } else {
    switch (key) {
      case K::Dot: insert(shift ? "y" : "."); break;
      case K::Scientific: insert(shift ? "z" : "*10^()", shift ? 0 : 1); break;
      case K::Add: insert("+"); break;
      case K::Subtract: insert("-"); break;
      case K::Multiply: insert("*"); break;
      case K::Divide: case K::Fraction: insert("/"); break;
      case K::Open: insert("("); break;
      case K::Close:
        if (!selected_ && cursor_ < text_.size() && text_[cursor_] == ')') ++cursor_;
        else insert(")");
        break;
      case K::Root: insert(shift ? "^(1/)" : "sqrt()", 1); break;
      case K::Power: insert(shift ? "^(-1)" : "^()", shift ? 0 : 1); break;
      case K::Square: if (shift) insert("log()", 1); else insert("^2"); break;
      case K::Log: insert(shift ? "ln()" : "log()", 1); break;
      case K::Sin: insert(shift ? "asin()" : "sin()", 1); break;
      case K::Cos: insert(shift ? "acos()" : "cos()", 1); break;
      case K::Tan: insert(shift ? "atan()" : "tan()", 1); break;
      case K::X: insert("x"); break;
      case K::Catalog: case K::Function: case K::Tools: catalog_ = 0; break;
      case K::Variable: catalog_ = 13; break;
      case K::Ans: notice_ = "Enter the value of Ans"; break;
      default: notice_ = "CATALOG: functions and symbols"; break;
    }
  }
  return Result::Edited;
}
}  // namespace ExpressionInput
