#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Local, unevaluated expression entry. No display or Arduino state.
namespace ExpressionInput {
enum class Key : uint8_t {
  None, Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7,
  Digit8, Digit9, Dot, Scientific, Format, Execute, Add, Subtract,
  Multiply, Divide, Delete, Clear, Shift, Variable, Function, Down,
  Catalog, Tools, Home, Up, PageUp, Settings, Return, Left, Okay,
  Right, PageDown, X, Fraction, Root, Power, Square, Log, Ans,
  Sin, Cos, Tan, Open, Close, Interrupted
};

Key matrixKey(uint8_t row, uint8_t column);

enum class Result { Edited, Save, Cancel };

// What the four arrows and OK mean. In Expression mode they walk the text
// cursor and EXE/OK both save -- the calculator keypad already carries every
// symbol an expression needs. A free-text prompt needs letters, which that
// keypad does not have at all, so Prompt mode spends the arrows and OK on an
// on-screen QWERTY board instead and moves the text cursor to PageUp/PageDown.
// Everything else -- insert, the length limit, delete, clear, the notice, and
// the physical digit/operator keys -- is shared.
enum class Mode : uint8_t { Expression, Prompt };

class Editor {
 public:
  static constexpr size_t kMaxLength = 256;
  void begin(const std::string& expression, Mode mode = Mode::Expression);
  Result press(Key key);
  const std::string& text() const { return text_; }
  size_t cursor() const { return cursor_; }
  bool selected() const { return selected_; }
  bool shifted() const { return shifted_; }
  const char* notice() const { return notice_; }
  void notice(const char* message) { notice_ = message; }
  int catalogIndex() const { return catalog_; }
  static size_t catalogSize();
  static const char* catalogLabel(size_t index);

  // On-screen QWERTY board, Prompt mode only. Rows differ in length --
  // keyboardRow() is the authority and the renderer centres each row on its
  // own width, so neither side hard-codes a column count.
  Mode mode() const { return mode_; }
  size_t keyRow() const { return keyRow_; }
  size_t keyCol() const { return keyCol_; }
  static size_t keyboardRows();
  static const char* keyboardRow(size_t row, bool shifted);

 private:
  void insert(const char* text, size_t retreat = 0);
  void moveKeyboard(Key key);
  char keyboardChar() const;
  std::string text_;
  size_t cursor_ = 0;
  bool selected_ = false, shifted_ = false;
  bool interrupted_ = false;
  // Prompt mode: EXE arms, a second EXE sends. Cleared by any other key,
  // which is why it needs no clock -- an arm cannot outlive one keypress.
  bool sendArmed_ = false;
  int catalog_ = -1;
  const char* notice_ = "";
  Mode mode_ = Mode::Expression;
  size_t keyRow_ = 1, keyCol_ = 0;
};
}  // namespace ExpressionInput
