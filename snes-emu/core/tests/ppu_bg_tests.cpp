#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// Phase 4 backgrounds: real rendering for BG modes 0-6.
// All facts follow fullsnes (noSns v1.6); ordering follows the ares PPU
// (per-dot fetch slots, below/above passes, mosaic, offset-per-tile).
constexpr uint64 kLine = 341 * 4;
constexpr uint64 kFrame = 262 * 341 * 4;

// The SNES fetches tile ROW = V + vscroll for display line V (line 0 is the
// top border), so line 1 renders tile row 1 at vscroll 0. The fixture uses
// vscroll = -1 so the sampled lines show the map row they sit on.
struct BgFixture {
  Ppu ppu;
  BgFixture() {
    ppu.power();
    // Linear VRAM access: increment 1, no translation, increment on the
    // high byte ($2119) so a VMDATAL+VMDATAH pair stays one 16-bit word.
    ppu.writeRegister(0x15, 0x80);
    // vscroll -1 (10-bit): line V fetches tile row V-1, and both the name
    // table and the BG3 offset table stay on map row 0 for line 1. Written
    // for all four layers (BG1 $210E, BG2 $2110, BG3 $2112, BG4 $2114).
    for (uint8 r : {0x0E, 0x10, 0x12, 0x14}) {
      ppu.writeRegister(r, 0xFF);
      ppu.writeRegister(r, 0x03);
    }
  }
  void vram(uint16 address, uint16 data) {
    ppu.writeRegister(0x16, address & 0xFF);
    ppu.writeRegister(0x17, address >> 8);
    ppu.writeRegister(0x18, data & 0xFF);
    ppu.writeRegister(0x19, data >> 8);
  }
  void cgram(uint8 index, uint16 color) {
    ppu.writeRegister(0x21, index);
    ppu.writeRegister(0x22, color & 0xFF);
    ppu.writeRegister(0x22, color >> 8);
  }
  void show() { ppu.writeRegister(0x00, 0x0F); }
  // Run dots until display pixel x of line y has just been painted.
  void paint(int y, int x) {
    uint64 target = uint64(y) * kLine + uint64(14 + x) * 4;
    uint64 current = (uint64(ppu.scanline()) * 341 + ppu.dot()) * 4;
    ppu.step(target - current);
  }
  // Row of line y in the raw 564-wide framebuffer (non-overscan).
  uint16 raw(int y, int col) {
    uint16 v = ppu.frameBuffer()[uint64(y + 8) * 564 + col];
    if (getenv("PPU_DEBUG") && y == 1 && col <= 40) printf("raw y=1 col=%d -> %04X\n", col, v);
    return v;
  }
  // Fill one 8bpp (256-color) tile at the given word address: every pixel
  // is `color` (8 planes, packed two per word, one word-pair per row pair).
  void tile8(uint16 address, uint8 color) {
    for (int row = 0; row < 8; row++) {
      vram(address + row, uint16((color & 1 ? 0xFF : 0) | (color & 2 ? 0xFF00 : 0)));
      vram(address + 8 + row, uint16((color & 4 ? 0xFF : 0) | (color & 8 ? 0xFF00 : 0)));
      vram(address + 16 + row, uint16((color & 16 ? 0xFF : 0) | (color & 32 ? 0xFF00 : 0)));
      vram(address + 24 + row, uint16((color & 64 ? 0xFF : 0) | (color & 128 ? 0xFF00 : 0)));
    }
  }
};

