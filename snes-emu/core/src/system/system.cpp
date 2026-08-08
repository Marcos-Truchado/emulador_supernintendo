#include "snes/snes.hpp"

#include "../cpu/cpu65816.hpp"

namespace snes {

// System facade: owns cartridge + bus + CPU and exposes the public API
// (load / reset / step / run). Phase 1 keeps execution to a single CPU
// instruction at a time; the scheduler (PPU/APU catch-up) lands later.

System::System() : cartridge_(std::make_unique<Cartridge>()),
                   bus_(std::make_unique<Bus>(*cartridge_)),
                   cpu_(std::make_unique<Cpu65816>(*bus_)) {}

System::~System() = default;

auto System::load(const std::string& filename, std::string* error) -> bool {
  if (!cartridge_->load(filename, error)) return false;
  cpu_->power();   // fresh power-on state, then load the reset vector
  cpu_->reset();
  return true;
}

auto System::reset() -> void { cpu_->reset(); }

auto System::step() -> uint64 { return cpu_->execute(); }

auto System::run(uint64 maxCycles) -> uint64 {
  uint64 total = 0;
  while (total < maxCycles && !cpu_->stopped()) {
    total += cpu_->execute();
  }
  return total;
}

auto System::cpu() -> Cpu65816& { return *cpu_; }
auto System::cpu() const -> const Cpu65816& { return *cpu_; }
auto System::bus() -> Bus& { return *bus_; }
auto System::bus() const -> const Bus& { return *bus_; }
auto System::cartridge() -> Cartridge& { return *cartridge_; }
auto System::cartridge() const -> const Cartridge& { return *cartridge_; }

}  // namespace snes