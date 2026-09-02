#include "coprocessor/spc7110.hpp"

#include <chrono>
#include <cstring>
#include <ctime>

namespace snes {

namespace {
auto daysInMonth(int year, int month) -> int {
  static const int d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int v = d[month % 12];
  if (month == 1) {
    bool leap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
    if (leap) v++;
  }
  return v;
}
}  // namespace

Spc7110::Model Spc7110::evolution_[53] = {
    {0x5a, {1, 1}},   {0x25, {2, 6}},   {0x11, {3, 8}},   {0x08, {4, 10}},  {0x03, {5, 12}},  {0x01, {5, 15}},
    {0x5a, {7, 7}},   {0x3f, {8, 19}},  {0x2c, {9, 21}},  {0x20, {10, 22}}, {0x17, {11, 23}}, {0x11, {12, 25}},
    {0x0c, {13, 26}}, {0x09, {14, 28}}, {0x07, {15, 29}}, {0x05, {16, 31}}, {0x04, {17, 32}}, {0x03, {18, 34}},
    {0x02, {5, 35}},  {0x5a, {20, 20}}, {0x48, {21, 39}}, {0x3a, {22, 40}}, {0x2e, {23, 42}}, {0x26, {24, 44}},
    {0x1f, {25, 45}}, {0x19, {26, 46}}, {0x15, {27, 25}}, {0x11, {28, 26}}, {0x0e, {29, 26}}, {0x0b, {30, 27}},
    {0x09, {31, 28}}, {0x08, {32, 29}}, {0x07, {33, 30}}, {0x05, {34, 31}}, {0x04, {35, 33}}, {0x04, {36, 33}},
    {0x03, {37, 34}}, {0x02, {38, 35}}, {0x02, {5, 36}},  {0x58, {40, 39}}, {0x4d, {41, 47}}, {0x43, {42, 48}},
    {0x3b, {43, 49}}, {0x34, {44, 50}}, {0x2e, {45, 51}}, {0x29, {46, 44}}, {0x25, {24, 45}}, {0x56, {48, 47}},
    {0x4f, {49, 47}}, {0x47, {50, 48}}, {0x41, {51, 49}}, {0x3c, {52, 50}}, {0x37, {43, 51}},
};

Spc7110::Spc7110() { power(); }

auto Spc7110::handles(uint24 address) const -> bool {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if (bank == 0x50 || bank == 0x58) return true;
  if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
    if (offs >= 0x4800 && offs <= 0x4842) return true;
  }
  return false;
}

auto Spc7110::dataromAddr(uint32 addr) const -> uint32 {
  if (!romData_ || romSize_ == 0) return addr;
  uint32 dataSize = romSize_ > 0x100000 ? uint32(romSize_ - 0x100000) : 0;
  if (dataSize == 0) return addr & 0xFFFFF;
  // wrap like snes9x: repeatedly subtract dataSize
  uint32 off = addr;
  if (off >= 0x100000) off -= 0x100000;
  while (off >= dataSize) off -= dataSize;
  return off + 0x100000;
}

auto Spc7110::dataromRead(uint32 addr) const -> uint8 {
  if (!romData_ || romSize_ == 0) return 0xFF;
  uint32 off = dataromAddr(addr);
  if (off >= romSize_) {
    if ((romSize_ & (romSize_ - 1)) == 0) off &= uint32(romSize_ - 1);
    else return 0x00;
  }
  return romData_[off];
}

auto Spc7110::dataPointer() const -> uint32 { return uint32(r4811_) | (uint32(r4812_) << 8) | (uint32(r4813_) << 16); }
auto Spc7110::dataAdjust() const -> uint32 { return uint32(r4814_) | (uint32(r4815_) << 8); }
auto Spc7110::dataIncrement() const -> uint32 { return uint32(r4816_) | (uint32(r4817_) << 8); }
void Spc7110::setDataPointer(uint32 a) { r4811_ = uint8(a); r4812_ = uint8(a >> 8); r4813_ = uint8(a >> 16); }
void Spc7110::setDataAdjust(uint32 a) { r4814_ = uint8(a); r4815_ = uint8(a >> 8); }

void Spc7110::dataPortRead() {
  uint32 off = dataPointer();
  uint32 adj = (r4818_ & 2) ? dataAdjust() : 0;
  if (r4818_ & 8) adj = uint32(int16(adj));
  r4810_ = dataromRead(off + adj);
}

