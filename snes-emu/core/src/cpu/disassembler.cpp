#include "cpu65816.hpp"

#include <cstdio>
#include <string>

//====================================================================
// Disassembler + trace helpers, ported from the higan/bsnes WDC65816
// core (disassembler.cpp). Format: "mnemonic operand [effective]".
// I/O register reads are masked out ($2000-$5fff in the $00-3f/$80-bf
// banks) to avoid side effects when tracing.
//====================================================================

namespace snes {

namespace {

std::string hex4(uint16 v) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%04x", v);
  return buf;
}

std::string hex6(uint32 v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%06x", v & 0xffffff);
  return buf;
}

std::string hex2(uint8 v) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02x", v);
  return buf;
}

}  // namespace

auto Cpu65816::disassemble(uint24 address) -> std::string {
  auto read = [&](uint24 a) -> uint8 {
    if ((a & 0x40ffff) >= 0x2000 && (a & 0x40ffff) <= 0x5fff) return 0x00;
    return mem_.read(a & 0xffffff);
  };
  auto readByte = [&](uint24 a) -> uint8 { return read(a); };
  auto readWord = [&](uint24 a) -> uint16 {
    return uint16(readByte(a + 0)) | uint16(readByte(a + 1)) << 8;
  };
  auto readLong = [&](uint24 a) -> uint24 {
    return uint24(readByte(a + 0)) | uint24(readWord(a + 1)) << 8;
  };

  uint24 pc = address & 0xffffff;  // instruction start (branch targets anchor here)
  uint24 cursor = pc;              // advances over operands
  auto next = [&]() -> uint8 {
    uint8 data = readByte(cursor);
    cursor = (cursor & 0xff0000) | uint16(cursor + 1);
    return data;
  };

  uint8 opcode = next();
  uint8 operand0 = next();
  uint8 operand1 = next();
  uint8 operand2 = next();

  uint8 operandByte = operand0;
  uint16 operandWord = uint16(operand0) | uint16(operand1) << 8;
  uint24 operandLong = uint24(operand0) | uint24(operand1) << 8 | uint24(operand2) << 16;

  uint24 effective = 0;
  bool hasEffective = false;
  auto set = [&](uint24 value) { effective = value; hasEffective = true; };

  std::string name;
  std::string operand;

  auto absolute = [&]() -> std::string { set((B << 16) | operandWord); return "$" + hex4(operandWord); };
  auto absolutePC = [&]() -> std::string { set((pc >> 16) << 16 | operandWord); return "$" + hex4(operandWord); };
  auto absoluteX = [&]() -> std::string { set((B << 16) + operandWord + X.w); return "$" + hex4(operandWord) + ",x"; };
  auto absoluteY = [&]() -> std::string { set((B << 16) + operandWord + Y.w); return "$" + hex4(operandWord) + ",y"; };
  auto absoluteLong = [&]() -> std::string { set(operandLong); return "$" + hex6(operandLong); };
  auto absoluteLongX = [&]() -> std::string { set(operandLong + X.w); return "$" + hex6(operandLong) + ",x"; };
  auto direct = [&]() -> std::string { set(uint16(D.w + operandByte)); return "$" + hex2(operandByte); };
  auto directX = [&]() -> std::string { set(uint16(D.w + operandByte + X.w)); return "$" + hex2(operandByte) + ",x"; };
  auto directY = [&]() -> std::string { set(uint16(D.w + operandByte + Y.w)); return "$" + hex2(operandByte) + ",y"; };
  auto immediate = [&]() -> std::string { return "#$" + hex2(operandByte); };
  auto immediateA = [&]() -> std::string { return MF ? "#$" + hex2(operandByte) : "#$" + hex4(operandWord); };
  auto immediateX = [&]() -> std::string { return XF ? "#$" + hex2(operandByte) : "#$" + hex4(operandWord); };
  auto implied = [&]() -> std::string { return ""; };
  auto indexedIndirectX = [&]() -> std::string {
    set((B << 16) + readWord(uint16(D.w + operandByte + X.w)));
    return "($" + hex2(operandByte) + ",x)";
  };
  auto indirect = [&]() -> std::string {
    set((B << 16) + readWord(uint16(D.w + operandByte)));
    return "($" + hex2(operandByte) + ")";
  };
  auto indirectPC = [&]() -> std::string {
    set((pc >> 16) << 16 | readWord(operandWord));
    return "($" + hex4(operandWord) + ")";
  };
  auto indirectX = [&]() -> std::string {
    set((pc >> 16) << 16 | readWord(uint16(operandWord + X.w)));
    return "($" + hex4(operandWord) + ",x)";
  };
  auto indirectIndexedY = [&]() -> std::string {
    set((B << 16) + readWord(uint16(D.w + operandByte)) + Y.w);
    return "($" + hex2(operandByte) + "),y";
  };
  auto indirectLong = [&]() -> std::string {
    set(readLong(uint16(D.w + operandByte)));
    return "[$" + hex2(operandByte) + "]";
  };
  auto indirectLongPC = [&]() -> std::string {
    set(readLong(operandWord));
    return "[$" + hex4(operandWord) + "]";
  };
  auto indirectLongY = [&]() -> std::string {
    set(readLong(uint16(D.w + operandByte)) + Y.w);
    return "[$" + hex2(operandByte) + "],y";
  };
  auto move = [&]() -> std::string { return "$" + hex2(operand0) + "=$" + hex2(operand1); };
  auto relative = [&]() -> std::string {
    set(((pc >> 16) << 16) | uint16(pc + 2 + (int8)operandByte));
    return "$" + hex4(effective);
  };
  auto relativeWord = [&]() -> std::string {
    set(((pc >> 16) << 16) | uint16(pc + 3 + (int16)operandWord));
    return "$" + hex4(effective);
  };
  auto stack = [&]() -> std::string { set(uint16(S.w + operandByte)); return "$" + hex2(operandByte) + ",s"; };
  auto stackIndirect = [&]() -> std::string {
    set((B << 16) + readWord(uint16(S.w + operandByte)) + Y.w);
    return "($" + hex2(operandByte) + ",s),y";
  };

  #define op(id, label, function) case id: name = label; operand = function(); break;
  switch (opcode) {
  op(0x00, "brk", immediate)          op(0x01, "ora", indexedIndirectX)
  op(0x02, "cop", immediate)          op(0x03, "ora", stack)
  op(0x04, "tsb", direct)             op(0x05, "ora", direct)
  op(0x06, "asl", direct)             op(0x07, "ora", indirectLong)
  op(0x08, "php", implied)            op(0x09, "ora", immediateA)
  op(0x0a, "asl", implied)            op(0x0b, "phd", implied)
  op(0x0c, "tsb", absolute)           op(0x0d, "ora", absolute)
  op(0x0e, "asl", absolute)           op(0x0f, "ora", absoluteLong)
  op(0x10, "bpl", relative)           op(0x11, "ora", indirectIndexedY)
  op(0x12, "ora", indirect)           op(0x13, "ora", stackIndirect)
  op(0x14, "trb", direct)             op(0x15, "ora", directX)
  op(0x16, "asl", directX)            op(0x17, "ora", indirectLongY)
  op(0x18, "clc", implied)            op(0x19, "ora", absoluteY)
  op(0x1a, "inc", implied)            op(0x1b, "tcs", implied)
  op(0x1c, "trb", absolute)           op(0x1d, "ora", absoluteX)
  op(0x1e, "asl", absoluteX)          op(0x1f, "ora", absoluteLongX)

  op(0x20, "jsr", absolutePC)         op(0x21, "and", indexedIndirectX)
  op(0x22, "jsl", absoluteLong)       op(0x23, "and", stack)
  op(0x24, "bit", direct)             op(0x25, "and", direct)
  op(0x26, "rol", direct)             op(0x27, "and", indirectLong)
  op(0x28, "plp", implied)            op(0x29, "and", immediateA)
  op(0x2a, "rol", implied)            op(0x2b, "pld", implied)
  op(0x2c, "bit", absolute)           op(0x2d, "and", absolute)
  op(0x2e, "rol", absolute)           op(0x2f, "and", absoluteLong)
  op(0x30, "bmi", relative)           op(0x31, "and", indirectIndexedY)
  op(0x32, "and", indirect)           op(0x33, "and", stackIndirect)
  op(0x34, "bit", directX)            op(0x35, "and", directX)
  op(0x36, "rol", directX)            op(0x37, "and", indirectLongY)
  op(0x38, "sec", implied)            op(0x39, "and", absoluteY)
  op(0x3a, "dec", implied)            op(0x3b, "tsc", implied)
  op(0x3c, "bit", absoluteX)          op(0x3d, "and", absoluteX)
  op(0x3e, "rol", absoluteX)          op(0x3f, "and", absoluteLongX)

  op(0x40, "rti", implied)            op(0x41, "eor", indexedIndirectX)
  op(0x42, "wdm", immediate)          op(0x43, "eor", stack)
  op(0x44, "mvp", move)               op(0x45, "eor", direct)
  op(0x46, "lsr", direct)             op(0x47, "eor", indirectLong)
  op(0x48, "pha", implied)            op(0x49, "eor", immediateA)
  op(0x4a, "lsr", implied)            op(0x4b, "phk", implied)
  op(0x4c, "jmp", absolutePC)         op(0x4d, "eor", absolute)
  op(0x4e, "lsr", absolute)           op(0x4f, "eor", absoluteLong)
  op(0x50, "bvc", relative)           op(0x51, "eor", indirectIndexedY)
  op(0x52, "eor", indirect)           op(0x53, "eor", stackIndirect)
  op(0x54, "mvn", move)               op(0x55, "eor", directX)
  op(0x56, "lsr", directX)            op(0x57, "eor", indirectLongY)
  op(0x58, "cli", implied)            op(0x59, "eor", absoluteY)
  op(0x5a, "phy", implied)            op(0x5b, "tcd", implied)
  op(0x5c, "jml", absoluteLong)       op(0x5d, "eor", absoluteX)
  op(0x5e, "lsr", absoluteX)          op(0x5f, "eor", absoluteLongX)

  op(0x60, "rts", implied)            op(0x61, "adc", indexedIndirectX)
  op(0x62, "per", relativeWord)       op(0x63, "adc", stack)
  op(0x64, "stz", direct)             op(0x65, "adc", direct)
  op(0x66, "ror", direct)             op(0x67, "adc", indirectLong)
  op(0x68, "pla", implied)            op(0x69, "adc", immediateA)
  op(0x6a, "ror", implied)            op(0x6b, "rtl", implied)
  op(0x6c, "jmp", indirectPC)         op(0x6d, "adc", absolute)
  op(0x6e, "ror", absolute)           op(0x6f, "adc", absoluteLong)
  op(0x70, "bvs", relative)           op(0x71, "adc", indirectIndexedY)
  op(0x72, "adc", indirect)           op(0x73, "adc", stackIndirect)
  op(0x74, "stz", absoluteX)          op(0x75, "adc", absoluteX)
  op(0x76, "ror", absoluteX)          op(0x77, "adc", indirectLongY)
  op(0x78, "sei", implied)            op(0x79, "adc", absoluteY)
  op(0x7a, "ply", implied)            op(0x7b, "tdc", implied)
  op(0x7c, "jmp", indirectX)          op(0x7d, "adc", absoluteX)
  op(0x7e, "ror", absoluteX)          op(0x7f, "adc", absoluteLongX)

  op(0x80, "bra", relative)           op(0x81, "sta", indexedIndirectX)
  op(0x82, "brl", relativeWord)       op(0x83, "sta", stack)
  op(0x84, "sty", direct)             op(0x85, "sta", direct)
  op(0x86, "stx", direct)             op(0x87, "sta", indirectLong)
  op(0x88, "dey", implied)            op(0x89, "bit", immediateA)
  op(0x8a, "txa", implied)            op(0x8b, "phb", implied)
  op(0x8c, "sty", absolute)           op(0x8d, "sta", absolute)
  op(0x8e, "stx", absolute)           op(0x8f, "sta", absoluteLong)
  op(0x90, "bcc", relative)           op(0x91, "sta", indirectIndexedY)
  op(0x92, "sta", indirect)           op(0x93, "sta", stackIndirect)
  op(0x94, "sty", directX)            op(0x95, "sta", directX)
  op(0x96, "stx", directY)            op(0x97, "sta", indirectLongY)
  op(0x98, "tya", implied)            op(0x99, "sta", absoluteY)
  op(0x9a, "txs", implied)            op(0x9b, "txy", implied)
  op(0x9c, "stz", absolute)           op(0x9d, "sta", absoluteX)
  op(0x9e, "stz", absoluteX)          op(0x9f, "sta", absoluteLongX)

  op(0xa0, "ldy", immediateX)         op(0xa1, "lda", indexedIndirectX)
  op(0xa2, "ldx", immediateX)         op(0xa3, "lda", stack)
  op(0xa4, "ldy", direct)             op(0xa5, "lda", direct)
  op(0xa6, "ldx", direct)             op(0xa7, "lda", indirectLong)
  op(0xa8, "tay", implied)            op(0xa9, "lda", immediateA)
  op(0xaa, "tax", implied)            op(0xab, "plb", implied)
  op(0xac, "ldy", absolute)           op(0xad, "lda", absolute)
  op(0xae, "ldx", absolute)           op(0xaf, "lda", absoluteLong)
  op(0xb0, "bcs", relative)           op(0xb1, "lda", indirectIndexedY)
  op(0xb2, "lda", indirect)           op(0xb3, "lda", stackIndirect)
  op(0xb4, "ldy", directX)            op(0xb5, "lda", directX)
  op(0xb6, "ldx", directY)            op(0xb7, "lda", indirectLongY)
  op(0xb8, "clv", implied)            op(0xb9, "lda", absoluteY)
  op(0xba, "tsx", implied)            op(0xbb, "tyx", implied)
  op(0xbc, "ldy", absoluteX)          op(0xbd, "lda", absoluteX)
  op(0xbe, "ldx", absoluteY)          op(0xbf, "lda", absoluteLongX)

  op(0xc0, "cpy", immediateX)         op(0xc1, "cmp", indexedIndirectX)
  op(0xc2, "rep", immediate)          op(0xc3, "cmp", stack)
  op(0xc4, "cpy", direct)             op(0xc5, "cmp", direct)
  op(0xc6, "dec", direct)             op(0xc7, "cmp", indirectLong)
  op(0xc8, "iny", implied)            op(0xc9, "cmp", immediateA)
  op(0xca, "dex", implied)            op(0xcb, "wai", implied)
  op(0xcc, "cpy", absolute)           op(0xcd, "cmp", absolute)
  op(0xce, "dec", absolute)           op(0xcf, "cmp", absoluteLong)
  op(0xd0, "bne", relative)           op(0xd1, "cmp", indirectIndexedY)
  op(0xd2, "cmp", indirect)           op(0xd3, "cmp", stackIndirect)
  op(0xd4, "pei", indirect)           op(0xd5, "cmp", directX)
  op(0xd6, "dec", directX)            op(0xd7, "cmp", indirectLongY)
  op(0xd8, "cld", implied)            op(0xd9, "cmp", absoluteY)
  op(0xda, "phx", implied)            op(0xdb, "stp", implied)
  op(0xdc, "jmp", indirectLongPC)     op(0xdd, "cmp", absoluteX)
  op(0xde, "dec", absoluteX)          op(0xdf, "cmp", absoluteLongX)

  op(0xe0, "cpx", immediateX)         op(0xe1, "sbc", indexedIndirectX)
  op(0xe2, "sep", immediate)          op(0xe3, "sbc", stack)
  op(0xe4, "cpx", direct)             op(0xe5, "sbc", direct)
  op(0xe6, "inc", direct)             op(0xe7, "sbc", indirectLong)
  op(0xe8, "inx", implied)            op(0xe9, "sbc", immediateA)
  op(0xea, "nop", implied)            op(0xeb, "xba", implied)
  op(0xec, "cpx", absolute)           op(0xed, "sbc", absolute)
  op(0xee, "inc", absolute)           op(0xef, "sbc", absoluteLong)
  op(0xf0, "beq", relative)           op(0xf1, "sbc", indirectIndexedY)
  op(0xf2, "sbc", indirect)           op(0xf3, "sbc", stackIndirect)
  op(0xf4, "pea", absolute)           op(0xf5, "sbc", directX)
  op(0xf6, "inc", directX)            op(0xf7, "sbc", indirectLongY)
  op(0xf8, "sed", implied)            op(0xf9, "sbc", absoluteY)
  op(0xfa, "plx", implied)            op(0xfb, "xce", implied)
  op(0xfc, "jsr", indirectX)          op(0xfd, "sbc", absoluteX)
  op(0xfe, "inc", absoluteX)          op(0xff, "sbc", absoluteLongX)
  }
  #undef op

  std::string s = name;
  if (!operand.empty()) s += " " + operand;
  while (s.size() < 23) s += " ";
  if (hasEffective) s += "[" + hex6(effective) + "]";
  return s;
}

