#include "snes/snes.hpp"

namespace snes {

// LoROM bus. Writes to MMIO registers have no side effects yet (no PPU/APU/DMA),
// so they are discarded. ROM reads go through the cartridge mapping.

auto Bus::read(uint24 address) -> uint8 {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;

  // 128KB WRAM at $7E0000-$7FFFFF.
  if (bank == 0x7E) return wram_[offs];
  if (bank == 0x7F) return wram_[0x10000 + offs];

  // Banks 00-3F / 80-BF: RAM, MMIO, SRAM slot, ROM.
  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs < 0x2000) return wram_[offs];           // $0000-$1FFF
    if (offs < 0x4000) return wram_[offs & 0x1FFF];  // $2000-$3FFF mirror
    if (offs < 0x6000) return mmioRead(address);     // $4000-$5FFF
    if (offs < 0x8000) return 0x00;                  // $6000-$7FFF: no SRAM
    return romRead(address);                         // $8000-$FFFF
  }

  // Banks 40-7D: WRAM mirrors low, ROM high.
  if (bank >= 0x40 && bank <= 0x7D) {
    if (offs < 0x8000) {
      if (bank <= 0x5F) return wram_[0x10000 + (bank - 0x40) * 0x8000 + offs];
      return 0x00;
    }
    return romRead(address);
  }

  return 0x00;  // unmapped -> open bus
}

void Bus::write(uint24 address, uint8 data) {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;

  if (bank == 0x7E) return void(wram_[offs] = data);
  if (bank == 0x7F) return void(wram_[0x10000 + offs] = data);

  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs < 0x2000) return void(wram_[offs] = data);
    if (offs < 0x4000) return void(wram_[offs & 0x1FFF] = data);
    return;  // $4000-$FFFF: MMIO/ROM writes are no-ops at this phase
  }
}

// $4210/$4211 NMI flag: alternates per read so wait_for_vblank's poll loop
// (first read 0, then wait for 1) terminates. $4212 reads 0 so joypad
// auto-read busy loops exit; $4218-$421F read 0 (no gamepads).
uint8 Bus::mmioRead(uint24 address) {
  uint32 offs = address & 0xFFFF;
  if (offs == 0x4210 || offs == 0x4211) {
    uint8 flag = vblankToggle_ ? 0x80 : 0x00;
    vblankToggle_ = !vblankToggle_;
    return flag;
  }
  return 0x00;
}

uint8 Bus::romRead(uint24 address) const {
  uint32 offset = cartridge_.romOffset(address);
  if (offset == uint32(-1)) return 0x00;
  return cartridge_.rom()[offset];
}

}  // namespace snes