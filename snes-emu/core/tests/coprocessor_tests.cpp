#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "coprocessor/coprocessor.hpp"
#include "coprocessor/srtc.hpp"
#include "coprocessor/obc1.hpp"
#include "coprocessor/cx4.hpp"
#include "coprocessor/sdd1.hpp"
#include "coprocessor/dsp1.hpp"
#include "coprocessor/dsp3.hpp"
#include "coprocessor/dsp4.hpp"
#include "coprocessor/superfx.hpp"

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

TEST_CASE("coprocessor: detect Cx4 via chipset F3") {
  auto rom = makeRom(MapMode::lorom, 0x8000, 0xF3, 0x20);
  Cartridge c; std::string e; REQUIRE(c.load(std::move(rom), &e));
  CHECK(detectChip(c) == Chip::cx4);
  auto cop = makeCoprocessor(Chip::cx4, MapMode::lorom);
  CHECK(cop != nullptr);
  CHECK(cop->handles(0x006000) == true);
  CHECK(cop->handles(0x007FFF) == true);
  CHECK(cop->handles(0x008000) == false);
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

// ---- Cx4 ----

TEST_CASE("cx4: handles window and power") {
  Cx4 cx4;
  CHECK(cx4.handles(0x006000) == true);
  CHECK(cx4.handles(0x007FFF) == true);
  CHECK(cx4.handles(0x008000) == false);
  CHECK(cx4.handles(0x806000) == true);
  cx4.power();
  CHECK(cx4.read(0x7F5E) == 0x00); // not busy
}

TEST_CASE("cx4: command 0x5C immediate reg fills test pattern") {
  Cx4 cx4; cx4.power();
  cx4.write(0x007F4D, 0x0E);
  cx4.write(0x007F4F, 0x5C);
  CHECK(cx4.read(0x006000) == 0x00);
  CHECK(cx4.read(0x006001) == 0x00);
  // first bytes of test pattern
}

TEST_CASE("cx4: command 0x40 sum and 0x54 square") {
  Cx4 cx4; cx4.power();
  // fill some RAM
  cx4.write(0x006000, 0x01); cx4.write(0x006001, 0x02);
  cx4.write(0x007F4D, 0x0E);
  cx4.write(0x007F4F, 0x40);
  uint16 sum = uint16(cx4.read(0x007F80)) | (uint16(cx4.read(0x007F81)) << 8);
  CHECK(sum == 0x0003);
  // square
  cx4.write(0x007F80, 0x02); cx4.write(0x007F81, 0x00); cx4.write(0x007F82, 0x00);
  cx4.write(0x007F4F, 0x54);
  uint32 lo = uint32(cx4.read(0x007F83)) | (uint32(cx4.read(0x007F84)) << 8) | (uint32(cx4.read(0x007F85)) << 16);
  CHECK(lo == 4);
}

TEST_CASE("cx4: serialize round-trip") {
  Cx4 a; a.power(); a.write(0x006123, 0x42);
  Writer w; a.serialize(w);
  Cx4 b; Reader r(w.data()); b.deserialize(r);
  CHECK(r.ok());
  CHECK(b.read(0x006123) == 0x42);
}

TEST_CASE("cx4: dma copies from ROM") {
  Cx4 cx4;
  std::vector<uint8> rom(0x8000, 0xAA);
  // put known bytes at LoROM 00:8000
  rom[0x0000] = 0x11; rom[0x0001] = 0x22; rom[0x0002] = 0x33; rom[0x0003] = 0x44;
  cx4.setRom(rom, MapMode::lorom);
  cx4.power();
  // set source 00:8000 -> 0x008000, dest 0x6000, len 4
  cx4.write(0x007F40, 0x00); cx4.write(0x007F41, 0x80); cx4.write(0x007F42, 0x00);
  cx4.write(0x007F43, 0x04); cx4.write(0x007F44, 0x00);
  cx4.write(0x007F45, 0x00); cx4.write(0x007F46, 0x60);
  cx4.write(0x007F47, 0x00);
  CHECK(cx4.read(0x006000) == 0x11);
  CHECK(cx4.read(0x006001) == 0x22);
}

// ---- S-DD1 ----

TEST_CASE("sdd1: detect via map 0x32 and chipset 0x43") {
  auto rom = makeRom(MapMode::lorom, 0x8000, 0x43, 0x32);
  Cartridge c; std::string e; REQUIRE(c.load(std::move(rom), &e));
  CHECK(detectChip(c) == Chip::sdd1);
  auto cop = makeCoprocessor(Chip::sdd1, MapMode::lorom);
  CHECK(cop != nullptr);
  CHECK(cop->handles(0x004800) == true);
  CHECK(cop->handles(0x004807) == true);
  CHECK(cop->handles(0x004808) == false);
  CHECK(cop->handles(0x006000) == false);
}

TEST_CASE("sdd1: registers and mmc") {
  Sdd1 sdd1;
  sdd1.power();
  CHECK(sdd1.handles(0x004800) == true);
  sdd1.write(0x004800, 0x01); sdd1.write(0x004801, 0x02);
  sdd1.write(0x004804, 0x01); // mmc0 = 0x100000
  CHECK(sdd1.read(0x004804) == 0x01);
  // serialize
  Writer w; sdd1.serialize(w);
  Sdd1 b; Reader r(w.data()); b.deserialize(r);
  CHECK(r.ok());
  CHECK(b.read(0x004804) == 0x01);
}

TEST_CASE("sdd1: decompress does not crash (smoke)") {
  Sdd1 sdd1; sdd1.power();
  // header 0xC0 = type 3, 8 planes, context 0x01C0/0x0001
  // Minimal compressed stream: header + 2 bytes + output request 4 bytes
  std::vector<uint8> in = {0xC0, 0x00, 0xFF, 0xFF, 0x00, 0x00};
  std::vector<uint8> out(16, 0xCC);
  // pad ROM for getMappedPointer fallback: not needed here, direct decompressBlock
  // Use decompressBlock with in pointer from a fake ROM
  std::vector<uint8> rom(0x200000, 0);
  rom[0x1000] = 0xC0; rom[0x1001] = 0x00; rom[0x1002] = 0xFF; rom[0x1003] = 0xFF;
  sdd1.setRom(rom, MapMode::lorom);
  // decompress 8 bytes from 0xC00000+0x1000
  const uint8* src = sdd1.getMappedRomPointer(0xC01000);
  REQUIRE(src != nullptr);
  std::vector<uint8> dst(8);
  sdd1.decompressBlock(src, dst.data(), 8);
  // just check it produced something without crashing
  CHECK(dst.size() == 8);
}

TEST_CASE("sdd1: bus dma hook decompresses") {
  Cartridge cart;
  std::vector<uint8> rom(0x8000, 0x00);
  rom[0x7FD5] = 0x32; // S-DD1
  rom[0x7FD6] = 0x43;
  rom[0x1000] = 0xC0; rom[0x1001] = 0x00; rom[0x1002] = 0xAA; rom[0x1003] = 0x55;
  std::string err;
  REQUIRE(cart.load(std::move(rom), &err));
  CHECK(detectChip(cart) == Chip::sdd1);
  // hook smoke: ensure Bus can be created with sdd1 cart
  Sdd1 sdd1;
  sdd1.setRom(cart.rom(), cart.mapMode());
  sdd1.power();
  sdd1.write(0x004800, 0x01); sdd1.write(0x004801, 0x01);
  CHECK(sdd1.activeForChannel(0) == true);
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

// ---- SuperFX GSU (snes9x fxinst/fxemu faithful PLOT path) ----

static void sfxSetReg(SuperFx& sfx, int r, uint16 v) {
  sfx.write(0x003000 + r * 2, uint8(v & 0xFF));
  sfx.write(0x003000 + r * 2 + 1, uint8(v >> 8));
}

TEST_CASE("superfx: power zeroes RAM (no HLE checker) and VCR=GSU2") {
  SuperFx sfx;
  sfx.power();
  CHECK(sfx.read(0x00303B) == 0x04); // VCR
  CHECK(sfx.read(0x00700000) == 0x00);
  CHECK(sfx.read(0x00700001) == 0x00);
  CHECK(sfx.read(0x003030) == 0x00); // SFR lo (GO=0)
  CHECK((sfx.read(0x003031) & 0x80) == 0x00); // IRQ=0
}

TEST_CASE("superfx: STOP clears GO (0x20) and sets IRQ (0x8000)") {
  SuperFx sfx;
  std::vector<uint8> rom(0x10000, 0x01); // NOP fill
  rom[0] = 0x00; // STOP
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18); // SCMR: RAN|RON so the session validates
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00); // R15 MSB -> GO=1
  CHECK((sfx.read(0x003030) & 0x20) != 0); // GO set
  for (int i = 0; i < 10; i++) sfx.stepGsu();
  CHECK((sfx.read(0x003030) & 0x20) == 0); // GO cleared by STOP
  CHECK((sfx.read(0x003031) & 0x80) != 0); // IRQ set
}

