#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

// Build a cart of the given map mode with its header set; sramExp 0 means the
// header advertises no battery SRAM (tests that need SRAM pass 3 = 8KB).
static snes::Cartridge makeCart(snes::MapMode mode, size_t size, snes::uint8 sramExp = 0) {
  std::vector<snes::uint8> rom(size, 0xEA);
  snes::uint32 hdr = mode == snes::MapMode::lorom     ? 0x7FD5
                     : mode == snes::MapMode::hirom   ? 0xFFD5
                     : mode == snes::MapMode::exhirom ? 0x40FFD5
                                                      : 0x7FD5;
  rom[hdr] = mode == snes::MapMode::lorom     ? 0x20
             : mode == snes::MapMode::hirom   ? 0x21
             : mode == snes::MapMode::exhirom ? 0x22
                                              : 0x20;
  rom[hdr + 3] = sramExp;  // RAM-size byte ($FFD8 family)

  snes::Cartridge cart;
  std::string error;
  if (!cart.load(std::move(rom), &error)) FAIL(error);
  return cart;
}

TEST_CASE("bus: LoROM routing (ROM windows, SRAM, WRAM)") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x400000, 0x03);  // 4MB + 8KB SRAM
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  // Banks 40-5F mirror banks 00-3F:8000; 60-7D continue linearly.
  CHECK(bus.read(0x400000) == 0xEA);
  CHECK(bus.read(0x408000) == 0xEA);
  CHECK(bus.read(0x600000) == 0xEA);
  // ROM writes are ignored.
  bus.write(0x408000, 0x99);
  CHECK(bus.read(0x408000) == 0xEA);

  // SRAM at 70-7D/F0-FF:0000-7FFF (8KB, mirrored across banks).
  bus.write(0x700000, 0x42);
  CHECK(bus.read(0x700000) == 0x42);
  CHECK(bus.read(0x710000) == 0x42);  // mirror bank
  CHECK(bus.read(0xF00000) == 0x42);  // mirror bank
  CHECK(bus.read(0x700010) == 0x00);  // distinct SRAM byte
  // Same bank above $8000 is ROM again.
  CHECK(bus.read(0x708000) == 0xEA);

  // Full 128KB WRAM.
  bus.write(0x7EFFFF, 0x11);
  CHECK(bus.read(0x7EFFFF) == 0x11);
  bus.write(0x7F0000, 0x22);
  CHECK(bus.read(0x7F0000) == 0x22);

  // Unmapped bank 44:0000 is ROM; bank 00 offset 4400 is open bus.
  CHECK(bus.read(0x440000) == 0xEA);
  bus.write(0x7E0000, 0xAB);
  CHECK(bus.read(0x004400) == 0xAB);
}

TEST_CASE("bus: HiROM routing (ROM windows, WRAM mirrors, SRAM)") {
  snes::Cartridge cart = makeCart(snes::MapMode::hirom, 0x400000, 0x03);  // 4MB + 8KB SRAM
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  CHECK(bus.read(0x400000) == 0xEA);
  CHECK(bus.read(0x408000) == 0xEA);
  // System banks mirror 40-7D at +$8000.
  CHECK(bus.read(0x008000) == 0xEA);
  CHECK(bus.read(0x808000) == 0xEA);
  // 3E-3F/BE-BF mirror the full 128KB WRAM.
  bus.write(0x3E0000, 0x33);
  CHECK(bus.read(0x3E0000) == 0x33);
  CHECK(bus.read(0x7E0000) == 0x33);
  bus.write(0x3F8000, 0x44);
  CHECK(bus.read(0x3F8000) == 0x44);
  CHECK(bus.read(0x7F8000) == 0x44);
  bus.write(0xBE0000, 0x55);
  CHECK(bus.read(0x7E0000) == 0x55);

  // SRAM at 20-3F/A0-BF:6000-7FFF.
  bus.write(0x206000, 0x42);
  CHECK(bus.read(0x206000) == 0x42);
  CHECK(bus.read(0x206005) == 0x00);
  CHECK(bus.read(0xA06000) == 0x42);  // mirror bank
  // Bank 60:6000 is ROM, not SRAM, in HiROM.
  CHECK(bus.read(0x606000) == 0xEA);
  // Lower half of 40-7D is ROM in HiROM.
  CHECK(bus.read(0x407000) == 0xEA);

  // Open bus in unused system-bank space.
  bus.write(0x7E0000, 0xAB);
  CHECK(bus.read(0x004400) == 0xAB);
}

