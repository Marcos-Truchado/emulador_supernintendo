#pragma once

#include <functional>

#include "scheduler/thread.hpp"
#include "snes/snes.hpp"

namespace snes {

// Picture Processing Unit — Phase 3: timing only, no rendering.
//
// Dot-accurate model of the SNES H/V counters and the events games depend
// on before any pixel is drawn. Verified against fullsnes (noSns v1.6)
// "SNES Timing Oscillators / H/V Counters / H/V Events" and the I/O map:
//
//   dot       = 4 master cycles (21.47727 MHz / 4)
//   scanline  = 341 dots (1364 master cycles), H counter 0..340
//   frame     = 262 scanlines (NTSC), V counter 0..261
//
// Events (fullsnes "SNES Timing H/V Events" table):
//   H=0,   V=225  set VBlank flag ($4212 bit7 / $4210 bit7 latch)
//   H=0.5, V=225  set NMI flag (half a dot later; unobservable at the
//                 dot-granularity sampling of phase 3, same dot here)
//   H=0,   V=0    clear VBlank flag + auto-ack (reset) the NMI flag
//   H=274         set HBlank flag ($4212 bit6)
//   H=1           clear HBlank flag (of the next scanline)
//
// Registers:
//   $4210 RDNMI : latched NMI flag (Read/Ack) — cleared by reading AND
//                 auto-cleared at end of VBlank. NOT a live mirror (§7.5).
//   $4211 TIMEUP: latched H/V-IRQ flag (Read/Ack) — set when the H/V
//                 counters match HTIME/VTIME per the $4200 mode bits
//                 (register-note semantics, §7.4); cleared by reading and
//                 by disabling IRQs via $4200 bits 5-4.
//   $4212 HVBJOY: LIVE mirror of the V/H counters — never clear-on-read
//                 (§7.6). bit7 = V>=225, bit6 = H in 274..340 or 0.
//   $4200 NMITIMEN / $4207-$420A HTIME/VTIME written from the bus.
//
// Power-on values follow the fullsnes I/O map right column (HTIME/VTIME =
// FFh/01h, i.e. 0x1FF — see update.md erratum 3: fase3 doc §7.7 said 0).
// On soft reset, bracketed registers keep their values; only NMITIMEN
// clears (00h, unbracketed), which also acknowledges any pending IRQ.
//
// Out of scope (phase 3, documented): rendering, forced blank, counter
// latches ($213C-$213F), joypad auto-read, OAMADD reload, HDMA, DRAM
// refresh (40 master cycles at H=133.5), long/short scanlines, PAL, and
// interlace.
//
// Phase 3b adds the notification mechanism (fase3 doc §11.8, update.md):
// the PPU owns the exact 65816 semantics and drives two sink pins toward
// the CPU via std::function hooks wired by the System:
//   NMI — edge-detected internally: raised once per 0->1 edge of
//   "NMITIMEN.7 AND $4210-latch" (fullsnes "NMI flag gets set when
//   [4200h].7 AND [4210h].7 changes from 0-to-1"); the CPU clears the pin
//   on dispatch, so one edge = one NMI, and re-enabling NMITIMEN.7 inside
//   a pending VBlank re-arms the edge ("old NMI mis-executed", fullsnes).
//   IRQ — level-sensitive: the pin is a live mirror of the $4211 latch,
//   re-driven every dot and whenever the latch is cleared without a dot
//   advance (read $4211, disable via $4200 bits 5-4, power/reset), so an
//   un-acked IRQ re-fires after RTI (I flag gating lives in the CPU).
class Ppu : public Thread {
 public:
  Ppu() = default;

  // Thread: advance masterCycles (must be a multiple of 4) one dot at a
  // time; the Scheduler calls this with 4.
  auto step(uint64 masterCycles) -> void override;

  // Power-on and soft-reset register/counter values (fullsnes I/O map).
  auto power() -> void;
  auto reset() -> void;

  // ---- MMIO from the bus (CPU side) ----
  auto write4200(uint8 data) -> void;  // NMITIMEN
  auto write4207(uint8 data) -> void;  // HTIMEL
  auto write4208(uint8 data) -> void;  // HTIMEH
  auto write4209(uint8 data) -> void;  // VTIMEL
  auto write420A(uint8 data) -> void;  // VTIMEH
  auto read4210() -> uint8;            // RDNMI (Read/Ack)
  auto read4211() -> uint8;            // TIMEUP (Read/Ack)
  auto read4212() -> uint8;            // HVBJOY (live)

  // ---- interrupt delivery sinks (phase 3b; wired by System) ----
  auto setNmiPin(std::function<void(bool)> sink) -> void { nmiPin_ = std::move(sink); }
  auto setIrqPin(std::function<void(bool)> sink) -> void { irqPin_ = std::move(sink); }

  // ---- counters / flags (tests, runner, later phases) ----
  auto dot() const -> uint16 { return dot_; }            // H counter 0..340
  auto scanline() const -> uint16 { return scanline_; }  // V counter 0..261
  auto frame() const -> uint64 { return frame_; }
  auto vblankFlag() const -> bool { return vblank_; }     // $4210 bit7 latch
  auto irqFlag() const -> bool { return irqFlag_; }       // $4211 bit7 latch
  auto hblank() const -> bool { return hblank_; }         // live $4212 bit6
  auto vblankPeriod() const -> bool { return scanline_ >= 225; }
  auto nmitimen() const -> uint8 { return nmitimen_; }

 private:
  auto advanceDot() -> void;  // one dot = 4 master cycles
  auto driveIrqPin() -> void; // pin := live mirror of the $4211 latch

  uint16 dot_ = 0;        // H counter 0..340
  uint16 scanline_ = 0;   // V counter 0..261 (NTSC)
  uint64 frame_ = 0;

  uint8 nmitimen_ = 0;    // $4200
  uint16 htime_ = 0;      // $4207/$4208
  uint16 vtime_ = 0;      // $4209/$420A

  bool vblank_ = false;   // $4210 bit7 latched (Read/Ack)
  bool irqFlag_ = false;  // $4211 bit7 latched (Read/Ack)
  bool hblank_ = false;   // $4212 bit6 live

  bool nmiEdgePrev_ = false;  // NMI edge-detect: previous dot's "NMITIMEN.7
                              // AND $4210-latch" sample
  std::function<void(bool)> nmiPin_;  // raised on the 0->1 NMI edge
  std::function<void(bool)> irqPin_;  // level mirror of irqFlag_
};

}  // namespace snes