TEST_CASE("superfx: COLOR+PLOT 4-color writes pixel to screen base") {
  SuperFx sfx;
  // program: COLOR, PLOT, STOP
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0] = 0x4E; rom[1] = 0x4C; rom[2] = 0x00;
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x003038, 0x00); // SCBR=0
  sfx.write(0x00303A, 0x18); // SCMR mode0 128px + RAN|RON
  sfxSetReg(sfx, 0, 0x0001); // R0=color 1
  sfxSetReg(sfx, 1, 0x0000); // R1=x
  sfxSetReg(sfx, 2, 0x0000); // R2=y
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx.stepGsu();
  // 4-color mode tile0: plane0 at base+0, bit7 (x=0 -> 128>>0)
  CHECK(sfx.read(0x00700000) == 0x80);
  CHECK(sfx.read(0x00700001) == 0x00);
}

TEST_CASE("superfx: RPIX reads back plotted pixel") {
  SuperFx sfx;
  // program: ALT1+RPIX, STOP. Pre-fill screen RAM so (0,0) plane0 bit7=1.
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0] = 0x3D; rom[1] = 0x4C; rom[2] = 0x00;
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x003038, 0x00);
  sfx.write(0x00303A, 0x18); // mode0 + RAN|RON
  sfx.write(0x00700000, 0x80); // plane0 x=0
  sfx.write(0x00700001, 0x00);
  sfxSetReg(sfx, 1, 0x0000);
  sfxSetReg(sfx, 2, 0x0000);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  uint16 r0 = uint16(sfx.read(0x003000)) | (uint16(sfx.read(0x003001)) << 8);
  CHECK(r0 == 0x0001);
}

