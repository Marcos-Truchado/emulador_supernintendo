#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// Phase 4 MMIO: B-Bus register file $2100-$213F.
// All facts below are from fullsnes (noSns v1.6) with the ares PPU as the
// reference implementation for ordering and edge cases.
constexpr uint64 kLine = 341 * 4;
constexpr uint64 kFrame = 262 * 341 * 4;

TEST_CASE("ppu: power-on register values (fullsnes I/O map)") {
  Ppu ppu;
  ppu.power();

  // INIDISP = 80h on power: forced blank + brightness 0.
  CHECK(ppu.displayDisabled() == true);
  // VMAIN = 0Fh on power: increment step 128, translation mode 3.
  ppu.writeRegister(0x16, 0x00);  // VMADDL
  ppu.writeRegister(0x17, 0x00);  // VMADDH
  ppu.writeRegister(0x18, 0xAB);  // VMDATAL
  CHECK(ppu.vramRead(0x0000) == 0x00AB);
  CHECK(ppu.vramAddress() == 128);  // stepped by 128 after the low byte

  CHECK(ppu.vramAddress() == 128);
  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  CHECK(ppu.vramAddress() == 0);

  CHECK(ppu.oamAddress() == 0);
  CHECK(ppu.cgramAddress() == 0);
  CHECK(ppu.bgMode() == 0);
  CHECK(ppu.statRangeOver() == false);
  CHECK(ppu.statTimeOver() == false);
  CHECK(ppu.readRegister(0x3E) == 0x01);  // STAT77: version 1, no overflows
}

TEST_CASE("ppu: $2100 INIDISP forced blank and OAM reset at V=225") {
  Ppu ppu;
  ppu.power();

  ppu.writeRegister(0x00, 0x80);
  CHECK(ppu.displayDisabled() == true);
  ppu.writeRegister(0x00, 0x0F);
  CHECK(ppu.displayDisabled() == false);

  // OAM base address 0x50 via $2102 (bits 1-8).
  ppu.writeRegister(0x02, 0x28);
  CHECK(ppu.oamAddress() == 0x50);

  // Leaving forced blank exactly at V=225 resets the OAM address.
  ppu.writeRegister(0x00, 0x80);  // blank again
  ppu.step(225 * kLine);          // V=225, H=0
  ppu.writeRegister(0x00, 0x00);  // old blank bit + V==vdisp -> addressReset
  CHECK(ppu.oamAddress() == 0x50);
}

TEST_CASE("ppu: $2102/$2103/$2104 OAM address and write-twice data") {
  Ppu ppu;
  ppu.power();
  ppu.writeRegister(0x02, 0x00);
  ppu.writeRegister(0x03, 0x00);

  // Low byte of an even address only fills the latch.
  ppu.writeRegister(0x04, 0x34);
  CHECK(ppu.oamReadRaw(0) == 0x00);
  CHECK(ppu.oamReadRaw(1) == 0x00);
  CHECK(ppu.oamAddress() == 1);

  // Odd address commits both bytes.
  ppu.writeRegister(0x04, 0x56);
  CHECK(ppu.oamReadRaw(0) == 0x34);  // object 0 X low
  CHECK(ppu.oamReadRaw(1) == 0x56);  // object 0 Y
  CHECK(ppu.oamAddress() == 2);

  ppu.writeRegister(0x04, 0x78);
  ppu.writeRegister(0x04, 0x9A);
  CHECK(ppu.oamReadRaw(2) == 0x78);
  CHECK(ppu.oamReadRaw(3) == 0x9A);
  CHECK(ppu.oamAddress() == 4);

  // High table ($200-$21F) is single-byte: every write lands immediately.
  ppu.writeRegister(0x02, 0x00);
  ppu.writeRegister(0x03, 0x01);  // base bit9 -> 0x200
  CHECK(ppu.oamAddress() == 0x200);
  ppu.writeRegister(0x04, 0xA1);
  CHECK(ppu.oamReadRaw(0x200) == 0xA1);
  CHECK(ppu.oamAddress() == 0x201);
  ppu.writeRegister(0x04, 0xE2);
  CHECK(ppu.oamReadRaw(0x201) == 0xE2);
  CHECK(ppu.oamAddress() == 0x202);
}

TEST_CASE("ppu: $2105 BGMODE low 3 bits") {
  Ppu ppu;
  ppu.power();
  ppu.writeRegister(0x05, 0x29);
  CHECK(ppu.bgMode() == 1);
  ppu.writeRegister(0x05, 0x60);
  CHECK(ppu.bgMode() == 0);
  ppu.writeRegister(0x05, 0x07);
  CHECK(ppu.bgMode() == 7);
}

