#include "coprocessor/superfx.hpp"

namespace snes {

namespace {
// SFR flag bits (snes9x fxinst.h / fullsnes GSU I/O Map)
constexpr uint16 kZ = 0x0002, kCY = 0x0004, kS = 0x0008, kOV = 0x0010;
constexpr uint16 kG = 0x0020, kA1 = 0x0100, kA2 = 0x0200, kB = 0x1000, kIRQ = 0x8000;
}  // namespace

SuperFx::SuperFx() { power(); }

auto SuperFx::handles(uint24 address) const -> bool {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs >= 0x3000 && offs <= 0x32FF) return true;
    if (offs >= 0x6000 && offs <= 0x7FFF) return true;  // 1st 8K of Game Pak RAM
  }
  if (bank == 0x70 || bank == 0x71) return true;
  if (bank >= 0x60 && bank <= 0x6F) return true;
  return false;
}

auto SuperFx::ramBase() -> uint8* {
  if (sram_ && !sram_->empty()) return sram_->data();
  return gsuRam_.data();
}
auto SuperFx::ramBase() const -> const uint8* {
  if (sram_ && !sram_->empty()) return sram_->data();
  return gsuRam_.data();
}
auto SuperFx::ramSize() const -> size_t {
  if (sram_ && !sram_->empty()) return sram_->size();
  return gsuRam_.size();
}

auto SuperFx::progByte(uint8 bank, uint16 addr) -> uint8 {
  if (bank >= 0x70 && bank <= 0x73) return ramByte((uint32(bank & 3) << 16) | addr);
  if (romSize_ == 0 || romData_ == nullptr) return 0x01;  // open bus -> NOP
  uint8 b = bank & 0x7F;
  size_t off;
  if (b < 0x40)
    off = (size_t(b % (nRomBanks_ * 2)) << 15) | (addr & 0x7FFF);
  else
    off = (size_t(b % nRomBanks_) << 16) | addr;
  return romData_[off % romSize_];
}

auto SuperFx::romByte(uint8 bank, uint16 addr) -> uint8 { return progByte(bank, addr); }

auto SuperFx::ramByte(uint32 addr) -> uint8 {
  size_t size = ramSize();
  if (size == 0) return 0;
  return ramBase()[addr % size];
}

void SuperFx::ramWriteByte(uint32 addr, uint8 v) {
  size_t size = ramSize();
  if (size == 0) return;
  ramBase()[addr % size] = v;
}

auto SuperFx::ramWord(uint16 addr) -> uint16 {
  uint32 base = uint32(regs_.rambr & 3) << 16;
  uint16 lo = ramByte(base | addr);
  uint16 hi = ramByte(base | (addr ^ 1));
  return lo | (hi << 8);
}

void SuperFx::ramWriteWord(uint16 addr, uint16 v) {
  uint32 base = uint32(regs_.rambr & 3) << 16;
  ramWriteByte(base | addr, uint8(v & 0xFF));
  ramWriteByte(base | (addr ^ 1), uint8(v >> 8));
}

void SuperFx::storeReg(uint8 idx, uint16 v) {
  idx &= 15;
  regs_.r[idx] = v;
  if (idx == 14) refillRomBuffer();
  if (idx == 15) requestJump(regs_.pbr, v);
}

void SuperFx::writeDReg(uint16 v) { storeReg(regs_.dregIdx & 15, v); }