void Spc7110::dataPortInc4810() {
  uint32 off = dataPointer();
  uint32 inc = (r4818_ & 1) ? dataIncrement() : 1;
  uint32 adj = dataAdjust();
  if (r4818_ & 4) inc = uint32(int16(inc));
  if (r4818_ & 8) adj = uint32(int16(adj));
  if ((r4818_ & 16) == 0) setDataPointer(off + inc);
  else setDataAdjust(adj + inc);
  dataPortRead();
}

void Spc7110::dataPortInc4814() {
  if ((r4818_ >> 5) != 1) return;
  uint32 off = dataPointer();
  uint32 adj = dataAdjust();
  if (r4818_ & 8) adj = uint32(int16(adj));
  setDataPointer(off + adj);
  dataPortRead();
}

void Spc7110::dataPortInc4815() {
  if ((r4818_ >> 5) != 2) return;
  uint32 off = dataPointer();
  uint32 adj = dataAdjust();
  if (r4818_ & 8) adj = uint32(int16(adj));
  setDataPointer(off + adj);
  dataPortRead();
}

void Spc7110::dataPortInc481a() {
  if ((r4818_ >> 5) != 3) return;
  uint32 off = dataPointer();
  uint32 adj = dataAdjust();
  if (r4818_ & 8) adj = uint32(int16(adj));
  setDataPointer(off + adj);
  dataPortRead();
}

auto Spc7110::deinterleave(uint64 data, uint bits) -> uint32 {
  data &= (bits >= 64 ? ~0ULL : ((1ULL << bits) - 1));
  data = 0x5555555555555555ULL & (data << bits | data >> 1);
  data = 0x3333333333333333ULL & (data | data >> 1);
  data = 0x0f0f0f0f0f0f0f0fULL & (data | data >> 2);
  data = 0x00ff00ff00ff00ffULL & (data | data >> 4);
  data = 0x0000ffff0000ffffULL & (data | data >> 8);
  return uint32(data | data >> 16);
}

auto Spc7110::moveToFront(uint64 list, uint64 nibble) -> uint64 {
  for (uint64 n = 0, mask = ~15ULL; n < 64; n += 4, mask <<= 4) {
    if (((list >> n) & 15) != nibble) continue;
    return (list & mask) + ((list << 4) & ~mask) + nibble;
  }
  return list;
}

void Spc7110::decompInit(uint32 mode, uint32 origin) {
  for (auto& row : ctx_) for (auto& c : row) c = {0, 0};
  bpp_ = 1u << (mode & 3);
  offset_ = origin;
  bits_ = 8;
  range_ = 0x100;
  uint8 b0 = dataromRead(offset_++);
  uint8 b1 = dataromRead(offset_++);
  input_ = uint16(b0 << 8 | b1);
  output_ = 0;
  pixels_ = 0;
  colormap_ = 0xfedcba9876543210ULL;
  result_ = 0;
}

void Spc7110::decompDecode() {
  for (uint32 pixel = 0; pixel < 8; pixel++) {
    uint64 map = colormap_;
    uint32 diff = 0;
    if (bpp_ > 1) {
      uint32 pa = (bpp_ == 2 ? (pixels_ >> 2) & 3 : (pixels_ >> 0) & 15);
      uint32 pb = (bpp_ == 2 ? (pixels_ >> 14) & 3 : (pixels_ >> 28) & 15);
      uint32 pc = (bpp_ == 2 ? (pixels_ >> 16) & 3 : (pixels_ >> 32) & 15);
      if (pa != pb || pb != pc) {
        uint32 match = pa ^ pb ^ pc;
        diff = 4;
        if ((match ^ pc) == 0) diff = 3;
        if ((match ^ pb) == 0) diff = 2;
        if ((match ^ pa) == 0) diff = 1;
      }
      colormap_ = moveToFront(colormap_, pa);
      map = moveToFront(map, pc);
      map = moveToFront(map, pb);
      map = moveToFront(map, pa);
    }
    for (uint32 plane = 0; plane < bpp_; plane++) {
      uint32 bit = bpp_ > 1 ? (1u << plane) : (1u << (pixel & 3));
      uint32 history = (bit - 1) & output_;
      uint32 set = 0;
      if (bpp_ == 1) set = pixel >= 4 ? 1 : 0;
      else if (bpp_ == 2) set = diff;
      else if (plane >= 2 && history <= 1) set = diff;
      uint32 idx = bit + history - 1;
      if (idx >= 15) idx = 14;
      auto& ctx = ctx_[set][idx];
      auto& mod = evolution_[ctx.pred];
      uint16 lps = range_ - mod.prob;
      bool sym = input_ >= (lps << 8);
      output_ = uint8((output_ << 1) | (sym ^ ctx.swap));
      if (!sym) range_ = lps;
      else { range_ -= lps; input_ -= lps << 8; }
      while (range_ <= 0x7F) {
        ctx.pred = mod.next[sym ? 1 : 0];
        range_ <<= 1;
        input_ <<= 1;
        if (--bits_ == 0) { bits_ = 8; input_ += dataromRead(offset_++); }
      }
      if (sym && mod.prob > 0x55) ctx.swap ^= 1;
    }
    uint32 index = output_ & ((1u << bpp_) - 1);
    if (bpp_ == 1) index ^= (pixels_ >> 15) & 1;
    pixels_ = (pixels_ << bpp_) | ((map >> (4 * index)) & 15);
  }
  if (bpp_ == 1) result_ = uint32(pixels_);
  else if (bpp_ == 2) result_ = deinterleave(pixels_, 16);
  else result_ = deinterleave(deinterleave(pixels_, 32), 32);
}

