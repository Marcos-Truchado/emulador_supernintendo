#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "cpu/cpu65816.hpp"
#include "ppu/ppu.hpp"

// Phase 3b integration tests: the PPU's interrupt pins wired to the CPU
// through the System. These verify the real delivery path — internal NMI
// edge-detect + IRQ level mirror inside the PPU, vectors fetched by the
// CPU, and RTI returning to the interrupted flow (see update.md, fase 3b).
//
// Each test boots a tiny LoROM image directly (no file on disk): the
// program lives at $008000, E-mode vectors at $FFFA (NMI) / $FFFE (IRQ),
// and handlers write their invocation counter to WRAM $0200.

namespace {

constexpr snes::uint24 kNmiHandler = 0x008100;
constexpr snes::uint24 kIrqHandler = 0x008200;
constexpr snes::uint16 kCounter = 0x0200;  // WRAM bank 0

// 32KB LoROM image (0x8000 bytes, power-of-two -> LoROM size heuristic);
// default-filled with NOPs, map-mode byte set to LoROM/FastROM.
struct TestRom {
  std::vector<snes::uint8> data = std::vector<snes::uint8>(0x8000, 0xEA);

  auto poke(snes::uint24 address, snes::uint8 value) -> TestRom& {
    data[address & 0x7FFF] = value;
    return *this;
  }
  auto poke16(snes::uint24 address, snes::uint16 value) -> TestRom& {
    data[address & 0x7FFF] = value & 0xFF;
    data[(address & 0x7FFF) + 1] = value >> 8;
    return *this;
  }
  auto program(const std::vector<snes::uint8>& bytes, snes::uint24 at) -> TestRom& {
    for (size_t i = 0; i < bytes.size(); i++) data[(at + i) & 0x7FFF] = bytes[i];
    return *this;
  }
};

void boot(snes::System& system, TestRom& rom) {
  rom.poke(0x007FD5, 0x20);        // LoROM, FastROM
  rom.poke16(0x00FFFC, 0x008000);  // reset vector -> the test program
  std::string error;
  CHECK(system.cartridge().load(rom.data, &error));
  CHECK(error.empty());
  system.bus().power();
  system.ppu().power();
  system.scheduler().reset();
  system.cpu().power();
  system.cpu().reset();
}

// Step the system until pred() holds or the cap is exhausted.
bool runUntil(snes::System& system, auto&& pred, snes::uint64 cap) {
  for (snes::uint64 i = 0; i < cap; i++) {
    if (pred()) return true;
    system.step();
  }
  return pred();
}

auto counter(snes::System& system) -> snes::uint8 {
  return system.bus().wram()[kCounter];
}

// inc $0200 / rti
const std::vector<snes::uint8> kIncRti = {0xEE, 0x00, 0x02, 0x40};

}  // namespace

