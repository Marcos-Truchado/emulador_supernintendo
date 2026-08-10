#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "ppu/ppu.hpp"

namespace snes {

// One scanline = 341 dots = 1364 master cycles; one frame = 262 lines.
constexpr uint64 kDots = 341;
constexpr uint64 kLine = 341 * 4;
constexpr uint64 kFrame = 262 * 341 * 4;

TEST_CASE("ppu: counters advance dot/scanline/frame") {
  Ppu ppu;
  ppu.power();
  CHECK(ppu.dot() == 0);
  CHECK(ppu.scanline() == 0);
  CHECK(ppu.frame() == 0);

  ppu.step(kDots * 4);
  CHECK(ppu.dot() == 0);
  CHECK(ppu.scanline() == 1);
  CHECK(ppu.frame() == 0);

  ppu.step(4);
  CHECK(ppu.dot() == 1);

  // A full frame (262 lines) from the same point: back there, one frame
  // later (frame counter wraps at V=261, so scanline 1 -> 1).
  ppu.step(kFrame);
  CHECK(ppu.frame() == 1);
  CHECK(ppu.dot() == 1);
  CHECK(ppu.scanline() == 1);
}

TEST_CASE("ppu: VBlank/NMI event table (H=0,V=225 set; H=0,V=0 clear)") {
  Ppu ppu;
  ppu.power();

  // Through V=224 nothing happens.
  ppu.step(225 * kLine - 4);
  CHECK(ppu.vblankFlag() == false);
  CHECK(ppu.vblankPeriod() == false);

  // H=0, V=225: VBlank flag set (NMI flag same dot, half-dot unobservable).
  ppu.step(4);
  CHECK(ppu.vblankFlag() == true);
  CHECK(ppu.vblankPeriod() == true);
  CHECK(ppu.read4210() == 0x82);  // latched flag + version 2
  CHECK(ppu.read4210() == 0x02);  // Read/Ack cleared it

  // Still inside VBlank (V=240): live flag stays, latch stays cleared.
  ppu.step((240 - 225) * kLine);
  CHECK(ppu.vblankFlag() == false);
  CHECK(ppu.vblankPeriod() == true);
  CHECK(ppu.read4212() == 0xC0);  // bit7 live + bit6 HBlank (dot 0 is HBlank)

  // V=261 -> V=0: end of VBlank clears both.
  ppu.step((262 - 240) * kLine);
  CHECK(ppu.scanline() == 0);
  CHECK(ppu.vblankPeriod() == false);
  CHECK(ppu.vblankFlag() == false);

  // The flag re-sets every frame: we are now at H=0,V=0 of frame 1
  // (the 22-line step above wrapped past V=261); H=0,V=225 sets it again.
  CHECK(ppu.frame() == 1);
  ppu.step(225 * kLine);
  CHECK(ppu.vblankFlag() == true);
}

TEST_CASE("ppu: HBlank flag H=274 set, H=1 clear") {
  Ppu ppu;
  ppu.power();

  ppu.step(274 * 4 - 4);
  CHECK(ppu.hblank() == false);
  ppu.step(4);
  CHECK(ppu.hblank() == true);
  CHECK((ppu.read4212() & 0x40) == 0x40);

  // Stays set through H=340 and H=0 of the next scanline.
  ppu.step((kDots - 274) * 4);  // to H=0 of line 1
  CHECK(ppu.hblank() == true);
  ppu.step(4);  // H=1: cleared
  CHECK(ppu.hblank() == false);
}

TEST_CASE("ppu: V-IRQ (mode 2) fires once per frame at V=VTIME, H=0") {
  Ppu ppu;
  ppu.power();
  ppu.write4200(0x20);  // mode 2 (V-IRQ)
  ppu.write4209(100);
  ppu.write420A(0);

  ppu.step(100 * kLine - 4);
  CHECK(ppu.irqFlag() == false);
  ppu.step(4);  // H=0, V=100
  CHECK(ppu.irqFlag() == true);
  CHECK(ppu.read4211() == 0x80);
  CHECK(ppu.read4211() == 0x00);  // Read/Ack

  // Exactly once per frame: nothing at H=100 of any other line.
  ppu.step(kLine);
  CHECK(ppu.irqFlag() == false);  // H=0, V=101
  ppu.step(kLine);                // H=0, V=102
  CHECK(ppu.irqFlag() == false);
  // Next frame retriggers.
  ppu.step((262 - 102) * kLine + 100 * kLine - 4);
  ppu.step(4);
  CHECK(ppu.irqFlag() == true);
}

TEST_CASE("ppu: H-IRQ (mode 1) fires every scanline at H=HTIME") {
  Ppu ppu;
  ppu.power();
  ppu.write4200(0x10);  // mode 1 (H-IRQ)
  ppu.write4207(200);
  ppu.write4208(0);

  ppu.step(200 * 4 - 4);
  CHECK(ppu.irqFlag() == false);
  ppu.step(4);  // H=200
  CHECK(ppu.irqFlag() == true);
  CHECK(ppu.read4211() == 0x80);

  // Next scanline triggers again (H-IRQ: any V).
  ppu.step(kLine - 200 * 4 + 200 * 4 - 4);
  ppu.step(4);
  CHECK(ppu.irqFlag() == true);
  CHECK(ppu.read4211() == 0x80);
}

TEST_CASE("ppu: HV-IRQ (mode 3) fires at H=HTIME and V=VTIME") {
  Ppu ppu;
  ppu.power();
  ppu.write4200(0x30);  // mode 3 (HV-IRQ)
  ppu.write4207(150);
  ppu.write4208(0);
  ppu.write4209(50);
  ppu.write420A(0);

  // H=150 on another scanline: no trigger.
  ppu.step(150 * 4 - 4);
  ppu.step(4);
  CHECK(ppu.irqFlag() == false);
  // To V=49 (dot 339), then through H=0..150 of V=50: trigger.
  ppu.step(50 * kLine - 150 * 4 - 4);
  ppu.step(151 * 4);
  CHECK(ppu.irqFlag() == true);
  CHECK(ppu.read4211() == 0x80);
}

TEST_CASE("ppu: disabling IRQs acknowledges the flag") {
  Ppu ppu;
  ppu.power();
  ppu.write4200(0x20);
  ppu.write4209(10);
  ppu.write420A(0);
  ppu.step(10 * kLine + 4);
  CHECK(ppu.irqFlag() == true);

  ppu.write4200(0x00);  // bits 5-4 -> 0: acknowledge
  CHECK(ppu.irqFlag() == false);
  CHECK(ppu.read4211() == 0x00);

  // NMI disable does NOT acknowledge ($4210 latch keeps its life).
  ppu.step(215 * kLine + 4);  // V=225: NMI latch set
  CHECK(ppu.vblankFlag() == true);
  ppu.write4200(0x00);
  CHECK(ppu.vblankFlag() == true);  // untouched
}

TEST_CASE("ppu: power/reset register values (fullsnes I/O map)") {
  Ppu ppu;
  ppu.power();
  CHECK(ppu.nmitimen() == 0x00);
  CHECK(ppu.dot() == 0);
  CHECK(ppu.scanline() == 0);
  CHECK(ppu.frame() == 0);

  ppu.write4200(0x81);  // NMI + joypad
  ppu.write4207(0x64);  // HTIME = 0x064 (low byte)
  ppu.write4208(0x00);  // HTIME high bit 0 (overrides power-on 0x1FF)
  ppu.write4209(0x40);
  ppu.write420A(0x00);
  ppu.step(1000);

  // Soft reset: NMITIMEN clears; bracketed HTIME/VTIME survive.
  ppu.reset();
  CHECK(ppu.nmitimen() == 0x00);
  CHECK(ppu.dot() == 0);
  CHECK(ppu.scanline() == 0);
  ppu.step(100 * 4 - 4);
  CHECK(ppu.irqFlag() == false);  // not at HTIME yet
  ppu.write4200(0x10);            // H-IRQ mode
  ppu.step(4);                    // enter H=100 == HTIME (survived reset)
  CHECK(ppu.irqFlag() == true);
}

TEST_CASE("ppu: long scanline/H-counter wrap is exact (341 dots)") {
  Ppu ppu;
  ppu.power();
  // 262 frames of dots -> 262 scanline wrap-arounds, frame counter exact.
  for (int f = 0; f < 3; f++) {
    for (int v = 0; v < 262; v++) {
      CHECK(ppu.scanline() == v);
      for (int h = 0; h < 340; h++) {
        CHECK(ppu.dot() == h);
        ppu.step(4);
      }
      ppu.step(4);  // H=340 -> wraps
      CHECK(ppu.dot() == 0);
    }
    CHECK(ppu.frame() == f + 1);
  }
}

TEST_CASE("ppu: NMI pin raises once per frame on the internal edge") {
  Ppu ppu;
  ppu.power();
  int raises = 0;
  ppu.setNmiPin([&](bool value) { if (value) raises++; });

  ppu.write4200(0x80);  // NMI enabled
  ppu.step(225 * kLine - 4);
  CHECK(raises == 0);
  ppu.step(4);  // H=0, V=225: internal NMI flag edge (NMITIMEN.7 AND latch)
  CHECK(raises == 1);

  // Still inside VBlank: the AND stays high, no second edge -> no re-raise.
  ppu.step(20 * kLine);
  CHECK(raises == 1);

  // V=0: end of VBlank drops the AND; the next VBlank re-arms the edge.
  ppu.step((262 - 245) * kLine);
  CHECK(raises == 1);
  ppu.step(225 * kLine - 4);
  ppu.step(4);  // next frame's VBlank
  CHECK(raises == 2);
}

TEST_CASE("ppu: NMI edge re-arms when $4210 is read mid-VBlank") {
  Ppu ppu;
  ppu.power();
  int raises = 0;
  ppu.setNmiPin([&](bool value) { if (value) raises++; });

  ppu.write4200(0x80);
  ppu.step(225 * kLine - 4);
  ppu.step(4);
  CHECK(raises == 1);

  // Reading $4210 clears the latch -> the AND drops -> the edge re-arms
  // within the same VBlank (next VBlank raises again).
  CHECK(ppu.read4210() == 0x82);
  ppu.step((262 - 225) * kLine);  // through V=0 and back to the frame start
  CHECK(raises == 1);
  ppu.step(225 * kLine - 4);
  ppu.step(4);  // next frame VBlank: fresh 0->1 edge
  CHECK(raises == 2);
}

TEST_CASE("ppu: NMI disabled by $4200 bit7 raises nothing; re-enable "
          "mid-VBlank mis-executes the old NMI") {
  Ppu ppu;
  ppu.power();
  int raises = 0;
  ppu.setNmiPin([&](bool value) { if (value) raises++; });

  ppu.step(225 * kLine - 4);
  ppu.step(4);  // VBlank latch set, NMI disabled: no raise
  CHECK(ppu.vblankFlag() == true);
  CHECK(raises == 0);

  // Re-enabling NMITIMEN.7 inside the pending VBlank: the AND 0->1 edge
  // fires immediately (fullsnes "old NMI mis-executed on re-enable").
  ppu.write4200(0x80);
  ppu.step(4);
  CHECK(raises == 1);
}

TEST_CASE("ppu: IRQ pin mirrors the $4211 latch (level model)") {
  Ppu ppu;
  ppu.power();
  bool pin = false;
  int calls = 0;
  ppu.setIrqPin([&](bool value) { pin = value; calls++; });

  ppu.write4200(0x10);  // H-IRQ mode
  ppu.write4207(50);
  ppu.write4208(0);
  ppu.step(50 * 4 - 4);
  CHECK(pin == false);
  ppu.step(4);  // H=50: latch set, pin raised
  CHECK(pin == true);

  // Read/Ack drops the pin without a dot advance.
  CHECK(ppu.read4211() == 0x80);
  CHECK(pin == false);

  // Next scanline re-triggers: latch and pin back up.
  ppu.step(kLine - 4);
  ppu.step(4);
  CHECK(pin == true);

  // Disabling IRQs ($4200 bits 5-4 -> 0) acknowledges: pin drops.
  ppu.write4200(0x00);
  CHECK(pin == false);

  // And re-arming the mode makes it trigger again.
  ppu.write4200(0x10);
  ppu.step(kLine - 4);
  ppu.step(4);
  CHECK(pin == true);
}

}  // namespace snes