void Spc7110::dcuLoadAddress() {
  uint32 table = uint32(r4801_) | (uint32(r4802_) << 8) | (uint32(r4803_) << 16);
  uint32 idx = uint32(r4804_) << 2;
  uint32 addr = dataromAddr(table + idx);
  dcuMode_ = dataromRead(addr + 0);
  dcuAddress_ = (uint32(dataromRead(addr + 1)) << 16) | (uint32(dataromRead(addr + 2)) << 8) | uint32(dataromRead(addr + 3));
}

void Spc7110::dcuBeginTransfer() {
  if (dcuMode_ == 3) return;
  decompInit(dcuMode_, dcuAddress_);
  decompDecode();
  uint32 seek = (r480b_ & 2) ? (uint32(r4805_) | (uint32(r4806_) << 8)) : 0;
  // apply mode-dependent scaling: fullsnes says offset_mode x1/x2/x4 based on low 2 bits of first byte? Simplified: if mode 1 seek*=2, mode2 seek*=4?
  // Use bsnes: while(seek--) decompressor->decode(); without scaling; scaling already in init offset?
  // Follow snes9x: offset = (4805|4806<<8) << mode ; already done in init via <<mode . Do not double shift here.
  while (seek--) decompDecode();
  r480c_ |= 0x80;
  dcuOffset_ = 0;
}

auto Spc7110::dcuRead() -> uint8 {
  if ((r480c_ & 0x80) == 0) return 0x00;
  if (dcuOffset_ == 0) {
    for (int row = 0; row < 8; row++) {
      switch (bpp_) {
        case 1: dcuTile_[row] = uint8(result_ >> 0); break;
        case 2:
          dcuTile_[row * 2 + 0] = uint8(result_ >> 0);
          dcuTile_[row * 2 + 1] = uint8(result_ >> 8);
          break;
        case 4:
          dcuTile_[row * 2 + 0] = uint8(result_ >> 0);
          dcuTile_[row * 2 + 1] = uint8(result_ >> 8);
          dcuTile_[row * 2 + 16] = uint8(result_ >> 16);
          dcuTile_[row * 2 + 17] = uint8(result_ >> 24);
          break;
        default: break;
      }
      uint32 seek = (r480b_ & 1) ? r4807_ : 1;
      while (seek--) decompDecode();
    }
  }
  uint8 v = dcuTile_[dcuOffset_];
  dcuOffset_ = uint8((dcuOffset_ + 1) & (8 * bpp_ - 1));
  return v;
}

void Spc7110::aluMultiply() {
  if (r482e_ & 1) {
    int16 a = int16(r4824_ | (r4825_ << 8));
    int16 b = int16(r4820_ | (r4821_ << 8));
    int32 res = int32(a) * int32(b);
    r4828_ = uint8(res); r4829_ = uint8(res >> 8); r482a_ = uint8(res >> 16); r482b_ = uint8(res >> 24);
  } else {
    uint16 a = uint16(r4824_ | (r4825_ << 8));
    uint16 b = uint16(r4820_ | (r4821_ << 8));
    uint32 res = uint32(a) * uint32(b);
    r4828_ = uint8(res); r4829_ = uint8(res >> 8); r482a_ = uint8(res >> 16); r482b_ = uint8(res >> 24);
  }
  r482f_ &= 0x7F;
}