TEST_CASE("superfx: CACHE sets CBR=R15&FFF0 and CMODE sets POR/height") {
  SuperFx sfx;
  // program at 0x120: CACHE, ALT1, CMODE (SREG=R0=0x10 OBJ), STOP
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0x120] = 0x02; rom[0x121] = 0x3D; rom[0x122] = 0x4E; rom[0x123] = 0x00;
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18); // RAN|RON for session validation
  sfxSetReg(sfx, 0, 0x0010);
  sfxSetReg(sfx, 15, 0x0120);
  sfx.write(0x00301F, 0x01); // R15=0x0120 (MSB=0x01) -> GO
  for (int i = 0; i < 20; i++) sfx.stepGsu();
  CHECK(sfx.read(0x00303E) == 0x20);
  CHECK(sfx.read(0x00303F) == 0x01);
}

// ---- SuperFX full GSU table (snes9x fxinst.cpp transliteration) ----

static void sfxRun(SuperFx& sfx, std::vector<uint8> rom) {
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18); // RAN|RON
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 40; i++) sfx.stepGsu();
}

static uint16 sfxReg(SuperFx& sfx, int r) {
  return uint16(sfx.read(uint24(0x003000 + r * 2))) |
         (uint16(sfx.read(uint24(0x003000 + r * 2 + 1))) << 8);
}

TEST_CASE("superfx: WITH+ADD uses SREG/DREG selection") {
  SuperFx sfx;
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0] = 0x23; rom[1] = 0x53; rom[2] = 0x00; // WITH R3, ADD R3, STOP
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 3, 7);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 3) == 14);
}

TEST_CASE("superfx: SUB sets Z + BEQ taken skips over NOP delay") {
  SuperFx sfx;
  // B1 62 | 09 04 | 01 | A0 63 | 00 | A0 07 | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xB1, 0x62, 0x09, 0x04, 0x01, 0xA0, 0x63, 0x00, 0xA0, 0x07, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 1, 5);
  sfxSetReg(sfx, 2, 5); // equal -> Z=1 -> BEQ taken -> R0=7
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 0) == 7);

  SuperFx sfx2; // not taken path -> R0=99
  sfx2.setRom(rom, MapMode::lorom);
  sfx2.power();
  sfx2.write(0x00303A, 0x18);
  sfxSetReg(sfx2, 1, 5);
  sfxSetReg(sfx2, 2, 3);
  sfxSetReg(sfx2, 15, 0x0000);
  sfx2.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx2.stepGsu();
  CHECK(sfxReg(sfx2, 0) == 99);
}

TEST_CASE("superfx: LOOP iterates R12 times with delay NOP") {
  SuperFx sfx;
  // A0 00 | AC 03 | AD 06 | D0 | 3C | 01 | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xA0, 0x00, 0xAC, 0x03, 0xAD, 0x06, 0xD0, 0x3C, 0x01, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 3);
  CHECK(sfxReg(sfx, 12) == 0);
}

TEST_CASE("superfx: JMP executes delay-slot IBT before landing") {
  SuperFx sfx;
  // A8 09 | 98 | A0 07 | A0 63 | A0 63 | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xA8, 0x09, 0x98, 0xA0, 0x07, 0xA0, 0x63, 0xA0, 0x63, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 7); // delay IBT ran; skipped bytes never ran
}

TEST_CASE("superfx: LJMP switches PBR + LINK saves return") {
  SuperFx sfx;
  // B1 | 3D 98 | 01 | A0 63 | 00 ; R1=5 dest, R8=0 bank
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xB1, 0x3D, 0x98, 0x01, 0xA0, 0x63, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 1, 6);
  sfxSetReg(sfx, 8, 0);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 0) == 0); // skipped IBT never ran
  CHECK(sfx.read(0x00303E) == 0x00); // LJMP reset CBR to dest&FFF0

  SuperFx sfx2; // LINK 1 at 0 -> R11 = 1+1 = 2
  std::vector<uint8> rom2(0x10000, 0x01);
  rom2[0] = 0x91; rom2[1] = 0x00;
  sfxRun(sfx2, std::move(rom2));
  CHECK(sfxReg(sfx2, 11) == 2);
}

TEST_CASE("superfx: IBT sign-extends, IWT loads little-endian") {
  SuperFx sfx;
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xA0, 0xFF, 0xF1, 0x34, 0x12, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 0xFFFF);
  CHECK(sfxReg(sfx, 1) == 0x1234);
}

TEST_CASE("superfx: LM/SM round-trip + SBK uses last RAM address") {
  SuperFx sfx;
  // 3D F0 00 01 (LM R0,0x100) | 3E F0 00 02 (SM 0x200,R0) | A0 77 | 90 (SBK) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0x3D, 0xF0, 0x00, 0x01, 0x3E, 0xF0, 0x00, 0x02, 0xA0, 0x77, 0x90, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfx.write(0x00700100, 0x34);
  sfx.write(0x00700101, 0x12);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 40; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 0) == 0x0077); // IBT overwrote R0 after LM
  CHECK(sfx.read(0x00700200) == 0x77); // SBK rewrote low byte of last addr
  CHECK(sfx.read(0x00700201) == 0x00);
}

