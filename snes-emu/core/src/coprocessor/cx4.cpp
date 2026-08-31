#include "coprocessor/cx4.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace snes {

namespace {

// snes9x C4 sin/cos tables (512 entries, 16-bit signed)
static const int16 kSinTable[512] = {
	     0,    402,    804,   1206,   1607,   2009,   2410,   2811,
	  3211,   3611,   4011,   4409,   4808,   5205,   5602,   5997,
	  6392,   6786,   7179,   7571,   7961,   8351,   8739,   9126,
	  9512,   9896,  10278,  10659,  11039,  11416,  11793,  12167,
	 12539,  12910,  13278,  13645,  14010,  14372,  14732,  15090,
	 15446,  15800,  16151,  16499,  16846,  17189,  17530,  17869,
	 18204,  18537,  18868,  19195,  19519,  19841,  20159,  20475,
	 20787,  21097,  21403,  21706,  22005,  22301,  22594,  22884,
	 23170,  23453,  23732,  24007,  24279,  24547,  24812,  25073,
	 25330,  25583,  25832,  26077,  26319,  26557,  26790,  27020,
	 27245,  27466,  27684,  27897,  28106,  28310,  28511,  28707,
	 28898,  29086,  29269,  29447,  29621,  29791,  29956,  30117,
	 30273,  30425,  30572,  30714,  30852,  30985,  31114,  31237,
	 31357,  31471,  31581,  31685,  31785,  31881,  31971,  32057,
	 32138,  32214,  32285,  32351,  32413,  32469,  32521,  32568,
	 32610,  32647,  32679,  32706,  32728,  32745,  32758,  32765,
	 32767,  32765,  32758,  32745,  32728,  32706,  32679,  32647,
	 32610,  32568,  32521,  32469,  32413,  32351,  32285,  32214,
	 32138,  32057,  31971,  31881,  31785,  31685,  31581,  31471,
	 31357,  31237,  31114,  30985,  30852,  30714,  30572,  30425,
	 30273,  30117,  29956,  29791,  29621,  29447,  29269,  29086,
	 28898,  28707,  28511,  28310,  28106,  27897,  27684,  27466,
	 27245,  27020,  26790,  26557,  26319,  26077,  25832,  25583,
	 25330,  25073,  24812,  24547,  24279,  24007,  23732,  23453,
	 23170,  22884,  22594,  22301,  22005,  21706,  21403,  21097,
	 20787,  20475,  20159,  19841,  19519,  19195,  18868,  18537,
	 18204,  17869,  17530,  17189,  16846,  16499,  16151,  15800,
	 15446,  15090,  14732,  14372,  14010,  13645,  13278,  12910,
	 12539,  12167,  11793,  11416,  11039,  10659,  10278,   9896,
	  9512,   9126,   8739,   8351,   7961,   7571,   7179,   6786,
	  6392,   5997,   5602,   5205,   4808,   4409,   4011,   3611,
	  3211,   2811,   2410,   2009,   1607,   1206,    804,    402,
	     0,   -402,   -804,  -1206,  -1607,  -2009,  -2410,  -2811,
	 -3211,  -3611,  -4011,  -4409,  -4808,  -5205,  -5602,  -5997,
	 -6392,  -6786,  -7179,  -7571,  -7961,  -8351,  -8739,  -9126,
	 -9512,  -9896, -10278, -10659, -11039, -11416, -11793, -12167,
	-12539, -12910, -13278, -13645, -14010, -14372, -14732, -15090,
	-15446, -15800, -16151, -16499, -16846, -17189, -17530, -17869,
	-18204, -18537, -18868, -19195, -19519, -19841, -20159, -20475,
	-20787, -21097, -21403, -21706, -22005, -22301, -22594, -22884,
	-23170, -23453, -23732, -24007, -24279, -24547, -24812, -25073,
	-25330, -25583, -25832, -26077, -26319, -26557, -26790, -27020,
	-27245, -27466, -27684, -27897, -28106, -28310, -28511, -28707,
	-28898, -29086, -29269, -29447, -29621, -29791, -29956, -30117,
	-30273, -30425, -30572, -30714, -30852, -30985, -31114, -31237,
	-31357, -31471, -31581, -31685, -31785, -31881, -31971, -32057,
	-32138, -32214, -32285, -32351, -32413, -32469, -32521, -32568,
	-32610, -32647, -32679, -32706, -32728, -32745, -32758, -32765,
	-32767, -32765, -32758, -32745, -32728, -32706, -32679, -32647,
	-32610, -32568, -32521, -32469, -32413, -32351, -32285, -32214,
	-32138, -32057, -31971, -31881, -31785, -31685, -31581, -31471,
	-31357, -31237, -31114, -30985, -30852, -30714, -30572, -30425,
	-30273, -30117, -29956, -29791, -29621, -29447, -29269, -29086,
	-28898, -28707, -28511, -28310, -28106, -27897, -27684, -27466,
	-27245, -27020, -26790, -26557, -26319, -26077, -25832, -25583,
	-25330, -25073, -24812, -24547, -24279, -24007, -23732, -23453,
	-23170, -22884, -22594, -22301, -22005, -21706, -21403, -21097,
	-20787, -20475, -20159, -19841, -19519, -19195, -18868, -18537,
	-18204, -17869, -17530, -17189, -16846, -16499, -16151, -15800,
	-15446, -15090, -14732, -14372, -14010, -13645, -13278, -12910,
	-12539, -12167, -11793, -11416, -11039, -10659, -10278,  -9896,
	 -9512,  -9126,  -8739,  -8351,  -7961,  -7571,  -7179,  -6786,
	 -6392,  -5997,  -5602,  -5205,  -4808,  -4409,  -4011,  -3611,
	 -3211,  -2811,  -2410,  -2009,  -1607,  -1206,   -804,   -402
};

static const int16 kCosTable[512] = {
	 32767,  32765,  32758,  32745,  32728,  32706,  32679,  32647,
	 32610,  32568,  32521,  32469,  32413,  32351,  32285,  32214,
	 32138,  32057,  31971,  31881,  31785,  31685,  31581,  31471,
	 31357,  31237,  31114,  30985,  30852,  30714,  30572,  30425,
	 30273,  30117,  29956,  29791,  29621,  29447,  29269,  29086,
	 28898,  28707,  28511,  28310,  28106,  27897,  27684,  27466,
	 27245,  27020,  26790,  26557,  26319,  26077,  25832,  25583,
	 25330,  25073,  24812,  24547,  24279,  24007,  23732,  23453,
	 23170,  22884,  22594,  22301,  22005,  21706,  21403,  21097,
	 20787,  20475,  20159,  19841,  19519,  19195,  18868,  18537,
	 18204,  17869,  17530,  17189,  16846,  16499,  16151,  15800,
	 15446,  15090,  14732,  14372,  14010,  13645,  13278,  12910,
	 12539,  12167,  11793,  11416,  11039,  10659,  10278,   9896,
	  9512,   9126,   8739,   8351,   7961,   7571,   7179,   6786,
	  6392,   5997,   5602,   5205,   4808,   4409,   4011,   3611,
	  3211,   2811,   2410,   2009,   1607,   1206,    804,    402,
	     0,   -402,   -804,  -1206,  -1607,  -2009,  -2410,  -2811,
	 -3211,  -3611,  -4011,  -4409,  -4808,  -5205,  -5602,  -5997,
	 -6392,  -6786,  -7179,  -7571,  -7961,  -8351,  -8739,  -9126,
	 -9512,  -9896, -10278, -10659, -11039, -11416, -11793, -12167,
	-12539, -12910, -13278, -13645, -14010, -14372, -14732, -15090,
	-15446, -15800, -16151, -16499, -16846, -17189, -17530, -17869,
	-18204, -18537, -18868, -19195, -19519, -19841, -20159, -20475,
	-20787, -21097, -21403, -21706, -22005, -22301, -22594, -22884,
	-23170, -23453, -23732, -24007, -24279, -24547, -24812, -25073,
	-25330, -25583, -25832, -26077, -26319, -26557, -26790, -27020,
	-27245, -27466, -27684, -27897, -28106, -28310, -28511, -28707,
	-28898, -29086, -29269, -29447, -29621, -29791, -29956, -30117,
	-30273, -30425, -30572, -30714, -30852, -30985, -31114, -31237,
	-31357, -31471, -31581, -31685, -31785, -31881, -31971, -32057,
	-32138, -32214, -32285, -32351, -32413, -32469, -32521, -32568,
	-32610, -32647, -32679, -32706, -32728, -32745, -32758, -32765,
	-32767, -32765, -32758, -32745, -32728, -32706, -32679, -32647,
	-32610, -32568, -32521, -32469, -32413, -32351, -32285, -32214,
	-32138, -32057, -31971, -31881, -31785, -31685, -31581, -31471,
	-31357, -31237, -31114, -30985, -30852, -30714, -30572, -30425,
	-30273, -30117, -29956, -29791, -29621, -29447, -29269, -29086,
	-28898, -28707, -28511, -28310, -28106, -27897, -27684, -27466,
	-27245, -27020, -26790, -26557, -26319, -26077, -25832, -25583,
	-25330, -25073, -24812, -24547, -24279, -24007, -23732, -23453,
	-23170, -22884, -22594, -22301, -22005, -21706, -21403, -21097,
	-20787, -20475, -20159, -19841, -19519, -19195, -18868, -18537,
	-18204, -17869, -17530, -17189, -16846, -16499, -16151, -15800,
	-15446, -15090, -14732, -14372, -14010, -13645, -13278, -12910,
	-12539, -12167, -11793, -11416, -11039, -10659, -10278,  -9896,
	 -9512,  -9126,  -8739,  -8351,  -7961,  -7571,  -7179,  -6786,
	 -6392,  -5997,  -5602,  -5205,  -4808,  -4409,  -4011,  -3611,
	 -3211,  -2811,  -2410,  -2009,  -1607,  -1206,   -804,   -402,
	     0,    402,    804,   1206,   1607,   2009,   2410,   2811,
	  3211,   3611,   4011,   4409,   4808,   5205,   5602,   5997,
	  6392,   6786,   7179,   7571,   7961,   8351,   8739,   9126,
	  9512,   9896,  10278,  10659,  11039,  11416,  11793,  12167,
	 12539,  12910,  13278,  13645,  14010,  14372,  14732,  15090,
	 15446,  15800,  16151,  16499,  16846,  17189,  17530,  17869,
	 18204,  18537,  18868,  19195,  19519,  19841,  20159,  20475,
	 20787,  21097,  21403,  21706,  22005,  22301,  22594,  22884,
	 23170,  23453,  23732,  24007,  24279,  24547,  24812,  25073,
	 25330,  25583,  25832,  26077,  26319,  26557,  26790,  27020,
	 27245,  27466,  27684,  27897,  28106,  28310,  28511,  28707,
	 28898,  29086,  29269,  29447,  29621,  29791,  29956,  30117,
	 30273,  30425,  30572,  30714,  30852,  30985,  31114,  31237,
	 31357,  31471,  31581,  31685,  31785,  31881,  31971,  32057,
	 32138,  32214,  32285,  32351,  32413,  32469,  32521,  32568,
	 32610,  32647,  32679,  32706,  32728,  32745,  32758,  32765
};

static const uint8 kTestPattern[48] = {
	0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
	0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x80, 0xff, 0xff, 0x7f,
	0x00, 0x80, 0x00, 0xff, 0x7f, 0x00, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0xff,
	0x00, 0x00, 0x01, 0xff, 0xff, 0xfe, 0x00, 0x01, 0x00, 0xff, 0xfe, 0x00
};

inline int32 sar32(int32 v, int n) { return v >> n; }

constexpr double kPi = 3.14159265358979323846;

}  // namespace