TEST_CASE("bus: ExHiROM routing (two 4MB halves, no SRAM window)") {
  snes::Cartridge cart = makeCart(snes::MapMode::exhirom, 0x600000);  // 6MB, no SRAM
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  CHECK(bus.read(0x400000) == 0xEA);
  CHECK(bus.read(0xC00000) == 0xEA);  // second half
  CHECK(bus.read(0xE00000) == 0xEA);
  // System mirrors of the first half.
  CHECK(bus.read(0x008000) == 0xEA);
  CHECK(bus.read(0x808000) == 0xEA);
  // No SRAM: 6000-7FFF is open bus in system banks.
  bus.write(0x7E0000, 0xAB);
  CHECK(bus.read(0x006000) == 0xAB);
  // 40-7D lower halves are ROM (not WRAM).
  CHECK(bus.read(0x406000) == 0xEA);
}

TEST_CASE("bus: multiply and divide ports") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  // $4202/$4203 WRMPYA/WRMPYB: 0x12 * 0x34 = 0x3A8.
  bus.write(0x004202, 0x12);
  bus.write(0x004203, 0x34);
  CHECK(bus.read(0x004216) == 0xA8);
  CHECK(bus.read(0x004217) == 0x03);
  // Fullsnes quirk: multiply also sets RDDIVL=WRMPYB, RDDIVH=0.
  CHECK(bus.read(0x004214) == 0x34);
  CHECK(bus.read(0x004215) == 0x00);

  // $4204-$4206 WRDIV: 0x1337 / 5 = 0x3D7, remainder 4.
  bus.write(0x004204, 0x37);
  bus.write(0x004205, 0x13);
  bus.write(0x004206, 0x05);
  CHECK(bus.read(0x004214) == 0xD7);
  CHECK(bus.read(0x004215) == 0x03);
  CHECK(bus.read(0x004216) == 0x04);
  CHECK(bus.read(0x004217) == 0x00);

  // Division by zero: quotient FFFFh, remainder = dividend.
  bus.write(0x004204, 0x37);
  bus.write(0x004205, 0x13);
  bus.write(0x004206, 0x00);
  CHECK(bus.read(0x004214) == 0xFF);
  CHECK(bus.read(0x004215) == 0xFF);
  CHECK(bus.read(0x004216) == 0x37);
  CHECK(bus.read(0x004217) == 0x13);
}

TEST_CASE("bus: WRAM port via $2180-$2183") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  // 17-bit address: WMADDL/WMADDM/WMADDH.
  bus.write(0x002181, 0xFE);
  bus.write(0x002182, 0xFF);
  bus.write(0x002183, 0x01);
  CHECK(bus.wramAddress() == 0x1FFFE);

  // Writes land at the address and auto-increment.
  bus.write(0x002180, 0x42);
  CHECK(bus.read(0x7FFFFE) == 0x42);  // 17-bit index 0x1FFFE = $7F:FFFE
  CHECK(bus.wramAddress() == 0x1FFFF);

  // Reads return the addressed byte and auto-increment.
  CHECK(bus.read(0x002180) == 0x00);  // wram[0x1FFFF] untouched
  CHECK(bus.wramAddress() == 0x00000);
  bus.write(0x002180, 0x99);
  CHECK(bus.read(0x7E0000) == 0x99);

  // Partial address updates keep other bytes.
  bus.write(0x002183, 0x00);
  bus.write(0x002182, 0x10);
  bus.write(0x002181, 0x20);
  CHECK(bus.wramAddress() == 0x1020);
}

TEST_CASE("bus: PPU write-only shadows and open-bus reads") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  bus.write(0x002100, 0x0F);
  CHECK(bus.ppuRegister(0x00) == 0x0F);
  bus.write(0x002133, 0x81);
  CHECK(bus.ppuRegister(0x33) == 0x81);

  // Reads of write-only registers return the last data-bus byte.
  bus.write(0x7E0000, 0xAA);
  CHECK(bus.read(0x002100) == 0xAA);
  CHECK(bus.read(0x002101) == 0xAA);
  bus.write(0x7E0000, 0xBB);
  CHECK(bus.read(0x002137) == 0xBB);  // SLHV latch strobe
  CHECK(bus.read(0x002138) == 0x00);  // RDOAM: latched 0
}

