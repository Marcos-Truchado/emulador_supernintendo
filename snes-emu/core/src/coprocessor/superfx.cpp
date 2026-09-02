#include "coprocessor/superfx.hpp"
#include <cstring>

namespace snes {

SuperFx::SuperFx(){ power(); }

auto SuperFx::handles(uint24 address) const -> bool {
  uint32 bank = address>>16;
  uint32 offs = address & 0xFFFF;
  if (bank<=0x3F || (bank>=0x80 && bank<=0xBF)){
    if (offs>=0x3000 && offs<=0x32FF) return true;
  }
  // GSU RAM at 70-71:0000-FFFF (and 60-6F linear) - let Bus handle via sram, but also handle here for completeness
  if ((bank==0x70 || bank==0x71) && offs<=0xFFFF) return true;
  if (bank>=0x60 && bank<=0x6F) return true;
  return false;
}

auto SuperFx::read(uint24 address) -> uint8 {
  uint32 bank = address>>16;
  uint32 offs = address & 0xFFFF;
  if (offs>=0x3000 && offs<=0x32FF) return gsuRegs_[offs-0x3000];
  if (bank==0x70 || bank==0x71 || (bank>=0x60 && bank<=0x6F)) {
    uint32 gaddr = (bank<<16)|offs;
    // map SNES 70:xxxx to GSU 700000 etc.
    if (bank==0x70) gaddr = 0x700000 | offs;
    else if (bank==0x71) gaddr = 0x710000 | (offs & 0xFFFF);
    else gaddr = 0x600000 | ((bank-0x60)<<16)|offs;
    return gsuRead(gaddr);
  }
  return 0xFF;
}

auto SuperFx::write(uint24 address, uint8 data) -> void {
  uint32 bank = address>>16;
  uint32 offs = address & 0xFFFF;
  if (offs>=0x3000 && offs<=0x32FF) {
    gsuRegs_[offs-0x3000]=data;
    switch(offs){
      case 0x3000: regs_.r[0]= (regs_.r[0]&0xFF00)|data; break;
      case 0x3001: regs_.r[0]= (regs_.r[0]&0x00FF)|(data<<8); break;
      case 0x301F: gsuGo_= data&1; if(gsuGo_) regs_.sfr &= ~0x8000; break;
      case 0x3034: regs_.pbr = data & 0x7F; break;
      case 0x3036: regs_.rombr = data; break;
      case 0x303B: regs_.rambr = data; break;
      case 0x303C: regs_.sfr = (regs_.sfr & 0xFF00)|data; break;
      case 0x303D: regs_.sfr = (regs_.sfr & 0x00FF)|(data<<8); break;
      default: break;
    }
    return;
  }
  if (bank==0x70 || bank==0x71 || (bank>=0x60 && bank<=0x6F)) {
    uint32 gaddr = (bank<<16)|offs;
    if (bank==0x70) gaddr = 0x700000 | offs;
    else if (bank==0x71) gaddr = 0x710000 | (offs & 0xFFFF);
    else gaddr = 0x600000 | ((bank-0x60)<<16)|offs;
    gsuWrite(gaddr, data);
    return;
  }
}

auto SuperFx::power() -> void {
  regs_={}; gsuRegs_.fill(0); gsuRam_.fill(0); cache_.fill(0);
  cacheDirty_=true; gsuGo_=false; gsuIrq_=false;
  regs_.sfr=0x0000; regs_.pbr=0; regs_.rombr=0; regs_.rambr=0;
  gsuRegs_[0x1F]=0;
}

auto SuperFx::serialize(Writer& w) const -> void {
  w.raw(regs_.r, sizeof(regs_.r));
  w.u16(regs_.sfr); w.u16(regs_.pbr); w.u8(regs_.rombr); w.u8(regs_.rambr);
  w.raw(gsuRegs_.data(), gsuRegs_.size());
  w.raw(gsuRam_.data(), gsuRam_.size());
  w.raw(cache_.data(), cache_.size());
  w.b(gsuGo_); w.b(gsuIrq_);
}
auto SuperFx::deserialize(Reader& r) -> void {
  r.raw(regs_.r, sizeof(regs_.r));
  regs_.sfr=r.u16(); regs_.pbr=r.u16(); regs_.rombr=r.u8(); regs_.rambr=r.u8();
  r.raw(gsuRegs_.data(), gsuRegs_.size());
  r.raw(gsuRam_.data(), gsuRam_.size());
  r.raw(cache_.data(), cache_.size());
  gsuGo_=r.b(); gsuIrq_=r.b();
}
auto SuperFx::setRom(const std::vector<uint8>& rom, MapMode mode) -> void {
  (void)mode; romData_=rom.data(); romSize_=rom.size();
}
void SuperFx::setSram(std::vector<uint8>* sram){ sram_=sram; }
auto SuperFx::mapRomAddress(uint24) const -> uint32 { return UINT32_MAX; }

auto SuperFx::gsuRead(uint32 addr) -> uint8 {
  if (addr>=0x3000 && addr<0x3300) return gsuRegs_[addr-0x3000];
  if (addr>=0x700000 && addr<0x710000){
    if(sram_ && !sram_->empty()) return (*sram_)[(addr-0x700000) & (sram_->size()-1)];
    return gsuRam_[(addr-0x700000) & 0xFFFF];
  }
  if (addr<romSize_) return romData_[addr];
  if (addr>=0x600000 && addr<0x700000){
    if(sram_ && !sram_->empty()) return (*sram_)[(addr-0x600000) & (sram_->size()-1)];
  }
  return 0;
}
auto SuperFx::gsuWrite(uint32 addr, uint8 data) -> void {
  if (addr>=0x3000 && addr<0x3300){ gsuRegs_[addr-0x3000]=data; return; }
  if (addr>=0x700000 && addr<0x710000){
    if(sram_ && !sram_->empty()) (*sram_)[(addr-0x700000) & (sram_->size()-1)]=data;
    else gsuRam_[(addr-0x700000)&0xFFFF]=data;
    return;
  }
  if (addr>=0x600000 && addr<0x700000){
    if(sram_ && !sram_->empty()) (*sram_)[(addr-0x600000) & (sram_->size()-1)]=data;
    return;
  }
}

void SuperFx::updateSfr(uint16 v){
  regs_.sfr &= ~(0x000F);
  if(v==0) regs_.sfr|=0x0002;
  if(v & 0x8000) regs_.sfr|=0x0008;
}

void SuperFx::execOne(){
  uint32 pc = (uint32(regs_.pbr)<<16) | regs_.r[15];
  uint8 op = gsuRead(pc++);
  regs_.r[15]= uint16(pc & 0xFFFF);
  // very small dispatch - enough to not crash
  switch(op){
    case 0x00: // STOP
      gsuGo_=false; regs_.sfr|=0x8000; break;
    case 0x01: break; // NOP
    case 0x02: cacheDirty_=true; break; // CACHE
    case 0x03: // LSR
      regs_.sfr = (regs_.sfr & ~0x0002) | ((regs_.r[0]&1)?0:0x0002);
      regs_.r[0] >>=1;
      break;
    case 0x04: { // ROL
      uint16 c = regs_.sfr & 1;
      uint16 nc = regs_.r[0]>>15;
      regs_.r[0]= (regs_.r[0]<<1)|c;
      regs_.sfr = (regs_.sfr & ~1) | nc;
      break;
    }
    case 0x05: { // BRA
      int8 d = int8(gsuRead(pc++)); regs_.r[15]+=d; break;
    }
    case 0x0C: { // MOVE
      uint8 d = gsuRead(pc++); uint8 s = gsuRead(pc++);
      regs_.r[d &15]= regs_.r[s &15];
      break;
    }
    default: {
      // handle 0x10-0x1F TO Rn etc. For minimal, treat 0x80-0x8F as TO, 0x90-0x9F FROM
      if ((op & 0xF0)==0x80) { // TO Rn
        uint8 r = op & 0x0F;
        regs_.r[r]= regs_.sreg;
      } else if ((op & 0xF0)==0x90) {
        uint8 r = op & 0x0F;
        regs_.sreg = regs_.r[r];
      } else if ((op & 0xF0)==0x20) { // ADD Rn
        uint8 r = op & 0x0F;
        uint32 a = regs_.sreg, b= regs_.r[r];
        uint32 res = a + b;
        regs_.sfr = (regs_.sfr & ~0x003F) | ((res & 0x8000?0x08:0)|(res==0?0x02:0)|(res>0xFFFF?0x01:0));
        regs_.r[r]= uint16(res);
      } else if ((op & 0xF0)==0x40) { // SUB Rn
        uint8 r = op & 0x0F;
        uint32 a = regs_.sreg; uint32 b= regs_.r[r];
        uint32 res = b - a;
        regs_.sfr = (regs_.sfr & ~0x003F) | ((res & 0x8000?0x08:0)|(res==0?0x02:0)|(res>0xFFFF?0x01:0));
        regs_.r[r]= uint16(res);
      } else if ((op & 0xF0)==0x60) { // LOAD Rn
        uint8 r = op & 0x0F;
        uint32 addr = regs_.r[r];
        regs_.r[r] = gsuRead(addr) | (gsuRead(addr+1)<<8);
      } else if ((op & 0xF0)==0x70) { // STORE Rn
        uint8 r = op & 0x0F;
        uint32 addr = regs_.r[r];
        gsuWrite(addr, regs_.sreg & 0xFF);
        gsuWrite(addr+1, regs_.sreg>>8);
      } else {
        // unknown -> NOP
      }
      break;
    }
  }
  gsuCycles_++;
}

void SuperFx::stepGsu(){
  if(!gsuGo_) return;
  // run a few instructions per System step (approx 10MHz vs 3.5MHz => 3:1)
  for(int i=0;i<3;i++) execOne();
}

} // namespace snes