Cx4::Cx4() { power(); }

auto Cx4::handles(uint24 address) const -> bool {
  uint32 bank = address >> 16;
  if (bank > 0x3F && bank < 0x80) return false;
  if (bank >= 0x80) bank -= 0x80;
  if (bank > 0x3F) return false;
  uint32 offs = address & 0xFFFF;
  return offs >= 0x6000 && offs <= 0x7FFF;
}

auto Cx4::ramReadWord(uint16 off) const -> uint16 {
  return uint16(ram_[off]) | (uint16(ram_[off + 1]) << 8);
}
auto Cx4::ramRead3Word(uint16 off) const -> uint32 {
  return uint32(ram_[off]) | (uint32(ram_[off + 1]) << 8) | (uint32(ram_[off + 2]) << 16);
}
void Cx4::ramWriteWord(uint16 off, uint16 v) {
  ram_[off] = uint8(v);
  ram_[off + 1] = uint8(v >> 8);
}
void Cx4::ramWrite3Word(uint16 off, uint32 v) {
  ram_[off] = uint8(v);
  ram_[off + 1] = uint8(v >> 8);
  ram_[off + 2] = uint8(v >> 16);
}

auto Cx4::getRomPointer(uint32 snesAddr) const -> const uint8* {
  if (!romData_ || romSize_ == 0) return nullptr;
  // LoROM/HiROM translation — reuse the same logic as Cartridge::offsetInFile
  uint32 bank = snesAddr >> 16;
  uint32 offs = snesAddr & 0xFFFF;
  uint32 fileOff = uint32(-1);
  switch (romMode_) {
    case MapMode::lorom:
      if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
        if (offs >= 0x8000) fileOff = ((bank & 0x3F) << 15) | (offs & 0x7FFF);
      } else if ((bank >= 0x40 && bank <= 0x7D) || (bank >= 0xC0 && bank <= 0xFF)) {
        fileOff = ((bank & 0x3F) << 16) | offs;
      }
      break;
    case MapMode::hirom:
      if (bank == 0x7E || bank == 0x7F) break;
      if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
        if (offs >= 0x8000) fileOff = ((bank & 0x3F) << 16) | offs;
      } else if (bank >= 0x40 && bank <= 0x7D) fileOff = ((bank & 0x3F) << 16) | offs;
      else if (bank >= 0xC0) fileOff = ((bank & 0x3F) << 16) | offs;
      break;
    case MapMode::exhirom:
      if (bank >= 0x40 && bank <= 0x7D) fileOff = ((bank & 0x3F) << 16) | offs;
      else if (bank >= 0xC0) fileOff = 0x400000 + ((bank - 0xC0) << 16) | offs;
      else if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) fileOff = ((bank & 0x3F) << 16) | (offs & 0x7FFF);
      break;
    default: break;
  }
  if (fileOff == uint32(-1) || fileOff >= romSize_) return nullptr;
  if ((romSize_ & (romSize_ - 1)) == 0) fileOff &= romSize_ - 1;
  return romData_ + fileOff;
}