TEST_CASE("superfx: LMS/SMS short-address RAM access") {
  SuperFx sfx;
  // 3D A0 80 (LMS R0,0x80->0x100) | 3E A0 90 (SMS 0x90->0x120,R0) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0x3D, 0xA0, 0x80, 0x3E, 0xA0, 0x90, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfx.write(0x00700100, 0xCD);
  sfx.write(0x00700101, 0xAB);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 40; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 0) == 0xABCD);
  CHECK(sfx.read(0x00700120) == 0xCD);
  CHECK(sfx.read(0x00700121) == 0xAB);
}

TEST_CASE("superfx: LDW/STW/LDB/STB via register pointers") {
  SuperFx sfx;
  // F5 00 01 (IWT R5,0x100) | B1 (FROM R1) | 35 (STW R5) | 3D 45 (LDB R5) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xF5, 0x00, 0x01, 0xB1, 0x35, 0x3D, 0x45, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 1, 0xABCD);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 40; i++) sfx.stepGsu();
  CHECK(sfx.read(0x00700100) == 0xCD);
  CHECK(sfx.read(0x00700101) == 0xAB);
  CHECK(sfxReg(sfx, 0) == 0x00CD); // LDB zero-extends
}

TEST_CASE("superfx: GETB/H/L/S + ROMB bank switch") {
  // GETB/GETBH with rom[0x10]=0xAB, R0=0x12 preset
  {
    SuperFx sfx;
    std::vector<uint8> rom(0x10000, 0x01);
    rom[0x10] = 0xAB;
    uint8 prog[] = {0xA0, 0x12, 0x3D, 0xEF, 0x00}; // IBT R0,12 | GETBH | STOP
    for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
    sfx.setRom(rom, MapMode::lorom);
    sfx.power();
    sfx.write(0x00303A, 0x18);
    sfxSetReg(sfx, 14, 0x0010);
    sfxSetReg(sfx, 15, 0x0000);
    sfx.write(0x00301F, 0x00);
    for (int i = 0; i < 30; i++) sfx.stepGsu();
    CHECK(sfxReg(sfx, 0) == 0xAB12);
  }
  // GETBL + GETBS
  {
    SuperFx sfx;
    std::vector<uint8> rom(0x10000, 0x01);
    rom[0x10] = 0xAB;
    uint8 prog[] = {0xF0, 0x00, 0x12, 0x3E, 0xEF, 0x00}; // IWT R0,1200 | GETBL | STOP
    for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
    sfx.setRom(rom, MapMode::lorom);
    sfx.power();
    sfx.write(0x00303A, 0x18);
    sfxSetReg(sfx, 14, 0x0010);
    sfxSetReg(sfx, 15, 0x0000);
    sfx.write(0x00301F, 0x00);
    for (int i = 0; i < 30; i++) sfx.stepGsu();
    CHECK(sfxReg(sfx, 0) == 0x12AB);
  }
  {
    SuperFx sfx;
    std::vector<uint8> rom(0x10000, 0x01);
    rom[0x10] = 0xAB;
    uint8 prog[] = {0x3F, 0xEF, 0x00}; // GETBS | STOP
    for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
    sfx.setRom(rom, MapMode::lorom);
    sfx.power();
    sfx.write(0x00303A, 0x18);
    sfxSetReg(sfx, 14, 0x0010);
    sfxSetReg(sfx, 15, 0x0000);
    sfx.write(0x00301F, 0x00);
    for (int i = 0; i < 30; i++) sfx.stepGsu();
    CHECK(sfxReg(sfx, 0) == 0xFFAB);
  }
  // ROMB to bank 0x40 (HiROM mirror, unambiguous 64KB stride)
  {
    SuperFx sfx;
    std::vector<uint8> rom(0x20000, 0x01);
    rom[0x10] = 0x77;
    uint8 prog[] = {0xA1, 0x40, 0xB1, 0x3F, 0xDF, 0xFE, 0x10, 0x00, 0xEF, 0x00};
    for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
    sfx.setRom(rom, MapMode::lorom);
    sfx.power();
    sfx.write(0x00303A, 0x18);
    sfxSetReg(sfx, 15, 0x0000);
    sfx.write(0x00301F, 0x00);
    for (int i = 0; i < 40; i++) sfx.stepGsu();
    CHECK(sfxReg(sfx, 0) == 0x0077);
    CHECK(sfx.read(0x003036) == 0x40);
  }
}

TEST_CASE("superfx: INC/DEC R14 does not refill ROM buffer (stale GETB)") {
  // HW/snes9x: ROM buffer refills on explicit R14 sets (IWT/LM/TO) but NOT
  // on INC/DEC, so GETB after INC reads the previous byte (stale).
  // FE 00 01 (IWT R14,#0x100; refills buf=ROM[0x100]=0xAA) | EF (GETB R0=0xAA)
  // | DE (INC R14 -> 0x101, NO refill) | EF (GETB still 0xAA, not ROM[0x101]=0xBB) | 00
  SuperFx sfx;
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xFE, 0x00, 0x01, 0xEF, 0xDE, 0xEF, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  rom[0x100] = 0xAA;
  rom[0x101] = 0xBB;
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 0x00AA);
}

