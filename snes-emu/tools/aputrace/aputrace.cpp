#include "snes/snes.hpp"

#include "apu/apu.hpp"
#include "cpu/cpu65816.hpp"
#include "ppu/ppu.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Diagnostic runner for the SMW APU handshake hang. Loads a ROM, runs it
// headlessly, and detects when the main CPU parks in a wait loop (same PC for
// many consecutive instructions). On detection it dumps the CPU + APU state,
// the four CPU<->APU ports in both directions, and the full 64KB APU RAM to
// "apuram.bin" for offline disassembly.
//
// Usage: snes_aputrace <rom.sfc> [stall_threshold]

namespace {

int globalPeak = 0;

struct TraceEntry {
  snes::uint24 pc;
  snes::uint16 a, x, y, s, d;
  snes::uint8 b, p;
  bool e;
};

constexpr size_t kTraceSize = 4096;
std::vector<TraceEntry> trace(kTraceSize);
size_t traceIndex = 0;

void tracePush(const snes::Cpu65816& cpu) {
  auto ts = cpu.traceState();
  TraceEntry& e = trace[traceIndex % kTraceSize];
  e.pc = ts.pc;
  e.a = ts.a;
  e.x = ts.x;
  e.y = ts.y;
  e.s = ts.s;
  e.d = ts.d;
  e.b = ts.b;
  e.p = ts.p;
  e.e = ts.e;
  traceIndex++;
}

void traceDump(snes::Cpu65816& cpu) {
  fprintf(stderr, "=== last %zu CPU instructions ===\n",
          std::min<size_t>(traceIndex, kTraceSize));
  const size_t start = traceIndex >= kTraceSize ? traceIndex % kTraceSize : 0;
  const size_t n = std::min<size_t>(traceIndex, kTraceSize);
  for (size_t i = 0; i < n; i++) {
    const TraceEntry& e = trace[(start + i) % kTraceSize];
    fprintf(stderr, "%06x: a=%04x x=%04x y=%04x s=%04x d=%04x b=%02x p=%02x e=%d %s\n",
            e.pc, e.a, e.x, e.y, e.s, e.d, e.b, e.p, e.e ? 1 : 0,
            cpu.disassemble(e.pc).c_str());
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <rom.sfc> [stall_threshold]\n", argv[0]);
    return 2;
  }
  uint64_t threshold = 1'000'000ull;
  if (argc >= 3) threshold = std::strtoull(argv[2], nullptr, 10);
  uint64_t maxInstr = 400'000'000ull;
  if (argc >= 4) maxInstr = std::strtoull(argv[3], nullptr, 10);

  snes::System system;
  std::string error;
  if (!system.load(argv[1], &error)) {
    std::fprintf(stderr, "load: %s\n", error.c_str());
    return 2;
  }
  auto& cpu = system.cpu();
  auto& apu = system.apu();
  system.reset();

  uint64_t total = 0;
  const uint64_t kMax = maxInstr;
  snes::uint24 recent[8] = {0};
  int recentCount = 0;
  uint64_t runLen = 0;
  bool startHeld = false;
  uint64_t startAt = getenv("START_AT") ? std::strtoull(getenv("START_AT"), nullptr, 10) : 0;
  uint64_t startEnd = getenv("START_END") ? std::strtoull(getenv("START_END"), nullptr, 10) : 0;

  for (; total < kMax; total++) {
    if (startAt && total >= startAt && total < startEnd && !startHeld) {
      system.setJoypad(0, 0x1000);
      startHeld = true;
    } else if (startEnd && total >= startEnd && startHeld) {
      system.setJoypad(0, 0);
      startHeld = false;
    }

    snes::uint24 pc = cpu.pc();
    bool inRecent = false;
    for (int i = 0; i < recentCount; i++) {
      if (recent[i] == pc) { inRecent = true; break; }
    }
    if (!inRecent) {
      if (recentCount < 8) {
        recent[recentCount++] = pc;
      } else {
        recent[0] = pc;
        recentCount = 1;
        runLen = 0;
      }
    } else {
      runLen++;
    }
    if (runLen >= threshold) break;
    tracePush(cpu);
    system.step();
    if ((total & 0x3FFFF) == 0) {
      snes::int16 ab[2048];
      size_t an = apu.readAudio(ab, 2048);
      for (size_t k = 0; k < an; k++) {
        int v = ab[k] < 0 ? -ab[k] : ab[k];
        if (v > globalPeak) globalPeak = v;
      }
    }
    if ((total & 0xFFFFF) == 0xFFFFF) {
      fprintf(stderr, "progress: %llu instructions, cpu pc=%06x apu pc=%04x KON=%02x ENDX=%02x\n",
              (unsigned long long)total, cpu.pc(), apu.pc(),
              apu.dspRegister(0x4C), apu.dspRegister(0x7C));
      fflush(stderr);
    }
  }

  printf("ran %llu instructions; cpu pc=%06x (run_len=%llu) globalPeak=%d\n",
         (unsigned long long)total, cpu.pc(), (unsigned long long)runLen, globalPeak);
  auto ts = cpu.traceState();
  printf("cpu: a=%04x x=%04x y=%04x s=%04x d=%04x b=%02x p=%02x e=%d\n",
         ts.a, ts.x, ts.y, ts.s, ts.d, ts.b, ts.p, ts.e ? 1 : 0);
  printf("apu: pc=%04x a=%02x x=%02x y=%02x sp=%02x psw=%02x\n",
         apu.pc(), apu.aReg(), apu.xReg(), apu.yReg(), apu.spReg(), apu.pswReg());
  for (int i = 0; i < 4; i++) {
    printf("  port $%d ($F%X): cpu->apu=%02x  apu->cpu=%02x\n",
           i, 4 + i, apu.inputPort(i), apu.readPort(i));
  }
  printf("dsp: KON=%02x KOFF=%02x FLG=%02x ENDX=%02x DIR=%02x MVOL=%02x/%02x audio=%zu\n",
         apu.dspRegister(0x4C), apu.dspRegister(0x5C), apu.dspRegister(0x6C),
         apu.dspRegister(0x7C), apu.dspRegister(0x5D), apu.dspRegister(0x0C),
         apu.dspRegister(0x1C), apu.audioAvailable());
  for (int n = 0; n < 8; n++) {
    printf("  voice %d: vol=%02x/%02x pitch=%02x%02x srcn=%02x adsr=%02x/%02x gain=%02x envx=%02x outx=%02x\n",
           n, apu.dspRegister(n * 0x10 + 0), apu.dspRegister(n * 0x10 + 1),
           apu.dspRegister(n * 0x10 + 3), apu.dspRegister(n * 0x10 + 2),
           apu.dspRegister(n * 0x10 + 4), apu.dspRegister(n * 0x10 + 5),
           apu.dspRegister(n * 0x10 + 6), apu.dspRegister(n * 0x10 + 7),
           apu.dspRegister(0x08 + n * 0x10), apu.dspRegister(0x09 + n * 0x10));
  }
  {
    snes::int16 buf[4096];
    size_t avail = apu.audioAvailable();
    if (avail > 4096) {
      snes::int16 disc[8192];
      size_t toDiscard = avail - 4096;
      while (toDiscard > 0) {
        size_t n = toDiscard > 8192 ? 8192 : toDiscard;
        apu.readAudio(disc, n);
        toDiscard -= n;
      }
    }
    size_t n = apu.readAudio(buf, 4096);
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
      int v = buf[i] < 0 ? -buf[i] : buf[i];
      if (v > peak) peak = v;
    }
    printf("audio: %zu samples, peak=%d (envx: %02x %02x %02x %02x %02x %02x %02x %02x)\n",
           n, peak,
           apu.dspRegister(0x08), apu.dspRegister(0x18), apu.dspRegister(0x28),
           apu.dspRegister(0x38), apu.dspRegister(0x48), apu.dspRegister(0x58),
           apu.dspRegister(0x68), apu.dspRegister(0x78));
  }

