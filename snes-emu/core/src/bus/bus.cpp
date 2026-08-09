#include "snes/snes.hpp"

#include <algorithm>

namespace snes {

// Full SNES memory map (Phase 2). Routing depends on the cartridge map mode:
//
//   System banks $00-3F / $80-BF, offsets $0000-$7FFF (all modes):
//     $0000-$1FFF  8KB WRAM mirror (of $7E:0000-1FFF)
//     $2000-$20FF  WRAM mirror        (kept from phase 1; fullsnes marks
//     $2200-$3FFF  WRAM mirror         unused, common emulators mirror)
//     $2100-$21FF  B-Bus I/O ports (PPU/APU/WRAM port)
//     $4000-$43FF  CPU on-chip I/O (joypad, NMITIMEN, timers, DMA)
//     $4400-$5FFF  unused -> open bus
//     $6000-$7FFF  expansion; SRAM for HiROM (20-3F/A0-BF) and ExHiROM (80-BF)
//     $8000-$FFFF  ROM window (mode-dependent)
//
//   LoROM:  $40-7D/$C0-FF:0000-FFFF = 64KB ROM windows, except SRAM at
//           $70-7D/$F0-FF:0000-7FFF.
//   HiROM:  $40-7D:0000-FFFF = 64KB ROM windows; $C0-FF mirrors them;
//           $3E-3F/$BE-BF:0000-FFFF mirror the full 128KB WRAM.
//   ExHiROM:$40-7D:0000-FFFF = first 4MB, $C0-FF = next 4MB.
//
// PPU/APU are not implemented yet: PPU registers are write-only shadows
// ($2100-$2133), APU ports are plain R/W storage ($2140-$2143 + mirrors),
// DMA registers are R/W storage for Phase 5, and reads of write-only or
// unimplemented registers return open bus (last byte on the data bus).

auto Bus::latch(uint8 value) -> uint8 {
  lastData_ = value;
  return value;
}

auto Bus::read(uint24 address) -> uint8 {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;

  // 128KB WRAM at $7E0000-$7FFFFF.
  if (bank == 0x7E) return latch(wram_[offs]);
  if (bank == 0x7F) return latch(wram_[0x10000 + offs]);

  // HiROM only: $3E-3F/$BE-BF mirror the full 128KB WRAM.
  if (cartridge_.mapMode() == MapMode::hirom &&
      (bank == 0x3E || bank == 0x3F || bank == 0xBE || bank == 0xBF)) {
    return latch(wram_[(bank & 1) * 0x10000 + offs]);
  }

  // System banks 00-3F / 80-BF.
  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs < 0x2000) return latch(wram_[offs]);
    if (offs < 0x2100) return latch(wram_[offs & 0x1FFF]);
    if (offs < 0x2200) return mmioRead(address);  // B-Bus I/O ports
    if (offs < 0x4000) return latch(wram_[offs & 0x1FFF]);
    if (offs < 0x4400) return mmioRead(address);  // CPU on-chip I/O
    if (offs < 0x6000) return lastData_;          // unused -> open bus
    if (offs < 0x8000) {
      // Expansion / SRAM window.
      if (!sram_.empty()) {
        bool hiromBank = (bank >= 0x20 && bank <= 0x3F) || (bank >= 0xA0 && bank <= 0xBF);
        bool exhiromBank = bank >= 0x80 && bank <= 0xBF;
        if ((cartridge_.mapMode() == MapMode::hirom && hiromBank) ||
            (cartridge_.mapMode() == MapMode::exhirom && exhiromBank)) {
          return sramRead(offs, 0x6000);
        }
      }
      return lastData_;  // no SRAM -> open bus
    }
    return romRead(address);
  }

  // Cartridge windows.
  switch (cartridge_.mapMode()) {
    case MapMode::lorom:
      if ((bank >= 0x40 && bank <= 0x7D) || (bank >= 0xC0 && bank <= 0xFF)) {
        // SRAM banks 70-7D/F0-FF:0000-7FFF (when the cart has SRAM).
        if (!sram_.empty() && (bank & 0x3F) >= 0x30 && (bank & 0x3F) <= 0x3D &&
            offs < 0x8000) {
          return sramRead(offs, 0x0000);
        }
        return romRead(address);
      }
      return lastData_;  // 40-7D handled above; 7E-7F earlier; else open bus
    case MapMode::hirom:
      if (bank >= 0x40 && bank <= 0x7D) return romRead(address);
      if (bank >= 0xC0) return romRead(address);
      return lastData_;
    case MapMode::exhirom:
      if (bank >= 0x40 && bank <= 0x7D) return romRead(address);
      if (bank >= 0xC0) return romRead(address);
      return lastData_;
    default:
      return lastData_;
  }
}

