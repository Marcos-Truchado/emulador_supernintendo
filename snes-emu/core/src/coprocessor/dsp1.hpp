#pragma once

// dsp1.hpp — NEC uPD77C25 data ROM + DSP-1/1A/1B command table.
//
// The five DSP variants share the uPD77C25 core, but DSP-1/1A/1B all use the
// same program/Data ROM (only the clock differs: 8 MHz vs 10 MHz). Rather
// than emulating the 77C25, the classic table approach runs each SNES BIOS
// "command" as a closed-form C++ math function (ZSNES → snes9x lineage).
// 1A and 1B are handled identically — clock skew is irrelevant at the
// command level.
//
// Window: HiROM 00-1F/80-9F:6000-7FFF, LoROM 30-3F/B0-BF:8000-FFFF
// (DataRAM at 6000-6FFF/8000-BFFF, status/command past boundary).
// Inside the window the stream is a single FIFO: writes push bytes,
// reads pop them. Boundary = 0x7000 HiROM, 0xC000 LoROM.

#include "coprocessor/coprocessor.hpp"

namespace snes {

class Dsp1 : public Coprocessor {
 public:
  explicit Dsp1(bool hirom);
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;

 private:
  void execCommand();

  // Tables
  static const uint16 kRom[1024];
  static const int16 kMulTable[256];
  static const int16 kSinTable[256];

  // --- FIFO ---
  bool waiting4command_ = true;
  bool firstParam_ = false;
  uint8 command_ = 0;
  uint32 inCount_ = 0, inIndex_ = 0, outCount_ = 0, outIndex_ = 0;
  uint8 params_[512]{};
  uint8 output_[512]{};

  // --- Geometry state (Parameter/Raster/Project) ---
  int16 centreX_ = 0, centreY_ = 0, vOffset_ = 0;
  int16 vPlaneC_ = 0, vPlaneE_ = 0;
  int16 sinAas_ = 0, cosAas_ = 0, sinAzs_ = 0, cosAzs_ = 0;
  int16 sinAZS_ = 0, cosAZS_ = 0, secAzsC1_ = 0, secAzsE1_ = 0, secAzsC2_ = 0, secAzsE2_ = 0;
  int16 nx_ = 0, ny_ = 0, nz_ = 0, gx_ = 0, gy_ = 0, gz_ = 0;
  int16 cLes_ = 0, eLes_ = 0, gLes_ = 0;
  int16 matrixA_[3][3]{}, matrixB_[3][3]{}, matrixC_[3][3]{};

