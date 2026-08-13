#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "snes/snes.hpp"
#include "apu/apu.hpp"
#include "ppu/ppu.hpp"
#include "scheduler/scheduler.hpp"

using namespace snes;

// Build a minimal LoROM cartridge (the DMA tests only read/write WRAM, but the
// Bus constructor needs a cartridge).
static Cartridge makeCart() {
  std::vector<uint8> rom(0x8000, 0xEA);
  rom[0x7FD5] = 0x20;  // LoROM header
  Cartridge cart;
  std::string error;
  if (!cart.load(std::move(rom), &error)) FAIL(error);
  return cart;
}

TEST_CASE("dma: channel register file ($4300-$437B)") {
  Cartridge cart = makeCart();
  Ppu ppu;
  Apu apu;
  Scheduler scheduler(ppu);
  Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  // DMAP0, BBAD0, A1T0, DAS0 for channel 0.
  bus.write(0x004300, 0x00);  // DMAP0: A->B, increment, 1-byte unit
  bus.write(0x004301, 0x80);  // BBAD0 = $2180 (WRAM port)
  bus.write(0x004302, 0x34);  // A1T0L
  bus.write(0x004303, 0x12);  // A1T0H
  bus.write(0x004304, 0x7E);  // A1B0
  bus.write(0x004305, 0xAB);  // DAS0L
  bus.write(0x004306, 0x00);  // DAS0H

  CHECK(bus.dmaRegister(0x00) == 0x00);
  CHECK(bus.dmaRegister(0x01) == 0x80);
  CHECK(bus.dmaRegister(0x02) == 0x34);
  CHECK(bus.dmaRegister(0x03) == 0x12);
  CHECK(bus.dmaRegister(0x04) == 0x7E);
  CHECK(bus.dmaRegister(0x05) == 0xAB);
  CHECK(bus.dmaRegister(0x06) == 0x00);

  // Channel 1 lives at $4310-$431F and is independent.
  bus.write(0x004310, 0x08);  // DMAP1: fixed A-bus step, 2-byte unit (xx,xx)
  CHECK(bus.dmaRegister(0x10) == 0x08);
  CHECK(bus.dmaRegister(0x00) == 0x00);  // channel 0 untouched
}

TEST_CASE("dma: GP-DMA transfers N bytes in N*8 master cycles") {
  snes::Cartridge cart = makeCart();
  snes::Ppu ppu;
  ppu.power();
  snes::Apu apu;
  snes::Scheduler scheduler(ppu);
  snes::Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  constexpr int N = 8;
  for (int i = 0; i < N; i++) bus.write(0x7E0000 + i, uint8(0x11 * (i + 1)));

  // Destination: WRAM port $2180 with wramAddr_ = 0x1000.
  bus.write(0x002181, 0x00);
  bus.write(0x002182, 0x10);
  bus.write(0x002183, 0x00);
  CHECK(bus.wramAddress() == 0x1000);

  // Channel 0: A-bus WRAM ($7E0000) -> B-bus WRAM port ($2180), 1-byte units.
  bus.write(0x004300, 0x00);  // DMAP0: A->B, increment, 1-byte unit
  bus.write(0x004301, 0x80);  // BBAD0 = $2180
  bus.write(0x004302, 0x00);  // A1T0L
  bus.write(0x004303, 0x00);  // A1T0H
  bus.write(0x004304, 0x7E);  // A1B0
  bus.write(0x004305, N & 0xFF);  // DAS0L
  bus.write(0x004306, N >> 8);    // DAS0H

  const uint64 before = uint64(ppu.scanline()) * 341 + ppu.dot();

  bus.write(0x00420B, 0x01);  // MDMAEN: start channel 0 (blocks)

  // Bytes landed in WRAM at the destination.
  for (int i = 0; i < N; i++) CHECK(bus.wram()[0x1000 + i] == uint8(0x11 * (i + 1)));
  CHECK(bus.wramAddress() == 0x1000 + N);  // auto-incremented
  CHECK(bus.cpuRegister(0x0B) == 0x00);    // MDMAEN cleared on completion

  // Cycle count: N bytes * 8 master = 2N dots.
  const uint64 after = uint64(ppu.scanline()) * 341 + ppu.dot();
  CHECK(after - before == 2 * N);
}

