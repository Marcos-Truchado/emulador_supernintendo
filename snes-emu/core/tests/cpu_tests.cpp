#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "cpu/cpu65816.hpp"

namespace {

// Minimal 64K memory for CPU-only tests (bank 0).
struct TestMemory : snes::Memory {
  snes::uint8 data[0x10000] = {};
  auto read(snes::uint24 address) -> snes::uint8 override {
    return data[address & 0xffff];
  }
  auto write(snes::uint24 address, snes::uint8 data) -> void override {
    this->data[address & 0xffff] = data;
  }
};

// No cooperative thread in CPU-only tests: the scheduler just accumulates.
struct DummyThread : snes::Thread {
  auto step(snes::uint64 masterCycles) -> void override {
    (void)masterCycles;
  }
};

void poke(TestMemory& m, snes::uint24 addr, snes::uint8 v) {
  m.data[addr & 0xffff] = v;
}
void poke16(TestMemory& m, snes::uint24 addr, snes::uint16 v) {
  m.data[addr & 0xffff] = v & 0xff;
  m.data[(addr & 0xffff) + 1] = v >> 8;
}

}  // namespace

namespace snes {

TEST_CASE("cpu: power and reset state") {
  TestMemory mem;
  DummyThread thread;
  Scheduler scheduler(thread);
  Cpu65816 cpu(mem, scheduler);
  cpu.power();
  CHECK(cpu.emulation());
  CHECK(cpu.flagP() == 0x34);
  CHECK(cpu.pc() == 0);
  CHECK(cpu.stack() == 0x01ff);

  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  CHECK(cpu.pc() == 0x008000);
  CHECK(cpu.emulation());
  CHECK(cpu.flagP() == 0x34);
  CHECK((cpu.flagP() & 0x30) == 0x30);  // M=X=1 forced in E mode
  CHECK(cpu.xReg() == 0);
  CHECK(cpu.yReg() == 0);
}

TEST_CASE("cpu: WAI parks until NMI wakes it") {
  TestMemory mem;
  DummyThread thread;
  Scheduler scheduler(thread);
  Cpu65816 cpu(mem, scheduler);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0xcb);  // wai
  poke(mem, 0x8001, 0xea);  // nop (must NOT execute while parked)
  poke16(mem, 0xfffa, 0x0100);  // E-mode NMI vector

  CHECK(cpu.execute() == 1);        // wai: opcode fetch + flag
  CHECK(cpu.pc() == 0x008001);
  CHECK(cpu.waiting());
  CHECK(cpu.execute() == 1);        // parked: 1 idle/step
  CHECK(cpu.pc() == 0x008001);
  CHECK(cpu.execute() == 1);

  cpu.setNmi(true);
  CHECK(cpu.execute() == 8);        // wake: 1 trailing idle + 7 (E-mode NMI)
  CHECK_FALSE(cpu.waiting());
  CHECK(cpu.pc() == 0x000100);
  CHECK((cpu.flagP() & 0x04) == 0x04);        // I=1 after interrupt
  CHECK(cpu.stack() == 0x01fc);     // 3 pushes in E mode
  CHECK((mem.data[0x01fd] & 0x10) == 0);  // B flag pushed as 0
  CHECK(mem.data[0x01fe] == 0x01);  // PCL
  CHECK(mem.data[0x01ff] == 0x80);  // PCH
}

TEST_CASE("cpu: NMI in native mode pushes 4 bytes") {
  TestMemory mem;
  DummyThread thread;
  Scheduler scheduler(thread);
  Cpu65816 cpu(mem, scheduler);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0x18);  // clc
  poke(mem, 0x8001, 0xfb);  // xce -> E=0
  poke(mem, 0x8002, 0xea);  // nop
  poke16(mem, 0xffea, 0x0100);  // native NMI vector

  cpu.execute();             // clc
  cpu.execute();             // xce
  CHECK_FALSE(cpu.emulation());

  cpu.setNmi(true);
  CHECK(cpu.execute() == 8);        // native NMI: 2 prelude + 4 push + 2 read
  CHECK(cpu.pc() == 0x000100);
  CHECK(cpu.stack() == 0x01fb);     // 4 pushes
  CHECK(mem.data[0x01ff] == 0x00);  // PBR (deepest)
  CHECK(mem.data[0x01fe] == 0x80);  // PCH
  CHECK(mem.data[0x01fd] == 0x02);  // PCL
  CHECK(mem.data[0x01fc] == 0x35);  // P (C=1 from XCE)
}

