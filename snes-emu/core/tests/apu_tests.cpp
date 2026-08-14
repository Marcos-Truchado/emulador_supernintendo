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
  // The 7-instruction program takes ~23 SMP cycles; run 40 SMP cycles of
  // master-clock budget (40 * 21 master cycles) so it finishes.
  for (int i = 0; i < 40; i++) apu.step(21);
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
  apu.setDspRegister(0x02, 0x00);  // V0PITCHL
  apu.setDspRegister(0x03, 0x10);  // V0PITCHH -> pitch = 0x1000
  apu.setDspRegister(0x00, 0x7F);  // V0VOLL = 127
  apu.setDspRegister(0x01, 0x7F);  // V0VOLR = 127
  apu.setDspRegister(0x05, 0xDF);  // V0ADSR1: ADSR mode, attack rate 31
  apu.setDspRegister(0x06, 0xE0);  // V0ADSR2
  apu.setDspRegister(0x0C, 0x7F);  // MVOLL = 127
  apu.setDspRegister(0x1C, 0x7F);  // MVOLR = 127
  apu.setDspRegister(0x6C, 0x20);  // FLG: unmute
  apu.setDspRegister(0x4C, 0x01);  // KON voice 0

  // Run enough DSP samples for the BRR decode (every 4 samples at pitch
  // 0x1000) and the attack to produce a non-zero output.
  for (int i = 0; i < 256; i++) apu.step(21);
  CHECK(apu.sampleLeft() != 0);
}

TEST_CASE("apu: SMP timers count (128-cycle clock) and disable clears output") {
  Apu apu;
  apu.power();
  apu.setControl(0x01);           // bit7=0 (RAM at $FFC0), bit0=1 (timer 0 on)
  apu.setRam(0xFFC0, 0x2F);       // BRA -2 (4-cycle self loop)
  apu.setRam(0xFFC1, 0xFE);
  apu.reset();                    // PC = 0xFFC0
  apu.setTimerDivider(0, 1);      // divide by 1: T0OUT++ on every 128-cycle tick

  CHECK(apu.timerOut(0) == 0);
  // 32 BRA iterations * 4 SMP cycles = 128 SMP cycles = one timer-0 tick.
  // Each BRA consumes 4*21 master cycles, so step 84 master cycles per BRA.
  for (int i = 0; i < 32; i++) apu.step(21 * 4);
  CHECK(apu.timerOut(0) == 1);
  for (int i = 0; i < 32; i++) apu.step(21 * 4);
  CHECK(apu.timerOut(0) == 2);

  // Disabling the timer (CONTROL bit 0 = 0) resets the output to zero.
  apu.setControl(0x00);
  CHECK(apu.timerOut(0) == 0);
}

TEST_CASE("apu: SMP timer 2 runs on the 16-cycle (64kHz) clock") {
  Apu apu;
  apu.power();
  apu.setControl(0x04);           // timer 2 enabled (bit2), RAM mode
  apu.setRam(0xFFC0, 0x2F);       // BRA -2 (4-cycle self loop)
  apu.setRam(0xFFC1, 0xFE);
  apu.reset();
  apu.setTimerDivider(2, 1);      // divide by 1: T2OUT++ every 16-cycle tick

  CHECK(apu.timerOut(2) == 0);
  // 4 BRA iterations * 4 SMP cycles = 16 SMP cycles = one timer-2 tick.
  for (int i = 0; i < 4; i++) apu.step(21 * 4);
  CHECK(apu.timerOut(2) == 1);
  for (int i = 0; i < 4; i++) apu.step(21 * 4);
  CHECK(apu.timerOut(2) == 2);
}

TEST_CASE("apu: audio ring buffer accumulates and drains") {
  Apu apu;
  apu.power();
  CHECK(apu.audioAvailable() == 0);

  // Run the boot ROM and then idle: the DSP emits 32kHz samples regardless.
  for (int i = 0; i < 1024; i++) apu.step(21);
  const size_t available = apu.audioAvailable();
  CHECK(available > 0);
  CHECK(available % 2 == 0);  // stereo (interleaved L/R int16)

  std::array<int16, 4096> buf{};
  const size_t n = apu.readAudio(buf.data(), buf.size());
  CHECK(n == available);
  CHECK(apu.audioAvailable() == 0);
}

TEST_CASE("apu: SPC700 CALL/RET returns to the correct address") {
  Apu apu;
  apu.power();
  apu.setControl(0x00);  // RAM mode
  // 0xFFC0: MOV A,#$34 / CALL $0500 / MOV $00,A  (store $56 if RET works)
  apu.setRam(0xFFC0, 0xE8); apu.setRam(0xFFC1, 0x34);
  apu.setRam(0xFFC2, 0x3F); apu.setRam(0xFFC3, 0x00); apu.setRam(0xFFC4, 0x05);
  apu.setRam(0xFFC5, 0xC4); apu.setRam(0xFFC6, 0x00);
  // subroutine at $0500: MOV A,#$56 / RET
  apu.setRam(0x0500, 0xE8); apu.setRam(0x0501, 0x56);
  apu.setRam(0x0502, 0x6F);
  apu.reset();  // pc = 0xFFC0
  for (int i = 0; i < 40; i++) apu.step(21);
  CHECK(apu.ram(0x0000) == 0x56);  // A=$56 survived the CALL/RET round-trip
}

