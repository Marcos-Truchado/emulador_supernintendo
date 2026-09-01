#pragma once

// coprocessor.hpp — on-cart enhancement chips (S-RTC, OBC-1, DSP family, Cx4, S-DD1).
//
// Each chip owns its memory window(s) and its protocol state. The Bus asks
// `handles(address)` before its own routing, so the chips decide where they
// live (fullsnes "DSP Mapping" / OBC-1 / S-RTC / CX4 sections):
//
//   S-RTC   00-3F/80-BF:2800-2801          (Dai Kaijuu Monogatari 2, ExHiROM)
//   OBC-1   00-3F/80-BF:6000-7FFF          (Metal Combat, Battle Clash)
//   DSP-1   HiROM 00-1F/80-9F:6000-7FFF    (Pilotwings)
//           LoROM 30-3F/B0-BF:8000-FFFF    (Super Mario Kart)
//   DSP-3   LoROM 20-3F/A0-BF:8000-FFFF    (SD Gundam G-NEXT)
//   DSP-4   LoROM 30-3F/B0-BF:8000-FFFF    (Top Gear 3000)
//   Cx4     00-3F/80-BF:6000-7FFF          (Mega Man X2/X3, LoROM)
//   S-DD1   00-3F/80-BF:4800-4807 + hook DMA C0-FF:0000-FFFF (Star Ocean, SFA2)
//
// Detection is header-driven via the chipset byte $FFD6 (fullsnes
// "Chipset"): high nibble 5 = S-RTC, 2 = OBC-1, 0 = DSP family. Inside the
// DSP family the sub-type is told apart by FastROM flag / game code:
// Top Gear 3000 is the only FastROM DSP cart (DSP-4); the DSP-3 games are
// SD Gundam G-NEXT ("ZX3J") and Gundam W Endless Duel ("AEDJ"); everything
// else uses the DSP-1 command set (1/1A/1B share it, only clock differs).
//
// References: snes9x dsp1/dsp3/dsp4/srtc/obc1 (command-table approach — no
// real uPD77C25 emulation), bsnes srtcemu, fullsnes.

#include "../serialize/serialize.hpp"
#include "snes/snes.hpp"

#include <memory>
#include <vector>

namespace snes {

class Coprocessor {
 public:
  virtual ~Coprocessor() = default;

  // True when `address` falls inside this chip's mapped windows.
  virtual auto handles(uint24 address) const -> bool = 0;
  virtual auto read(uint24 address) -> uint8 = 0;
  virtual auto write(uint24 address, uint8 data) -> void = 0;

  virtual auto power() -> void = 0;
  virtual auto serialize(Writer& w) const -> void = 0;
  virtual auto deserialize(Reader& r) -> void = 0;
  virtual auto setRom(const std::vector<uint8>& rom, MapMode mode) -> void {
    (void)rom;
    (void)mode;
  }
};

// Header-based detection; Chip::none for plain cartridges.
auto detectChip(const Cartridge& cartridge) -> Chip;

auto makeCoprocessor(Chip chip, MapMode mapMode) -> std::unique_ptr<Coprocessor>;

inline auto createCoprocessor(const Cartridge& cartridge) -> std::unique_ptr<Coprocessor> {
  return makeCoprocessor(detectChip(cartridge), cartridge.mapMode());
}

}  // namespace snes