void Spc7110::aluDivide() {
  if (r482e_ & 1) {
    int32 dividend = int32(r4820_ | (r4821_ << 8) | (r4822_ << 16) | (r4823_ << 24));
    int16 divisor = int16(r4826_ | (r4827_ << 8));
    int32 q = 0; int16 rem = 0;
    if (divisor != 0) { q = dividend / divisor; rem = dividend % divisor; }
    else { q = 0; rem = int16(dividend & 0xFFFF); }
    r4828_ = uint8(q); r4829_ = uint8(q >> 8); r482a_ = uint8(q >> 16); r482b_ = uint8(q >> 24);
    r482c_ = uint8(rem); r482d_ = uint8(rem >> 8);
  } else {
    uint32 dividend = uint32(r4820_ | (r4821_ << 8) | (r4822_ << 16) | (r4823_ << 24));
    uint16 divisor = uint16(r4826_ | (r4827_ << 8));
    uint32 q = 0; uint16 rem = 0;
    if (divisor != 0) { q = dividend / divisor; rem = dividend % divisor; }
    else { q = 0; rem = uint16(dividend & 0xFFFF); }
    r4828_ = uint8(q); r4829_ = uint8(q >> 8); r482a_ = uint8(q >> 16); r482b_ = uint8(q >> 24);
    r482c_ = uint8(rem); r482d_ = uint8(rem >> 8);
  }
  r482f_ &= 0x7F;
}

void Spc7110::rtcUpdate() {
  // update rtcRegs_ from host time if timer not disabled
  bool timerDisabled = (rtcRegs_[13] & 1) || (rtcRegs_[15] & 3);
  if (timerDisabled) return;
  std::time_t now = std::time(nullptr);
  // diff since last
  if (rtcLastSec_ == 0) { rtcLastSec_ = uint32(now); return; }
  uint32 diff = uint32(now) - rtcLastSec_;
  if (diff == 0) return;
  rtcLastSec_ = uint32(now);
  // decode regs
  int sec = rtcRegs_[0] + rtcRegs_[1] * 10;
  int minu = rtcRegs_[2] + rtcRegs_[3] * 10;
  int hour = rtcRegs_[4] + rtcRegs_[5] * 10;
  int day = rtcRegs_[6] + rtcRegs_[7] * 10;
  int mon = rtcRegs_[8] + rtcRegs_[9] * 10;
  int year = rtcRegs_[10] + rtcRegs_[11] * 10;
  int wday = rtcRegs_[12];
  year += (year >= 90 ? 1900 : 2000);
  day--; mon--;
  sec += diff;
  while (sec >= 60) {
    sec -= 60; minu++;
    if (minu < 60) continue; minu = 0;
    hour++;
    if (hour < 24) continue; hour = 0;
    day++; wday = (wday + 1) % 7;
    int dim = daysInMonth(year, mon);
    if (day < dim) continue; day = 0;
    mon++;
    if (mon < 12) continue; mon = 0; year++;
  }
  day++; mon++; year %= 100;
  rtcRegs_[0] = sec % 10; rtcRegs_[1] = sec / 10;
  rtcRegs_[2] = minu % 10; rtcRegs_[3] = minu / 10;
  rtcRegs_[4] = hour % 10; rtcRegs_[5] = hour / 10;
  rtcRegs_[6] = day % 10; rtcRegs_[7] = day / 10;
  rtcRegs_[8] = mon % 10; rtcRegs_[9] = mon / 10;
  rtcRegs_[10] = year % 10; rtcRegs_[11] = (year / 10) % 10;
  rtcRegs_[12] = wday % 7;
}

auto Spc7110::rtcRead() -> uint8 {
  rtcUpdate();
  r4842_ = 0x80;
  uint8 v = rtcRegs_[rtcIndex_] & 0x0F;
  rtcIndex_ = (rtcIndex_ + 1) & 15;
  return v;
}