void SuperFx::setLogicFlags(uint16 v) {
  if (v & 0x8000) regs_.sfr |= kS; else regs_.sfr &= ~kS;
  if (v == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
}

void SuperFx::setAddFlags(uint16 s, uint16 n, int32 sum) {
  if (sum >= 0x10000) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
  if ((~(s ^ n) & (n ^ sum) & 0x8000) != 0) regs_.sfr |= kOV; else regs_.sfr &= ~kOV;
  setLogicFlags(uint16(sum));
}

void SuperFx::setSubFlags(uint16 s, uint16 n, int32 diff) {
  if (diff >= 0) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
  if (((s ^ n) & (s ^ diff) & 0x8000) != 0) regs_.sfr |= kOV; else regs_.sfr &= ~kOV;
  setLogicFlags(uint16(diff));
}

void SuperFx::requestJump(uint8 bank, uint16 dest) {
  jump_.active = true;
  jump_.withDelay = true;
  jump_.bank = bank;
  jump_.dest = dest;
}

bool SuperFx::startSession() {
  // snes9x S9xSuperFXExec gate + fx_checkStartAddress
  uint8 scmr = regs_.scmr;
  if ((scmr & 0x18) == 0) return false;
  bool cacheHit =
      cacheActive_ && regs_.r[15] >= regs_.cbr && regs_.r[15] < regs_.cbr + 512;
  bool ron = (scmr & 0x10) != 0, ran = (scmr & 0x08) != 0;
  uint8 pbr = regs_.pbr;
  return cacheHit || (ron && (pbr <= 0x5F || pbr >= 0x80)) || (pbr <= 0x7F && ran);
}

void SuperFx::goFromSnes() {
  regs_.sfr |= kG;
  regs_.sfr &= ~kIRQ;
  regs_.sregIdx = 0;
  regs_.dregIdx = 0;
  regs_.sfr &= ~(kA1 | kA2 | kB);
  syncScreenRegs();
  if (!startSession()) {
    regs_.sfr &= ~kG;
    gsuGo_ = false;
    return;
  }
  gsuGo_ = true;
  gsuCycles_ = 0;
  gsuRegs_[0x30] = uint8(regs_.sfr & 0xFF);
  gsuRegs_[0x31] = uint8(regs_.sfr >> 8);
}

void SuperFx::syncScreenRegs() {
  // snes9x fx_readRegisterSpace screen part
  static const uint32 avHeight[] = {128, 160, 192, 256};
  static const uint32 avMult[] = {16, 32, 32, 64};
  uint8 scbr = regs_.scbr;
  uint8 scmr = regs_.scmr;
  size_t rsize = ramSize();
  const uint8* base = ramBase();
  int n = (scmr & 0x04 ? 1 : 0) | (scmr & 0x20 ? 2 : 0);
  vScreenRealHeight_ = avHeight[n];
  vMode_ = scmr & 0x03;
  if (n == 3)
    vScreenSize_ = (256 / 8) * (256 / 8) * 32;
  else
    vScreenSize_ = (vScreenRealHeight_ / 8) * (256 / 8) * avMult[vMode_];
  if (regs_.por & 0x10)
    vScreenHeight_ = 256;
  else
    vScreenHeight_ = vScreenRealHeight_;
  size_t off = size_t(scbr) << 10;
  if (off + vScreenSize_ > rsize) off = rsize > vScreenSize_ ? rsize - vScreenSize_ : 0;
  pvScreenBase_ = const_cast<uint8*>(base) + off;
  computeScreenPointers();
}

void SuperFx::computeScreenPointers() {
  // snes9x fx_computeScreenPointers verbatim
  if (vMode_ == vPrevMode_ && vScreenHeight_ == vPrevScreenHeight_ && !scbrDirty_) return;
  scbrDirty_ = false;
  switch (vScreenHeight_) {
    case 128:
      switch (vMode_) {
        case 0: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 4); x_[i] = i << 8; } break;
        case 1: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 5); x_[i] = i << 9; } break;
        default: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 6); x_[i] = i << 10; } break;
      }
      break;
    case 160:
      switch (vMode_) {
        case 0: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 4); x_[i] = (i << 8) + (i << 6); } break;
        case 1: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 5); x_[i] = (i << 9) + (i << 7); } break;
        default: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 6); x_[i] = (i << 10) + (i << 8); } break;
      }
      break;
    case 192:
      switch (vMode_) {
        case 0: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 4); x_[i] = (i << 8) + (i << 7); } break;
        case 1: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 5); x_[i] = (i << 9) + (i << 8); } break;
        default: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + (i << 6); x_[i] = (i << 10) + (i << 9); } break;
      }
      break;
    case 256:
      switch (vMode_) {
        case 0: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + ((i & 0x10) << 9) + ((i & 0xF) << 8); x_[i] = ((i & 0x10) << 8) + ((i & 0xF) << 4); } break;
        case 1: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + ((i & 0x10) << 10) + ((i & 0xF) << 9); x_[i] = ((i & 0x10) << 9) + ((i & 0xF) << 5); } break;
        default: for (int i = 0; i < 32; i++) { apvScreen_[i] = pvScreenBase_ + ((i & 0x10) << 11) + ((i & 0xF) << 10); x_[i] = ((i & 0x10) << 10) + ((i & 0xF) << 6); } break;
      }
      break;
    default: break;
  }
  vPrevMode_ = vMode_;
  vPrevScreenHeight_ = vScreenHeight_;
}

void SuperFx::opColor() {
  // snes9x fx_color
  uint8 c = uint8(readSReg() & 0xFF);
  if (regs_.por & 0x04) c = uint8((c & 0xF0) | (c >> 4));
  if (regs_.por & 0x08)
    regs_.colr = uint8((regs_.colr & 0xF0) | (c & 0x0F));
  else
    regs_.colr = c;
  clearPlotFlags();
}

void SuperFx::opCmode() {
  // snes9x fx_cmode
  regs_.por = uint8(readSReg() & 0x1F);
  if (regs_.por & 0x10)
    vScreenHeight_ = 256;
  else
    vScreenHeight_ = vScreenRealHeight_;
  computeScreenPointers();
  clearPlotFlags();
}

void SuperFx::opPlot2bit() {
  uint32 x = regs_.r[1] & 0xFF, y = regs_.r[2] & 0xFF;
  regs_.r[1] = uint16(regs_.r[1] + 1);
  clearPlotFlags();
  if (y >= vScreenHeight_) return;
  if (!(regs_.por & 0x01) && !(regs_.colr & 0x0F)) return;
  uint8 c = (regs_.por & 0x02) ? uint8(((x ^ y) & 1) ? (regs_.colr >> 4) : regs_.colr) : regs_.colr;
  uint8* a = apvScreen_[y >> 3] + x_[x >> 3] + ((y & 7) << 1);
  uint8 v = uint8(128 >> (x & 7));
  if (c & 0x01) a[0] |= v; else a[0] &= uint8(~v);
  if (c & 0x02) a[1] |= v; else a[1] &= uint8(~v);
}