  // Per-op scratch — flat copy of the snes9x SDSP1 field layout so the
  // ported functions stay legible.
  int16 op00Multiplicand_ = 0, op00Multiplier_ = 0, op00Result_ = 0;
  int16 op20Multiplicand_ = 0, op20Multiplier_ = 0, op20Result_ = 0;
  int16 op10Coeff_ = 0, op10Exp_ = 0, op10CoeffR_ = 0, op10ExpR_ = 0;
  int16 op04Angle_ = 0; uint16 op04Radius_ = 0; int16 op04Sin_ = 0, op04Cos_ = 0;
  int16 op0CA_ = 0, op0CX1_ = 0, op0CY1_ = 0, op0CX2_ = 0, op0CY2_ = 0;
  int16 op02FX_ = 0, op02FY_ = 0, op02FZ_ = 0, op02LFE_ = 0, op02LES_ = 0;
  uint16 op02AAS_ = 0, op02AZS_ = 0; int16 op02VOF_ = 0, op02VVA_ = 0, op02CX_ = 0, op02CY_ = 0;
  int16 op0AVS_ = 0, op0AA_ = 0, op0AB_ = 0, op0AC_ = 0, op0AD_ = 0;
  int16 op06X_ = 0, op06Y_ = 0, op06Z_ = 0, op06H_ = 0, op06V_ = 0, op06M_ = 0;
  int16 op01m_ = 0, op01Zr_ = 0, op01Yr_ = 0, op01Xr_ = 0;
  int16 op11m_ = 0, op11Zr_ = 0, op11Yr_ = 0, op11Xr_ = 0;
  int16 op21m_ = 0, op21Zr_ = 0, op21Yr_ = 0, op21Xr_ = 0;
  int16 op0DX_ = 0, op0DY_ = 0, op0DZ_ = 0, op0DF_ = 0, op0DL_ = 0, op0DU_ = 0;
  int16 op1DX_ = 0, op1DY_ = 0, op1DZ_ = 0, op1DF_ = 0, op1DL_ = 0, op1DU_ = 0;
  int16 op2DX_ = 0, op2DY_ = 0, op2DZ_ = 0, op2DF_ = 0, op2DL_ = 0, op2DU_ = 0;
  int16 op03F_ = 0, op03L_ = 0, op03U_ = 0, op03X_ = 0, op03Y_ = 0, op03Z_ = 0;
  int16 op13F_ = 0, op13L_ = 0, op13U_ = 0, op13X_ = 0, op13Y_ = 0, op13Z_ = 0;
  int16 op23F_ = 0, op23L_ = 0, op23U_ = 0, op23X_ = 0, op23Y_ = 0, op23Z_ = 0;
  int16 op0BX_ = 0, op0BY_ = 0, op0BZ_ = 0, op0BS_ = 0;
  int16 op1BX_ = 0, op1BY_ = 0, op1BZ_ = 0, op1BS_ = 0;
  int16 op2BX_ = 0, op2BY_ = 0, op2BZ_ = 0, op2BS_ = 0;
  int16 op14Zr_ = 0, op14Xr_ = 0, op14Yr_ = 0, op14U_ = 0, op14F_ = 0, op14L_ = 0;
  int16 op14Zrr_ = 0, op14Xrr_ = 0, op14Yrr_ = 0;
  int16 op0EH_ = 0, op0EV_ = 0, op0EX_ = 0, op0EY_ = 0;
  int16 op08X_ = 0, op08Y_ = 0, op08Z_ = 0; uint16 op08Ll_ = 0, op08Lh_ = 0;
  int16 op18X_ = 0, op18Y_ = 0, op18Z_ = 0, op18R_ = 0, op18D_ = 0;
  int16 op38X_ = 0, op38Y_ = 0, op38Z_ = 0, op38R_ = 0, op38D_ = 0;
  int16 op28X_ = 0, op28Y_ = 0, op28Z_ = 0, op28R_ = 0;
  int16 op1CX_ = 0, op1CY_ = 0, op1CZ_ = 0, op1CXBR_ = 0, op1CYBR_ = 0, op1CZBR_ = 0;
  int16 op1CX1_ = 0, op1CY1_ = 0, op1CZ1_ = 0, op1CXAR_ = 0, op1CYAR_ = 0, op1CZAR_ = 0;
  int16 op0FPass_ = 0, op0FRamsize_ = 0, op2FSize_ = 0, op2FUnknown_ = 0;

  // Helpers ported verbatim
  void op00(), op20(), op10(), op04(), op0C(), op02(), op0A(), op06(), op01(), op11(), op21();
  void op0D(), op1D(), op2D(), op03(), op13(), op23(), op0B(), op1B(), op2B(), op14(), op0E();
  void op08(), op18(), op38(), op28(), op1C(), op0F(), op2F();
  int16 dspSin(int16 a), dspCos(int16 a);
  void dspInverse(int16 c, int16 e, int16* ic, int16* ie);
  void dspNormalize(int16 m, int16* c, int16* e);
  void dspNormalizeDouble(int32 p, int16* c, int16* e);
  int16 dspTruncate(int16 c, int16 e);
  void dspParameter(int16 fx, int16 fy, int16 fz, int16 lfe, int16 les, int16 aas, int16 azs,
                    int16* vof, int16* vva, int16* cx, int16* cy);
  void dspRaster(int16 vs, int16* an, int16* bn, int16* cn, int16* dn);
  void dspProject(int16 x, int16 y, int16 z, int16* h, int16* v, int16* m);
  void dspTarget(int16 h, int16 v, int16* x, int16* y);
  int16 dspShiftR(int16 c, int16 e);

  bool hirom_ = false;
  uint16 boundary_ = 0xC000;
};

}  // namespace snes