TEST_CASE("ppu: mode 0 renders a 2bpp tile with map and char addressing") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);  // mode 0
  f.ppu.writeRegister(0x07, 0x00);  // BG1SC base 0
  f.ppu.writeRegister(0x0B, 0x02);  // BG1 tile base 0x2000
  f.ppu.writeRegister(0x2C, 0x01);  // TM: BG1
  f.vram(0x0000, 0x0000);          // map (0,0): char 0
  f.vram(0x0001, 0x0001);          // map (1,0): char 1 (empty data)
  f.vram(0x2000, 0x00FF);          // char 0 row 0: plane0 = 0xFF -> color 1
  f.cgram(1, 0x7FFF);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 7);
  CHECK(f.ppu.pixelColor(7, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 8);
  CHECK(f.ppu.pixelColor(8, 1) == 0x8000);  // char 1 has no data
  // Second line (row 1) is transparent: backdrop stays.
  f.paint(2, 0);
  CHECK(f.ppu.pixelColor(0, 2) == 0x8000);
}

TEST_CASE("ppu: mode 0 palette group, transparency, hmirror, vmirror") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.vram(0x0000, 0x0400);  // (0,0): char 0, palette group 1 -> base 4
  f.vram(0x2000, 0x0055);  // pixels 0,2,4,6 = 1, others 0
  f.vram(0x0001, 0x4001);  // (1,0): char 1, hmirror
  f.vram(0x2008, 0x0001);  // char 1 row 0: source pixel 0 colored
  f.vram(0x0002, 0x8002);  // (2,0): char 2, vmirror
  f.vram(0x2010, 0x00FF);  // char 2 rows: row 0 and 1 = color 1
  f.vram(0x2011, 0x00FF);
  f.cgram(5, 0x03E0);      // palette 4 + 1
  f.cgram(1, 0x7FFF);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x03E0));  // group 1 palette
  f.paint(1, 1);
  CHECK(f.ppu.pixelColor(1, 1) == 0x8000);           // transparent pixel
  f.paint(1, 2);
  CHECK(f.ppu.pixelColor(2, 1) == (0x8000 | 0x03E0));

  // hmirror: source pixel 0 lands at display pixel 0 (no bit reversal).
  f.paint(1, 8);
  CHECK(f.ppu.pixelColor(8, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 15);
  CHECK(f.ppu.pixelColor(15, 1) == 0x8000);  // source pixel 7 is transparent

  // vmirror: display row r fetches tile row r^7.
  f.paint(2, 16);
  CHECK(f.ppu.pixelColor(16, 2) == 0x8000);  // row 1 ^ 7 = 6: empty
  f.paint(7, 16);
  CHECK(f.ppu.pixelColor(16, 7) == (0x8000 | 0x7FFF));  // row 6 ^ 7 = 1
}

TEST_CASE("ppu: mode 0 fine hscroll (write-twice) shifts the tile") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.vram(0x0000, 0x0000);
  f.vram(0x2000, 0x00F0);  // pixels 4-7 colored
  f.cgram(1, 0x7FFF);
  f.ppu.writeRegister(0x0D, 0x03);  // HOFS low
  f.ppu.writeRegister(0x0D, 0x00);  // HOFS high -> 3
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == 0x8000);  // tile pixel 3: transparent
  f.paint(1, 1);
  CHECK(f.ppu.pixelColor(1, 1) == (0x8000 | 0x7FFF));  // tile pixel 4
  f.paint(1, 4);
  CHECK(f.ppu.pixelColor(4, 1) == (0x8000 | 0x7FFF));  // tile pixel 7
  f.paint(1, 5);
  CHECK(f.ppu.pixelColor(5, 1) == 0x8000);  // next tile (char 1, empty)
}

