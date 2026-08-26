#pragma once

// dsp3.hpp — SD Gundam G-NEXT / Gundam W Endless Duel BIOS.
// Port of snes9x dsp3.cpp (ZSNES table lineage). State machine driven by
// a function-pointer (here an enum Handler) plus DR/SR registers and large
// terrain/cost/weight tables for the OP1E pathfinder.
//
// Window: LoROM 20-3F/A0-BF:8000-FFFF, boundary 0xC000.

#include "coprocessor/coprocessor.hpp"

namespace snes {

class Dsp3 : public Coprocessor {
 public:
  Dsp3();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;

 private:
  static const uint16 kDataRom[1024];

  enum class Handler : uint8 {
    Reset = 0,
    Command,
    TestMemory,
    DumpDataRom,
    MemoryDump,
    Coordinate,
    Convert,
    ConvertA,
    Op03,
    Op06,
    Op07,
    Op07A,
    Op07B,
    Op10,
    Op0C,
    Op1C,
    Op1C_A,
    Op1C_B,
    Op1C_C,
    Op1E,
    Op1E_A,
    Op1E_A1,
    Op1E_A2,
    Op1E_A3,
    Op1E_B,
    Op1E_B1,
    Op1E_C,
    Op1E_C1,
    Op1E_C2,
    Decode,
    DecodeA,
    DecodeSymbols,
    DecodeTree,
    DecodeData,
    Op3E,
  };

  void setHandler(Handler h);
  void callHandler();

  // handlers
  void hReset(), hCommand(), hTestMemory(), hDumpDataRom(), hMemoryDump();
  void hCoordinate(), hConvert(), hConvertA(), hOp03(), hOp06(), hOp07(), hOp07A(), hOp07B();
  void hOp10(), hOp0C(), hOp1C(), hOp1C_A(), hOp1C_B(), hOp1C_C();
  void hOp1E(), hOp1E_A(), hOp1E_A1(), hOp1E_A2(), hOp1E_A3(), hOp1E_B(), hOp1E_B1(), hOp1E_B2(), hOp1E_C(), hOp1E_C1(), hOp1E_C2();
  void hDecode(), hDecodeA(), hDecodeSymbols(), hDecodeTree(), hDecodeData();
  void hOp3E();
  void op1eD(int16 move, int16* lo, int16* hi);
  void op1eD1(int16 move, int16* lo, int16* hi);
  bool getBits(uint8 count);

  // ---- state (mirrors snes9x SDSP3) ----
  uint16 dr_ = 0x0080, sr_ = 0x0084;
  uint16 memoryIndex_ = 0;
  int16 winLo_ = 0, winHi_ = 0, addLo_ = 0, addHi_ = 0;
  uint16 codewords_ = 0, outwords_ = 0, symbol_ = 0, bitCount_ = 0, index_ = 0;
  uint16 codes_[512]{};
  uint16 bitsLeft_ = 0, reqBits_ = 0, reqData_ = 0, bitCommand_ = 0;
  uint8 baseLength_ = 0;
  uint16 baseCodes_ = 0, baseCode_ = 0;
  uint8 codeLengths_[8]{};
  uint16 codeOffsets_[8]{};
  uint16 lzCode_ = 0;
  uint8 lzLength_ = 0;
  uint16 x_ = 0, y_ = 0;
  uint8 bitmap_[8]{}, bitplane_[8]{};
  uint16 bmIndex_ = 0, bpIndex_ = 0, count_ = 0;
  int16 op3eX_ = 0, op3eY_ = 0;
  int16 terrain_[0x2000]{}, cost_[0x2000]{}, weight_[0x2000]{};
  int16 op1eCell_ = 0, op1eTurn_ = 0, op1eSearch_ = 0;
  int16 op1eX_ = 0, op1eY_ = 0;
  int16 op1eMinRadius_ = 0, op1eMaxRadius_ = 0, op1eMaxSearchRadius_ = 0, op1eMaxPathRadius_ = 0;
  int16 op1eLcvRadius_ = 0, op1eLcvSteps_ = 0, op1eLcvTurns_ = 0;

  Handler handler_ = Handler::Command;
};

}  // namespace snes
