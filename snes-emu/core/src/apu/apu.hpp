#pragma once

#include "scheduler/thread.hpp"
#include "snes/snes.hpp"

namespace snes {

// S-SMP (SPC700) + S-DSP — the SNES audio unit (phase 6).
//
// The SPC700 is an 8-bit 6502-derived CPU with 64KB of private RAM and an I/O
// port range at $00F0-$00FF (timer dividers/outputs, DSP access via $F2/$F3,
// and the four CPU↔APU ports $F4-$F7). The S-DSP generates the samples: 8
// voices, 4-bit BRR (ADPCM) decoding, ADSR/gain envelopes, pitch, noise, and
// a stereo mixer. Implemented against fullsnes (noSns v1.6).
//
// ponytail: echo/reverb is NOT generated yet (see the mixer comment); the DSP
// is correct for the non-echo path, which covers most sound effects and drums.
class Apu : public Thread {
 public:
  Apu();

  // Thread: the SPC700 runs at 24.576MHz/24 = 1.024MHz; one SMP cycle is
  // ~20.97 master cycles, modeled as 21 master cycles (ratio note in
  // thread.hpp). Advances the SMP (and the DSP, 32 samples per SMP-cycle
  // boundary) by the given master-clock amount.
  auto step(uint64 masterCycles) -> void override;

  auto power() -> void;
  auto reset() -> void;

  // Execute one SPC700 instruction; returns SMP cycles consumed.
  auto stepInstruction() -> int;

  // CPU↔APU ports $2140-$2143 (the bus routes these here).
  auto writePort(int index, uint8 data) -> void;
  auto readPort(int index) -> uint8;

  // readback for tests
  auto ram(uint16 address) const -> uint8;
  auto dspRegister(uint8 index) const -> uint8;
  auto sampleLeft() const -> int8 { return sample_[0]; }
  auto sampleRight() const -> int8 { return sample_[1]; }
  // test-only setup helpers
  auto setRam(uint16 address, uint8 data) -> void { ram_[address] = data; }
  auto setDspRegister(uint8 index, uint8 data) -> void { dspWrite(index, data); }
  auto setControl(uint8 data) -> void { write(0x00F1, data); }

 private:
  // ---- SPC700 CPU ----
  uint8 ram_[0x10000] = {};
  uint8 a_ = 0, x_ = 0, y_ = 0, sp_ = 0;  // SP addresses 0x100 + sp
  uint8 psw_ = 0;                          // N V P B H I Z C (bits 7..0)
  uint16 pc_ = 0;
  uint16 ya() const { return uint16((uint16(y_) << 8) | a_); }
  void setYa(uint16 v) { a_ = v & 0xFF; y_ = v >> 8; }

  // I/O ports $00F0-$00FF (write-only vs read-only semantics below).
  uint8 port_[0x10] = {};
  uint8 apuIn_[4] = {};  // CPUIO input latch (main CPU -> SMP, $F4-$F7 reads)
  // timers: dividers $FA-$FC, outputs $FD-$FF.
  int timerDivider_[3] = {};

  static constexpr uint8 kN = 0x80, kV = 0x40, kP = 0x20, kB = 0x10,
                           kH = 0x08, kI = 0x04, kZ = 0x02, kC = 0x01;

  auto flag(uint8 f) const -> bool { return psw_ & f; }
  void setFlag(uint8 f, bool v) { psw_ = v ? uint8(psw_ | f) : uint8(psw_ & ~f); }
  void setNZ(uint8 v) { setFlag(kN, v & 0x80); setFlag(kZ, v == 0); }

  auto read(uint16 addr) -> uint8;
  void write(uint16 addr, uint8 data);
  auto readRam(uint16 addr) -> uint8;  // always RAM (no port side effects)
  auto readOp() -> uint8 { return read(pc_++); }

  // ---- S-DSP ----
  uint8 dsp_[128] = {};  // registers 0x00-0x7F (0x80-0xFF mirror)
  int dspAddr_ = 0;
  // per-voice runtime state (ENVX, OUTX, BRR decoder position)
  int envx_[8] = {};  // 11-bit envelope level (0..0x7FF)
  int16 outx_[8] = {};
  uint16 brrOffset_[8] = {};  // BRR sample address in RAM (DIR + srcn*4)
  uint8 brrHeader_[8] = {};
  uint8 brrShift_[8] = {};
  uint8 brrFilter_[8] = {};
  uint8 brrNibble_[8] = {};  // 0..3 of the current BRR block
  int16 brrPrev_[8][2] = {};  // filter history (prev, prev2)

  int8 sample_[2] = {};  // final L/R sample output (signed 8-bit)

  auto dspRead(uint8 index) const -> uint8;
  void dspWrite(uint8 index, uint8 data);
  void voiceKeyOn(int n);
  void decodeBrr(int n);        // decode one BRR sample for voice n
  void runEnvelope(int n);      // one envelope step (ADSR/gain)
  void mixSample();             // sum 8 voices -> sample_[2]

  int counter_ = 0;   // master cycles accumulated toward the next SMP step
  int timerClock_ = 0;  // 8kHz/64kHz timer phase accumulator

  // 64-byte boot ROM at $FFC0-$FFFF (fullsnes "Boot ROM Disassembly").
  static const uint8 bootRom_[64];
};

}  // namespace snes
