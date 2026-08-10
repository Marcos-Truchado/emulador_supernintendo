#pragma once

#include <cstdint>

#include "scheduler/thread.hpp"

namespace snes {

// Relative scheduler (byuu model, bsnes.net/articles/schedulers) for the
// base SNES: a single signed 64-bit delta between the CPU and the PPU,
// both running on the shared master clock (21.47727 MHz) — no scaling, the
// PPU advances 4 master cycles per dot.
//
// The CPU is the conductor: every bus access (read/write/idle, including
// opcode fetches) subtracts its waitstate cost from the delta via step();
// sync() then advances the cooperative thread (the PPU) one dot at a time
// until it is within one dot of the CPU. Reads of the PPU timing registers
// ($4210/$4211/$4212) therefore see the PPU in the exact dot of the access.
//
// Phase 6 adds a second pair (SMP/DSP) with a 21:24 clock ratio; the ratio
// conversion lives inside that thread's step().
class Scheduler {
 public:
  // The cooperative thread that sync() drives (the PPU in phase 3).
  explicit Scheduler(Thread& thread) : thread_(thread) {}

  // The CPU consumed masterCycles of the master clock (6 for an internal
  // cycle, 6/8/12 for a bus access depending on the address).
  auto step(uint64 masterCycles) -> void { delta_ += masterCycles; }

  // Advance the cooperative thread until it is within one dot of the CPU.
  auto sync() -> void {
    while (delta_ >= 4) {  // one full dot (4 master cycles) ahead
      thread_.step(4);
      delta_ -= 4;
    }
  }

  // Current CPU-vs-thread delta in master cycles (positive = CPU ahead).
  auto clock() const -> std::int64_t { return delta_; }

  auto reset() -> void { delta_ = 0; }

 private:
  Thread& thread_;
  std::int64_t delta_ = 0;  // CPU master cycles - thread master cycles
};

}  // namespace snes
