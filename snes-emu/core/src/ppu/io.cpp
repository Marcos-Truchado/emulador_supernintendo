#include "ppu/ppu.hpp"

namespace snes {

// The render engines hold references back to the PPU (and a const layer id),
// so they must be wired up in the constructor's member-initializer list.
Ppu::Ppu()
    : layers_{Layer(*this, 0), Layer(*this, 1), Layer(*this, 2), Layer(*this, 3)},
      sprites_(*this),
      window_(*this),
      composer_(*this),
      mosaic_(*this) {}

// Phase 4 B-Bus register file. All behaviors follow fullsnes (noSns v1.6)
// with the ares PPU as the reference for ordering and edge cases; naming is
// our own. The CPU/PPU are dot-synchronous in this design, so there is no
// synchronization step here (ares uses cpu.synchronize(ppu)).

// B-Bus register file reset, used by both power() and reset(). Values follow
// the fullsnes I/O map reset column: INIDISP = 8xh (forced blank), VMAIN =
// 0Fh (step 128, translation 3), M7A/M7B = FFh; everything else 0.
auto Ppu::resetRegisters() -> void {
  io_ = IoRegs{};
  io_.displayBrightness = 0;
  io_.displayDisable = 1;
  io_.vramIncrementSize = 128;
  io_.vramMapping = 3;
  io_.m7a = 0x00FF;
  io_.m7b = 0x00FF;
  latch_ = Latch{};
  state_ = State{};
  ppu1_.mdr = 0;
  ppu2_.mdr = 0;
  recomputeLayers();
}

// ---- VRAM / OAM / CGRAM access ----

// $2115 bits 2-3: address translation, a left-rotate of the lower 8, 9 or
// 10 bits of the word address by 3 (fullsnes "Address Translation"). The
// translation is temporary: the address register itself never changes.
auto Ppu::mapVramAddress() const -> uint16 {
  uint16 address = io_.vramAddr;
  switch (io_.vramMapping) {
    case 0: return address;
    case 1: return uint16((address & 0xFF00) | ((address & 0x001F) << 3) | ((address >> 5) & 7));
    case 2: return uint16((address & 0xFE00) | ((address & 0x003F) << 3) | ((address >> 6) & 7));
    default: return uint16((address & 0xFC00) | ((address & 0x007F) << 3) | ((address >> 7) & 7));
  }
}

// The CPU cannot touch VRAM while the picture is being generated: reads
// return 0 and writes are dropped during the active display unless forced
// blank (INIDISP bit7) is on (fullsnes "Forced Blank").
auto Ppu::readVramWord() -> uint16 {
  if (!io_.displayDisable && scanline() < vdisp()) return 0x0000;
  return vram_[mapVramAddress() & kVramMask];
}

auto Ppu::writeVramByte(bool high, uint8 data) -> void {
  if (!io_.displayDisable && scanline() < vdisp()) return;
  uint16 address = mapVramAddress() & kVramMask;
  if (high) vram_[address] = uint16((vram_[address] & 0x00FF) | (uint16(data) << 8));
  else vram_[address] = uint16((vram_[address] & 0xFF00) | data);
}

// Raw OAM byte readback (tests, debugging): no rendering gates. The low
// table ($000-$1FF) packs one object per 4 bytes; the high table ($200-$21F)
// packs X bit8 + size for four objects per byte.
auto Ppu::oamReadRaw(uint16 address) const -> uint8 {
  if ((address & 0x200) == 0) {
    const SpriteObj& obj = oamObj_[(address >> 2) & 0x7F];
    switch (address & 3) {
      case 0: return obj.x & 0xFF;
      case 1: return obj.y;
      case 2: return obj.character;
      default:
        return uint8(uint8(obj.nameselect) | (obj.palette << 1) | (obj.priority << 4) |
                     (uint8(obj.hflip) << 6) | (uint8(obj.vflip) << 7));
    }
  }
  uint16 n = (address & 0x1F) << 2;
  uint8 data = 0;
  for (uint8 i = 0; i < 4; i++) {
    const SpriteObj& obj = oamObj_[n + i];
    data |= uint8((obj.x >> 8) & 1) << (i * 2);
    data |= uint8(obj.size) << (i * 2 + 1);
  }
  return data;
}

// During the active display the OAM address is taken from the render-time
// latch: the low table reads the object the PPU is evaluating, the high
// table reads its X bit8/size group (fullsnes "OAM Address Remapping").
auto Ppu::readOamByte(uint16 address) -> uint8 {
  if (!io_.displayDisable && scanline() < vdisp()) {
    address = (address & 0x200) ? uint16(0x200 | (latch_.oamAddr >> 2))
                                : uint16(0x000 | (latch_.oamAddr << 2) | (address & 1));
  }
  return oamReadRaw(address);
}

auto Ppu::writeOamByte(uint16 address, uint8 data) -> void {
  if (!io_.displayDisable && scanline() < vdisp()) {
    address = (address & 0x200) ? uint16(0x200 | (latch_.oamAddr >> 2))
                                : uint16(0x000 | (latch_.oamAddr << 2) | (address & 1));
  }
  if ((address & 0x200) == 0) {
    SpriteObj& obj = oamObj_[(address >> 2) & 0x7F];
    switch (address & 3) {
      case 0: obj.x = uint16((obj.x & 0x100) | data); break;
      case 1: obj.y = data; break;
      case 2: obj.character = data; break;
      default:
        obj.nameselect = data & 1;
        obj.palette = (data >> 1) & 7;
        obj.priority = (data >> 4) & 3;
        obj.hflip = (data >> 6) & 1;
        obj.vflip = (data >> 7) & 1;
        break;
    }
    return;
  }
  uint16 n = (address & 0x1F) << 2;
  for (uint8 i = 0; i < 4; i++) {
    SpriteObj& obj = oamObj_[n + i];
    if (data & (1 << (i * 2))) obj.x = uint16(obj.x | 0x100);
    else obj.x = uint16(obj.x & 0x0FF);
    obj.size = (data >> (i * 2 + 1)) & 1;
  }
}

// CGRAM addressing is remapped to the render-time latch during the active
// display window (dots 22..273, fullsnes "CGRAM address remapping").
auto Ppu::readCgramByte(bool high, uint8 address) -> uint8 {
  if (!io_.displayDisable && scanline() > 0 && scanline() < vdisp() &&
      dot_ >= 22 && dot_ < 274) {
    address = latch_.cgramAddr;
  }
  uint16 color = cgram_[address];
  return high ? uint8((color >> 8) & 0xFF) : uint8(color & 0xFF);
}

auto Ppu::writeCgramWord(uint8 address, uint16 data) -> void {
  if (!io_.displayDisable && scanline() > 0 && scanline() < vdisp() &&
      dot_ >= 22 && dot_ < 274) {
    address = latch_.cgramAddr;
  }
  cgram_[address] = data & 0x7FFF;
}

// ---- sprite engine helpers ($2102/$2103/$2104/$2138) ----

auto Ppu::SpriteEngine::reloadAddress() -> void {
  ppu.io_.oamAddr = ppu.io_.oamBaseAddr;
  refreshFirst();
}

auto Ppu::SpriteEngine::refreshFirst() -> void {
  firstSprite = 0;
  if (ppu.io_.oamPriority) firstSprite = (ppu.io_.oamAddr >> 2) & 0x7F;
}

// ---- BG mode / priority tables (per BGMODE/SETINI) ----

auto Ppu::Mosaic::active() const -> bool {
  return ppu.layers_[0].mosaic.enable || ppu.layers_[1].mosaic.enable ||
         ppu.layers_[2].mosaic.enable || ppu.layers_[3].mosaic.enable;
}

auto Ppu::objectWidth(const SpriteObj& obj) const -> uint32 {
  static constexpr uint32 small[8] = {8, 8, 8, 16, 16, 32, 16, 16};
  static constexpr uint32 large[8] = {16, 32, 64, 32, 64, 64, 32, 32};
  return obj.size ? large[sprites_.baseSize] : small[sprites_.baseSize];
}

auto Ppu::objectHeight(const SpriteObj& obj) const -> uint32 {
  static constexpr uint32 small[8] = {8, 8, 8, 16, 16, 32, 32, 32};
  static constexpr uint32 large[8] = {16, 32, 64, 32, 64, 64, 64, 32};
  if (!obj.size && sprites_.interlace && sprites_.baseSize >= 6) return 16;
  return obj.size ? large[sprites_.baseSize] : small[sprites_.baseSize];
}

// BGMODE/SETINI effect on layer modes and the per-tile display priorities
// (fullsnes "BG Mode 0-7 tables").
auto Ppu::recomputeLayers() -> void {
  state_.vdisp = io_.overscan ? 240 : 225;

  switch (io_.mode) {
    case 0:
      layers_[0].mode = kBpp2; layers_[1].mode = kBpp2;
      layers_[2].mode = kBpp2; layers_[3].mode = kBpp2;
      layers_[0].priority[0] = 8; layers_[0].priority[1] = 11;
      layers_[1].priority[0] = 7; layers_[1].priority[1] = 10;
      layers_[2].priority[0] = 2; layers_[2].priority[1] = 5;
      layers_[3].priority[0] = 1; layers_[3].priority[1] = 4;
      sprites_.priority[0] = 3; sprites_.priority[1] = 6;
      sprites_.priority[2] = 9; sprites_.priority[3] = 12;
      break;
    case 1:
      layers_[0].mode = kBpp4; layers_[1].mode = kBpp4;
      layers_[2].mode = kBpp2; layers_[3].mode = kInactive;
      if (io_.bgPriority) {
        layers_[0].priority[0] = 5; layers_[0].priority[1] = 8;
        layers_[1].priority[0] = 4; layers_[1].priority[1] = 7;
        layers_[2].priority[0] = 1; layers_[2].priority[1] = 10;
        sprites_.priority[0] = 2; sprites_.priority[1] = 3;
        sprites_.priority[2] = 6; sprites_.priority[3] = 9;
      } else {
        layers_[0].priority[0] = 6; layers_[0].priority[1] = 9;
        layers_[1].priority[0] = 5; layers_[1].priority[1] = 8;
        layers_[2].priority[0] = 1; layers_[2].priority[1] = 3;
        sprites_.priority[0] = 2; sprites_.priority[1] = 4;
        sprites_.priority[2] = 7; sprites_.priority[3] = 10;
      }
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      layers_[0].mode = io_.mode == 2 || io_.mode == 5 ? kBpp4
                       : io_.mode == 3             ? kBpp8
                                                   : kBpp8;
      layers_[1].mode = io_.mode == 2 ? kBpp4 : io_.mode == 3 ? kBpp4 : kBpp2;
      layers_[2].mode = kInactive;
      layers_[3].mode = kInactive;
      layers_[0].priority[0] = 3; layers_[0].priority[1] = 7;
      layers_[1].priority[0] = 1; layers_[1].priority[1] = 5;
      sprites_.priority[0] = 2; sprites_.priority[1] = 4;
      sprites_.priority[2] = 6; sprites_.priority[3] = 8;
      break;
    case 6:
      layers_[0].mode = kBpp4;
      layers_[1].mode = kInactive;
      layers_[2].mode = kInactive;
      layers_[3].mode = kInactive;
      layers_[0].priority[0] = 2; layers_[0].priority[1] = 5;
      sprites_.priority[0] = 1; sprites_.priority[1] = 3;
      sprites_.priority[2] = 4; sprites_.priority[3] = 6;
      break;
    default:  // mode 7
      if (!io_.extbg) {
        layers_[0].mode = kMode7;
        layers_[1].mode = kInactive;
        layers_[2].mode = kInactive;
        layers_[3].mode = kInactive;
        layers_[0].priority[0] = 2;
        sprites_.priority[0] = 1; sprites_.priority[1] = 3;
        sprites_.priority[2] = 4; sprites_.priority[3] = 5;
      } else {
        layers_[0].mode = kMode7; layers_[1].mode = kMode7;
        layers_[2].mode = kInactive;
        layers_[3].mode = kInactive;
        layers_[0].priority[0] = 3;
        layers_[1].priority[0] = 1; layers_[1].priority[1] = 5;
        sprites_.priority[0] = 2; sprites_.priority[1] = 4;
        sprites_.priority[2] = 6; sprites_.priority[3] = 7;
      }
      break;
  }
}

// ---- B-Bus writes $2100-$2133 ----

auto Ppu::writeRegister(uint8 offset, uint8 data) -> void {
  switch (offset) {
    case 0x00: {  // INIDISP
      // Leaving forced blank exactly at the last visible line resets the
      // OAM address (ares quirk, mirrors the VBlank-start reset).
      if (io_.displayDisable && scanline() == vdisp()) sprites_.reloadAddress();
      io_.displayBrightness = data & 0x0F;
      io_.displayDisable = data >> 7 & 1;
      break;
    }
    case 0x01:  // OBSEL
      sprites_.tileBase = (data & 0x07) << 13;
      sprites_.nameselect = (data >> 3) & 3;
      sprites_.baseSize = data >> 5;
      break;
    case 0x02:  // OAMADDL
      io_.oamBaseAddr = uint16((io_.oamBaseAddr & 0x200) | (uint16(data) << 1));
      sprites_.reloadAddress();
      break;
    case 0x03: {  // OAMADDH
      io_.oamBaseAddr = uint16((io_.oamBaseAddr & 0x1FE) | (uint16(data & 1) << 9));
      io_.oamPriority = data >> 7 & 1;
      sprites_.reloadAddress();
      break;
    }
    case 0x04: {  // OAMDATA (write-twice into the low table, single bytes
                  // in the high table $200-$21F)
      bool latchBit = (io_.oamAddr & 1) != 0;
      uint16 address = io_.oamAddr;
      io_.oamAddr = (io_.oamAddr + 1) & 0x3FF;
      if (!latchBit) latch_.oam = data;
      if (address & 0x200) {
        writeOamByte(address, data);
      } else if (latchBit) {
        writeOamByte(uint16(address & ~1), latch_.oam);
        writeOamByte(uint16((address & ~1) + 1), data);
      }
      sprites_.refreshFirst();
      break;
    }
    case 0x05:  // BGMODE
      io_.mode = data & 0x07;
      io_.bgPriority = data >> 3 & 1;
      layers_[0].tileSize = data >> 4 & 1;
      layers_[1].tileSize = data >> 5 & 1;
      layers_[2].tileSize = data >> 6 & 1;
      layers_[3].tileSize = data >> 7 & 1;
      recomputeLayers();
      break;
    case 0x06: {  // MOSAIC
      bool wasActive = mosaic_.active();
      layers_[0].mosaic.enable = data & 1;
      layers_[1].mosaic.enable = data >> 1 & 1;
      layers_[2].mosaic.enable = data >> 2 & 1;
      layers_[3].mosaic.enable = data >> 3 & 1;
      mosaic_.size = (data >> 4) + 1;
      // Enabling mosaic mid-frame restarts the vertical block counter.
      if (!wasActive && mosaic_.active()) mosaic_.vcounter = mosaic_.size + 1;
      break;
    }
    case 0x07: case 0x08: case 0x09: case 0x0A: {  // BGnSC
      Layer& layer = layers_[offset - 0x07];
      layer.screenSize = data & 0x03;
      layer.screenAddress = uint16((data >> 2) << 10);
      break;
    }
    case 0x0B:  // BG12NBA
      layers_[0].tileBase = uint16((data & 0x0F) << 12);
      if (getenv("PPU_DEBUG")) printf("w210B data=%02X tb=%X\n", data, layers_[0].tileBase);
      layers_[1].tileBase = uint16((data >> 4) << 12);
      break;
    case 0x0C:  // BG34NBA
      layers_[2].tileBase = uint16((data & 0x0F) << 12);
      layers_[3].tileBase = uint16((data >> 4) << 12);
      break;
    case 0x0D:  // BG1HOFS (and M7HOFS)
      io_.hoffsetMode7 = uint16((data << 8) | latch_.mode7);
      latch_.mode7 = data;
      layers_[0].hscroll = uint16((data << 8) | (latch_.bgofsPPU1 & ~7) | (latch_.bgofsPPU2 & 7));
      latch_.bgofsPPU1 = data;
      latch_.bgofsPPU2 = data;
      break;
    case 0x0E:  // BG1VOFS (and M7VOFS)
      io_.voffsetMode7 = uint16((data << 8) | latch_.mode7);
      latch_.mode7 = data;
      layers_[0].vscroll = uint16((data << 8) | latch_.bgofsPPU1);
      latch_.bgofsPPU1 = data;
      break;
    case 0x0F:  // BG2HOFS
    case 0x11:  // BG3HOFS
    case 0x13:  // BG4HOFS
      layers_[uint8((offset - 0x0F) / 2 + 1)].hscroll =
          uint16((data << 8) | (latch_.bgofsPPU1 & ~7) | (latch_.bgofsPPU2 & 7));
      latch_.bgofsPPU1 = data;
      latch_.bgofsPPU2 = data;
      break;
    case 0x10:  // BG2VOFS
    case 0x12:  // BG3VOFS
    case 0x14:  // BG4VOFS
      layers_[uint8((offset - 0x10) / 2 + 1)].vscroll = uint16((data << 8) | latch_.bgofsPPU1);
      latch_.bgofsPPU1 = data;
      break;
    case 0x15: {  // VMAIN
      static constexpr uint16 size[4] = {1, 32, 128, 128};
      io_.vramIncrementSize = size[data & 3];
      io_.vramMapping = (data >> 2) & 3;
      io_.vramIncrementMode = data >> 7 & 1;
      break;
    }
    case 0x16:  // VMADDL
      io_.vramAddr = uint16((io_.vramAddr & 0xFF00) | data);
      latch_.vram = readVramWord();
      if (getenv("PPU_DEBUG")) printf("vmaddl data=%02X -> addr=%04X\n", data, io_.vramAddr);
      break;
    case 0x17:  // VMADDH
      io_.vramAddr = uint16((io_.vramAddr & 0x00FF) | (uint16(data) << 8));
      latch_.vram = readVramWord();
      if (getenv("PPU_DEBUG")) printf("vmaddh data=%02X -> addr=%04X\n", data, io_.vramAddr);
      break;
    case 0x18:  // VMDATAL
      writeVramByte(false, data);
      if (!io_.vramIncrementMode) io_.vramAddr += io_.vramIncrementSize;
      if (getenv("PPU_DEBUG")) printf("vmdata addr=%04X map=%04X data=%02X %02X\n", io_.vramAddr & 0xFFFF, mapVramAddress(), data, 0);
      break;
    case 0x19:  // VMDATAH
      writeVramByte(true, data);
      if (io_.vramIncrementMode) io_.vramAddr += io_.vramIncrementSize;
      if (getenv("PPU_DEBUG")) printf("vmdata addr=%04X map=%04X data=%02X %02X\n", io_.vramAddr & 0xFFFF, mapVramAddress(), 0, data);
      break;
    case 0x1A:  // M7SEL
      io_.hflipMode7 = data & 1;
      io_.vflipMode7 = data >> 1 & 1;
      io_.repeatMode7 = data >> 6;
      break;
    case 0x1B: case 0x1C: case 0x1D: case 0x1E:  // M7A..M7D
    case 0x1F: case 0x20: {  // M7X, M7Y
      uint16* reg = offset == 0x1B ? &io_.m7a : offset == 0x1C ? &io_.m7b
                    : offset == 0x1D ? &io_.m7c : offset == 0x1E ? &io_.m7d
                    : offset == 0x1F ? &io_.m7x : &io_.m7y;
      *reg = uint16((data << 8) | latch_.mode7);
      latch_.mode7 = data;
      break;
    }
    case 0x21:  // CGADD
      io_.cgramAddr = data;
      io_.cgramAddrLatch = false;
      break;
    case 0x22:  // CGDATA (write-twice: even write latches, odd commits)
      if (io_.cgramAddrLatch) {
        writeCgramWord(io_.cgramAddr++, uint16((data & 0x7F) << 8) | latch_.cgram);
      } else {
        latch_.cgram = data;
      }
      io_.cgramAddrLatch = !io_.cgramAddrLatch;
      break;
    case 0x23:  // W12SEL
      window_.bg1.oneInvert = data & 1;
      window_.bg1.oneEnable = data >> 1 & 1;
      window_.bg1.twoInvert = data >> 2 & 1;
      window_.bg1.twoEnable = data >> 3 & 1;
      window_.bg2.oneInvert = data >> 4 & 1;
      window_.bg2.oneEnable = data >> 5 & 1;
      window_.bg2.twoInvert = data >> 6 & 1;
      window_.bg2.twoEnable = data >> 7 & 1;
      break;
    case 0x24:  // W34SEL
      window_.bg3.oneInvert = data & 1;
      window_.bg3.oneEnable = data >> 1 & 1;
      window_.bg3.twoInvert = data >> 2 & 1;
      window_.bg3.twoEnable = data >> 3 & 1;
      window_.bg4.oneInvert = data >> 4 & 1;
      window_.bg4.oneEnable = data >> 5 & 1;
      window_.bg4.twoInvert = data >> 6 & 1;
      window_.bg4.twoEnable = data >> 7 & 1;
      break;
    case 0x25:  // WOBJSEL
      window_.obj.oneInvert = data & 1;
      window_.obj.oneEnable = data >> 1 & 1;
      window_.obj.twoInvert = data >> 2 & 1;
      window_.obj.twoEnable = data >> 3 & 1;
      window_.col.oneInvert = data >> 4 & 1;
      window_.col.oneEnable = data >> 5 & 1;
      window_.col.twoInvert = data >> 6 & 1;
      window_.col.twoEnable = data >> 7 & 1;
      break;
    case 0x26: window_.oneLeft = data; break;
    case 0x27: window_.oneRight = data; break;
    case 0x28: window_.twoLeft = data; break;
    case 0x29: window_.twoRight = data; break;
    case 0x2A:  // WBGLOG
      window_.bg1.mask = data & 3;
      window_.bg2.mask = data >> 2 & 3;
      window_.bg3.mask = data >> 4 & 3;
      window_.bg4.mask = data >> 6 & 3;
      break;
    case 0x2B:  // WOBJLOG
      window_.obj.mask = data & 3;
      window_.col.mask = data >> 2 & 3;
      break;
    case 0x2C:  // TM
      layers_[0].aboveEnable = data & 1;
      layers_[1].aboveEnable = data >> 1 & 1;
      layers_[2].aboveEnable = data >> 2 & 1;
      layers_[3].aboveEnable = data >> 3 & 1;
      sprites_.aboveEnable = data >> 4 & 1;
      if (getenv("PPU_DEBUG")) printf("w212C data=%02X\n", data);
      break;
    case 0x2D:  // TS
      layers_[0].belowEnable = data & 1;
      layers_[1].belowEnable = data >> 1 & 1;
      layers_[2].belowEnable = data >> 2 & 1;
      layers_[3].belowEnable = data >> 3 & 1;
      sprites_.belowEnable = data >> 4 & 1;
      break;
    case 0x2E:  // TMW
      window_.bg1.aboveEnable = data & 1;
      window_.bg2.aboveEnable = data >> 1 & 1;
      window_.bg3.aboveEnable = data >> 2 & 1;
      window_.bg4.aboveEnable = data >> 3 & 1;
      window_.obj.aboveEnable = data >> 4 & 1;
      break;
    case 0x2F:  // TSW
      window_.bg1.belowEnable = data & 1;
      window_.bg2.belowEnable = data >> 1 & 1;
      window_.bg3.belowEnable = data >> 2 & 1;
      window_.bg4.belowEnable = data >> 3 & 1;
      window_.obj.belowEnable = data >> 4 & 1;
      break;
    case 0x30:  // CGWSEL
      composer_.directColor = data & 1;
      composer_.blendMode = data >> 1 & 1;
      window_.col.belowMask = data >> 4 & 3;
      window_.col.aboveMask = data >> 6 & 3;
      break;
    case 0x31:  // CGADDSUB
      composer_.bg1ColorEnable = data & 1;
      composer_.bg2ColorEnable = data >> 1 & 1;
      composer_.bg3ColorEnable = data >> 2 & 1;
      composer_.bg4ColorEnable = data >> 3 & 1;
      composer_.objColorEnable = data >> 4 & 1;
      composer_.backColorEnable = data >> 5 & 1;
      composer_.colorHalve = data >> 6 & 1;
      composer_.colorMode = data >> 7 & 1;
      break;
    case 0x32:  // COLDATA
      if (data & 0x20) composer_.colorRed = data & 0x1F;
      if (data & 0x40) composer_.colorGreen = data & 0x1F;
      if (data & 0x80) composer_.colorBlue = data & 0x1F;
      break;
    case 0x33:  // SETINI
      io_.interlace = data & 1;
      sprites_.interlace = data >> 1 & 1;
      io_.overscan = data >> 2 & 1;
      io_.pseudoHires = data >> 3 & 1;
      io_.extbg = data >> 6 & 1;
      recomputeLayers();
      break;
    default:
      break;
  }
}

// ---- B-Bus reads $2134-$213F ----

auto Ppu::readRegister(uint8 offset) -> uint8 {
  switch (offset) {
    case 0x34: case 0x35: case 0x36: {  // MPYL/MPYM/MPYH
      const int result = int16(io_.m7a) * int8(io_.m7b >> 8);
      const int shift = (offset - 0x34) * 8;
      return ppu1_.mdr = uint8((result >> shift) & 0xFF);
    }
    case 0x38:  // OAMDATAREAD
      ppu1_.mdr = readOamByte(io_.oamAddr);
      io_.oamAddr = (io_.oamAddr + 1) & 0x3FF;
      sprites_.refreshFirst();
      return ppu1_.mdr;
    case 0x39:  // VMDATALREAD
      ppu1_.mdr = latch_.vram & 0xFF;
      if (!io_.vramIncrementMode) {
        latch_.vram = readVramWord();
        io_.vramAddr += io_.vramIncrementSize;
      }
      return ppu1_.mdr;
    case 0x3A:  // VMDATAHREAD
      ppu1_.mdr = uint8((latch_.vram >> 8) & 0xFF);
      if (io_.vramIncrementMode) {
        latch_.vram = readVramWord();
        io_.vramAddr += io_.vramIncrementSize;
      }
      return ppu1_.mdr;
    case 0x3B:  // CGDATAREAD (1st read: low byte; 2nd: high, then step)
      if (io_.cgramAddrLatch) {
        ppu2_.mdr = uint8((ppu2_.mdr & 0x80) | readCgramByte(true, io_.cgramAddr++));
      } else {
        ppu2_.mdr = readCgramByte(false, io_.cgramAddr);
      }
      io_.cgramAddrLatch = !io_.cgramAddrLatch;
      return ppu2_.mdr;
    case 0x3C:  // OPHCT
      if (!latch_.hcounter) {
        ppu2_.mdr = io_.hcounter & 0xFF;
        latch_.hcounter = true;
      } else {
        ppu2_.mdr = uint8((ppu2_.mdr & 0xFE) | ((io_.hcounter >> 8) & 1));
      }
      return ppu2_.mdr;
    case 0x3D:  // OPVCT
      if (!latch_.vcounter) {
        ppu2_.mdr = io_.vcounter & 0xFF;
        latch_.vcounter = true;
      } else {
        ppu2_.mdr = uint8((ppu2_.mdr & 0xFE) | ((io_.vcounter >> 8) & 1));
      }
      return ppu2_.mdr;
    case 0x3E:  // STAT77 (bit4 = PPU1 open bus)
      ppu1_.mdr = uint8((ppu1_.mdr & 0x10) | (uint8(sprites_.timeOver) << 7) |
                        (uint8(sprites_.rangeOver) << 6) | 0x01);
      return ppu1_.mdr;
    case 0x3F: {  // STAT78: resets the OPHCT/OPVCT flipflops; bit6 latch
                  // flag reads as set while WRIO bit7 is clear (hardware
                  // quirk), else shows and clears latch.counters.
      latch_.hcounter = false;
      latch_.vcounter = false;
      ppu2_.mdr = uint8((ppu2_.mdr & 0x20) | (fieldBit() ? 0x80 : 0x00) | 0x01);
      if (!wrioBit7_) {
        ppu2_.mdr |= 0x40;
      } else {
        if (latch_.counters) ppu2_.mdr |= 0x40;
        latch_.counters = false;
      }
      return ppu2_.mdr;
    }
    default:
      return 0x00;
  }
}

auto Ppu::captureCounters() -> void {
  // fullsnes: latching (software strobe or WRIO 1->0 edge) only works while
  // WRIO bit7 is (or was) set.
  if (!wrioBit7_) return;
  io_.hcounter = dot_;
  io_.vcounter = scanline_;
  latch_.counters = true;
}

// ---- framebuffer readback ----

auto Ppu::pixelColor(int x, int y) const -> uint16 {
  // Mirrors the composer: framebuffer row = scanline + 7 (non-overscan) or
  // scanline - 1 (overscan), so visible row y = scanline y + 1.
  int row = y + (state_.overscan ? 0 : 8);
  int col = x + 26;
  uint16 v = frameBuffer_[row * kFrameWidth + col];
  if (getenv("PPU_DEBUG") && y == 1 && x <= 30) printf("px y=%d x=%d -> %04X\n", y, x, v);
  return v;
}

}  // namespace snes
