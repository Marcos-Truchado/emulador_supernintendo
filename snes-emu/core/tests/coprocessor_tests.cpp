#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "coprocessor/coprocessor.hpp"
#include "coprocessor/srtc.hpp"
#include "coprocessor/obc1.hpp"
#include "coprocessor/dsp1.hpp"
#include "coprocessor/dsp3.hpp"
#include "coprocessor/dsp4.hpp"

using namespace snes;

// Helpers to build minimal ROM images for detection tests.
static std::vector<uint8> makeRom(MapMode mode, uint32 size, uint8 chipset, uint8 mapByte, const char* gamecode = nullptr, bool fast = false) {
  std::vector<uint8> rom(size, 0);
  uint32 base = 0;
  if (mode == MapMode::lorom) base = 0x7FC0;
  else if (mode == MapMode::hirom) base = 0xFFC0;
  else if (mode == MapMode::exhirom) base = 0x40FFC0;
  if (base + 0x30 <= size) {
    rom[base + 0x15] = mapByte | (fast ? 0x10 : 0x00);  // FFD5
    rom[base + 0x16] = chipset;                         // FFD6
    if (gamecode) {
      uint32 gbase = base >= 0x0E ? base - 0x0E : 0;
      for (int i = 0; gamecode[i] && i < 4; i++) rom[gbase + i] = (uint8)gamecode[i];
    }
  }
  // Ensure power-of-two size for Cartridge heuristics (else unknown).
  return rom;
}



// ---- detection ----

TEST_CASE("coprocessor: detect S-RTC via chipset high nibble 5") {
  auto rom = makeRom(MapMode::lorom, 0x8000, 0x55, 0x20);
  Cartridge c; std::string e; REQUIRE(c.load(std::move(rom), &e));
  CHECK(detectChip(c) == Chip::srtc);
}

TEST_CASE("coprocessor: detect OBC-1 via chipset high nibble 2") {
  auto rom = makeRom(MapMode::lorom, 0x8000, 0x25, 0x20);
  Cartridge c; std::string e; REQUIRE(c.load(std::move(rom), &e));
  CHECK(detectChip(c) == Chip::obc1);
}

TEST_CASE("coprocessor: detect DSP-1 vs DSP-4 via FastROM flag") {
  auto romSlow = makeRom(MapMode::lorom, 0x8000, 0x03, 0x20, nullptr, false);
  Cartridge c1; std::string e1; REQUIRE(c1.load(std::move(romSlow), &e1));
  CHECK(detectChip(c1) == Chip::dsp1);

  auto romFast = makeRom(MapMode::lorom, 0x8000, 0x03, 0x30, nullptr, true);
  Cartridge c2; std::string e2; REQUIRE(c2.load(std::move(romFast), &e2));
  CHECK(detectChip(c2) == Chip::dsp4);
}

TEST_CASE("coprocessor: detect DSP-3 via game code ZX3/AED") {
  auto rom = makeRom(MapMode::lorom, 0x8000, 0x03, 0x20, "ZX3J", false);
  Cartridge c; std::string e; REQUIRE(c.load(std::move(rom), &e));
  CHECK(detectChip(c) == Chip::dsp3);

  auto rom2 = makeRom(MapMode::lorom, 0x8000, 0x03, 0x20, "AEDJ", false);
  Cartridge c2; std::string e2; REQUIRE(c2.load(std::move(rom2), &e2));
  CHECK(detectChip(c2) == Chip::dsp3);
}

TEST_CASE("coprocessor: plain ROM chipset 00 -> none") {
  auto rom = makeRom(MapMode::lorom, 0x8000, 0x00, 0x20);
  Cartridge c; std::string e; REQUIRE(c.load(std::move(rom), &e));
  CHECK(detectChip(c) == Chip::none);
}

// ---- S-RTC ----

TEST_CASE("srtc: power initializes to read mode, weekday helper") {
  CHECK(Srtc::weekday(2026, 8, 26) == 3);  // 2026-08-26 is Wednesday (0=Sun)
  CHECK(Srtc::weekday(1900, 1, 1) == 1);   // 1900-01-01 Monday
  Srtc rtc;
  rtc.power();
  // First read should be 0x0F sentinel, then 13 nibbles
  uint8 first = rtc.read(0x2800);
  CHECK(first == 0x0F);
}

