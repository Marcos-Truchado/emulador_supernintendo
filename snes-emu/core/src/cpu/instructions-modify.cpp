#include "cpu65816.hpp"

//====================================================================
// Read-modify-write type instruction families (ASL/LSR/ROL/ROR/INC/DEC
// and the TRB/TSB memory ops), ported from the higan/bsnes WDC65816
// core (instructions-modify.cpp).
//====================================================================

namespace snes {

auto Cpu65816::instructionImpliedModify8(alu8 op, r16& M) -> void {
  idleIRQ();
  M.l = alu(M.l);
}

auto Cpu65816::instructionImpliedModify16(alu16 op, r16& M) -> void {
  idleIRQ();
  M.w = alu(M.w);
}

auto Cpu65816::instructionBankModify8(alu8 op) -> void {
  V.l = fetch();
  V.h = fetch();
  W.l = readBank(V.w + 0);
  idle();
  W.l = alu(W.l);
  writeBank(V.w + 0, W.l);
}

auto Cpu65816::instructionBankModify16(alu16 op) -> void {
  V.l = fetch();
  V.h = fetch();
  W.l = readBank(V.w + 0);
  W.h = readBank(V.w + 1);
  idle();
  W.w = alu(W.w);
  writeBank(V.w + 1, W.h);
  writeBank(V.w + 0, W.l);
}

auto Cpu65816::instructionBankIndexedModify8(alu8 op) -> void {
  V.l = fetch();
  V.h = fetch();
  idle();
  W.l = readBank(V.w + X.w + 0);
  idle();
  W.l = alu(W.l);
  writeBank(V.w + X.w + 0, W.l);
}

auto Cpu65816::instructionBankIndexedModify16(alu16 op) -> void {
  V.l = fetch();
  V.h = fetch();
  idle();
  W.l = readBank(V.w + X.w + 0);
  W.h = readBank(V.w + X.w + 1);
  idle();
  W.w = alu(W.w);
  writeBank(V.w + X.w + 1, W.h);
  writeBank(V.w + X.w + 0, W.l);
}

auto Cpu65816::instructionDirectModify8(alu8 op) -> void {
  U.l = fetch();
  idle2();
  W.l = readDirect(U.l + 0);
  idle();
  W.l = alu(W.l);
  writeDirect(U.l + 0, W.l);
}

auto Cpu65816::instructionDirectModify16(alu16 op) -> void {
  U.l = fetch();
  idle2();
  W.l = readDirect(U.l + 0);
  W.h = readDirect(U.l + 1);
  idle();
  W.w = alu(W.w);
  writeDirect(U.l + 1, W.h);
  writeDirect(U.l + 0, W.l);
}

auto Cpu65816::instructionDirectIndexedModify8(alu8 op) -> void {
  U.l = fetch();
  idle2();
  idle();
  W.l = readDirect(U.l + X.w + 0);
  idle();
  W.l = alu(W.l);
  writeDirect(U.l + X.w + 0, W.l);
}

auto Cpu65816::instructionDirectIndexedModify16(alu16 op) -> void {
  U.l = fetch();
  idle2();
  idle();
  W.l = readDirect(U.l + X.w + 0);
  W.h = readDirect(U.l + X.w + 1);
  idle();
  W.w = alu(W.w);
  writeDirect(U.l + X.w + 1, W.h);
  writeDirect(U.l + X.w + 0, W.l);
}

}  // namespace snes