void Bus::write(uint24 address, uint8 data) {
  lastData_ = data;  // every write drives the data bus (open-bus tracking)
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;

  if (bank == 0x7E) return void(wram_[offs] = data);
  if (bank == 0x7F) return void(wram_[0x10000 + offs] = data);

  if (cartridge_.mapMode() == MapMode::hirom &&
      (bank == 0x3E || bank == 0x3F || bank == 0xBE || bank == 0xBF)) {
    return void(wram_[(bank & 1) * 0x10000 + offs] = data);
  }

  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs < 0x2000) return void(wram_[offs] = data);
    if (offs < 0x2100) return void(wram_[offs & 0x1FFF] = data);
    if (offs < 0x2200) return mmioWrite(address, data);  // B-Bus I/O ports
    if (offs < 0x4000) return void(wram_[offs & 0x1FFF] = data);
    if (offs < 0x4400) return mmioWrite(address, data);  // CPU on-chip I/O
    if (offs < 0x6000) return;                           // unused -> no effect
    if (offs < 0x8000) {
      if (!sram_.empty()) {
        bool hiromBank = (bank >= 0x20 && bank <= 0x3F) || (bank >= 0xA0 && bank <= 0xBF);
        bool exhiromBank = bank >= 0x80 && bank <= 0xBF;
        if ((cartridge_.mapMode() == MapMode::hirom && hiromBank) ||
            (cartridge_.mapMode() == MapMode::exhirom && exhiromBank)) {
          return sramWrite(offs, 0x6000, data);
        }
      }
      return;  // no SRAM -> ignored
    }
    return;  // ROM writes ignored
  }

  switch (cartridge_.mapMode()) {
    case MapMode::lorom:
      if ((bank >= 0x40 && bank <= 0x7D) || (bank >= 0xC0 && bank <= 0xFF)) {
        if (!sram_.empty() && (bank & 0x3F) >= 0x30 && (bank & 0x3F) <= 0x3D &&
            offs < 0x8000) {
          return sramWrite(offs, 0x0000, data);
        }
      }
      return;  // ROM
    case MapMode::hirom:
    case MapMode::exhirom:
      return;  // ROM windows
    default:
      return;
  }
}

uint8 Bus::sramRead(uint32 offs, uint32 baseOffs) {
  return latch(sram_[(offs - baseOffs) & (sram_.size() - 1)]);
}

void Bus::sramWrite(uint32 offs, uint32 baseOffs, uint8 data) {
  sram_[(offs - baseOffs) & (sram_.size() - 1)] = data;
}

uint8 Bus::romRead(uint24 address) {
  uint32 offset = cartridge_.romOffset(address);
  if (offset == uint32(-1)) return lastData_;  // unmapped window -> open bus
  const std::vector<uint8>& rom = cartridge_.rom();
  // Mask ROMs wrap modulo their size (all SNES images are power-of-two);
  // non-power-of-two images past the end read as open bus.
  if ((rom.size() & (rom.size() - 1)) == 0) return latch(rom[offset & (rom.size() - 1)]);
  if (offset >= rom.size()) return lastData_;
  return latch(rom[offset]);
}

// ---- MMIO ----

