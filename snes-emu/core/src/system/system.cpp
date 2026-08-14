#include "snes/snes.hpp"

#include "../apu/apu.hpp"
#include "../cpu/cpu65816.hpp"
#include "../ppu/ppu.hpp"
#include "../scheduler/scheduler.hpp"
#include "../serialize/serialize.hpp"

namespace snes {

// System facade: owns cartridge + PPU + APU + scheduler + bus + CPU and
// exposes the public API (load / reset / step / run). Phase 3 wires the
// relative scheduler: the CPU is the conductor (each bus access steps the
// delta and syncs the PPU inside the CPU's memory helpers); step() runs
// exactly one instruction and finishes with a final sync so the PPU is caught
// up at every instruction boundary. Phase 6 adds the APU as the scheduler's
// secondary thread (its own 21:24 clock ratio inside its step()).

System::System() : cartridge_(std::make_unique<Cartridge>()),
                   ppu_(std::make_unique<Ppu>()),
                   apu_(std::make_unique<Apu>()),
                    scheduler_(std::make_unique<Scheduler>(*ppu_, apu_.get())),
                    bus_(std::make_unique<Bus>(*cartridge_, *ppu_, *scheduler_, *apu_)),
                    cpu_(std::make_unique<Cpu65816>(*bus_, *scheduler_)) {
  // Phase 3b: PPU owns the 65816 interrupt semantics (NMI edge-detect,
  // IRQ level) and drives the CPU's external pins through these sinks.
  ppu_->setNmiPin([this](bool value) { cpu_->setNmi(value); });
  ppu_->setIrqPin([this](bool value) { cpu_->setIrq(value); });
  // Phase 5: PPU frame-start / HBlank events drive the DMA/HDMA engine.
  ppu_->setFrameStartSink([this]() { bus_->hdmaReset(); });
  ppu_->setHblankSink([this]() { bus_->hdmaRun(); });
}

System::~System() = default;

auto System::load(const std::string& filename, std::string* error) -> bool {
  if (!cartridge_->load(filename, error)) return false;
  bus_->power();   // power-on MMIO values + SRAM sizing, before CPU reads
  ppu_->power();   // power-on PPU registers/counters
  apu_->power();   // power-on APU (SPC700 + DSP)
  scheduler_->reset();
  cpu_->power();   // fresh power-on state, then load the reset vector
  cpu_->reset();
  return true;
}

auto System::reset() -> void {
  bus_->reset();   // soft reset: only un-bracketed MMIO registers
  ppu_->reset();   // NMITIMEN clears (acks IRQs); bracketed regs survive
  apu_->reset();   // SPC700 back to the boot-ROM transfer phase
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
auto System::apu() -> Apu& { return *apu_; }
auto System::apu() const -> const Apu& { return *apu_; }
auto System::scheduler() -> Scheduler& { return *scheduler_; }
auto System::scheduler() const -> const Scheduler& { return *scheduler_; }

// ---- frontend integration (phase 7) ----

auto System::readAudio(int16* buffer, size_t count) -> size_t {
  return apu_->readAudio(buffer, count);
}

auto System::setJoypad(int port, uint16 buttons) -> void {
  bus_->setJoypad(port, buttons);
}

// ---- framebuffer (phase 7) ----

auto System::frameWidth() const -> int { return 256; }
auto System::frameHeight() const -> int { return 224; }
auto System::pixelColor(int x, int y) const -> uint16 { return ppu_->pixelColor(x, y); }
auto System::renderedFrames() const -> uint64 { return ppu_->renderedFrames(); }

// Save state layout (versioned for forward compatibility):
//   u32 magic "SNSS" | u32 version | scheduler | cpu | ppu | apu | bus
auto System::saveState() -> std::vector<uint8> {
  Writer w;
  w.u32(0x53534E53);  // "SNSS"
  w.u32(2);           // version (bumped: APU serialization layout changed)
  scheduler_->serialize(w);
  cpu_->serialize(w);
  ppu_->serialize(w);
  apu_->serialize(w);
  bus_->serialize(w);
  return w.data();
}

auto System::loadState(const std::vector<uint8>& data) -> bool {
  Reader r(data);
  if (r.u32() != 0x53534E53) return false;
  if (r.u32() != 2) return false;
  scheduler_->deserialize(r);
  cpu_->deserialize(r);
  ppu_->deserialize(r);
  apu_->deserialize(r);
  bus_->deserialize(r);
  return r.ok();
}

}  // namespace snes
