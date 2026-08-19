#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "snes/snes.hpp"
#include "apu/apu.hpp"
#include "cpu/cpu65816.hpp"
#include "ppu/ppu.hpp"

// Phase 7 integration tests: APU handshake through the bus, and save/load
// state determinism. The save-state test is the strongest correctness net:
// it snapshots a running system, loads it into a fresh one, then steps both
// for a while and requires the re-serialized states to be byte-identical.

namespace {

// 32KB LoROM image default-filled with NOPs; map-mode byte set to LoROM.
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
  REQUIRE(system.cartridge().load(rom.data, &error));
  REQUIRE(error.empty());
  system.bus().power();
  system.ppu().power();
  system.apu().power();
  system.scheduler().reset();
  system.cpu().power();
  system.cpu().reset();
}

}  // namespace

namespace snes {

TEST_CASE("integration: APU boot ROM handshake ($AA/$BB) through $2140/$2141") {
  TestRom rom;
  rom.program({0x4C, 0x00, 0x80}, 0x008000);  // jmp $8000 (self loop)

  System system;
  boot(system, rom);

  // The CPU's scheduler sync drives the APU until its boot ROM posts $AA/$BB.
  bool posted = false;
  for (int i = 0; i < 200000 && !posted; i++) {
    system.step();
    posted = system.apu().readPort(0) == 0xAA && system.apu().readPort(1) == 0xBB;
  }
  CHECK(posted);
  CHECK(system.apu().readPort(0) == 0xAA);
  CHECK(system.apu().readPort(1) == 0xBB);
}

TEST_CASE("integration: System::setJoypad drives $4218/$4219 via auto-read") {
  TestRom rom;
  rom.program({0x4C, 0x00, 0x80}, 0x008000);  // jmp $8000 (self loop)

  System system;
  boot(system, rom);
  system.bus().write(0x004200, 0x01);    // NMITIMEN.0: enable auto-joypad-read
  system.setJoypad(0, 0x8000 | 0x0080);  // B + A

  // $4218/$4219 only update once per frame, at the auto-read window
  // (fullsnes "AUTO JOYPAD READ"); run long enough to cross it.
  for (int i = 0; i < 500000; i++) system.step();

  CHECK(system.bus().read(0x004219) == 0x80);
  CHECK(system.bus().read(0x004218) == 0x80);
}

TEST_CASE("save state: save/load is byte-exact and deterministic") {
  // A NMI-counter program so PPU timing, interrupts, WRAM and the CPU are
  // all exercised: lda #$80 / sta $4200, then a jmp-self wait loop, and a
  // NMI handler that increments WRAM $0200.
  TestRom rom;
  rom.program({0xA9, 0x80, 0x8D, 0x00, 0x42}, 0x008000);
  rom.program({0x4C, 0x05, 0x80}, 0x008005);
  rom.program({0xEE, 0x00, 0x02, 0x40}, 0x008100);  // inc $0200 / rti
  rom.poke16(0x00FFFA, 0x008100);                    // E-mode NMI vector

  System a;
  boot(a, rom);
  // Advance well past a frame boundary so the snapshot lands mid-frame.
  for (int i = 0; i < 30000; i++) a.step();
  const std::vector<uint8> state = a.saveState();
  CHECK(!state.empty());

  System b;
  boot(b, rom);
  REQUIRE(b.loadState(state));

  // Step both in lockstep and require byte-identical re-serialized state.
  for (int i = 0; i < 60000; i++) { a.step(); b.step(); }
  CHECK(a.saveState() == b.saveState());
  // And the observable CPU view agrees too.
  CHECK(a.cpu().pc() == b.cpu().pc());
  CHECK(a.bus().wram()[0x0200] == b.bus().wram()[0x0200]);
}

TEST_CASE("save state: corrupted or truncated data fails cleanly") {
  TestRom rom;
  rom.program({0x4C, 0x00, 0x80}, 0x008000);

  System system;
  boot(system, rom);
  const std::vector<uint8> state = system.saveState();

  // Truncate and corrupt; both must fail without crashing.
  std::vector<uint8> truncated(state.begin(), state.begin() + state.size() / 2);
  CHECK(!system.loadState(truncated));

  std::vector<uint8> corrupt = state;
  corrupt[0] ^= 0xFF;  // break the magic
  CHECK(!system.loadState(corrupt));
}

}  // namespace snes