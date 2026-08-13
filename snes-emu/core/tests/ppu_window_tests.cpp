#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// Phase 4 windows: $2123-$212B mask BG/OBJ display (TMW/TSW) and gate color
// math (CGWSEL math window). Facts follow fullsnes (noSns v1.6); the invert/
// mask decode and the two-window merge follow the ares PPU (window.cpp).
//
// $2123/$2124/$2125 encode each layer's two windows as 2-bit fields:
//   bit0 = invert (0=inside, 1=outside), bit1 = enable (1=active).
//   So value 2 = "inside", value 3 = "outside", 0/1 = disabled.
// $212A/$212B merge window 1 and 2 per layer (0=OR, 1=AND, 2=XOR, 3=XNOR).
constexpr uint64 kLine = 341 * 4;

struct WindowFixture {
  Ppu ppu;
  WindowFixture() {
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
  // BG1 (mode 0) fills the whole first screen row with color 1 (white).
  void solidBg1() {
    ppu.writeRegister(0x05, 0x00);  // mode 0
    ppu.writeRegister(0x07, 0x00);  // BG1SC
    ppu.writeRegister(0x0B, 0x02);  // BG1 tile base 0x2000
    ppu.writeRegister(0x2C, 0x01);  // TM: BG1
    ppu.writeRegister(0x0E, 0xFF);  // BG1 vscroll -1: line 1 = map row 0
    ppu.writeRegister(0x0E, 0x03);
    for (int c = 0; c < 32; c++) vram(c, 0x0000);  // map row 0: all char 0
    vram(0x2000, 0x00FF);  // char 0 row 0: color 1
    cgram(1, 0x7FFF);
  }
};

TEST_CASE("ppu: window 1 masks BG1 inside/outside (invert bit)") {
  SUBCASE("inside (value 2)") {
    WindowFixture f;
    f.solidBg1();
    f.ppu.writeRegister(0x26, 10);  // WH0
    f.ppu.writeRegister(0x27, 40);  // WH1
    f.ppu.writeRegister(0x23, 0x02);  // BG1 win1 = inside
    f.ppu.writeRegister(0x2E, 0x01);  // TMW: disable BG1 inside the window
    f.show();

    f.paint(1, 9);
    CHECK(f.px(9, 1) == (0x8000 | 0x7FFF));   // left of window: BG1
    f.paint(1, 10);
    CHECK(f.px(10, 1) == 0x8000);             // inside: hidden
    f.paint(1, 40);
    CHECK(f.px(40, 1) == 0x8000);             // inside edge: hidden
    f.paint(1, 41);
    CHECK(f.px(41, 1) == (0x8000 | 0x7FFF));  // right of window: BG1
  }
  SUBCASE("outside (value 3)") {
    WindowFixture f;
    f.solidBg1();
    f.ppu.writeRegister(0x26, 10);
    f.ppu.writeRegister(0x27, 40);
    f.ppu.writeRegister(0x23, 0x03);  // BG1 win1 = outside (inverted)
    f.ppu.writeRegister(0x2E, 0x01);
    f.show();

    f.paint(1, 9);
    CHECK(f.px(9, 1) == 0x8000);               // outside: hidden
    f.paint(1, 10);
    CHECK(f.px(10, 1) == (0x8000 | 0x7FFF));  // inside: BG1
    f.paint(1, 40);
    CHECK(f.px(40, 1) == (0x8000 | 0x7FFF));
    f.paint(1, 41);
    CHECK(f.px(41, 1) == 0x8000);
  }
}

TEST_CASE("ppu: window 1/2 mask logic (OR/AND/XOR/XNOR) over BG1") {
  // Window 1 = [10,40], window 2 = [30,60], both "inside".
  // Expected visibility (1 = BG1 visible, 0 = hidden) at x = 5,20,35,50,70.
  static constexpr uint8 expected[4][5] = {
      // x=5   x=20  x=35  x=50  x=70
      {1, 0, 0, 0, 1},  // OR:   hidden in [10,60]
      {1, 1, 0, 1, 1},  // AND:  hidden in [30,40]
      {1, 0, 1, 0, 1},  // XOR:  hidden in [10,29] and [41,60]
      {0, 1, 0, 1, 0},  // XNOR: hidden in [0,9], [30,40], [61,255]
  };
  static constexpr int xs[5] = {5, 20, 35, 50, 70};
  for (uint8 mask = 0; mask < 4; mask++) {
    WindowFixture f;
    f.solidBg1();
    f.ppu.writeRegister(0x26, 10);
    f.ppu.writeRegister(0x27, 40);
    f.ppu.writeRegister(0x28, 30);
    f.ppu.writeRegister(0x29, 60);
    f.ppu.writeRegister(0x23, 0x0A);  // BG1 win1 = win2 = inside (2)
    f.ppu.writeRegister(0x2E, 0x01);  // TMW: disable BG1 in the window
    f.ppu.writeRegister(0x2A, mask);  // WBGLOG: BG1 mask
    f.show();

    for (int i = 0; i < 5; i++) {
      f.paint(1, xs[i]);
      INFO("mask=", mask, " x=", xs[i]);
      CHECK(f.px(xs[i], 1) == (expected[mask][i] ? (0x8000 | 0x7FFF) : 0x8000));
    }
  }
}

TEST_CASE("ppu: color window gates math but display window (TMW) hides the layer") {
  // Backdrop = blue distinguishes the two: TMW hides BG1 to the blue
  // backdrop, while "force main screen black" keeps BG1 but paints black.
  SUBCASE("display window (TMW) hides BG1 to the backdrop color") {
    WindowFixture f;
    f.solidBg1();
    f.cgram(0, 0x001F);  // backdrop = blue
    f.ppu.writeRegister(0x26, 10);
    f.ppu.writeRegister(0x27, 40);
    f.ppu.writeRegister(0x23, 0x02);  // BG1 win1 = inside
    f.ppu.writeRegister(0x2E, 0x01);  // TMW
    f.show();

    f.paint(1, 5);
    CHECK(f.px(5, 1) == (0x8000 | 0x7FFF));   // outside: BG1 white
    f.paint(1, 10);
    CHECK(f.px(10, 1) == (0x8000 | 0x001F));  // inside: blue backdrop
  }
  SUBCASE("color window (force main screen black) keeps BG1 but paints black") {
    WindowFixture f;
    f.solidBg1();
    f.cgram(0, 0x001F);  // backdrop = blue (must NOT show)
    f.ppu.writeRegister(0x26, 10);
    f.ppu.writeRegister(0x27, 40);
    f.ppu.writeRegister(0x25, 0x20);  // MATH win1 = inside (bits 4-5 = 2)
    f.ppu.writeRegister(0x30, 0x80);  // CGWSEL: force black = MathWindow (bits 6-7 = 2)
    f.show();

    f.paint(1, 5);
    CHECK(f.px(5, 1) == (0x8000 | 0x7FFF));   // outside: BG1 white (not hidden)
    f.paint(1, 10);
    CHECK(f.px(10, 1) == 0x8000);             // inside: forced black, not blue
  }
}

}  // namespace snes