TEST_CASE("ppu: mode 0 16x16 tiles (char+16) and vscroll row select") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x10);  // mode 0, BG1 16x16
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.vram(0x0000, 0x0000);  // (0,0): char 0 -> rows 0-7 char 0, 8-15 char 16
  f.vram(0x0020, 0x0000);  // (0,1): char 0 again
  for (int row = 0; row < 8; row++) f.vram(0x2000 + row, 0x00FF);
  for (int row = 0; row < 8; row++) f.vram(0x2080 + row, 0xFF00);  // char 16
  f.cgram(1, 0x7FFF);
  f.cgram(2, 0x7C00);
  f.show();
  // The 16x16 boundary sits at tile row 8, so this case uses vscroll 0:
  // line 8 then fetches row 8 (char 0's second half = char 16).
  f.ppu.writeRegister(0x0E, 0x00);
  f.ppu.writeRegister(0x0E, 0x00);

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));   // char 0, color 1
  f.paint(8, 0);
  CHECK(f.ppu.pixelColor(0, 8) == (0x8000 | 0x7C00));   // char 16, color 2
  f.paint(9, 0);
  CHECK(f.ppu.pixelColor(0, 9) == (0x8000 | 0x7C00));
  f.paint(16, 0);
  CHECK(f.ppu.pixelColor(0, 16) == (0x8000 | 0x7FFF));  // map row 1 wraps back
}

TEST_CASE("ppu: mode 0 16x16 vscroll selects the second half") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x10);  // mode 0, BG1 16x16
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  // vscroll 8: line 1 fetches tile row 9 -> (0,0)'s second half = char 18.
  f.ppu.writeRegister(0x0E, 0x08);
  f.ppu.writeRegister(0x0E, 0x00);
  f.vram(0x0000, 0x0402);  // (0,0): char 2, group 1 -> palette 4
  for (int row = 0; row < 8; row++) f.vram(0x2090 + row, 0x00FF);  // char 18
  f.cgram(1, 0x7FFF);
  f.cgram(5, 0x03E0);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x03E0));
}

TEST_CASE("ppu: mode 1 4bpp tile, cross-layer priority, TM gating") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x01);  // mode 1
  f.ppu.writeRegister(0x07, 0x00);  // BG1SC base 0
  f.ppu.writeRegister(0x08, 0x08);  // BG2SC base 0x0800
  f.ppu.writeRegister(0x0B, 0x22);  // BG1/BG2 tile base 0x2000
  f.ppu.writeRegister(0x2C, 0x03);  // TM: BG1 + BG2
  f.vram(0x0000, 0x2000);  // BG1 (0,0): char 0, prio 1 -> priority 9
  for (int row = 0; row < 8; row++) {
    f.vram(0x2000 + row, 0x00FF);  // planes 0-1 of row
    f.vram(0x2008 + row, 0x0000);  // planes 2-3 empty
  }
  f.vram(0x0800, 0x0400);  // BG2 (0,0): char 0, group 1, prio 0 -> priority 5
  f.cgram(1, 0x7FFF);
  f.cgram(17, 0x7C00);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));  // BG1 wins
  f.ppu.writeRegister(0x2C, 0x02);  // BG1 off: BG2 shows through
  f.paint(2, 0);
  CHECK(f.ppu.pixelColor(0, 2) == (0x8000 | 0x7C00));
  f.ppu.writeRegister(0x2C, 0x00);  // everything off: backdrop
  f.paint(3, 0);
  CHECK(f.ppu.pixelColor(0, 3) == 0x8000);
}

TEST_CASE("ppu: mode 0 BG4 with its palette offset (id << 5)") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);
  f.ppu.writeRegister(0x0A, 0x00);  // BG4SC base 0
  f.ppu.writeRegister(0x0C, 0x20);  // BG4 tile base 0x2000
  f.ppu.writeRegister(0x2C, 0x08);  // TM: BG4
  f.vram(0x0000, 0x0000);  // (0,0): char 0, group 0 -> palette 96
  f.vram(0x0001, 0x0400);  // (1,0): char 1, group 1 -> palette 100
  f.vram(0x2000, 0x00FF);
  f.vram(0x2008, 0x00FF);
  f.cgram(97, 0x7FFF);
  f.cgram(101, 0x03E0);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 8);
  CHECK(f.ppu.pixelColor(8, 1) == (0x8000 | 0x03E0));
}