void SuperFx::opPlot4bit() {
  uint32 x = regs_.r[1] & 0xFF, y = regs_.r[2] & 0xFF;
  regs_.r[1] = uint16(regs_.r[1] + 1);
  clearPlotFlags();
  if (y >= vScreenHeight_) return;
  if (!(regs_.por & 0x01) && !(regs_.colr & 0x0F)) return;
  uint8 c = (regs_.por & 0x02) ? uint8(((x ^ y) & 1) ? (regs_.colr >> 4) : regs_.colr) : regs_.colr;
  uint8* a = apvScreen_[y >> 3] + x_[x >> 3] + ((y & 7) << 1);
  uint8 v = uint8(128 >> (x & 7));
  if (c & 0x01) a[0x00] |= v; else a[0x00] &= uint8(~v);
  if (c & 0x02) a[0x01] |= v; else a[0x01] &= uint8(~v);
  if (c & 0x04) a[0x10] |= v; else a[0x10] &= uint8(~v);
  if (c & 0x08) a[0x11] |= v; else a[0x11] &= uint8(~v);
}

void SuperFx::opPlot8bit() {
  uint32 x = regs_.r[1] & 0xFF, y = regs_.r[2] & 0xFF;
  regs_.r[1] = uint16(regs_.r[1] + 1);
  uint8 c = regs_.colr;
  bool skip = false;
  if (!(regs_.por & 0x10)) {
    if (!(regs_.por & 0x01) && (!c || ((regs_.por & 0x08) && !(c & 0x0F)))) skip = true;
  } else if (!(regs_.por & 0x01) && !c)
    skip = true;
  clearPlotFlags();
  if (y >= vScreenHeight_) return;
  if (skip) return;
  uint8* a = apvScreen_[y >> 3] + x_[x >> 3] + ((y & 7) << 1);
  uint8 v = uint8(128 >> (x & 7));
  if (c & 0x01) a[0x00] |= v; else a[0x00] &= uint8(~v);
  if (c & 0x02) a[0x01] |= v; else a[0x01] &= uint8(~v);
  if (c & 0x04) a[0x10] |= v; else a[0x10] &= uint8(~v);
  if (c & 0x08) a[0x11] |= v; else a[0x11] &= uint8(~v);
  if (c & 0x10) a[0x20] |= v; else a[0x20] &= uint8(~v);
  if (c & 0x20) a[0x21] |= v; else a[0x21] &= uint8(~v);
  if (c & 0x40) a[0x30] |= v; else a[0x30] &= uint8(~v);
  if (c & 0x80) a[0x31] |= v; else a[0x31] &= uint8(~v);
}

