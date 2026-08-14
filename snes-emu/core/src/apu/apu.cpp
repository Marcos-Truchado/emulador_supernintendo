#include "apu/apu.hpp"

#include <algorithm>

namespace snes {

// SPC700 boot ROM ($FFC0-$FFFF), from the fullsnes "Boot ROM Disassembly".
const uint8 Apu::bootRom_[64] = {
    0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
    0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
    0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
    0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF,
};

// SPC700: 1.024MHz (24.576MHz / 24); one SMP cycle is ~20.97 master cycles.
static constexpr uint64 kMasterPerSmpCycle = 21;
static constexpr int kSmpCyclesPerSample = 32;  // DSP runs at 32kHz

// ---- lifecycle ----

Apu::Apu() = default;

auto Apu::power() -> void {
  std::fill(std::begin(ram_), std::end(ram_), 0);
  std::fill(std::begin(port_), std::end(port_), 0);
  std::fill(std::begin(dsp_), std::end(dsp_), 0);
  std::fill(std::begin(envx_), std::end(envx_), 0);
  std::fill(std::begin(envMode_), std::end(envMode_), 0);
  std::fill(std::begin(envRaw_), std::end(envRaw_), 0);
  std::fill(std::begin(outx_), std::end(outx_), 0);
  std::fill(std::begin(brrOffset_), std::end(brrOffset_), 0);
  std::fill(std::begin(brrNibble_), std::end(brrNibble_), 0);
  std::fill(std::begin(timerDivider_), std::end(timerDivider_), 0xFF);
  std::fill(std::begin(timerOut_), std::end(timerOut_), 0);
  std::fill(std::begin(timerCounter_), std::end(timerCounter_), 0);
  a_ = x_ = y_ = sp_ = psw_ = 0;
  pc_ = 0;
  dspAddr_ = 0;
  counter_ = 0;
  sampleClock_ = 0;
  timerClock16_ = 0;
  timerClock128_ = 0;
  clockCounter_ = 0;
  audioWr_ = audioRd_ = audioCount_ = 0;
  port_[0x01] = 0xB0;  // $F1: ROM enabled at $FFC0-$FFFF (power-on)
  dsp_[0x6C] = 0xE0;  // FLG: muted on reset (fullsnes)
  reset();
}

auto Apu::reset() -> void {
  a_ = x_ = y_ = 0;
  sp_ = 0xEF;
  psw_ = 0x02;
  pc_ = 0xFFC0;
}

// ---- SMP memory map ----

auto Apu::readRam(uint16 addr) -> uint8 { return ram_[addr]; }

auto Apu::read(uint16 addr) -> uint8 {
  if (addr >= 0xFFC0 && (port_[1] & 0x80)) return bootRom_[addr & 0x3F];
  if (addr >= 0xF0 && addr < 0x0100) {
    switch (addr) {
      case 0xF2: return uint8(dspAddr_);  // DSPADDR
      case 0xF3: return dspRead(uint8(dspAddr_));  // DSPDATA
      case 0xF4: case 0xF5: case 0xF6: case 0xF7: return apuIn_[addr - 0xF4];  // CPUIO in
      case 0xFD: case 0xFE: case 0xFF: {  // TnOUT (reset on read)
        const uint8 v = timerOut_[addr - 0xFD] & 0x0F;
        timerOut_[addr - 0xFD] = 0;
        return v;
      }
      default: return port_[addr & 0x0F];
    }
  }
  return ram_[addr];
}

void Apu::write(uint16 addr, uint8 data) {
  ram_[addr] = data;  // writes to $F0-$FF also land in RAM (fullsnes)
  if (addr >= 0xF0 && addr < 0x0100) {
    switch (addr) {
      case 0xF4: case 0xF5: case 0xF6: case 0xF7: break;  // CPUIO out (below)
      case 0xF1:  // CONTROL (fullsnes): bit7 ROM at $FFC0-$FFFF, bits 0-2 timers
        // Bits 4/5 reset the CPU->SMP input latches ($F4/$F5 and $F6/$F7).
        if (data & 0x10) { apuIn_[0] = 0; apuIn_[1] = 0; }
        if (data & 0x20) { apuIn_[2] = 0; apuIn_[3] = 0; }
        // bits 0-2 enable the three timers; disabling resets TnOUT + reload.
        for (int n = 0; n < 3; n++) {
          if (!(data & (1 << n))) { timerOut_[n] = 0; timerCounter_[n] = 0; }
        }
        break;
      case 0xF2: dspAddr_ = data & 0x7F; break;
      case 0xF3: dspWrite(uint8(dspAddr_), data); break;
      case 0xFA: case 0xFB: case 0xFC:  // TnDIV: set divider + reload counter
        timerDivider_[addr - 0xFA] = data;
        timerCounter_[addr - 0xFA] = 0;
        break;
      default: break;
    }
    port_[addr & 0x0F] = data;
  }
}

// main-CPU ports $2140-$2143
auto Apu::writePort(int index, uint8 data) -> void { apuIn_[index] = data; }
auto Apu::readPort(int index) const -> uint8 { return port_[0x04 + index]; }

auto Apu::ram(uint16 address) const -> uint8 { return ram_[address]; }
auto Apu::dspRegister(uint8 index) const -> uint8 { return dsp_[index & 0x7F]; }

// ---- SMP execution ----

auto Apu::stepInstruction() -> int {
  const uint8 op = readOp();
  const uint8 zp = flag(kP) ? 0x01 : 0x00;  // zero-page base 0x0000/0x0100

  auto readDp = [&](uint8 a) { return read(uint16(uint16(zp) << 8) | a); };
  auto writeDp = [&](uint8 a, uint8 v) { write(uint16(uint16(zp) << 8) | a, v); };
  auto read16Dp = [&](uint8 a) -> uint16 { return uint16(readDp(a)) | (uint16(readDp(uint8(a + 1))) << 8); };
  auto readAbs = [&]() -> uint16 { const uint8 lo = readOp(); return uint16(uint16(readOp()) << 8) | lo; };

  switch (op) {
    // ---- register moves ----
    case 0xE8: a_ = readOp(); setNZ(a_); return 2;   // MOV A,#imm
    case 0xCD: x_ = readOp(); setNZ(x_); return 2;   // MOV X,#imm
    case 0x8D: y_ = readOp(); setNZ(y_); return 2;   // MOV Y,#imm
    case 0x7D: a_ = x_; setNZ(a_); return 2;         // MOV A,X
    case 0x5D: x_ = a_; setNZ(x_); return 2;         // MOV X,A
    case 0xDD: a_ = y_; setNZ(a_); return 2;         // MOV A,Y
    case 0xFD: y_ = a_; setNZ(y_); return 2;         // MOV Y,A
    case 0x9D: x_ = sp_; setNZ(x_); return 2;        // MOV X,SP
    case 0xBD: sp_ = x_; return 2;                   // MOV SP,X

    case 0xE4: a_ = readDp(readOp()); setNZ(a_); return 3;              // MOV A,dp
    case 0xF4: a_ = readDp(uint8(readOp() + x_)); setNZ(a_); return 4;   // MOV A,dp+X
    case 0xE5: a_ = read(readAbs()); setNZ(a_); return 4;                // MOV A,!abs
    case 0xF5: a_ = read(readAbs() + x_); setNZ(a_); return 5;           // MOV A,!abs+X
    case 0xF6: a_ = read(readAbs() + y_); setNZ(a_); return 5;           // MOV A,!abs+Y
    case 0xE6: a_ = readDp(x_); setNZ(a_); return 3;                     // MOV A,(X)
    case 0xBF: a_ = readDp(x_); x_++; setNZ(a_); return 4;               // MOV A,(X)+
    case 0xF7: a_ = read(read16Dp(readOp()) + y_); setNZ(a_); return 6;  // MOV A,[dp]+Y
    case 0xE7: a_ = read(read16Dp(uint8(readOp() + x_))); setNZ(a_); return 6;  // MOV A,[dp+X]
    case 0xF8: x_ = readDp(readOp()); setNZ(x_); return 3;               // MOV X,dp
    case 0xF9: x_ = readDp(uint8(readOp() + y_)); setNZ(x_); return 4;   // MOV X,dp+Y
    case 0xE9: x_ = read(readAbs()); setNZ(x_); return 4;                // MOV X,!abs
    case 0xEB: y_ = readDp(readOp()); setNZ(y_); return 3;               // MOV Y,dp
    case 0xFB: y_ = readDp(uint8(readOp() + x_)); setNZ(y_); return 4;   // MOV Y,dp+X
    case 0xEC: y_ = read(readAbs()); setNZ(y_); return 4;                // MOV Y,!abs
    case 0xBA: { const uint16 v = read16Dp(readOp()); setYa(v); setNZ(y_); return 5; }  // MOVW YA,dp

    case 0x8F: { const uint8 v = readOp(); writeDp(readOp(), v); return 5; }             // MOV dp,#imm
    case 0xFA: { const uint8 a = readOp(); const uint8 b = readOp(); writeDp(b, readDp(a)); return 5; }  // MOV dp,dp
    case 0xC4: writeDp(readOp(), a_); return 4;    // MOV dp,A
    case 0xD8: writeDp(readOp(), x_); return 4;    // MOV dp,X
    case 0xCB: writeDp(readOp(), y_); return 4;    // MOV dp,Y
    case 0xD4: writeDp(uint8(readOp() + x_), a_); return 5;  // MOV dp+X,A
    case 0xDB: writeDp(uint8(readOp() + x_), y_); return 5;  // MOV dp+X,Y
    case 0xD9: writeDp(uint8(readOp() + y_), x_); return 5;  // MOV dp+Y,X
    case 0xC5: write(readAbs(), a_); return 5;    // MOV !abs,A
    case 0xC9: write(readAbs(), x_); return 5;    // MOV !abs,X
    case 0xCC: write(readAbs(), y_); return 5;    // MOV !abs,Y
    case 0xD5: write(readAbs() + x_, a_); return 6;
    case 0xD6: write(readAbs() + y_, a_); return 6;
    case 0xAF: writeDp(x_, a_); x_++; return 4;   // MOV (X)+,A
    case 0xC6: writeDp(x_, a_); return 4;         // MOV (X),A
    case 0xD7: write(read16Dp(readOp()) + y_, a_); return 7;  // MOV [dp]+Y,A
    case 0xC7: write(read16Dp(uint8(readOp() + x_)), a_); return 7;  // MOV [dp+X],A
    case 0xDA: {  // MOVW dp,YA
      const uint8 a = readOp(); const uint16 v = ya();
      writeDp(a, uint8(v & 0xFF)); writeDp(uint8(a + 1), uint8(v >> 8)); return 5;
    }

    // ---- push/pop ----
    case 0x2D: write(0x100 | sp_, a_); sp_--; return 4;   // PUSH A
    case 0x4D: write(0x100 | sp_, x_); sp_--; return 4;   // PUSH X
    case 0x6D: write(0x100 | sp_, y_); sp_--; return 4;   // PUSH Y
    case 0x0D: write(0x100 | sp_, psw_); sp_--; return 4;  // PUSH PSW
    case 0xAE: sp_++; a_ = read(0x100 | sp_); return 4;   // POP A
    case 0xCE: sp_++; x_ = read(0x100 | sp_); return 4;   // POP X
    case 0xEE: sp_++; y_ = read(0x100 | sp_); return 4;   // POP Y
    case 0x8E: sp_++; psw_ = read(0x100 | sp_); return 4;  // POP PSW

    default: break;
  }

  // ---- ALU: OR/AND/EOR/CMP/ADC/SBC ----
  {
    const int sub = op & 0x1F;
    const int aluOp = (op >> 5) & 7;  // 0=OR 1=AND 2=EOR 3=CMP 4=ADC 5=SBC
    if (aluOp <= 5 && ((sub >= 0x04 && sub <= 0x09) || (sub >= 0x14 && sub <= 0x19))) {
      uint16 storeAddr = 0;
      bool store = false;
      uint8 v = 0;
      uint8 acc = a_;  // first operand: A for the A-forms, memory for RMW forms
      switch (sub) {
        case 0x04: v = readDp(readOp()); break;
        case 0x05: v = read(readAbs()); break;
        case 0x06: v = readDp(x_); break;
        case 0x07: v = read(read16Dp(uint8(readOp() + x_))); break;
        case 0x08: v = readOp(); break;
        case 0x09: {  // [aa] op [bb], encoded "bb aa" (fullsnes x+09 bb aa)
          const uint8 b = readOp();  // bb (second operand)
          const uint8 a = readOp();  // aa (first/destination operand)
          acc = readDp(a);
          v = readDp(b);
          storeAddr = uint16(uint16(zp) << 8) | a;
          store = true;
        } break;
        case 0x14: v = readDp(uint8(readOp() + x_)); break;
        case 0x15: v = read(readAbs() + x_); break;
        case 0x16: v = read(readAbs() + y_); break;
        case 0x17: v = read(read16Dp(readOp()) + y_); break;
        case 0x18: {  // [aa] op nn, encoded "nn aa" (fullsnes x+18 nn aa)
          v = readOp();          // nn (immediate)
          const uint8 a = readOp();  // aa (destination)
          acc = readDp(a);
          storeAddr = uint16(uint16(zp) << 8) | a;
          store = true;
        } break;
        case 0x19: {  // [X] op [Y]
          acc = readDp(x_);
          v = readDp(y_);
          storeAddr = uint16(uint16(zp) << 8) | x_;
          store = true;
        } break;
      }
      int cycles = (sub == 0x08) ? 2 : (sub == 0x04 || sub == 0x06) ? 3
                 : (sub == 0x05 || sub == 0x14) ? 4
                 : (sub == 0x15 || sub == 0x16 || sub == 0x18 || sub == 0x19) ? 5 : 6;
      if (aluOp == 3) {  // CMP: flags only (acc - v)
        const int d = acc - v;
        setFlag(kC, d >= 0);
        setNZ(uint8(d));
        return cycles;
      }
      uint8 r = acc;
      if (aluOp == 0) r = uint8(acc | v);
      else if (aluOp == 1) r = uint8(acc & v);
      else if (aluOp == 2) r = uint8(acc ^ v);
      else if (aluOp == 4) {  // ADC
        const int c = flag(kC) ? 1 : 0;
        const int d = acc + v + c;
        setFlag(kH, ((acc & 0xF) + (v & 0xF) + c) > 0xF);
        setFlag(kV, ((acc ^ v) & 0x80) == 0 && ((acc ^ d) & 0x80) != 0);
        setFlag(kC, d > 0xFF);
        r = uint8(d);
      } else {  // SBC
        const int c = flag(kC) ? 0 : 1;
        const int d = acc - v - c;
        setFlag(kH, ((acc & 0xF) - (v & 0xF) - c) >= 0);
        setFlag(kV, ((acc ^ v) & 0x80) != 0 && ((acc ^ d) & 0x80) != 0);
        setFlag(kC, d >= 0);
        r = uint8(d);
      }
      setNZ(r);
      if (store) write(storeAddr, r); else a_ = r;
      return cycles;
    }
  }

  // ---- CMP X / CMP Y (fullsnes: extra compare forms) ----
  {
    auto cmpX = [&](uint8 operand, int cycles) {
      const int d = x_ - operand;
      setFlag(kC, d >= 0);
      setNZ(uint8(d));
      return cycles;
    };
    auto cmpY = [&](uint8 operand, int cycles) {
      const int d = y_ - operand;
      setFlag(kC, d >= 0);
      setNZ(uint8(d));
      return cycles;
    };
    switch (op) {
      case 0xC8: return cmpX(readOp(), 2);            // CMP X,#nn
      case 0x3E: return cmpX(readDp(readOp()), 3);    // CMP X,dp
      case 0x1E: return cmpX(read(readAbs()), 4);     // CMP X,!abs
      case 0xAD: return cmpY(readOp(), 2);            // CMP Y,#nn
      case 0x7E: return cmpY(readDp(readOp()), 3);    // CMP Y,dp
      case 0x5E: return cmpY(read(readAbs()), 4);     // CMP Y,!abs
      default: break;
    }
  }

  // ---- rotate/shift/inc/dec ----
  {
    // memory forms: dp (x0B), !abs (x0C), dp+X (x1B); A form (x1C) handled below.
    const int base = op & 0xE0;  // 00=ASL 20=ROL 40=LSR 60=ROR 80=DEC A0=INC
    const int form = op & 0x1F;
    if (form == 0x0B || form == 0x0C || form == 0x1B) {
      auto rmw = [&](uint8 v) -> uint8 {
        const bool c = flag(kC);
        if (base == 0x00) { setFlag(kC, v & 0x80); v <<= 1; }
        else if (base == 0x20) { setFlag(kC, v & 0x80); v = uint8((v << 1) | c); }
        else if (base == 0x40) { setFlag(kC, v & 1); v >>= 1; }
        else if (base == 0x60) { setFlag(kC, v & 1); v = uint8((v >> 1) | (c << 7)); }
        else if (base == 0x80) v--;
        else v++;
        setNZ(v);
        return v;
      };
      if (form == 0x0B) { const uint8 a = readOp(); writeDp(a, rmw(readDp(a))); return 4; }
      if (form == 0x0C) { const uint16 a = readAbs(); write(a, rmw(read(a))); return 5; }
      { const uint8 a = readOp(); writeDp(uint8(a + x_), rmw(readDp(uint8(a + x_)))); return 5; }
    }
  }

  switch (op) {
    // rotate/shift/inc/dec on A/X/Y
    case 0x1C: setFlag(kC, a_ & 0x80); a_ <<= 1; setNZ(a_); return 2;  // ASL A
    case 0x3C: { const bool c = flag(kC); setFlag(kC, a_ & 0x80); a_ = uint8((a_ << 1) | c); setNZ(a_); return 2; }  // ROL A
    case 0x5C: setFlag(kC, a_ & 1); a_ >>= 1; setNZ(a_); return 2;     // LSR A
    case 0x7C: { const bool c = flag(kC); setFlag(kC, a_ & 1); a_ = uint8((a_ >> 1) | (c << 7)); setNZ(a_); return 2; }  // ROR A
    case 0x9C: a_--; setNZ(a_); return 2;   // DEC A
    case 0xBC: a_++; setNZ(a_); return 2;   // INC A
    case 0x1D: x_--; setNZ(x_); return 2;   // DEC X
    case 0x3D: x_++; setNZ(x_); return 2;   // INC X
    case 0xDC: y_--; setNZ(y_); return 2;   // DEC Y
    case 0xFC: y_++; setNZ(y_); return 2;   // INC Y

    // ---- 16-bit ALU ----
    case 0x7A: {  // ADDW YA,dp
      const uint16 w = read16Dp(readOp());
      const int r = ya() + w;
      setFlag(kC, r > 0xFFFF);
      setFlag(kH, ((ya() & 0xFFF) + (w & 0xFFF)) > 0xFFF);
      setFlag(kV, ((ya() ^ w) & 0x8000) == 0 && ((ya() ^ r) & 0x8000) != 0);
      setYa(uint16(r)); setNZ(y_); return 5;
    }
    case 0x9A: {  // SUBW YA,dp
      const uint16 w = read16Dp(readOp());
      const int r = ya() - w;
      setFlag(kC, r >= 0);
      setFlag(kH, ((ya() & 0xFFF) - (w & 0xFFF)) >= 0);
      setFlag(kV, ((ya() ^ w) & 0x8000) != 0 && ((ya() ^ r) & 0x8000) != 0);
      setYa(uint16(r)); setNZ(y_); return 5;
    }
    case 0x5A: {  // CMPW YA,dp
      const uint16 w = read16Dp(readOp());
      const int r = ya() - w;
      setFlag(kC, r >= 0); setNZ(uint8(r >> 8)); return 4;
    }
    case 0x3A: {  // INCW dp
      const uint8 a = readOp();
      const uint16 v = read16Dp(a) + 1;
      writeDp(a, uint8(v & 0xFF)); writeDp(uint8(a + 1), uint8(v >> 8));
      setNZ(uint8(v >> 8)); return 6;
    }
    case 0x1A: {  // DECW dp
      const uint8 a = readOp();
      const uint16 v = read16Dp(a) - 1;
      writeDp(a, uint8(v & 0xFF)); writeDp(uint8(a + 1), uint8(v >> 8));
      setNZ(uint8(v >> 8)); return 6;
    }
    case 0x9E: {  // DIV YA,X
      const uint16 t = ya();
      const int d = x_ ? x_ : 256;  // div-by-zero guard (undefined on hardware)
      setFlag(kV, y_ >= x_);
      setFlag(kH, (t / d) >= 0x100);
      a_ = uint8(t / d); y_ = uint8(t % d);
      setNZ(a_); return 12;
    }
    case 0xCF: {  // MUL YA
      setYa(uint16(a_) * y_); setNZ(y_); return 9;
    }

    default: break;
  }

  // ---- 1-bit ALU ----
  if ((op & 0x1F) == 0x02) {  // SET1 dp.b
    const uint8 a = readOp();
    writeDp(a, uint8(readDp(a) | (1 << (op >> 5)))); return 4;
  }
  if ((op & 0x1F) == 0x12) {  // CLR1 dp.b
    const uint8 a = readOp();
    writeDp(a, uint8(readDp(a) & ~(1 << (op >> 5)))); return 4;
  }
  switch (op) {
    case 0xEA: case 0xCA: case 0xAA: case 0x0A: case 0x2A: case 0x4A: case 0x6A: case 0x8A: {
      // 1-bit ops encode a 13-bit address + 3-bit bit select in two bytes:
      // byte1 = addr low, byte2 = (bit << 5) | (addr >> 8).
      const uint8 lo = readOp();
      const uint8 hi = readOp();
      const uint16 addr = uint16(lo) | (uint16(hi & 0x1F) << 8);
      const int bit = hi >> 5;
      const bool b = read(addr) & (1 << bit);
      int cyc = 4;
      if (op == 0xEA) { write(addr, uint8(read(addr) ^ (1 << bit))); cyc = 5; }  // NOT1
      else if (op == 0xCA) {  // MOV1 mem.bit,C
        write(addr, flag(kC) ? uint8(read(addr) | (1 << bit)) : uint8(read(addr) & ~(1 << bit))); cyc = 6;
      }
      else if (op == 0xAA) setFlag(kC, b);                       // MOV1 C,mem.bit
      else if (op == 0x0A) { setFlag(kC, flag(kC) || b); cyc = 5; }   // OR1 C,mem.bit
      else if (op == 0x2A) { setFlag(kC, flag(kC) || !b); cyc = 5; }  // OR1 C,/mem.bit
      else if (op == 0x4A) setFlag(kC, flag(kC) && b);           // AND1 C,mem.bit
      else if (op == 0x6A) setFlag(kC, flag(kC) && !b);          // AND1 C,/mem.bit
      else { setFlag(kC, flag(kC) != b); cyc = 5; }              // EOR1 C,mem.bit
      return cyc;
    }
    case 0x60: setFlag(kC, false); return 2;   // CLRC
    case 0x80: setFlag(kC, true); return 2;    // SETC
    case 0xED: setFlag(kC, !flag(kC)); return 3;  // NOTC
    case 0xE0: setFlag(kV, false); setFlag(kH, false); return 2;  // CLRV
    default: break;
  }

  // ---- special ----
  switch (op) {
    case 0xDF: {  // DAA
      if ((a_ & 0x0F) > 0x09 || flag(kH)) a_ += 0x06;
      if ((a_ & 0xF0) > 0x90 || flag(kC)) { a_ += 0x60; setFlag(kC, true); }
      setNZ(a_); return 3;
    }
    case 0xBE: {  // DAS
      if ((a_ & 0x0F) > 0x09 || !flag(kH)) a_ -= 0x06;
      if ((a_ & 0xF0) > 0x90 || !flag(kC)) { a_ -= 0x60; setFlag(kC, true); }
      setNZ(a_); return 3;
    }
    case 0x9F: a_ = uint8((a_ >> 4) | (a_ << 4)); setNZ(a_); return 5;  // XCN
    case 0x4E: {  // TCLR1 !abs
      const uint16 a = readAbs();
      const uint8 v = read(a);
      setNZ(uint8(a_ - v));
      write(a, uint8(v & ~a_)); return 6;
    }
    case 0x0E: {  // TSET1 !abs
      const uint16 a = readAbs();
      const uint8 v = read(a);
      setNZ(uint8(a_ - v));
      write(a, uint8(v | a_)); return 6;
    }
    default: break;
  }

  // ---- branches ----
  if ((op & 0x1F) == 0x03) {  // BBS dp.b
    const uint8 a = readOp(); const int8 r = int8(readOp());
    if (readDp(a) & (1 << (op >> 5))) { pc_ += r; return 7; }
    return 5;
  }
  if ((op & 0x1F) == 0x13) {  // BBC dp.b
    const uint8 a = readOp(); const int8 r = int8(readOp());
    if (!(readDp(a) & (1 << (op >> 5)))) { pc_ += r; return 7; }
    return 5;
  }
  switch (op) {
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0: {
      const int8 r = int8(readOp());
      const bool take = (op == 0x10 && !flag(kN)) || (op == 0x30 && flag(kN)) ||
                        (op == 0x50 && !flag(kV)) || (op == 0x70 && flag(kV)) ||
                        (op == 0x90 && !flag(kC)) || (op == 0xB0 && flag(kC)) ||
                        (op == 0xD0 && !flag(kZ)) || (op == 0xF0 && flag(kZ));
      if (take) { pc_ += r; return 4; }
      return 2;
    }
    case 0x2E: {  // CBNE dp,dest
      const uint8 a = readOp(); const int8 r = int8(readOp());
      if (a_ != readDp(a)) { pc_ += r; return 7; }
      return 5;
    }
    case 0xDE: {  // CBNE dp+X,dest
      const uint8 a = readOp(); const int8 r = int8(readOp());
      if (a_ != readDp(uint8(a + x_))) { pc_ += r; return 8; }
      return 6;
    }
    case 0xFE: {  // DBNZ Y,dest
      const int8 r = int8(readOp()); y_--;
      if (y_) { pc_ += r; return 6; }
      return 4;
    }
    case 0x6E: {  // DBNZ dp,dest
      const uint8 a = readOp(); const int8 r = int8(readOp());
      const uint8 v = uint8(readDp(a) - 1); writeDp(a, v);
      if (v) { pc_ += r; return 7; }
      return 5;
    }
    default: break;
  }

  // ---- jumps/calls ----
  if ((op & 0x0F) == 0x01) {  // TCALL n
    write(0x100 | sp_, uint8(pc_ >> 8)); sp_--;
    write(0x100 | sp_, uint8(pc_)); sp_--;
    const uint16 vec = uint16(read(0xFFDE - (op >> 4) * 2) | (uint16(read(0xFFDE - (op >> 4) * 2 + 1)) << 8));
    pc_ = vec;
    return 8;
  }
  switch (op) {
    case 0x2F: { const int8 r = int8(readOp()); pc_ += r; return 4; }  // BRA
    case 0x5F: pc_ = readAbs(); return 3;                              // JMP !abs
    case 0x1F: {  // JMP [!abs+X]
      const uint16 a = readAbs() + x_;
      pc_ = uint16(read(a) | (read(uint16(a + 1)) << 8)); return 6;
    }
    case 0x3F: {  // CALL !abs
      const uint16 a = readAbs();
      write(0x100 | sp_, uint8(pc_ >> 8)); sp_--;
      write(0x100 | sp_, uint8(pc_)); sp_--;
      pc_ = a; return 8;
    }
    case 0x4F: {  // PCALL uu
      const uint8 a = readOp();
      write(0x100 | sp_, uint8(pc_ >> 8)); sp_--;
      write(0x100 | sp_, uint8(pc_)); sp_--;
      pc_ = 0xFF00 | a; return 6;
    }
    case 0x6F: {  // RET
      pc_ = uint16(read(0x100 | uint8(sp_ + 1)) | (read(0x100 | uint8(sp_ + 2)) << 8));
      sp_ += 2; return 5;
    }
    case 0x7F: {  // RET1
      psw_ = read(0x100 | uint8(sp_ + 1));
      pc_ = uint16(read(0x100 | uint8(sp_ + 2)) | (read(0x100 | uint8(sp_ + 3)) << 8));
      sp_ += 3; return 6;
    }
    case 0x0F: {  // BRK
      write(0x100 | sp_, uint8(pc_ >> 8)); sp_--;
      write(0x100 | sp_, uint8(pc_)); sp_--;
      write(0x100 | sp_, uint8(psw_ | kB)); sp_--;
      setFlag(kI, false); setFlag(kB, true);
      pc_ = uint16(read(0xFFDE) | (read(0xFFDF) << 8)); return 8;
    }
    case 0x00: return 2;   // NOP
    case 0xEF: return 0;   // SLEEP (no interrupts in SNES APU -> hang)
    case 0xFF: return 0;   // STOP
    case 0x20: setFlag(kP, false); return 2;  // CLRP
    case 0x40: setFlag(kP, true); return 2;   // SETP
    case 0xA0: setFlag(kI, true); return 3;   // EI
    case 0xC0: setFlag(kI, false); return 3;  // DI
    default: return 1;
  }
}

// ---- S-DSP ----

auto Apu::dspRead(uint8 index) const -> uint8 {
  const uint8 i = index & 0x7F;
  if ((i & 0x0F) == 0x08) return uint8(envx_[i >> 4] >> 4);      // VxENVX (upper 7 bits)
  if ((i & 0x0F) == 0x09) return uint8(outx_[i >> 4] >> 8);  // VxOUTX
  return dsp_[i];
}

void Apu::dspWrite(uint8 index, uint8 data) {
  const uint8 i = index & 0x7F;
  switch (i) {
    case 0x4C:  // KON: key-on voices (edge-triggered)
      for (int n = 0; n < 8; n++) if (data & (1 << n)) voiceKeyOn(n);
      break;
    case 0x5C:  // KOFF: key off -> release
      dsp_[0x5C] = data;
      for (int n = 0; n < 8; n++) if (data & (1 << n)) envMode_[n] = 3;
      break;
    case 0x6C:  // FLG
      if (data & 0x80) for (int n = 0; n < 8; n++) { envMode_[n] = 3; envx_[n] = 0; }  // soft reset
      dsp_[0x6C] = data;
      break;
    case 0x7C: dsp_[0x7C] = 0; break;  // ENDX: any write acks all bits
    default: dsp_[i] = data; break;
  }
}

void Apu::voiceKeyOn(int n) {
  const uint8 srcn = dsp_[n * 0x10 + 0x4];
  const uint16 dir = uint16(dsp_[0x5D]) << 8;
  brrOffset_[n] = uint16(readRam(dir + srcn * 4) | (readRam(dir + srcn * 4 + 1) << 8));
  brrNibble_[n] = 0;
  brrPrev_[n][0] = brrPrev_[n][1] = 0;
  envx_[n] = 0;
  envRaw_[n] = 0;
  envMode_[n] = 0;  // attack
}

// Decode one BRR 4-bit nibble -> 15-bit sample (fullsnes "BRR Samples").
void Apu::decodeBrr(int n) {
  const uint8 srcn = dsp_[n * 0x10 + 0x4];
  const uint16 dir = uint16(dsp_[0x5D]) << 8;
  const uint16 offset = brrOffset_[n];

  if (brrNibble_[n] == 0) {  // new 9-byte block header
    brrHeader_[n] = readRam(offset);
    brrShift_[n] = brrHeader_[n] >> 4;
    brrFilter_[n] = (brrHeader_[n] >> 2) & 3;
  }
  const uint8 shift = brrShift_[n];
  const uint8 data = readRam(offset + 1 + (brrNibble_[n] >> 1));
  const int nib = (brrNibble_[n] & 1) ? (data >> 4) : (data & 0x0F);
  const int sign = (nib ^ 8) - 8;
  const int sample = shift <= 12 ? (sign << shift) >> 1 : sign & ~0x7FF;

  const int16 p = brrPrev_[n][0], p2 = brrPrev_[n][1];
  int out;
  switch (brrFilter_[n]) {
    case 0: out = sample; break;
    case 1: out = sample + p + ((-p) >> 4); break;
    case 2: out = sample + (p << 1) + ((-3 * p) >> 5) - p2 + (p2 >> 4); break;
    default: out = sample + (p << 1) + ((-13 * p) >> 6) - p2 + ((3 * p2) >> 4); break;
  }
  out = std::clamp(out, -0x4000, 0x3FFF);
  brrPrev_[n][1] = brrPrev_[n][0];
  brrPrev_[n][0] = int16(out);

  if (++brrNibble_[n] >= 16) {  // block done
    brrNibble_[n] = 0;
    const int loop = brrHeader_[n] & 3;
    if (loop == 1 || loop == 3) {  // end: jump to loop address, set ENDX
      brrOffset_[n] = uint16(readRam(dir + srcn * 4 + 2) | (readRam(dir + srcn * 4 + 3) << 8));
      dsp_[0x7C] |= uint8(1 << n);
      if (loop == 1) envx_[n] = 0;  // end+mute
    } else {
      brrOffset_[n] = uint16(offset + 9);
    }
  }
}

// ADSR/gain envelope step (ares dsp/envelope.cpp).
void Apu::runEnvelope(int n) {
  int envelope = envx_[n];

  if (envMode_[n] == 3) {  // release
    envelope -= 0x8;
    if (envelope < 0) envelope = 0;
    envx_[n] = envelope;
    return;
  }

  int rate;
  int envelopeData;
  if (dsp_[n * 0x10 + 0x5] & 0x80) {  // ADSR
    envelopeData = dsp_[n * 0x10 + 0x6];  // adsr2: sustain level/rate
    if (envMode_[n] >= 1) {  // decay / sustain
      envelope--;
      envelope -= envelope >> 8;
      rate = envelopeData & 0x1F;
      if (envMode_[n] == 1) {  // decay
        rate = ((dsp_[n * 0x10 + 0x5] >> 4) & 7) * 2 + 16;
      }
    } else {  // attack
      rate = (dsp_[n * 0x10 + 0x5] & 0x0F) * 2 + 1;
      envelope += rate < 31 ? 0x20 : 0x400;
    }
  } else {  // GAIN
    envelopeData = dsp_[n * 0x10 + 0x7];
    int mode = envelopeData >> 5;
    if (mode < 4) {  // direct
      envelope = envelopeData << 4;
      rate = 31;
    } else {
      rate = envelopeData & 0x1F;
      if (mode == 4) {
        envelope -= 0x20;
      } else if (mode < 6) {
        envelope--;
        envelope -= envelope >> 8;
      } else {
        envelope += 0x20;
        if (mode > 6 && uint32(envRaw_[n]) >= 0x600) {
          envelope += 0x8 - 0x20;
        }
      }
    }
  }

  if ((envelope >> 8) == (envelopeData >> 5) && envMode_[n] == 1) {
    envMode_[n] = 2;  // decay -> sustain
  }
  envRaw_[n] = envelope;

  if (uint32(envelope) > 0x7FF) {
    envelope = (envelope < 0 ? 0 : 0x7FF);
    if (envMode_[n] == 0) envMode_[n] = 1;  // attack -> decay
  }

  if (counterPoll(rate)) envx_[n] = envelope;
}

void Apu::mixSample() {
  int left = 0, right = 0;
  for (int n = 0; n < 8; n++) {
    outx_[n] = int16((brrPrev_[n][0] * envx_[n]) >> 11);
    const int16 v = outx_[n];
    left += (v * int8(dsp_[n * 0x10 + 0x0])) >> 7;   // VxVOLL
    right += (v * int8(dsp_[n * 0x10 + 0x1])) >> 7;  // VxVOLR
  }
  left = (left * int8(dsp_[0x0C])) >> 7;    // MVOLL
  right = (right * int8(dsp_[0x1C])) >> 7;  // MVOLR
  if (dsp_[0x6C] & 0x40) left = right = 0;  // mute
  sample_[0] = int16(std::clamp(left, -32768, 32767));
  sample_[1] = int16(std::clamp(right, -32768, 32767));
}

// ---- Thread ----

auto Apu::step(uint64 masterCycles) -> void {
  counter_ += int(masterCycles);
  while (counter_ >= int(kMasterPerSmpCycle)) {
    const int cycles = stepInstruction();
    if (cycles == 0) return;  // SLEEP/STOP: halt
    counter_ -= cycles * int(kMasterPerSmpCycle);
    tickTimers(cycles);
    sampleClock_ += cycles;
    while (sampleClock_ >= kSmpCyclesPerSample) {
      sampleClock_ -= kSmpCyclesPerSample;
      counterTick();
      for (int n = 0; n < 8; n++) { decodeBrr(n); runEnvelope(n); }
      mixSample();
      pushSample();
    }
  }
}

// ---- SMP timers ($FA-$FF) ----
//
// fullsnes "SPC700 I/O Ports": timers 0/1 are clocked at 8kHz (every 128 SMP
// cycles), timer 2 at 64kHz (every 16 cycles). Each tick advances the timer's
// internal counter; when it reaches the divider (0 = divide by 256) the 4-bit
// TnOUT output increments and the counter resets. Timers only run while their
// CONTROL ($F1) enable bit is set.
void Apu::tickTimers(int cycles) {
  timerClock16_ += cycles;
  while (timerClock16_ >= 16) {
    timerClock16_ -= 16;
    tickTimer(2);
  }
  timerClock128_ += cycles;
  while (timerClock128_ >= 128) {
    timerClock128_ -= 128;
    tickTimer(0);
    tickTimer(1);
  }
}

void Apu::tickTimer(int n) {
  if (!(port_[0x01] & (1 << n))) return;
  const int threshold = timerDivider_[n] == 0 ? 256 : timerDivider_[n];
  if (++timerCounter_[n] >= threshold) {
    timerCounter_[n] = 0;
    timerOut_[n] = uint8((timerOut_[n] + 1) & 0x0F);
  }
}

// ---- audio output (phase 7) ----

void Apu::pushSample() {
  if (audioCount_ + 2 > kAudioBuf) return;  // full: drop (shouldn't happen)
  audioBuf_[audioWr_] = sample_[0];
  audioWr_ = (audioWr_ + 1) % kAudioBuf;
  audioBuf_[audioWr_] = sample_[1];
  audioWr_ = (audioWr_ + 1) % kAudioBuf;
  audioCount_ += 2;
}

auto Apu::readAudio(int16* buffer, size_t count) -> size_t {
  const size_t n = std::min(count, audioCount_);
  for (size_t i = 0; i < n; i++) {
    buffer[i] = audioBuf_[audioRd_];
    audioRd_ = (audioRd_ + 1) % kAudioBuf;
  }
  audioCount_ -= n;
  return n;
}

// ---- save states ----

auto Apu::serialize(Writer& w) const -> void {
  w.raw(ram_, sizeof(ram_));
  w.raw(port_, sizeof(port_));
  w.raw(apuIn_, sizeof(apuIn_));
  for (int n = 0; n < 3; n++) {
    w.u8(uint8(timerDivider_[n] & 0xFF));
    w.u8(timerOut_[n]);
    w.u16(uint16(timerCounter_[n] & 0xFFFF));
  }
  w.u8(a_); w.u8(x_); w.u8(y_); w.u8(sp_); w.u8(psw_);
  w.u16(pc_);
  w.raw(dsp_, sizeof(dsp_));
  w.u8(uint8(dspAddr_ & 0x7F));
  for (int n = 0; n < 8; n++) {
    w.u16(uint16(envx_[n] & 0xFFFF));
    w.u16(uint16(outx_[n] & 0xFFFF));
    w.u16(brrOffset_[n]);
    w.u8(brrHeader_[n]);
    w.u8(brrShift_[n]);
    w.u8(brrFilter_[n]);
    w.u8(brrNibble_[n]);
    w.u16(uint16(brrPrev_[n][0] & 0xFFFF));
    w.u16(uint16(brrPrev_[n][1] & 0xFFFF));
  }
  w.u16(uint16(sample_[0])); w.u16(uint16(sample_[1]));
  w.u32(uint32(counter_));
  w.u32(uint32(sampleClock_));
  w.u32(uint32(timerClock16_));
  w.u32(uint32(timerClock128_));
}

auto Apu::deserialize(Reader& r) -> void {
  r.raw(ram_, sizeof(ram_));
  r.raw(port_, sizeof(port_));
  r.raw(apuIn_, sizeof(apuIn_));
  for (int n = 0; n < 3; n++) {
    timerDivider_[n] = r.u8();
    timerOut_[n] = r.u8();
    timerCounter_[n] = r.u16();
  }
  a_ = r.u8(); x_ = r.u8(); y_ = r.u8(); sp_ = r.u8(); psw_ = r.u8();
  pc_ = r.u16();
  r.raw(dsp_, sizeof(dsp_));
  dspAddr_ = r.u8() & 0x7F;
  for (int n = 0; n < 8; n++) {
    envx_[n] = r.u16();
    outx_[n] = int16(r.u16());
    brrOffset_[n] = r.u16();
    brrHeader_[n] = r.u8();
    brrShift_[n] = r.u8();
    brrFilter_[n] = r.u8();
    brrNibble_[n] = r.u8();
    brrPrev_[n][0] = int16(r.u16());
    brrPrev_[n][1] = int16(r.u16());
  }
  sample_[0] = int16(r.u16());
  sample_[1] = int16(r.u16());
  counter_ = int(r.u32());
  sampleClock_ = int(r.u32());
  timerClock16_ = int(r.u32());
  timerClock128_ = int(r.u32());
  // the audio ring buffer is not part of a save state; it refills as the
  // APU keeps running after load.
  audioWr_ = audioRd_ = audioCount_ = 0;
}

}  // namespace snes
