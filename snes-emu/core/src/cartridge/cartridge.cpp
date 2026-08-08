#include "snes/snes.hpp"

#include <fstream>

namespace snes {

// 512-byte copier header: a file with one is N*32KB + 512 bytes.
static bool isCopierHeader(uint64 size) { return (size % 0x8000) == 512; }

// Header map-mode byte ($7FD5 / $FFD5); bit layout 00ssmmmm:
//   bits 7-6 ignored, bit5 = speed (0=SlowROM, 1=FastROM),
//   bits 4-0 map mode (0=LoROM, 1=HiROM, 2=ExHiROM, ...).
static bool isLoRomFamily(uint8 b) { return (b & 0x1F) == 0x00; }
static bool isHiRomFamily(uint8 b) { return (b & 0x1F) == 0x01; }
static bool isExHiRomFamily(uint8 b) { return (b & 0x1F) == 0x02; }

// Raw-image file offset of the given 24-bit address for a map mode, before the
// copier header is stripped. Returns the 32-bit max (as the "unmapped" marker)
// when the address is outside the ROM windows of that mode.
static uint32 offsetInFile(MapMode mode, uint24 address) {
  uint32 bank = address >> 16;
  uint32 offs = address & 0x7FFF;  // $FFFF masked; ROM windows live at $8000-$FFFF
  switch (mode) {
    case MapMode::lorom:
      if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF))
        return ((bank & 0x3F) << 15) + offs;
      if (bank >= 0x40 && bank <= 0x5F || (bank >= 0xC0 && bank <= 0xDF))
        return ((bank & 0x1F) << 16) + (address & 0xFFFF);
      return -1;
    case MapMode::hirom:
      if (bank == 0x7E || bank == 0x7F) return -1;
      if (bank <= 0x3F)  // 00-3F: mirrors the top 32KB of banks 40-7D at +$8000
        return 0x400000 + (bank << 16) + (address & 0xFFFF) - 0x8000;
      if (bank >= 0x40 && bank <= 0x7D)  // longbanks 40-7D: full 64KB windows
        return ((bank & 0x3F) << 16) + (address & 0xFFFF);
      if (bank >= 0xC0)  // mirror of 40-7D
        return (((bank - 0xC0 + 0x40) & 0x3F) << 16) + (address & 0xFFFF);
      return -1;
    case MapMode::exhirom:
      if (bank >= 0xC0)  // only full 64KB windows in C0-FF
        return ((bank - 0xC0) << 16) + (address & 0xFFFF);
      return -1;
    default:
      return -1;
  }
}

bool Cartridge::load(const std::string& filename, std::string* error) {
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open '" + filename + "'";
    return false;
  }
  file.seekg(0, std::ios::end);
  auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  if (size <= 0) {
    if (error) *error = "empty file '" + filename + "'";
    return false;
  }
  std::vector<uint8> data(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
    if (error) *error = "read error on '" + filename + "'";
    return false;
  }
  return load(std::move(data), error);
}

bool Cartridge::load(std::vector<uint8> data, std::string* error) {
  data_ = std::move(data);
  hasHeader_ = isCopierHeader(data_.size());
  detect();
  if (mapMode_ == MapMode::unknown) {
    if (error) *error = "could not determine map mode (LoROM/HiROM)";
    return false;
  }
  if (hasHeader_) data_.erase(data_.begin(), data_.begin() + 512);
  return true;
}

MapMode Cartridge::mapMode() const { return mapMode_; }
const std::vector<uint8>& Cartridge::rom() const { return data_; }
uint32 Cartridge::romSize() const { return uint32(data_.size()); }
bool Cartridge::hasCopierHeader() const { return hasHeader_; }

uint32 Cartridge::romOffset(uint32 address) const {
  uint32 raw = offsetInFile(mapMode_, address);
  if (raw == uint32(-1)) return -1;
  uint32 logical = hasHeader_ ? raw - 512 : raw;
  return logical >= data_.size() ? -1 : logical;
}

void Cartridge::detect() {
  // Probe each map family at its conventional raw-file header position.
  auto byte = [&](uint32 offset) {
    if (hasHeader_) offset += 512;
    return offset < data_.size() ? data_[offset] : uint8(0);
  };
  const uint32 loPos = 0x7FD5;
  const uint32 hiPos = 0xFFD5;
  const uint32 exPos = 0x40FFD5;

  if (isExHiRomFamily(byte(exPos))) {
    mapMode_ = MapMode::exhirom;
  } else if (isHiRomFamily(byte(hiPos))) {
    mapMode_ = MapMode::hirom;
  } else if (isLoRomFamily(byte(loPos))) {
    mapMode_ = MapMode::lorom;
  } else if ((data_.size() & (data_.size() - 1)) == 0) {
    // No recognizable header byte: fall back to the size heuristic (and be
    // conservative with test/homebrew ROMs, which are small LoROM images).
    mapMode_ = data_.size() > 0x400000 ? MapMode::hirom : MapMode::lorom;
  } else {
    mapMode_ = MapMode::unknown;
  }
}

}  // namespace snes