TEST_CASE("ppu: $2115/$2118/$2119 VRAM write and increment") {
  Ppu ppu;
  ppu.power();
  ppu.writeRegister(0x15, 0x00);  // step 1, no translation, inc after low

  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  CHECK(ppu.vramAddress() == 0);

  // Mode 0 (increment after the low byte): the pair straddles two words.
  ppu.writeRegister(0x18, 0xCD);
  CHECK(ppu.vramRead(0) == 0x00CD);
  CHECK(ppu.vramAddress() == 1);  // stepped after the low byte
  ppu.writeRegister(0x19, 0xAB);
  CHECK(ppu.vramRead(1) == 0xAB00);  // high byte landed in the next word
  CHECK(ppu.vramAddress() == 1);     // no step after the high byte

  ppu.writeRegister(0x18, 0x01);
  CHECK(ppu.vramRead(1) == 0xAB01);
  CHECK(ppu.vramAddress() == 2);
  ppu.writeRegister(0x19, 0x02);
  CHECK(ppu.vramRead(2) == 0x0200);
  CHECK(ppu.vramAddress() == 2);

  // Mode 1 (increment after the high byte): a pair lands in one word.
  ppu.writeRegister(0x15, 0x80);
  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0xCD);
  CHECK(ppu.vramRead(0) == 0x00CD);
  CHECK(ppu.vramAddress() == 0);  // no step after the low byte
  ppu.writeRegister(0x19, 0xAB);
  CHECK(ppu.vramRead(0) == 0xABCD);
  CHECK(ppu.vramAddress() == 1);  // stepped after the high byte
}

TEST_CASE("ppu: $2115 increment steps 1/32/128/128 and high mode") {
  Ppu ppu;
  ppu.power();

  ppu.writeRegister(0x15, 0x01);  // step 32
  ppu.writeRegister(0x16, 0x10);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0x11);
  CHECK(ppu.vramAddress() == 0x30);
  ppu.writeRegister(0x19, 0x22);
  CHECK(ppu.vramRead(0x10) == 0x0011);
  CHECK(ppu.vramRead(0x30) == 0x2200);  // high byte follows the address
  CHECK(ppu.vramAddress() == 0x30);

  ppu.writeRegister(0x15, 0x02);  // step 128
  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0x33);
  CHECK(ppu.vramAddress() == 128);

  ppu.writeRegister(0x15, 0x03);  // step 128 again
  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0x44);
  CHECK(ppu.vramAddress() == 128);

  ppu.writeRegister(0x15, 0x80);  // increment after the high byte
  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0x55);
  CHECK(ppu.vramAddress() == 0);
  ppu.writeRegister(0x19, 0x66);
  CHECK(ppu.vramAddress() == 1);
}

TEST_CASE("ppu: $2115 address translation rotates lower 8/9/10 bits") {
  Ppu ppu;
  ppu.power();

  // Mapping 0: identity.
  ppu.writeRegister(0x15, 0x00);
  ppu.writeRegister(0x16, 0x85);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0xFE);
  CHECK(ppu.vramRead(0x0085) == 0x00FE);
  CHECK(ppu.vramRead(0x002C) == 0x0000);

  // Mapping 1: rotate left 3 the lower 8 bits.
  //   0x0085 -> 0x002C
  ppu.writeRegister(0x15, 0x04);
  ppu.writeRegister(0x16, 0x85);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0x11);
  CHECK(ppu.vramRead(0x002C) == 0x0011);  // landed at the rotated address
  CHECK(ppu.vramRead(0x0085) == 0x00FE);  // mapping-0 write untouched

  // Mapping 2: rotate the lower 9 bits.  0x0145 -> 0x002D.
  ppu.writeRegister(0x15, 0x08);
  ppu.writeRegister(0x16, 0x45);
  ppu.writeRegister(0x17, 0x01);
  ppu.writeRegister(0x18, 0xFE);
  CHECK(ppu.vramRead(0x002D) == 0x00FE);

  // Mapping 3: rotate the lower 10 bits.  0x0245 -> 0x022C.
  ppu.writeRegister(0x15, 0x0C);
  ppu.writeRegister(0x16, 0x45);
  ppu.writeRegister(0x17, 0x02);
  ppu.writeRegister(0x18, 0xFE);
  CHECK(ppu.vramRead(0x022C) == 0x00FE);
}