TEST_CASE("apu: SPC700 ALU memory forms (dp,#imm and (X),(Y))") {
  Apu apu;
  apu.power();
  apu.setControl(0x00);
  // MOV A,#$41 / MOV $00,A / OR $00,#$0F / AND $00,#$7B
  //   -> [$00] = ($41 | $0F) & $7B = $4F & $7B = $4B
  apu.setRam(0xFFC0, 0xE8); apu.setRam(0xFFC1, 0x41);
  apu.setRam(0xFFC2, 0xC4); apu.setRam(0xFFC3, 0x00);
  apu.setRam(0xFFC4, 0x18); apu.setRam(0xFFC5, 0x0F); apu.setRam(0xFFC6, 0x00);
  apu.setRam(0xFFC7, 0x38); apu.setRam(0xFFC8, 0x7B); apu.setRam(0xFFC9, 0x00);
  apu.reset();
  for (int i = 0; i < 16; i++) apu.step(21);
  CHECK(apu.ram(0x0000) == 0x4B);
}

TEST_CASE("apu: CONTROL $F1 bit4/5 reset the CPU->SMP input latches") {
  Apu apu;
  apu.power();
  apu.writePort(0, 0x12);
  apu.writePort(1, 0x34);
  apu.writePort(2, 0x56);
  apu.writePort(3, 0x78);
  CHECK(apu.inputPort(0) == 0x12);
  apu.setControl(0x10);  // bit4: reset $F4/$F5 input latches
  CHECK(apu.inputPort(0) == 0x00);
  CHECK(apu.inputPort(1) == 0x00);
  CHECK(apu.inputPort(2) == 0x56);  // bit5 untouched
  CHECK(apu.inputPort(3) == 0x78);
  apu.setControl(0x20);  // bit5: reset $F6/$F7 input latches
  CHECK(apu.inputPort(2) == 0x00);
  CHECK(apu.inputPort(3) == 0x00);
}

TEST_CASE("apu: SPC700 1-bit ops decode the bit select correctly") {
  Apu apu;
  apu.power();
  apu.setControl(0x00);  // RAM mode
  // CLRC / MOV1 C,$0010.5 / BCC +4 / MOV A,#$56 / MOV $00,A
  // With bit5 decoded correctly, C=1 so BCC is not taken and A=$56 is stored.
  // With the old "bit always 0" bug, C=0 so BCC skips the store and $00 stays 0.
  apu.setRam(0xFFC0, 0x60);                                  // CLRC
  apu.setRam(0xFFC1, 0xAA); apu.setRam(0xFFC2, 0x10); apu.setRam(0xFFC3, 0xA0);  // MOV1 C,$0010.5
  apu.setRam(0xFFC4, 0x90); apu.setRam(0xFFC5, 0x04);        // BCC +4
  apu.setRam(0xFFC6, 0xE8); apu.setRam(0xFFC7, 0x56);        // MOV A,#$56
  apu.setRam(0xFFC8, 0xC4); apu.setRam(0xFFC9, 0x00);        // MOV $00,A
  apu.setRam(0x0010, 0x20);  // bit 5 set (bit 0 clear)
  apu.reset();
  for (int i = 0; i < 16; i++) apu.step(21);
  CHECK(apu.ram(0x0000) == 0x56);  // bit 5 read as 1 -> C=1 -> store taken
}

TEST_CASE("apu: SPC700 CMP X,#imm sets Z and branches correctly") {
  Apu apu;
  apu.power();
  apu.setControl(0x00);
  // MOV A,#$05 / MOV X,A / CMP X,#$05 / BNE +4 / MOV A,#$77 / MOV $00,A
  //   -> CMP sets Z=1 so BNE is not taken, A=$77 is stored.
  apu.setRam(0xFFC0, 0xE8); apu.setRam(0xFFC1, 0x05);
  apu.setRam(0xFFC2, 0x5D);
  apu.setRam(0xFFC3, 0xC8); apu.setRam(0xFFC4, 0x05);
  apu.setRam(0xFFC5, 0xD0); apu.setRam(0xFFC6, 0x04);
  apu.setRam(0xFFC7, 0xE8); apu.setRam(0xFFC8, 0x77);
  apu.setRam(0xFFC9, 0xC4); apu.setRam(0xFFCA, 0x00);
  apu.reset();
  for (int i = 0; i < 16; i++) apu.step(21);
  CHECK(apu.ram(0x0000) == 0x77);
}

}  // namespace snes
