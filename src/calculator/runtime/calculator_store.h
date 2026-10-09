#pragma once
#include "calculator/runtime/calculator_session.h"
#include <cstdint>
#include <string>
#include <vector>

// Persistent calculator settings, scalar memory and f/g. Encoding is pure and
// host-tested; the NVS reads and writes go through CalculatorPlatform. Replay
// history, Verify and screen state are never persisted.
//
// calc_v2/state (blob) is the record: "CALC", version 3, payload length and
// CRC-32, then settings bytes, Ans (tagged real/complex), PreAns, A-F/x/y/z,
// f and g, then MatA-D/MatAns and VctA-D/VctAns as rows, cols and each cell's
// decimal text. A version 2 record (no aggregates) still loads. Every real is its 40-digit decimal text plus both exact terms, so a
// radical pair or a complex value survives a power cycle unchanged. A corrupt
// or truncated record is rejected whole. Without a valid v2 record the
// shipped calc_v1/state JSON is migrated in memory; v1 is never rewritten or
// deleted, and the next save writes v2.
namespace Calculator {

std::vector<uint8_t> encodeV2(const Settings& settings, const Memory& memory, const Aggregates* aggregates = nullptr);
// Both decoders leave their outputs untouched unless the record is accepted.
bool decodeV2(const std::vector<uint8_t>& record, Settings& settings, Memory& memory, Aggregates* aggregates = nullptr);
bool decodeV1(const std::string& json, Settings& settings, Memory& memory);

void loadStoredState(CalculatorSession& session);
bool saveStoredState(const CalculatorSession& session);

}  // namespace Calculator