void Spc7110::rtcWrite(uint8 data) {
  data &= 0x0F;
  if (rtcState_ == RtcState::ModeSelect) {
    if (data == 0x03 || data == 0x0C) {
      r4842_ = 0x80;
      rtcState_ = RtcState::IndexSelect;
      rtcMode_ = (data == 0x03 ? RtcMode::Linear : RtcMode::Indexed);
      rtcIndex_ = 0;
    }
    return;
  }
  if (rtcState_ == RtcState::IndexSelect) {
    rtcIndex_ = data & 15;
    r4842_ = 0x80;
    if (rtcMode_ == RtcMode::Linear) rtcState_ = RtcState::Write;
    return;
  }
  if (rtcState_ == RtcState::Write) {
    if (rtcIndex_ == 13) {
      if (data & 2) rtcUpdate(); // increment sec
      if (data & 8) {
        rtcUpdate();
        int sec = rtcRegs_[0] + rtcRegs_[1] * 10;
        rtcRegs_[0] = 0; rtcRegs_[1] = 0;
        if (sec >= 30) {
          // add minute
          int minu = rtcRegs_[2] + rtcRegs_[3] * 10 + 1;
          if (minu >= 60) minu = 0;
          rtcRegs_[2] = minu % 10; rtcRegs_[3] = minu / 10;
        }
      }
    }
    if (rtcIndex_ == 15) {
      if ((data & 1) && !(rtcRegs_[15] & 1)) { rtcRegs_[0]=0; rtcRegs_[1]=0; }
      if ((data & 2) && !(rtcRegs_[15] & 2)) rtcUpdate();
    }
    rtcRegs_[rtcIndex_] = data;
    r4842_ = 0x80;
    rtcIndex_ = (rtcIndex_ + 1) & 15;
  }
}

auto Spc7110::mapRomAddress(uint24 snesAddr) const -> uint32 {
  if (!romData_ || romSize_ == 0) return UINT32_MAX;
  uint32 bank = snesAddr >> 16;
  uint32 offs = snesAddr & 0xFFFF;
  // Program ROM C0-CF
  if (bank >= 0xC0 && bank <= 0xCF) {
    uint32 off = ((bank - 0xC0) << 16) | offs;
    if (off >= 0x100000) return UINT32_MAX;
    return off;
  }
  if (bank >= 0x80 && bank <= 0x8F && offs >= 0x8000) {
    uint32 off = ((bank - 0x80) << 15) | (offs & 0x7FFF);
    if (off >= 0x100000) return UINT32_MAX;
    return off;
  }
  // Data ROM D/E/F
  if (bank >= 0xD0 && bank <= 0xDF) {
    uint32 chunk = r4831_ & 7;
    uint32 inner = ((bank - 0xD0) << 16) | offs;
    uint32 addr = chunk * 0x100000 + inner;
    return dataromAddr(addr + 0x100000);
  }
  if (bank >= 0xE0 && bank <= 0xEF) {
    uint32 chunk = r4832_ & 7;
    uint32 inner = ((bank - 0xE0) << 16) | offs;
    uint32 addr = chunk * 0x100000 + inner;
    return dataromAddr(addr + 0x100000);
  }
  if (bank >= 0xF0) {
    uint32 chunk = r4833_ & 7;
    uint32 inner = ((bank - 0xF0) << 16) | offs;
    uint32 addr = chunk * 0x100000 + inner;
    return dataromAddr(addr + 0x100000);
  }
  // HiROM mirrors 40-4F -> C0 etc? For simplicity handle 40-5F as mirrors
  if (bank >= 0x40 && bank <= 0x4F) {
    uint32 off = ((bank - 0x40) << 16) | offs;
    if (off < 0x100000) return off;
  }
  return UINT32_MAX;
}

auto Spc7110::isDataRomWindow(uint24 a) const -> bool {
  return mapRomAddress(a) != UINT32_MAX;
}