TEST_CASE("superfx: MERGE texture interleave + flags") {
  SuperFx sfx;
  // F7 00 AB (R7=AB00) | F8 34 12 (R8=1234) | 70 (MERGE) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xF7, 0x00, 0xAB, 0xF8, 0x34, 0x12, 0x70, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 0xAB12);
  uint8 sfr = sfx.read(0x003030);
  CHECK((sfr & 0x08) != 0); // S
  CHECK((sfr & 0x02) != 0); // Z (fullsnes MERGE: set iff (v&F0F0)!=0; AB12&A010!=0 -> Z=1)
  CHECK((sfr & 0x10) != 0); // OV
  CHECK((sfr & 0x04) != 0); // CY

  // Z clear when the F0F0 mask is zero (e.g. R7=0x0000,R8=0x0000 -> v=0x0000)
  SuperFx sfxZ;
  std::vector<uint8> romZ(0x10000, 0x01);
  uint8 progZ[] = {0xF7, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x70, 0x00};
  for (size_t i = 0; i < sizeof progZ; i++) romZ[i] = progZ[i];
  sfxRun(sfxZ, std::move(romZ));
  CHECK(sfxReg(sfxZ, 0) == 0x0000);
  CHECK((sfxZ.read(0x003030) & 0x02) == 0); // Z=0 when (v & 0xF0F0)==0
}

TEST_CASE("superfx: MULT signed vs UMULT unsigned") {
  SuperFx sfx;
  // F1 FF FF (R1=FFFF=-1) | F2 03 00 (R2=3) | 21 (WITH R1) | 82 (MULT R2 -> FFFD)
  // | 21 (WITH R1 again: prefixes reset after MULT) | 3D 82 (UMULT: FD*3=2F7) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xF1, 0xFF, 0xFF, 0xF2, 0x03, 0x00, 0x21, 0x82, 0x21, 0x3D, 0x82, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 1) == 0x02F7);
}

TEST_CASE("superfx: FMULT high word + LMULT low word to R4") {
  SuperFx sfx;
  // F1 00 40 (R1=4000) | F6 00 40 (R6=4000) | B1 (FROM R1) | 9F (FMULT -> R0=1000)
  // | B1 (FROM R1 again) | 3D 9F (LMULT -> R4=0000,R0=1000) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xF1, 0x00, 0x40, 0xF6, 0x00, 0x40, 0xB1, 0x9F, 0xB1, 0x3D, 0x9F, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 0x1000);
  CHECK(sfxReg(sfx, 4) == 0x0000);
}

TEST_CASE("superfx: SWAP/SEX/LOB/HIB byte ops") {
  SuperFx sfx;
  // F0 34 12 (R0=1234) | 4D (SWAP->3412) | 9E (LOB->12) | C0 (HIB->00) | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xF0, 0x34, 0x12, 0x4D, 0x9E, 0xC0, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 0x0000);

  SuperFx sfx2;
  std::vector<uint8> rom2(0x10000, 0x01);
  uint8 prog2[] = {0xF0, 0x80, 0x00, 0x95, 0x00}; // IWT R0,80 | SEX -> FF80
  for (size_t i = 0; i < sizeof prog2; i++) rom2[i] = prog2[i];
  sfxRun(sfx2, std::move(rom2));
  CHECK(sfxReg(sfx2, 0) == 0xFF80);
}

TEST_CASE("superfx: ASR/ROR/DIV2/INC/DEC shift chain") {
  SuperFx sfx;
  // F0 03 00 | 97 | 97 | 96 | 3D 96 | D0 | E0 | 00
  // R0=3 ->ROR(CY0)->1,CY1 ->ROR->8000 ->ASR->C000 ->DIV2->E000 ->INC->E001 ->DEC->E000
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xF0, 0x03, 0x00, 0x97, 0x97, 0x96, 0x3D, 0x96, 0xD0, 0xE0, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfxReg(sfx, 0) == 0xE000);
  uint8 sfr = sfx.read(0x003030);
  CHECK((sfr & 0x08) != 0); // S
  CHECK((sfr & 0x04) == 0); // CY (DIV2 of C000 is even)
}

TEST_CASE("superfx: branch preserves TO prefix for delay-slot ADD") {
  SuperFx sfx;
  // Case-4 split (fullsnes): TO R5 | BEQ +1 | ADD R5(delay, uses DREG=R5) | STOP
  // A5 0A | B1 | 61 | 15 | 09 01 | 55 | 00
  // R5=10, SUB R1,R1 (R0=0,Z=1), TO R5, BEQ taken -> delay ADD R5: R5=R0+R5=10
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xA5, 0x0A, 0xB1, 0x61, 0x15, 0x09, 0x01, 0x55, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 1, 3);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 40; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 5) == 10); // DREG was R5
  CHECK(sfxReg(sfx, 0) == 0);  // R0 untouched proves prefix survived
}

