#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// Phase 4 sprites/OAM: SpriteEngine probe/draw/loadTiles (port of the ares
// PPU object engine) driven through the per-dot cadence of timing.cpp.
// Facts follow fullsnes (noSns v1.6); ordering follows the ares PPU:
//  - probe() range-checks 128 sprites every 2 dots (H=0..254), filling up
//    to 32 items per line (a 33rd touching sprite sets range overflow).
//  - loadTiles() (H=270) fetches the item tiles into the other buffer,
//    reverse item order, up to 34 tiles per line (time overflow beyond).
//  - draw() consumes the list fetched on the PREVIOUS line (hardware
//    one-line sprite delay): a sprite at y covers display lines y+1..y+h.
//  - lineStart() at V=225 reloads the OAM address from $2102/$2103.
constexpr uint64 kLine = 341 * 4;

struct SpriteFixture {
  Ppu ppu;
  SpriteFixture() {
    ppu.power();
    // Linear VRAM access: increment 1, no translation, increment on the
    // high byte ($2119) so a VMDATAL+VMDATAH pair stays one 16-bit word.
    ppu.writeRegister(0x15, 0x80);
    // Power-on OAM is all-zero (every sprite at x=0,y=0), which would make
    // 128 sprites touch line 0. Park every sprite below the active display
    // (y=255: never probed, since rendering stops at line 240) so a test
    // only sees the objects it writes itself, and leave the OAM address
    // back at 0.
    for (uint8 i = 0; i < 128; i++) oam(i, 0, 0xFF, 0, 0x00);
    ppu.writeRegister(0x02, 0x00);
    ppu.writeRegister(0x03, 0x00);
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
  uint16 px(int x, int y) const { return ppu.pixelColor(x, y); }

  // Write one OAM object: the 4 low-table bytes via $2104 (write-twice)
  // plus the high-table byte with its X bit9/size pair ($200 + idx/4).
  void oam(uint8 idx, uint16 x, uint8 y, uint8 ch, uint8 attr, bool size = false) {
    ppu.writeRegister(0x02, uint8(idx * 2));
    ppu.writeRegister(0x03, 0x00);
    ppu.writeRegister(0x04, x & 0xFF);
    ppu.writeRegister(0x04, y);
    ppu.writeRegister(0x04, ch);
    ppu.writeRegister(0x04, attr);
    uint8 hi = ppu.oamReadRaw(0x200 + (idx >> 2));
    uint8 shift = (idx & 3) * 2;
    hi = uint8((hi & ~(3u << shift)) | ((x >> 8) << shift) | (uint8(size) << (shift + 1)));
    // $2102 sets an even address (bit 0 is always 0), so an odd high-table
    // byte (idx>>2 odd) is reached by stepping through the even byte first.
    uint8 b = idx >> 2;
    ppu.writeRegister(0x02, uint8((0x200 + (b & ~1)) >> 1));
    ppu.writeRegister(0x03, 0x01);
    if (b & 1) ppu.writeRegister(0x04, ppu.oamReadRaw(0x200 + (b & ~1)));
    ppu.writeRegister(0x04, hi);
  }

  // Fill the sprite tile grid (sizePx square of 8x8 tiles, 4bpp) with
  // color-1 pixels so every pixel of the sprite is opaque.
  void solidTiles(uint8 sizePx) {
    uint8 tiles = sizePx / 8;
    for (uint8 ty = 0; ty < tiles; ty++)
      for (uint8 tx = 0; tx < tiles; tx++) {
        uint16 t = uint16(ty * 16 + tx);
        for (uint8 row = 0; row < 8; row++) {
          vram((t << 4) + row, 0x00FF);        // planes 0/1: all color 1
          vram((t << 4) + row + 8, 0x0000);    // planes 2/3
        }
      }
  }
};

TEST_CASE("ppu: sprite 8x8 renders its palette color with the right span") {
  SpriteFixture f;
  f.ppu.writeRegister(0x01, 0x00);  // OBSEL: 8x8 sizes, tile base 0
  f.ppu.writeRegister(0x2C, 0x10);  // TM: sprites on the main screen
  f.oam(0, 0, 0, 0, 0x00);          // sprite 0: x=0, y=0, char 0
  f.solidTiles(8);
  f.cgram(129, 0x7FFF);             // sprite palette 0, color 1
  f.show();

  // Sprite tiles fetched at line N-1 are drawn on line N, so y=0 is shown
  // on display lines 1..8.
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 7);
  CHECK(f.px(7, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 8);
  CHECK(f.px(8, 1) == 0x8000);  // one column past the sprite: backdrop
  f.paint(8, 0);
  CHECK(f.px(0, 8) == (0x8000 | 0x7FFF));  // last covered line
  f.paint(9, 0);
  CHECK(f.px(0, 9) == 0x8000);
}

TEST_CASE("ppu: sprite hflip mirrors the tile horizontally") {
  // plane0 bit 6 = pixel 1 normally (draw shifts by 7-px); with hflip the
  // same bit paints pixel 6.
  SUBCASE("normal orientation") {
    SpriteFixture f;
    f.ppu.writeRegister(0x01, 0x00);
    f.ppu.writeRegister(0x2C, 0x10);
    f.oam(0, 0, 0, 0, 0x00);
    f.vram(0x0000, 0x0040);
    f.cgram(129, 0x7FFF);
    f.show();
    f.paint(1, 1);
    CHECK(f.px(1, 1) == (0x8000 | 0x7FFF));
    f.paint(1, 6);
    CHECK(f.px(6, 1) == 0x8000);
  }
  SUBCASE("hflip") {
    SpriteFixture f;
    f.ppu.writeRegister(0x01, 0x00);
    f.ppu.writeRegister(0x2C, 0x10);
    f.oam(0, 0, 0, 0, 0x40);  // hflip bit
    f.vram(0x0000, 0x0040);
    f.cgram(129, 0x7FFF);
    f.show();
    f.paint(1, 1);
    CHECK(f.px(1, 1) == 0x8000);
    f.paint(1, 6);
    CHECK(f.px(6, 1) == (0x8000 | 0x7FFF));
  }
}

TEST_CASE("ppu: sprite vflip mirrors the tile vertically") {
  // Only tile row 0 is colored; vflip shows it at the bottom of the sprite
  // (row 7), i.e. on display line 8 instead of line 1.
  SpriteFixture f;
  f.ppu.writeRegister(0x01, 0x00);
  f.ppu.writeRegister(0x2C, 0x10);
  f.oam(0, 0, 0, 0, 0x80);  // vflip bit
  f.vram(0x0000, 0x00FF);   // row 0 colored; rows 1..7 empty
  f.cgram(129, 0x7FFF);
  f.show();
  f.paint(1, 0);
  CHECK(f.px(0, 1) == 0x8000);
  f.paint(7, 0);
  CHECK(f.px(0, 7) == 0x8000);
  f.paint(8, 0);
  CHECK(f.px(0, 8) == (0x8000 | 0x7FFF));
}

TEST_CASE("ppu: sprite nameselect selects an alternate 4KB tile base") {
  // $2101 bits 3-4 pick the alternate name base (+(1+nameselect)*0x1000);
  // the OAM attribute bit 0 (nameselect) makes a sprite use it.
  SpriteFixture f;
  f.ppu.writeRegister(0x01, 0x08);
  f.ppu.writeRegister(0x2C, 0x10);
  f.oam(0, 0, 0, 0, 0x01);  // nameselect bit
  f.vram(0x2000, 0x00FF);  // only the alternate base has data
  f.cgram(129, 0x7FFF);
  f.show();
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));
}