TEST_CASE("dma: GP-DMA unit pattern and A-bus step modes") {
  snes::Cartridge cart = makeCart();
  snes::Ppu ppu;
  snes::Apu apu;
  snes::Scheduler scheduler(ppu);
  snes::Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  // Source WRAM: 8 bytes 0x11..0x88.
  for (int i = 0; i < 8; i++) bus.write(0x7E0000 + i, uint8(0x11 * (i + 1)));

  SUBCASE("4-byte unit to APU ports (xx, xx+1, xx+2, xx+3), increment") {
    bus.write(0x004300, 0x04);  // unit 4 (4 bytes), increment, A->B
    bus.write(0x004301, 0x40);  // BBAD0 = $2140 (APU port 0)
    bus.write(0x004302, 0x00);
    bus.write(0x004303, 0x00);
    bus.write(0x004304, 0x7E);
    bus.write(0x004305, 0x08);  // 8 bytes = 2 units
    bus.write(0x004306, 0x00);

    bus.write(0x00420B, 0x01);

    // Unit 1: apu[0..3] = 0x11,0x22,0x33,0x44; unit 2: apu[0..3] = 0x55..0x88.
    // DMA to $2140-$2143 writes the SMP input latches.
    CHECK(apu.inputPort(0) == 0x55);
    CHECK(apu.inputPort(1) == 0x66);
    CHECK(apu.inputPort(2) == 0x77);
    CHECK(apu.inputPort(3) == 0x88);
  }

  SUBCASE("fixed A-bus step re-reads the same source byte") {
    bus.write(0x004300, 0x0C);  // unit 4, fixed step (bits 4-3 = 1), A->B
    bus.write(0x004301, 0x40);  // BBAD0 = $2140
    bus.write(0x004302, 0x00);
    bus.write(0x004303, 0x00);
    bus.write(0x004304, 0x7E);
    bus.write(0x004305, 0x08);
    bus.write(0x004306, 0x00);

    bus.write(0x00420B, 0x01);

    // Both units read source byte 0 (0x11) four times.
    CHECK(apu.inputPort(0) == 0x11);
    CHECK(apu.inputPort(1) == 0x11);
    CHECK(apu.inputPort(2) == 0x11);
    CHECK(apu.inputPort(3) == 0x11);
  }
}

TEST_CASE("dma: GP-DMA writes VRAM through the PPU B-bus ($2118/$2119)") {
  Cartridge cart = makeCart();
  Ppu ppu;
  ppu.power();
  Apu apu;
  Scheduler scheduler(ppu);
  Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  // Two source words in WRAM: 0x1234, 0x5678.
  bus.write(0x7E0000, 0x34);
  bus.write(0x7E0001, 0x12);
  bus.write(0x7E0002, 0x78);
  bus.write(0x7E0003, 0x56);

  // VRAM target: word 0, linear increment on the high byte.
  bus.write(0x002115, 0x80);  // VMAIN: increment 1 on high byte
  bus.write(0x002116, 0x00);  // VMADDL
  bus.write(0x002117, 0x00);  // VMADDH

  // Channel 0: 2-byte unit (xx, xx+1) -> $2118/$2119, 4 bytes total.
  bus.write(0x004300, 0x01);  // DMAP0: A->B, increment, 2-byte unit
  bus.write(0x004301, 0x18);  // BBAD0 = $2118 (VMDATAL)
  bus.write(0x004302, 0x00);
  bus.write(0x004303, 0x00);
  bus.write(0x004304, 0x7E);
  bus.write(0x004305, 0x04);
  bus.write(0x004306, 0x00);

  bus.write(0x00420B, 0x01);

  CHECK(ppu.vramRead(0) == 0x1234);
  CHECK(ppu.vramRead(1) == 0x5678);
}

TEST_CASE("dma: HDMA direct mode writes a per-line gradient") {
  Cartridge cart = makeCart();
  Ppu ppu;
  ppu.power();
  Apu apu;
  Scheduler scheduler(ppu);
  Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  // Wire the PPU frame/HBlank events to the HDMA engine.
  ppu.setFrameStartSink([&]() { bus.hdmaReset(); });
  ppu.setHblankSink([&]() { bus.hdmaRun(); });

  // HDMA table at $7E1000: repeat 3 lines (0x83), data 0x0F/0x0E/0x0D,
  // then 0x00 to terminate.
  bus.write(0x7E1000, 0x83);
  bus.write(0x7E1001, 0x0F);
  bus.write(0x7E1002, 0x0E);
  bus.write(0x7E1003, 0x0D);
  bus.write(0x7E1004, 0x00);

  // Channel 0: direct mode, A->B, 1-byte unit, BBAD = $2100.
  bus.write(0x004300, 0x00);  // DMAP0
  bus.write(0x004301, 0x00);  // BBAD0 = $2100
  bus.write(0x004302, 0x00);  // A1T0L
  bus.write(0x004303, 0x10);  // A1T0H (table at $7E1000)
  bus.write(0x004304, 0x7E);  // A1B0
  bus.write(0x00420C, 0x01);  // HDMAEN channel 0

  constexpr uint64 kLine = 341 * 4;
  ppu.step(kLine);  // line 0 HBlank -> 0x0F
  CHECK(bus.ppuRegister(0x00) == 0x0F);
  ppu.step(kLine);  // line 1 HBlank -> 0x0E
  CHECK(bus.ppuRegister(0x00) == 0x0E);
  ppu.step(kLine);  // line 2 HBlank -> 0x0D
  CHECK(bus.ppuRegister(0x00) == 0x0D);
  ppu.step(kLine);  // line 3 HBlank -> 0x00 terminates the channel
  CHECK(bus.cpuRegister(0x0C) == 0x00);
}