void Cx4::doDma() {
  uint32 src = ramRead3Word(0x1F40);
  uint16 len16 = ramReadWord(0x1F43);
  uint32 len = len16 == 0 ? 0x10000 : len16;
  uint16 dst = ramReadWord(0x1F45);
  uint16 dstOff = dst - 0x6000;
  if (dstOff >= 0x2000) return;
  if (dstOff + len > 0x2000) len = 0x2000 - dstOff;
  const uint8* srcPtr = getRomPointer(src);
  if (srcPtr) {
    // copy byte by byte to handle wrap
    for (uint16 i = 0; i < len; i++) {
      if (dstOff + i < ram_.size()) ram_[dstOff + i] = srcPtr[i];
    }
  } else {
    // no ROM source — zero fill (keeps games from reading garbage)
    std::memset(ram_.data() + dstOff, 0, len);
  }
}

// ---- c4.cpp helpers ----

void Cx4::transfWireFrame() {
  c4x_ = double(wfxVal_);
  c4y_ = double(wfyVal_);
  c4z_ = double(wfzVal_) - 0x95;
  tanVal_ = -double(wfx2Val_) * kPi * 2 / 128;
  c4y2_ = c4y_ * std::cos(tanVal_) - c4z_ * std::sin(tanVal_);
  c4z2_ = c4y_ * std::sin(tanVal_) + c4z_ * std::cos(tanVal_);
  tanVal_ = -double(wfy2Val_) * kPi * 2 / 128;
  c4x2_ = c4x_ * std::cos(tanVal_) + c4z2_ * std::sin(tanVal_);
  c4z_ = c4x_ * -std::sin(tanVal_) + c4z2_ * std::cos(tanVal_);
  tanVal_ = -double(wfDist_) * kPi * 2 / 128;
  c4x_ = c4x2_ * std::cos(tanVal_) - c4y2_ * std::sin(tanVal_);
  c4y_ = c4x2_ * std::sin(tanVal_) + c4y2_ * std::cos(tanVal_);
  wfxVal_ = int16(c4x_ * double(wfScale_) / (0x90 * (c4z_ + 0x95)) * 0x95);
  wfyVal_ = int16(c4y_ * double(wfScale_) / (0x90 * (c4z_ + 0x95)) * 0x95);
}
void Cx4::transfWireFrame2() {
  c4x_ = double(wfxVal_);
  c4y_ = double(wfyVal_);
  c4z_ = double(wfzVal_);
  tanVal_ = -double(wfx2Val_) * kPi * 2 / 128;
  c4y2_ = c4y_ * std::cos(tanVal_) - c4z_ * std::sin(tanVal_);
  c4z2_ = c4y_ * std::sin(tanVal_) + c4z_ * std::cos(tanVal_);
  tanVal_ = -double(wfy2Val_) * kPi * 2 / 128;
  c4x2_ = c4x_ * std::cos(tanVal_) + c4z2_ * std::sin(tanVal_);
  c4z_ = c4x_ * -std::sin(tanVal_) + c4z2_ * std::cos(tanVal_);
  tanVal_ = -double(wfDist_) * kPi * 2 / 128;
  c4x_ = c4x2_ * std::cos(tanVal_) - c4y2_ * std::sin(tanVal_);
  c4y_ = c4x2_ * std::sin(tanVal_) + c4y2_ * std::cos(tanVal_);
  wfxVal_ = int16(c4x_ * double(wfScale_) / 0x100);
  wfyVal_ = int16(c4y_ * double(wfScale_) / 0x100);
}
void Cx4::calcWireFrame() {
  wfxVal_ = int16(wfx2Val_ - wfxVal_);
  wfyVal_ = int16(wfy2Val_ - wfyVal_);
  if (std::abs(wfxVal_) > std::abs(wfyVal_)) {
    wfDist_ = int16(std::abs(wfxVal_) + 1);
    wfyVal_ = int16(256 * double(wfyVal_) / std::abs(wfxVal_));
    wfxVal_ = wfxVal_ < 0 ? -256 : 256;
  } else {
    if (wfyVal_ != 0) {
      wfDist_ = int16(std::abs(wfyVal_) + 1);
      wfxVal_ = int16(256 * double(wfxVal_) / std::abs(wfyVal_));
      wfyVal_ = wfyVal_ < 0 ? -256 : 256;
    } else wfDist_ = 0;
  }
}
void Cx4::op1F() {
  if (f41FXVal_ == 0) {
    f41FAngleRes_ = f41FYVal_ > 0 ? 0x80 : 0x180;
  } else {
    tanVal_ = double(f41FYVal_) / double(f41FXVal_);
    f41FAngleRes_ = int16(std::atan(tanVal_) / (kPi * 2) * 512);
    if (f41FXVal_ < 0) f41FAngleRes_ += 0x100;
    f41FAngleRes_ &= 0x1FF;
  }
}
void Cx4::op15() {
  tanVal_ = std::sqrt(double(f41FYVal_) * f41FYVal_ + double(f41FXVal_) * f41FXVal_);
  f41FDist_ = int16(tanVal_);
}
void Cx4::op0D() {
  tanVal_ = std::sqrt(double(f41FYVal_) * f41FYVal_ + double(f41FXVal_) * f41FXVal_);
  tanVal_ = double(f41FDistVal_) / tanVal_;
  f41FYVal_ = int16(f41FYVal_ * tanVal_ * 0.99);
  f41FXVal_ = int16(f41FXVal_ * tanVal_ * 0.98);
}

