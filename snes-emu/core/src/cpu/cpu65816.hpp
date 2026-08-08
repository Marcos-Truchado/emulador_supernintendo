#pragma once

// Cpu65816 — Ricoh 5A22 / WDC 65C816 interpreter core.
//
// Decoupled from the host: every memory access (read/write) plus every
// internal "dummy" cycle counts as exactly one CPU step, so the cycle count
// returned by execute() matches the canonical 65816 instruction tables.
// FastROM/slowROM scaling and scheduler integration are later phases; the
// core just needs the Memory interface to fetch/execute.
//
// Register model mirrors the physical chip: A/X/Y/S/D are 16-bit with 8-bit
// operation selected by the M/X status bits, PC is 24-bit (bank:offset), and
// the emulation flag (E) forces M=X=1.
//
// Execution dispatch follows the well-known 4-way table approach: a single
// 256-entry opcode table, expanded four times (M and X each 8/16) via
// macros (see opcodes.cpp).

#include "snes/snes.hpp"

#include <cstdint>
#include <string>

namespace snes {

class Cpu65816 {
 public:
  using alu8 = auto (Cpu65816::*)(uint8) -> uint8;
  using alu16 = auto (Cpu65816::*)(uint16) -> uint16;

  explicit Cpu65816(Memory& mem) : mem_(mem) {}
  ~Cpu65816() = default;

  Cpu65816(const Cpu65816&) = delete;
  auto operator=(const Cpu65816&) -> Cpu65816& = delete;

  // Power-on state: emulation mode, E=1, P=$34, PC=0, S=$01ff.
  auto power() -> void;
  // Reset: load the reset vector from $fffc like hardware.
  auto reset() -> void;
  // Execute exactly one instruction; returns the CPU cycles consumed.
  auto execute() -> uint64;

  // ---- register / flag access for harness, trace logger, save states ----
  auto pc() const -> uint24;
  auto setPc(uint24 value) -> void;
  auto acc() const -> uint16;
  auto setAcc(uint16 value) -> void;
  auto xReg() const -> uint16;
  auto yReg() const -> uint16;
  auto stack() const -> uint16;
  auto setStack(uint16 value) -> void;
  auto direct() const -> uint16;
  auto dbr() const -> uint8;
  auto pbr() const -> uint8;
  auto flagP() const -> uint8;
  auto setFlagP(uint8 value) -> void;
  auto emulation() const -> bool;

  // STP/WAI state (execute() parks inside these instructions until cleared).
  auto stopped() const -> bool;
  auto waiting() const -> bool;
  auto interruptPending() const -> bool;

  // external interrupt pins (level model; cleared on dispatch)
  auto setNmi(bool value) -> void;
  auto setIrq(bool value) -> void;

  // disassembler / trace support
  auto disassemble() -> std::string;
  auto disassemble(uint24 address) -> std::string;
  auto opcodeName(uint8 op) -> const char*;

  // snapshot for trace logging
  struct TraceState {
    uint24 pc;
    uint16 a;
    uint16 x;
    uint16 y;
    uint16 s;
    uint16 d;
    uint8 b;
    uint8 p;
    bool e;
  };
  auto traceState() const -> TraceState;

 private:
  // ---- status register as discrete bits (bit layout of P) ----
  struct P8 {
    bool c = 0;  // carry
    bool z = 0;  // zero
    bool i = 0;  // interrupt disable
    bool d = 0;  // decimal mode
    bool x = 0;  // index register width (1 = 8-bit)
    bool m = 0;  // accumulator width (1 = 8-bit)
    bool v = 0;  // overflow
    bool n = 0;  // negative

    operator uint8() const {
      return c << 0 | z << 1 | i << 2 | d << 3 | x << 4 | m << 5 | v << 6 | n << 7;
    }
    auto operator=(uint8 data) -> P8& {
      c = data & 0x01;
      z = data & 0x02;
      i = data & 0x04;
      d = data & 0x08;
      x = data & 0x10;
      m = data & 0x20;
      v = data & 0x40;
      n = data & 0x80;
      return *this;
    }
  };

  // 16-bit register with byte views (little-endian: l = low byte)
  union r16 {
    r16() : w(0) {}
    r16(uint16 value) : w(value) {}
    auto operator=(uint16 value) -> r16& { w = value; return *this; }
    uint16 w;
    struct {
      uint8 l;
      uint8 h;
    };
  };

  // 24-bit register with word/byte views (b = bank byte)
  union r24 {
    r24() : d(0) {}
    r24(uint32 value) : d(value) {}
    auto operator=(uint32 value) -> r24& { d = value; return *this; }
    uint32 d;
    struct {
      uint16 w;
      uint16 x;
    };
    struct {
      uint8 l;
      uint8 h;
      uint8 b;
      uint8 y;
    };
  };

