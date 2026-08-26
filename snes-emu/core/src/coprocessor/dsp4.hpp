#pragma once

// dsp4.hpp — Top Gear 3000 projection/sprite co-processor.
// Port of snes9x dsp4.cpp (Crémat/Overload). State machine uses goto-resume
// Logic ids (0..6) for each opcode's incremental I/O. Window
// 30-3F/B0-BF:8000-FFFF, boundary 0xC000, half-word command packing.

#include "coprocessor/coprocessor.hpp"

namespace snes {

class Dsp4 : public Coprocessor {
 public:
  Dsp4();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;

 private:
  // snes9x DSP4_WAIT macro: save.Logic and return from the running opcode.
  // Our op loops call checkInput() which returns false if more input is needed
  // (caller returns to Bus, will re-enter the same opcode next write/read).
  bool needInput(uint32 need);

  void op01(), op03(), op05(), op06(), op07(), op08(), op09(), op0D(), op0E(), op0F(), op10();
  void op0A(int16 n2, int16* o1,int16* o2,int16* o3,int16* o4);
  void op0B(bool* draw, int16 x,int16 y,int16 attr,bool size,bool stop);
  void op11(int16 A,int16 B,int16 C,int16 D,int16* M);
  int16 inv(int16 v);
  void multiply(int16 a,int16 b,int32* p);
  int16 readWord();
  int32 readDword();
  void writeWord(int16 v);
  void writeByte(uint8 v);
  void clearOut();
  void setByte();
  void getByte();

  // ----- state (mirrors SDSP4) -----
  bool waiting4command_ = true, halfCommand_ = false;
  uint16 command_ = 0;
  uint32 inCount_ = 0, inIndex_ = 0, outCount_ = 0, outIndex_ = 0;
  uint8 params_[512]{}, output_[512]{};
  uint8 byte_ = 0;
  uint16 address_ = 0;
  int8 logic_ = 0;

  int16 lcv_ = 0, distance_ = 0, raster_ = 0, segments_ = 0;
  int32 worldX_ = 0, worldY_ = 0, worldDx_ = 0, worldDy_ = 0;
  int16 worldDdx_ = 0, worldDdy_ = 0;
  int32 worldXenv_ = 0;
  int16 worldYofs_ = 0, viewX1_ = 0, viewY1_ = 0, viewX2_ = 0, viewY2_ = 0;
  int16 viewDx_ = 0, viewDy_ = 0;
  int16 viewXofs1_ = 0, viewYofs1_ = 0, viewXofs2_ = 0, viewYofs2_ = 0, viewYofsenv_ = 0;
  int16 viewTurnoffX_ = 0, viewTurnoffDx_ = 0;
  int16 viewportCx_ = 0, viewportCy_ = 0, viewportLeft_ = 0, viewportRight_ = 0, viewportTop_ = 0, viewportBottom_ = 0;
  int16 polyClipLf_[2][2]{}, polyClipRt_[2][2]{};
  int16 polyPtr_[2][2]{}, polyRaster_[2][2]{}, polyTop_[2][2]{}, polyBottom_[2][2]{}, polyCx_[2][2]{};
  int16 polyStart_[2]{}, polyPlane_[2]{};
  int16 spriteX_ = 0, spriteY_ = 0, spriteAttr_ = 0;
  bool spriteSize_ = false;
  int16 spriteClipy_ = 0, spriteCount_ = 0;
  int16 oamAttr_[16]{}, oamIndex_ = 0, oamBits_ = 0, oamRowMax_ = 0, oamRow_[32]{};
};

}  // namespace snes