// ---- c4emu helpers ----

void Cx4::convOam() {
  uint8* oamPtr = ram_.data() + (ram_[0x626] << 2);
  for (uint8* i = ram_.data() + 0x1FD; i > oamPtr; i -= 4) *i = 0xE0;
  uint8* oamPtr2 = ram_.data() + 0x200 + (ram_[0x626] >> 2);
  uint16 globalX = ramReadWord(0x621);
  uint16 globalY = ramReadWord(0x623);
  if (ram_[0x620] != 0) {
    uint8 sprCount = 128 - ram_[0x626];
    uint8 offset = (ram_[0x626] & 3) * 2;
    uint8* srcPtr = ram_.data() + 0x220;
    for (int i = ram_[0x620]; i > 0 && sprCount > 0; i--, srcPtr += 16) {
      int16 sprX = int16(ramReadWord(srcPtr - ram_.data()));
      sprX -= int16(globalX);
      int16 sprY = int16(ramReadWord(srcPtr - ram_.data() + 2));
      sprY -= int16(globalY);
      uint8 sprName = srcPtr[5];
      uint8 sprAttr = srcPtr[4] | srcPtr[6];
      uint32 addr = uint32(srcPtr[7]) | (uint32(srcPtr[8]) << 8) | (uint32(srcPtr[9]) << 16);
      const uint8* sprData = getRomPointer(addr);
      // fallback to ram if no ROM
      if (!sprData) sprData = ram_.data() + 0x600; // dummy
      if (sprData && *sprData != 0) {
        int16 X, Y;
        for (int cnt = sprData[0]; cnt > 0 && sprCount > 0; cnt--) {
          // simplified: use sprData offset walking (port faithfully but compact)
          // original loops over sprptr[1..3] — we keep the same bounds checks
          sprData += 4;
          X = int8(sprData[1]);
          if (sprAttr & 0x40) X = -X - ((sprData[0] & 0x20) ? 16 : 8);
          X += sprX;
          if (X < -16 || X > 272) continue;
          Y = int8(sprData[2]);
          if (sprAttr & 0x80) Y = -Y - ((sprData[0] & 0x20) ? 16 : 8);
          Y += sprY;
          if (Y < -16 || Y > 224) continue;
          oamPtr[0] = uint8(X);
          oamPtr[1] = uint8(Y);
          oamPtr[2] = sprName + sprData[3];
          oamPtr[3] = sprAttr ^ (sprData[0] & 0xC0);
          *oamPtr2 &= ~(3 << offset);
          if (X & 0x100) *oamPtr2 |= 1 << offset;
          if (sprData[0] & 0x20) *oamPtr2 |= 2 << offset;
          oamPtr += 4;
          sprCount--;
          offset = (offset + 2) & 6;
          if (offset == 0) oamPtr2++;
        }
      } else if (sprCount > 0) {
        oamPtr[0] = uint8(sprX);
        oamPtr[1] = uint8(sprY);
        oamPtr[2] = sprName;
        oamPtr[3] = sprAttr;
        *oamPtr2 &= ~(3 << offset);
        if (sprX & 0x100) *oamPtr2 |= 3 << offset;
        else *oamPtr2 |= 2 << offset;
        oamPtr += 4;
        sprCount--;
        offset = (offset + 2) & 6;
        if (offset == 0) oamPtr2++;
      }
    }
  }
}

