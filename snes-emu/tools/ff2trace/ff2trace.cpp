// ff2trace — per-frame reference tracer for the FF2 US boot/title hang.
// Mirror of tools/traceport (snes9x): runs the ROM headlessly and logs one
// line per frame with per-region instruction counts, WRAM snapshots for the
// values the game uses to advance, and the D register at the bank-1 entry.
//
// Usage: snes_ff2trace <rom.smc> <max_frames>

#include "snes/snes.hpp"
#include "apu/apu.hpp"
#include "cpu/cpu65816.hpp"
#include "ppu/ppu.hpp"
#include "scheduler/scheduler.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct FrameStats {
  unsigned nmiHits = 0;        // PC in 0x009085..0x0092ef
  unsigned bank1Hits = 0;      // PB == 0x01
  unsigned transformHits = 0;  // 0x14fd00..0x14feff
  unsigned titleLoopHits = 0;  // 0x008660..0x008690
  unsigned hubHits = 0;        // 0x0080a0..0x008138
unsigned setterHits = 0;     // PC == 0x14fe4e..0x14fe5d
  unsigned fd00Hits = 0;       // 0x14fd00..0x14fd11 (dispatch)
  unsigned fdd9Hits = 0;       // 0x14fdd9..0x14feff (setter body)
  unsigned init801cHits = 0;   // PC == 0x01801c (bank-1 init)
  unsigned bank1Entries = 0;   // transitions into 0x018010 (+S/RTL region)
  snes::uint32 pcFirstBank1 = 0;
  snes::uint32 pcLastBank1 = 0;
  bool inBank1 = false;
  char lastBank1EntryD[8] = "----";
};

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <rom.smc> <max_frames>\n", argv[0]);
    return 2;
  }
  const long maxFrames = std::strtol(argv[2], nullptr, 10);

  snes::System system;
  std::string error;
  if (!system.load(argv[1], &error)) {
    std::fprintf(stderr, "load: %s\n", error.c_str());
    return 2;
  }
  auto& cpu = system.cpu();
  auto& bus = system.bus();
  auto& ppu = system.ppu();
  auto& apu = system.apu();
  system.reset();

  FILE* out = fopen("ff2_wram.log", "w");
  if (!out) {
    std::fprintf(stderr, "cannot open ff2_wram.log\n");
    return 2;
  }

  snes::uint64 framesDone = 0;
  snes::uint64 target = ppu.renderedFrames() + 1;
  FrameStats* st = new FrameStats();
  const snes::uint64 kMaxInst = 200000000ull;
  long pressStart = -1, pressHold = 1;
  long pressValue = 0x1000;
  long press2Start = -1, press2Hold = 1;
  long press2Value = 0x0080;
  long repeatStart = -1, repeatInterval = 300, repeatHold = 5, repeatCount = 0;
  long repeatValue = 0x0080;
  if (const char* e = getenv("FF2_PRESS_FRAME")) pressStart = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_PRESS_HOLD")) pressHold = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_PRESS_VALUE")) pressValue = strtol(e, nullptr, 16);
  if (const char* e = getenv("FF2_PRESS2_FRAME")) press2Start = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_PRESS2_HOLD")) press2Hold = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_PRESS2_VALUE")) press2Value = strtol(e, nullptr, 16);
  if (const char* e = getenv("FF2_REPEAT_FRAME")) repeatStart = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_REPEAT_INTERVAL")) repeatInterval = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_REPEAT_HOLD")) repeatHold = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_REPEAT_COUNT")) repeatCount = strtol(e, nullptr, 10);
  if (const char* e = getenv("FF2_REPEAT_VALUE")) repeatValue = strtol(e, nullptr, 16);

  long traceFrom = -1, traceTo = -1;
  FILE* tr = nullptr;
  FILE* apuTrace = nullptr;
  if (const char* e = getenv("FF2_TRACE")) {
    traceFrom = strtol(e, nullptr, 10);
    if (const char* c = strchr(e, ':')) traceTo = strtol(c + 1, nullptr, 10);
    if (traceTo < traceFrom) traceTo = traceFrom;
    tr = fopen("ff2_trace.log", "w");
  }
  if (getenv("FF2_APU_TRACE")) apuTrace = fopen("ff2_apu_trace.log", "w");

  snes::uint8 lastApuIn = apu.inputPort(0);
  snes::uint8 lastApuOut = apu.readPort(0);

  for (snes::uint64 total = 0; total < kMaxInst && framesDone < (snes::uint64)maxFrames; total++) {
    snes::uint24 pc = cpu.pc();

    // region hits
    if (pc >= 0x009085 && pc <= 0x0092ef) st->nmiHits++;
    if ((pc >> 16) == 0x01) {
      st->bank1Hits++;
      if (!st->pcFirstBank1) {
        st->pcFirstBank1 = pc;
        if (pc == 0x018010 || pc == 0x01801c || pc == 0x018020 ||
            (pc >= 0x018000 && pc <= 0x0180ff)) {
          snprintf(st->lastBank1EntryD, sizeof(st->lastBank1EntryD), "%04x",
                   cpu.traceState().d);
        }
      }
      st->pcLastBank1 = pc;
    }
    if (pc >= 0x14fd00 && pc <= 0x14feff) st->transformHits++;
    if (pc >= 0x14fd00 && pc <= 0x14fd11) st->fd00Hits++;
    if (pc >= 0x14fdd9 && pc <= 0x14feff) st->fdd9Hits++;
    if (pc == 0x01801c) st->init801cHits++;
    if (pc >= 0x008660 && pc <= 0x008690) st->titleLoopHits++;
    if (pc >= 0x0080a0 && pc <= 0x008138) st->hubHits++;
    if (pc >= 0x14fe4e && pc <= 0x14fe5d) st->setterHits++;

    if (pressStart >= 0 || press2Start >= 0 || repeatStart >= 0) {
      const snes::uint64 fr = ppu.renderedFrames();
      const bool first = pressStart >= 0 && fr >= (snes::uint64)pressStart &&
                         fr < (snes::uint64)(pressStart + pressHold);
      const bool second = press2Start >= 0 && fr >= (snes::uint64)press2Start &&
                          fr < (snes::uint64)(press2Start + press2Hold);
      const long repeatIndex = repeatInterval > 0 && repeatStart >= 0
                                   ? long((fr - (snes::uint64)repeatStart) / repeatInterval)
                                   : -1;
      const bool repeated = repeatStart >= 0 && fr >= (snes::uint64)repeatStart &&
                            (repeatCount <= 0 || repeatIndex < repeatCount) &&
                            fr < (snes::uint64)(repeatStart + repeatIndex * repeatInterval + repeatHold);
      system.setJoypad(0, first ? snes::uint16(pressValue) :
                          second ? snes::uint16(press2Value) :
                          repeated ? snes::uint16(repeatValue) : 0x0000);
    }

    const snes::uint64 fr = ppu.renderedFrames();
    if (tr && fr >= (snes::uint64)traceFrom && fr <= (snes::uint64)traceTo) {
      fprintf(tr, "%06x %02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n", pc,
              cpu.traceState().d >> 8, bus.read(0x7e0100), bus.read(0x7e0101), bus.read(0x7e0102),
              bus.read(0x7e0103), bus.read(0x7e0104), bus.read(0x7e0105), bus.read(0x7e0106),
              bus.read(0x7e0107), bus.read(0x7e0108), bus.read(0x7e0109), bus.read(0x7e010a),
              bus.read(0x7e010b), bus.read(0x7e010c), bus.read(0x7e010d));
    }

    system.step();

    if (apuTrace && (apu.inputPort(0) != lastApuIn || apu.readPort(0) != lastApuOut)) {
      const auto state = cpu.traceState();
      std::fprintf(apuTrace, "f%05llu pc=%06x a=%04x in=%02x out=%02x apupc=%04x "
                   "apua=%02x x=%02x y=%02x p=%02x\n",
                   (unsigned long long)ppu.renderedFrames(), state.pc, state.a,
                   apu.inputPort(0), apu.readPort(0), apu.pc(), apu.aReg(), apu.xReg(),
                   apu.yReg(), apu.pswReg());
      lastApuIn = apu.inputPort(0);
      lastApuOut = apu.readPort(0);
    }

    if (ppu.renderedFrames() >= target) {
      // frame boundary
      char w0100[128] = {0}, w0600[64] = {0}, w1a64[32] = {0};
      for (int i = 0; i < 0x20; i++)
        snprintf(w0100 + i * 3, sizeof(w0100) - i * 3, "%02x ", bus.read(0x7e0100 + i));
      for (int i = 0; i < 4; i++)
        snprintf(w0600 + i * 3, sizeof(w0600) - i * 3, "%02x ", bus.read(0x7e0600 + i));
      snprintf(w1a64, sizeof(w1a64), "%02x", bus.read(0x7e1a64));
      char w1700[16];
      snprintf(w1700, sizeof(w1700), "%02x", bus.read(0x7e1700));
      const snes::uint8 w352d = bus.read(0x7e352d);
      const snes::uint16 j1 = snes::uint16(bus.read(0x4218)) | snes::uint16(bus.read(0x4219)) << 8;
      auto m7 = ppu.debugM7();
      auto w = ppu.debugWindows();

      fprintf(out,
              "f%05llu nm%u b1%u tr%u ti%u se%u init%u fd00%u fdd9%u e018010=%u d=%s pc1st=%06x pcLast=%06x "
              "| 0100: %s| 0600-03: %s| 1a64: %s 1700: %s 352d: %02x j1=%04x | br=%u m7=%04x %04x %04x %04x "
              "win=%02x%02x%02x%02x s=%02x%02x%02x%02x%02x e=%02x%02x%02x%02x%02x "
              "apu=%04x in=%02x%02x%02x%02x out=%02x%02x%02x%02x ramf4=%02x\n",
              (unsigned long long)framesDone, st->nmiHits, st->bank1Hits, st->transformHits,
              st->titleLoopHits, st->setterHits, st->init801cHits, st->fd00Hits, st->fdd9Hits,
              st->bank1Entries, st->lastBank1EntryD, st->pcFirstBank1, st->pcLastBank1, w0100,
              w0600, w1a64, w1700, w352d, j1, ppu.debugBrightness(), m7.a, m7.b, m7.c, m7.d,
              w.oneLeft, w.oneRight, w.twoLeft, w.twoRight,
               w.bg1, w.bg2, w.bg3, w.bg4, w.obj,
               w.bg1en, w.bg2en, w.bg3en, w.bg4en, w.objen,
               apu.pc(), apu.inputPort(3), apu.inputPort(2), apu.inputPort(1), apu.inputPort(0),
               apu.readPort(3), apu.readPort(2), apu.readPort(1), apu.readPort(0), apu.ram(0x00f4));
      fflush(out);

      if (framesDone < 200 || framesDone == 299 || framesDone == 399 || framesDone == 599 ||
          framesDone == 799 || framesDone == 899 || framesDone == 999 || framesDone == 1199 ||
          framesDone == 1599 || framesDone == 1999 || framesDone == 2399 ||
          framesDone == 2999 || framesDone == 3599) {
        char name[64];
        snprintf(name, sizeof(name), "ff2_cg%03llu.txt", (unsigned long long)framesDone);
        FILE* cf = fopen(name, "w");
        if (cf) {
          for (int i = 0; i < 256; i++)
            fprintf(cf, "%04x%c", ppu.cgramRead(i), (i & 15) == 15 ? '\n' : ' ');
          fclose(cf);
        }
        char name2[64];
        snprintf(name2, sizeof(name2), "ff2_oa%03llu.txt", (unsigned long long)framesDone);
        FILE* of = fopen(name2, "w");
        if (of) {
          for (int i = 0; i < 544; i++)
            fprintf(of, "%02x%c", ppu.oamReadRaw(i), (i & 31) == 31 ? '\n' : ' ');
          fclose(of);
        }
        snprintf(name2, sizeof(name2), "ff2_vr%03llu.bin", (unsigned long long)framesDone);
        FILE* vf = fopen(name2, "wb");
        if (vf) {
          for (int i = 0; i < 32768; i++) {
            snes::uint16 w = ppu.vramRead(i);
            fputc(w & 0xff, vf);
            fputc(w >> 8, vf);
          }
          fclose(vf);
        }
        snprintf(name2, sizeof(name2), "ff2_f%03llu.ppm", (unsigned long long)framesDone);
        FILE* pf = fopen(name2, "wb");
        if (pf) {
          const int vw = 256, vh = 224;
          fprintf(pf, "P6\n%d %d\n255\n", vw, vh);
          static snes::uint8 row[512 * 3];
          for (int y = 0; y < vh; y++) {
            for (int x = 0; x < vw; x++) {
              // pixelColor(x, y + 8) reads content(scanline y + 9), mirroring
              // the snes9x traceport dump (screen row = scanline - 1, dump
              // starts 8 rows into the screen).
              snes::uint16 c = ppu.pixelColor(x, y + 8) & 0x7fff;
              row[x * 3 + 0] = (snes::uint8)((c & 0x1f) * 255 / 31);
              row[x * 3 + 1] = (snes::uint8)(((c >> 5) & 0x1f) * 255 / 31);
              row[x * 3 + 2] = (snes::uint8)(((c >> 10) & 0x1f) * 255 / 31);
            }
            fwrite(row, 1, vw * 3, pf);
          }
          fclose(pf);
        }
      }
      // snapshot deltas per frame
      *st = FrameStats();
      target = ppu.renderedFrames() + 1;
      framesDone++;
    }
  }

  std::fprintf(stderr, "ran %llu instructions, %llu frames\n",
               (unsigned long long)kMaxInst > 0 ? (unsigned long long)kMaxInst : 0,
               (unsigned long long)framesDone);
  fclose(out);
  delete st;
  return 0;
}