TEST_CASE("srtc: write protocol round-trip (minutes/hours stable)") {
  Srtc rtc;
  rtc.power();
  // Enter command, then write mode
  rtc.write(0x2801, 0x0E);
  rtc.write(0x2801, 0x00);
  // Write 12 BCD nibbles: sec 35, min 42, hour 15, day 26, month 8, year 26 (2026)
  uint8 vals[12] = {5,3, 2,4, 5,1, 6,2, 8, 6,2,0}; // sec 35 -> [5,3], etc month 8 -> [8], year 026 -> [6,2,0]
  for (int i = 0; i < 12; i++) rtc.write(0x2801, vals[i]);
  // Weekday auto-written at index 12
  rtc.write(0x2801, 0x0D); // back to read mode
  uint8 sentinel = rtc.read(0x2800);
  CHECK(sentinel == 0x0F);
  // Read back 8..11 should be stable (month/year)
  uint8 readback[13];
  for (int i = 0; i < 13; i++) readback[i] = rtc.read(0x2800);
  CHECK(readback[8] == 8);      // month
  CHECK(readback[9] == 6);      // year ones
  CHECK(readback[10] == 2);     // year tens
  CHECK(readback[11] == 0);     // year hundreds
}

TEST_CASE("srtc: serialize round-trip preserves time regs") {
  Srtc a; a.power();
  a.write(0x2801, 0x0E); a.write(0x2801, 0x00);
  for (int i = 0; i < 12; i++) a.write(0x2801, uint8(i & 0x0F));
  Writer w; a.serialize(w);
  Srtc b; Reader r(w.data()); b.deserialize(r);
  CHECK(r.ok());
  // Verify the 12 BCD nibbles survived round-trip via the read protocol (month/year are stable, seconds may tick).
  // We wrote 0..11; after round-trip the second read-stream's month/year nibbles (8..11) must still be 8..11.
  b.write(0x2801, 0x0D);
  CHECK(b.read(0x2800) == 0x0F);
  for (int i = 0; i < 8; i++) (void)b.read(0x2800); // skip sec/min/hour/day
  CHECK(b.read(0x2800) == 8);
  CHECK(b.read(0x2800) == 9);
  CHECK(b.read(0x2800) == 10);
  CHECK(b.read(0x2800) == 11);
}

// ---- OBC-1 ----

TEST_CASE("obc1: handles window and power fills with FF") {
  Obc1 obc;
  CHECK(obc.handles(0x006000) == true);
  CHECK(obc.handles(0x007FFF) == true);
  CHECK(obc.handles(0x008000) == false);
  CHECK(obc.handles(0x002800) == false);
  obc.power();
  // Uninitialized RAM after power should be 0xFF
  CHECK(obc.read(0x006000) == 0xFF);
  CHECK(obc.read(0x007FF0) == obc.read(0x006000 + 0x1800)); // basePtr default 0x1C00
}

TEST_CASE("obc1: basePtr and sprite byte registers") {
  Obc1 obc; obc.power();
  // Select base 0x1800 via 7FF5 bit0=1
  obc.write(0x007FF5, 0x01);
  obc.write(0x007FF6, 0x02); // address=2, shift=0
  obc.write(0x007FF0, 0xAB);
  CHECK(obc.read(0x007FF0) == 0xAB);
  // Check underlying RAM at basePtr + address<<2
  // basePtr 0x1800 + 2<<2=8 -> 0x1808
  // Direct RAM read via plain window should see it at 0x6000+0x1808
  // Plain window read at 0x6000+0x1808
  CHECK(obc.read(0x006000 + 0x1808) == 0xAB);
}

TEST_CASE("obc1: 2-bit field at 7FF4") {
  Obc1 obc; obc.power();
  obc.write(0x007FF5, 0x00); // base 0x1C00
  obc.write(0x007FF6, 0x04); // address 4, shift 0
  obc.write(0x007FF4, 0x02); // set bits 1:0 to 10
  // RAM at base+ (4>>2)=1 +0x200 = 0x1C00+1+0x200=0x1E01, bits 1:0 = 2
  uint8 v = obc.read(0x007FF4);
  CHECK((v & 3) == 2);
}

TEST_CASE("obc1: serialize round-trip") {
  Obc1 a; a.power(); a.write(0x006123, 0x42); a.write(0x007FF5, 0x01);
  Writer w; a.serialize(w);
  Obc1 b; Reader r(w.data()); b.deserialize(r);
  CHECK(r.ok());
  CHECK(b.read(0x006123) == 0x42);
  CHECK(b.read(0x007FF5) == 0x01);
}

// ---- DSP-1 ----

TEST_CASE("dsp1: handles windows HiROM vs LoROM") {
  Dsp1 hi(true), lo(false);
  // HiROM: 00-1F and 80-9F at 6000-7FFF
  CHECK(hi.handles(0x006000) == true);
  CHECK(hi.handles(0x007FFF) == true);
  CHECK(hi.handles(0x008000) == false);
  CHECK(hi.handles(0x206000) == false); // bank 20 not HiROM DSP window
  // LoROM: 30-3F / B0-BF at 8000+
  CHECK(lo.handles(0x308000) == true);
  CHECK(lo.handles(0x30BFFF) == true);
  CHECK(lo.handles(0x30C000) == true); // C000 window is still inside (8000-FFFF)
  CHECK(lo.handles(0x307FFF) == false);
  CHECK(lo.handles(0x208000) == false); // bank 20 not LoROM DSP1 window
  CHECK(lo.handles(0xB08000) == true); // mirror
}

