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
  // Master-clock waitstate cost of one bus access at `address` (6/8/12
  // on real hardware; phase 3 timing). The CPU's scheduler steps this.
  virtual auto waitStates(uint24 address) -> uint8 { return 6; }
};

// What the frontend / test harness needs from the CPU in phase 1.
class Cpu65816;
class Thread;
class Scheduler;
class Ppu;

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
  // battery SRAM size in bytes, from the header RAM-size byte at $FFD8
  // ((1 SHL n) Kbytes; 0 = no SRAM).
  auto sramSize() const -> uint32;

 private:
  auto detect() -> void;
  std::vector<uint8> data_;
  MapMode mapMode_ = MapMode::unknown;
  bool hasHeader_ = false;
};

// Full SNES memory map bus: LoROM/HiROM/ExHiROM routing, 128KB WRAM, SRAM,
// and the base MMIO registers (CPU on-chip, PPU/APU stubs, WRAM port).
// PPU timing registers are delegated to the Ppu (phase 3): reads of
// $4210/$4211/$4212 and writes of $4200/$4207-$420A go to the PPU, which
// owns the H/V counters, VBlank/HBlank flags and IRQ logic. $420D MEMSEL
// is stored here (bit0 selects the WS2 waitstate, 0 = 2.68MHz on reset).
// APU ports are plain storage, DMA registers are R/W storage for Phase 5.
// Unmapped reads return open bus (the last byte on the data bus, tracked
// in lastData_).
class Bus : public Memory {
 public:
  explicit Bus(Cartridge& cartridge, Ppu& ppu, Scheduler& scheduler)
      : cartridge_(cartridge), ppu_(ppu), scheduler_(scheduler) {}

  auto read(uint24 address) -> uint8 override;
  auto write(uint24 address, uint8 data) -> void override;
  auto waitStates(uint24 address) -> uint8 override;

  // Power-on and soft-reset register values (fullsnes I/O map right column:
  // bracketed values survive reset, only power-on sets them).
  auto power() -> void;
  auto reset() -> void;

  // direct WRAM/SRAM access for test harnesses
  auto wram() -> std::vector<uint8>& { return wram_; }
  auto wram() const -> const std::vector<uint8>& { return wram_; }
  auto sram() -> std::vector<uint8>& { return sram_; }
  auto sram() const -> const std::vector<uint8>& { return sram_; }

  // last byte on the data bus (what open-bus reads return)
  auto openBus() const -> uint8 { return lastData_; }

  // register readback for tests and later phases
  auto ppuRegister(uint8 offset) const -> uint8;  // $2100-$2133 write shadows
  auto apuPort(uint8 index) const -> uint8;       // $2140-$2143
  auto cpuRegister(uint8 offset) const -> uint8;  // $4200-$420D write shadows
  auto dmaRegister(uint8 offset) const -> uint8;  // $4300-$437B
  auto wramAddress() const -> uint32;             // $2181-$2183 17-bit addr

  // Phase 5 DMA/HDMA entry points (wired by System via the PPU sinks).
  auto hdmaReset() -> void;  // HDMA table reload (V=0)
  auto hdmaRun() -> void;    // HDMA one-unit transfer per line (HBlank)

 private:
  uint8 mmioRead(uint24 address);
  void mmioWrite(uint24 address, uint8 data);
  void writeCpuRegister(uint8 offset, uint8 data);
  void dmaRun();                 // GP-DMA for the channels set in $420B
  void dmaTransfer(int channel);  // one channel's GP-DMA transfer
  uint8 romRead(uint24 address);
  uint8 sramRead(uint32 offs, uint32 baseOffs);
  void sramWrite(uint32 offs, uint32 baseOffs, uint8 data);
  uint8 latch(uint8 value);  // put value on the data bus (open-bus tracking)

  Cartridge& cartridge_;
  Ppu& ppu_;
  Scheduler& scheduler_;
  std::vector<uint8> wram_ = std::vector<uint8>(128 * 1024);
  std::vector<uint8> sram_;
  uint8 lastData_ = 0;

  uint8 ppuReg_[0x34] = {};  // $2100-$2133 write shadows
  uint8 apuPort_[4] = {};    // $2140-$2143
  uint8 cpuReg_[0x10] = {};  // $4200-$420D write shadows
  uint8 dmaReg_[0x80] = {};  // $4300-$437B (8 channels x 16 bytes)
  uint32 wramAddr_ = 0;      // $2181-$2183 17-bit WRAM port address

  struct HdmaState {  // per-channel HDMA runtime state (phase 5)
    uint16 tableAddr = 0;  // A2Ax: current table address
    uint16 dataAddr = 0;   // DASx: current indirect data address
    uint8 remaining = 0;   // lines left in the current table entry
    bool repeat = false;   // repeat mode (transfer every line)
    bool firstLine = false;
  } hdma_[8];

  uint8 mpyA_ = 0xFF;        // $4202 multiplicand
  uint16 divDividend_ = 0;   // $4204/$4205 dividend
  uint16 divQuotient_ = 0;   // $4214/$4215 division quotient
  uint16 mathResult_ = 0;    // $4216/$4217 product or remainder
};

// System facade: wires cartridge + bus + PPU + scheduler + CPU, exposes
// step/run/reset. Phase 3: step() runs one instruction with the CPU as
// conductor (each bus access subtracts waitstates from the scheduler delta
// and syncs the PPU); the PPU advances dot by dot (4 master cycles).
class System {
 public:
  System();
  ~System();

  System(const System&) = delete;
  auto operator=(const System&) -> System& = delete;

  auto load(const std::string& filename, std::string* error = nullptr) -> bool;

  // Reset the CPU (and PPU/scheduler) and load the reset vector from the bus.
  auto reset() -> void;
  // Execute exactly one instruction; returns the CPU cycles it took.
  auto step() -> uint64;
  // Execute up to maxCycles cycles; stops early if the CPU halts (STP/WAI).
  auto run(uint64 maxCycles) -> uint64;

  auto cpu() -> Cpu65816&;
  auto cpu() const -> const Cpu65816&;
  auto bus() -> Bus&;
  auto bus() const -> const Bus&;
  auto cartridge() -> Cartridge&;
  auto cartridge() const -> const Cartridge&;
  auto ppu() -> Ppu&;
  auto ppu() const -> const Ppu&;
  auto scheduler() -> Scheduler&;
  auto scheduler() const -> const Scheduler&;

 private:
  std::unique_ptr<Cartridge> cartridge_;
  std::unique_ptr<Ppu> ppu_;
  std::unique_ptr<Scheduler> scheduler_;
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<Cpu65816> cpu_;
};

}  // namespace snes