TEST_CASE("cpu: IRQ is gated by the I flag") {
  TestMemory mem;
  DummyThread thread;
  Scheduler scheduler(thread);
  Cpu65816 cpu(mem, scheduler);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0xea);  // nop (I=1 in E mode: IRQ must not fire)
  poke(mem, 0x8001, 0x58);  // cli
  poke(mem, 0x8002, 0xea);  // nop (IRQ fires here)
  poke16(mem, 0xfffe, 0x0200);  // E-mode IRQ vector (shared with BRK)

  cpu.setIrq(true);
  cpu.execute();             // nop: IRQ ignored (I=1)
  CHECK(cpu.pc() == 0x008001);
  CHECK((cpu.flagP() & 0x04) == 0x04);

  cpu.execute();             // cli: I=0
  CHECK((cpu.flagP() & 0x04) == 0);

  CHECK(cpu.execute() == 7); // IRQ fires (E mode): 2 prelude + 3 push + 2 read
  CHECK(cpu.pc() == 0x000200);
  CHECK((cpu.flagP() & 0x04) == 0x04); // I set again
}

TEST_CASE("cpu: NMI takes priority over IRQ") {
  TestMemory mem;
  DummyThread thread;
  Scheduler scheduler(thread);
  Cpu65816 cpu(mem, scheduler);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0x58);  // cli
  poke(mem, 0x8001, 0xea);  // nop
  poke16(mem, 0xfffa, 0x0100);  // E-mode NMI vector
  poke16(mem, 0xfffe, 0x0200);  // E-mode IRQ vector

  cpu.execute();             // cli
  cpu.setNmi(true);
  cpu.setIrq(true);
  cpu.execute();
  CHECK(cpu.pc() == 0x000100);   // NMI wins
  CHECK((cpu.flagP() & 0x04) == 0x04);     // I=1 after NMI: held IRQ cannot dispatch

  poke(mem, 0x0100, 0x58);  // cli at NMI handler
  poke(mem, 0x0101, 0xea);  // nop
  cpu.execute();             // cli (I=0)
  CHECK(cpu.execute() == 7); // held IRQ fires (E mode)
  CHECK(cpu.pc() == 0x000200);
}

TEST_CASE("cpu: canonical cycle counts") {
  struct Case {
    const char* name;
    std::vector<snes::uint8> prog;
    int cycles;
    snes::uint8 p = 0x34;  // P override (default = post-reset)
  };
  const std::vector<Case> cases = {
      {"nop", {0xea}, 2},
      {"lda #imm", {0xa9, 0x12}, 2},
      {"lda dp", {0xa5, 0x34}, 3},
      {"lda abs", {0xad, 0x00, 0x20}, 4},
      {"lda abs,x (8-bit X)", {0xbd, 0x00, 0x20}, 4},
      {"lda (dp),y (Y=0)", {0xb1, 0x34}, 5},
      {"jmp abs", {0x4c, 0x00, 0x80}, 3},
      {"jsr abs", {0x20, 0x00, 0x80}, 6},
      {"rts", {0x60}, 6},
      {"bne not taken", {0xd0, 0x00}, 2, 0x36},  // Z=1: stays untaken
      {"bne taken", {0xd0, 0x02}, 3},
      {"xce", {0xfb}, 2},
      {"brk (E mode)", {0x00}, 7},
  };
  for (const auto& c : cases) {
    TestMemory mem;
    DummyThread thread;
    Scheduler scheduler(thread);
    Cpu65816 cpu(mem, scheduler);
    cpu.power();
    for (size_t i = 0; i < c.prog.size(); i++) poke(mem, 0x8000 + i, c.prog[i]);
    poke16(mem, 0xfffc, 0x8000);
    cpu.reset();
    cpu.setFlagP(c.p);
    cpu.setPc(0x8000);
    INFO(c.name);
    CHECK(cpu.execute() == (snes::uint64)c.cycles);
  }
}

TEST_CASE("cpu: STP halts forever, WAI parked cycles") {
  TestMemory mem;
  DummyThread thread;
  Scheduler scheduler(thread);
  Cpu65816 cpu(mem, scheduler);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0xdb);  // stp

  CHECK(cpu.execute() == 1);        // STP: opcode fetch + flag
  CHECK(cpu.stopped());
  CHECK(cpu.execute() == 1);        // parked idle
  CHECK(cpu.execute() == 1);
  CHECK(cpu.pc() == 0x008001);
}

}  // namespace snes
