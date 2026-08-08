#include "cpu65816.hpp"

//====================================================================
// Read-type instruction families (addressing modes), ported from the
// higan/bsnes WDC65816 core (instructions-read.cpp). Each bus access
// and each dummy cycle costs exactly one CPU cycle.
//====================================================================

namespace snes {

auto Cpu65816::instructionImmediateRead8(alu8 op) -> void {
  W.l = fetch();
  alu(W.l);
}

auto Cpu65816::instructionImmediateRead16(alu16 op) -> void {
  W.l = fetch();
  W.h = fetch();
  alu(W.w);
}

auto Cpu65816::instructionBankRead8(alu8 op) -> void {
  V.l = fetch();
  V.h = fetch();
  W.l = readBank(V.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionBankRead16(alu16 op) -> void {
  V.l = fetch();
  V.h = fetch();
  W.l = readBank(V.w + 0);
  W.h = readBank(V.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionBankRead8(alu8 op, r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  idle4(V.w, V.w + I.w);
  W.l = readBank(V.w + I.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionBankRead16(alu16 op, r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  idle4(V.w, V.w + I.w);
  W.l = readBank(V.w + I.w + 0);
  W.h = readBank(V.w + I.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionLongRead8(alu8 op, r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  V.b = fetch();
  W.l = read(V.d + I.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionLongRead16(alu16 op, r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  V.b = fetch();
  W.l = read(V.d + I.w + 0);
  W.h = read(V.d + I.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionDirectRead8(alu8 op) -> void {
  U.l = fetch();
  idle2();
  W.l = readDirect(U.l + 0);
  alu(W.l);
}

auto Cpu65816::instructionDirectRead16(alu16 op) -> void {
  U.l = fetch();
  idle2();
  W.l = readDirect(U.l + 0);
  W.h = readDirect(U.l + 1);
  alu(W.w);
}

auto Cpu65816::instructionDirectRead8(alu8 op, r16 I) -> void {
  U.l = fetch();
  idle2();
  idle();
  W.l = readDirect(U.l + I.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionDirectRead16(alu16 op, r16 I) -> void {
  U.l = fetch();
  idle2();
  idle();
  W.l = readDirect(U.l + I.w + 0);
  W.h = readDirect(U.l + I.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionIndirectRead8(alu8 op) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  W.l = readBank(V.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionIndirectRead16(alu16 op) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  W.l = readBank(V.w + 0);
  W.h = readBank(V.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionIndexedIndirectRead8(alu8 op) -> void {
  U.l = fetch();
  idle2();
  idle();
  V.l = readDirectX(U.l + X.w, 0);
  V.h = readDirectX(U.l + X.w, 1);
  W.l = readBank(V.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionIndexedIndirectRead16(alu16 op) -> void {
  U.l = fetch();
  idle2();
  idle();
  V.l = readDirectX(U.l + X.w, 0);
  V.h = readDirectX(U.l + X.w, 1);
  W.l = readBank(V.w + 0);
  W.h = readBank(V.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionIndirectIndexedRead8(alu8 op) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  idle4(V.w, V.w + Y.w);
  W.l = readBank(V.w + Y.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionIndirectIndexedRead16(alu16 op) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  idle4(V.w, V.w + Y.w);
  W.l = readBank(V.w + Y.w + 0);
  W.h = readBank(V.w + Y.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionIndirectLongRead8(alu8 op, r16 I) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirectN(U.l + 0);
  V.h = readDirectN(U.l + 1);
  V.b = readDirectN(U.l + 2);
  W.l = read(V.d + I.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionIndirectLongRead16(alu16 op, r16 I) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirectN(U.l + 0);
  V.h = readDirectN(U.l + 1);
  V.b = readDirectN(U.l + 2);
  W.l = read(V.d + I.w + 0);
  W.h = read(V.d + I.w + 1);
  alu(W.w);
}

auto Cpu65816::instructionStackRead8(alu8 op) -> void {
  U.l = fetch();
  idle();
  W.l = readStack(U.l + 0);
  alu(W.l);
}

auto Cpu65816::instructionStackRead16(alu16 op) -> void {
  U.l = fetch();
  idle();
  W.l = readStack(U.l + 0);
  W.h = readStack(U.l + 1);
  alu(W.w);
}

auto Cpu65816::instructionIndirectStackRead8(alu8 op) -> void {
  U.l = fetch();
  idle();
  V.l = readStack(U.l + 0);
  V.h = readStack(U.l + 1);
  idle();
  W.l = readBank(V.w + Y.w + 0);
  alu(W.l);
}

auto Cpu65816::instructionIndirectStackRead16(alu16 op) -> void {
  U.l = fetch();
  idle();
  V.l = readStack(U.l + 0);
  V.h = readStack(U.l + 1);
  idle();
  W.l = readBank(V.w + Y.w + 0);
  W.h = readBank(V.w + Y.w + 1);
  alu(W.w);
}

}  // namespace snes