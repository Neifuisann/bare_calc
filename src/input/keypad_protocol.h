#pragma once
#include <stdint.h>
#include <string.h>

// Wire format v2. Keep this file byte-identical in the keypad scanner and the
// calculator.
namespace KeypadProtocol {
constexpr uint8_t kAddress = 0x33, kVersion = 2, kFrameSize = 32;
constexpr uint8_t kSnapshot = 0x10, kSession = 0x20, kMode = 0x21;
constexpr uint8_t kKeepAlive = 0x22, kAck = 0x23, kResync = 0x24, kReset = 0x25;
enum Mode : uint8_t { Released = 0, Owned = 1, Passive = 2 };
enum Flags : uint8_t { Valid = 1, Overflow = 2, LeaseExpired = 4 };
constexpr uint8_t kRows = 9, kCapacity = 16, kLeaseTicks = 8;
constexpr uint8_t kCells[kRows] = {0x3e,0x3e,0x3e,0x3e,0x3f,0x15,0x3f,0x3f,0x3f};
inline uint16_t get16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t *p) { return uint32_t(get16(p)) | (uint32_t(get16(p + 2)) << 16); }
inline void put16(uint8_t *p, uint16_t n) { p[0] = uint8_t(n); p[1] = uint8_t(n >> 8); }
inline void put32(uint8_t *p, uint32_t n) { put16(p, uint16_t(n)); put16(p + 2, uint16_t(n >> 16)); }
inline uint16_t crc(const uint8_t *p, uint8_t size) {
  uint16_t v = 0xffff;
  while (size--) {
    v ^= uint16_t(*p++) << 8;
    for (uint8_t i = 0; i < 8; ++i) v = (v & 0x8000) ? uint16_t((v << 1) ^ 0x1021) : uint16_t(v << 1);
  }
  return v;
}
inline bool validFrame(const uint8_t *p) {
  return p[0] == 0x4b && p[1] == kVersion && get16(p + 30) == crc(p, 30);
}

// Byte 29 carries the two extras the scanner grew when a divider was wired to
// A6 and KPI1 (the ON/power key) to D13: bit7 is the power button, 1 = pressed
// since the last snapshot that carried it (a press is held until a snapshot has
// reported it and the key is up, so it reaches reads exactly once);
// bits 0-6 are the pack voltage in 20 mV steps above 2000 mV, 0 meaning "no
// reading yet". One byte because the frame is otherwise full, and 20 mV is
// already finer than a 3300-4200 mV gauge can use. It used to be reserved-zero,
// which is exactly what an old scanner reports and what an old master ignores,
// so neither side needs a version bump.
constexpr uint16_t kBatteryBaseMv = 2000, kBatteryStepMv = 20;
inline uint8_t packAux(uint16_t millivolts, bool powerPressed) {
  uint16_t steps = millivolts <= kBatteryBaseMv
                       ? 0
                       : uint16_t((millivolts - kBatteryBaseMv + kBatteryStepMv / 2) /
                                  kBatteryStepMv);
  if (steps > 0x7f) steps = 0x7f;
  return uint8_t(uint8_t(steps) | (powerPressed ? 0x80 : 0));
}
inline uint16_t batteryMv(const uint8_t *p) {
  const uint8_t steps = p[29] & 0x7f;
  return steps ? uint16_t(kBatteryBaseMv + uint16_t(steps) * kBatteryStepMv) : 0;
}
inline bool powerPressed(const uint8_t *p) { return (p[29] & 0x80) != 0; }

// Called only with interrupts excluded on AVR. Reads never mutate this state.
struct Device {
  uint32_t session = 0, scans = 0;
  uint16_t generation = 0, nextSequence = 1, leaseAt = 0;
  uint8_t state[kRows] = {}, events[kCapacity] = {};
  uint16_t sequences[kCapacity] = {};
  uint8_t head = 0, count = 0, flags = 0, resetCause = 0, columns = 0, aux = 0;
  Mode mode = Released;
  bool resetPending = false;

  void clearKeys() {
    memset(state, 0, sizeof state);
    head = count = 0; flags &= LeaseExpired; ++generation;
  }
  // True means the caller must release all matrix pins before returning to TWI.
  bool receive(const uint8_t *p, uint8_t n, uint16_t tick) {
    if (n == 1 && p[0] == kSnapshot) return false;
    if (n < 7 || get16(p + n - 2) != crc(p, n - 2)) return false;
    const uint32_t token = get32(p + 1);
    if (p[0] == kSession && n == 7 && token) {
      if (token == session) return false; // retry must not clear new events
      session = token; mode = Released; flags = 0; clearKeys(); leaseAt = tick;
      return true;
    }
    if (!session || token != session) return false;
    if (p[0] == kMode && n == 8 && p[5] <= Passive) {
      leaseAt = tick;
      if (mode == Mode(p[5])) return false;
      mode = Mode(p[5]); flags = 0; clearKeys(); return true;
    }
    if (p[0] == kKeepAlive && n == 7) { leaseAt = tick; return false; }
    if (p[0] == kAck && n == 9) {
      if (count && get16(p + 5) == sequences[head]) {
        head = (head + 1) % kCapacity; --count;
      }
    } else if (p[0] == kResync && n == 9) {
      // Snapshot changed while in flight: keep the queue and make the host retry.
      if (get16(p + 5) == generation) { head = count = 0; flags &= ~Overflow; }
    } else if (p[0] == kReset && n == 7) {
      mode = Released; session = 0; flags = 0; clearKeys(); resetPending = true;
      return true;
    }
    return false;
  }
  bool expire(uint16_t tick) {
    if (mode != Owned || uint16_t(tick - leaseAt) < kLeaseTicks) return false;
    mode = Released; flags = LeaseExpired; clearKeys(); return true;
  }
  void sample(const uint8_t *keys, bool valid) {
    ++scans;
    if (!valid) {
      if (flags & Valid) { flags &= ~Valid; ++generation; }
      return;
    }
    bool changed = !(flags & Valid);
    for (uint8_t r = 0; r < kRows; ++r) {
      const uint8_t value = keys[r] & kCells[r];
      const uint8_t diff = state[r] ^ value;
      if (diff) changed = true;
      for (uint8_t c = 0; c < 6; ++c) if (diff & (1 << c)) {
        if (count == kCapacity) { flags |= Overflow; continue; }
        const uint8_t tail = (head + count) % kCapacity;
        events[tail] = uint8_t((value & (1 << c) ? 0x80 : 0) | (r << 3) | c);
        sequences[tail] = nextSequence++;
        ++count;
      }
      state[r] = value;
    }
    flags |= Valid;
    if (changed) ++generation;
  }
  void snapshot(uint8_t *p) const {
    memset(p, 0, kFrameSize);
    p[0] = 0x4b; p[1] = kVersion; p[2] = flags; p[3] = count;
    put32(p + 4, session); put16(p + 8, generation);
    memcpy(p + 10, state, kRows);
    if (count) { put16(p + 19, sequences[head]); p[21] = events[head]; }
    p[22] = resetCause; p[23] = mode; put32(p + 24, scans);
    p[28] = columns; p[29] = aux;
    put16(p + 30, crc(p, 30));
  }
};
} // namespace KeypadProtocol
