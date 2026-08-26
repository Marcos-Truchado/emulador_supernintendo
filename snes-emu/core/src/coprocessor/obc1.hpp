#pragma once

// obc1.hpp — Nintendo OBC-1 OBJ controller.
// 8KB SRAM at 00-3F/80-BF:6000-7FFF. Last 16 bytes are control registers:
//
//   7FF0-7FF3  sprite bytes [address<<2 + N] at basePtr
//   7FF4       2-bit plane/priority nibble at basePtr+(address>>2)+0x200
//   7FF5       basePtr (bit0: 0x1C00/0x1800)
//   7FF6       address+shift
//   7FF7       misc scratch
// Ported from snes9x obc1.cpp / bsnes obc1.cpp (bsnes semantics for
// register vs RAM mirroring: writes to 7FF0-4 don't duplicate into RAM).

#include "coprocessor/coprocessor.hpp"

#include <array>

namespace snes {

class Obc1 : public Coprocessor {
 public:
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;

  // Exposed for bus save-state / tests (the 8KB).
  auto ram() -> std::array<uint8, 0x2000>& { return ram_; }
  auto ram() const -> const std::array<uint8, 0x2000>& { return ram_; }

 private:
  std::array<uint8, 0x2000> ram_{};
  uint16 basePtr_ = 0x1C00;
  uint8 address_ = 0;
  uint8 shift_ = 0;
};

}  // namespace snes
