#include "cpu65816.hpp"

//====================================================================
// Miscellaneous instructions (status, transfers, pushes/pulls, block
// moves, interrupts, STP/WAI), ported from the higan/bsnes WDC65816
// core (instructions-other.cpp).
//====================================================================

namespace snes {

auto Cpu65816::instructionBitImmediate8() -> void {
  U.l = fetch();
  ZF = (U.l & A.l) == 0;
}

auto Cpu65816::instructionBitImmediate16() -> void {
  U.l = fetch();
  U.h = fetch();
  ZF = (U.w & A.w) == 0;
}

auto Cpu65816::instructionNoOperation() -> void {
  idleIRQ();
}

auto Cpu65816::instructionPrefix() -> void {
  fetch();
}

auto Cpu65816::instructionExchangeBA() -> void {
  idle();
  idle();
  A.w = A.w >> 8 | A.w << 8;
  ZF = A.l == 0;
  NF = A.l & 0x80;
}

auto Cpu65816::instructionBlockMove8(int8 adjust) -> void {
  U.b = fetch();
  V.b = fetch();
  B = U.b;
  W.l = read(V.b << 16 | X.w);
  write(U.b << 16 | Y.w, W.l);
  idle();
  X.l += adjust;
  Y.l += adjust;
  idle();
  if (A.w--) PC.w -= 3;
}

auto Cpu65816::instructionBlockMove16(int8 adjust) -> void {
  U.b = fetch();
  V.b = fetch();
  B = U.b;
  W.l = read(V.b << 16 | X.w);
  write(U.b << 16 | Y.w, W.l);
  idle();
  X.w += adjust;
  Y.w += adjust;
  idle();
  if (A.w--) PC.w -= 3;
}

auto Cpu65816::instructionInterrupt(r16 vector) -> void {
  fetch();
  if (!EF) push(PC.b);
  push(PC.h);
  push(PC.l);
  push(P);
  IF = 1;
  DF = 0;
  PC.l = read(vector.w + 0);
  PC.h = read(vector.w + 1);
  PC.b = 0x00;
}

auto Cpu65816::instructionStop() -> void {
  stp = true;
}

auto Cpu65816::instructionWait() -> void {
  wai = true;
}

auto Cpu65816::instructionExchangeCE() -> void {
  idleIRQ();
  bool t = CF;
  CF = EF;
  E = t;
  if (EF) {
    XF = 1;
    MF = 1;
    X.h = 0x00;
    Y.h = 0x00;
    S.h = 0x01;
  }
}

auto Cpu65816::instructionSetFlag(bool& flag) -> void {
  idleIRQ();
  flag = 1;
}

auto Cpu65816::instructionClearFlag(bool& flag) -> void {
  idleIRQ();
  flag = 0;
}

auto Cpu65816::instructionResetP() -> void {
  W.l = fetch();
  idle();
  P = P & ~W.l;
  if (EF) { XF = 1; MF = 1; }
  if (XF) { X.h = 0x00; Y.h = 0x00; }
}

auto Cpu65816::instructionSetP() -> void {
  W.l = fetch();
  idle();
  P = P | W.l;
  if (EF) { XF = 1; MF = 1; }
  if (XF) { X.h = 0x00; Y.h = 0x00; }
}

auto Cpu65816::instructionTransfer8(r16 F, r16& T) -> void {
  idleIRQ();
  T.l = F.l;
  ZF = T.l == 0;
  NF = T.l & 0x80;
}

auto Cpu65816::instructionTransfer16(r16 F, r16& T) -> void {
  idleIRQ();
  T.w = F.w;
  ZF = T.w == 0;
  NF = T.w & 0x8000;
}

auto Cpu65816::instructionTransferCS() -> void {
  idleIRQ();
  S.w = A.w;
  if (EF) S.h = 0x01;
}

auto Cpu65816::instructionTransferSX8() -> void {
  idleIRQ();
  X.l = S.l;
  ZF = X.l == 0;
  NF = X.l & 0x80;
}

auto Cpu65816::instructionTransferSX16() -> void {
  idleIRQ();
  X.w = S.w;
  ZF = X.w == 0;
  NF = X.w & 0x8000;
}

auto Cpu65816::instructionTransferXS() -> void {
  idleIRQ();
  if (EF) { S.l = X.l; }
  else { S.w = X.w; }
}

auto Cpu65816::instructionPush8(r16 F) -> void {
  idle();
  push(F.l);
}

auto Cpu65816::instructionPush16(r16 F) -> void {
  idle();
  push(F.h);
  push(F.l);
}

auto Cpu65816::instructionPushD() -> void {
  idle();
  pushN(D.h);
  pushN(D.l);
  if (EF) S.h = 0x01;
}

auto Cpu65816::instructionPull8(r16& T) -> void {
  idle();
  idle();
  T.l = pull();
  ZF = T.l == 0;
  NF = T.l & 0x80;
}

auto Cpu65816::instructionPull16(r16& T) -> void {
  idle();
  idle();
  T.l = pull();
  T.h = pull();
  ZF = T.w == 0;
  NF = T.w & 0x8000;
}

auto Cpu65816::instructionPullD() -> void {
  idle();
  idle();
  D.l = pullN();
  D.h = pullN();
  ZF = D.w == 0;
  NF = D.w & 0x8000;
  if (EF) S.h = 0x01;
}

auto Cpu65816::instructionPullB() -> void {
  idle();
  idle();
  B = pullN();
  ZF = B == 0;
  NF = B & 0x80;
  if (EF) S.h = 0x01;
}

auto Cpu65816::instructionPullP() -> void {
  idle();
  idle();
  P = pull();
  if (EF) { XF = 1; MF = 1; }
  if (XF) { X.h = 0x00; Y.h = 0x00; }
}

auto Cpu65816::instructionPushEffectiveAddress() -> void {
  W.l = fetch();
  W.h = fetch();
  pushN(W.h);
  pushN(W.l);
  if (EF) S.h = 0x01;
}

auto Cpu65816::instructionPushEffectiveIndirectAddress() -> void {
  U.l = fetch();
  idle2();
  W.l = readDirectN(U.l + 0);
  W.h = readDirectN(U.l + 1);
  pushN(W.h);
  pushN(W.l);
  if (EF) S.h = 0x01;
}

auto Cpu65816::instructionPushEffectiveRelativeAddress() -> void {
  V.l = fetch();
  V.h = fetch();
  idle();
  W.w = PC.d + (int16)V.w;
  pushN(W.h);
  pushN(W.l);
  if (EF) S.h = 0x01;
}

}  // namespace snes