void SuperFx::opRpix2bit() {
  uint32 x = regs_.r[1] & 0xFF, y = regs_.r[2] & 0xFF;
  uint16 d = 0;
  if (y < vScreenHeight_) {
    uint8* a = apvScreen_[y >> 3] + x_[x >> 3] + ((y & 7) << 1);
    uint8 v = uint8(128 >> (x & 7));
    if (a[0] & v) d |= 1;
    if (a[1] & v) d |= 2;
  }
  storeReg(regs_.dregIdx & 15, d);
  if (d == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
  clearPlotFlags();
}

void SuperFx::opRpix4bit() {
  uint32 x = regs_.r[1] & 0xFF, y = regs_.r[2] & 0xFF;
  uint16 d = 0;
  if (y < vScreenHeight_) {
    uint8* a = apvScreen_[y >> 3] + x_[x >> 3] + ((y & 7) << 1);
    uint8 v = uint8(128 >> (x & 7));
    if (a[0x00] & v) d |= 1;
    if (a[0x01] & v) d |= 2;
    if (a[0x10] & v) d |= 4;
    if (a[0x11] & v) d |= 8;
  }
  storeReg(regs_.dregIdx & 15, d);
  if (d == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
  clearPlotFlags();
}

void SuperFx::opRpix8bit() {
  uint32 x = regs_.r[1] & 0xFF, y = regs_.r[2] & 0xFF;
  uint16 d = 0;
  if (y < vScreenHeight_) {
    uint8* a = apvScreen_[y >> 3] + x_[x >> 3] + ((y & 7) << 1);
    uint8 v = uint8(128 >> (x & 7));
    if (a[0x00] & v) d |= 1;
    if (a[0x01] & v) d |= 2;
    if (a[0x10] & v) d |= 4;
    if (a[0x11] & v) d |= 8;
    if (a[0x20] & v) d |= 16;
    if (a[0x21] & v) d |= 32;
    if (a[0x30] & v) d |= 64;
    if (a[0x31] & v) d |= 128;
  }
  storeReg(regs_.dregIdx & 15, d);
  if (d == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
  clearPlotFlags();
}

void SuperFx::opCache() {
  // snes9x fx_cache (data copy disabled upstream too; CBR + active only)
  uint16 c = uint16(regs_.r[15] & 0xFFF0);
  if (regs_.cbr != c || !cacheActive_) {
    regs_.cbr = c;
    cacheActive_ = true;
    cacheDirty_ = false;
  }
  clearPlotFlags();
}

auto SuperFx::read(uint24 address) -> uint8 {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && offs >= 0x3000 && offs <= 0x32FF) {
    uint32 i = offs - 0x3000;
    if (i < 0x20) {
      uint8 r = uint8(i >> 1);
      return (i & 1) ? uint8(regs_.r[r] >> 8) : uint8(regs_.r[r] & 0xFF);
    }
    switch (i) {
      case 0x30: return uint8(regs_.sfr & 0xFF);
      case 0x31: {
        uint8 v = uint8(regs_.sfr >> 8);
        regs_.sfr &= ~kIRQ;  // reading SFR high clears IRQ (snes9x)
        return v;
      }
      case 0x34: return regs_.pbr & 0x7F;
      case 0x36: return regs_.rombr & 0x7F;
      case 0x37: return regs_.cfgr;
      case 0x38: return regs_.scbr;
      case 0x39: return regs_.clsr;
      case 0x3A: return regs_.scmr;
      case 0x3B: return regs_.vcr;
      case 0x3C: return regs_.rambr;
      case 0x3E: return uint8(regs_.cbr & 0xFF);
      case 0x3F: return uint8(regs_.cbr >> 8);
      default: break;
    }
    return gsuRegs_[i];
  }
  if (bank == 0x70 || bank == 0x71) return ramByte((uint32(bank & 1) << 16) | offs);
  if (bank >= 0x60 && bank <= 0x6F) return ramByte((uint32(bank - 0x60) << 16) | offs);
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && offs >= 0x6000 && offs <= 0x7FFF)
    return ramByte(offs - 0x6000);
  return 0xFF;
}

auto SuperFx::write(uint24 address, uint8 data) -> void {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && offs >= 0x3000 && offs <= 0x32FF) {
    uint32 i = offs - 0x3000;
    gsuRegs_[i] = data;
    if (i < 0x20) {
      uint8 r = uint8(i >> 1);
      if ((i & 1) == 0) {
        latch_ = data;
      } else {
        regs_.r[r] = uint16(uint16(latch_) | (uint16(data) << 8));
        if (r == 14) refillRomBuffer();
        if (r == 15) goFromSnes();  // writing R15 MSB starts GSU (fullsnes)
      }
      return;
    }
    switch (i) {
      case 0x30: {
        bool wasGo = (regs_.sfr & kG) != 0;
        regs_.sfr = uint16((regs_.sfr & 0xFF00) | data);
        if (wasGo && !(regs_.sfr & kG)) {  // GO 1->0 aborts: CBR=0, flush
          gsuGo_ = false;
          regs_.cbr = 0;
          cacheActive_ = false;
        } else if (!wasGo && (regs_.sfr & kG)) {
          goFromSnes();
        }
        break;
      }
      case 0x31:
        regs_.sfr = uint16((regs_.sfr & 0x00FF) | (uint16(data) << 8));
        if (!(regs_.sfr & kG)) {
          gsuGo_ = false;
          regs_.cbr = 0;
          cacheActive_ = false;
        } else if (!gsuGo_) {
          goFromSnes();
        }
        break;
      case 0x33: regs_.bramr = data; break;
      case 0x34: regs_.pbr = data & 0x7F; break;
      case 0x36: regs_.rombr = data & 0x7F; break;
      case 0x37: regs_.cfgr = data; break;
      case 0x38: regs_.scbr = data; scbrDirty_ = true; syncScreenRegs(); break;
      case 0x39: regs_.clsr = data; break;
      case 0x3A: regs_.scmr = data; syncScreenRegs(); break;
      case 0x3C: regs_.rambr = data; break;
      default: break;  // VCR/CBR read-only, cache RAM plain storage
    }
    return;
  }
  if (bank == 0x70 || bank == 0x71) {
    ramWriteByte((uint32(bank & 1) << 16) | offs, data);
    return;
  }
  if (bank >= 0x60 && bank <= 0x6F) {
    ramWriteByte((uint32(bank - 0x60) << 16) | offs, data);
    return;
  }
  if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) && offs >= 0x6000 && offs <= 0x7FFF) {
    ramWriteByte(offs - 0x6000, data);
    return;
  }
}

auto SuperFx::power() -> void {
  regs_ = {};
  regs_.vcr = 4;
  gsuRegs_.fill(0);
  gsuRam_.fill(0);
  cache_.fill(0);
  cacheDirty_ = true;
  cacheActive_ = false;
  gsuGo_ = false;
  jump_ = {};
  latch_ = 0;
  gsuCycles_ = 0;
  jump_ = {};
  vScreenHeight_ = 128;
  vScreenRealHeight_ = 128;
  vScreenSize_ = 0;
  vMode_ = 0;
  vPrevMode_ = 0xFFFFFFFFu;
  vPrevScreenHeight_ = 0xFFFFFFFFu;
  scbrDirty_ = true;
  pvScreenBase_ = ramBase();
  syncScreenRegs();
  gsuRegs_[0x3B] = 4;
}

auto SuperFx::serialize(Writer& w) const -> void {
  w.raw(regs_.r, sizeof(regs_.r));
  w.u16(regs_.sfr);
  w.u8(regs_.pbr);
  w.u8(regs_.rombr);
  w.u8(regs_.rambr);
  w.u16(regs_.cbr);
  w.u8(regs_.scbr);
  w.u8(regs_.scmr);
  w.u8(regs_.colr);
  w.u8(regs_.por);
  w.u8(regs_.clsr);
  w.u8(regs_.cfgr);
  w.u8(regs_.vcr);
  w.u8(regs_.bramr);
  w.u8(regs_.sregIdx);
  w.u8(regs_.dregIdx);
  w.u8(regs_.romBuffer);
  w.u32(regs_.lastRamAdr);
  w.u8(latch_);
  w.b(cacheActive_);
  w.b(scbrDirty_);
  w.u32(vMode_);
  w.u32(vScreenHeight_);
  w.u32(vScreenRealHeight_);
  w.u32(vScreenSize_);
  w.raw(gsuRegs_.data(), gsuRegs_.size());
  w.raw(gsuRam_.data(), gsuRam_.size());
  w.raw(cache_.data(), cache_.size());
  w.b(gsuGo_);
}