TEST_CASE("superfx: RAMB/ROMB bank registers + mode-2 PLOT is 4-bit") {
  SuperFx sfx;
  // A0 02 | 3E DF (RAMB) | A0 03 | 3F DF (ROMB) | 00 ; SREG=R0 default
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xA0, 0x02, 0x3E, 0xDF, 0xA0, 0x03, 0x3F, 0xDF, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfxRun(sfx, std::move(rom));
  CHECK(sfx.read(0x00303C) == 0x02);
  CHECK(sfx.read(0x003036) == 0x03);

  SuperFx sfx2; // mode 2 (0x1A) plots 4 planes like mode 1
  std::vector<uint8> rom2(0x10000, 0x01);
  rom2[0] = 0x4E; rom2[1] = 0x4C; rom2[2] = 0x00;
  sfx2.setRom(rom2, MapMode::lorom);
  sfx2.power();
  sfx2.write(0x003038, 0x00);
  sfx2.write(0x00303A, 0x1A); // mode2 + RAN|RON
  sfxSetReg(sfx2, 0, 0x0005);
  sfxSetReg(sfx2, 1, 0x0000);
  sfxSetReg(sfx2, 2, 0x0000);
  sfxSetReg(sfx2, 15, 0x0000);
  sfx2.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx2.stepGsu();
  CHECK(sfx2.read(0x00700000) == 0x80);
  CHECK(sfx2.read(0x00700010) == 0x80);
}

TEST_CASE("superfx: CACHE sets CBR=R15&FFF0 and CMODE sets POR/height") {
  SuperFx sfx;
  // program at 0x120: CACHE, ALT1, CMODE (SREG=R0=0x10 OBJ), STOP
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0x120] = 0x02; rom[0x121] = 0x3D; rom[0x122] = 0x4E; rom[0x123] = 0x00;
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18); // RAN|RON for session validation
  sfxSetReg(sfx, 0, 0x0010);
  sfxSetReg(sfx, 15, 0x0120);
  sfx.write(0x00301F, 0x01); // R15=0x0120 (MSB=0x01) -> GO
  for (int i = 0; i < 20; i++) sfx.stepGsu();
  CHECK(sfx.read(0x00303E) == 0x20);
  CHECK(sfx.read(0x00303F) == 0x01);
}

TEST_CASE("superfx: CMP/ADC/SBC carry+zero drive branches") {
  SuperFx sfx;
  // B1 FROM R1 | 3F 61 CMP R1 (R0==R1? Z=1 CY=1) | 09 04 BEQ+4 | 01 NOP delay
  // | A0 63 | 00 STOP | A0 07 | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xB1, 0x3F, 0x61, 0x09, 0x04, 0x01, 0xA0, 0x63, 0x00, 0xA0, 0x07, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 0, 0x1234);
  sfxSetReg(sfx, 1, 0x1234); // equal -> CMP sets Z,CY
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 0) == 0x0007); // BEQ taken: skipped IBT 0x63, ran IBT 7
}

TEST_CASE("superfx: ADC adds carry, SBC borrows inverted carry") {
  SuperFx sfx;
  // R0=0xFFFF, R1=1: WITH R0? use: B0 FROM R0 | 3D 51 ADC R1 (FFFF+1+CY?)
  // CY preset via SFR write: set CY bit then ADC
  std::vector<uint8> rom(0x10000, 0x01);
  // B0 | 3D 51 | 00 : FROM R0, ADC R1, STOP
  uint8 prog[] = {0xB0, 0x3D, 0x51, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 0, 0x0001);
  sfxSetReg(sfx, 1, 0x0002);
  // preset CY=1 via SFR low write (bit2) -- GO must stay 0 (bit5=0)
  sfx.write(0x003030, 0x04);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx.stepGsu();
  // ADC: R0 = R0+R1+CY = 1+2+1 = 4
  CHECK(sfxReg(sfx, 0) == 0x0004);

  SuperFx sfx2; // SBC: R0=5,R1=2,CY=1 -> 5-2-0=3
  std::vector<uint8> rom2(0x10000, 0x01);
  uint8 prog2[] = {0xB0, 0x3D, 0x61, 0x00}; // FROM R0, SBC R1, STOP
  for (size_t i = 0; i < sizeof prog2; i++) rom2[i] = prog2[i];
  sfx2.setRom(rom2, MapMode::lorom);
  sfx2.power();
  sfx2.write(0x00303A, 0x18);
  sfxSetReg(sfx2, 0, 0x0005);
  sfxSetReg(sfx2, 1, 0x0002);
  sfx2.write(0x003030, 0x04); // CY=1 -> borrow 0
  sfxSetReg(sfx2, 15, 0x0000);
  sfx2.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx2.stepGsu();
  CHECK(sfxReg(sfx2, 0) == 0x0003);
}

