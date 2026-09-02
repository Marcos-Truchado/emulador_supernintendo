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

 private:
  // GSU regs
  struct Regs {
    uint16 r[16]{};
    uint16 sfr = 0;
    uint16 pbr = 0;
    uint8 rombr = 0;
    uint8 rambr = 0;
    uint8 cbr = 0;
    uint16 scmr = 0;
    uint16 sreg = 0;
    uint32 gsuAddr = 0;
    uint8 gsuData = 0;
    uint8 scbr = 0;
    uint8 vcr = 0;
    uint8 ramr = 0;
    uint8 clsr = 0;
  } regs_;

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
  uint32 gsuClock_ = 10740000;
  uint64 gsuCycles_ = 0;

  const uint8* romData_ = nullptr;
  size_t romSize_ = 0;
  std::vector<uint8>* sram_ = nullptr;

  // helpers
  auto gsuRead(uint32 addr) -> uint8;
  auto gsuWrite(uint32 addr, uint8 data) -> void;
  uint16 getReg(uint8 r) const { return regs_.r[r & 15]; }
  void setReg(uint8 r, uint16 v) { regs_.r[r & 15] = v; }
  void execOne();
  void updateSfr(uint16 v);
  bool checkSfr(uint16 flag) const { return (regs_.sfr & flag) != 0; }

  // opcode handlers (subset, enough for StarFox/Yoshi)
  void opSTOP(), opNOP(), opCACHE(), opLSR(), opROL(), opBRA(int8), opBGE(int8), opBLT(int8), opBEQ(int8), opBNE(int8);
  void opTO_RN(uint8), opFROM_RN(uint8), opADD(uint8), opADC(uint8), opSUB(uint8), opCMP(uint8);
  void opINC(uint8), opDEC(uint8), opMULT(uint8), opUMULT(uint8), opFMULT(), opLMULT();
  void opAND(uint8), opOR(uint8), opXOR(uint8), opBIC(uint8), opNOT(uint8);
  void opASR(uint8), opDIV2(uint8), opROR(uint8), opLOB(), opSBC(uint8);
  void opADC_IMM(uint8), opADD_IMM(uint8), opCMP_IMM(uint8), opAND_IMM(uint8), opOR_IMM(uint8), opBIC_IMM(uint8);
  void opLOAD(uint8), opSTORE(uint8), opMOVE(uint8,uint8), opSWAP(uint8), opCOLOR(), opPLOT(), opRPIX();
  void opLOOP(), opALT1(), opALT2(), opALT3(), opJMP(uint8), opLJMP(uint8), opLINK(uint8);
  void opMERGE(), opSBK(), opGETB(), opGETBH(), opGETBL(), opGETBS();
  void opRAMB(), opROMB(), opCMODE(), opWITH(uint8), opHIB(), opFROMSB(uint8), opTOSB(uint8);
};

} // namespace snes
