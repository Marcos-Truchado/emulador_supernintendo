#include "coprocessor/sa1.hpp"
#include "cpu/cpu65816.hpp"
#include "scheduler/scheduler.hpp"
#include "ppu/ppu.hpp"

#include <cstring>

namespace snes {

Sa1::Sa1() {
  dummyPpu_ = std::make_unique<Ppu>();
  sa1Scheduler_ = std::make_unique<Scheduler>(*dummyPpu_);
  sa1Bus_ = std::make_unique<Sa1Bus>(*this);
  sa1Cpu_ = std::make_unique<Cpu65816>(*sa1Bus_, *sa1Scheduler_);
  power();
}
Sa1::~Sa1() = default;

auto Sa1::handles(uint24 address) const -> bool {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs >= 0x2200 && offs <= 0x23FF) return true;
    if (offs >= 0x3000 && offs <= 0x37FF) return true;
  }
  return false;
}

auto Sa1::read(uint24 address) -> uint8 {
  uint32 offs = address & 0xFFFF;
  if (offs >= 0x3000 && offs <= 0x37FF) return iram_[offs - 0x3000];
  if (offs >= 0x2200 && offs <= 0x23FF) return readReg(uint16(offs));
  return 0xFF;
}

auto Sa1::write(uint24 address, uint8 data) -> void {
  uint32 offs = address & 0xFFFF;
  if (offs >= 0x3000 && offs <= 0x37FF) {
    // check write protection 2229/222A
    bool snesProt = (regs_[0x29] & 0x80) == 0; // simplified: SNES side protection?
    // For SNES side writes, check 2229; for SA-1 side, 222A. Here we are SNES side.
    if (snesProt) {
      // allow
    }
    iram_[offs - 0x3000] = data;
    return;
  }
  if (offs >= 0x2200 && offs <= 0x23FF) { writeReg(uint16(offs), data); return; }
}

auto Sa1::readReg(uint16 addr) -> uint8 {
  switch (addr) {
    case 0x2300: return (regs_[0x00] & 0x5F) | (regs_[0x100] & 0xA0);
    case 0x2301: return (regs_[0x09] & 0x0F) | (regs_[0x101] & 0xF0);
    case 0x2302: return uint8(hTimer_ & 0xFF);
    case 0x2303: return uint8(hTimer_ >> 8);
    case 0x2304: return uint8(vTimer_ & 0xFF);
    case 0x2305: return uint8(vTimer_ >> 8);
    case 0x2306: return uint8(sum_ >> 0);
    case 0x2307: return uint8(sum_ >> 8);
    case 0x2308: return uint8(sum_ >> 16);
    case 0x2309: return uint8(sum_ >> 24);
    case 0x230a: return uint8(sum_ >> 32);
    case 0x230b: return overflow_ ? 0x80 : 0x00;
    case 0x230c: return regs_[0x0C];
    case 0x230d: {
      uint8 v = regs_[0x0D];
      if (regs_[0x58] & 0x80) doVld(true, false);
      return v;
    }
    case 0x230e: return 0x23;
    default: return regs_[addr - 0x2200];
  }
}