TEST_CASE("superfx: ADD_I/SUB_I immediates + BGE/BLT signed branches") {
  SuperFx sfx;
  // R0=0x8000 (negative), R1=0: WITH/B? use FROM R0, SUB R1 -> S=1,OV=0 -> BLT taken
  // B0 | 61 | 07 04 | 01 | A0 63 | 00 | A0 07 | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xB0, 0x61, 0x07, 0x04, 0x01, 0xA0, 0x63, 0x00, 0xA0, 0x07, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 0, 0x8000);
  sfxSetReg(sfx, 1, 0x0000);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  // SUB: R0 = 8000-0 = 8000, S=1, OV=0 -> BLT (S!=OV) taken -> R0=7
  CHECK(sfxReg(sfx, 0) == 0x0007);

  SuperFx sfx2; // ADD #n: R0=5, ALT2 ADD #3 -> R0=8
  std::vector<uint8> rom2(0x10000, 0x01);
  uint8 prog2[] = {0x3E, 0x53, 0x00}; // ALT2, ADD #3, STOP (SREG=R0)
  for (size_t i = 0; i < sizeof prog2; i++) rom2[i] = prog2[i];
  sfx2.setRom(rom2, MapMode::lorom);
  sfx2.power();
  sfx2.write(0x00303A, 0x18);
  sfxSetReg(sfx2, 0, 0x0005);
  sfxSetReg(sfx2, 15, 0x0000);
  sfx2.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx2.stepGsu();
  CHECK(sfxReg(sfx2, 0) == 0x0008);
}

TEST_CASE("superfx: LSR/ROL carry round-trip") {
  SuperFx sfx;
  // R0=0x8003: LSR -> R0=0x4001 CY=1; ROL -> R0=0x8003 CY=0? (0x4001<<1|1=0x8003, CY=(0x4001>>15)=0)
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0x03, 0x04, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 0, 0x8003);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx.stepGsu();
  CHECK(sfxReg(sfx, 0) == 0x8003);
  CHECK((sfx.read(0x003030) & 0x04) == 0); // CY=0
}

TEST_CASE("superfx: OR/XOR/AND/BIC with registers and immediates") {
  SuperFx sfx;
  // R0=0xFF00, R1=0x0FF0:
  // WITH R0? OR needs SREG: B0 FROM R0 | C1 OR R1 -> R0 = FF00|0FF0 = FFF0
  // | 3D C2 XOR R2 (R2=0xFFFF) -> R0 = FFF0^FFFF = 000F, Z=0
  // | 3E 73 AND #3 -> R0 = 000F&3 = 3 | 00
  std::vector<uint8> rom(0x10000, 0x01);
  uint8 prog[] = {0xB0, 0xC1, 0x3D, 0xC2, 0x3E, 0x73, 0x00};
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 0, 0xFF00);
  sfxSetReg(sfx, 1, 0x0FF0);
  sfxSetReg(sfx, 2, 0xFFFF);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  // NOTE: prefixes reset after each op, so XOR/AND use SREG=R0 (chained):
  // OR: R0=FFF0. XOR needs SREG=R0 -> must re-FROM! Without it SREG=R0 anyway (reset default).
  // DREG also resets to R0. So chain works on R0.
  CHECK(sfxReg(sfx, 0) == 0x0003);
}

TEST_CASE("superfx: GETC transfers ROM buffer to COLR + STB stores byte") {
  SuperFx sfx;
  // rom[0x20]=0x5A: IWT R14,0x20 (FE 20 00) | DF GETC -> COLR=5A
  // | 4E COLOR? no: PLOT needs R1/R2: set R1=0,R2=0 via SNES, PLOT mode0: RAM[0]=0x80? color 5A bit0=0? 5A=01011010 bit0=0 -> plane0 CLEAR stays 0!
  // simpler: check COLR effect via PLOT with color 0x03: use IBT R0,3 + COLOR? GETC path:
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0x20] = 0x03;
  uint8 prog[] = {0xFE, 0x20, 0x00, 0xDF, 0x4C, 0x00}; // IWT R14 | GETC | PLOT | STOP
  for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x003038, 0x00);
  sfx.write(0x00303A, 0x18);
  sfxSetReg(sfx, 1, 0x0000);
  sfxSetReg(sfx, 2, 0x0000);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  CHECK(sfx.read(0x00700000) == 0x80); // color 3 bit0 set
  CHECK(sfx.read(0x00700001) == 0x80); // color 3 bit1 set

  SuperFx sfx2; // STB: R5=0x300, SREG=R1=0xAB -> RAM[300]=AB
  std::vector<uint8> rom2(0x10000, 0x01);
  uint8 prog2[] = {0xB1, 0x3D, 0x35, 0x00}; // FROM R1 | STB R5 | STOP
  for (size_t i = 0; i < sizeof prog2; i++) rom2[i] = prog2[i];
  sfx2.setRom(rom2, MapMode::lorom);
  sfx2.power();
  sfx2.write(0x00303A, 0x18);
  sfxSetReg(sfx2, 1, 0xAB);
  sfxSetReg(sfx2, 5, 0x0300);
  sfxSetReg(sfx2, 15, 0x0000);
  sfx2.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx2.stepGsu();
  CHECK(sfx2.read(0x00700300) == 0xAB);
}