namespace snes {

TEST_CASE("interrupt: NMI delivered from the PPU edge, once per frame, "
          "RTI returns to the loop") {
  TestRom rom;
  // lda #$80 / sta $4200 (NMI enable), then jmp-self wait loop (PC is
  // always $8005 between instructions, so the pushed PC is deterministic).
  rom.program({0xA9, 0x80, 0x8D, 0x00, 0x42}, 0x008000);
  rom.program({0x4C, 0x05, 0x80}, 0x008005);
  rom.program(kIncRti, kNmiHandler);
  rom.poke16(0x00FFFA, kNmiHandler);  // E-mode NMI vector

  System system;
  boot(system, rom);

  // VBlank at V=225: the internal NMI edge raises the pin; the CPU takes
  // the E-mode vector. Dispatch happened with I=1 (reset value): NMI is
  // not gated by I.
  CHECK(runUntil(system, [&] { return system.cpu().pc() == kNmiHandler; }, 100000));
  CHECK((system.cpu().flagP() & 0x04) == 0x04);  // I=1 inside the handler
  CHECK(system.cpu().stack() == 0x01FC);         // 3 pushes (E mode)
  // Pushed P/PCL/PCH: lda #$80 left N=1, I=1; B flag pushed as 0 in E
  // mode (stack layout: P at S+1, PCL at S+2, PCH at S+3 — cpu_tests).
  CHECK(system.bus().wram()[0x01FD] == 0xA4);
  CHECK(system.bus().wram()[0x01FE] == 0x05);
  CHECK(system.bus().wram()[0x01FF] == 0x80);

  // RTI restores PC to the loop and P (I back to 1).
  CHECK(runUntil(system, [&] { return system.cpu().pc() == 0x008005; }, 100));
  CHECK(counter(system) == 1);
  CHECK((system.cpu().flagP() & 0x04) == 0x04);

  // Exactly one NMI per frame (edge-detect, not level): the second frame
  // raises the second NMI, no re-fire inside the first VBlank.
  CHECK(runUntil(system, [&] { return counter(system) == 2 && system.cpu().pc() == 0x008005; },
                 600000));
  CHECK(counter(system) == 2);
}

TEST_CASE("interrupt: IRQ gated by I, then level re-fire after RTI without ack") {
  TestRom rom;
  // H-IRQ mode 1 (0x10), HTIME=10. The loop polls WRAM $0201 (go flag):
  // the test raises it when it wants the CPU to reach the cli, so the IRQ
  // latches while I=1 and the gating is observable first.
  rom.program({0xA9, 0x10, 0x8D, 0x00, 0x42,
               0xA9, 0x0A, 0x8D, 0x07, 0x42, 0x9C, 0x08, 0x42}, 0x008000);
  rom.program({0xAD, 0x01, 0x02}, 0x00800D);  // poll: lda $0201
  rom.program({0xF0, 0xFB}, 0x008010);        // beq $800D
  rom.program({0x58}, 0x008012);              // cli
  rom.program({0x4C, 0x13, 0x80}, 0x008013);  // jmp-self wait loop
  rom.program(kIncRti, kIrqHandler);
  rom.poke16(0x00FFFE, kIrqHandler);  // E-mode IRQ/BRK vector

  System system;
  boot(system, rom);

  // Long enough for several IRQ latches (H=10 of every scanline): the pin
  // is up but I=1 gates the dispatch.
  for (int i = 0; i < 2000; i++) system.step();
  CHECK(system.ppu().irqFlag() == true);   // latched, pin raised
  CHECK(counter(system) == 0);             // never dispatched
  CHECK((system.cpu().flagP() & 0x04) == 0x04);  // I=1

  // Release the poll: cli runs, the pending level IRQ fires immediately,
  // and because the handler never acks $4211, every RTI re-enters it.
  system.bus().write(0x000201, 1);
  CHECK(runUntil(system, [&] { return counter(system) >= 5; }, 5000));
  CHECK(counter(system) >= 5);

  // RTI keeps returning to the interrupted flow (P restored: I=0 between
  // dispatches).
  CHECK(runUntil(system, [&] { return system.cpu().pc() == 0x008013; }, 100));
  CHECK((system.cpu().flagP() & 0x04) == 0);
}

TEST_CASE("interrupt: $4211 read/ack limits the level IRQ to one per frame") {
  TestRom rom;
  // V-IRQ mode 2 (0x20), VTIME=5, cli, jmp-self loop.
  rom.program({0xA9, 0x20, 0x8D, 0x00, 0x42,
               0xA9, 0x05, 0x8D, 0x09, 0x42}, 0x008000);
  rom.program({0x9C, 0x0A, 0x42}, 0x00800A);  // stz $420A (VTIME high = 0)
  rom.program({0x58}, 0x00800D);              // cli
  rom.program({0x4C, 0x0E, 0x80}, 0x00800E);  // jmp-self wait loop
  // lda $4211 (ack) / inc $0200 / rti
  rom.program({0xAD, 0x11, 0x42, 0xEE, 0x00, 0x02, 0x40}, kIrqHandler);
  rom.poke16(0x00FFFE, kIrqHandler);

  System system;
  boot(system, rom);

  // Two frames -> exactly two dispatches (V=5 of each frame).
  CHECK(runUntil(system, [&] { return counter(system) == 2; }, 500000));
  CHECK(counter(system) == 2);

  // The ack really dropped the level: no re-fire for the rest of the frame.
  for (int i = 0; i < 500; i++) system.step();
  CHECK(counter(system) == 2);
  CHECK(runUntil(system, [&] { return system.cpu().pc() == 0x00800E; }, 100));
  CHECK((system.cpu().flagP() & 0x04) == 0);  // P restored by RTI (I=0)
}

}  // namespace snes