auto Spc7110::read(uint24 address) -> uint8 {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if (bank == 0x50 || bank == 0x58) {
    return dcuRead();
  }
  uint32 a = offs;
  if (a >= 0x4800 && a <= 0x4842) {
    switch (a) {
      case 0x4800: {
        uint16 ctr = uint16(r4809_ | (r480a_ << 8));
        ctr--;
        r4809_ = uint8(ctr); r480a_ = uint8(ctr >> 8);
        return dcuRead();
      }
      case 0x4801: return r4801_;
      case 0x4802: return r4802_;
      case 0x4803: return r4803_;
      case 0x4804: return r4804_;
      case 0x4805: return r4805_;
      case 0x4806: return r4806_;
      case 0x4807: return r4807_;
      case 0x4808: return 0x00;
      case 0x4809: return r4809_;
      case 0x480a: return r480a_;
      case 0x480b: return r480b_;
      case 0x480c: { uint8 s = r480c_; r480c_ &= 0x7F; return s; }
      case 0x4810: {
        if (r481x_ != 0x07) return 0x00;
        uint32 ptr = dataPointer();
        uint32 adj = dataAdjust();
        if (r4818_ & 8) adj = uint32(int16(adj));
        uint32 addr2 = ptr;
        if (r4818_ & 2) { addr2 += adj; setDataAdjust(adj + 1); }
        uint8 d = dataromRead(dataromAddr(addr2));
        if (!(r4818_ & 2)) {
          uint32 inc = (r4818_ & 1) ? dataIncrement() : 1;
          if (r4818_ & 4) inc = uint32(int16(inc));
          if ((r4818_ & 16) == 0) setDataPointer(ptr + inc);
          else setDataAdjust(adj + inc);
        }
        return d;
      }
      case 0x4811: return r4811_;
      case 0x4812: return r4812_;
      case 0x4813: return r4813_;
      case 0x4814: return r4814_;
      case 0x4815: return r4815_;
      case 0x4816: return r4816_;
      case 0x4817: return r4817_;
      case 0x4818: return r4818_;
      case 0x481a: {
        if (r481x_ != 0x07) return 0x00;
        uint32 ptr = dataPointer();
        uint32 adj = dataAdjust();
        if (r4818_ & 8) adj = uint32(int16(adj));
        uint8 d = dataromRead(dataromAddr(ptr + adj));
        if ((r4818_ & 0x60) == 0x60) {
          if ((r4818_ & 16) == 0) setDataPointer(ptr + adj);
          else setDataAdjust(adj + adj);
        }
        return d;
      }
      case 0x4820: return r4820_;
      case 0x4821: return r4821_;
      case 0x4822: return r4822_;
      case 0x4823: return r4823_;
      case 0x4824: return r4824_;
      case 0x4825: return r4825_;
      case 0x4826: return r4826_;
      case 0x4827: return r4827_;
      case 0x4828: return r4828_;
      case 0x4829: return r4829_;
      case 0x482a: return r482a_;
      case 0x482b: return r482b_;
      case 0x482c: return r482c_;
      case 0x482d: return r482d_;
      case 0x482e: return r482e_;
      case 0x482f: { uint8 s = r482f_; r482f_ &= 0x7F; return s; }
      case 0x4830: return r4830_;
      case 0x4831: return r4831_;
      case 0x4832: return r4832_;
      case 0x4833: return r4833_;
      case 0x4834: return r4834_;
      case 0x4840: return r4840_;
      case 0x4841: {
        if (rtcState_ == RtcState::Inactive || rtcState_ == RtcState::ModeSelect) return 0x00;
        return rtcRead();
      }
      case 0x4842: { uint8 s = r4842_; r4842_ &= 0x7F; return s; }
      default: return 0x00;
    }
  }
  return 0xFF;
}