void Sa1::writeReg(uint16 addr, uint8 data) {
  uint8 old = regs_[addr - 0x2200];
  regs_[addr - 0x2200] = data;
  switch (addr) {
    case 0x2200: {
      if (!(data & 0x80) && (old & 0x20)) {
        uint16 pc = regs_[0x03] | (regs_[0x04] << 8);
        sa1Cpu_->setPc((regs_[0x00+0x100] << 16) | pc); // PB from 220? simplified
        sa1Cpu_->setPc(pc);
      }
      if (data & 0x80) regs_[0x101] |= 0x80;
      if (data & 0x10) regs_[0x101] |= 0x10;
      if (mainCpu_ && (data & 0x80)) {
        // IRQ to SA-1
        sa1Cpu_->setIrq(true);
      }
      break;
    }
    case 0x2201: {
      if (((data ^ old) & 0x80) && (regs_[0x100] & data & 0x80)) regs_[0x02] &= ~0x80;
      if (((data ^ old) & 0x20) && (regs_[0x100] & data & 0x20)) regs_[0x02] &= ~0x20;
      break;
    }
    case 0x2202: {
      if (data & 0x80) regs_[0x100] &= ~0x80;
      if (data & 0x20) regs_[0x100] &= ~0x20;
      if (mainCpu_ && !(regs_[0x100] & 0xA0)) mainCpu_->setIrq(false);
      break;
    }
    case 0x2209: {
      if (data & 0x80) {
        regs_[0x100] |= 0x80;
        if (regs_[0x01] & 0x80) regs_[0x101] &= ~0x80;
        if (regs_[0x0A] & 0x80 && (regs_[0x101] & 0x80)) regs_[0x0B] &= ~0x80;
      }
      if (mainCpu_ && (data & 0x80) && (regs_[0x01] & 0x80)) {
        // would set main IRQ
      }
      break;
    }
    case 0x220A: {
      if (((data ^ old) & 0x80) && (regs_[0x101] & data & 0x80)) regs_[0x0B] &= ~0x80;
      if (((data ^ old) & 0x40) && (regs_[0x101] & data & 0x40)) regs_[0x0B] &= ~0x40;
      if (((data ^ old) & 0x20) && (regs_[0x101] & data & 0x20)) regs_[0x0B] &= ~0x20;
      if (((data ^ old) & 0x10) && (regs_[0x101] & data & 0x10)) regs_[0x0B] &= ~0x10;
      break;
    }
    case 0x220B: {
      if (data & 0x80) regs_[0x101] &= ~0x80;
      if (data & 0x40) regs_[0x101] &= ~0x40;
      if (data & 0x20) regs_[0x101] &= ~0x20;
      if (data & 0x10) regs_[0x101] &= ~0x10;
      if (!regs_[0x101]) sa1Cpu_->setIrq(false);
      break;
    }
    case 0x2210: break;
    case 0x2211: hCounter_=0; vCounter_=0; break;
    case 0x2212: hTimer_ = (hTimer_ & 0xFF00) | data; break;
    case 0x2213: hTimer_ = (hTimer_ & 0x00FF) | (data<<8); break;
    case 0x2214: vTimer_ = (vTimer_ & 0xFF00) | data; break;
    case 0x2215: vTimer_ = (vTimer_ & 0x00FF) | (data<<8); break;
    case 0x2220: case 0x2221: case 0x2222: case 0x2223: break;
    case 0x2224: bwRamMapSnes_ = data & 0x1F; break;
    case 0x2225: bwRamMapSa1_ = data; break;
    case 0x2226: case 0x2227: case 0x2228: case 0x2229: case 0x222A: break;
    case 0x2230: break;
    case 0x2231: if (data & 0x80) inCharDma_ = false; break;
    case 0x2236: {
      // 2236 triggers DMA or char conv start
      if ((regs_[0x30] & 0xA4)==0x80) doDma();
      else if ((regs_[0x30] & 0xB0)==0xB0) {
        inCharDma_ = true;
        regs_[0x100] |= 0x20;
      }
      break;
    }
    case 0x2237: if ((regs_[0x30] & 0xA4)==0x84) doDma(); break;
    case 0x223F: break;
    case 0x224F: {
      if ((regs_[0x30] & 0xB0)==0xA0) {
        // char conv 2
        doCharConv();
      }
      break;
    }
    case 0x2250: {
      if (data & 2) sum_ = 0;
      arithMode_ = data & 3;
      break;
    }
    case 0x2251: op1_ = (op1_ & 0xFF00) | data; break;
    case 0x2252: op1_ = (op1_ & 0x00FF) | (data << 8); break;
    case 0x2253: op2_ = (op2_ & 0xFF00) | data; break;
    case 0x2254: {
      op2_ = (op2_ & 0x00FF) | (data << 8);
      if (arithMode_ == 0) {
        int16 a = int16(op1_); int16 b = int16(op2_);
        int32 r = int32(a) * int32(b);
        sum_ = uint32(r);
        op2_ = 0;
      } else if (arithMode_ == 1) {
        if (op2_==0) sum_=0;
        else {
          int16 dividend = int16(op1_);
          uint16 divisor = op2_;
          uint32 ext = uint32(uint16(dividend)) + uint32(divisor)*65536;
          uint16 rem = ext % divisor;
          uint16 quo = ext / divisor;
          sum_ = (uint32(rem)<<16) | quo;
        }
        op1_=op2_=0;
      } else {
        int16 a = int16(op1_); int16 b = int16(op2_);
        sum_ += uint64(int32(a) * int32(b)) & ((1ULL<<40)-1);
        overflow_ = sum_ >= (1ULL<<40);
        sum_ &= (1ULL<<40)-1;
        op2_ = 0;
      }
      break;
    }
    case 0x2258: regs_[0x58]=data; doVld(true,false); return;
    case 0x2259: case 0x225A: case 0x225B: vbitPos_=0; doVld(false,true); break;
    default: break;
  }
}