TEST_CASE("bus: APU ports R/W with mirroring") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  bus.write(0x002140, 0x55);
  CHECK(bus.apuPort(0) == 0x55);
  CHECK(bus.read(0x002140) == 0x55);
  // $2144-$217F mirror the four ports.
  bus.write(0x002144, 0x66);
  CHECK(bus.apuPort(0) == 0x66);
  bus.write(0x002147, 0x77);
  CHECK(bus.apuPort(3) == 0x77);
  CHECK(bus.read(0x00217F) == 0x77);
}

TEST_CASE("bus: DMA channel registers and $43xF mirror") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  // Power-on: DMAPx=FF, A1Bx undefined (fullsnes xxh) -> FF in this impl.
  CHECK(bus.dmaRegister(0x00) == 0xFF);
  CHECK(bus.dmaRegister(0x04) == 0xFF);

  bus.write(0x004305, 0xAB);
  CHECK(bus.dmaRegister(0x05) == 0xAB);
  CHECK(bus.read(0x004305) == 0xAB);
  // $43xF is a mirror of the same channel's $43xB.
  bus.write(0x00431F, 0xCD);
  CHECK(bus.dmaRegister(0x1B) == 0xCD);  // channel 1's $43xB
  CHECK(bus.read(0x00431F) == 0xCD);
  // $43xC-$43xE are unused -> open bus.
  CHECK(bus.read(0x00430C) == 0xCD);
}

TEST_CASE("bus: open bus reflects the last data-bus byte") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  bus.write(0x7E0000, 0xAB);
  CHECK(bus.read(0x004400) == 0xAB);
  CHECK(bus.read(0x004401) == 0xAB);  // reading open bus does not change it
  CHECK(bus.openBus() == 0xAB);
  // Writes also drive the data bus.
  bus.write(0x004400, 0xCD);
  CHECK(bus.read(0x004401) == 0xCD);
}

TEST_CASE("bus: power and reset register semantics") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);

  bus.power();
  // Power-on values from fullsnes (bracketed [=] only at power-on).
  CHECK(bus.cpuRegister(0x00) == 0x00);  // 4200 NMITIMEN
  CHECK(bus.cpuRegister(0x01) == 0xFF);  // 4201 WRIO
  CHECK(bus.cpuRegister(0x07) == 0xFF);  // 4207 HTIMEL
  CHECK(bus.cpuRegister(0x08) == 0x01);  // 4208 HTIMEH
  CHECK(bus.cpuRegister(0x09) == 0xFF);  // 4209 VTIMEL
  CHECK(bus.cpuRegister(0x0A) == 0x01);  // 420A VTIMEH
  CHECK(bus.read(0x004214) == 0x00);     // RDDIVL reset
  CHECK(bus.read(0x004216) == 0x00);     // RDMPYL reset

  bus.write(0x004200, 0x81);
  bus.write(0x00420B, 0xFF);  // MDMAEN
  bus.write(0x004304, 0xAB);  // A1B0 (unchanged by reset)

  bus.reset();
  // Soft reset restores un-bracketed values only.
  CHECK(bus.cpuRegister(0x00) == 0x00);
  CHECK(bus.cpuRegister(0x01) == 0xFF);
  CHECK(bus.cpuRegister(0x07) == 0xFF);
  CHECK(bus.cpuRegister(0x08) == 0x01);
  CHECK(bus.cpuRegister(0x0B) == 0x00);  // MDMAEN cleared
  CHECK(bus.dmaRegister(0x00) == 0xFF);  // DMAPx restored
  CHECK(bus.dmaRegister(0x04) == 0xAB);  // A1Bx survives reset
  // Registers still writable after reset.
  bus.write(0x004200, 0x81);
  CHECK(bus.cpuRegister(0x00) == 0x81);
}