auto Cpu65816::disassemble() -> std::string {
  return disassemble(PC.d);
}

auto Cpu65816::opcodeName(uint8 op) -> const char* {
  static const char* names[256] = {
    "brk", "ora", "cop", "ora", "tsb", "ora", "asl", "ora",
    "php", "ora", "asl", "phd", "tsb", "ora", "asl", "ora",
    "bpl", "ora", "ora", "ora", "trb", "ora", "asl", "ora",
    "clc", "ora", "inc", "tcs", "trb", "ora", "asl", "ora",
    "jsr", "and", "jsl", "and", "bit", "and", "rol", "and",
    "plp", "and", "rol", "pld", "bit", "and", "rol", "and",
    "bmi", "and", "and", "and", "bit", "and", "rol", "and",
    "sec", "and", "dec", "tsc", "bit", "and", "rol", "and",
    "rti", "eor", "wdm", "eor", "mvp", "eor", "lsr", "eor",
    "pha", "eor", "lsr", "phk", "jmp", "eor", "lsr", "eor",
    "bvc", "eor", "eor", "eor", "mvn", "eor", "lsr", "eor",
    "cli", "eor", "phy", "tcd", "jml", "eor", "lsr", "eor",
    "rts", "adc", "per", "adc", "stz", "adc", "ror", "adc",
    "pla", "adc", "ror", "rtl", "jmp", "adc", "ror", "adc",
    "bvs", "adc", "adc", "adc", "stz", "adc", "ror", "adc",
    "sei", "adc", "ply", "tdc", "jmp", "adc", "ror", "adc",
    "bra", "sta", "brl", "sta", "sty", "sta", "stx", "sta",
    "dey", "bit", "txa", "phb", "sty", "sta", "stx", "sta",
    "bcc", "sta", "sta", "sta", "sty", "sta", "stx", "sta",
    "tya", "sta", "txs", "txy", "stz", "sta", "stz", "sta",
    "ldy", "lda", "ldx", "lda", "ldy", "lda", "ldx", "lda",
    "tay", "lda", "tax", "plb", "ldy", "lda", "ldx", "lda",
    "bcs", "lda", "lda", "lda", "ldy", "lda", "ldx", "lda",
    "clv", "lda", "tsx", "tyx", "ldy", "lda", "ldx", "lda",
    "cpy", "cmp", "rep", "cmp", "cpy", "cmp", "dec", "cmp",
    "iny", "cmp", "dex", "wai", "cpy", "cmp", "dec", "cmp",
    "bne", "cmp", "cmp", "cmp", "pei", "cmp", "dec", "cmp",
    "cld", "cmp", "phx", "stp", "jmp", "cmp", "dec", "cmp",
    "cpx", "sbc", "sep", "sbc", "cpx", "sbc", "inc", "sbc",
    "inx", "sbc", "nop", "xba", "cpx", "sbc", "inc", "sbc",
    "beq", "sbc", "sbc", "sbc", "pea", "sbc", "inc", "sbc",
    "sed", "sbc", "plx", "xce", "jsr", "sbc", "inc", "sbc",
  };
  return names[op];
}

}  // namespace snes