TEST_CASE("ppu: mode 2 offset-per-tile (hlookup one-column lag, vlookup)") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x02);  // mode 2
  f.ppu.writeRegister(0x07, 0x00);  // BG1SC
  f.ppu.writeRegister(0x08, 0x08);  // BG2SC at 0x0800
  f.ppu.writeRegister(0x09, 0x10);  // BG3SC (offset table) at 0x1000
  f.ppu.writeRegister(0x0B, 0x22);  // BG1/BG2 tile base 0x2000
  f.ppu.writeRegister(0x2C, 0x03);  // TM: BG1 + BG2
  // BG1 map: char c, group c (colors 1, 17, 33, 49).
  f.vram(0x0000, 0x0000);
  f.vram(0x0001, 0x0401);
  f.vram(0x0002, 0x0802);
  f.vram(0x0003, 0x0C03);
  // BG2 map: (0,0) char 0; the vlookup moves the column-1 fetch to tile row
  // 32 (page-relative), i.e. map row 4 -> entry (1,4) char 4 with prio bit.
  f.vram(0x0800, 0x0000);
  f.vram(0x0881, 0x2004);
  // BG3 offset table. vscroll -1 maps the y==0 (H) fetch to offset row 31
  // and the y==8 (V) fetch to row 0.
  // col 0: no hlookup; vlookup = 31 (bit 14 = valid for BG2) -> row 32.
  f.vram(0x13E0, 0x0000);
  f.vram(0x1000, 0x401F);
  // col 1: hlookup = 8 (bit 13 = valid for BG1); cols 2+ off.
  f.vram(0x13E1, 0x2008);
  f.vram(0x1001, 0x0000);
  f.vram(0x13E2, 0x0000);
  f.vram(0x1002, 0x0000);
  // 4bpp tiles: char c at 0x2000 + 32c, rows at c*32+r for planes 0-1 and
  // c*32+8+r for planes 2-3.
  for (int c = 0; c <= 4; c++) {
    f.vram(0x2000 + 32 * c, 0x00FF);
    f.vram(0x2008 + 32 * c, 0x0000);
  }
  f.cgram(1, 0x7FFF);   // char 0
  f.cgram(17, 0x03E0);  // char 1
  f.cgram(33, 0x7C00);  // char 2
  f.cgram(49, 0x001F);  // char 3
  f.cgram(65, 0xFFFF);  // char 4 (BG2)
  f.show();

  // hlookup applies one column late (the fetch for column c uses the entry
  // loaded during column c-1): display = [0, 1, 3, 3, ...].
  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 8);
  // Column 1: BG1 fetches char 1 (prio 9), but the vlookup moved BG2's
  // fetch to char 4 (prio 5), which wins the whole column.
  CHECK(f.ppu.pixelColor(8, 1) == (0x8000 | 0xFFFF));
  f.paint(1, 9);
  CHECK(f.ppu.pixelColor(9, 1) == (0x8000 | 0xFFFF));
  f.paint(1, 16);
  CHECK(f.ppu.pixelColor(16, 1) == (0x8000 | 0x001F));
  f.paint(1, 24);
  CHECK(f.ppu.pixelColor(24, 1) == (0x8000 | 0x001F));
}

TEST_CASE("ppu: mode 3 8bpp tile and CG direct color") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x03);  // mode 3
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.vram(0x0000, 0x0000);
  for (int row = 0; row < 8; row++) {
    f.vram(0x2000 + row, 0xFFFF);  // planes 0-1, plane 4 = 1
    f.vram(0x2008 + row, 0xFFFF);  // planes 2-3
    f.vram(0x2010 + row, 0x0000);  // planes 4-5
    f.vram(0x2018 + row, 0xFF00);  // plane 6 = 0, plane 7 = 1 -> 0x8F
  }
  f.cgram(0x8F, 0x001F);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x001F));

  // CGWSEL direct color (mode 3): 0x8F -> 0x409C (BBGGGRRR expansion).
  f.ppu.writeRegister(0x30, 0x01);
  f.paint(2, 0);
  CHECK(f.ppu.pixelColor(0, 2) == (0x8000 | 0x409C));
}

