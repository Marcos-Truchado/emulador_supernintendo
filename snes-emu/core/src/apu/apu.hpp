#pragma once

#include <array>
#include <cstddef>

#include "scheduler/thread.hpp"
#include "serialize/serialize.hpp"
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
  auto readPort(int index) const -> uint8;

  // readback for tests
  auto ram(uint16 address) const -> uint8;
  auto dspRegister(uint8 index) const -> uint8;
  auto sampleLeft() const -> int16 { return sample_[0]; }
  auto sampleRight() const -> int16 { return sample_[1]; }
  // test-only setup helpers
  auto setRam(uint16 address, uint8 data) -> void { ram_[address] = data; }
  auto setDspRegister(uint8 index, uint8 data) -> void { dspWrite(index, data); }
  auto setControl(uint8 data) -> void { write(0x00F1, data); }
  auto inputPort(int index) const -> uint8 { return apuIn_[index]; }   // CPU->SMP latch
  auto setOutputPort(int index, uint8 data) -> void { port_[0x04 + index] = data; }
  auto setTimerDivider(int n, uint8 data) -> void { write(uint8(0xFA + n), data); }
  auto timerOut(int n) const -> uint8 { return timerOut_[n] & 0x0F; }
  auto pc() const -> uint16 { return pc_; }
  auto aReg() const -> uint8 { return a_; }
  auto xReg() const -> uint8 { return x_; }
  auto yReg() const -> uint8 { return y_; }
  auto spReg() const -> uint8 { return sp_; }
  auto pswReg() const -> uint8 { return psw_; }

  // ---- audio output (phase 7) ----
  // Number of DSP samples (stereo int16, interleaved L/R) waiting in the
  // ring buffer; the frontend pulls them with readAudio().
  auto audioAvailable() const -> size_t { return audioCount_; }
  auto readAudio(int16* buffer, size_t count) -> size_t;

  // ---- save states (phase 7) ----
  auto serialize(Writer& w) const -> void;
  auto deserialize(Reader& r) -> void;

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
  uint8 timerOut_[3] = {};  // TnOUT (4-bit, reset on read)
  int timerCounter_[3] = {};  // internal counter toward the next output step

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
  uint8 envMode_[8] = {};  // 0=attack 1=decay 2=sustain 3=release
  int envRaw_[8] = {};     // unclamped envelope (two-slope GAIN detection)
  int16 outx_[8] = {};
  uint16 brrOffset_[8] = {};  // BRR sample address in RAM (DIR + srcn*4)
  uint8 brrHeader_[8] = {};
  uint8 brrShift_[8] = {};
  uint8 brrFilter_[8] = {};
  uint8 brrNibble_[8] = {};  // 0..3 of the current BRR block
  int16 brrPrev_[8][2] = {};  // filter history (prev, prev2)

  int16 sample_[2] = {};  // final L/R sample output (signed 16-bit)

  int clockCounter_ = 0;  // DSP envelope clock (0x7800-period down counter)

  // DSP envelope counter rate/offset tables (fullsnes, ares).
  static constexpr int kCounterRate[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1,
  };
  static constexpr int kCounterOffset[32] = {
    0, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
    0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 0, 0,
  };
  void counterTick() {
    if (!clockCounter_) clockCounter_ = 30720;
    clockCounter_--;
  }
  bool counterPoll(int rate) const {
    if (rate == 0) return false;
    return (clockCounter_ + kCounterOffset[rate]) % kCounterRate[rate] == 0;
  }

  auto dspRead(uint8 index) const -> uint8;
  void dspWrite(uint8 index, uint8 data);
  void voiceKeyOn(int n);
  void decodeBrr(int n);        // decode one BRR sample for voice n
  void runEnvelope(int n);      // one envelope step (ADSR/gain)
  void mixSample();             // sum 8 voices -> sample_[2]
  void pushSample();            // append sample_ to the audio ring buffer
  void tickTimers(int cycles);  // advance the three SMP timers by cycles
  void tickTimer(int n);        // one clock tick of a single timer

  int counter_ = 0;   // master cycles accumulated toward the next SMP step
  int sampleClock_ = 0;  // SMP cycles accumulated toward the next DSP sample

  // SMP timers: timer 2 uses a 16-cycle clock, timers 0/1 a 128-cycle clock.
  int timerClock16_ = 0;
  int timerClock128_ = 0;

  // DSP sample ring buffer (stereo int16, interleaved). Fixed capacity; the
  // frontend drains it once per frame, so it never fills in practice.
  static constexpr size_t kAudioBuf = 32768;
  std::array<int16, kAudioBuf> audioBuf_ = {};
  size_t audioWr_ = 0;
  size_t audioRd_ = 0;
  size_t audioCount_ = 0;

  // 64-byte boot ROM at $FFC0-$FFFF (fullsnes "Boot ROM Disassembly").
  static const uint8 bootRom_[64];
};

}  // namespace snes
