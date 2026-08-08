#include <doctest/doctest.h>

#include "snes/snes.hpp"

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

  snes::Bus bus(cart);

  // WRAM write/read
  bus.write(0x7E0000, 0x42);
  CHECK(bus.read(0x7E0000) == 0x42);
  // WRAM mirror $2000-$3FFF
  bus.write(0x012000, 0x99);
  CHECK(bus.read(0x010000) == 0x99);
  // ROM through LoROM mapping
  CHECK(bus.read(0x008000) == 0xFF);
  // $4210 alternates for wait_for_vblank
  CHECK(bus.read(0x004210) == 0x00);
  CHECK(bus.read(0x004210) == 0x80);
  // $4212 idle
  CHECK(bus.read(0x004212) == 0x00);
}