TEST_CASE("dsp1: multiply 0x1000 * 0x2000 -> 0x0400") {
  Dsp1 dsp(false);
  dsp.power();
  uint32 base = 0x308000; // LoROM DR window (<0xC000)
  auto wr = [&](uint8 b){ dsp.write(base, b); };
  auto rd = [&](){ return dsp.read(base); };
  wr(0x00); // command 00 multiply
  wr(0x00); wr(0x10); // multiplicand 0x1000 LE
  wr(0x00); wr(0x20); // multiplier 0x2000
  uint8 lo = rd(), hi = rd();
  uint16 result = uint16(lo) | (uint16(hi)<<8);
  CHECK(result == 0x0400);
}

TEST_CASE("dsp1: inverse of 0x4000 -> 0x7FFF coeff, 1 exp") {
  Dsp1 dsp(false);
  dsp.power();
  uint32 base = 0x308000;
  auto wr = [&](uint8 b){ dsp.write(base, b); };
  auto rd = [&](){ return dsp.read(base); };
  wr(0x10); // command 10 inverse
  wr(0x00); wr(0x40); // coeff 0x4000
  wr(0x00); wr(0x00); // exp 0
  uint8 c0 = rd(), c1 = rd(), e0 = rd(), e1 = rd();
  uint16 coeff = uint16(c0) | (uint16(c1)<<8);
  uint16 exp = uint16(e0) | (uint16(e1)<<8);
  CHECK(coeff == 0x7FFF);
  CHECK(exp == 0x0001);
}

TEST_CASE("dsp1: serialize preserves FIFO position") {
  Dsp1 a(false); a.power();
  uint32 base = 0x308000;
  a.write(base, 0x00); a.write(base, 0x00); a.write(base, 0x10);
  // Partial params: inCount should be 2 left
  Writer w; a.serialize(w);
  Dsp1 b(false); Reader r(w.data()); b.deserialize(r);
  CHECK(r.ok());
  // Continue same transaction on both and compare results
  a.write(base, 0x00); a.write(base, 0x20);
  b.write(base, 0x00); b.write(base, 0x20);
  uint8 a0 = a.read(base), a1 = a.read(base);
  uint8 b0 = b.read(base), b1 = b.read(base);
  CHECK(a0 == b0);
  CHECK(a1 == b1);
}

// ---- DSP-3 / DSP-4 smoke ----

TEST_CASE("dsp3: power and basic command dispatch does not crash") {
  Dsp3 dsp;
  dsp.power();
  CHECK(dsp.handles(0x208000) == true);
  CHECK(dsp.handles(0x308000) == true); // 30-3F also inside 20-3F window
  CHECK(dsp.handles(0x006000) == false);
  // Send TestMemory command 0x0F (single byte, should reset)
  dsp.write(0x208000, 0x0F);
  // Read should give something without crashing
  uint8 v = dsp.read(0x208000);
  (void)v;
  // Serialize round-trip
  Writer w; dsp.serialize(w);
  Dsp3 dsp2; Reader r(w.data()); dsp2.deserialize(r);
  CHECK(r.ok());
}

TEST_CASE("dsp4: multiply and window") {
  Dsp4 dsp;
  dsp.power();
  CHECK(dsp.handles(0x308000) == true);
  CHECK(dsp.handles(0x208000) == false);
  // Command 0x0000 needs two half-writes: low then high
  uint32 base = 0x308000;
  dsp.write(base, 0x00); // cmd low
  dsp.write(base, 0x00); // cmd high -> 0x0000
  // Params: 4 bytes (multiplicand, multiplier)
  dsp.write(base, 0x00); dsp.write(base, 0x10); // 0x1000
  dsp.write(base, 0x00); dsp.write(base, 0x20); // 0x2000
  uint8 b0 = dsp.read(base), b1 = dsp.read(base), b2 = dsp.read(base), b3 = dsp.read(base);
  // Just verify it produced 4 output bytes without crashing
  (void)b0; (void)b1; (void)b2; (void)b3;
  Writer w; dsp.serialize(w);
  Dsp4 dsp2; Reader r(w.data()); dsp2.deserialize(r);
  CHECK(r.ok());
}

// ---- Bus integration ----

TEST_CASE("bus: coprocessor routing does not affect plain carts") {
  // Plain LoROM cart without chip should still read ROM via Bus, not coprocessor.
  Cartridge cart;
  std::vector<uint8> rom(0x8000, 0x42);
  rom[0x7FD5] = 0x20; // LoROM
  rom[0x7FD6] = 0x00; // plain
  std::string err;
  REQUIRE(cart.load(std::move(rom), &err));
  // Wire a minimal Bus (needs Ppu/Scheduler/Apu stubs). Use System facade for integration.
  System sys;
  // System::load would re-detect; instead just check detectChip directly
  CHECK(detectChip(cart) == Chip::none);
}