void Cx4::doScaleRotate(int rowPadding) {
  int16 A, B, C, D;
  int32 xScale = int16(ramReadWord(0x1F8F));
  if (xScale & 0x8000) xScale = 0x7FFF;
  int32 yScale = int16(ramReadWord(0x1F92));
  if (yScale & 0x8000) yScale = 0x7FFF;
  uint16 ang = ramReadWord(0x1F80) & 0x1FF;
  if (ang == 0) { A = int16(xScale); B = 0; C = 0; D = int16(yScale); }
  else if (ang == 128) { A = 0; B = int16(-yScale); C = int16(xScale); D = 0; }
  else if (ang == 256) { A = int16(-xScale); B = 0; C = 0; D = int16(-yScale); }
  else if (ang == 384) { A = 0; B = int16(yScale); C = int16(-xScale); D = 0; }
  else {
    A = int16(sar32(int32(kCosTable[ang]) * xScale, 15));
    B = int16(-sar32(int32(kSinTable[ang]) * yScale, 15));
    C = int16(sar32(int32(kSinTable[ang]) * xScale, 15));
    D = int16(sar32(int32(kCosTable[ang]) * yScale, 15));
  }
  uint8 w = ram_[0x1F89] & ~7;
  uint8 h = ram_[0x1F8C] & ~7;
  std::memset(ram_.data(), 0, (w + rowPadding / 4) * h / 2);
  int32 cx = int16(ramReadWord(0x1F83));
  int32 cy = int16(ramReadWord(0x1F86));
  int32 lineX = (cx << 12) - cx * A - cx * B;
  int32 lineY = (cy << 12) - cy * C - cy * D;
  uint32 X, Y;
  uint8 byte;
  int outIdx = 0;
  uint8 bit = 0x80;
  for (int y = 0; y < h; y++) {
    X = lineX; Y = lineY;
    for (int x = 0; x < w; x++) {
      if ((X >> 12) >= w || (Y >> 12) >= h) byte = 0;
      else {
        uint32 addr = (Y >> 12) * w + (X >> 12);
        byte = ram_[0x600 + (addr >> 1)];
        if (addr & 1) byte >>= 4;
      }
      if (byte & 1) ram_[outIdx] |= bit;
      if (byte & 2) ram_[outIdx + 1] |= bit;
      if (byte & 4) ram_[outIdx + 16] |= bit;
      if (byte & 8) ram_[outIdx + 17] |= bit;
      bit >>= 1;
      if (bit == 0) { bit = 0x80; outIdx += 32; }
      X += A; Y += C;
    }
    outIdx += 2 + rowPadding;
    if (outIdx & 0x10) outIdx &= ~0x10;
    else outIdx -= w * 4 + rowPadding;
    lineX += B; lineY += D;
  }
}

void Cx4::drawLine(int32 X1, int32 Y1, int16 Z1, int32 X2, int32 Y2, int16 Z2, uint8 color) {
  wfxVal_ = int16(X1); wfyVal_ = int16(Y1); wfzVal_ = Z1;
  wfScale_ = int16(ram_[0x1F90]); wfx2Val_ = int16(ram_[0x1F86]); wfy2Val_ = int16(ram_[0x1F87]); wfDist_ = int16(ram_[0x1F88]);
  transfWireFrame2();
  X1 = (wfxVal_ + 48) << 8; Y1 = (wfyVal_ + 48) << 8;
  wfxVal_ = int16(X2); wfyVal_ = int16(Y2); wfzVal_ = Z2;
  transfWireFrame2();
  X2 = (wfxVal_ + 48) << 8; Y2 = (wfyVal_ + 48) << 8;
  wfxVal_ = int16(X1 >> 8); wfyVal_ = int16(Y1 >> 8);
  wfx2Val_ = int16(X2 >> 8); wfy2Val_ = int16(Y2 >> 8);
  calcWireFrame();
  X2 = wfxVal_; Y2 = wfyVal_;
  for (int i = wfDist_ ? wfDist_ : 1; i > 0; i--) {
    if (X1 > 0xFF && Y1 > 0xFF && X1 < 0x6000 && Y1 < 0x6000) {
      uint16 addr = (((Y1 >> 8) >> 3) << 8) - (((Y1 >> 8) >> 3) << 6) + (((X1 >> 8) >> 3) << 4) + ((Y1 >> 8) & 7) * 2;
      uint8 bit = 0x80 >> ((X1 >> 8) & 7);
      if (addr + 0x301 < ram_.size()) {
        ram_[addr + 0x300] &= ~bit;
        ram_[addr + 0x301] &= ~bit;
        if (color & 1) ram_[addr + 0x300] |= bit;
        if (color & 2) ram_[addr + 0x301] |= bit;
      }
    }
    X1 += X2; Y1 += Y2;
  }
}

