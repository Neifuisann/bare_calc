#include "calculator/runtime/calculator_session.h"
#include "calculator/runtime/calculator_store.h"
#include "calculator_platform_fake.h"
#include <cassert>
#include <iostream>

using namespace Calculator;

void updateCrc(std::vector<uint8_t>& record) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 11; i < record.size(); ++i) {
    crc ^= record[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  crc = ~crc;
  for (int i = 0; i < 4; ++i) record[7 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

int main() {
  // Computing an aggregate answer and then switching apps must not poison
  // the entire saved record (including unrelated settings and variables).
  for (AppId app : {AppId::Matrix, AppId::Vector}) {
    CalculatorPlatform::fakeNvs().clear();
    CalculatorSession original;
    original.app = app;
    original.settings.angle = Angle::Radian;
    original.memory.variables[0] = Value(17);
    auto& slots = app == AppId::Matrix ? original.aggregates.matrices : original.aggregates.vectors;
    slots[0].rows = 2;
    slots[0].cols = app == AppId::Matrix ? 2 : 1;
    slots[0].at(0, 0) = Decimal(3);
    slots[kAnswerSlot] = slots[0];
    launchApp(original, AppId::Calculate);
    assert(!slots[kAnswerSlot].defined());
    assert(saveStoredState(original));
    CalculatorSession restored;
    loadStoredState(restored);
    assert(restored.settings.angle == Angle::Radian);
    assert(restored.memory.variables[0].re.number == Decimal(17));
    const auto& saved = app == AppId::Matrix ? restored.aggregates.matrices : restored.aggregates.vectors;
    assert(saved[0].rows == 2 && saved[0].cols == slots[0].cols);
    assert(saved[0].at(0, 0) == Decimal(3) && !saved[kAnswerSlot].defined());
  }
  // Recover a record already written by the old firmware after VctAns was
  // cleared. Keep rejecting actual invalid dimensions, even with a valid CRC.
  CalculatorSession original;
  original.memory.variables[0] = Value(29);
  auto record = encodeV2(original.settings, original.memory, &original.aggregates);
  Settings settings;
  Memory memory;
  Aggregates aggregates;
  for (uint8_t width : {1, 2, 3, 4}) {
    record.back() = width;
    updateCrc(record);
    assert(decodeV2(record, settings, memory, &aggregates));
    assert(memory.variables[0].re.number == Decimal(29));
    assert(aggregates.vectors[kAnswerSlot].rows == 0 && aggregates.vectors[kAnswerSlot].cols == 0);
  }
  record.back() = 5;
  updateCrc(record);
  assert(!decodeV2(record, settings, memory, &aggregates));
  record.back() = 0;
  record[record.size() - 2] = 1;
  updateCrc(record);
  assert(!decodeV2(record, settings, memory, &aggregates));
  std::cout << "Calculator store: app switches preserve reloadable settings, variables and named aggregates.\n";
}