  // ---- register file ----
  r24 PC;   // program counter (b = bank, w = offset)
  r16 A;    // accumulator
  r16 X;    // index X
  r16 Y;    // index Y
  r16 Z;    // always zero (STZ source)
  r16 S;    // stack pointer
  r16 D;    // direct page
  uint8 B;  // data bank register
  P8 P;     // processor status
  bool E = 0;  // emulation mode
  bool nmi = 0;  // NMI pin (cleared on dispatch, NMI > IRQ priority)
  bool irq = 0;  // IRQ pin
  bool wai = 0;  // raised during WAI
  bool stp = 0;  // raised during STP
  uint16 vector = 0xfffc;  // interrupt vector
  r24 U;    // temporary
  r24 V;    // temporary
  r24 W;    // temporary

  Memory& mem_;
  uint64 cycles_ = 0;

  // ---- 1-cycle memory operations ----
  auto idle() -> uint8;
  auto read(uint24 address) -> uint8;
  auto write(uint24 address, uint8 data) -> void;
  auto fetch() -> uint8;   // read PC, increment
  auto pull() -> uint8;    // read S, increment (E-mode wraps at $01ff)
  auto push(uint8 data) -> void;
  auto pullN() -> uint8;   // like pull but never wraps in E mode
  auto pushN(uint8 data) -> void;
  auto readDirect(uint16 offset) -> uint8;
  auto writeDirect(uint16 offset, uint8 data) -> void;
  auto readDirectN(uint16 offset) -> uint8;  // no E-mode wrap quirk
  auto readDirectX(uint16 address, uint16 offset) -> uint8;  // (dp,X) high-byte page wrap in E mode
  auto readBank(uint24 address) -> uint8;
  auto writeBank(uint24 address, uint8 data) -> void;
  auto readStack(uint16 offset) -> uint8;
  auto writeStack(uint16 offset, uint8 data) -> void;

  auto idle2() -> void;  // extra cycle if D.l != 0
  auto idle4(uint16 x, uint16 y) -> void;  // extra cycle on page cross
  auto idle6(uint16 address) -> void;  // extra cycle if E and page cross
  auto idleIRQ() -> void;

  // ---- algorithms (see algorithms.cpp) ----
  auto algorithmADC8(uint8) -> uint8;
  auto algorithmADC16(uint16) -> uint16;
  auto algorithmAND8(uint8) -> uint8;
  auto algorithmAND16(uint16) -> uint16;
  auto algorithmASL8(uint8) -> uint8;
  auto algorithmASL16(uint16) -> uint16;
  auto algorithmBIT8(uint8) -> uint8;
  auto algorithmBIT16(uint16) -> uint16;
  auto algorithmCMP8(uint8) -> uint8;
  auto algorithmCMP16(uint16) -> uint16;
  auto algorithmCPX8(uint8) -> uint8;
  auto algorithmCPX16(uint16) -> uint16;
  auto algorithmCPY8(uint8) -> uint8;
  auto algorithmCPY16(uint16) -> uint16;
  auto algorithmDEC8(uint8) -> uint8;
  auto algorithmDEC16(uint16) -> uint16;
  auto algorithmEOR8(uint8) -> uint8;
  auto algorithmEOR16(uint16) -> uint16;
  auto algorithmINC8(uint8) -> uint8;
  auto algorithmINC16(uint16) -> uint16;
  auto algorithmLDA8(uint8) -> uint8;
  auto algorithmLDA16(uint16) -> uint16;
  auto algorithmLDX8(uint8) -> uint8;
  auto algorithmLDX16(uint16) -> uint16;
  auto algorithmLDY8(uint8) -> uint8;
  auto algorithmLDY16(uint16) -> uint16;
  auto algorithmLSR8(uint8) -> uint8;
  auto algorithmLSR16(uint16) -> uint16;
  auto algorithmORA8(uint8) -> uint8;
  auto algorithmORA16(uint16) -> uint16;
  auto algorithmROL8(uint8) -> uint8;
  auto algorithmROL16(uint16) -> uint16;
  auto algorithmROR8(uint8) -> uint8;
  auto algorithmROR16(uint16) -> uint16;
  auto algorithmSBC8(uint8) -> uint8;
  auto algorithmSBC16(uint16) -> uint16;
  auto algorithmTRB8(uint8) -> uint8;
  auto algorithmTRB16(uint16) -> uint16;
  auto algorithmTSB8(uint8) -> uint8;
  auto algorithmTSB16(uint16) -> uint16;

  // ---- instruction families (addressing modes; see addressing_modes.cpp) ----
  auto instructionImmediateRead8(alu8) -> void;
  auto instructionImmediateRead16(alu16) -> void;
  auto instructionBankRead8(alu8) -> void;
  auto instructionBankRead16(alu16) -> void;
  auto instructionBankRead8(alu8, r16) -> void;
  auto instructionBankRead16(alu16, r16) -> void;
  auto instructionLongRead8(alu8, r16 = r16{}) -> void;
  auto instructionLongRead16(alu16, r16 = r16{}) -> void;
  auto instructionDirectRead8(alu8) -> void;
  auto instructionDirectRead16(alu16) -> void;
  auto instructionDirectRead8(alu8, r16) -> void;
  auto instructionDirectRead16(alu16, r16) -> void;
  auto instructionIndirectRead8(alu8) -> void;
  auto instructionIndirectRead16(alu16) -> void;
  auto instructionIndexedIndirectRead8(alu8) -> void;
  auto instructionIndexedIndirectRead16(alu16) -> void;
  auto instructionIndirectIndexedRead8(alu8) -> void;
  auto instructionIndirectIndexedRead16(alu16) -> void;
  auto instructionIndirectLongRead8(alu8, r16 = r16{}) -> void;
  auto instructionIndirectLongRead16(alu16, r16 = r16{}) -> void;
  auto instructionStackRead8(alu8) -> void;
  auto instructionStackRead16(alu16) -> void;
  auto instructionIndirectStackRead8(alu8) -> void;
  auto instructionIndirectStackRead16(alu16) -> void;

