#include "ppu/ppu.hpp"

#include <cassert>

namespace snes {

// Scanline/frame geometry (NTSC, phase 3; no long/short lines, no PAL):
constexpr uint16 kDotsPerLine = 341;      // H counter 0..340
constexpr uint16 kLinesPerFrame = 262;    // V counter 0..261
constexpr uint8 kCpuVersion = 0x02;       // $4210 bits 3-0 (5A22 version 2)

auto Ppu::step(uint64 masterCycles) -> void {
  assert(masterCycles % 4 == 0);
  for (uint64 i = 0; i < masterCycles / 4; i++) advanceDot();
}

auto Ppu::advanceDot() -> void {
  // Enter dot (H = dot_, V = scanline_) and evaluate its events. Events
  // fire when the counters BECOME the trigger values, so a read of $4212
  // during dot H=0,V=225 already sees the VBlank flag set.
  if (++dot_ == kDotsPerLine) {
    dot_ = 0;
    if (++scanline_ == kLinesPerFrame) {
      scanline_ = 0;
      frame_++;
    }
  }

  // VBlank: set at H=0, V=225; the NMI flag follows half a dot later
  // (H=0.5) — unobservable at dot-granularity sampling, same dot here.
  // End of VBlank at H=0, V=0 auto-clears both.
  if (dot_ == 0 && scanline_ == 225) vblank_ = true;
  if (dot_ == 0 && scanline_ == 0) vblank_ = false;

  // HBlank flag (toggles every scanline, including VBlank): set H=274,
  // cleared at H=1 of the following scanline (so set during H=274..340,0).
  if (dot_ == 274) hblank_ = true;
  if (dot_ == 1) hblank_ = false;

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
}

// ---- MMIO ----

auto Ppu::write4200(uint8 data) -> void {
  nmitimen_ = data;
  // Disabling IRQs (bits 5-4 -> 0) acknowledges them (fullsnes); disabling
  // NMI (bit7 -> 0) does NOT touch the $4210 latch.
  if (((data >> 4) & 3) == 0) irqFlag_ = false;
}

auto Ppu::write4207(uint8 data) -> void { htime_ = (htime_ & 0x100) | data; }
auto Ppu::write4208(uint8 data) -> void { htime_ = (htime_ & 0x0FF) | ((uint16(data) & 1) << 8); }
auto Ppu::write4209(uint8 data) -> void { vtime_ = (vtime_ & 0x100) | data; }
auto Ppu::write420A(uint8 data) -> void { vtime_ = (vtime_ & 0x0FF) | ((uint16(data) & 1) << 8); }

auto Ppu::read4210() -> uint8 {
  uint8 value = (vblank_ ? 0x80 : 0x00) | kCpuVersion;
  vblank_ = false;  // Read/Ack (also auto-cleared at end of VBlank)
  return value;
}

auto Ppu::read4211() -> uint8 {
  uint8 value = irqFlag_ ? 0x80 : 0x00;
  irqFlag_ = false;  // Read/Ack
  // fullsnes exception (read exactly at the trigger instant keeps the
  // flag) is not modeled: unobservable at dot-granularity sampling.
  return value;
}

auto Ppu::read4212() -> uint8 {
  // Live mirror of the counters: never cleared by reading.
  return (vblankPeriod() ? 0x80 : 0x00) | (hblank_ ? 0x40 : 0x00);
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
}

}  // namespace snes
