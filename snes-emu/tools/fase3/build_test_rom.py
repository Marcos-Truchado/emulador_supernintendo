#!/usr/bin/env python3
"""Builds the phase-3 timing test ROM (LoROM, 32KB, no copier header).

The ROM exercises the scheduler/PPU timing of the emulator from the CPU side:

  test 1 (VBlank counting, fullsnes H/V events):
    polls $4212 bit7 in busy loops; counts rising edges over 10 frames.
    Must be exactly 10 -- a live-mirror or latched flag that never clears
    (or clears on read) would hang or under/over-count here. Deliberately
    NEVER uses $4210 for the frame edge, only for the NMI-latch check below.

  test 2 (NMI latch read/ack, fase3 doc §4.1/§7.5):
    right after the VBlank edge, reads $4210 once. It must return 0x82
    (bit7 NMI latch set at H=0,V=225 + CPU version 2). A live mirror
    ($4212-style) would make a second read in the same frame also return
    bit7 set; a Read/Ack that auto-cleared too early would return 0x02.

  test 3 (V-IRQ, mode 2, VTIME=100, fase3 doc §6.3/§7.4):
    with $4200 = $20 and VTIME = 100, $4211 bit7 latches once per frame
    (at H=0,V=100) and is acknowledged by reading. Busy-polling with
    read/ack must count exactly 10 over 10 frames. A register-note vs
    event-table (+3.5/+2.5) difference would still count 10 here; the
    exact instant is covered by the unit tests.

  test 4 (HBlank sampling, fase3 doc §10.1.3):
    samples $4212 bit6 in a tight loop, 200 iterations, counting set vs
    clear samples. The emulator's own loop timing makes this deterministic;
    the runner validates a broad range (HBlank is ~68/341 of each line).

Result layout (WRAM $0200-$0206, bank 0):
    $0200 frames_left (must be 0), $0201 vblanks, $0202 irq_count,
    $0203 nmi_ack_ok, $0204 hb_set, $0205 hb_clr, $0206 done (1).

Exit: parks in a self-loop; the park address and a magic word are stored
at ROM $7FF0 for the runner to find.
"""

ROM_SIZE = 0x8000

# ---- results (WRAM bank 0, above the default stack) ----
R_FRAMES = 0x0200  # 8-bit, decremented per frame; 0 when done
R_VBLANKS = 0x0201
R_IRQ = 0x0202
R_NMI_ACK = 0x0203
R_HB_SET = 0x0204
R_HB_CLR = 0x0205
R_DONE = 0x0206

NMITIMEN = 0x4200
VTIMEL = 0x4209
VTIMEH = 0x420A
RDNMI = 0x4210
TIMEUP = 0x4211
HVBJOY = 0x4212

# ---- assembly (emulation mode, 8-bit A/X/Y), explicit branch-site pass ----

INSN = []
LABELS = {}
BRANCHES = []  # (program offset of the opcode, opcode, target label)


def emit(label, *chunks):
    if label:
        assert label not in LABELS, f"duplicate label {label}"
        LABELS[label] = sum(len(c) for c in INSN)
    INSN.append(b"".join(chunks))


def op(label, bin_text):
    emit(label, bytes(int(b, 16) for b in bin_text.split()))


def rel(label, opcode, target):
    pos = sum(len(b) for b in INSN)
    BRANCHES.append((pos, opcode, target))
    emit(label, bytes([opcode]) + b"\x00")


def abs24(label, opcode, addr):
    emit(label, bytes([opcode]) + addr.to_bytes(2, "little"))


def imm(label, opcode, value):
    emit(label, bytes([opcode, value]))

FRAMES = 10

op("reset", "78")                    # sei
imm(None, 0xA9, FRAMES)              # lda #FRAMES
abs24(None, 0x8D, R_FRAMES)          # sta R_FRAMES
imm(None, 0xA9, 0x00)                # lda #0
for r in (R_VBLANKS, R_IRQ, R_NMI_ACK, R_HB_SET, R_HB_CLR):
    abs24(None, 0x8D, r)             # sta r
imm(None, 0xA9, 0x20)                # lda #$20
abs24(None, 0x8D, NMITIMEN)          # sta NMITIMEN   (V-IRQ, mode 2)
imm(None, 0xA9, 100)                 # lda #100
abs24(None, 0x8D, VTIMEL)            # sta VTIMEL     (VTIME low)
imm(None, 0xA9, 0x00)                # lda #0
abs24(None, 0x8D, VTIMEH)            # sta VTIMEH     (VTIME high)