TEST_CASE("bus: $4210 RDNMI read/ack vs $4212 live mirror (PPU)") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();
  ppu.power();

  // No VBlank yet: RDNMI is version 2 with bit7 clear; repeated reads stay 0.
  CHECK(bus.read(0x004210) == 0x02);
  CHECK(bus.read(0x004210) == 0x02);
  CHECK(bus.read(0x004212) == 0x00);

  // Drive the PPU into VBlank (V=225): 225 scanlines of 341 dots each.
  ppu.step(225ull * 341 * 4);

  // $4212 is a live mirror: bit7 stays set across repeated reads. Bit6
  // (HBlank) is also live and set at dot 0 (clears at H=1), so 0xC0.
  CHECK(bus.read(0x004212) == 0xC0);
  CHECK(bus.read(0x004212) == 0xC0);
  // $4210 is a latched Read/Ack flag: the first read clears it.
  CHECK(bus.read(0x004210) == 0x82);
  CHECK(bus.read(0x004210) == 0x02);

  // End of VBlank (V=0): both flags clear; NMI flag auto-acks too.
  ppu.step((262 - 225) * 341 * 4);
  CHECK(bus.read(0x004212) == 0x40);  // bit7 clear, bit6 HBlank live at dot 0
  CHECK(bus.read(0x004210) == 0x02);
}

TEST_CASE("bus: $4200/$4209/$420A -> PPU V-IRQ, $4211 read/ack") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x40000);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();
  ppu.power();

  // Enable V-IRQ at VTIME=100 (mode 2): $4200 = $20, $4209/$420A = 100.
  bus.write(0x004200, 0x20);
  bus.write(0x004209, 100);
  bus.write(0x00420A, 0x00);

  // Step to just before V=100.
  ppu.step(100ull * 341 * 4 - 4);
  CHECK(bus.read(0x004211) == 0x00);  // not yet

  // Entering V=100, H=0 triggers the IRQ flag; read/ack clears it.
  ppu.step(4);
  CHECK(bus.read(0x004211) == 0x80);
  CHECK(bus.read(0x004211) == 0x00);
  // No retrigger within the same scanline.
  ppu.step(4);
  CHECK(bus.read(0x004211) == 0x00);

  // Disabling IRQs ($4200 bits 5-4 -> 0) also acknowledges them.
  ppu.step(262ull * 341 * 4);  // next frame: V=100 triggers again
  CHECK(bus.read(0x004211) == 0x80);
  bus.write(0x004200, 0x00);
  CHECK(bus.read(0x004211) == 0x00);
}

TEST_CASE("bus: waitstates per memory region (fullsnes memory map)") {
  snes::Cartridge cart = makeCart(snes::MapMode::lorom, 0x400000, 0x03);
  snes::Ppu ppu;
  snes::Bus bus(cart, ppu);
  bus.power();

  // WRAM + mirrors: 8.
  CHECK(bus.waitStates(0x7E0000) == 8);
  CHECK(bus.waitStates(0x7F8000) == 8);
  CHECK(bus.waitStates(0x001000) == 8);
  // Unused + B-Bus I/O + CPU I/O: 6.
  CHECK(bus.waitStates(0x002000) == 6);
  CHECK(bus.waitStates(0x002100) == 6);
  CHECK(bus.waitStates(0x004200) == 6);
  // Manual joypad: 12.
  CHECK(bus.waitStates(0x004016) == 12);
  CHECK(bus.waitStates(0x0041FF) == 12);
  // Expansion/SRAM window: 8.
  CHECK(bus.waitStates(0x006000) == 8);

  // WS1 LoROM (00-3F:8000-FFFF): fixed 8.
  CHECK(bus.waitStates(0x008000) == 8);
  CHECK(bus.waitStates(0x3F8000) == 8);
  // WS1 HiROM (40-7D): fixed 8.
  CHECK(bus.waitStates(0x400000) == 8);
  CHECK(bus.waitStates(0x7D0000) == 8);

  // WS2 (80-BF:8000-FFFF, C0-FF): 8 by default, 6 after $420D bit0.
  CHECK(bus.waitStates(0x808000) == 8);
  CHECK(bus.waitStates(0xC00000) == 8);
  bus.write(0x00420D, 0x01);
  CHECK(bus.waitStates(0x808000) == 6);
  CHECK(bus.waitStates(0xC00000) == 6);
  // WS1 unaffected by MEMSEL.
  CHECK(bus.waitStates(0x008000) == 8);
  CHECK(bus.waitStates(0x400000) == 8);
  // Reset restores SlowROM.
  bus.reset();
  CHECK(bus.waitStates(0x808000) == 8);
  CHECK(bus.waitStates(0xC00000) == 8);
}