auto Sa1::sa1Read(uint24 address) -> uint8 {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if (offs >= 0x3000 && offs <= 0x37FF) return iram_[offs - 0x3000];
  if (offs >= 0x2200 && offs <= 0x23FF) return readReg(uint16(offs));
  if (offs >= 0x6000 && offs <= 0x7FFF) {
    if (!sram_) return 0x00;
    // BW-RAM 6000-7FFF via SA-1 mapping 2225
    // simplified: direct offset
    uint32 idx = (offs - 0x6000) & (sram_->size()-1);
    // if bitmap mode 223F bit7 set, handle differently? ignore
    return (*sram_)[idx];
  }
  // ROM
  uint32 mapped = mapRomAddress(address);
  if (mapped != UINT32_MAX && romData_ && mapped < romSize_) return romData_[mapped];
  return 0x00;
}

auto Sa1::sa1Write(uint24 address, uint8 data) -> void {
  uint32 offs = address & 0xFFFF;
  if (offs >= 0x3000 && offs <= 0x37FF) { iram_[offs - 0x3000] = data; return; }
  if (offs >= 0x2200 && offs <= 0x23FF) { writeReg(uint16(offs), data); return; }
  if (offs >= 0x6000 && offs <= 0x7FFF) {
    if (!sram_) return;
    uint32 idx = (offs - 0x6000) & (sram_->size()-1);
    (*sram_)[idx] = data;
    return;
  }
}

auto Sa1::Sa1Bus::read(uint24 address) -> uint8 { return sa1_.sa1Read(address); }
auto Sa1::Sa1Bus::write(uint24 address, uint8 data) -> void { sa1_.sa1Write(address, data); }
auto Sa1::Sa1Bus::waitStates(uint24 address) -> uint8 { (void)address; return 6; }

auto Sa1::mapRomAddress(uint24 snesAddr) const -> uint32 {
  if (!romData_ || romSize_==0) return UINT32_MAX;
  uint32 bank = snesAddr >> 16;
  uint32 offs = snesAddr & 0xFFFF;
  // I-RAM/BW-RAM not ROM
  if (offs < 0x8000 && bank <= 0x3F) return UINT32_MAX;
  // SA-1 ROM banking: 2220-2223 each controls 8Mbit bank? simplified 1MB per bank
  // For SNES: C0-FF and 00-3F:8000 map via 2220-2223
  // Determine which bank selector
  uint8 sel = 0;
  if (bank >= 0xC0 && bank <= 0xCF) sel = regs_[0x20];
  else if (bank >= 0xD0 && bank <= 0xDF) sel = regs_[0x21];
  else if (bank >= 0xE0 && bank <= 0xEF) sel = regs_[0x22];
  else if (bank >= 0xF0) sel = regs_[0x23];
  else if (bank <= 0x3F && offs >= 0x8000) {
    // mirror of C0-CF
    sel = regs_[0x20 + (bank & 3)];
  } else if (bank >= 0x80 && bank <= 0xBF && offs >= 0x8000) {
    sel = regs_[0x20 + (bank & 3)];
  } else return UINT32_MAX;
  uint32 chunk = sel & 0x07;
  uint32 inner = (offs & 0x7FFF);
  // for HiROM 64KB banks, inner is 0-FFFF but we have 32KB windows for LoROM?
  // simplified: use 64KB linear
  if (offs < 0x8000) inner = offs;
  else inner = (offs & 0x7FFF);
  uint32 fileOff = chunk * 0x100000 + inner + ((bank & 0x0F) * 0x8000);
  // wrap
  if (fileOff >= romSize_) {
    if ((romSize_ & (romSize_-1))==0) fileOff &= romSize_-1;
    else fileOff %= romSize_;
  }
  return fileOff;
}

