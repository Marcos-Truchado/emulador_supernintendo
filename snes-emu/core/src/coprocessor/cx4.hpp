#pragma once

// cx4.hpp — Capcom CX4 (Hitachi HG51B169) for Mega Man X2/X3.
// LoROM 00-3F/80-BF:6000-7FFF (3 KiB RAM at 6000-6BFF + regs at 7F40-7FFF).
// Based on fullsnes "CX4 I/O Map" and the snes9x c4 / c4emu HLE port
// (ZSNES → snes9x lineage, decap by Overload). This is the classic
// table/command approach, not a full HG51B core: writes to $7F4F dispatch
// closed-form math (wireframe, trig, scale/rotate) that the games expect.
// Data ROM is synthesized from closed forms at runtime — no firmware file.
//
// References: fullsnes.txt "SNES Cart Capcom CX4", snes9x c4.cpp / c4emu.cpp,
// wiki.superfamicom.org/capcom-cx4-hitachi-hg51b169, ares cx4.

#include "coprocessor/coprocessor.hpp"

#include <array>
#include <vector>

namespace snes {

class Cx4 : public Coprocessor {
 public:
  Cx4();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;
  auto setRom(const std::vector<uint8>& rom, MapMode mode) -> void override;

 private:
  // helpers operating on ram_ (offsets are 0x6000-based, like snes9x C4RAM)
  auto ramReadWord(uint16 off) const -> uint16;
  auto ramRead3Word(uint16 off) const -> uint32;
  void ramWriteWord(uint16 off, uint16 v);
  void ramWrite3Word(uint16 off, uint32 v);
  auto getRomPointer(uint32 snesAddr) const -> const uint8*;
  auto getCx4Pointer(uint32 snesAddr) -> uint8*;
  auto getCx4Pointer(uint32 snesAddr) const -> const uint8*;
  void doDma();
  void execCommand(uint8 cmd);

  // c4.cpp wireframe helpers (operate on ram_ + scale regs)
  void transfWireFrame();
  void transfWireFrame2();
  void calcWireFrame();
  void op1F();
  void op15();
  void op0D();

  // c4emu helpers
  void convOam();
  void doScaleRotate(int rowPadding);
  void drawLine(int32 x1, int32 y1, int16 z1, int32 x2, int32 y2, int16 z2, uint8 color);
  void drawWireFrame();
  void transformLines();
  void bitPlaneWave();
  void sprDisintegrate();
  void processSprites();

  // 8 KiB window (6000-7FFF), only 0xC00 used but mirror for simplicity.
  // Matches snes9x Memory.C4RAM[0x2000].
  std::array<uint8, 0x2000> ram_{};

  // Cartridge ROM for DMA / program fetch. Not owned.
  const uint8* romData_ = nullptr;
  size_t romSize_ = 0;
  MapMode romMode_ = MapMode::unknown;

  // busy + program paging (7F49-4B, 7F4D-4E, 7F48/7F52)
  bool busy_ = false;
  uint32 progBase_ = 0;   // 24-bit base from 7F49-4B (LoROM addr)
  uint16 progPage_ = 0;   // 7F4D-4E
  uint8 cacheEn_ = 0;     // 7F48

  // transient scale regs from c4.cpp (ported as members, not globals)
  int16 wfxVal_ = 0, wfyVal_ = 0, wfzVal_ = 0;
  int16 wfx2Val_ = 0, wfy2Val_ = 0, wfDist_ = 0, wfScale_ = 0;
  int16 f41FXVal_ = 0, f41FYVal_ = 0, f41FAngleRes_ = 0, f41FDist_ = 0, f41FDistVal_ = 0;

  // scratch doubles for transf (kept as members to avoid re-alloc)
  double c4x_ = 0, c4y_ = 0, c4z_ = 0, c4x2_ = 0, c4y2_ = 0, c4z2_ = 0, tanVal_ = 0;
};

}  // namespace snes