TEST_CASE("ppu: 32 sprite items per line and the 33rd sets range overflow") {
  SUBCASE("33 sprites on the line: range overflow, 33rd not drawn") {
    SpriteFixture f;
    f.ppu.writeRegister(0x01, 0x00);
    f.ppu.writeRegister(0x2C, 0x10);
    for (uint8 i = 0; i < 32; i++) f.oam(i, i * 8, 0, 0, 0x00);
    f.oam(32, 248, 0, 0, 0x02);  // 33rd touching sprite, palette 1
    f.solidTiles(8);
    f.cgram(129, 0x7FFF);
    f.cgram(145, 0x03E0);        // palette 1 (would mark sprite 32)
    f.show();
    f.paint(1, 0);
    CHECK(f.ppu.statRangeOver() == true);
    // The 33rd sprite is dropped from the item list: x=248 still shows
    // sprite 31 (palette 0), not sprite 32.
    f.paint(1, 248);
    CHECK(f.px(248, 1) == (0x8000 | 0x7FFF));
  }
  SUBCASE("32 sprites on the line: no overflow") {
    SpriteFixture f;
    f.ppu.writeRegister(0x01, 0x00);
    f.ppu.writeRegister(0x2C, 0x10);
    for (uint8 i = 0; i < 32; i++) f.oam(i, i * 8, 0, 0, 0x00);
    f.solidTiles(8);
    f.cgram(129, 0x7FFF);
    f.show();
    f.paint(1, 0);
    CHECK(f.ppu.statRangeOver() == false);
    f.paint(1, 248);
    CHECK(f.px(248, 1) == (0x8000 | 0x7FFF));
  }
}

