#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "scheduler/scheduler.hpp"
#include "scheduler/thread.hpp"

namespace {

// Counting thread: records every step() call and the master cycles it ran.
struct CountingThread : snes::Thread {
  snes::uint64 steps = 0;
  snes::uint64 master = 0;
  auto step(snes::uint64 masterCycles) -> void override {
    steps++;
    master += masterCycles;
  }
};

}  // namespace

namespace snes {

TEST_CASE("scheduler: CPU leads, thread catches up one dot at a time") {
  CountingThread thread;
  Scheduler scheduler(thread);

  scheduler.step(4);  // one full dot ahead
  CHECK(thread.steps == 0);
  scheduler.sync();
  CHECK(thread.steps == 1);   // one dot (4 master cycles)
  CHECK(thread.master == 4);
  CHECK(scheduler.clock() == 0);

  // 10 master cycles = 2 dots + 2 residual master cycles.
  scheduler.step(10);
  scheduler.sync();
  CHECK(thread.steps == 3);
  CHECK(thread.master == 12);
  CHECK(scheduler.clock() == 2);  // PPU within one dot of the CPU

  // Residual 2 + 2 = a full dot: the thread advances exactly once.
  scheduler.step(2);
  scheduler.sync();
  CHECK(thread.steps == 4);
  CHECK(thread.master == 16);
  CHECK(scheduler.clock() == 0);

  // A single small step below a dot never advances the thread.
  Scheduler half(thread);
  half.step(3);
  half.sync();
  CHECK(thread.steps == 4);
  CHECK(half.clock() == 3);
}

TEST_CASE("scheduler: reset clears the delta") {
  CountingThread thread;
  Scheduler scheduler(thread);

  scheduler.step(64);
  scheduler.sync();
  CHECK(thread.master == 64);
  scheduler.reset();
  CHECK(scheduler.clock() == 0);
  scheduler.sync();  // no catch-up owed
  CHECK(thread.master == 64);
}

TEST_CASE("scheduler: accumulated master cycles match the CPU total") {
  CountingThread thread;
  Scheduler scheduler(thread);

  uint64 cpuMaster = 0;
  for (int i = 0; i < 1000; i++) {
    uint64 access = (i % 3 == 0) ? 6 : (i % 3 == 1) ? 8 : 12;
    scheduler.step(access);
    cpuMaster += access;
    scheduler.sync();
  }
  // The thread has consumed every full dot; the remainder is < 4.
  CHECK(thread.master + scheduler.clock() == cpuMaster);
  CHECK(scheduler.clock() >= 0);
  CHECK(scheduler.clock() < 4);
}

}  // namespace snes
