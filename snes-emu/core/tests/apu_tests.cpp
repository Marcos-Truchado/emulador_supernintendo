#include <doctest/doctest.h>

#include "apu/apu.hpp"

namespace snes {

TEST_CASE("apu: SPC700 boot ROM runs and signals the main CPU") {
  Apu apu;
  apu.power();
  // Run the boot ROM (zero-fill + write $F4/$F5 = 0xAA/0xBB).
  for (int i = 0; i < 4096; i++) apu.step(21);
  CHECK(apu.readPort(0) == 0xAA);
  CHECK(apu.readPort(1) == 0xBB);
  // The boot ROM zero-fills $0001-$00EF (leaving the I/O ports alone).
  CHECK(apu.ram(0x0010) == 0x00);
  CHECK(apu.ram(0x00EF) == 0x00);
}

TEST_CASE("apu: SPC700 executes a small program (ALU, branch, stack)") {
  Apu apu;
  apu.power();
  // Disable ROM so $FFC0-$FFFF is RAM, and put a program at the reset entry.
  apu.setControl(0x00);
  //   E8 40  mov a,#40      ; A=0x40
  //   60     clrc
  //   88 20  adc a,#20      ; A=0x60
  //   2D     push a
  //   9F     xcn a          ; A=0x06
  //   AE     pop a          ; A=0x60
  //   C4 00  mov [00],a     ; store A -> $0000
  apu.setRam(0xFFC0, 0xE8); apu.setRam(0xFFC1, 0x40);
  apu.setRam(0xFFC2, 0x60);
  apu.setRam(0xFFC3, 0x88); apu.setRam(0xFFC4, 0x20);
  apu.setRam(0xFFC5, 0x2D);
  apu.setRam(0xFFC6, 0x9F);
  apu.setRam(0xFFC7, 0xAE);
  apu.setRam(0xFFC8, 0xC4); apu.setRam(0xFFC9, 0x00);
  apu.reset();  // pc = 0xFFC0
  for (int i = 0; i < 16; i++) apu.step(21);
  CHECK(apu.ram(0x0000) == 0x60);  // A survived push/xcn/pop as 0x60
}

TEST_CASE("apu: DSP BRR decode produces a non-zero sample after key-on") {
  Apu apu;
  apu.power();

  // Sample table at RAM $0000: BRR start = $0100, loop = $0100.
  apu.setRam(0x0000, 0x00); apu.setRam(0x0001, 0x01);
  apu.setRam(0x0002, 0x00); apu.setRam(0x0003, 0x01);
  // BRR block at $0100: header (shift 12, filter 0, no loop) + nibbles +7.
  apu.setRam(0x0100, 0xC0);
  for (int i = 0; i < 8; i++) apu.setRam(0x0101 + i, 0x77);

  apu.setDspRegister(0x5D, 0x00);  // DIR = 0
  apu.setDspRegister(0x04, 0x00);  // V0SRCN = 0
  apu.setDspRegister(0x00, 0x7F);  // V0VOLL = 127
  apu.setDspRegister(0x01, 0x7F);  // V0VOLR = 127
  apu.setDspRegister(0x05, 0xDF);  // V0ADSR1: ADSR mode, attack rate 31
  apu.setDspRegister(0x06, 0xE0);  // V0ADSR2
  apu.setDspRegister(0x0C, 0x7F);  // MVOLL = 127
  apu.setDspRegister(0x1C, 0x7F);  // MVOLR = 127
  apu.setDspRegister(0x6C, 0x20);  // FLG: unmute
  apu.setDspRegister(0x4C, 0x01);  // KON voice 0

  // 64 SMP cycles = 2 DSP samples; attack raises the envelope above zero.
  for (int i = 0; i < 64; i++) apu.step(21);
  CHECK(apu.sampleLeft() != 0);
}

}  // namespace snes