auto SuperFx::deserialize(Reader& r) -> void {
  r.raw(regs_.r, sizeof(regs_.r));
  regs_.sfr = r.u16();
  regs_.pbr = r.u8();
  regs_.rombr = r.u8();
  regs_.rambr = r.u8();
  regs_.cbr = r.u16();
  regs_.scbr = r.u8();
  regs_.scmr = r.u8();
  regs_.colr = r.u8();
  regs_.por = r.u8();
  regs_.clsr = r.u8();
  regs_.cfgr = r.u8();
  regs_.vcr = r.u8();
  regs_.bramr = r.u8();
  regs_.sregIdx = r.u8();
  regs_.dregIdx = r.u8();
  regs_.romBuffer = r.u8();
  regs_.lastRamAdr = r.u32();
  latch_ = r.u8();
  cacheActive_ = r.b();
  scbrDirty_ = r.b();
  vMode_ = r.u32();
  vScreenHeight_ = r.u32();
  vScreenRealHeight_ = r.u32();
  vScreenSize_ = r.u32();
  r.raw(gsuRegs_.data(), gsuRegs_.size());
  r.raw(gsuRam_.data(), gsuRam_.size());
  r.raw(cache_.data(), cache_.size());
  gsuGo_ = r.b();
  gsuGo_ = gsuGo_ && (regs_.sfr & kG) != 0;
  jump_ = {};
  vPrevMode_ = 0xFFFFFFFFu;
  vPrevScreenHeight_ = 0xFFFFFFFFu;
  syncScreenRegs();
}

auto SuperFx::setRom(const std::vector<uint8>& rom, MapMode mode) -> void {
  (void)mode;
  romData_ = rom.data();
  romSize_ = rom.size();
  size_t banks = romSize_ >> 16;
  if (banks < 1) banks = 1;
  if (banks > 0x20) banks = 0x20;
  nRomBanks_ = banks;
}

void SuperFx::setSram(std::vector<uint8>* sram) { sram_ = sram; }

auto SuperFx::mapRomAddress(uint24) const -> uint32 { return UINT32_MAX; }

auto SuperFx::gsuRead(uint32 addr) -> uint8 {
  if (addr >= 0x3000 && addr < 0x3300) {
    uint32 i = addr - 0x3000;
    if (i < 0x20) {
      uint8 r = uint8(i >> 1);
      return (i & 1) ? uint8(regs_.r[r] >> 8) : uint8(regs_.r[r] & 0xFF);
    }
    return read(0x003000 + i);
  }
  if (addr >= 0x700000 && addr < 0x720000) return ramByte(addr - 0x700000);
  if (addr >= 0x600000 && addr < 0x700000) return ramByte(addr - 0x600000);
  return progByte(uint8(addr >> 16), uint16(addr & 0xFFFF));
}

auto SuperFx::gsuWrite(uint32 addr, uint8 data) -> void {
  if (addr >= 0x3000 && addr < 0x3300) {
    uint32 i = addr - 0x3000;
    if (i >= 0x100) return;
    gsuRegs_[i] = data;
    return;
  }
  if (addr >= 0x700000 && addr < 0x720000) {
    ramWriteByte(addr - 0x700000, data);
    return;
  }
  if (addr >= 0x600000 && addr < 0x700000) {
    ramWriteByte(addr - 0x600000, data);
    return;
  }
}

void SuperFx::execOne() {
  if (!gsuRunning()) return;
  execAt(regs_.pbr, regs_.r[15], true);
  gsuCycles_++;
}

