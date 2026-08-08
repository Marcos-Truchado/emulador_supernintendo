#include "snes/snes.hpp"

#include "cpu/cpu65816.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if 1  // always on: trace ring buffer (small, useful on any build)
#define CPUTEST_TRACEBUFFER 1
#endif

// Runner for the "65C816 TEST" suite (cputest by the SNES homebrew
// community). The ROM chain-executes tests in order; passing the last test
// jumps into the `success` handler, which parks the CPU in an infinite
// `jmp`-loop. We detect that parking address and stop.
//
// A failing test instead parks in `wait_for_key` polling $4218 bit 7; our
// stubbed bus returns 0 there, so the poll never exits and the CPU stalls at
// a known PC. We report that stall along with the stored result registers.
//
// Usage: snes_cputest <cputest-full.sfc>

namespace {

// Parking addresses proven from disassembling the linked ROM:
//   success: `@end: jmp @end` self-loop at $0081A2.
//   fail:    cputest's wait_for_key loops at $008240 reading $4212/$4218.
constexpr snes::uint24 kSuccessPark = 0x0081a2;
constexpr snes::uint24 kFailLoop = 0x008240;

constexpr uint64_t kMaxInstructions = 400'000'000ull;

// cputest ZEROPAGE memory layout (ZEROPAGE segment starts at $00).
// .res $10, then: test_num, result_a, result_x, result_y, result_p ...
constexpr snes::uint16 kTestNum = 0x10;
constexpr snes::uint16 kResultA = 0x12;
constexpr snes::uint16 kResultX = 0x14;
constexpr snes::uint16 kResultY = 0x16;
constexpr snes::uint16 kResultP = 0x18;

snes::uint16 readWord(const snes::Bus& bus, snes::uint16 addr) {
  const auto& wram = bus.wram();
  return snes::uint16(wram[addr]) | snes::uint16(wram[addr + 1]) << 8;
}

void dump(const snes::Cpu65816& cpu, const snes::Bus& bus) {
  auto ts = cpu.traceState();
  printf(
      "pc=%06x a=%04x x=%04x y=%04x s=%04x d=%04x b=%02x p=%02x e=%d\n",
      ts.pc, ts.a, ts.x, ts.y, ts.s, ts.d, ts.b, ts.p, ts.e ? 1 : 0);
  printf(
      "  stored: test_num=%u result_a=%04x result_x=%04x result_y=%04x "
      "result_p=%04x\n",
      readWord(bus, kTestNum), readWord(bus, kResultA), readWord(bus, kResultX),
      readWord(bus, kResultY), readWord(bus, kResultP));
}

#ifdef CPUTEST_TRACEBUFFER
// Ring buffer of the last N executed instructions (with pre-instruction
// register state) dumped on failure to help diagnose core bugs.
struct TraceEntry {
  snes::uint24 pc;
  snes::uint16 a, x, y, s, d;
  snes::uint8 b, p;
  bool e;
  std::string insn;

  auto format() const -> std::string {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "%06x: a=%04x x=%04x y=%04x s=%04x d=%04x b=%02x p=%02x e=%d %s",
             pc, a, x, y, s, d, b, p, e ? 1 : 0, insn.c_str());
    return buf;
  }
};

constexpr size_t kTraceSize = 8192;
std::vector<TraceEntry>& trace() {
  static std::vector<TraceEntry> t(kTraceSize);
  return t;
}
size_t traceIndex = 0;

void tracePush(const snes::Cpu65816& cpu, const std::string& insn) {
  auto ts = cpu.traceState();
  TraceEntry& e = trace()[traceIndex % kTraceSize];
  e.pc = ts.pc;
  e.a = ts.a;
  e.x = ts.x;
  e.y = ts.y;
  e.s = ts.s;
  e.d = ts.d;
  e.b = ts.b;
  e.p = ts.p;
  e.e = ts.e;
  e.insn = insn;
  traceIndex++;
}

void traceDump() {
  fprintf(stderr, "=== last %zu instructions ===\n",
          std::min<size_t>(traceIndex, kTraceSize));
  const size_t start =
      traceIndex >= kTraceSize ? traceIndex % kTraceSize : 0;
  for (size_t i = 0; i < std::min<size_t>(traceIndex, kTraceSize); i++) {
    const TraceEntry& e = trace()[(start + i) % kTraceSize];
    fprintf(stderr, "%s\n", e.format().c_str());
  }
}
#else
void tracePush(const snes::Cpu65816&, const std::string&) {}
void traceDump() {}
#endif

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <rom.sfc>\n", argv[0]);
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

  printf("reset pc=%06x\n", cpu.pc());
  for (uint64_t i = 0; i < kMaxInstructions; i++) {
    if (cpu.stopped()) {
      printf("CPU halted (STP) at iter %llu, pc=%06x\n",
             (unsigned long long)i, cpu.pc());
      dump(cpu, bus);
      return 1;
    }
    if (cpu.pc() == kSuccessPark) {
      printf("SUCCESS: reached %06x (success handler) after %llu instructions\n",
             kSuccessPark, (unsigned long long)i);
      dump(cpu, bus);
      return 0;
    }
    if (cpu.pc() == kFailLoop || cpu.pc() == 0x008247) {
      snes::uint16 test_num = readWord(bus, kTestNum);
      printf("FAIL: stalled at pc=%06x (wait_for_key) after %llu instructions\n",
             cpu.pc(), (unsigned long long)i);
      dump(cpu, bus);
      traceDump();
      return 1;
    }
    tracePush(cpu, cpu.disassemble());
    system.step();
  }

  printf("FAIL: never reached success handler; stall pc=%06x\n", cpu.pc());
  dump(cpu, bus);
  return 1;
}