TEST_CASE("ppu: sprite display priorities against BG1 and OAM overlap order") {
  SUBCASE("sprite priority 0 hides behind BG1 (mode 0: 3 vs 8)") {
    SpriteFixture f;
    f.ppu.writeRegister(0x05, 0x00);  // mode 0
    f.ppu.writeRegister(0x07, 0x04);  // BG1SC = 1: map at 0x0400 (clear of the sprite tile base)
    f.ppu.writeRegister(0x0B, 0x02);  // BG1 tile base 0x2000
    f.ppu.writeRegister(0x0E, 0xFF);  // BG1 vscroll -1: line 1 = map row 0
    f.ppu.writeRegister(0x0E, 0x03);
    f.ppu.writeRegister(0x2C, 0x11);  // TM: BG1 + sprites
    f.vram(0x0400, 0x0000);
    f.vram(0x2000, 0x00FF);
    f.oam(0, 0, 0, 0, 0x00);          // sprite priority 0
    f.solidTiles(8);
    f.cgram(1, 0x001F);
    f.cgram(129, 0x7FFF);
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x001F));  // BG1 wins
  }
  SUBCASE("sprite priority 3 draws over BG1 (mode 0: 12 vs 8)") {
    SpriteFixture f;
    f.ppu.writeRegister(0x05, 0x00);
    f.ppu.writeRegister(0x07, 0x04);
    f.ppu.writeRegister(0x0B, 0x02);
    f.ppu.writeRegister(0x0E, 0xFF);
    f.ppu.writeRegister(0x0E, 0x03);
    f.ppu.writeRegister(0x2C, 0x11);
    f.vram(0x0400, 0x0000);
    f.vram(0x2000, 0x00FF);
    f.oam(0, 0, 0, 0, 0x30);          // sprite priority 3
    f.solidTiles(8);
    f.cgram(1, 0x001F);
    f.cgram(129, 0x7FFF);
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));  // sprite wins
  }
  SUBCASE("two same-priority sprites: the lower OAM index wins") {
    SpriteFixture f;
    f.ppu.writeRegister(0x01, 0x00);
    f.ppu.writeRegister(0x2C, 0x10);
    f.oam(0, 0, 0, 0, 0x00);  // palette 0
    f.oam(1, 0, 0, 0, 0x02);  // palette 1, same position
    f.solidTiles(8);
    f.cgram(129, 0x7FFF);
    f.cgram(145, 0x03E0);
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));  // sprite 0 on top
  }
}

TEST_CASE("ppu: OAM priority rotation reorders the sprite evaluation") {
  SpriteFixture f;
  f.ppu.writeRegister(0x01, 0x00);
  f.ppu.writeRegister(0x2C, 0x10);
  f.oam(0, 0, 0, 0, 0x00);  // palette 0
  f.oam(1, 0, 0, 0, 0x02);  // palette 1
  f.oam(2, 0, 0, 0, 0x04);  // palette 2
  f.solidTiles(8);
  f.cgram(129, 0x7FFF);
  f.cgram(145, 0x03E0);
  f.cgram(161, 0x7C00);
  f.show();
  // Rotate the priority list: OAMADDH bit7 + base address 8 -> the
  // evaluation starts at sprite 2, which then wins the overlap.
  f.ppu.writeRegister(0x03, 0x80);
  f.ppu.writeRegister(0x02, 0x04);
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x7C00));  // sprite 2 (red)
}

