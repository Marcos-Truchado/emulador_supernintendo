#include "cpu65816.hpp"

//====================================================================
// Write-type instruction families, ported from the higan/bsnes
// WDC65816 core (instructions-write.cpp).
//====================================================================

namespace snes {

auto Cpu65816::instructionBankWrite8(r16 F) -> void {
  V.l = fetch();
  V.h = fetch();
  writeBank(V.w + 0, F.l);
}

auto Cpu65816::instructionBankWrite16(r16 F) -> void {
  V.l = fetch();
  V.h = fetch();
  writeBank(V.w + 0, F.l);
  writeBank(V.w + 1, F.h);
}

auto Cpu65816::instructionBankWrite8(r16 F, r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  idle();
  writeBank(V.w + I.w + 0, F.l);
}

auto Cpu65816::instructionBankWrite16(r16 F, r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  idle();
  writeBank(V.w + I.w + 0, F.l);
  writeBank(V.w + I.w + 1, F.h);
}

auto Cpu65816::instructionLongWrite8(r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  V.b = fetch();
  write(V.d + I.w + 0, A.l);
}

auto Cpu65816::instructionLongWrite16(r16 I) -> void {
  V.l = fetch();
  V.h = fetch();
  V.b = fetch();
  write(V.d + I.w + 0, A.l);
  write(V.d + I.w + 1, A.h);
}

auto Cpu65816::instructionDirectWrite8(r16 F) -> void {
  U.l = fetch();
  idle2();
  writeDirect(U.l + 0, F.l);
}

auto Cpu65816::instructionDirectWrite16(r16 F) -> void {
  U.l = fetch();
  idle2();
  writeDirect(U.l + 0, F.l);
  writeDirect(U.l + 1, F.h);
}

auto Cpu65816::instructionDirectWrite8(r16 F, r16 I) -> void {
  U.l = fetch();
  idle2();
  idle();
  writeDirect(U.l + I.w + 0, F.l);
}

auto Cpu65816::instructionDirectWrite16(r16 F, r16 I) -> void {
  U.l = fetch();
  idle2();
  idle();
  writeDirect(U.l + I.w + 0, F.l);
  writeDirect(U.l + I.w + 1, F.h);
}

auto Cpu65816::instructionIndirectWrite8() -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  writeBank(V.w + 0, A.l);
}

auto Cpu65816::instructionIndirectWrite16() -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  writeBank(V.w + 0, A.l);
  writeBank(V.w + 1, A.h);
}

auto Cpu65816::instructionIndexedIndirectWrite8() -> void {
  U.l = fetch();
  idle2();
  idle();
  V.l = readDirectX(U.l + X.w, 0);
  V.h = readDirectX(U.l + X.w, 1);
  writeBank(V.w + 0, A.l);
}

auto Cpu65816::instructionIndexedIndirectWrite16() -> void {
  U.l = fetch();
  idle2();
  idle();
  V.l = readDirectX(U.l + X.w, 0);
  V.h = readDirectX(U.l + X.w, 1);
  writeBank(V.w + 0, A.l);
  writeBank(V.w + 1, A.h);
}

auto Cpu65816::instructionIndirectIndexedWrite8() -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  idle();
  writeBank(V.w + Y.w + 0, A.l);
}

auto Cpu65816::instructionIndirectIndexedWrite16() -> void {
  U.l = fetch();
  idle2();
  V.l = readDirect(U.l + 0);
  V.h = readDirect(U.l + 1);
  idle();
  writeBank(V.w + Y.w + 0, A.l);
  writeBank(V.w + Y.w + 1, A.h);
}

auto Cpu65816::instructionIndirectLongWrite8(r16 I) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirectN(U.l + 0);
  V.h = readDirectN(U.l + 1);
  V.b = readDirectN(U.l + 2);
  write(V.d + I.w + 0, A.l);
}

auto Cpu65816::instructionIndirectLongWrite16(r16 I) -> void {
  U.l = fetch();
  idle2();
  V.l = readDirectN(U.l + 0);
  V.h = readDirectN(U.l + 1);
  V.b = readDirectN(U.l + 2);
  write(V.d + I.w + 0, A.l);
  write(V.d + I.w + 1, A.h);
}

auto Cpu65816::instructionStackWrite8() -> void {
  U.l = fetch();
  idle();
  writeStack(U.l + 0, A.l);
}

auto Cpu65816::instructionStackWrite16() -> void {
  U.l = fetch();
  idle();
  writeStack(U.l + 0, A.l);
  writeStack(U.l + 1, A.h);
}

auto Cpu65816::instructionIndirectStackWrite8() -> void {
  U.l = fetch();
  idle();
  V.l = readStack(U.l + 0);
  V.h = readStack(U.l + 1);
  idle();
  writeBank(V.w + Y.w + 0, A.l);
}

auto Cpu65816::instructionIndirectStackWrite16() -> void {
  U.l = fetch();
  idle();
  V.l = readStack(U.l + 0);
  V.h = readStack(U.l + 1);
  idle();
  writeBank(V.w + Y.w + 0, A.l);
  writeBank(V.w + Y.w + 1, A.h);
}

}  // namespace snes