  FILE* f = std::fopen("apuram.bin", "wb");
  if (f) {
    std::vector<uint8_t> ram(0x10000);
    for (uint32_t a = 0; a < 0x10000; a++) ram[a] = apu.ram(snes::uint16(a));
    std::fwrite(ram.data(), 1, ram.size(), f);
    std::fclose(f);
    printf("wrote apuram.bin (64KB)\n");
  }

  // Dump the visible framebuffer (256x224) as a PPM for inspection.
  FILE* ppm = std::fopen("frame.ppm", "wb");
  if (ppm) {
    const int w = 256, h = 224;
    std::fprintf(ppm, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        snes::uint16 v = system.pixelColor(x, y);
        int r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
        uint8_t rgb[3] = {
          uint8_t((r << 3) | (r >> 2)),
          uint8_t((g << 3) | (g >> 2)),
          uint8_t((b << 3) | (b >> 2)),
        };
        std::fwrite(rgb, 1, 3, ppm);
      }
    }
    std::fclose(ppm);
    printf("wrote frame.ppm (%d frames rendered)\n", int(system.renderedFrames()));
  }

  {
    auto& ppu = system.ppu();
    printf("ppu: mode=%d\n", ppu.bgMode());
    for (int bg = 0; bg < 4; bg++) {
      printf("  bg%d screen=%04x tilebase=%04x hscroll=%03x vscroll=%03x\n",
             bg, ppu.bgScreenAddress(bg), ppu.bgTileBase(bg),
             ppu.bgHScroll(bg), ppu.bgVScroll(bg));
    }
    // Dump full VRAM (64KB = 0x8000 words) for offline analysis.
    FILE* vf = std::fopen("vram.bin", "wb");
    if (vf) {
      std::vector<uint8_t> v(0x10000);
      for (int i = 0; i < 0x8000; i++) {
        snes::uint16 w = ppu.vramRead(i);
        v[i * 2] = w & 0xFF;
        v[i * 2 + 1] = w >> 8;
      }
      std::fwrite(v.data(), 1, v.size(), vf);
      std::fclose(vf);
      printf("wrote vram.bin (64KB)\n");
    }
    // Dump WRAM $7E1b00-$7E1d00 (the ch1 DMA source region).
    auto& wr = system.bus().wram();
    printf("wram[7e0100] gamemode=%02x (%02x %02x %02x %02x)\n",
           wr[0x0100], wr[0x0101], wr[0x0102], wr[0x0103], wr[0x0104]);
    printf("wram[7e0010..20]:");
    for (int a = 0x10; a < 0x20; a++) printf(" %02x", wr[a]);
    printf("\n");
    FILE* wf = std::fopen("wram.bin", "wb");
    if (wf) {
      std::fwrite(wr.data(), 1, wr.size(), wf);
      std::fclose(wf);
      printf("wrote wram.bin (%zu bytes)\n", wr.size());
    }
  }

  traceDump(cpu);
  return 0;
}
