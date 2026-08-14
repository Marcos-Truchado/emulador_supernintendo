#include "snes/snes.hpp"

#include "apu/apu.hpp"
#include "cpu/cpu65816.hpp"
#include "ppu/ppu.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Loads a ROM + save state and prints the CPU/APU/port state, so we can see
// where the user's saved session was when it hung.
int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <rom.sfc> <state.ss>\n", argv[0]);
    return 2;
  }
  snes::System system;
  std::string error;
  if (!system.load(argv[1], &error)) {
    std::fprintf(stderr, "load: %s\n", error.c_str());
    return 2;
  }
  system.reset();

  std::FILE* f = std::fopen(argv[2], "rb");
  if (!f) { std::fprintf(stderr, "cannot open state\n"); return 2; }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<snes::uint8> data(size > 0 ? size : 0);
  if (size > 0) std::fread(data.data(), 1, data.size(), f);
  std::fclose(f);
  if (!system.loadState(data)) { std::fprintf(stderr, "loadState failed\n"); return 2; }

  auto& cpu = system.cpu();
  auto& apu = system.apu();
  auto ts = cpu.traceState();
  printf("cpu pc=%06x a=%04x x=%04x y=%04x s=%04x d=%04x b=%02x p=%02x e=%d\n",
         ts.pc, ts.a, ts.x, ts.y, ts.s, ts.d, ts.b, ts.p, ts.e ? 1 : 0);
  printf("apu pc=%04x a=%02x x=%02x y=%02x sp=%02x psw=%02x\n",
         apu.pc(), apu.aReg(), apu.xReg(), apu.yReg(), apu.spReg(), apu.pswReg());
  for (int i = 0; i < 4; i++)
    printf("  port $%d ($F%X): cpu->apu=%02x apu->cpu=%02x\n",
           i, 4 + i, apu.inputPort(i), apu.readPort(i));
  printf("dsp: KON=%02x KOFF=%02x FLG=%02x ENDX=%02x DIR=%02x audio=%zu\n",
         apu.dspRegister(0x4C), apu.dspRegister(0x5C), apu.dspRegister(0x6C),
         apu.dspRegister(0x7C), apu.dspRegister(0x5D), apu.audioAvailable());
  printf("frames=%llu\n", (unsigned long long)system.renderedFrames());
  printf("gamemode=%02x\n", system.bus().wram()[0x0100]);

  // Dump the framebuffer + VRAM so we can inspect the saved screen.
  FILE* ppm = std::fopen("state.ppm", "wb");
  if (ppm) {
    std::fprintf(ppm, "P6\n%d %d\n255\n", 256, 224);
    for (int y = 0; y < 224; y++)
      for (int x = 0; x < 256; x++) {
        snes::uint16 v = system.pixelColor(x, y);
        uint8_t rgb[3] = {
          uint8_t((v & 0x1F) << 3 | (v & 0x1F) >> 2),
          uint8_t(((v >> 5) & 0x1F) << 3 | ((v >> 5) & 0x1F) >> 2),
          uint8_t(((v >> 10) & 0x1F) << 3 | ((v >> 10) & 0x1F) >> 2),
        };
        std::fwrite(rgb, 1, 3, ppm);
      }
    std::fclose(ppm);
    printf("wrote state.ppm\n");
  }
  // Run forward 60 frames to get past any fade/transition.
  int frames = getenv("RUN_FRAMES") ? std::atoi(getenv("RUN_FRAMES")) : 0;
  uint64_t target = system.renderedFrames() + frames;
  while (system.renderedFrames() < target) system.step();

  auto& ppu = system.ppu();
  printf("ppu mode=%d\n", ppu.bgMode());
  for (int bg = 0; bg < 4; bg++)
    printf("  bg%d screen=%04x tilebase=%04x hscroll=%03x vscroll=%03x\n",
           bg, ppu.bgScreenAddress(bg), ppu.bgTileBase(bg),
           ppu.bgHScroll(bg), ppu.bgVScroll(bg));
  FILE* vf = std::fopen("vram.bin", "wb");
  if (vf) {
    std::vector<uint8_t> v(0x10000);
    for (int i = 0; i < 0x8000; i++) {
      snes::uint16 w = ppu.vramRead(i);
      v[i * 2] = w & 0xFF; v[i * 2 + 1] = w >> 8;
    }
    std::fwrite(v.data(), 1, v.size(), vf);
    std::fclose(vf);
    printf("wrote vram.bin\n");
  }
  return 0;
}
