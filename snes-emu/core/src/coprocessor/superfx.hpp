#pragma once

// superfx.hpp — GSU 1/2 (SuperFX) RISC 10.74/21MHz for Star Fox, Yoshi's Island etc.
// Handles 3000-32FF regs + GSU-side ROM/RAM. HLE interpreter, not cycle-exact.
// References: fullsnes GSU, snes9x fxemu/fxinst, bsnes gsu.

#include "coprocessor/coprocessor.hpp"
#include <array>
#include <vector>

namespace snes {

class SuperFx : public Coprocessor {
 public:
  SuperFx();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;
  auto setRom(const std::vector<uint8>& rom, MapMode mode) -> void override;
  void setSram(std::vector<uint8>* sram);

  void stepGsu(); // called by System
  auto mapRomAddress(uint24 snesAddr) const -> uint32;
  auto irqLine() const -> bool { return (regs_.sfr & 0x8000) && !(regs_.cfgr & 0x80); }
  auto gsuRunning() const -> bool { return regs_.sfr & 0x0020; }

 private:
  // GSU regs (snes9x fxinst.h / fullsnes GSU I/O Map)
  // SFR bits: Z=0x0002 CY=0x0004 S=0x0008 OV=0x0010 GO=0x0020 ALT1=0x0100 ALT2=0x0200 B=0x1000 IRQ=0x8000
  struct Regs {
    uint16 r[16]{};
    uint16 sfr = 0;
    uint8 pbr = 0;
    uint8 rombr = 0;
    uint8 rambr = 0;
    uint16 cbr = 0;
    uint8 scbr = 0;
    uint8 scmr = 0;
    uint8 colr = 0;
    uint8 por = 0;
    uint8 clsr = 0;
    uint8 cfgr = 0;
    uint8 vcr = 4; // GSU2
    uint8 bramr = 0;
    uint8 sregIdx = 0;
    uint8 dregIdx = 0;
    uint8 romBuffer = 0;
    uint32 lastRamAdr = 0;
  } regs_;
  // screen for PLOT (snes9x fxemu.cpp fx_readRegisterSpace/fx_computeScreenPointers)
  uint8* pvScreenBase_ = nullptr;
  uint8* apvScreen_[32]{};
  int32 x_[32]{};
  uint32 vScreenHeight_ = 128;
  uint32 vScreenRealHeight_ = 128;
  uint32 vScreenSize_ = 0;
  uint32 vMode_ = 0;
  uint32 vPrevMode_ = 0xFFFFFFFFu;
  uint32 vPrevScreenHeight_ = 0xFFFFFFFFu;
  bool scbrDirty_ = true;

  // GSU RAM: 64KB for GSU-2, 32KB for GSU-1. Use 64KB.
  std::array<uint8, 0x10000> gsuRam_{};
  // Cache: 512 bytes (GSU-1) / 2x512 (GSU-2)
  std::array<uint8, 512> cache_{};
  bool cacheDirty_ = true;

  // Control regs 3000-32FF mirror
  std::array<uint8, 0x300> gsuRegs_{}; // 3000-32FF -> 0x300 bytes

  // status
  bool gsuGo_ = false;
  bool gsuIrq_ = false;
  bool cacheActive_ = false;
  uint8 latch_ = 0; // SNES 3000-301F even/odd latch
  uint32 gsuClock_ = 10740000;
  uint64 gsuCycles_ = 0;

  const uint8* romData_ = nullptr;
  size_t romSize_ = 0;
  size_t nRomBanks_ = 1; // 64KB banks (cap 0x20 like snes9x)
  std::vector<uint8>* sram_ = nullptr;
  // pending jump for delay-slot handling (transient inside execAt)
  struct PendingJump { bool active = false; bool withDelay = true; uint8 bank = 0; uint16 dest = 0; };
  PendingJump jump_;

  // helpers
  auto gsuRead(uint32 addr) -> uint8;
  auto gsuWrite(uint32 addr, uint8 data) -> void;
  void execOne();
  void execAt(uint8 bank, uint16 pc, bool withDelay);
  bool checkSfr(uint16 flag) const { return (regs_.sfr & flag) != 0; }
  // snes9x faithful core (fxinst.cpp/fxemu.cpp): program/data access
  auto progByte(uint8 bank, uint16 addr) -> uint8;
  auto romByte(uint8 bank, uint16 addr) -> uint8;
  auto ramByte(uint32 addr) -> uint8;
  void ramWriteByte(uint32 addr, uint8 v);
  auto ramWord(uint16 addr) -> uint16;
  void ramWriteWord(uint16 addr, uint16 v);
  void refillRomBuffer() { regs_.romBuffer = romByte(regs_.rombr, regs_.r[14]); }
  auto readSReg() const -> uint16 { return regs_.r[regs_.sregIdx & 15]; }
  void writeDReg(uint16 v);
  void storeReg(uint8 idx, uint16 v);
  void setLogicFlags(uint16 v);
  void setAddFlags(uint16 s, uint16 n, int32 sum);
  void setSubFlags(uint16 s, uint16 n, int32 diff);
  void requestJump(uint8 bank, uint16 dest);
  bool startSession();  // validate PBR/RAN/RON + init pipe-less state
  void goFromSnes();    // SNES GO edge: reset prefixes, sync screen, validate
  // snes9x faithful PLOT path (fxinst.cpp/fxemu.cpp)
  auto ramBase() -> uint8*;
  auto ramBase() const -> const uint8*;
  auto ramSize() const -> size_t;
  void syncScreenRegs(); // SCBR/SCMR/POR -> pvScreenBase_/vMode_/vScreenHeight_
  void computeScreenPointers();
  void clearPlotFlags() { regs_.sfr &= uint16(~(0x0100|0x0200|0x1000)); regs_.sregIdx = 0; regs_.dregIdx = 0; }
  void opPlot2bit(), opPlot4bit(), opPlot8bit();
  void opRpix2bit(), opRpix4bit(), opRpix8bit();
  void opColor(), opCmode(), opCache();
};

} // namespace snes