  auto instructionBankWrite8(r16) -> void;
  auto instructionBankWrite16(r16) -> void;
  auto instructionBankWrite8(r16, r16) -> void;
  auto instructionBankWrite16(r16, r16) -> void;
  auto instructionLongWrite8(r16 = r16{}) -> void;
  auto instructionLongWrite16(r16 = r16{}) -> void;
  auto instructionDirectWrite8(r16) -> void;
  auto instructionDirectWrite16(r16) -> void;
  auto instructionDirectWrite8(r16, r16) -> void;
  auto instructionDirectWrite16(r16, r16) -> void;
  auto instructionIndirectWrite8() -> void;
  auto instructionIndirectWrite16() -> void;
  auto instructionIndexedIndirectWrite8() -> void;
  auto instructionIndexedIndirectWrite16() -> void;
  auto instructionIndirectIndexedWrite8() -> void;
  auto instructionIndirectIndexedWrite16() -> void;
  auto instructionIndirectLongWrite8(r16 = r16{}) -> void;
  auto instructionIndirectLongWrite16(r16 = r16{}) -> void;
  auto instructionStackWrite8() -> void;
  auto instructionStackWrite16() -> void;
  auto instructionIndirectStackWrite8() -> void;
  auto instructionIndirectStackWrite16() -> void;

  auto instructionImpliedModify8(alu8, r16&) -> void;
  auto instructionImpliedModify16(alu16, r16&) -> void;
  auto instructionBankModify8(alu8) -> void;
  auto instructionBankModify16(alu16) -> void;
  auto instructionBankIndexedModify8(alu8) -> void;
  auto instructionBankIndexedModify16(alu16) -> void;
  auto instructionDirectModify8(alu8) -> void;
  auto instructionDirectModify16(alu16) -> void;
  auto instructionDirectIndexedModify8(alu8) -> void;
  auto instructionDirectIndexedModify16(alu16) -> void;

  auto instructionBranch(bool take = true) -> void;
  auto instructionBranchLong() -> void;
  auto instructionJumpShort() -> void;
  auto instructionJumpLong() -> void;
  auto instructionJumpIndirect() -> void;
  auto instructionJumpIndexedIndirect() -> void;
  auto instructionJumpIndirectLong() -> void;
  auto instructionCallShort() -> void;
  auto instructionCallLong() -> void;
  auto instructionCallIndexedIndirect() -> void;
  auto instructionReturnInterrupt() -> void;
  auto instructionReturnShort() -> void;
  auto instructionReturnLong() -> void;

  auto instructionBitImmediate8() -> void;
  auto instructionBitImmediate16() -> void;
  auto instructionNoOperation() -> void;
  auto instructionPrefix() -> void;
  auto instructionExchangeBA() -> void;
  auto instructionBlockMove8(int8 adjust) -> void;
  auto instructionBlockMove16(int8 adjust) -> void;
  auto instructionInterrupt(r16 vector) -> void;
  auto interrupt() -> void;  // external interrupt dispatch (NMI/IRQ)
  auto instructionStop() -> void;
  auto instructionWait() -> void;
  auto instructionExchangeCE() -> void;
  auto instructionSetFlag(bool& flag) -> void;
  auto instructionClearFlag(bool& flag) -> void;
  auto instructionResetP() -> void;
  auto instructionSetP() -> void;
  auto instructionTransfer8(r16 F, r16& T) -> void;
  auto instructionTransfer16(r16 F, r16& T) -> void;
  auto instructionTransferCS() -> void;
  auto instructionTransferSX8() -> void;
  auto instructionTransferSX16() -> void;
  auto instructionTransferXS() -> void;
  auto instructionPush8(r16 F) -> void;
  auto instructionPush16(r16 F) -> void;
  auto instructionPushD() -> void;
  auto instructionPull8(r16& T) -> void;
  auto instructionPull16(r16& T) -> void;
  auto instructionPullD() -> void;
  auto instructionPullB() -> void;
  auto instructionPullP() -> void;
  auto instructionPushEffectiveAddress() -> void;
  auto instructionPushEffectiveIndirectAddress() -> void;
  auto instructionPushEffectiveRelativeAddress() -> void;

  auto instruction() -> void;  // 4-way dispatch (opcodes.cpp)
};

// flag access macros used by the implementation translation units
#define CF P.c
#define ZF P.z
#define IF P.i
#define DF P.d
#define XF P.x
#define MF P.m
#define VF P.v
#define NF P.n
#define EF E

#define alu(...) (this->*op)(__VA_ARGS__)

}  // namespace snes