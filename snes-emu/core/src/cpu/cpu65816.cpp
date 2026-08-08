#include "cpu65816.hpp"

#include <cstring>
#include <sstream>

namespace snes {

//====================================================================
// 1-cycle memory operations.
//
// The host bus (mem_) is stepped once per CPU cycle; FastROM/slowROM
// scaling and scheduler catch-up live in a later phase, so each bus
// access here counts as exactly one cycle. This reproduces the classic
// 65816 instruction cycle tables (e.g. LDA #imm = 2 cycles).
//
// Ported cycle-for-cycle from the higan/bsnes WDC65816 core memory.cpp.
//====================================================================

auto Cpu65816::idle() -> uint8 {
  cycles_++;
  return 0;
}

auto Cpu65816::read(uint24 address) -> uint8 {
  cycles_++;
  return mem_.read(address & 0xffffff);
}

auto Cpu65816::write(uint24 address, uint8 data) -> void {
  cycles_++;
  mem_.write(address & 0xffffff, data);
}

// immediate, 2-cycle opcodes with idle cycle will become bus read
// when an IRQ is to be triggered immediately after opcode completion.
// this affects the following opcodes:
//   clc, cld, cli, clv, sec, sed, sei,
//   tax, tay, txa, txy, tya, tyx,
//   tcd, tcs, tdc, tsc, tsx, txs,
//   inc, inx, iny, dec, dex, dey,
//   asl, lsr, rol, ror, nop, xce.
auto Cpu65816::idleIRQ() -> void {
  if (interruptPending()) {
    //modify I/O cycle to bus read cycle, do not increment PC
    read(PC.d);
  } else {
    idle();
  }
}

auto Cpu65816::idle2() -> void {
  if (D.l) idle();
}

auto Cpu65816::idle4(uint16 x, uint16 y) -> void {
  if (!XF || x >> 8 != y >> 8) idle();
}

auto Cpu65816::idle6(uint16 address) -> void {
  if (EF && PC.h != address >> 8) idle();
}

auto Cpu65816::fetch() -> uint8 {
  return read((PC.b << 16) | PC.w++);
}

auto Cpu65816::pull() -> uint8 {
  EF ? void(S.l++) : void(S.w++);
  return read(S.w);
}

auto Cpu65816::push(uint8 data) -> void {
  write(S.w, data);
  EF ? void(S.l--) : void(S.w--);
}

auto Cpu65816::pullN() -> uint8 {
  return read(++S.w);
}

auto Cpu65816::pushN(uint8 data) -> void {
  write(S.w--, data);
}

auto Cpu65816::readDirect(uint16 offset) -> uint8 {
  if (EF && !D.l) return read(D.w | uint8(offset));
  return read(uint16(D.w + offset));
}

auto Cpu65816::writeDirect(uint16 offset, uint8 data) -> void {
  if (EF && !D.l) return write(D.w | uint8(offset), data);
  write(uint16(D.w + offset), data);
}

auto Cpu65816::readDirectN(uint16 offset) -> uint8 {
  return read(uint16(D.w + offset));
}

auto Cpu65816::readDirectX(uint16 address, uint16 offset) -> uint8 {
  // The (direct,X) addressing mode has a bug in which the high byte is
  // wrapped within the page if E = 1 and D&0xFF != 0.
  if (EF && D.l) return read(((D.w + address) & 0xffff00) | uint8(D.w + address + offset));
  return readDirect(address + offset);
}

auto Cpu65816::readBank(uint24 address) -> uint8 {
  return read((B << 16) + address);
}

auto Cpu65816::writeBank(uint24 address, uint8 data) -> void {
  write((B << 16) + address, data);
}

auto Cpu65816::readStack(uint16 offset) -> uint8 {
  return read(uint16(S.w + offset));
}

auto Cpu65816::writeStack(uint16 offset, uint8 data) -> void {
  write(uint16(S.w + offset), data);
}

//====================================================================
// power / reset / execute
//====================================================================

auto Cpu65816::power() -> void {
  PC = 0x000000;
  A = 0x0000;
  X = 0x0000;
  Y = 0x0000;
  S = 0x01ff;
  D = 0x0000;
  B = 0x00;
  P = 0x34;
  E = true;   // power-on resumes in emulation mode
  nmi = 0;
  irq = 0;
  wai = 0;
  stp = 0;
  vector = 0xfffc;  //reset vector address
  cycles_ = 0;
}

auto Cpu65816::reset() -> void {
  P = 0x34;
  E = true;
  nmi = 0;
  irq = 0;
  wai = 0;
  stp = 0;
  vector = 0xfffc;
  // emulation mode forces M=X=1 on reset
  XF = 1;
  MF = 1;
  X.h = 0x00;
  Y.h = 0x00;
  S.h = 0x01;
  uint8 lo = read(0xfffc);
  uint8 hi = read(0xfffd);
  PC = 0x0000 | uint16(lo) | (uint16(hi) << 8);
  cycles_ = 0;
}

auto Cpu65816::execute() -> uint64 {
  cycles_ = 0;
  if (stopped()) { idle(); return cycles_; }   // STP: hard stop, idles forever
  if (waiting()) {                             // WAI: parked until interrupt
    if (!interruptPending()) { idle(); return cycles_; }
    wai = false;
    idle();                                    // trailing idle before dispatch
  }
  if (!interruptPending()) { instruction(); return cycles_; }
  if (nmi) { nmi = false; vector = EF ? 0xfffa : 0xffea; interrupt(); return cycles_; }
  if (irq) { irq = false; vector = EF ? 0xfffe : 0xffee; interrupt(); return cycles_; }
  return cycles_;
}

auto Cpu65816::interruptPending() const -> bool {
  return nmi || (irq && !IF);   // IRQ gated by the I flag (ares irqTest)
}

auto Cpu65816::interrupt() -> void {
  read(PC.d);                   // N: prelude bus read
  idle();                       // N: prelude idle
  if (!EF) push(PC.b);          // PBR only in native mode
  push(PC.h);
  push(PC.l);
  push(EF ? uint8(P) & ~0x10 : uint8(P));  // B flag = 0 in E mode
  IF = 1;
  DF = 0;
  PC.l = read(vector + 0);
  PC.h = read(vector + 1);
  PC.b = 0x00;
}

auto Cpu65816::setNmi(bool value) -> void { nmi = value; }
auto Cpu65816::setIrq(bool value) -> void { irq = value; }

//====================================================================
// register / flag accessors
//====================================================================

auto Cpu65816::pc() const -> uint24 { return PC.d; }
auto Cpu65816::setPc(uint24 value) -> void { PC = value; }
auto Cpu65816::acc() const -> uint16 { return A.w; }
auto Cpu65816::setAcc(uint16 value) -> void { A = value; }
auto Cpu65816::xReg() const -> uint16 { return X.w; }
auto Cpu65816::yReg() const -> uint16 { return Y.w; }
auto Cpu65816::stack() const -> uint16 { return S.w; }
auto Cpu65816::setStack(uint16 value) -> void { S = value; }
auto Cpu65816::direct() const -> uint16 { return D.w; }
auto Cpu65816::dbr() const -> uint8 { return B; }
auto Cpu65816::pbr() const -> uint8 { return PC.b; }
auto Cpu65816::flagP() const -> uint8 { return uint8(P); }
auto Cpu65816::setFlagP(uint8 value) -> void { P = value; }
auto Cpu65816::emulation() const -> bool { return E; }
auto Cpu65816::stopped() const -> bool { return stp; }
auto Cpu65816::waiting() const -> bool { return wai; }
auto Cpu65816::traceState() const -> TraceState {
  TraceState t;
  t.pc = PC.d;
  t.a = A.w;
  t.x = X.w;
  t.y = Y.w;
  t.s = S.w;
  t.d = D.w;
  t.b = B;
  t.p = uint8(P);
  t.e = E;
  return t;
}

}  // namespace snes