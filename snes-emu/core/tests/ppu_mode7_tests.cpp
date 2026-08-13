#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// Phase 4 mode 7: Layer::mode7Draw() rotation/scaling matrix (M7A-D, center,
// offset, flips, screen-over via repeatMode7, EXTBG). Facts follow fullsnes
// (noSns v1.6); the matrix math follows the ares PPU (mode7.cpp).
//
// Mode 7 VRAM: the 128x128 tile map is in the LOW byte of each word; the
// 8bpp tile pixel data is in the HIGH byte. Map entry (tx,ty) lives at word
// (ty<<7|tx); tile T pixel (px,py) lives at word (T<<6 | py<<3 | px).
constexpr uint64 kLine = 341 * 4;

struct Mode7Fixture {
  Ppu ppu;
  Mode7Fixture() {
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
  // Mode 7 write-twice register ($210D/$210E/$211B-$2120).
  void m7reg(uint8 offset, uint16 value) {
    ppu.writeRegister(offset, value & 0xFF);
    ppu.writeRegister(offset, value >> 8);
  }
  // Set the map entry (tx, ty) to `tile` (low byte of the word).
  void m7map(uint8 tx, uint8 ty, uint8 tile) {
    uint16 addr = uint16(uint16(ty) << 7 | tx);
    uint16 cur = ppu.vramRead(addr);
    vram(addr, uint16((cur & 0xFF00) | tile));
  }
  // Set tile `tile` pixel (px, py) to `color` (high byte of the word).
  void m7pixel(uint8 tile, uint8 px, uint8 py, uint8 color) {
    uint16 addr = uint16(uint16(tile) << 6 | (uint16(py) << 3) | px);
    uint16 cur = ppu.vramRead(addr);
    vram(addr, uint16((cur & 0x00FF) | (uint16(color) << 8)));
  }
  // Fill an entire 8x8 tile with one color.
  void m7fill(uint8 tile, uint8 color) {
    for (uint8 py = 0; py < 8; py++)
      for (uint8 px = 0; px < 8; px++) m7pixel(tile, px, py, color);
  }
  // Identity matrix: A=D=1.0, B=C=0, center/offset 0.
  void identity() {
    m7reg(0x1B, 0x0100);  // M7A = 1.0
    m7reg(0x1C, 0x0000);  // M7B = 0
    m7reg(0x1D, 0x0000);  // M7C = 0
    m7reg(0x1E, 0x0100);  // M7D = 1.0
    m7reg(0x1F, 0x0000);  // M7X
    m7reg(0x20, 0x0000);  // M7Y
  }
};

TEST_CASE("ppu: mode 7 identity matrix maps screen 1:1") {
  Mode7Fixture f;
  f.ppu.writeRegister(0x05, 0x07);  // mode 7
  f.ppu.writeRegister(0x2C, 0x01);  // TM: BG1
  f.identity();
  f.m7map(0, 0, 0);  // map (0,0) = tile 0
  f.m7map(1, 0, 1);  // map (1,0) = tile 1
  f.m7fill(0, 1);
  f.m7fill(1, 2);
  f.cgram(1, 0x7FFF);
  f.cgram(2, 0x03E0);
  f.show();

  // Screen (x, 1) -> VRAM (x, 1): pixel 0 -> tile 0, pixel 8 -> tile 1.
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));
  f.paint(1, 8);
  CHECK(f.px(8, 1) == (0x8000 | 0x03E0));
}

TEST_CASE("ppu: mode 7 90-degree rotation maps screen Y to VRAM X") {
  Mode7Fixture f;
  f.ppu.writeRegister(0x05, 0x07);
  f.ppu.writeRegister(0x2C, 0x01);
  // 90-degree rotation: A=0, B=1.0, C=-1.0, D=0 -> screen (x,y) -> VRAM (y,-x).
  f.m7reg(0x1B, 0x0000);  // M7A = 0
  f.m7reg(0x1C, 0x0100);  // M7B = 1.0
  f.m7reg(0x1D, 0xFE00);  // M7C = -1.0
  f.m7reg(0x1E, 0x0000);  // M7D = 0
  f.m7reg(0x1F, 0x0000);
  f.m7reg(0x20, 0x0000);
  f.m7map(0, 0, 0);  // map (0,0) = tile 0
  f.m7map(1, 0, 1);  // map (1,0) = tile 1
  f.m7fill(0, 1);
  f.m7fill(1, 2);
  f.cgram(1, 0x7FFF);
  f.cgram(2, 0x03E0);
  f.show();

  // Column x=0: screen (0, y) -> VRAM (y, 0): y=1 -> tile 0, y=8 -> tile 1.
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x7FFF));
  f.paint(8, 0);
  CHECK(f.px(0, 8) == (0x8000 | 0x03E0));
}

TEST_CASE("ppu: mode 7 screen-over (repeat/wrap vs transparent vs tile 0)") {
  // A=D=8.0: screen (128,2) -> pixelX=1024 (out of bounds), pixelY=16.
  // Correct wrap maps it to tile (0,2); an unmasked tileX reads (0,3).
  for (int mode = 0; mode <= 3; mode++) {
    Mode7Fixture f;
    f.ppu.writeRegister(0x05, 0x07);
    f.ppu.writeRegister(0x2C, 0x01);
    f.m7reg(0x1B, 0x0800);  // M7A = 8.0
    f.m7reg(0x1C, 0x0000);
    f.m7reg(0x1D, 0x0000);
    f.m7reg(0x1E, 0x0800);  // M7D = 8.0
    f.m7reg(0x1F, 0x0000);
    f.m7reg(0x20, 0x0000);
    f.ppu.writeRegister(0x1A, uint8(mode << 6));  // M7SEL: screen over
    f.m7map(0, 2, 0);  // correct wrap target
    f.m7map(0, 3, 1);  // what an unmasked tileX would read
    f.m7fill(0, 1);
    f.m7fill(1, 2);
    f.cgram(1, 0x7FFF);
    f.cgram(2, 0x03E0);
    f.show();

    f.paint(2, 128);
    uint16 want = 0x8000;
    if (mode == 0 || mode == 1) want |= 0x7FFF;  // wrap -> tile 0
    if (mode == 3) want |= 0x7FFF;               // single-tile -> tile 0
    INFO("repeatMode7=", mode);
    CHECK(f.px(128, 2) == want);
  }
}

TEST_CASE("ppu: mode 7 EXTBG (id 1) priority comes from palette bit 7") {
  Mode7Fixture f;
  f.ppu.writeRegister(0x05, 0x07);
  f.ppu.writeRegister(0x33, 0x40);  // SETINI bit 6: EXTBG
  f.ppu.writeRegister(0x2C, 0x03);  // TM: BG1 + BG2
  f.identity();
  f.m7map(0, 0, 0);  // map (0,0) = tile 0
  f.m7fill(0, 0x81);   // every pixel: bit 7 = 1 (BG2 priority 5), color 1
  f.cgram(0x81, 0x001F);     // BG1 would show this if it won (red)
  f.cgram(1, 0x03E0);        // BG2 color 1 (green)
  f.show();

  // BG2 (priority 5) beats BG1 (priority 3): green, not red.
  f.paint(1, 0);
  CHECK(f.px(0, 1) == (0x8000 | 0x03E0));
}

}  // namespace snes
