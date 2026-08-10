#include "snes/snes.hpp"

#include "cpu/cpu65816.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Runner for the phase-3 timing test ROM (tools/fase3/build_test_rom.py).
// The ROM busy-polls the PPU status registers (no NMI/IRQ delivery needed,
// that is phase 3b) and parks in a `jmp`-self loop when done; the park
// address is embedded in the ROM at $7FF0 (magic "F3PA" + park LE + frames).
//
// The runner runs until the CPU parks, then reads the WRAM result block
// ($0200-$0206) and validates it:
//   $0200 frames_left == 0, $0201 vblanks == frames, $0202 irqs == frames,
//   $0203 nmi_ack == frames, $0204 hb_set / $0205 hb_clr within range,
//   $0206 done == 1.
//
// Usage: snes_fase3 <fase3_timing.sfc>

namespace {

constexpr snes::uint16 kResults = 0x0200;  // WRAM bank 0 (wram() is 128KB)
constexpr uint64_t kMaxInstructions = 400'000'000ull;

struct RomMeta {
  snes::uint24 park = 0;
  int frames = 0;
  bool ok = false;
};

RomMeta readMeta(const std::string& path) {
  RomMeta meta;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return meta;
  uint8_t buf[16] = {};
  std::fseek(f, 0x7FF0, SEEK_SET);
  if (std::fread(buf, 1, 7, f) == 7 && std::memcmp(buf, "F3PA", 4) == 0) {
    meta.park = snes::uint24(buf[4]) | (snes::uint24(buf[5]) << 8);
    meta.frames = buf[6];
    meta.ok = true;
  }
  std::fclose(f);
  return meta;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <fase3_timing.sfc>\n", argv[0]);
    return 2;
  }

  const RomMeta meta = readMeta(argv[1]);
  if (!meta.ok) {
    std::fprintf(stderr, "not a fase3 timing ROM (no F3PA magic at $7FF0)\n");
    return 2;
  }

  snes::System system;
  std::string error;
  if (!system.load(argv[1], &error)) {
    std::fprintf(stderr, "load: %s\n", error.c_str());
    return 2;
  }

  auto& cpu = system.cpu();
  auto& bus = system.bus();
  system.reset();

  for (uint64_t i = 0; i < kMaxInstructions; i++) {
    if (cpu.pc() == meta.park) break;
    system.step();
    if (i == kMaxInstructions - 1) {
      std::fprintf(stderr, "FAIL: never reached park $%06x; stall pc=%06x\n",
                   meta.park, cpu.pc());
      return 1;
    }
  }

  const auto& wram = bus.wram();
  const int frames_left = wram[kResults + 0];
  const int vblanks = wram[kResults + 1];
  const int irqs = wram[kResults + 2];
  const int nmi_ack = wram[kResults + 3];
  const int hb_set = wram[kResults + 4];
  const int hb_clr = wram[kResults + 5];
  const int done = wram[kResults + 6];

  printf("park pc=%06x\n", meta.park);
  printf("  frames_left=%d vblanks=%d irqs=%d nmi_ack=%d "
         "hb_set=%d hb_clr=%d done=%d\n",
         frames_left, vblanks, irqs, nmi_ack, hb_set, hb_clr, done);

  bool pass = true;
  auto check = [&](bool cond, const char* what) {
    if (!cond) {
      std::fprintf(stderr, "FAIL: %s\n", what);
      pass = false;
    }
  };

  check(done == 1, "ROM never finished (done flag)");
  check(frames_left == 0, "frame loop did not complete exactly");
  check(vblanks == meta.frames, "VBlank edge count != frames (test 1)");
  check(irqs == meta.frames, "V-IRQ trigger count != frames (test 3)");
  check(nmi_ack == meta.frames, "NMI latch $4210 read did not return 0x82 "
                                "every frame (test 2)");
  // HBlank sampling (test 4): ~68/341 of each line is HBlank, so the set
  // share of 200 samples is a minority; validate a broad range, not the
  // raw count (fase3 doc §10.1.3).
  check(hb_set + hb_clr == 200, "HBlank sampler did not run 200 iterations");
  check(hb_set > 0 && hb_set < hb_clr,
        "HBlank set/clear samples out of expected range "
        "(set should be a minority of a scanline)");

  if (!pass) return 1;
  printf("SUCCESS: fase3 timing ROM (VBlank x%d, V-IRQ x%d, NMI ack x%d, "
         "HBlank %d/%d samples)\n",
         vblanks, irqs, nmi_ack, hb_set, hb_clr);
  return 0;
}
