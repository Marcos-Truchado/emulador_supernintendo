#pragma once

// sdd1.hpp — S-DD1 data decompressor (Star Ocean, Street Fighter Alpha 2).
// LoROM 00-3F/80-BF:4800-4807 + DMA hook C0-FF:0000-FFFF.
// Based on fullsnes "S-DD1 I/O Ports" and snes9x sdd1emu.cpp (Andreas Naive).
// This is a faithful port of the HLE decompressor — not a stub. The chip
// decompresses on the fly as DMA reads from ROM; we hook Bus::dmaTransfer.
//
// References: snes9x sdd1emu.cpp / sdd1.cpp, fullsnes.txt S-DD1,
// wiki.superfamicom.org/s-dd1, gufranco/snes-sdd1-python.

#include "coprocessor/coprocessor.hpp"

#include <array>
#include <vector>

namespace snes {

class Sdd1 : public Coprocessor {
 public:
  Sdd1();
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;
  auto setRom(const std::vector<uint8>& rom, MapMode mode) -> void override;

  // DMA hook: true when this channel should be decompressed.
  auto isActive() const -> bool { return sdd1Enable_ && xferEnable_; }
  auto activeForChannel(int channel) const -> bool {
    return (sdd1Enable_ & xferEnable_ & (1 << channel)) != 0;
  }
  void clearChannel(int channel) { xferEnable_ &= uint8_t(~(1 << channel)); }
  // Decompress `len` bytes from `in` (compressed) to `out` (decompressed).
  // `len==0` means 0x10000 as per DMA spec.
  void decompressBlock(const uint8* in, uint8* out, int len) const;

  // for Bus::dmaTransfer to get the correct input pointer via MMC
  auto getMappedRomPointer(uint32 snesAddr) const -> const uint8*;

 private:
  // registers $4800-$4807
  uint8 sdd1Enable_ = 0;  // $4800 bit mask per channel
  uint8 xferEnable_ = 0;  // $4801 bit mask, set on DMA start
  std::array<uint32, 4> mmc_{};  // $4804-$4807 <<20, maps 1Mbit banks

  const uint8* romData_ = nullptr;
  size_t romSize_ = 0;
  MapMode romMode_ = MapMode::unknown;
};

}  // namespace snes
