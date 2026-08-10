#pragma once

#include <cstdint>

namespace snes {

// Cooperative thread running on the shared master clock (21.47727 MHz NTSC,
// 21.28137 MHz PAL). Each chip with its own clock domain implements this:
//
//   Phase 3: the PPU (one dot = 4 master cycles), driven by the Scheduler.
//   Phase 6: the SMP/DSP pair (own 24.576 MHz oscillator, ratio 21:24);
//            their step() will convert master cycles to audio cycles.
//
// step() advances the thread's own state by masterCycles of the master
// clock; the Scheduler keeps the relative delta between the CPU
// (conductor) and this thread.
class Thread {
 public:
  virtual ~Thread() = default;
  virtual auto step(std::uint64_t masterCycles) -> void = 0;
};

}  // namespace snes
