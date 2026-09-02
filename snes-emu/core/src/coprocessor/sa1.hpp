#pragma once

// sa1.hpp — SA-1 (Super Accelerator) second 65816 at 10.74MHz.
// Used by 34 games (Kirby Super Star, SMRPG, etc.).
// Features: I-RAM 2KB 3000-37FF, BW-RAM up to 2Mbit mapped via 2224/2225,
// ROM banking 2220-2223 (C0-FF/D0-F0), interrupts 2200-220B/2300-2301,
// vectors 2203-2208 / 220C-220F, timer 2210-2215, DMA 2230-2239,
// char-conv 2231/2240-224F+2300, maths 2250-2254 (40-bit), VLD 2258-225B/230C/D, bitmap 223F.
// HLE, not cycle-accurate. Second CPU runs synchronously with main CPU.
// References: fullsnes SA-1, snes9x sa1.cpp/sa1cpu.cpp, bsnes sa1.

#include "coprocessor/coprocessor.hpp"

#include <array>
#include <memory>
#include <vector>

namespace snes {

class Cpu65816;
class Scheduler;
class Ppu;

class Sa1 : public Coprocessor {
 public:
  Sa1();
  ~Sa1();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;
  auto setRom(const std::vector<uint8>& rom, MapMode mode) -> void override;
  void setSram(std::vector<uint8>* sram);
  void setMainCpu(Cpu65816* cpu) { mainCpu_ = cpu; }

  // Bus hooks
  auto mapRomAddress(uint24 snesAddr) const -> uint32; // UINT32_MAX = not mapped
  void stepSa1(); // run one SA-1 instruction (called by System)

 private:
  // SA-1 Bus (Memory) for second CPU
  class Sa1Bus : public Memory {
   public:
    explicit Sa1Bus(Sa1& sa1) : sa1_(sa1) {}
    auto read(uint24 address) -> uint8 override;
    auto write(uint24 address, uint8 data) -> void override;
    auto waitStates(uint24 address) -> uint8 override;
   private:
    Sa1& sa1_;
  };

  auto sa1Read(uint24 address) -> uint8;
  auto sa1Write(uint24 address, uint8 data) -> void;
  auto readReg(uint16 addr) -> uint8;
  void writeReg(uint16 addr, uint8 data);
  void updateIrq();
  void doMath();
  void doDma();
  void doCharConv();
  void doVld(bool inc, bool noShift);

  // regs 2200-22FF + 2300-230F mirrored in 00-3F/80-BF
  std::array<uint8, 0x400> regs_{};
  std::array<uint8, 0x800> iram_{}; // 3000-37FF
  // bwRam is alias to Bus::sram (set via setSram)
  std::vector<uint8>* sram_ = nullptr;
  // math
  uint16 op1_ = 0, op2_ = 0;
  uint64 sum_ = 0;
  bool overflow_ = false;
  uint8 arithMode_ = 0;
  // vld
  uint8 vbitPos_ = 0;
  // char conv state
  bool inCharDma_ = false;
  uint8 charDmaPos_ = 0;
  // timer
  uint16 hTimer_ = 0, vTimer_ = 0;
  uint16 hCounter_ = 0, vCounter_ = 0;
  // banking cache
  uint8 bwRamMapSnes_ = 0, bwRamMapSa1_ = 0;
  // ROM
  const uint8* romData_ = nullptr;
  size_t romSize_ = 0;
  MapMode romMode_ = MapMode::unknown;
  // second CPU
  std::unique_ptr<Ppu> dummyPpu_;
  std::unique_ptr<Sa1Bus> sa1Bus_;
  std::unique_ptr<Scheduler> sa1Scheduler_;
  std::unique_ptr<Cpu65816> sa1Cpu_;
  // callbacks to main CPU irq
  Cpu65816* mainCpu_ = nullptr; // set via System
  friend class SaBusHelper;
};

}  // namespace snes
