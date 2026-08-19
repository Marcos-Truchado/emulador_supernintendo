#include "ppu/ppu.hpp"

#include <cassert>

namespace snes {

// Scanline/frame geometry (NTSC, phase 3; no long/short lines, no PAL):
constexpr uint16 kDotsPerLine = 341;      // H counter 0..340
constexpr uint16 kLinesPerFrame = 262;    // V counter 0..261
constexpr uint8 kCpuVersion = 0x02;       // $4210 bits 3-0 (5A22 version 2)

auto Ppu::step(uint64 masterCycles) -> void {
  assert(masterCycles % 4 == 0);
  if (masterCycles / 4 > 500000) {
    fprintf(stderr, "Ppu::step runaway: %llu dots\n", (unsigned long long)(masterCycles / 4));
    abort();
  }
  for (uint64 i = 0; i < masterCycles / 4; i++) advanceDot();
}

auto Ppu::advanceDot() -> void {
  // Enter dot (H = dot_, V = scanline_) and evaluate its events. Events
  // fire when the counters BECOME the trigger values, so a read of $4212
  // during dot H=0,V=225 already sees the VBlank flag set.
  //
  // The counter starts at dot 0 after power/reset and the increment below
  // skips straight to dot 1, so the H=0 work of the very first line never
  // runs on its own. Run it once here, before the first increment, while
  // the game's own register/OAM writes are already visible.
  if (pendingStart_) {
    pendingStart_ = false;
    startLine();
    sprites_.probe(0);
    fetchSlot(0);
  }

  if (++dot_ == kDotsPerLine) {
    dot_ = 0;
    if (++scanline_ == kLinesPerFrame) {
      scanline_ = 0;
      frame_++;
    }
  }

  // VBlank: set at H=0, V=225; the $4210 NMI latch arms half a dot later
  // (H=0.5) — unobservable at dot-granularity sampling, same dot here. Both
  // clear at H=0, V=0 (snes9x cpuexec.cpp V=0: $4210 = version).
  if (dot_ == 0 && scanline_ == 225) vblank_ = true, nmiLatch_ = true;
  if (dot_ == 0 && scanline_ == 0) vblank_ = false, nmiLatch_ = false;

  // HBlank flag (toggles every scanline, including VBlank): set H=274,
  // cleared at H=1 of the following scanline (so set during H=274..340,0).
  if (dot_ == 274) hblank_ = true;
  if (dot_ == 1) hblank_ = false;
  // Phase 5: the HBlank event drives HDMA transfers on visible lines only
  // (HDMA is idle during VBlank and reloads its table at V=0).
  if (dot_ == 274 && scanline_ < state_.vdisp && hblankSink_) hblankSink_();

  // H/V-timer IRQ flag, register-note semantics (fase3 doc §7.4 — the
  // H==HTIME / V==VTIME comparison, not the +3.5/+2.5 event-table offsets):
  //   mode 1: H-IRQ  at H = HTIME, any V
  //   mode 2: V-IRQ  at V = VTIME, H = 0
  //   mode 3: HV-IRQ at H = HTIME and V = VTIME
  // The flag latches (stays set until read/ack or IRQ disable), so a
  // polling loop sees exactly one trigger per frame.
  if (uint8 mode = (nmitimen_ >> 4) & 3) {
    bool match = mode == 1 ? dot_ == htime_
                : mode == 2 ? scanline_ == vtime_ && dot_ == 0
                : dot_ == htime_ && scanline_ == vtime_;
    if (match) irqFlag_ = true;
  }

  // NMI delivery (phase 3b): edge-detect the internal flag, which is the
  // AND of NMITIMEN.7 and the $4210 latch (fullsnes: "NMI flag gets set
  // when [4200h].7 AND [4210h].7 changes from 0-to-1"). The latch arms at
  // V=225 and clears on a $4210 read, so re-enabling NMITIMEN.7 inside a
  // pending VBlank re-arms the edge only while $4210 is unread (snes9x
  // ppu.cpp "NMI can trigger immediately during VBlank as long as
  // NMI_read ($4210) wasn't cleared"). Raised once per 0->1 edge; the CPU
  // clears its pin on dispatch, so one edge = one NMI.
  bool nmiSource = (nmitimen_ & 0x80) && nmiLatch_;
  if (nmiSource && !nmiEdgePrev_ && nmiPin_) nmiPin_(true);
  nmiEdgePrev_ = nmiSource;

  // IRQ pin: level model, live mirror of the $4211 latch. Driven every
  // dot so an un-acked IRQ stays asserted after the CPU's dispatch clears
  // its own latch and after RTI (re-fires until $4211 is read or the IRQ
  // is disabled — fullsnes level semantics).
  driveIrqPin();

  // Auto-joypad-read (fullsnes "AUTO JOYPAD READ"): once per frame, if
  // $4200 bit0 is set, the read starts between H=32.5 and H=95.5 of the
  // first VBlank scanline (approximated here at H=76) and keeps $4212
  // bit0 set for 4224 master cycles (1056 dots). Many games poll for the
  // 0->1 edge of this bit before trusting $4218-$421F, so it must actually
  // transition — a permanently-0 bit0 hangs any game using that idiom.
  if (dot_ == 76 && scanline_ == 225 && (nmitimen_ & 0x01) && !autoJoyBusy_) {
    autoJoyBusy_ = true;
    autoJoyCyclesLeft_ = 4224;
    if (autoJoySink_) autoJoySink_();
  }
  if (autoJoyBusy_) {
    autoJoyCyclesLeft_ -= 4;  // 4 master cycles per dot
    if (autoJoyCyclesLeft_ <= 0) autoJoyBusy_ = false;
  }

  // ---- phase 4 render pipeline (ppu.hpp cadence) ----
  if (dot_ == 0) {
    // H=0: frame start (V=0 only: latch interlace/overscan, layer/sprite
    // frame state), then the scanline-start of every engine.
    startLine();
    if (scanline_ == 240) renderedFrames_++;
    if (scanline_ > 240) return;  // VBlank: no per-dot work
  }

  // H=0..254: sprite range check every 2 dots.
  if ((dot_ & 1) == 0 && dot_ <= 254) sprites_.probe(dot_ >> 1);

  // H=0..263: one map/character fetch slot per dot.
  if (dot_ <= 263) fetchSlot(dot_ & 7);

  // H=14: prime the per-layer tile shifters.
  if (dot_ == 14) {
    layers_[0].prime();
    layers_[1].prime();
    layers_[2].prime();
    layers_[3].prime();
  }

  // H=14..269: below/above passes, sprite draw, window step, compositor.
  if (dot_ >= 14 && dot_ <= 269) paintDot();

  // H=270: sprite tile fetch.
  if (dot_ == 270) sprites_.loadTiles();
}

// H=0: frame start (V=0 only) + the per-line start of every render engine.
auto Ppu::startLine() -> void {
  if (scanline_ == 0) {
    state_.interlace = io_.interlace;
    state_.overscan = io_.overscan;
    layers_[0].newFrame();
    layers_[1].newFrame();
    layers_[2].newFrame();
    layers_[3].newFrame();
    sprites_.newFrame();
    if (frameStartSink_) frameStartSink_();  // phase 5: HDMA table reload
  }
  mosaic_.lineStart();
  layers_[0].lineStart();
  layers_[1].lineStart();
  layers_[2].lineStart();
  layers_[3].lineStart();
  sprites_.lineStart();
  window_.lineStart();
  composer_.lineStart();
}

// ---- interrupt delivery sinks (phase 3b) ----

auto Ppu::driveIrqPin() -> void {
  if (irqPin_) irqPin_(irqFlag_);
}

// ---- MMIO ----

auto Ppu::write4200(uint8 data) -> void {
  nmitimen_ = data;
  // Disabling IRQs (bits 5-4 -> 0) acknowledges them (fullsnes); disabling
  // NMI (bit7 -> 0) does NOT touch the $4210 latch.
  if (((data >> 4) & 3) == 0) irqFlag_ = false;
  driveIrqPin();  // ack drops the IRQ pin without a dot advance
}

auto Ppu::write4207(uint8 data) -> void { htime_ = (htime_ & 0x100) | data; }
auto Ppu::write4208(uint8 data) -> void { htime_ = (htime_ & 0x0FF) | ((uint16(data) & 1) << 8); }
auto Ppu::write4209(uint8 data) -> void { vtime_ = (vtime_ & 0x100) | data; }
auto Ppu::write420A(uint8 data) -> void { vtime_ = (vtime_ & 0x0FF) | ((uint16(data) & 1) << 8); }

auto Ppu::read4210() -> uint8 {
  uint8 value = (nmiLatch_ ? 0x80 : 0x00) | kCpuVersion;
  nmiLatch_ = false;  // Read/Ack (also auto-cleared at end of VBlank)
  return value;
}

auto Ppu::read4211() -> uint8 {
  uint8 value = irqFlag_ ? 0x80 : 0x00;
  irqFlag_ = false;  // Read/Ack
  driveIrqPin();     // ack drops the IRQ pin without a dot advance
  // fullsnes exception (read exactly at the trigger instant keeps the
  // flag) is not modeled: unobservable at dot-granularity sampling.
  return value;
}

auto Ppu::read4212() -> uint8 {
  // Live mirror of the counters: never cleared by reading.
  return (vblankPeriod() ? 0x80 : 0x00) | (hblank_ ? 0x40 : 0x00) | (autoJoyBusy_ ? 0x01 : 0x00);
}

// ---- power / reset ----

auto Ppu::power() -> void {
  nmitimen_ = 0;
  // fullsnes I/O map right column: 4207h=(FFh), 4208h=(01h), i.e. 0x1FF.
  htime_ = 0x1FF;
  vtime_ = 0x1FF;
  dot_ = 0;
  scanline_ = 0;
  frame_ = 0;
  vblank_ = false;
  irqFlag_ = false;
  hblank_ = false;
  nmiEdgePrev_ = false;
  nmiLatch_ = false;
  autoJoyBusy_ = false;
  autoJoyCyclesLeft_ = 0;
  pendingStart_ = true;
  resetRegisters();
  driveIrqPin();
}

auto Ppu::reset() -> void {
  // Soft reset: unbracketed registers only. HTIME/VTIME (bracketed)
  // keep whatever the game left in them; NMITIMEN clears (and acks IRQs).
  nmitimen_ = 0;
  irqFlag_ = false;
  dot_ = 0;
  scanline_ = 0;
  frame_ = 0;
  vblank_ = false;
  hblank_ = false;
  nmiEdgePrev_ = false;
  autoJoyBusy_ = false;
  autoJoyCyclesLeft_ = 0;
  pendingStart_ = true;
  resetRegisters();
  driveIrqPin();
}

}  // namespace snes
