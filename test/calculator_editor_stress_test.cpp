// P5 editor/document stress: deterministic structural-editing fuzz.
//
// Invariant (the goal is NOT ClassWiz comparison): after every key, the
// cursor must be a valid EditDocument stop, the cursor must be in range, the
// source must stay within the input limit, the outer edit transaction must
// have closed (depth zero), INS arming must never go stale (armed only right
// after the Shift modifier or the Delete arming key), and nothing may crash,
// hang or access out of range. Any key that legitimately refuses an edit
// (input limit, refused delimiter) is fine; an invalid cursor is not.
//
//   stress.exe [seeds] [opsPerSeed]   defaults: 40 seeds x 200 ops (smoke)
//   P5 acceptance: stress.exe 1000 200  (200,000 deterministic edits)
//
// Seed scheme: seed s runs xorshift32 stream 0xC41C0000 + s*0x9E3779B1, so a
// failure reproduces by rerunning one seed. The key pool stresses structural
// editing only (no asynchronous app/controller keys). Shift is included so
// INS arming/cancellation and overwrite paths interleave; function keys now
// insert open calls. A FNV-1a digest over every (text, cursor) pair is
// printed so runs are exactly comparable.
#include "calculator/ui/calculator_document.h"
#include "calculator/ui/calculator_editor.h"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace Calculator;
using K = ExpressionInput::Key;

namespace {
// Structural editing pool: digits, operators, templates, groups, function
// insertions (open-call inserters, synchronous like the editor's own
// Power/Square keys), Shift (INS arming modifier), arrows and DEL. No
// Clear/menus: the point is deep structural states, not wiping them.
const K kPool[] = {
  K::Digit0, K::Digit1, K::Digit2, K::Digit3, K::Digit4,
  K::Digit5, K::Digit6, K::Digit7, K::Digit8, K::Digit9,
  K::Add, K::Subtract, K::Multiply, K::Divide,
  K::Fraction, K::Root, K::Power, K::Square,
  K::Open, K::Close, K::Sin, K::Cos, K::Tan, K::Log,
  K::Left, K::Right, K::Up, K::Down, K::Delete, K::Shift,
};
uint32_t next(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
}

int main(int argc, char** argv) {
  unsigned seeds = 40, ops = 200;
  if (argc > 1) seeds = static_cast<unsigned>(std::stoul(argv[1]));
  if (argc > 2) ops = static_cast<unsigned>(std::stoul(argv[2]));
  if (seeds == 0 || ops == 0 || seeds > 100000 || ops > 100000) {
    std::printf("usage: stress.exe [seeds] [opsPerSeed]\n");
    return 2;
  }
  uint64_t digest = 0xcbf29ce484222325ull;  // FNV-1a over every observed state
  auto mix = [&](uint64_t v) { digest ^= v; digest *= 0x100000001b3ull; };
  uint64_t total = 0;
  FILE* trace = argc > 3 ? std::fopen(argv[3], "w") : nullptr;
  if (argc > 3 && !trace) return 2;
  for (unsigned s = 0; s < seeds; ++s) {
    uint32_t rng = 0xC41C0000u + s * 0x9E3779B1u;
    MathEditor e;
    e.begin();
    e.wrapHorizontal = true;  // production main-entry policy
    for (unsigned i = 0; i < ops; ++i) {
      const K key = kPool[next(rng) % (sizeof(kPool) / sizeof(kPool[0]))];
      e.press(key);
      if (trace) std::fprintf(trace, "%u\t%u\t%d\t%u\t%s\n", s, i, int(key), unsigned(e.cursor()), e.text().c_str());
      ++total;
      for (char c : e.text()) mix(static_cast<uint64_t>(static_cast<unsigned char>(c)));
      mix(static_cast<uint64_t>(e.cursor()) + 0x9e3779b97f4a7c15ull);
      mix(static_cast<uint64_t>(static_cast<int>(e.caretKind())));
      EditDocument d;
      d.rebuild(e.text());
      // INS is transient: armed only immediately after the Shift modifier or
      // the SHIFT+DEL arming key. Anything else must have consumed or
      // cancelled it (Root consumes; every other key cancels).
      const bool staleIns = e.insArmed() && key != K::Shift && key != K::Delete;
      if (e.cursor() > e.text().size() || !d.valid(e.cursor()) ||
          e.text().size() > MathEditor::kLimit || e.editDepth() != 0 || staleIns) {
        std::printf("INVALID seed=%u op=%u text=\"%s\" cursor=%u depth=%d ins=%d\n", s, i,
                    e.text().c_str(), static_cast<unsigned>(e.cursor()),
                    e.editDepth(), e.insArmed() ? 1 : 0);
        return 1;
      }
    }
  }
  std::printf("editor stress: %u seeds x %u ops = %llu edits, 0 invalid, digest=%016llx\n",
              seeds, ops, total, digest);
  if (trace) std::fclose(trace);
  return 0;
}
