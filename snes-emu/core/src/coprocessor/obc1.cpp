#include "coprocessor/obc1.hpp"

namespace snes {

auto Obc1::handles(uint24 address) const -> bool {
  uint32 bank = address >> 16;
  if (bank > 0x3F && bank < 0x80) return false;
  if (bank >= 0x80) bank -= 0x80;
  if (bank > 0x3F) return false;
  uint32 offs = address & 0xFFFF;
  return offs >= 0x6000 && offs < 0x8000;
}

void Obc1::power() {
  ram_.fill(0xFF);
  basePtr_ = (ram_[0x1FF5] & 1) ? 0x1800 : 0x1C00;
  address_ = ram_[0x1FF6] & 0x7F;
  shift_ = (ram_[0x1FF6] & 3) << 1;
}

auto Obc1::read(uint24 address) -> uint8 {
  uint32 a = address & 0x1FFF;
  switch (a) {
    case 0x1FF0: return ram_[basePtr_ + (address_ << 2) + 0];
    case 0x1FF1: return ram_[basePtr_ + (address_ << 2) + 1];
    case 0x1FF2: return ram_[basePtr_ + (address_ << 2) + 2];
    case 0x1FF3: return ram_[basePtr_ + (address_ << 2) + 3];
    case 0x1FF4: return ram_[basePtr_ + (address_ >> 2) + 0x200];
    default: return ram_[a];
  }
}

void Obc1::write(uint24 address, uint8 data) {
  uint32 a = address & 0x1FFF;
  switch (a) {
    case 0x1FF0:
      ram_[basePtr_ + (address_ << 2) + 0] = data;
      return;
    case 0x1FF1:
      ram_[basePtr_ + (address_ << 2) + 1] = data;
      return;
    case 0x1FF2:
      ram_[basePtr_ + (address_ << 2) + 2] = data;
      return;
    case 0x1FF3:
      ram_[basePtr_ + (address_ << 2) + 3] = data;
      return;
    case 0x1FF4: {
      uint8 t = ram_[basePtr_ + (address_ >> 2) + 0x200];
      t = (t & uint8(~(3 << shift_))) | uint8((data & 3) << shift_);
      ram_[basePtr_ + (address_ >> 2) + 0x200] = t;
      return;
    }
    case 0x1FF5:
      basePtr_ = (data & 1) ? 0x1800 : 0x1C00;
      ram_[a] = data;
      return;
    case 0x1FF6:
      address_ = data & 0x7F;
      shift_ = (data & 3) << 1;
      ram_[a] = data;
      return;
    case 0x1FF7:
      ram_[a] = data;
      return;
    default:
      ram_[a] = data;
      return;
  }
}

void Obc1::serialize(Writer& w) const {
  w.raw(ram_.data(), ram_.size());
  w.u16(basePtr_);
  w.u8(address_);
  w.u8(shift_);
}

void Obc1::deserialize(Reader& r) {
  r.raw(ram_.data(), ram_.size());
  basePtr_ = r.u16();
  address_ = r.u8();
  shift_ = r.u8();
}

}  // namespace snes