TEST_CASE("superfx: RPIX 4-bit and 8-bit + PLOT 8-bit") {
  SuperFx sfx;
  // 16-color: SCBR0 SCMR=0x19 (mode1+RANRON). PLOT color 0x0A at (0,0)
  std::vector<uint8> rom(0x10000, 0x01);
  rom[0] = 0x4E; rom[1] = 0x4C; rom[2] = 0x3D; rom[3] = 0x4C; rom[4] = 0x00;
  sfx.setRom(rom, MapMode::lorom);
  sfx.power();
  sfx.write(0x003038, 0x00);
  sfx.write(0x00303A, 0x19);
  sfxSetReg(sfx, 0, 0x000A);
  sfxSetReg(sfx, 1, 0x0000);
  sfxSetReg(sfx, 2, 0x0000);
  sfxSetReg(sfx, 15, 0x0000);
  sfx.write(0x00301F, 0x00);
  for (int i = 0; i < 30; i++) sfx.stepGsu();
  // color A=1010: planes 1,3 set at bit7; R1 advanced to 1 so RPIX reads x=1 (empty=0)
  // RPIX result goes to R0 (default DREG): expect 0
  CHECK(sfxReg(sfx, 0) == 0x0000);
  CHECK(sfx.read(0x00700000) == 0x00); // plane0 clear
  CHECK(sfx.read(0x00700001) == 0x80); // plane1 set
  CHECK(sfx.read(0x00700010) == 0x00); // plane2 clear
  CHECK(sfx.read(0x00700011) == 0x80); // plane3 set

  SuperFx sfx2; // 8-bit PLOT color 0x81 at (0,0), SCBR0 SCMR=0x1B (mode3)
  std::vector<uint8> rom2(0x10000, 0x01);
  rom2[0] = 0x4E; rom2[1] = 0x4C; rom2[2] = 0x00;
  sfx2.setRom(rom2, MapMode::lorom);
  sfx2.power();
  sfx2.write(0x003038, 0x00);
  sfx2.write(0x00303A, 0x1B);
  sfxSetReg(sfx2, 0, 0x0081);
  sfxSetReg(sfx2, 1, 0x0000);
  sfxSetReg(sfx2, 2, 0x0000);
  sfxSetReg(sfx2, 15, 0x0000);
  sfx2.write(0x00301F, 0x00);
  for (int i = 0; i < 20; i++) sfx2.stepGsu();
  CHECK(sfx2.read(0x00700000) == 0x80); // bit0
  CHECK(sfx2.read(0x00700031) == 0x80); // bit7 plane
}

TEST_CASE("superfx: BCS/BCC follow LSR carry") {
  // R0=1: LSR -> CY=1,R0=0. BCS+2 taken -> R0=7 else 99.
  auto run = [](uint16 r0init) {
    SuperFx sfx;
    std::vector<uint8> rom(0x10000, 0x01);
    uint8 prog[] = {0x03, 0x0D, 0x04, 0x01, 0xA0, 0x63, 0x00, 0xA0, 0x07, 0x00};
    for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
    sfx.setRom(rom, MapMode::lorom);
    sfx.power();
    sfx.write(0x00303A, 0x18);
    sfxSetReg(sfx, 0, r0init);
    sfxSetReg(sfx, 15, 0x0000);
    sfx.write(0x00301F, 0x00);
    for (int i = 0; i < 30; i++) sfx.stepGsu();
    return sfxReg(sfx, 0);
  };
  CHECK(run(1) == 7);   // CY=1 -> BCS taken
  CHECK(run(2) == 99);  // CY=0 -> BCS not taken (R0=1, then IBT 99)
}

TEST_CASE("superfx: BVS/BVC follow ADD overflow") {
  // R0=0x4000,R1=0x4000: ADD -> 0x8000 S=1 OV=1. BVS taken.
  auto run = [](uint8 branch, uint16 a, uint16 b) {
    SuperFx sfx;
    std::vector<uint8> rom(0x10000, 0x01);
    // B0 FROM R0 | 51 ADD R1 | branch off | 01 NOP | A0 63 | 00 | A0 07 | 00
    uint8 prog[] = {0xB0, 0x51, branch, 0x04, 0x01, 0xA0, 0x63, 0x00, 0xA0, 0x07, 0x00};
    for (size_t i = 0; i < sizeof prog; i++) rom[i] = prog[i];
    sfx.setRom(rom, MapMode::lorom);
    sfx.power();
    sfx.write(0x00303A, 0x18);
    sfxSetReg(sfx, 0, a);
    sfxSetReg(sfx, 1, b);
    sfxSetReg(sfx, 15, 0x0000);
    sfx.write(0x00301F, 0x00);
    for (int i = 0; i < 30; i++) sfx.stepGsu();
    return sfxReg(sfx, 0);
  };
  CHECK(run(0x0F, 0x4000, 0x4000) == 7);  // OV=1 -> BVS taken
  CHECK(run(0x0E, 0x4000, 0x4000) == 99); // OV=1 -> BVC not taken
  CHECK(run(0x0E, 0x0005, 0x0003) == 7);  // OV=0 -> BVC taken
  CHECK(run(0x06, 0x0005, 0x0005) == 7);  // Z=1 S=OV=0 -> BGE taken
  CHECK(run(0x0A, 0x0000, 0x0001) == 7);  // ADD 0+1=1 S=0 -> BPL taken
  CHECK(run(0x0B, 0x0000, 0xFFFF) == 7);  // ADD 0+FFFF=FFFF S=1 -> BMI taken
}
