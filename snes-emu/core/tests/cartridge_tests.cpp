#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"
#include "scheduler/scheduler.hpp"

TEST_CASE("cartridge: LoROM map mode detection from header byte") {
  // 256KB "LoROM" test image, header byte $20 (LoROM family), no copier header.
  std::vector<snes::uint8> rom(0x40000, 0xEA);
  rom[0x7FD5] = 0x20;
  rom[0x7FC0] = 'T';

  snes::Cartridge cart;
  std::string error;
  REQUIRE(cart.load(std::move(rom), &error));
  CHECK(cart.mapMode() == snes::MapMode::lorom);
  CHECK(cart.hasCopierHeader() == false);
  CHECK(cart.romSize() == 0x40000);
}

TEST_CASE("cartridge: copier header detection and strip") {
  // 256KB logical ROM plus a 512-byte copier header => 0x40200 bytes on disk.
  std::vector<snes::uint8> data(0x40000 + 512, 0x00);
  std::fill(data.begin() + 512, data.end(), 0xEA);
  data[512 + 0x7FD5] = 0x20;

  snes::Cartridge cart;
  std::string error;
  REQUIRE(cart.load(std::move(data), &error));
  CHECK(cart.hasCopierHeader() == true);
  CHECK(cart.romSize() == 0x40000);
  CHECK(cart.mapMode() == snes::MapMode::lorom);
}

TEST_CASE("cartridge: LoROM address -> rom offset") {
  std::vector<snes::uint8> rom(0x40000, 0xEA);
  rom[0x7FD5] = 0x20;

  snes::Cartridge cart;
  std::string error;
  REQUIRE(cart.load(std::move(rom), &error));

  // bank $00, address $8000 -> offset 0x00000
  CHECK(cart.romOffset(0x008000) == 0x00000);
  // bank $00, address $FFFC (reset vector) -> offset 0x7FFC
  CHECK(cart.romOffset(0x00FFFC) == 0x07FFC);
  // bank $01, address $8000 -> offset 0x08000
  CHECK(cart.romOffset(0x018000) == 0x08000);
  // middle of bank $03, address $BFC0 -> 3*0x8000 + 0x3FC0 = 0x1BFC0
  CHECK(cart.romOffset(0x03BFC0) == 0x1BFC0);
}

TEST_CASE("bus: WRAM + MMIO + ROM reads") {
  snes::Cartridge cart;
  std::string error;
  std::vector<snes::uint8> rom(0x40000, 0xFF);
  rom[0x7FD5] = 0x20;
  REQUIRE(cart.load(std::move(rom), &error));

  snes::Ppu ppu;
  snes::Scheduler scheduler(ppu);
  snes::Bus bus(cart, ppu, scheduler);

  // WRAM write/read
  bus.write(0x7E0000, 0x42);
  CHECK(bus.read(0x7E0000) == 0x42);
  // WRAM mirror $2000-$3FFF
  bus.write(0x012000, 0x99);
  CHECK(bus.read(0x010000) == 0x99);
  // ROM through LoROM mapping
  CHECK(bus.read(0x008000) == 0xFF);
  // $4210 RDNMI: 5A22 version 2, NMI flag clear (PPU idle -> no VBlank)
  CHECK(bus.read(0x004210) == 0x02);
  CHECK(bus.read(0x004210) == 0x02);
  // $4212 idle
  CHECK(bus.read(0x004212) == 0x00);
}

TEST_CASE("cartridge: LoROM 64KB windows in banks 40-7D / C0-FF") {
  // 4MB LoROM: banks 40-5F mirror banks 00-3F:8000 (0-2MB),
  // banks 60-7D continue linearly (2-4MB), C0-FF mirror 40-7D.
  std::vector<snes::uint8> rom(0x400000, 0xEA);
  rom[0x7FD5] = 0x20;

  snes::Cartridge cart;
  std::string error;
  REQUIRE(cart.load(std::move(rom), &error));

  CHECK(cart.romOffset(0x400000) == 0x000000);  // mirror of $00:8000
  CHECK(cart.romOffset(0x408000) == 0x008000);
  CHECK(cart.romOffset(0x5FFFFF) == 0x1FFFFF);
  CHECK(cart.romOffset(0x600000) == 0x200000);  // continuation
  CHECK(cart.romOffset(0x608000) == 0x208000);
  CHECK(cart.romOffset(0x7DFFFF) == 0x3DFFFF);
  CHECK(cart.romOffset(0xC00000) == 0x000000);  // mirror of $40:0000
  CHECK(cart.romOffset(0xFFF000) == 0x3FF000);
}