void Cx4::drawWireFrame() {
  uint32 base = ramRead3Word(0x1F80);
  const uint8* romBase = getRomPointer(base);
  // fallback to ram pointer if ROM not mapped (for test)
  uint8 lineAddr = ram_[0x295];
  // line data is at base in ROM/ram — try ROM first
  for (int i = lineAddr; i > 0; i--) {
    // snes9x reads 5 bytes per edge from getMemPointer(base)
    // For our port we read from ram if base is in 6000 range else ROM
    uint32 edgeAddr = base + (lineAddr - i) * 5;
    const uint8* edge = getRomPointer(edgeAddr);
    if (!edge) edge = ram_.data() + (edgeAddr & 0x1FFF);
    uint8 a0 = edge[0], a1 = edge[1], a2 = edge[2], a3 = edge[3], col = edge[4];
    uint16 p1off = (uint16(a0) << 8) | a1;
    uint16 p2off = (uint16(a2) << 8) | a3;
    uint32 p1addr = (uint32(ram_[0x1F82]) << 16) | p1off;
    uint32 p2addr = (uint32(ram_[0x1F82]) << 16) | p2off;
    const uint8* p1 = getRomPointer(p1addr);
    const uint8* p2 = getRomPointer(p2addr);
    if (!p1) p1 = ram_.data() + (p1off & 0x1FFF);
    if (!p2) p2 = ram_.data() + (p2off & 0x1FFF);
    int16 X1 = int16(p1[0] << 8 | p1[1]);
    int16 Y1 = int16(p1[2] << 8 | p1[3]);
    int16 Z1 = int16(p1[4] << 8 | p1[5]);
    int16 X2 = int16(p2[0] << 8 | p2[1]);
    int16 Y2 = int16(p2[2] << 8 | p2[3]);
    int16 Z2 = int16(p2[4] << 8 | p2[5]);
    drawLine(X1, Y1, Z1, X2, Y2, Z2, col);
    (void)romBase;
  }
  // keep compatibility with snes9x: if lineAddr==0, nothing drawn (handled)
}

void Cx4::transformLines() {
  wfx2Val_ = int16(ram_[0x1F83]); wfy2Val_ = int16(ram_[0x1F86]); wfDist_ = int16(ram_[0x1F89]); wfScale_ = int16(ram_[0x1F8C]);
  uint8* ptr = ram_.data();
  for (int i = int16(ramReadWord(0x1F80)); i > 0; i--, ptr += 0x10) {
    wfxVal_ = int16(ramReadWord(ptr - ram_.data() + 1));
    wfyVal_ = int16(ramReadWord(ptr - ram_.data() + 5));
    wfzVal_ = int16(ramReadWord(ptr - ram_.data() + 9));
    transfWireFrame();
    ramWriteWord(ptr - ram_.data() + 1, uint16(wfxVal_ + 0x80));
    ramWriteWord(ptr - ram_.data() + 5, uint16(wfyVal_ + 0x50));
  }
  ramWriteWord(0x600, 23); ramWriteWord(0x602, 0x60); ramWriteWord(0x605, 0x40);
  ramWriteWord(0x608, 23); ramWriteWord(0x60A, 0x60); ramWriteWord(0x60D, 0x40);
}

void Cx4::bitPlaneWave() {
  static const uint16 bmpdata[40] = {
	0x0000,0x0002,0x0004,0x0006,0x0008,0x000A,0x000C,0x000E,
	0x0200,0x0202,0x0204,0x0206,0x0208,0x020A,0x020C,0x020E,
	0x0400,0x0402,0x0404,0x0406,0x0408,0x040A,0x040C,0x040E,
	0x0600,0x0602,0x0604,0x0606,0x0608,0x060A,0x060C,0x060E,
	0x0800,0x0802,0x0804,0x0806,0x0808,0x080A,0x080C,0x080E
  };
  uint8* dst = ram_.data();
  uint32 waveptr = ram_[0x1F83];
  uint16 mask1 = 0xC0C0, mask2 = 0x3F3F;
  for (int j = 0; j < 0x10; j++) {
    do {
      int16 h = -int8(ram_[ (waveptr + 0xB00) & 0x1FFF]) - 16;
      for (int i = 0; i < 40; i++) {
        uint16 tmp = ramReadWord(dst - ram_.data() + bmpdata[i]) & mask2;
        if (h >= 0) {
          if (h < 8) tmp |= mask1 & ramReadWord(0xA00 + h * 2);
          else tmp |= mask1 & 0xFF00;
        }
        ramWriteWord(dst - ram_.data() + bmpdata[i], tmp);
        h++;
      }
      waveptr = (waveptr + 1) & 0x7F;
      mask1 = (mask1 >> 2) | (mask1 << 6);
      mask2 = (mask2 >> 2) | (mask2 << 6);
    } while (mask1 != 0xC0C0);
    dst += 16;
    do {
      int16 h = -int8(ram_[(waveptr + 0xB00) & 0x1FFF]) - 16;
      for (int i = 0; i < 40; i++) {
        uint16 tmp = ramReadWord(dst - ram_.data() + bmpdata[i]) & mask2;
        if (h >= 0) {
          if (h < 8) tmp |= mask1 & ramReadWord(0xA10 + h * 2);
          else tmp |= mask1 & 0xFF00;
        }
        ramWriteWord(dst - ram_.data() + bmpdata[i], tmp);
        h++;
      }
      waveptr = (waveptr + 1) & 0x7F;
      mask1 = (mask1 >> 2) | (mask1 << 6);
      mask2 = (mask2 >> 2) | (mask2 << 6);
    } while (mask1 != 0xC0C0);
    dst += 16;
  }
}

