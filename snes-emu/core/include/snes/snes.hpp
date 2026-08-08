#pragma once

// snes.hpp — public API of the emulator core.
//
// The core is completely decoupled from any frontend (window, audio, input).
// It exposes: a minimal 24-bit Memory bus interface, Cartridge (ROM loading
// + header detection), a minimal LoROM bus, and the System facade that wires
// CPU + bus + cartridge together (run, reset, step).
//
// Phase 1: CPU 65816 standalone, validated against community test ROMs.
// PPU/APU/DMA/scheduler arrive in later phases; the System API below will
// grow (run_frame, framebuffer, audio buffer, save states).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snes {

using uint8 = std::uint8_t;
using int8 = std::int8_t;
using uint16 = std::uint16_t;
using int16 = std::int16_t;
using uint24 = std::uint32_t;  // 24-bit address, kept in a uint32
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// Minimal 24-bit address space interface the CPU core talks to.
class Memory {
 public:
  virtual ~Memory() = default;
  virtual auto read(uint24 address) -> uint8 = 0;
  virtual auto write(uint24 address, uint8 data) -> void = 0;
};

// What the frontend / test harness needs from the CPU in phase 1.
class Cpu65816;

enum class MapMode {
  unknown,
  lorom,
  hirom,
  exhirom,
};

// Loaded SNES cartridge: ROM image + parsed header.
class Cartridge {
 public:
  // Loads a ROM file; detects 512-byte copier header and map mode heuristically.
  // Returns false and fills `error` when the file cannot be loaded.
  auto load(const std::string& filename, std::string* error = nullptr) -> bool;
  auto load(std::vector<uint8> data, std::string* error = nullptr) -> bool;

  auto mapMode() const -> MapMode;
  auto rom() const -> const std::vector<uint8>&;
  auto romSize() const -> uint32;
  // true when a 512-byte copier header is present in the file (data[0..512]).
  auto hasCopierHeader() const -> bool;
  // size in bytes of the logical ROM (file minus copier header).
  auto romOffset(uint32 address) const -> uint32;  // mapped ROM offset, -1 if unmapped

 private:
  auto detect() -> void;
  std::vector<uint8> data_;
  MapMode mapMode_ = MapMode::unknown;
  bool hasHeader_ = false;
};

// Minimal LoROM bus: ROM + 128KB WRAM + MMIO stubs (enough to run the
// community CPU test ROMs). PPU/APU/DMA registers are write-ignored;
// $4210 returns an alternating vblank bit so wait_for_vblank terminates.
class Bus : public Memory {
 public:
  explicit Bus(Cartridge& cartridge) : cartridge_(cartridge) {}

  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;

  // direct WRAM access for test harnesses
  auto wram() -> std::vector<uint8>& { return wram_; }
  auto wram() const -> const std::vector<uint8>& { return wram_; }

 private:
  uint8 mmioRead(uint24 address);
  uint8 romRead(uint24 address) const;

  Cartridge& cartridge_;
  std::vector<uint8> wram_ = std::vector<uint8>(128 * 1024);
  bool vblankToggle_ = false;  // alternates $4210 bit7 per read
};

// System facade: wires cartridge + bus + CPU, exposes step/run/reset.
class System {
 public:
  System();
  ~System();

  System(const System&) = delete;
  auto operator=(const System&) -> System& = delete;

  auto load(const std::string& filename, std::string* error = nullptr) -> bool;

  // Reset the CPU and load the reset vector from the bus.
  auto reset() -> void;
  // Execute exactly one instruction; returns the number of cycles it took.
  auto step() -> uint64;
  // Execute up to maxCycles cycles; stops early if the CPU halts (STP/WAI).
  auto run(uint64 maxCycles) -> uint64;

  auto cpu() -> Cpu65816&;
  auto cpu() const -> const Cpu65816&;
  auto bus() -> Bus&;
  auto bus() const -> const Bus&;
  auto cartridge() -> Cartridge&;
  auto cartridge() const -> const Cartridge&;

 private:
  std::unique_ptr<Cartridge> cartridge_;
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<Cpu65816> cpu_;
};

}  // namespace snes