void Sa1::doDma() {
  uint32 src = regs_[0x32] | (regs_[0x33]<<8) | (regs_[0x34]<<16);
  uint32 dst = regs_[0x35] | (regs_[0x36]<<8) | (regs_[0x37]<<16);
  uint32 len = regs_[0x38] | (regs_[0x39]<<8);
  // simplified: ROM->I-RAM or ROM->BW-RAM etc. Just memmove from ROM to I-RAM
  if (len==0) len=0x10000;
  for (uint32 i=0;i<len;i++) {
    uint8 v = sa1Read(src + i);
    sa1Write(dst + i, v);
  }
  regs_[0x01] |= 0x20; // DMA IRQ flag
}

void Sa1::doCharConv() {
  uint32 dest = regs_[0x35] | (regs_[0x36]<<8);
  uint32 offset = (inCharDma_ & 7) ? 0 : 1;
  int depth = (regs_[0x31] & 3)==0 ? 8 : (regs_[0x31] & 3)==1 ? 4 : 2;
  int bpc = 8 * depth;
  // I-RAM dest + offset
  uint8* p = iram_.data() + (dest & 0x7FF) + offset * bpc;
  // ROM temp buffer at MAX_ROM_SIZE-0x10000 + inCharDma*16 (use romData tail)
  // For HLE, use regs 2240-224F as source (16 bytes)
  uint8 q[16];
  for(int i=0;i<16;i++) q[i]=regs_[0x40+i];
  // bitplane conversion
  switch(depth){
    case 2:
      for(int l=0;l<8;l++){
        for(int b=0;b<8;b++){
          uint8 r = q[l*2 + (b>>3)]; // simplified
          (void)r;
        }
      }
      break;
    case 4: {
      for(int l=0;l<8;l++){
        for(int b=0;b<8;b++){
          uint8 r = q[l + b];
          if(b<2){
            p[0] = (p[0]<<1) | ((r>>0)&1);
            p[1] = (p[1]<<1) | ((r>>1)&1);
          }
        }
        p+=2;
      }
      break;
    }
    case 8: {
      for(int l=0;l<8;l++){
        for(int b=0;b<8;b++){
          uint8 r = q[l];
          p[0] = (p[0]<<1) | ((r>>0)&1);
          p[1] = (p[1]<<1) | ((r>>1)&1);
          p[16] = (p[16]<<1) | ((r>>2)&1);
          p[17] = (p[17]<<1) | ((r>>3)&1);
          p[32] = (p[32]<<1) | ((r>>4)&1);
          p[33] = (p[33]<<1) | ((r>>5)&1);
          p[48] = (p[48]<<1) | ((r>>6)&1);
          p[49] = (p[49]<<1) | ((r>>7)&1);
        }
        p+=2;
      }
      break;
    }
  }
  inCharDma_ = (inCharDma_ +1) & 7;
}

