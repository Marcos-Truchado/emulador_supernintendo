#include "coprocessor/coprocessor.hpp"
#include "coprocessor/srtc.hpp"
#include "coprocessor/obc1.hpp"
#include "coprocessor/dsp1.hpp"
#include "coprocessor/dsp3.hpp"
#include "coprocessor/dsp4.hpp"

#include <cstring>

namespace snes {

namespace {

auto headerOffset(MapMode mode) -> uint32 {
  switch (mode) {
    case MapMode::lorom: return 0x7FC0;
    case MapMode::hirom: return 0xFFC0;
    case MapMode::exhirom: return 0x40FFC0;
    default: return uint32(-1);
  }
}

bool gameCodePrefix(const Cartridge& cart, uint32 base, const char* pre) {
  // Game code at $FFB2 = header base - 0x0E (14 bytes before $FFC0).
  if (base < 0x0E || base - 0x0E + 4 > cart.romSize()) return false;
  const uint8* g = cart.rom().data() + base - 0x0E;
  for (int i = 0; pre[i]; i++) {
    if ((char)g[i] != pre[i]) return false;
  }
  return true;
}

}  // namespace

auto detectChip(const Cartridge& cartridge) -> Chip {
  const uint32 base = headerOffset(cartridge.mapMode());
  if (base == uint32(-1) || base + 0x30 > cartridge.romSize()) return Chip::none;
  const uint8* h = cartridge.rom().data() + base;
  const uint8 chipset = h[0x16];  // $FFD6

  switch (chipset >> 4) {
    case 0x5: return Chip::srtc;  // $55 = S-RTC
    case 0x2: return Chip::obc1;  // $25 = OBC-1
    case 0x0:
      if (chipset == 0x00) return Chip::none;
      // DSP family inside $0x (03/05 used for DSP carts).
      // Top Gear 3000 is the only FastROM DSP cart (DSP-4).
      if (h[0x15] & 0x10) return Chip::dsp4;  // FFD5 bit4 = FastROM
      // DSP-3 games: SD Gundam G-NEXT (ZX3J) and Gundam W Endless Duel (AEDJ).
      if (gameCodePrefix(cartridge, base, "ZX3") || gameCodePrefix(cartridge, base, "AED")) return Chip::dsp3;
      return Chip::dsp1;
    default:
      return Chip::none;
  }
}

auto makeCoprocessor(Chip chip, MapMode mapMode) -> std::unique_ptr<Coprocessor> {
  switch (chip) {
    case Chip::none: return nullptr;
    case Chip::srtc: return std::make_unique<Srtc>();
    case Chip::obc1: return std::make_unique<Obc1>();
    case Chip::dsp1: return std::make_unique<Dsp1>(mapMode == MapMode::hirom);
    case Chip::dsp3: return std::make_unique<Dsp3>();
    case Chip::dsp4: return std::make_unique<Dsp4>();
  }
  return nullptr;
}

}  // namespace snes