op("wait_pre", "AD 12 42")           # wait_pre: lda HVBJOY
rel(None, 0x30, "wait_pre")          #            bmi wait_pre
# (reset lands at H=0,V=0: exits immediately)

op("frame_loop", "AD 12 42")         # frame_loop: lda HVBJOY
rel(None, 0x10, "frame_loop")        #             bpl frame_loop  (VBlank edge)

abs24(None, 0xAD, RDNMI)             # lda RDNMI
imm(None, 0xC9, 0x82)                # cmp #$82  (NMI latch + version 2)
op("nmi_bad", "D0 03")               # bne nmi_bad -> +3
abs24(None, 0xEE, R_NMI_ACK)         # inc R_NMI_ACK
op("irq_check", "AD 11 42")          # nmi_bad: lda TIMEUP
imm(None, 0x29, 0x80)                # and #$80
op("irq_none", "F0 03")              # beq irq_none -> +3
abs24(None, 0xEE, R_IRQ)             # inc R_IRQ
op("after_irq", "EE 01 02")          # irq_none: inc R_VBLANKS

op("wait_vbclr", "AD 12 42")         # wait_vbclr: lda HVBJOY
rel(None, 0x30, "wait_vbclr")        #             bmi wait_vbclr (V=0 edge)
abs24(None, 0xCE, R_FRAMES)          # dec R_FRAMES
rel(None, 0xD0, "frame_loop")        # bne frame_loop

imm(None, 0xA9, 0x00)                # lda #0
abs24(None, 0x8D, R_HB_SET)          # sta R_HB_SET
abs24(None, 0x8D, R_HB_CLR)          # sta R_HB_CLR
imm(None, 0xA2, 200)                 # ldx #200   (sample iterations)

op("hbl_loop", "AD 12 42")           # hbl_loop: lda HVBJOY
imm(None, 0x29, 0x40)                # and #$40
rel(None, 0xF0, "hbl_clr")           # beq hbl_clr (sample clear)
abs24(None, 0xEE, R_HB_SET)          # inc R_HB_SET
rel(None, 0x80, "cont")              # bra cont
op("hbl_clr", "EE 05 02")            # hbl_clr: inc R_HB_CLR
op("cont", "CA")                     # cont: dex
rel(None, 0xD0, "hbl_loop")          # bne hbl_loop

imm(None, 0xA9, 0x01)                # lda #1
abs24(None, 0x8D, R_DONE)            # sta R_DONE
park_pos = sum(len(b) for b in INSN)
op("park", "4C 00 00")               # park: jmp park (patched below)

code = b"".join(INSN)
assert len(code) <= 0x7E00, "program too long"

rom = bytearray(ROM_SIZE)
rom[:len(code)] = code

# Fix the 2-byte relative branches (displacement byte at site+1).
for pos, opcode, target in BRANCHES:
    disp = LABELS[target] - (pos + 2)
    assert -128 <= disp <= 127, f"branch out of range at {pos:#x}"
    rom[pos + 1] = disp & 0xFF

# Fix the park jump: jmp abs at park_pos, absolute address $8000 + offset.
assert rom[park_pos] == 0x4C
park_abs = 0x8000 + park_pos
rom[park_pos + 1] = park_abs & 0xFF
rom[park_pos + 2] = (park_abs >> 8) & 0xFF
assert park_abs <= 0xFFD0, "park collides with the header/vectors"

# ---- LoROM header (map byte $20 = LoROM slow) + vectors + magic ----

rom[0x7FC0:0x7FCF] = b"FASE3 TIMING TEST!"
rom[0x7FD5] = 0x20  # map mode: LoROM, slow
rom[0x7FD7] = 0x05  # ROM size: 32KB (not validated by the core)
rom[0x7FD8] = 0x00  # no SRAM
rom[0x7FDE:0x7FE0] = b"\x00\x00"  # checksum (not validated)

# Magic + park address for the runner: "F3PA" + park (LE) + frames count.
rom[0x7FF0:0x7FF4] = b"F3PA"
rom[0x7FF4:0x7FF6] = park_abs.to_bytes(2, "little")
rom[0x7FF6] = FRAMES

# Vectors (LoROM): reset at $FFFC, NMI/IRQ unused (point at park).
rom[0x7FFC:0x7FFE] = (0x8000).to_bytes(2, "little")
rom[0x7FFE:0x8000] = park_abs.to_bytes(2, "little")


def main():
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "tools/fase3/fase3_timing.sfc"
    with open(out, "wb") as f:
        f.write(rom)
    print(f"wrote {out}: {ROM_SIZE} bytes, park at ${park_abs:06x}")


if __name__ == "__main__":
    main()