uint8 Bus::mmioRead(uint24 address) {
  uint32 offs = address & 0xFFFF;

  // PPU write-only shadows: reads are open bus.
  if (offs <= 0x2133) return lastData_;
  // PPU1 multiply result (unimplemented: no PPU yet).
  if (offs >= 0x2134 && offs <= 0x2136) return latch(0x00);
  // SLHV latch strobe + remaining read-only PPU ports: no PPU -> 0 / open bus.
  if (offs == 0x2137) return lastData_;
  if (offs <= 0x213F) return latch(0x00);
  // APU communication ports ($2140-$2143 + mirrors $2144-$217F).
  if (offs >= 0x2140 && offs <= 0x217F) return latch(apuPort_[offs & 0x03]);
  // WRAM port data, auto-incrementing 17-bit address.
  if (offs == 0x2180) {
    uint8 v = wram_[wramAddr_ & 0x1FFFF];
    wramAddr_ = (wramAddr_ + 1) & 0x1FFFF;
    return latch(v);
  }
  // WMADDL/WMADDM/WMADDH write-only; 2184-21FF unused.
  if (offs >= 0x2181 && offs <= 0x2183) return lastData_;
  if (offs >= 0x2184 && offs <= 0x21FF) return lastData_;

  // Manual joypad ports: no controllers attached -> 0.
  if (offs == 0x4016 || offs == 0x4017) return latch(0x00);
  if (offs >= 0x4018 && offs <= 0x41FF) return lastData_;

  // CPU write-only registers $4200-$420F: reads are open bus.
  if (offs < 0x4210) return lastData_;
  // CPU read-only status $4210-$421F.
  if (offs <= 0x421F) {
    switch (offs) {
      case 0x4210: {  // RDNMI: vblank flag (stub) + 5A22 version 2
        uint8 v = (vblankToggle_ ? 0x80 : 0x00) | 0x02;
        vblankToggle_ = !vblankToggle_;
        return latch(v);
      }
      case 0x4211: {  // TIMEUP: IRQ flag stub, alternates for wait loops
        uint8 v = vblankToggle_ ? 0x80 : 0x00;
        vblankToggle_ = !vblankToggle_;
        return latch(v);
      }
      case 0x4212: return latch(0x00);  // HVBJOY: no vblank/hblank/busy
      case 0x4213: return latch(0x00);  // RDIO
      case 0x4214: return latch(divQuotient_ & 0xFF);
      case 0x4215: return latch(divQuotient_ >> 8);
      case 0x4216: return latch(mathResult_ & 0xFF);
      case 0x4217: return latch(mathResult_ >> 8);
      default: return latch(0x00);  // $4218-$421F joypads: none connected
    }
  }
  if (offs < 0x4300) return lastData_;  // $4220-$42FF unused

  // DMA channel registers $4300-$437F: R/W storage.
  if (offs < 0x4380) {
    uint32 o = offs & 0x0F;
    if (o <= 0x0B) return latch(dmaReg_[offs - 0x4300]);
    if (o == 0x0F) return latch(dmaReg_[(offs & 0xF0) + 0x0B]);  // mirror of 43xB
    return lastData_;  // 43xC-43xE unused
  }
  return lastData_;  // $4380-$43FF unused
}

void Bus::mmioWrite(uint24 address, uint8 data) {
  uint32 offs = address & 0xFFFF;

  // PPU write-only shadows $2100-$2133 (real PPU in Phase 4).
  if (offs <= 0x2133) return void(ppuReg_[offs - 0x2100] = data);
  // APU ports $2140-$2143 + mirrors $2144-$217F (real APU in Phase 6).
  if (offs >= 0x2140 && offs <= 0x217F) return void(apuPort_[offs & 0x03] = data);
  // WRAM port.
  if (offs == 0x2180) {
    wram_[wramAddr_ & 0x1FFFF] = data;
    wramAddr_ = (wramAddr_ + 1) & 0x1FFFF;
    return;
  }
  if (offs >= 0x2181 && offs <= 0x2183) {
    switch (offs) {
      case 0x2181: wramAddr_ = (wramAddr_ & 0x1FF00) | data; break;
      case 0x2182: wramAddr_ = (wramAddr_ & 0x100FF) | (uint32(data) << 8); break;
      case 0x2183: wramAddr_ = (wramAddr_ & 0xFFFF) | (uint32(data & 1) << 16); break;
    }
    return;
  }
  // Joypad output strobe (no controllers; stored for later phases).
  if (offs == 0x4016) return;
  if (offs >= 0x4018 && offs <= 0x41FF) return;  // unused

  // CPU on-chip registers.
  if (offs >= 0x4200 && offs <= 0x420D) return writeCpuRegister(offs & 0x0F, data);
  // DMA channel registers $4300-$437F.
  if (offs >= 0x4300 && offs <= 0x437F) {
    uint32 o = offs & 0x0F;
    if (o <= 0x0B) dmaReg_[offs - 0x4300] = data;
    else if (o == 0x0F) dmaReg_[(offs & 0xF0) + 0x0B] = data;  // mirror of 43xB
  }
}