auto Spc7110::write(uint24 address, uint8 data) -> void {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  if (bank == 0x50 || bank == 0x58) return;
  uint32 a = offs;
  if (a < 0x4800 || a > 0x4842) return;
  switch (a) {
    case 0x4801: r4801_ = data; break;
    case 0x4802: r4802_ = data; break;
    case 0x4803: r4803_ = data; break;
    case 0x4804: r4804_ = data; dcuLoadAddress(); break;
    case 0x4805: r4805_ = data; break;
    case 0x4806: r4806_ = data; r480c_ &= 0x7F; dcuBeginTransfer(); break;
    case 0x4807: r4807_ = data; break;
    case 0x4808: r4808_ = data; break;
    case 0x4809: r4809_ = data; break;
    case 0x480a: r480a_ = data; break;
    case 0x480b: r480b_ = data & 0x03; break;
    case 0x4811: r4811_ = data; r481x_ |= 0x01; break;
    case 0x4812: r4812_ = data; r481x_ |= 0x02; break;
    case 0x4813: r4813_ = data; r481x_ |= 0x04; dataPortRead(); break;
    case 0x4814: {
      r4814_ = data; r4814_latch_ = true;
      if (!r4815_latch_) break;
      if (!(r4818_ & 2)) break;
      if (r4818_ & 16) break;
      if ((r4818_ & 0x60) == 0x20) {
        uint32 inc = dataAdjust() & 0xFF;
        if (r4818_ & 8) inc = uint32(int8(inc));
        setDataPointer(dataPointer() + inc);
      } else if ((r4818_ & 0x60) == 0x40) {
        uint32 inc = dataAdjust();
        if (r4818_ & 8) inc = uint32(int16(inc));
        setDataPointer(dataPointer() + inc);
      }
      break;
    }
    case 0x4815: {
      r4815_ = data; r4815_latch_ = true;
      if (!r4814_latch_) break;
      if (!(r4818_ & 2)) break;
      if (r4818_ & 16) break;
      if ((r4818_ & 0x60) == 0x20) {
        uint32 inc = dataAdjust() & 0xFF;
        if (r4818_ & 8) inc = uint32(int8(inc));
        setDataPointer(dataPointer() + inc);
      } else if ((r4818_ & 0x60) == 0x40) {
        uint32 inc = dataAdjust();
        if (r4818_ & 8) inc = uint32(int16(inc));
        setDataPointer(dataPointer() + inc);
      }
      break;
    }
    case 0x4816: r4816_ = data; break;
    case 0x4817: r4817_ = data; break;
    case 0x4818: {
      if (r481x_ != 0x07) break;
      r4818_ = data & 0x7F;
      r4814_latch_ = r4815_latch_ = false;
      dataPortRead();
      break;
    }
    case 0x4820: r4820_ = data; break;
    case 0x4821: r4821_ = data; break;
    case 0x4822: r4822_ = data; break;
    case 0x4823: r4823_ = data; break;
    case 0x4824: r4824_ = data; break;
    case 0x4825: r4825_ = data; r482f_ |= 0x80; aluMultiply(); break;
    case 0x4826: r4826_ = data; break;
    case 0x4827: r4827_ = data; r482f_ |= 0x80; aluDivide(); break;
    case 0x482e: {
      r4820_ = r4821_ = r4822_ = r4823_ = 0;
      r4824_ = r4825_ = r4826_ = r4827_ = 0;
      r4828_ = r4829_ = r482a_ = r482b_ = 0;
      r482c_ = r482d_ = 0;
      r482e_ = data & 1;
      break;
    }
    case 0x4830: r4830_ = data & 0x87; break;
    case 0x4831: r4831_ = data & 0x07; dxOffset_ = dataromAddr((r4831_ & 7) * 0x100000 + 0x100000); break;
    case 0x4832: r4832_ = data & 0x07; exOffset_ = dataromAddr((r4832_ & 7) * 0x100000 + 0x100000); break;
    case 0x4833: r4833_ = data & 0x07; fxOffset_ = dataromAddr((r4833_ & 7) * 0x100000 + 0x100000); break;
    case 0x4834: r4834_ = data & 0x07; break;
    case 0x4840: {
      r4840_ = data;
      if (!(r4840_ & 1)) { rtcState_ = RtcState::Inactive; rtcUpdate(); }
      else { r4842_ = 0x80; rtcState_ = RtcState::ModeSelect; }
      break;
    }
    case 0x4841: r4841_ = data; rtcWrite(data); break;
    default: break;
  }
}

void Spc7110::power() {
  r4801_ = r4802_ = r4803_ = r4804_ = 0;
  r4805_ = r4806_ = r4807_ = r4808_ = 0;
  r4809_ = r480a_ = r480b_ = 0; r480c_ = 0;
  dcuMode_ = 0; dcuAddress_ = 0; dcuOffset_ = 0; std::memset(dcuTile_, 0, sizeof(dcuTile_));
  for (auto& row : ctx_) for (auto& c : row) c = {0,0};
  bpp_ = 1; offset_ = 0; bits_ = 8; range_ = 0x100; input_ = 0; output_ = 0; pixels_ = 0; colormap_ = 0xfedcba9876543210ULL; result_ = 0;
  r4810_ = 0; r4811_ = r4812_ = r4813_ = 0; r4814_ = r4815_ = 0; r4816_ = r4817_ = 0; r4818_ = 0; r481x_ = 0; r4814_latch_ = r4815_latch_ = false;
  r4820_ = r4821_ = r4822_ = r4823_ = 0; r4824_ = r4825_ = r4826_ = r4827_ = 0; r4828_ = r4829_ = r482a_ = r482b_ = 0; r482c_ = r482d_ = 0; r482e_ = 0; r482f_ = 0;
  r4830_ = 0; r4831_ = 0; r4832_ = 1; r4833_ = 2; r4834_ = 0; dxOffset_ = dataromAddr(0x100000); exOffset_ = dataromAddr(0x200000); fxOffset_ = dataromAddr(0x300000);
  r4840_ = r4841_ = r4842_ = 0; rtcState_ = RtcState::Inactive; rtcMode_ = RtcMode::Linear; rtcIndex_ = 0; rtcRegs_.fill(0);
  rtcRegs_[6] = 1; rtcRegs_[8] = 1; // 01/01
  rtcLastSec_ = uint32(std::time(nullptr));
}

