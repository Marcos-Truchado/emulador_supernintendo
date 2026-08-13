#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// Phase 4 color math: CGWSEL/CGADSUB/COLDATA ($2130-$2132) add/subtract and
// half-color on the front-most main/sub pixels. Facts follow fullsnes (noSns
// v1.6); the blend math follows the ares PPU (dac.cpp blend()).
constexpr uint64 kLine = 341 * 4;

struct ColorMathFixture {
  Ppu ppu;
  ColorMathFixture() {
    ppu.power();
    ppu.writeRegister(0x15, 0x80);  // linear VRAM access
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
  void paint(int y, int x) {
    uint64 target = uint64(y) * kLine + uint64(14 + x) * 4;
    uint64 current = (uint64(ppu.scanline()) * 341 + ppu.dot()) * 4;
    ppu.step(target - current);
  }
  uint16 px(int x, int y) const { return ppu.pixelColor(x, y); }
  // BG1 on the main screen (palette 0 -> CGRAM[1]) and BG2 on the sub screen
  // (palette 32 -> CGRAM[33]); both render char 0 = color 1.
  void bgMainSub(uint16 mainColor, uint16 subColor) {
    ppu.writeRegister(0x05, 0x00);  // mode 0
    ppu.writeRegister(0x07, 0x00);  // BG1SC
    ppu.writeRegister(0x08, 0x08);  // BG2SC at 0x0800
    ppu.writeRegister(0x0B, 0x22);  // BG1/BG2 tile base 0x2000
    ppu.writeRegister(0x0E, 0xFF); ppu.writeRegister(0x0E, 0x03);  // BG1 vscroll -1
    ppu.writeRegister(0x10, 0xFF); ppu.writeRegister(0x10, 0x03);  // BG2 vscroll -1
    ppu.writeRegister(0x2C, 0x01);  // TM: BG1 main
    ppu.writeRegister(0x2D, 0x02);  // TS: BG2 sub
    vram(0x0000, 0x0000);  // BG1 map (0,0): char 0
    vram(0x0800, 0x0000);  // BG2 map (0,0): char 0
    vram(0x2000, 0x00FF);  // char 0 row 0: color 1 (shared)
    cgram(1, mainColor);
    cgram(33, subColor);
  }
  // BG1 on the main screen only; the sub screen is the fixed color.
  void bgMainOnly(uint16 mainColor) {
    ppu.writeRegister(0x05, 0x00);
    ppu.writeRegister(0x07, 0x00);
    ppu.writeRegister(0x0B, 0x02);  // BG1 tile base 0x2000
    ppu.writeRegister(0x0E, 0xFF); ppu.writeRegister(0x0E, 0x03);
    ppu.writeRegister(0x2C, 0x01);  // TM: BG1
    vram(0x0000, 0x0000);
    vram(0x2000, 0x00FF);
    cgram(1, mainColor);
  }
};

TEST_CASE("ppu: color math additive vs subtractive ($2131 bit 7)") {
  SUBCASE("additive (Main + Sub)") {
    ColorMathFixture f;
    f.bgMainSub(0x001F, 0x03E0);  // main red (R=31), sub green (G=31)
    f.ppu.writeRegister(0x30, 0x02);  // CGWSEL bit1: sub screen BG/OBJ enable
    f.ppu.writeRegister(0x31, 0x01);  // CGADSUB: BG1 math, add
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x03FF));  // red + green = yellow
  }
  SUBCASE("subtractive (Main - Sub)") {
    ColorMathFixture f;
    f.bgMainSub(0x001F, 0x03E0);
    f.ppu.writeRegister(0x30, 0x02);
    f.ppu.writeRegister(0x31, 0x81);  // CGADSUB: BG1 math, subtract
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x001F));  // green saturates to 0
  }
}

TEST_CASE("ppu: color math half-color ($2131 bit 6) with fixed color ($2132)") {
  SUBCASE("no halve") {
    ColorMathFixture f;
    f.bgMainOnly(0x001F);  // main red (R=31)
    f.ppu.writeRegister(0x32, 0x9F);  // COLDATA: apply blue, intensity 31
    f.ppu.writeRegister(0x31, 0x01);  // CGADSUB: BG1 math, add, no halve
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x7C1F));  // red + blue (R=31, B=31)
  }
  SUBCASE("halve") {
    ColorMathFixture f;
    f.bgMainOnly(0x001F);
    f.ppu.writeRegister(0x32, 0x9F);  // COLDATA blue 31
    f.ppu.writeRegister(0x31, 0x41);  // CGADSUB: BG1 math, add, halve (bit 6)
    f.show();
    f.paint(1, 0);
    CHECK(f.px(0, 1) == (0x8000 | 0x3C0F));  // (red + blue) / 2 -> R=15, B=15
  }
}

TEST_CASE("ppu: color math on backdrop (main priority 0) uses $2131 bit 5") {
  ColorMathFixture f;
  // No main-screen layer: the main pixel is the backdrop (CGRAM[0]).
  f.cgram(0, 0x001F);               // backdrop red (R=31)
  f.ppu.writeRegister(0x32, 0x5F);  // COLDATA: apply green, intensity 31
  f.show();

  // Backdrop math disabled: raw backdrop.
  f.ppu.writeRegister(0x31, 0x00);
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x001F));  // raw red backdrop

  // Backdrop math enabled (bit 5): red + green = yellow.
  f.ppu.writeRegister(0x31, 0x20);
  f.paint(2, 0);
  CHECK(f.px(0, 2) == (0x8000 | 0x03FF));  // yellow
}

}  // namespace snes
