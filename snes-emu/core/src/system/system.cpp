#include "snes/snes.hpp"

#include "../cpu/cpu65816.hpp"
#include "../ppu/ppu.hpp"
#include "../scheduler/scheduler.hpp"

namespace snes {

// System facade: owns cartridge + PPU + scheduler + bus + CPU and exposes
// the public API (load / reset / step / run). Phase 3 wires the relative
// scheduler: the CPU is the conductor (each bus access steps the delta and
// syncs the PPU inside the CPU's memory helpers); step() runs exactly one
// instruction and finishes with a final sync so the PPU is caught up at
// every instruction boundary.

System::System() : cartridge_(std::make_unique<Cartridge>()),
                   ppu_(std::make_unique<Ppu>()),
                   scheduler_(std::make_unique<Scheduler>(*ppu_)),
                   bus_(std::make_unique<Bus>(*cartridge_, *ppu_)),
                   cpu_(std::make_unique<Cpu65816>(*bus_, *scheduler_)) {
  // Phase 3b: PPU owns the 65816 interrupt semantics (NMI edge-detect,
  // IRQ level) and drives the CPU's external pins through these sinks.
  ppu_->setNmiPin([this](bool value) { cpu_->setNmi(value); });
  ppu_->setIrqPin([this](bool value) { cpu_->setIrq(value); });
}

System::~System() = default;

auto System::load(const std::string& filename, std::string* error) -> bool {
  if (!cartridge_->load(filename, error)) return false;
  bus_->power();   // power-on MMIO values + SRAM sizing, before CPU reads
  ppu_->power();   // power-on PPU registers/counters
  scheduler_->reset();
  cpu_->power();   // fresh power-on state, then load the reset vector
  cpu_->reset();
  return true;
}

auto System::reset() -> void {
  bus_->reset();   // soft reset: only un-bracketed MMIO registers
  ppu_->reset();   // NMITIMEN clears (acks IRQs); bracketed regs survive
  scheduler_->reset();
  cpu_->reset();
}

auto System::step() -> uint64 {
  uint64 cycles = cpu_->execute();
  scheduler_->sync();  // final sync: PPU caught up at instruction boundary
  return cycles;
}

auto System::run(uint64 maxCycles) -> uint64 {
  uint64 total = 0;
  while (total < maxCycles && !cpu_->stopped()) {
    total += step();
  }
  return total;
}

auto System::cpu() -> Cpu65816& { return *cpu_; }
auto System::cpu() const -> const Cpu65816& { return *cpu_; }
auto System::bus() -> Bus& { return *bus_; }
auto System::bus() const -> const Bus& { return *bus_; }
auto System::cartridge() -> Cartridge& { return *cartridge_; }
auto System::cartridge() const -> const Cartridge& { return *cartridge_; }
auto System::ppu() -> Ppu& { return *ppu_; }
auto System::ppu() const -> const Ppu& { return *ppu_; }
auto System::scheduler() -> Scheduler& { return *scheduler_; }
auto System::scheduler() const -> const Scheduler& { return *scheduler_; }

}  // namespace snes
