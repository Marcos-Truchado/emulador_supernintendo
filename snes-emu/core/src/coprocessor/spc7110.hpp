#pragma once

// spc7110.hpp — Epson SPC7110F0A data decompressor + RTC-4513 + bank mapper.
// Used by 3 Hudson games: FEoEZ (5MB+RTC), SPL4 (3MB), MDH (3MB).
// HiROM + SPC7110 mapping $C00000/$D00000/$E00000/$F00000 + SRAM 6000-7FFF.
//
// Windows: 4800-4842 in banks 00-3F/80-BF (mirrored at 500000/580000).
// Data decompression via arithmetic coder (53 states), 1/2/4bpp Morton.
// References: fullsnes SPC7110, bsnes spc7110/{spc7110,dcu,decompressor,data,alu},
// snes9x spc7110.cpp/spc7110emu.cpp/spc7110dec.cpp, wiki.superfamicom.org/spc7110.

#include "coprocessor/coprocessor.hpp"

#include <array>
#include <vector>

namespace snes {

class Spc7110 : public Coprocessor {
 public:
  Spc7110();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;
  auto setRom(const std::vector<uint8>& rom, MapMode mode) -> void override;

  // Bus hooks
  auto sramEnabled() const -> bool { return (r4830_ & 0x80) != 0; }
  auto mapRomAddress(uint24 snesAddr) const -> uint32;  // UINT32_MAX = not mapped
  auto isDataRomWindow(uint24 snesAddr) const -> bool;

 private:
  // helpers
  auto dataromRead(uint32 addr) const -> uint8;
  auto dataromAddr(uint32 addr) const -> uint32;
  auto dataPointer() const -> uint32;
  auto dataAdjust() const -> uint32;
  auto dataIncrement() const -> uint32;
  void setDataPointer(uint32 addr);
  void setDataAdjust(uint32 addr);
  void dataPortRead();
  void dataPortInc4810();
  void dataPortInc4814();
  void dataPortInc4815();
  void dataPortInc481a();

  void dcuLoadAddress();
  void dcuBeginTransfer();
  auto dcuRead() -> uint8;

  void aluMultiply();
  void aluDivide();

  void rtcUpdate();
  auto rtcRead() -> uint8;
  void rtcWrite(uint8 data);

  // ---- registers ----
  uint8 r4801_ = 0, r4802_ = 0, r4803_ = 0, r4804_ = 0;
  uint8 r4805_ = 0, r4806_ = 0, r4807_ = 0, r4808_ = 0;
  uint8 r4809_ = 0, r480a_ = 0, r480b_ = 0, r480c_ = 0;

  uint8 r4810_ = 0;
  uint8 r4811_ = 0, r4812_ = 0, r4813_ = 0;
  uint8 r4814_ = 0, r4815_ = 0;
  uint8 r4816_ = 0, r4817_ = 0;
  uint8 r4818_ = 0;
  uint8 r481x_ = 0;
  bool r4814_latch_ = false, r4815_latch_ = false;

  uint8 r4820_ = 0, r4821_ = 0, r4822_ = 0, r4823_ = 0;
  uint8 r4824_ = 0, r4825_ = 0, r4826_ = 0, r4827_ = 0;
  uint8 r4828_ = 0, r4829_ = 0, r482a_ = 0, r482b_ = 0;
  uint8 r482c_ = 0, r482d_ = 0;
  uint8 r482e_ = 0, r482f_ = 0;

  uint8 r4830_ = 0, r4831_ = 0, r4832_ = 0, r4833_ = 0, r4834_ = 0;

  uint8 r4840_ = 0, r4841_ = 0, r4842_ = 0;
  // rtc state
  enum class RtcState : uint8 { Inactive, ModeSelect, IndexSelect, Write };
  enum class RtcMode : uint8 { Linear = 0x03, Indexed = 0x0C };
  RtcState rtcState_ = RtcState::Inactive;
  RtcMode rtcMode_ = RtcMode::Linear;
  uint8 rtcIndex_ = 0;
  std::array<uint8, 20> rtcRegs_{};
  // last time sync
  uint32 rtcLastSec_ = 0;

  // bank offsets for D/E/F windows
  uint32 dxOffset_ = 0, exOffset_ = 0, fxOffset_ = 0;

  // DCU
  uint8 dcuMode_ = 0;
  uint32 dcuAddress_ = 0;
  uint8 dcuTile_[32]{};
  uint8 dcuOffset_ = 0;

  // decompressor state
  struct Context { uint8 pred = 0; uint8 swap = 0; };
  std::array<std::array<Context, 15>, 5> ctx_{};
  uint32 bpp_ = 1;
  uint32 offset_ = 0;
  uint32 bits_ = 8;
  uint16 range_ = 0x100;
  uint16 input_ = 0;
  uint8 output_ = 0;
  uint64 pixels_ = 0;
  uint64 colormap_ = 0xfedcba9876543210ULL;
  uint32 result_ = 0;

  // ROM
  const uint8* romData_ = nullptr;
  size_t romSize_ = 0;

  static struct Model { uint8 prob; uint8 next[2]; } evolution_[53];

  auto deinterleave(uint64 data, uint bits) -> uint32;
  auto moveToFront(uint64 list, uint64 nibble) -> uint64;
  void decompInit(uint32 mode, uint32 origin);
  void decompDecode();
};

}  // namespace snes