void Bus::writeCpuRegister(uint8 offset, uint8 data) {
  cpuReg_[offset] = data;
  switch (offset) {
    case 0x02: mpyA_ = data; break;  // WRMPYA
    case 0x03: {                     // WRMPYB: start 8x8 unsigned multiply
      mathResult_ = uint16(mpyA_) * data;
      // Hardware quirk (fullsnes): multiply also sets RDDIVL=WRMPYB, RDDIVH=0.
      divQuotient_ = data;
      break;
    }
    case 0x04: divDividend_ = (divDividend_ & 0xFF00) | data; break;  // WRDIVL
    case 0x05: divDividend_ = (divDividend_ & 0x00FF) | (uint16(data) << 8); break;
    case 0x06: {  // WRDIVB: start 16-bit / 8-bit unsigned division
      if (data == 0) {
        divQuotient_ = 0xFFFF;              // division by zero (fullsnes)
        mathResult_ = divDividend_;         // remainder = dividend
      } else {
        divQuotient_ = divDividend_ / data;
        mathResult_ = divDividend_ % data;
      }
      break;
    }
    default: break;  // 4200/4201/4207-420D: storage only (Phase 3/5)
  }
}

// ---- power / reset ----

void Bus::power() {
  // Size the SRAM buffer from the cartridge header and clear it.
  sram_.assign(cartridge_.sramSize(), 0);

  lastData_ = 0;
  vblankToggle_ = false;
  wramAddr_ = 0;
  mpyA_ = 0xFF;
  divDividend_ = 0;
  divQuotient_ = 0;
  mathResult_ = 0;
  std::fill(std::begin(ppuReg_), std::end(ppuReg_), 0);
  std::fill(std::begin(apuPort_), std::end(apuPort_), 0);
  std::fill(std::begin(cpuReg_), std::end(cpuReg_), 0);
  std::fill(std::begin(dmaReg_), std::end(dmaReg_), 0xFF);

  // Power-on values (fullsnes I/O map right column).
  cpuReg_[0x01] = 0xFF;  // 4201 WRIO
  cpuReg_[0x07] = 0xFF;  // 4207 HTIMEL
  cpuReg_[0x08] = 0x01;  // 4208 HTIMEH
  cpuReg_[0x09] = 0xFF;  // 4209 VTIMEL
  cpuReg_[0x0A] = 0x01;  // 420A VTIMEH
}

void Bus::reset() {
  // Soft reset only touches un-bracketed values (fullsnes): bracketed
  // registers keep whatever the game left in them.
  vblankToggle_ = false;
  cpuReg_[0x00] = 0x00;  // 4200 NMITIMEN (disables NMI/IRQ/joypad)
  cpuReg_[0x01] = 0xFF;  // 4201 WRIO
  cpuReg_[0x07] = 0xFF;  // 4207 HTIMEL
  cpuReg_[0x08] = 0x01;  // 4208 HTIMEH
  cpuReg_[0x09] = 0xFF;  // 4209 VTIMEL
  cpuReg_[0x0A] = 0x01;  // 420A VTIMEH
  cpuReg_[0x0B] = 0x00;  // 420B MDMAEN
  cpuReg_[0x0C] = 0x00;  // 420C HDMAEN
  cpuReg_[0x0D] = 0x00;  // 420D MEMSEL
  for (int c = 0; c < 8; c++)
    for (int o = 0; o < 16; o++)
      if (o != 0x04) dmaReg_[c * 16 + o] = 0xFF;  // A1Bx unchanged on reset
}

// ---- register readback (tests / later phases) ----

uint8 Bus::ppuRegister(uint8 offset) const { return ppuReg_[offset & 0x33]; }
uint8 Bus::apuPort(uint8 index) const { return apuPort_[index & 0x03]; }
uint8 Bus::cpuRegister(uint8 offset) const { return cpuReg_[offset & 0x0F]; }
uint8 Bus::dmaRegister(uint8 offset) const { return dmaReg_[offset & 0x7F]; }
uint32 Bus::wramAddress() const { return wramAddr_; }

}  // namespace snes