TEST_CASE("ppu: $2139/$213A VRAM read prefetch (first value twice)") {
  Ppu ppu;
  ppu.power();

  // Write the pair with increment-after-high so both bytes land in word 4.
  ppu.writeRegister(0x15, 0x80);
  ppu.writeRegister(0x16, 0x04);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0xCD);
  ppu.writeRegister(0x19, 0xAB);  // vram[4] = 0xABCD
  CHECK(ppu.vramAddress() == 5);

  ppu.writeRegister(0x15, 0x00);  // mode 0 for the read phase
  ppu.writeRegister(0x16, 0x04);  // prefetch vram[4]
  CHECK(ppu.readRegister(0x39) == 0xCD);  // low; refetch (vram[4]) + step
  CHECK(ppu.vramAddress() == 5);
  // fullsnes quirk: the refetch happens BEFORE the increment, so the first
  // word is delivered twice.
  CHECK(ppu.readRegister(0x39) == 0xCD);
  CHECK(ppu.vramAddress() == 6);
  CHECK(ppu.readRegister(0x39) == 0x00);  // vram[5] low
  CHECK(ppu.vramAddress() == 7);
  CHECK(ppu.readRegister(0x3A) == 0x00);  // high of vram[6]; mode 0: no step

  ppu.writeRegister(0x15, 0x80);  // increment after the high byte
  ppu.writeRegister(0x16, 0x04);  // prefetch vram[4]
  CHECK(ppu.readRegister(0x39) == 0xCD);  // low; no step in mode 1
  CHECK(ppu.vramAddress() == 4);
  CHECK(ppu.readRegister(0x3A) == 0xAB);  // high of vram[4]; refetch + step
  CHECK(ppu.vramAddress() == 5);
  CHECK(ppu.readRegister(0x3A) == 0xAB);  // same quirk: vram[4] again
  CHECK(ppu.vramAddress() == 6);
  CHECK(ppu.readRegister(0x3A) == 0x00);  // vram[5] high
  CHECK(ppu.vramAddress() == 7);
}

TEST_CASE("ppu: VRAM writes and reads are gated during active display") {
  Ppu ppu;
  ppu.power();
  ppu.writeRegister(0x15, 0x00);
  ppu.writeRegister(0x00, 0x0F);  // display enabled
  ppu.step(kLine);                // V=1: active display

  ppu.writeRegister(0x16, 0x00);
  ppu.writeRegister(0x17, 0x00);
  ppu.writeRegister(0x18, 0xAB);  // write ignored...
  ppu.writeRegister(0x19, 0xCD);
  CHECK(ppu.vramRead(0) == 0x0000);  // ...VRAM untouched
  CHECK(ppu.vramAddress() == 1);     // but the address still steps
  CHECK(ppu.readRegister(0x39) == 0x00);  // prefetch reads return 0
  CHECK(ppu.vramAddress() == 2);

  ppu.writeRegister(0x00, 0x80);  // forced blank again
  ppu.writeRegister(0x18, 0xEF);
  CHECK(ppu.vramRead(2) == 0x00EF);  // writes work while blanked
}

TEST_CASE("ppu: $211B/$211C M7 write-twice and signed multiply") {
  Ppu ppu;
  ppu.power();

  ppu.writeRegister(0x1B, 0x00);  // M7A low
  ppu.writeRegister(0x1B, 0x01);  // M7A high -> 0x0100
  ppu.writeRegister(0x1C, 0x00);  // M7B low
  ppu.writeRegister(0x1C, 0x02);  // M7B high -> 0x0200
  CHECK(ppu.readRegister(0x34) == 0x00);  // 256 * 2 = 0x200
  CHECK(ppu.readRegister(0x35) == 0x02);
  CHECK(ppu.readRegister(0x36) == 0x00);

  ppu.writeRegister(0x1B, 0x00);
  ppu.writeRegister(0x1B, 0xFF);  // M7A = 0xFF00 = -256
  ppu.writeRegister(0x1C, 0x00);
  ppu.writeRegister(0x1C, 0xFF);  // M7B = 0xFF00 -> -1
  CHECK(ppu.readRegister(0x34) == 0x00);  // -256 * -1 = 0x100
  CHECK(ppu.readRegister(0x35) == 0x01);
  CHECK(ppu.readRegister(0x36) == 0x00);

  ppu.writeRegister(0x1B, 0x00);
  ppu.writeRegister(0x1B, 0x80);  // M7A = 0x8000 = -32768
  ppu.writeRegister(0x1C, 0x00);
  ppu.writeRegister(0x1C, 0x02);  // 2
  CHECK(ppu.readRegister(0x34) == 0x00);  // -65536 -> 24-bit 0xFF0000
  CHECK(ppu.readRegister(0x35) == 0x00);
  CHECK(ppu.readRegister(0x36) == 0xFF);
}