void Cx4::sprDisintegrate() {
  uint8 w = ram_[0x1F89], h = ram_[0x1F8C];
  int16 cx = int16(ramReadWord(0x1F80)), cy = int16(ramReadWord(0x1F83));
  int32 scaleX = int16(ramReadWord(0x1F86)), scaleY = int16(ramReadWord(0x1F8F));
  int32 startX = -cx * scaleX + (cx << 8);
  int32 startY = -cy * scaleY + (cy << 8);
  uint8* src = ram_.data() + 0x600;
  std::memset(ram_.data(), 0, w * h / 2);
  for (uint32 y = startY, i = 0; i < h; i++, y += scaleY) {
    for (uint32 x = startX, j = 0; j < w; j++, x += scaleX) {
      if ((x >> 8) < w && (y >> 8) < h) {
        uint32 srcOff = (y >> 8) * w + (x >> 8);
        uint8 pix = (j & 1) ? (src[srcOff >> 1] >> 4) : (src[srcOff >> 1] & 0xF);
        int idx = (y >> 11) * w * 4 + (x >> 11) * 32 + ((y >> 8) & 7) * 2;
        uint8 mask = 0x80 >> ((x >> 8) & 7);
        if (idx >= 0 && idx + 17 < int(ram_.size())) {
          if (pix & 1) ram_[idx] |= mask;
          if (pix & 2) ram_[idx + 1] |= mask;
          if (pix & 4) ram_[idx + 16] |= mask;
          if (pix & 8) ram_[idx + 17] |= mask;
        }
      }
      if (j & 1) src++;
    }
  }
}

void Cx4::processSprites() {
  switch (ram_[0x1F4D]) {
    case 0x00: convOam(); break;
    case 0x03: doScaleRotate(0); break;
    case 0x05: transformLines(); break;
    case 0x07: doScaleRotate(64); break;
    case 0x08: drawWireFrame(); break;
    case 0x0B: sprDisintegrate(); break;
    case 0x0C: bitPlaneWave(); break;
    default: break;
  }
}

void Cx4::execCommand(uint8 cmd) {
  switch (cmd) {
    case 0x00: processSprites(); break;
    case 0x01: std::memset(ram_.data() + 0x300, 0, 16 * 12 * 3 * 4); drawWireFrame(); break;
    case 0x05: {
      int32 tmp = 0x10000;
      if (ramReadWord(0x1F83)) tmp = sar32((tmp / int16(ramReadWord(0x1F83))) * int16(ramReadWord(0x1F81)), 8);
      ramWriteWord(0x1F80, uint16(tmp));
      break;
    }
    case 0x0D: {
      f41FXVal_ = int16(ramReadWord(0x1F80));
      f41FYVal_ = int16(ramReadWord(0x1F83));
      f41FDistVal_ = int16(ramReadWord(0x1F86));
      op0D();
      ramWriteWord(0x1F89, uint16(f41FXVal_));
      ramWriteWord(0x1F8C, uint16(f41FYVal_));
      break;
    }
    case 0x10: {
      int32 r1 = int16(ramReadWord(0x1F83));
      if (r1 & 0x8000) r1 |= ~0x7FFF; else r1 &= 0x7FFF;
      int32 tmp = sar32(r1 * kCosTable[ramReadWord(0x1F80) & 0x1FF] * 2, 16);
      ramWrite3Word(0x1F86, uint32(tmp));
      tmp = sar32(r1 * kSinTable[ramReadWord(0x1F80) & 0x1FF] * 2, 16);
      tmp = tmp - sar32(tmp, 6);
      ramWrite3Word(0x1F89, uint32(tmp));
      break;
    }
    case 0x13: {
      int32 tmp = sar32(int32(int16(ramReadWord(0x1F83))) * kCosTable[ramReadWord(0x1F80) & 0x1FF] * 2, 8);
      ramWrite3Word(0x1F86, uint32(tmp));
      tmp = sar32(int32(int16(ramReadWord(0x1F83))) * kSinTable[ramReadWord(0x1F80) & 0x1FF] * 2, 8);
      ramWrite3Word(0x1F89, uint32(tmp));
      break;
    }
    case 0x15: {
      f41FXVal_ = int16(ramReadWord(0x1F80));
      f41FYVal_ = int16(ramReadWord(0x1F83));
      op15();
      ramWriteWord(0x1F80, uint16(f41FDist_));
      break;
    }
    case 0x1F: {
      f41FXVal_ = int16(ramReadWord(0x1F80));
      f41FYVal_ = int16(ramReadWord(0x1F83));
      op1F();
      ramWriteWord(0x1F86, uint16(f41FAngleRes_));
      break;
    }
    case 0x22: {
      int16 ang1 = int16(ramReadWord(0x1F8C)) & 0x1FF;
      int16 ang2 = int16(ramReadWord(0x1F8F)) & 0x1FF;
      int32 tan1 = kCosTable[ang1] ? (int32(kSinTable[ang1]) << 16) / kCosTable[ang1] : 0x80000000;
      int32 tan2 = kCosTable[ang2] ? (int32(kSinTable[ang2]) << 16) / kCosTable[ang2] : 0x80000000;
      int16 y = int16(ramReadWord(0x1F83)) - int16(ramReadWord(0x1F89));
      for (int j = 0; j < 225; j++) {
        int16 left, right;
        if (y >= 0) {
          left = sar32(tan1 * y, 16) - int16(ramReadWord(0x1F80)) + int16(ramReadWord(0x1F86));
          right = sar32(tan2 * y, 16) - int16(ramReadWord(0x1F80)) + int16(ramReadWord(0x1F86)) + int16(ramReadWord(0x1F93));
          if (left < 0 && right < 0) { left = 1; right = 0; }
          else if (left < 0) left = 0;
          else if (right < 0) right = 0;
          if (left > 255 && right > 255) { left = 255; right = 254; }
          else if (left > 255) left = 255;
          else if (right > 255) right = 255;
        } else { left = 1; right = 0; }
        ram_[j + 0x800] = uint8(left);
        ram_[j + 0x900] = uint8(right);
        y++;
      }
      break;
    }
    case 0x25: {
      int32 foo = int32(ramRead3Word(0x1F80));
      int32 bar = int32(ramRead3Word(0x1F83));
      // sign extend 24-bit
      if (foo & 0x800000) foo |= ~0xFFFFFF;
      if (bar & 0x800000) bar |= ~0xFFFFFF;
      foo *= bar;
      ramWrite3Word(0x1F80, uint32(foo));
      break;
    }
    case 0x2D: {
      wfxVal_ = int16(ramReadWord(0x1F81));
      wfyVal_ = int16(ramReadWord(0x1F84));
      wfzVal_ = int16(ramReadWord(0x1F87));
      wfx2Val_ = int16(ram_[0x1F89]);
      wfy2Val_ = int16(ram_[0x1F8A]);
      wfDist_ = int16(ram_[0x1F8B]);
      wfScale_ = int16(ramReadWord(0x1F90));
      transfWireFrame2();
      ramWriteWord(0x1F80, uint16(wfxVal_));
      ramWriteWord(0x1F83, uint16(wfyVal_));
      break;
    }
    case 0x40: {
      uint16 sum = 0;
      for (int i = 0; i < 0x800; i++) sum += ram_[i];
      ramWriteWord(0x1F80, sum);
      break;
    }
    case 0x54: {
      int64 a = int64(ramRead3Word(0x1F80));
      if (a & 0x800000) a |= 0xFFFFFFFFFF000000LL;
      a *= a;
      ramWrite3Word(0x1F83, uint32(a));
      ramWrite3Word(0x1F86, uint32(a >> 24));
      break;
    }
    case 0x5C: {
      for (int i = 0; i < 48; i++) ram_[i] = kTestPattern[i];
      break;
    }
    case 0x89: {
      ram_[0x1F80] = 0x36; ram_[0x1F81] = 0x43; ram_[0x1F82] = 0x05;
      break;
    }
    default: break;
  }
}