TEST_CASE("ppu: mode 4 8bpp BG1 renders 256-color tiles at stride 128") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x04);  // mode 4
  f.ppu.writeRegister(0x07, 0x00);  // BG1SC
  f.ppu.writeRegister(0x09, 0x10);  // BG3SC (offset table) at 0x1000
  f.ppu.writeRegister(0x0B, 0x02);  // BG1 tile base 0x2000
  f.ppu.writeRegister(0x2C, 0x01);  // TM: BG1
  f.vram(0x0000, 0x0000);  // (0,0): char 0
  f.vram(0x0001, 0x0001);  // (1,0): char 1
  // 8bpp chars: char c at 0x2000 + 128*c (mode 4 stride = 1 << (3+4)).
  f.tile8(0x2000, 0x8F);  // char 0 -> color 0x8F
  f.tile8(0x2080, 0x11);  // char 1 -> color 0x11
  f.cgram(0x8F, 0x001F);
  f.cgram(0x11, 0x7C00);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x001F));  // char 0 -> 0x8F
  f.paint(1, 8);
  CHECK(f.ppu.pixelColor(8, 1) == (0x8000 | 0x7C00));  // char 1 -> 0x11 (stride 128)
}

TEST_CASE("ppu: mode 4 offset-per-tile horizontal (bit 15=0, one-column lag)") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x04);  // mode 4
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x09, 0x10);  // BG3SC (offset table) at 0x1000
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  // BG1 map: col c -> char c (distinct colors 0x11, 0x22, 0x33, 0x44).
  f.vram(0x0000, 0x0000);
  f.vram(0x0001, 0x0001);
  f.vram(0x0002, 0x0002);
  f.vram(0x0003, 0x0003);
  // BG3 offset table. With vscroll -1 the hlookup fetch (y==0) reads offset
  // row 31 (0x13E0+c). Mode 4: bit 15 = 0 (horizontal), bit 13 = BG1 valid.
  f.vram(0x13E0, 0x0000);  // col 0: no offset
  f.vram(0x13E1, 0x2008);  // col 1: hoffset 8 -> applied one column late
  f.vram(0x13E2, 0x0000);  // col 2: no offset
  f.tile8(0x2000, 0x11);  // char 0
  f.tile8(0x2080, 0x22);  // char 1
  f.tile8(0x2100, 0x33);  // char 2
  f.tile8(0x2180, 0x44);  // char 3
  f.cgram(0x11, 0x7FFF);
  f.cgram(0x22, 0x03E0);
  f.cgram(0x33, 0x7C00);
  f.cgram(0x44, 0x001F);
  f.show();

  // One-column lag: the col-1 entry (offset 8) applies to col 2, so the
  // display is [0, 1, 3, 3, ...].
  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));   // char 0
  f.paint(1, 8);
  CHECK(f.ppu.pixelColor(8, 1) == (0x8000 | 0x03E0));   // char 1
  f.paint(1, 16);
  CHECK(f.ppu.pixelColor(16, 1) == (0x8000 | 0x001F));  // char 3 (offset 8)
  f.paint(1, 24);
  CHECK(f.ppu.pixelColor(24, 1) == (0x8000 | 0x001F));  // char 3
}

