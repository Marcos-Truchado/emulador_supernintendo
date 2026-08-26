#pragma once

// srtc.hpp — Sharp S-RTC (4513) real-time clock.
// One game: Dai Kaijuu Monogatari 2 (ExHiROM, $55 chipset).
// Protocol: $2801 selects mode (0x0E→Command, 0x0D→Read, write-mode entered
// with 0x00), $2800 streams 13 BCD nibbles (second … year). reg[16..19]
// holds the unix time of the last update so the wall clock keeps ticking
// across saves. Ported from byuu's srtcemu (snes9x master).

#include "coprocessor/coprocessor.hpp"

#include <array>

namespace snes {

class Srtc : public Coprocessor {
 public:
  auto handles(uint24 address) const -> bool override;
  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto power() -> void override;
  auto serialize(Writer& w) const -> void override;
  auto deserialize(Reader& r) -> void override;

  // Public for the weekday unit test.
  static auto weekday(unsigned year, unsigned month, unsigned day) -> unsigned;
  static const unsigned kMonths[12];

 private:
  void updateTime();
  void reset();

  enum Mode : uint8 { Ready = 0, Command = 1, Read = 2, Write = 3 };

  std::array<uint8, 20> reg_{};
  Mode mode_ = Read;
  int index_ = -1;  // -1 sentinel before the 0..12 stream
};

}  // namespace snes
