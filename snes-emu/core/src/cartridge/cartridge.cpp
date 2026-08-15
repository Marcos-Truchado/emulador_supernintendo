#include "snes/snes.hpp"

#include <fstream>

namespace snes {

// 512-byte copier header: a file with one is N*32KB + 512 bytes.
static bool isCopierHeader(uint64 size) { return (size % 0x8000) == 512; }

// Header map-mode byte ($7FD5 / $FFD5); per fullsnes "ROM Speed and Map Mode":
//   bits 7-6 always 0, bit5 always 1, bit4 = speed (0=Slow, 1=Fast),
//   bits 3-0 map mode (0=LoROM, 1=HiROM, 2=LoROM+S-DD1, 3=LoROM+SA-1,
//   5=ExHiROM, A=HiROM+SPC7110). Only the low nibble selects the map family.
static bool isLoRomFamily(uint8 b) { return (b & 0x0F) == 0x00; }
static bool isHiRomFamily(uint8 b) { return (b & 0x0F) == 0x01; }
static bool isExHiRomFamily(uint8 b) { return (b & 0x0F) == 0x05; }

// Raw-image file offset of the given 24-bit address for a map mode, before the
// copier header is stripped. Returns the 32-bit max (as the "unmapped" marker)
// when the address is outside the ROM windows of that mode.
static uint32 offsetInFile(MapMode mode, uint24 address) {
  uint32 bank = address >> 16;
  uint32 offs = address & 0xFFFF;
  switch (mode) {
    case MapMode::lorom:
      // $00-3F/$80-BF:8000-FFFF -> 32KB windows.
      if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF))
        return ((bank & 0x3F) << 15) + (address & 0x7FFF);
      // $40-7D/$C0-FF:0000-FFFF -> full 64KB windows (linear; banks 40-5F
      // mirror the first 2MB, 60-7D continue past it, per the standard
      // LoROM decoder used by bsnes/Mesen-S).
      if ((bank >= 0x40 && bank <= 0x7D) || (bank >= 0xC0 && bank <= 0xFF))
        return ((bank & 0x3F) << 16) + offs;
      return -1;
    case MapMode::hirom:
      if (bank == 0x7E || bank == 0x7F) return -1;
      // $00-3F/$80-BF:8000-FFFF mirror the upper halves of the $40-7D/$C0-FF
      // banks (fullsnes "Plain HiROM": ROM at 40-7d,c0-ff:0000-ffff, ROM
      // mirrors at 00-3f,80-bf:8000-ffff). The lower halves of the system
      // banks are WRAM/I/O, not ROM.
      if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
        if (offs < 0x8000) return -1;
        return ((bank & 0x3F) << 16) + offs;
      }
      // $40-7D:0000-FFFF -> full 64KB windows; $C0-FF mirrors them.
      if (bank >= 0x40 && bank <= 0x7D)
        return ((bank & 0x3F) << 16) + offs;
      if (bank >= 0xC0)
        return ((bank & 0x3F) << 16) + offs;
      return -1;
    case MapMode::exhirom:
      // $40-7D:0000-FFFF -> first 4MB; $C0-FF -> next 4MB; system banks
      // $00-3F/$80-BF:8000-FFFF mirror the $40-7D windows.
      if (bank >= 0x40 && bank <= 0x7D)
        return ((bank & 0x3F) << 16) + offs;
      if (bank >= 0xC0)
        return 0x400000 + ((bank - 0xC0) << 16) + offs;
      if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF))
        return ((bank & 0x3F) << 16) + (address & 0x7FFF);
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

uint32 Cartridge::sramSize() const {
  // Header RAM-size byte at $FFD8 in the map mode's header window:
  // (1 SHL n) Kbytes, n in bits 2-0 (fullsnes cartridge header). 0 = no SRAM.
  uint32 pos = mapMode_ == MapMode::lorom     ? 0x7FD8
               : mapMode_ == MapMode::hirom   ? 0xFFD8
               : mapMode_ == MapMode::exhirom ? 0x40FFD8
                                              : uint32(-1);
  if (pos == uint32(-1) || pos >= data_.size()) return 0;
  uint8 n = data_[pos] & 0x07;
  return n ? uint32(1) << (n + 10) : 0;
}

uint32 Cartridge::romOffset(uint32 address) const {
  // Raw offset into the (header-stripped) ROM image for a 24-bit address,
  // or the uint32 max when the address is outside the map mode's ROM
  // windows. Mirror reads that run past the end of a power-of-two image
  // wrap modulo the image size, exactly like a real mask ROM.
  return offsetInFile(mapMode_, address);
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