void SuperFx::execAt(uint8 bank, uint16 pc, bool withDelay) {
  jump_.active = false;
  gsuCycles_++;
  uint8 op = progByte(bank, pc++);
  regs_.r[15] = pc;
  bool a1 = (regs_.sfr & kA1) != 0;
  bool a2 = (regs_.sfr & kA2) != 0;
  bool b = (regs_.sfr & kB) != 0;
  auto imm8 = [&]() -> uint8 {
    uint8 v = progByte(bank, pc++);
    regs_.r[15] = pc;
    return v;
  };
  uint8 n = op & 0x0F;

  if (op <= 0x0F) {
    switch (op) {
      case 0x00: {  // STOP: prefetched byte at $+1 NOT executed
        regs_.r[15] = pc + 1;
        regs_.sfr &= ~kG;
        if (!(regs_.cfgr & 0x80)) regs_.sfr |= kIRQ;
        regs_.por = 0;
        clearPlotFlags();
        gsuGo_ = false;
        jump_.active = false;
        return;
      }
      case 0x01: clearPlotFlags(); break;  // NOP
      case 0x02: opCache(); break;         // CACHE
      case 0x03: {                         // LSR
        uint16 s = readSReg();
        if (s & 1) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
        uint16 v = uint16(s >> 1);
        writeDReg(v);
        setLogicFlags(v);
        clearPlotFlags();
        break;
      }
      case 0x04: {  // ROL
        uint16 s = readSReg();
        uint16 c = (regs_.sfr & kCY) ? 1 : 0;
        if (s & 0x8000) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
        uint16 v = uint16((s << 1) | c);
        writeDReg(v);
        setLogicFlags(v);
        clearPlotFlags();
        break;
      }
      case 0x05: {  // BRA (preserves prefixes)
        int8_t o = int8_t(imm8());
        requestJump(bank, uint16(int(pc) + int(o)));
        break;
      }
      case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
      case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: {
        int8_t o = int8_t(imm8());
        bool s = (regs_.sfr & kS) != 0, z = (regs_.sfr & kZ) != 0;
        bool cy = (regs_.sfr & kCY) != 0, ov = (regs_.sfr & kOV) != 0;
        bool take = false;
        switch (op) {
          case 0x06: take = (s == ov); break;  // BGE
          case 0x07: take = (s != ov); break;  // BLT
          case 0x08: take = !z; break;         // BNE
          case 0x09: take = z; break;          // BEQ
          case 0x0A: take = !s; break;         // BPL
          case 0x0B: take = s; break;          // BMI
          case 0x0C: take = !cy; break;        // BCC
          case 0x0D: take = cy; break;         // BCS
          case 0x0E: take = !ov; break;        // BVC
          default: take = ov; break;           // BVS
        }
        if (take) requestJump(bank, uint16(int(pc) + int(o)));
        break;
      }
    }
  } else if (op <= 0x1F) {  // TO Rn / MOVE Rn (B)
    if (b) {
      storeReg(n, readSReg());
      clearPlotFlags();
    } else {
      regs_.dregIdx = n;
    }
  } else if (op <= 0x2F) {  // WITH Rn
    regs_.sfr |= kB;
    regs_.sregIdx = n;
    regs_.dregIdx = n;
  } else if (op <= 0x3B) {  // STW / STB Rn
    uint16 a = regs_.r[n];
    regs_.lastRamAdr = a;
    if (a1) {
      uint8 v = uint8(readSReg() & 0xFF);
      ramWriteByte((uint32(regs_.rambr & 3) << 16) | a, v);
    } else {
      uint16 v = readSReg();
      ramWriteWord(a, v);
    }
    clearPlotFlags();
  } else if (op == 0x3C) {  // LOOP
    uint16 c = uint16(regs_.r[12] - 1);
    regs_.r[12] = c;
    setLogicFlags(c);
    if (c != 0) requestJump(bank, regs_.r[13]);
    clearPlotFlags();
  } else if (op == 0x3D) {  // ALT1
    regs_.sfr |= kA1;
    regs_.sfr &= ~kB;
  } else if (op == 0x3E) {  // ALT2
    regs_.sfr |= kA2;
    regs_.sfr &= ~kB;
  } else if (op == 0x3F) {  // ALT3
    regs_.sfr |= (kA1 | kA2);
    regs_.sfr &= ~kB;
  } else if (op <= 0x4B) {  // LDW / LDB Rn
    uint16 a = regs_.r[n];
    regs_.lastRamAdr = a;
    writeDReg(a1 ? ramByte((uint32(regs_.rambr & 3) << 16) | a) : ramWord(a));
    clearPlotFlags();
  } else if (op == 0x4C) {  // PLOT / RPIX
    if (a1) {
      if (vMode_ == 0) opRpix2bit(); else if (vMode_ <= 2) opRpix4bit(); else opRpix8bit();
    } else {
      if (vMode_ == 0) opPlot2bit(); else if (vMode_ <= 2) opPlot4bit(); else opPlot8bit();
    }
  } else if (op == 0x4D) {  // SWAP
    uint16 s = readSReg();
    uint16 v = uint16(((s & 0xFF) << 8) | ((s >> 8) & 0xFF));
    writeDReg(v);
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op == 0x4E) {  // COLOR / CMODE
    if (a1) opCmode(); else opColor();
  } else if (op == 0x4F) {  // NOT
    uint16 v = uint16(~readSReg());
    writeDReg(v);
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op <= 0x5F) {  // ADD/ADC Rn/#n
    uint16 s = readSReg();
    if (!a1 && !a2) {
      uint16 r = regs_.r[n];
      setAddFlags(s, r, int32(s) + int32(r));
      writeDReg(uint16(s + r));
    } else if (a1 && !a2) {
      uint16 r = regs_.r[n];
      uint16 cy = (regs_.sfr & kCY) ? 1 : 0;
      setAddFlags(s, r, int32(s) + int32(r) + int32(cy));
      writeDReg(uint16(s + r + cy));
    } else if (!a1 && a2) {
      setAddFlags(s, n, int32(s) + int32(n));
      writeDReg(uint16(s + n));
    } else {
      uint16 cy = (regs_.sfr & kCY) ? 1 : 0;
      setAddFlags(s, n, int32(s) + int32(n) + int32(cy));
      writeDReg(uint16(s + n + cy));
    }
    clearPlotFlags();
  } else if (op <= 0x6F) {  // SUB/SBC Rn/#n, CMP Rn
    uint16 s = readSReg();
    if (!a1 && !a2) {
      uint16 r = regs_.r[n];
      setSubFlags(s, r, int32(s) - int32(r));
      writeDReg(uint16(s - r));
    } else if (a1 && !a2) {
      uint16 r = regs_.r[n];
      uint16 bc = (regs_.sfr & kCY) ? 0 : 1;
      setSubFlags(s, r, int32(s) - int32(r) - int32(bc));
      writeDReg(uint16(s - r - bc));
    } else if (!a1 && a2) {
      setSubFlags(s, n, int32(s) - int32(n));
      writeDReg(uint16(s - n));
    } else {
      uint16 r = regs_.r[n];
      setSubFlags(s, r, int32(s) - int32(r));
    }
    clearPlotFlags();
  } else if (op == 0x70) {  // MERGE (fullsnes: Z set iff (v&F0F0)!=0, "(not set when zero!)"; ares matches)
    uint16 v = uint16((regs_.r[7] & 0xFF00) | ((regs_.r[8] & 0xFF00) >> 8));
    writeDReg(v);
    if ((v & 0x8080) != 0) regs_.sfr |= kS; else regs_.sfr &= ~kS;
    if ((v & 0xF0F0) != 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
    if ((v & 0xC0C0) != 0) regs_.sfr |= kOV; else regs_.sfr &= ~kOV;
    if ((v & 0xE0E0) != 0) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
    clearPlotFlags();
  } else if (op <= 0x7F) {  // AND/BIC Rn/#n
    uint16 s = readSReg();
    uint16 v = (!a1 && !a2) ? uint16(s & regs_.r[n])
               : (a1 && !a2) ? uint16(s & ~regs_.r[n])
               : (!a1 && a2) ? uint16(s & n)
                             : uint16(s & ~n);
    writeDReg(v);
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op <= 0x8F) {  // MULT/UMULT Rn/#n
    uint16 s = readSReg();
    int32 v = (!a1) ? int32(int8_t(s & 0xFF)) * (!a2 ? int32(int8_t(regs_.r[n] & 0xFF)) : int32(n))
                    : int32(uint16(s & 0xFF)) * (!a2 ? int32(uint16(regs_.r[n] & 0xFF)) : int32(n));
    writeDReg(uint16(v));
    setLogicFlags(uint16(v));
    clearPlotFlags();
  } else if (op == 0x90) {  // SBK
    uint16 v = readSReg();
    ramWriteWord(uint16(regs_.lastRamAdr), v);
    clearPlotFlags();
  } else if (op >= 0x91 && op <= 0x94) {  // LINK #n
    regs_.r[11] = uint16(regs_.r[15] + n);
    clearPlotFlags();
  } else if (op == 0x95) {  // SEX
    uint16 v = uint16(int16_t(int8_t(readSReg() & 0xFF)));
    writeDReg(v);
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op == 0x96) {  // ASR / DIV2
    if (a1) {
      int32 s = int16_t(readSReg());
      if (s & 1) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
      uint16 v = (s == -1) ? 0 : uint16(s >> 1);
      writeDReg(v);
      setLogicFlags(v);
    } else {
      uint16 s = readSReg();
      if (s & 1) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
      uint16 v = uint16(int16_t(s) >> 1);
      writeDReg(v);
      setLogicFlags(v);
    }
    clearPlotFlags();
  } else if (op == 0x97) {  // ROR
    uint16 s = readSReg();
    uint16 c = (regs_.sfr & kCY) ? 1 : 0;
    if (s & 1) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
    uint16 v = uint16((s >> 1) | (c << 15));
    writeDReg(v);
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op >= 0x98 && op <= 0x9D) {  // JMP / LJMP Rn
    if (a1) {
      uint8 nb = regs_.r[n] & 0x7F;
      uint16 d = readSReg();
      requestJump(nb, d);
      regs_.cbr = d & 0xFFF0;
      cacheActive_ = true;
    } else {
      requestJump(regs_.pbr, regs_.r[n]);
    }
    clearPlotFlags();
  } else if (op == 0x9E) {  // LOB
    uint16 v = readSReg() & 0xFF;
    writeDReg(v);
    if (v & 0x80) regs_.sfr |= kS; else regs_.sfr &= ~kS;
    if (v == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
    clearPlotFlags();
  } else if (op == 0x9F) {  // FMULT / LMULT
    int32 c = int32(int16_t(readSReg())) * int32(int16_t(regs_.r[6]));
    if (a1) {
      regs_.r[4] = uint16(uint32(c) & 0xFFFF);
      uint16 v = uint16(uint32(c) >> 16);
      writeDReg(v);
      setLogicFlags(v);
      if (regs_.r[4] & 0x8000) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
    } else {
      uint16 v = uint16(uint32(c) >> 16);
      writeDReg(v);
      setLogicFlags(v);
      if ((uint32(c) >> 15) & 1) regs_.sfr |= kCY; else regs_.sfr &= ~kCY;
    }
    clearPlotFlags();
  } else if (op <= 0xAF) {  // IBT / LMS / SMS Rn
    if (!a1 && !a2) {
      storeReg(n, uint16(int16_t(int8_t(imm8()))));
    } else if (!a1 && a2) {
      uint16 snap = regs_.r[n];
      uint16 a = uint16(imm8()) * 2;
      regs_.lastRamAdr = a;
      ramWriteWord(a, snap);
    } else {
      uint16 a = uint16(imm8()) * 2;
      regs_.lastRamAdr = a;
      storeReg(n, ramWord(a));
    }
    clearPlotFlags();
  } else if (op <= 0xBF) {  // FROM Rn / MOVES Rn (B)
    if (b) {
      uint16 v = regs_.r[n];
      storeReg(regs_.dregIdx & 15, v);
      if (v & 0x80) regs_.sfr |= kOV; else regs_.sfr &= ~kOV;
      if (v & 0x8000) regs_.sfr |= kS; else regs_.sfr &= ~kS;
      if (v == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
      clearPlotFlags();
    } else {
      regs_.sregIdx = n;
    }
  } else if (op == 0xC0) {  // HIB
    uint16 v = uint16((readSReg() >> 8) & 0xFF);
    writeDReg(v);
    if (v & 0x80) regs_.sfr |= kS; else regs_.sfr &= ~kS;
    if (v == 0) regs_.sfr |= kZ; else regs_.sfr &= ~kZ;
    clearPlotFlags();
  } else if (op <= 0xCF) {  // OR/XOR Rn/#n
    uint16 s = readSReg();
    uint16 v = (!a1 && !a2) ? uint16(s | regs_.r[n])
               : (a1 && !a2) ? uint16(s ^ regs_.r[n])
               : (!a1 && a2) ? uint16(s | n)
                             : uint16(s ^ n);
    writeDReg(v);
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op <= 0xDE) {  // INC Rn (HW/snes9x: no ROM-buffer refill on INC/DEC; Yoshi boot depends on stale GETB)
    uint16 v = uint16(regs_.r[n] + 1);
    regs_.r[n] = v;
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op == 0xDF) {  // GETC / RAMB / ROMB
    if (!a1 && !a2) {
      uint8 c = regs_.romBuffer;
      if (regs_.por & 0x04) c = uint8((c & 0xF0) | (c >> 4));
      if (regs_.por & 0x08)
        regs_.colr = uint8((regs_.colr & 0xF0) | (c & 0x0F));
      else
        regs_.colr = c;
    } else if (!a1 && a2) {
      regs_.rambr = readSReg() & 3;
    } else if (a1 && a2) {
      regs_.rombr = readSReg() & 0x7F;
    } else {
      uint8 c = regs_.romBuffer;
      if (regs_.por & 0x04) c = uint8((c & 0xF0) | (c >> 4));
      if (regs_.por & 0x08)
        regs_.colr = uint8((regs_.colr & 0xF0) | (c & 0x0F));
      else
        regs_.colr = c;
    }
    clearPlotFlags();
  } else if (op <= 0xEE) {  // DEC Rn (snes9x: no TESTR14/READR14 here)
    uint16 v = uint16(regs_.r[n] - 1);
    regs_.r[n] = v;
    setLogicFlags(v);
    clearPlotFlags();
  } else if (op == 0xEF) {  // GETB/H/L/S
    uint8 cbuf = regs_.romBuffer;
    if (!a1 && !a2) {
      writeDReg(cbuf);
    } else if (a1 && !a2) {
      writeDReg(uint16((readSReg() & 0xFF) | (uint16(cbuf) << 8)));
    } else if (!a1 && a2) {
      writeDReg(uint16((readSReg() & 0xFF00) | cbuf));
    } else {
      writeDReg(uint16(int16_t(int8_t(cbuf))));
    }
    clearPlotFlags();
  } else {  // IWT / LM / SM Rn
    if (!a1 && !a2) {
      uint8 lo = imm8();
      uint8 hi = imm8();
      storeReg(n, uint16(lo | (hi << 8)));
    } else if (!a1 && a2) {
      uint16 snap = regs_.r[n];
      uint8 lo = imm8();
      uint8 hi = imm8();
      uint16 a = uint16(lo | (hi << 8));
      regs_.lastRamAdr = a;
      ramWriteWord(a, snap);
    } else {
      uint8 lo = imm8();
      uint8 hi = imm8();
      uint16 a = uint16(lo | (hi << 8));
      regs_.lastRamAdr = a;
      storeReg(n, ramWord(a));
    }
    clearPlotFlags();
  }

  PendingJump mine = jump_;
  jump_.active = false;
  if (mine.active && withDelay) {
    // delay slot: next byte runs before the jump lands (fullsnes)
    execAt(bank, pc, false);
    PendingJump inner = jump_;
    jump_.active = false;
    if (inner.active) {
      regs_.pbr = inner.bank;
      regs_.r[15] = inner.dest;
    } else {
      regs_.pbr = mine.bank;
      regs_.r[15] = mine.dest;
    }
    return;
  }
  if (mine.active) {
    regs_.pbr = mine.bank;
    regs_.r[15] = mine.dest;
    return;
  }
  regs_.r[15] = pc;
}

void SuperFx::stepGsu() {  if (!gsuRunning()) return;
  // Batch model (like snes9x per-line batching): run the GSU session until
  // STOP or a slice cap so tight SNES GO-polls always observe completion.
  for (int i = 0; i < 256 && gsuRunning(); i++) execOne();
  gsuRegs_[0x30] = uint8(regs_.sfr & 0xFF);
  gsuRegs_[0x31] = uint8(regs_.sfr >> 8);
}

}  // namespace snes
