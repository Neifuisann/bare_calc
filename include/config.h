#pragma once

// Calculator tunables a human would reasonably adjust. Constants are grouped
// under Config::<Subsystem>; call sites name them in full
// (Config::Clock::kUtcOffsetSeconds).

#include <cstddef>
#include <cstdint>

namespace Config {

// --- Math Box clock ----------------------------------------------------------
// The wall time comes from the board (CalculatorPlatform::wallClock()) and is
// shown in one fixed zone; Vietnam keeps no DST.
namespace Clock {
constexpr int32_t kUtcOffsetSeconds = 7 * 3600;
constexpr const char *kZoneLabel = "UTC+7";
}  // namespace Clock

}  // namespace Config