TEST_CASE("ppu: 34 tile budget per line and the time overflow flag") {
  SpriteFixture f;
  f.ppu.writeRegister(0x01, 0x00);
  f.ppu.writeRegister(0x2C, 0x10);
  // 18 16x16 sprites on line 0 = 36 tiles. Items are fetched in reverse
  // order, so the FIRST sprite is the one beyond the 34-tile budget.
  for (uint8 i = 0; i < 16; i++) f.oam(i, i * 16, 0, 0, 0x00, true);
  f.oam(16, 240, 0, 0, 0x00, true);
  f.oam(17, 240, 0, 0, 0x00, true);
  f.solidTiles(16);
  f.cgram(129, 0x7FFF);
  f.show();
  f.paint(1, 0);
  CHECK(f.ppu.statTimeOver() == true);
  CHECK(f.ppu.statRangeOver() == false);
  CHECK(f.px(0, 1) == 0x8000);  // sprite 0 dropped past the budget
  f.paint(1, 16);
  CHECK(f.px(16, 1) == (0x8000 | 0x7FFF));
}

TEST_CASE("ppu: sprite sizes 8x8 .. 64x64 (small and large tables)") {
  static constexpr uint8 smallW[8] = {8, 8, 8, 16, 16, 32, 16, 16};
  static constexpr uint8 largeW[8] = {16, 32, 64, 32, 64, 64, 32, 32};
  static constexpr uint8 smallH[8] = {8, 8, 8, 16, 16, 32, 32, 32};
  static constexpr uint8 largeH[8] = {16, 32, 64, 32, 64, 64, 64, 32};
  for (uint8 bs = 0; bs < 8; bs++) {
    for (bool large : {false, true}) {
      uint8 w = large ? largeW[bs] : smallW[bs];
      uint8 h = large ? largeH[bs] : smallH[bs];
      SpriteFixture f;
      f.ppu.writeRegister(0x01, bs << 5);
      f.ppu.writeRegister(0x2C, 0x10);
      f.oam(0, 0, 0, 0, 0x00, large);
      f.solidTiles(h);  // tile grid must cover the full sprite height
      f.cgram(129, 0x7FFF);
      f.show();
      f.paint(1, 0);
      CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));
      f.paint(1, w - 1);
      CHECK(f.px(w - 1, 1) == (0x8000 | 0x7FFF));
      f.paint(1, w);
      CHECK(f.px(w, 1) == 0x8000);
      f.paint(h, 0);
      CHECK(f.px(0, h) == (0x8000 | 0x7FFF));
      f.paint(h + 1, 0);
      CHECK(f.px(0, h + 1) == 0x8000);
    }
  }
}

TEST_CASE("ppu: sprite interlace halves the small base-size 6/7 height") {
  // SETINI bit1: small sprites with OBSEL base size 6/7 are half-height
  // (16 instead of 32), and the line test halves again.
  SpriteFixture f;
  f.ppu.writeRegister(0x01, 6 << 5);
  f.ppu.writeRegister(0x33, 0x02);  // SETINI bit1: sprite interlace
  f.ppu.writeRegister(0x2C, 0x10);
  f.oam(0, 0, 0, 0, 0x00);
  f.solidTiles(16);
  f.cgram(129, 0x7FFF);
  f.show();
  f.paint(8, 0);
  CHECK(f.px(0, 8) == (0x8000 | 0x7FFF));  // 8 covered lines
  f.paint(9, 0);
  CHECK(f.px(0, 9) == 0x8000);
}

TEST_CASE("ppu: sprite OAM address auto-resets at V=225") {
  SpriteFixture f;
  f.ppu.writeRegister(0x02, 0x28);  // OAMADDL: base address 0x50
  CHECK(f.ppu.oamAddress() == 0x50);
  // Advance the address via OAMDATAREAD without touching the base.
  f.ppu.readRegister(0x38);
  f.ppu.readRegister(0x38);
  f.ppu.readRegister(0x38);
  CHECK(f.ppu.oamAddress() == 0x53);
  // Mid-display the address keeps advancing; the lineStart at V=225
  // reloads it from the base.
  f.show();
  f.ppu.step(100 * kLine);
  CHECK(f.ppu.oamAddress() == 0x53);
  f.ppu.step(125 * kLine);  // now at V=225, H=0
  CHECK(f.ppu.oamAddress() == 0x50);
  f.ppu.step(1 * kLine);
  CHECK(f.ppu.oamAddress() == 0x50);
}

}  // namespace snes