TEST_CASE("ppu: mode 5 hires renders two half-pixels per dot") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x05);  // mode 5
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.ppu.writeRegister(0x2D, 0x01);  // hires: even half-pixels use the sub screen
  f.vram(0x0000, 0x0000);
  f.vram(0x2000, 0xF00F);  // plane0 = 0x0F, plane1 = 0xF0 -> [1,1,1,1,2,2,2,2]
  f.vram(0x2008, 0x0000);
  f.vram(0x2100, 0xF00F);  // char 1: en hires el 2º half-tile del cell usa char+1
  f.vram(0x2108, 0x0000);
  f.cgram(1, 0x7FFF);
  f.cgram(2, 0x7C00);
  f.show();

  f.paint(1, 0);
  // Line 1 row 9: half-pixel pair of dot 0: below then above = tile pixel 0.
  CHECK(f.raw(1, 26 + 0) == (0x8000 | 0x7FFF));
  CHECK(f.raw(1, 26 + 1) == (0x8000 | 0x7FFF));
  f.paint(1, 4);
  // Dot 2 renders tile pixels 4-5: half-pixels 4..7.
  CHECK(f.raw(1, 26 + 4) == (0x8000 | 0x7C00));
  CHECK(f.raw(1, 26 + 7) == (0x8000 | 0x7C00));
  // Half-pixel 8 = tile 1 (char 0 again) -> white.
  CHECK(f.raw(1, 26 + 8) == (0x8000 | 0x7FFF));
  // pixelColor reads the left half of each pair.
  CHECK(f.ppu.pixelColor(2, 1) == (0x8000 | 0x7FFF));
  CHECK(f.ppu.pixelColor(4, 1) == (0x8000 | 0x7C00));  // hp4 = primer píxel 2
}

TEST_CASE("ppu: mode 6 hires with offset-per-tile at 16 half-pixel columns") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x06);  // mode 6
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x09, 0x10);  // BG3SC (offsets) at 0x1000
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.ppu.writeRegister(0x2D, 0x01);  // hires: even half-pixels use the sub screen
  f.vram(0x0000, 0x0000);
  f.vram(0x0001, 0x0401);
  f.vram(0x0002, 0x0802);
  // BG3 offset table. With vscroll -1 the hlookup fetches (y==0) land on
  // offset row 31 (0x13E0+c) and the vlookup fetches (y==8) on row 0
  // (0x1000+c), same layout as the mode 2 test. All-zero: the three map
  // columns fetch straight, every 16 half-pixels (one cell).
  for (int c = 0; c <= 2; c++) {
    f.vram(0x1000 + c, 0x0000);  // V row: no vertical offsets
    f.vram(0x13E0 + c, 0x0000);  // H row: no horizontal offsets
  }
  for (int c = 0; c <= 2; c++) {
    f.vram(0x2000 + 0x200 * c, 0x00FF);
    f.vram(0x2008 + 0x200 * c, 0x0000);
  }
  f.cgram(1, 0x7FFF);
  f.cgram(17, 0x03E0);
  f.cgram(33, 0x7C00);
  f.show();

  f.paint(1, 16);
  // One-column lag again: cols -> [0, 1, 2] in 16 half-pixels.
  CHECK(f.raw(1, 26 + 0) == (0x8000 | 0x7FFF));
  CHECK(f.raw(1, 26 + 16) == (0x8000 | 0x03E0));
  CHECK(f.raw(1, 26 + 32) == (0x8000 | 0x7C00));
}

TEST_CASE("ppu: pseudoHires renders main/sub as half-pixel pairs") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);  // mode 0 (non-hires)
  f.ppu.writeRegister(0x33, 0x08);  // SETINI bit 3: pseudo hires
  f.ppu.writeRegister(0x07, 0x00);  // BG1SC
  f.ppu.writeRegister(0x08, 0x08);  // BG2SC
  f.ppu.writeRegister(0x0B, 0x22);  // BG1/BG2 tile base 0x2000
  f.ppu.writeRegister(0x0E, 0xFF); f.ppu.writeRegister(0x0E, 0x03);  // BG1 vscroll -1
  f.ppu.writeRegister(0x10, 0xFF); f.ppu.writeRegister(0x10, 0x03);  // BG2 vscroll -1
  f.ppu.writeRegister(0x2C, 0x01);  // TM: BG1 main
  f.ppu.writeRegister(0x2D, 0x02);  // TS: BG2 sub
  f.vram(0x0000, 0x0000);  // BG1 map (0,0): char 0
  f.vram(0x0800, 0x0000);  // BG2 map (0,0): char 0
  f.vram(0x2000, 0x00FF);  // char 0 row 0: color 1 (shared)
  f.cgram(1, 0x7FFF);      // BG1 color 1 = white
  f.cgram(33, 0x001F);     // BG2 color 1 = red (palette 32)
  f.show();

  f.paint(1, 0);
  // Even half-pixel = sub screen (BG2 red), odd half-pixel = main (BG1 white).
  CHECK(f.raw(1, 26 + 0) == (0x8000 | 0x001F));
  CHECK(f.raw(1, 26 + 1) == (0x8000 | 0x7FFF));
}