auto Spc7110::serialize(Writer& w) const -> void {
  w.u8(r4801_); w.u8(r4802_); w.u8(r4803_); w.u8(r4804_); w.u8(r4805_); w.u8(r4806_); w.u8(r4807_); w.u8(r4808_); w.u8(r4809_); w.u8(r480a_); w.u8(r480b_); w.u8(r480c_);
  w.u8(r4810_); w.u8(r4811_); w.u8(r4812_); w.u8(r4813_); w.u8(r4814_); w.u8(r4815_); w.u8(r4816_); w.u8(r4817_); w.u8(r4818_); w.u8(r481x_); w.b(r4814_latch_); w.b(r4815_latch_);
  w.u8(r4820_); w.u8(r4821_); w.u8(r4822_); w.u8(r4823_); w.u8(r4824_); w.u8(r4825_); w.u8(r4826_); w.u8(r4827_); w.u8(r4828_); w.u8(r4829_); w.u8(r482a_); w.u8(r482b_); w.u8(r482c_); w.u8(r482d_); w.u8(r482e_); w.u8(r482f_);
  w.u8(r4830_); w.u8(r4831_); w.u8(r4832_); w.u8(r4833_); w.u8(r4834_);
  w.u8(r4840_); w.u8(r4841_); w.u8(r4842_); w.u8(uint8(rtcState_)); w.u8(uint8(rtcMode_)); w.u8(rtcIndex_); w.raw(rtcRegs_.data(), rtcRegs_.size()); w.u32(rtcLastSec_);
  w.u32(dxOffset_); w.u32(exOffset_); w.u32(fxOffset_);
  w.u8(dcuMode_); w.u32(dcuAddress_); w.raw(dcuTile_, sizeof(dcuTile_)); w.u8(dcuOffset_);
  for (auto& row : ctx_) for (auto& c : row) { w.u8(c.pred); w.u8(c.swap); }
  w.u32(bpp_); w.u32(offset_); w.u32(bits_); w.u16(range_); w.u16(input_); w.u8(output_); w.u64(pixels_); w.u64(colormap_); w.u32(result_);
}

auto Spc7110::deserialize(Reader& r) -> void {
  r4801_ = r.u8(); r4802_ = r.u8(); r4803_ = r.u8(); r4804_ = r.u8(); r4805_ = r.u8(); r4806_ = r.u8(); r4807_ = r.u8(); r4808_ = r.u8(); r4809_ = r.u8(); r480a_ = r.u8(); r480b_ = r.u8(); r480c_ = r.u8();
  r4810_ = r.u8(); r4811_ = r.u8(); r4812_ = r.u8(); r4813_ = r.u8(); r4814_ = r.u8(); r4815_ = r.u8(); r4816_ = r.u8(); r4817_ = r.u8(); r4818_ = r.u8(); r481x_ = r.u8(); r4814_latch_ = r.b(); r4815_latch_ = r.b();
  r4820_ = r.u8(); r4821_ = r.u8(); r4822_ = r.u8(); r4823_ = r.u8(); r4824_ = r.u8(); r4825_ = r.u8(); r4826_ = r.u8(); r4827_ = r.u8(); r4828_ = r.u8(); r4829_ = r.u8(); r482a_ = r.u8(); r482b_ = r.u8(); r482c_ = r.u8(); r482d_ = r.u8(); r482e_ = r.u8(); r482f_ = r.u8();
  r4830_ = r.u8(); r4831_ = r.u8(); r4832_ = r.u8(); r4833_ = r.u8(); r4834_ = r.u8();
  r4840_ = r.u8(); r4841_ = r.u8(); r4842_ = r.u8(); rtcState_ = RtcState(r.u8()); rtcMode_ = RtcMode(r.u8()); rtcIndex_ = r.u8(); r.raw(rtcRegs_.data(), rtcRegs_.size()); rtcLastSec_ = r.u32();
  dxOffset_ = r.u32(); exOffset_ = r.u32(); fxOffset_ = r.u32();
  dcuMode_ = r.u8(); dcuAddress_ = r.u32(); r.raw(dcuTile_, sizeof(dcuTile_)); dcuOffset_ = r.u8();
  for (auto& row : ctx_) for (auto& c : row) { c.pred = r.u8(); c.swap = r.u8(); }
  bpp_ = r.u32(); offset_ = r.u32(); bits_ = r.u32(); range_ = r.u16(); input_ = r.u16(); output_ = r.u8(); pixels_ = r.u64(); colormap_ = r.u64(); result_ = r.u32();
}

auto Spc7110::setRom(const std::vector<uint8>& rom, MapMode mode) -> void {
  (void)mode;
  romData_ = rom.data();
  romSize_ = rom.size();
  dxOffset_ = dataromAddr(0x100000); exOffset_ = dataromAddr(0x200000); fxOffset_ = dataromAddr(0x300000);
}

}  // namespace snes