TEST_CASE("ppu: $2121/$2122 CGRAM write-twice and $213B read") {
  Ppu ppu;
  ppu.power();

  ppu.writeRegister(0x21, 0x08);
  ppu.writeRegister(0x22, 0x1F);  // low byte -> latch
  CHECK(ppu.cgramRead(8) == 0x0000);
  ppu.writeRegister(0x22, 0x7C);  // high byte -> commit (bit7 ignored)
  CHECK(ppu.cgramRead(8) == 0x7C1F);
  CHECK(ppu.cgramAddress() == 9);

  ppu.writeRegister(0x21, 0x09);  // CGADD resets the 1st/2nd flipflop
  ppu.writeRegister(0x22, 0xAA);
  CHECK(ppu.cgramRead(9) == 0x0000);
  ppu.writeRegister(0x22, 0x55);
  CHECK(ppu.cgramRead(9) == 0x55AA);
  CHECK(ppu.cgramAddress() == 10);

  ppu.writeRegister(0x21, 0x08);
  ppu.writeRegister(0x22, 0x1F);
  ppu.writeRegister(0x22, 0x7C);
  ppu.writeRegister(0x21, 0x08);  // reset the flipflop for reading
  CHECK(ppu.readRegister(0x3B) == 0x1F);  // low byte
  CHECK(ppu.readRegister(0x3B) == 0x7C);  // high byte bits 0-6
  CHECK(ppu.cgramAddress() == 9);
}

TEST_CASE("ppu: $213C/$213D counter latches and 1st/2nd reads") {
  Ppu ppu;
  ppu.power();
  ppu.step(7 * kLine + 300 * 4);  // H=300, V=7

  ppu.setWrio(0x80);
  ppu.captureCounters();  // $2137 read equivalent
  CHECK(ppu.readRegister(0x3C) == 0x2C);  // 300 low byte
  CHECK(ppu.readRegister(0x3C) == 0x2D);  // bit0 = bit8 of 300
  CHECK(ppu.readRegister(0x3D) == 0x07);
  CHECK(ppu.readRegister(0x3D) == 0x06);  // bit0 = bit8 of 7

  // STAT78: latch flag set by captureCounters, cleared by the read.
  CHECK(ppu.readRegister(0x3F) == 0x41);
  CHECK(ppu.readRegister(0x3F) == 0x01);
  // Reading STAT78 also resets both 1st/2nd-read flipflops.
  CHECK(ppu.readRegister(0x3C) == 0x2C);
  // Restore the PPU2 MDR (bit5 of STAT78 tracks it): 0x2C has bit5 set.
  CHECK(ppu.readRegister(0x3D) == 0x07);  // first read again; MDR = 0x07

  // WRIO bit7 clear: the latch flag reads as set and is never cleared.
  ppu.captureCounters();  // re-latch (wrio still set)
  ppu.setWrio(0x00);
  CHECK(ppu.readRegister(0x3F) == 0x41);  // reads as set...
  CHECK(ppu.readRegister(0x3F) == 0x41);  // ...and is not cleared
  ppu.setWrio(0x80);
  // The flag was never cleared during the WRIO-clear window, so it still
  // reads as set; this read finally clears it.
  CHECK(ppu.readRegister(0x3F) == 0x41);
  CHECK(ppu.readRegister(0x3F) == 0x01);
}

TEST_CASE("ppu: $213E/$213F status, versions and PPU1 open bus bit") {
  Ppu ppu;
  ppu.power();

  CHECK(ppu.readRegister(0x3E) == 0x01);

  // STAT77 bit4 reflects the PPU1 open bus (MDR from the last PPU1 read).
  ppu.writeRegister(0x1B, 0x30);
  ppu.writeRegister(0x1B, 0x01);  // M7A = 0x0130
  ppu.writeRegister(0x1C, 0x00);
  ppu.writeRegister(0x1C, 0x02);  // product = 0x260
  CHECK(ppu.readRegister(0x34) == 0x60);  // MDR = 0x60 now
  CHECK(ppu.readRegister(0x3E) == 0x01);  // 0x60 bit4 = 0, version 1
}

TEST_CASE("ppu: $2133 SETINI interlace drives the STAT78 field bit") {
  Ppu ppu;
  ppu.power();
  ppu.writeRegister(0x33, 0x01);  // interlace on
  CHECK((ppu.readRegister(0x3F) & 0x80) == 0x00);  // first field

  ppu.step(kFrame);
  CHECK(ppu.frame() == 1);
  CHECK((ppu.readRegister(0x3F) & 0x80) == 0x80);  // second field
}

}  // namespace snes
