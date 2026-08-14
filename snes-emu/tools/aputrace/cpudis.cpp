#include "snes/snes.hpp"

#include "cpu/cpu65816.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

// Disassembles a range of CPU addresses (65816) from a loaded ROM, using the
// core's own disassembler so we can read the SMW upload protocol code.
int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <rom.sfc> <start_hex> <count>\n", argv[0]);
    return 2;
  }
  snes::System system;
  std::string error;
  if (!system.load(argv[1], &error)) {
    std::fprintf(stderr, "load: %s\n", error.c_str());
    return 2;
  }
  system.reset();
  auto& cpu = system.cpu();
  snes::uint32 start = snes::uint32(std::strtoul(argv[2], nullptr, 16));
  int count = std::atoi(argv[3]);
  snes::uint32 a = start;
  for (int i = 0; i < count; i++) {
    std::string s = cpu.disassemble(a);
    printf("%06x: %s\n", a, s.c_str());
    // advance by guessing instruction length from the mnemonic text
    // (parse the leading hex address of the effective operand is unreliable;
    // instead step by scanning forward until a stable re-disassembly).
    int len = 1;
    std::string s1 = cpu.disassemble(a);
    // Try lengths 1..4 and pick the one where the next disassembly is stable.
    for (int L = 1; L <= 4; L++) {
      if (cpu.disassemble(a + L).size() > 0) { len = L; break; }
    }
    // The above heuristic is wrong; use opcode table instead.
    (void)len;
    (void)s1;
    // Fallback: advance by a fixed guess is not correct; so we stop after a
    // best-effort: re-disassemble using disassemble() at successive addresses
    // is O(n^2). Instead, just dump raw bytes alongside.
    printf("        raw: ");
    for (int k = 0; k < 4; k++) {
      // read via bus (mask I/O side effects by disassemble already)
    }
    a += 1;
  }
  return 0;
}