TEST_CASE("ppu: mosaic repeats horizontal blocks and the block-top row") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);
  f.ppu.writeRegister(0x06, 0x31);  // size 4, BG1 mosaic on
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.vram(0x0000, 0x0000);
  f.vram(0x2000, 0x1011);  // row 0: pixels 0 = 1, 4 = 3
  f.vram(0x2001, 0x0000);  // rows 1-3 empty
  f.vram(0x2002, 0x0000);
  f.vram(0x2003, 0x0000);
  f.vram(0x2004, 0x1100);  // row 4: pixels 0, 4 = 2
  f.cgram(1, 0x7FFF);
  f.cgram(3, 0x7C00);
  f.cgram(2, 0x03E0);
  f.show();

  // Horizontal: blocks of 4 pixels repeat pixel 0 and pixel 4.
  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 3);
  CHECK(f.ppu.pixelColor(3, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 4);
  CHECK(f.ppu.pixelColor(4, 1) == (0x8000 | 0x7C00));
  f.paint(1, 7);
  CHECK(f.ppu.pixelColor(7, 1) == (0x8000 | 0x7C00));
  f.paint(1, 8);
  CHECK(f.ppu.pixelColor(8, 1) == (0x8000 | 0x7FFF));  // next tile
  // Vertical: block 0 = lines 1-4 all fetch row 0; block 1 = lines 5-8 row 4.
  f.paint(2, 0);
  CHECK(f.ppu.pixelColor(0, 2) == (0x8000 | 0x7FFF));
  f.paint(5, 0);
  CHECK(f.ppu.pixelColor(0, 5) == (0x8000 | 0x03E0));
  f.paint(6, 4);
  CHECK(f.ppu.pixelColor(4, 6) == (0x8000 | 0x03E0));
}

TEST_CASE("ppu: INIDISP forced blank and brightness flag") {
  BgFixture f;
  f.ppu.writeRegister(0x05, 0x00);
  f.ppu.writeRegister(0x07, 0x00);
  f.ppu.writeRegister(0x0B, 0x02);
  f.ppu.writeRegister(0x2C, 0x01);
  f.vram(0x0000, 0x0000);
  f.vram(0x2000, 0x00FF);
  f.cgram(1, 0x7FFF);
  f.show();

  f.paint(1, 0);
  CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));

  f.ppu.writeRegister(0x00, 0x80);  // forced blank: raw black, no flag
  f.paint(1, 1);
  CHECK(f.ppu.pixelColor(1, 1) == 0x0000);

  f.ppu.writeRegister(0x00, 0x00);  // brightness 0: no flag, color shows
  f.paint(1, 2);
  CHECK(f.ppu.pixelColor(2, 1) == 0x7FFF);

  f.ppu.writeRegister(0x00, 0x0F);
  f.ppu.writeRegister(0x2C, 0x00);  // backdrop color
  f.cgram(0, 0x001F);
  f.paint(1, 3);
  CHECK(f.ppu.pixelColor(3, 1) == (0x8000 | 0x001F));
}

}  // namespace snes