// ---- Coprocessor interface ----

auto Cx4::read(uint24 address) -> uint8 {
  uint16 a = address & 0xFFFF;
  if (a == 0x7F5E) return 0x00; // not busy (snes9x always 0)
  if (a < 0x6000 || a > 0x7FFF) return 0xFF;
  return ram_[a - 0x6000];
}

auto Cx4::write(uint24 address, uint8 data) -> void {
  uint16 a = address & 0xFFFF;
  if (a < 0x6000 || a > 0x7FFF) return;
  ram_[a - 0x6000] = data;
  if (a == 0x7F47 && data == 0x00) doDma();
  else if (a == 0x7F4F) {
    if (ram_[0x1F4D] == 0x0E && data < 0x40 && (data & 3) == 0) {
      ram_[0x1F80] = data >> 2;
    } else {
      execCommand(data);
    }
  }
}

void Cx4::power() {
  ram_.fill(0);
  wfxVal_ = wfyVal_ = wfzVal_ = wfx2Val_ = wfy2Val_ = wfDist_ = wfScale_ = 0;
  f41FXVal_ = f41FYVal_ = f41FAngleRes_ = f41FDist_ = f41FDistVal_ = 0;
  c4x_ = c4y_ = c4z_ = c4x2_ = c4y2_ = c4z2_ = tanVal_ = 0;
}

auto Cx4::serialize(Writer& w) const -> void {
  w.raw(ram_.data(), ram_.size());
  w.u16(uint16(wfxVal_)); w.u16(uint16(wfyVal_)); w.u16(uint16(wfzVal_));
  w.u16(uint16(wfx2Val_)); w.u16(uint16(wfy2Val_)); w.u16(uint16(wfDist_)); w.u16(uint16(wfScale_));
  w.u16(uint16(f41FXVal_)); w.u16(uint16(f41FYVal_)); w.u16(uint16(f41FAngleRes_)); w.u16(uint16(f41FDist_)); w.u16(uint16(f41FDistVal_));
}

auto Cx4::deserialize(Reader& r) -> void {
  r.raw(ram_.data(), ram_.size());
  wfxVal_ = int16(r.u16()); wfyVal_ = int16(r.u16()); wfzVal_ = int16(r.u16());
  wfx2Val_ = int16(r.u16()); wfy2Val_ = int16(r.u16()); wfDist_ = int16(r.u16()); wfScale_ = int16(r.u16());
  f41FXVal_ = int16(r.u16()); f41FYVal_ = int16(r.u16()); f41FAngleRes_ = int16(r.u16()); f41FDist_ = int16(r.u16()); f41FDistVal_ = int16(r.u16());
}

auto Cx4::setRom(const std::vector<uint8>& rom, MapMode mode) -> void {
  romData_ = rom.data();
  romSize_ = rom.size();
  romMode_ = mode;
}

}  // namespace snes
