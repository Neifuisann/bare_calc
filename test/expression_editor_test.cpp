#include "input/expression_editor.h"
#include "input/keypad_protocol.h"
#include <cassert>
#include <iostream>
#include <initializer_list>

using namespace ExpressionInput;
using K = Key;
void type(Editor& editor, std::initializer_list<Key> keys) {
  for (Key key : keys) assert(editor.press(key) == Result::Edited);
}
int main() {
  unsigned mapped = 0;
  for (uint8_t row = 0; row < 9; ++row) {
    for (uint8_t col = 0; col < 6; ++col) {
      const bool valid = KeypadProtocol::kCells[row] & (1 << col);
      assert((matrixKey(row, col) != K::None) == valid);
      mapped += valid;
    }
  }
  assert(mapped == 47);
  assert(matrixKey(9, 0) == K::None && matrixKey(0, 6) == K::None);
  Editor editor;
  editor.begin("10");
  // Measured cells: sqrt, 2, close, division, 3 -> exact unevaluated fraction.
  type(editor, {matrixKey(7, 3), matrixKey(1, 4), matrixKey(8, 0),
                matrixKey(2, 1), matrixKey(1, 3)});
  assert(editor.text() == "sqrt(2)/3");
  assert(editor.press(matrixKey(0, 1)) == Result::Save);
  assert(editor.text() == "sqrt(2)/3");
  type(editor, {K::Left, K::Delete, K::Multiply});
  assert(editor.text() == "sqrt(2)*3");
  assert(editor.cursor() == 8);
  assert(editor.press(K::Return) == Result::Cancel);

  editor.begin("123");
  type(editor, {K::Right, K::Digit4, K::Left, K::Delete});
  assert(editor.text() == "124");
  type(editor, {K::Up, K::Subtract, K::Down, K::Square});
  assert(editor.text() == "-124^2");
  editor.begin("123");
  type(editor, {K::Digit9});
  assert(editor.text() == "9");
  type(editor, {K::Clear});
  assert(editor.text().empty());
  assert(editor.press(K::Execute) == Result::Edited);
  type(editor, {K::Digit1, K::Dot, K::Digit2, K::Scientific, K::Subtract,
                K::Digit3, K::Close});
  assert(editor.text() == "1.2*10^(-3)");

  editor.begin("");
  type(editor, {K::Shift, K::Sin, K::Digit1, K::Close, K::Add,
                K::Shift, K::Digit7, K::Add, K::Shift, K::Dot});
  assert(editor.text() == "asin(1)+pi+y");
  assert(!editor.shifted());
  type(editor, {K::Add, K::Shift, K::Scientific});
  assert(editor.text() == "asin(1)+pi+y+z");
  editor.begin("2");
  type(editor, {K::Right, K::Shift, K::Root, K::Digit3, K::Close});
  assert(editor.text() == "2^(1/3)");

  editor.begin("");
  type(editor, {K::Catalog, K::Up, K::Okay});
  assert(editor.text() == "F");
  assert(editor.catalogIndex() < 0);
  type(editor, {K::Catalog, K::Return});
  assert(editor.text() == "F");
  editor.begin("");
  type(editor, {K::Function, K::Execute, K::Digit2, K::Close});
  assert(editor.text() == "sqrt(2)");
  assert(editor.press(K::Home) == Result::Cancel);
  editor.begin(std::string(Editor::kMaxLength, '1'));
  type(editor, {K::Right, K::Digit2});
  assert(editor.text().size() == Editor::kMaxLength);
  assert(editor.notice()[0]);
  type(editor, {K::Delete, K::Digit3});
  assert(editor.text().back() == '3');

  editor.begin("12");
  type(editor, {K::Shift, K::Interrupted});
  assert(!editor.shifted());
  assert(editor.text() == "12");
  assert(editor.press(K::Execute) == Result::Edited);
  assert(editor.press(K::Execute) == Result::Save);
  // --- Prompt mode: the on-screen QWERTY board --------------------------------
  //
  // Board faces, for the cell coordinates below:
  //   row 0  1234567890-=   row 1  qwertyuiop[]
  //   row 2  asdfghjkl;'    row 3  zxcvbnm,./<space>
  editor.begin("", Mode::Prompt);
  assert(editor.mode() == Mode::Prompt);
  assert(editor.keyRow() == 1 && editor.keyCol() == 0);  // 'q'
  assert(editor.press(K::Execute) == Result::Edited);    // nothing to send yet
  assert(editor.notice()[0]);
  type(editor, {K::Okay});
  assert(editor.text() == "q");
  // Left off the start of a row lands on the last cell of the row above. With
  // four arrows and no letters on the keypad, a dead end is a key you cannot type.
  type(editor, {K::Left});
  assert(editor.keyRow() == 0 && editor.keyCol() == 11);
  type(editor, {K::Okay});
  assert(editor.text() == "q=");
  // Down from a 12-cell row onto an 11-cell one clamps the column.
  type(editor, {K::Down, K::Down});
  assert(editor.keyRow() == 2 && editor.keyCol() == 10);
  type(editor, {K::Okay});
  assert(editor.text() == "q='");
  // SHIFT latches across a walk of the board and clears only when a character lands.
  type(editor, {K::Shift});
  assert(editor.shifted());
  type(editor, {K::Up});
  assert(editor.shifted() && editor.keyRow() == 1 && editor.keyCol() == 10);
  type(editor, {K::Okay});
  assert(editor.text() == "q='{" && !editor.shifted());
  // Space: the Format and Ans keys, and the bottom row's trailing cell.
  type(editor, {K::Ans, K::Format});
  assert(editor.text() == "q='{  ");
  type(editor, {K::Down, K::Down});
  assert(editor.keyRow() == 3 && editor.keyCol() == 10);
  type(editor, {K::Okay});
  assert(editor.text() == "q='{   ");
  // The text cursor moves on PageUp/PageDown; the board keeps its cell.
  const size_t boardRow = editor.keyRow(), boardCol = editor.keyCol();
  const size_t at = editor.cursor();
  type(editor, {K::PageUp});
  assert(editor.cursor() == at - 1 && editor.keyRow() == boardRow &&
         editor.keyCol() == boardCol);
  type(editor, {K::PageDown});
  assert(editor.cursor() == at);
  // Everything the board does not claim still types exactly as it does in an
  // expression, and the catalog overlay stays out of the way of the board.
  type(editor, {K::Digit5, K::Add, K::Catalog});
  assert(editor.text() == "q='{   5+");
  assert(editor.catalogIndex() < 0 && editor.notice()[0]);
  type(editor, {K::Delete});
  assert(editor.text() == "q='{   5");
  // Sending is confirmed, and any key in between disarms it -- a model turn
  // cannot be taken back, and EXE sits one row above the arrows.
  assert(editor.press(K::Execute) == Result::Edited);
  assert(editor.notice()[0]);
  type(editor, {K::PageUp});
  assert(editor.press(K::Execute) == Result::Edited);
  assert(editor.press(K::Execute) == Result::Save);
  type(editor, {K::Clear});
  assert(editor.text().empty());
  assert(editor.press(K::Return) == Result::Cancel);
  // begin() without a mode is still the expression editor, arrows and all.
  editor.begin("12");
  assert(editor.mode() == Mode::Expression);
  type(editor, {K::Left});
  assert(editor.cursor() == 0 && editor.text() == "12");

  std::cout << "Expression editor: map, entry, cursor, SHIFT, catalog, cancellation, limits, interruption and the prompt keyboard passed.\n";
}