void Sa1::doVld(bool inc, bool noShift) {
  uint32 addr = regs_[0x59] | (regs_[0x5A]<<8) | (regs_[0x5B]<<16);
  uint8 shift = regs_[0x58] & 15;
  if (noShift) shift = 0;
  else if (shift==0) shift = 16;
  uint8 s = shift + vbitPos_;
  if (s >= 16) { addr += (s>>4)<<1; s&=15; }
  uint32 data = uint32(sa1Read(addr) | (sa1Read(addr+1)<<8) | (sa1Read(addr+2)<<16) | (sa1Read(addr+3)<<24));
  // snes9x does GetWord(addr) | (GetWord(addr+2)<<16)
  data >>= s;
  regs_[0x0C] = uint8(data);
  regs_[0x0D] = uint8(data>>8);
  if (inc) {
    vbitPos_ = (vbitPos_ + shift) & 15;
    regs_[0x59] = uint8(addr);
    regs_[0x5A] = uint8(addr>>8);
    regs_[0x5B] = uint8(addr>>16);
  }
}

void Sa1::power() {
  regs_.fill(0);
  iram_.fill(0);
  regs_[0x00]=0x20; regs_[0x20]=0x00; regs_[0x21]=0x01; regs_[0x22]=0x02; regs_[0x23]=0x03; regs_[0x28]=0x0F;
  sum_=0; overflow_=false; op1_=op2_=0; arithMode_=0; vbitPos_=0; inCharDma_=false; charDmaPos_=0;
  hTimer_=vTimer_=hCounter_=vCounter_=0;
  bwRamMapSnes_=0; bwRamMapSa1_=0;
  if (sa1Cpu_) { sa1Cpu_->power(); }
  // set SA-1 reset vector from regs 2203/2204 (initial 0)
  if (sa1Cpu_) sa1Cpu_->setPc(0x0000);
}

auto Sa1::serialize(Writer& w) const -> void {
  w.raw(regs_.data(), regs_.size());
  w.raw(iram_.data(), iram_.size());
  w.u16(op1_); w.u16(op2_); w.u64(sum_); w.b(overflow_); w.u8(arithMode_); w.u8(vbitPos_);
  w.b(inCharDma_); w.u8(charDmaPos_);
  w.u16(hTimer_); w.u16(vTimer_); w.u16(hCounter_); w.u16(vCounter_);
  w.u8(bwRamMapSnes_); w.u8(bwRamMapSa1_);
  if (sa1Cpu_) sa1Cpu_->serialize(w);
}

auto Sa1::deserialize(Reader& r) -> void {
  r.raw(regs_.data(), regs_.size());
  r.raw(iram_.data(), iram_.size());
  op1_=r.u16(); op2_=r.u16(); sum_=r.u64(); overflow_=r.b(); arithMode_=r.u8(); vbitPos_=r.u8();
  inCharDma_=r.b(); charDmaPos_=r.u8();
  hTimer_=r.u16(); vTimer_=r.u16(); hCounter_=r.u16(); vCounter_=r.u16();
  bwRamMapSnes_=r.u8(); bwRamMapSa1_=r.u8();
  if (sa1Cpu_) sa1Cpu_->deserialize(r);
}

auto Sa1::setRom(const std::vector<uint8>& rom, MapMode mode) -> void {
  romData_=rom.data(); romSize_=rom.size(); romMode_=mode;
}

void Sa1::setSram(std::vector<uint8>* sram) { sram_=sram; }

void Sa1::stepSa1() {
  if (!sa1Cpu_) return;
  // timer tick (H/V) — HLE: increment by 4 dots per SA-1 instruction, ~10MHz
  hCounter_ += 4;
  if (hCounter_ >= 340) { hCounter_=0; vCounter_++; if(vCounter_>=262) vCounter_=0; }
  uint8 ctrl = regs_[0x10];
  bool hEn = ctrl & 1, vEn = ctrl & 2;
  bool hvMode = ctrl & 0x80;
  bool trig = false;
  if (hvMode) trig = (hEn && hCounter_==hTimer_) || (vEn && vCounter_==vTimer_);
  else trig = (!hEn || hCounter_==hTimer_) && (!vEn || vCounter_==vTimer_);
  if (trig && (regs_[0x0A] & 0x40)) {
    regs_[0x101] |= 0x40;
    if (regs_[0x0A] & 0x40) sa1Cpu_->setIrq(true);
  }
  sa1Cpu_->execute();
}

} // namespace snes