TEST_CASE("dma: HDMA single mode transfers once then pauses") {
  Cartridge cart = makeCart();
  Ppu ppu;
  ppu.power();
  Apu apu;
  Scheduler scheduler(ppu);
  Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  ppu.setFrameStartSink([&]() { bus.hdmaReset(); });
  ppu.setHblankSink([&]() { bus.hdmaRun(); });

  // Table: single 0x02 (transfer once, pause 1 line), data 0x0F, then 0x00.
  bus.write(0x7E1000, 0x02);
  bus.write(0x7E1001, 0x0F);
  bus.write(0x7E1002, 0x00);

  bus.write(0x004300, 0x00);  // direct, A->B, 1-byte unit
  bus.write(0x004301, 0x00);  // BBAD0 = $2100
  bus.write(0x004302, 0x00);
  bus.write(0x004303, 0x10);
  bus.write(0x004304, 0x7E);
  bus.write(0x00420C, 0x01);

  constexpr uint64 kLine = 341 * 4;
  ppu.step(kLine);  // line 0: transfer 0x0F
  CHECK(bus.ppuRegister(0x00) == 0x0F);
  ppu.step(kLine);  // line 1: pause (no transfer), value stays
  CHECK(bus.ppuRegister(0x00) == 0x0F);
  ppu.step(kLine);  // line 2: fetch 0x00 -> terminate
  CHECK(bus.cpuRegister(0x0C) == 0x00);
}

TEST_CASE("dma: HDMA indirect mode follows the table pointer") {
  Cartridge cart = makeCart();
  Ppu ppu;
  ppu.power();
  Apu apu;
  Scheduler scheduler(ppu);
  Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  ppu.setFrameStartSink([&]() { bus.hdmaReset(); });
  ppu.setHblankSink([&]() { bus.hdmaRun(); });

  // Table at $7E1000: single 0x01, pointer -> $7E2000, then 0x00 terminate.
  bus.write(0x7E1000, 0x01);  // single, transfer once
  bus.write(0x7E1001, 0x00);  // pointer low
  bus.write(0x7E1002, 0x20);  // pointer high -> data at $7E2000
  bus.write(0x7E1003, 0x00);  // terminate
  bus.write(0x7E2000, 0x0F);  // the actual data byte

  bus.write(0x004300, 0x40);  // DMAP0: indirect (bit 6), A->B, 1-byte unit
  bus.write(0x004301, 0x00);  // BBAD0 = $2100
  bus.write(0x004302, 0x00);  // A1T0L
  bus.write(0x004303, 0x10);  // A1T0H (table at $7E1000)
  bus.write(0x004304, 0x7E);  // A1B0 (table bank)
  bus.write(0x004307, 0x7E);  // DASB0 (data bank)
  bus.write(0x00420C, 0x01);

  constexpr uint64 kLine = 341 * 4;
  ppu.step(kLine);  // line 0: fetch pointer, transfer data byte 0x0F
  CHECK(bus.ppuRegister(0x00) == 0x0F);
}

TEST_CASE("dma: GP-DMA B->A reads OAM into WRAM") {
  Cartridge cart = makeCart();
  Ppu ppu;
  ppu.power();
  Apu apu;
  Scheduler scheduler(ppu);
  Bus bus(cart, ppu, scheduler, apu);
  bus.power();

  // Put 0x42 in OAM[0].x (via $2102/$2103/$2104).
  bus.write(0x002102, 0x00);
  bus.write(0x002103, 0x00);
  bus.write(0x002104, 0x42);
  bus.write(0x002104, 0x00);
  bus.write(0x002104, 0x00);
  bus.write(0x002104, 0x00);
  // Reset the OAM address back to 0.
  bus.write(0x002102, 0x00);
  bus.write(0x002103, 0x00);

  // Channel 0: B->A, 1-byte unit, BBAD = $2138 (OAMDATAREAD), dest $7E1000.
  bus.write(0x004300, 0x80);  // DMAP0: direction B->A
  bus.write(0x004301, 0x38);  // BBAD0 = $2138
  bus.write(0x004302, 0x00);  // A1T0L = 0x00 -> dest $7E1000
  bus.write(0x004303, 0x10);
  bus.write(0x004304, 0x7E);
  bus.write(0x004305, 0x01);  // 1 byte
  bus.write(0x004306, 0x00);

  bus.write(0x00420B, 0x01);

  CHECK(bus.wram()[0x1000] == 0x42);
}