TEST_CASE("cartridge: HiROM mapping") {
  std::vector<snes::uint8> rom(0x400000, 0xEA);
  rom[0xFFD5] = 0x21;

  snes::Cartridge cart;
  std::string error;
  REQUIRE(cart.load(std::move(rom), &error));

  // Banks 40-7D are full 64KB windows.
  CHECK(cart.romOffset(0x400000) == 0x000000);
  CHECK(cart.romOffset(0x408000) == 0x008000);
  CHECK(cart.romOffset(0x7DFFFF) == 0x3DFFFF);
  // System banks mirror 40-7D at +$8000.
  CHECK(cart.romOffset(0x008000) == 0x400000);
  CHECK(cart.romOffset(0x018000) == 0x410000);
  CHECK(cart.romOffset(0x808000) == 0x400000);
  // C0-FF mirror 40-7D.
  CHECK(cart.romOffset(0xC00000) == 0x000000);
  CHECK(cart.romOffset(0xFF0000) == 0x3F0000);
}

TEST_CASE("cartridge: ExHiROM mapping") {
  // 6MB ExHiROM: 40-7D cover the first 4MB, C0-FF the last 2MB.
  std::vector<snes::uint8> rom(0x600000, 0xEA);
  rom[0x40FFD5] = 0x22;

  snes::Cartridge cart;
  std::string error;
  REQUIRE(cart.load(std::move(rom), &error));

  CHECK(cart.romOffset(0x400000) == 0x000000);
  CHECK(cart.romOffset(0x418000) == 0x018000);
  CHECK(cart.romOffset(0x7DFFFF) == 0x3DFFFF);
  CHECK(cart.romOffset(0xC00000) == 0x400000);  // second half
  CHECK(cart.romOffset(0xE00000) == 0x600000);
  CHECK(cart.romOffset(0x008000) == 0x000000);  // system mirror
  CHECK(cart.romOffset(0x808000) == 0x000000);
}

TEST_CASE("cartridge: SRAM size from header byte") {
  // (1 SHL n) KB for n = bits 2-0 of the header RAM-size byte ($FFD8 family).
  {
    std::vector<snes::uint8> rom(0x40000, 0xEA);
    rom[0x7FD5] = 0x20;
    rom[0x7FD8] = 0x03;  // 8KB
    snes::Cartridge cart;
    std::string error;
    REQUIRE(cart.load(std::move(rom), &error));
    CHECK(cart.sramSize() == 0x2000);
  }
  {
    std::vector<snes::uint8> rom(0x40000, 0xEA);
    rom[0x7FD5] = 0x20;
    rom[0x7FD8] = 0x00;  // no SRAM
    snes::Cartridge cart;
    std::string error;
    REQUIRE(cart.load(std::move(rom), &error));
    CHECK(cart.sramSize() == 0);
  }
  {
    std::vector<snes::uint8> rom(0x400000, 0xEA);
    rom[0xFFD5] = 0x21;
    rom[0xFFD8] = 0x01;  // 2KB (HiROM header at $FFD8)
    snes::Cartridge cart;
    std::string error;
    REQUIRE(cart.load(std::move(rom), &error));
    CHECK(cart.sramSize() == 0x800);
  }
  {
    std::vector<snes::uint8> rom(0x600000, 0xEA);
    rom[0x40FFD5] = 0x22;
    rom[0x40FFD8] = 0x05;  // 32KB (ExHiROM header at $40FFD8)
    snes::Cartridge cart;
    std::string error;
    REQUIRE(cart.load(std::move(rom), &error));
    CHECK(cart.sramSize() == 0x8000);
  }
}