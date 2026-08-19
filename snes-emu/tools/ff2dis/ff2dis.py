#!/usr/bin/env python3
"""65816 linear disassembler for LoROM images (copier header aware).

Usage: ff2dis.py <rom.smc> <bank:addr> <count> [--step]
  addr like 00:8000, 01:80CA, 14:FD12. --step prints byte offsets too.
Maintains M/X state across the listing for correct immediate sizes.
"""
import sys

# (name, size) for each opcode; size is operand bytes.
# Sizes for M/X-dependent immediates are decided by flags (m/x) at disasm time.
T = {}
def impl(op, name): T[op] = (name, 0, 'impl')
def opm(op, name, size, mode=''): T[op] = (name, size, mode or ('dp' if size==1 else 'abs' if size==2 else 'long') if not name in ('bpl','bmi','bvc','bvs','bcc','bcs','bne','beq','bra','brl','rep','sep','brk','cop','wdm','per') else 'imm' if name in ('brk','cop','wdm') else 'rel' if size==1 else ('rel16' if name=='brl' else 'per16'))

# implied
for op, name in [(0x08,'php'),(0x0a,'asl'),(0x0b,'phd'),(0x18,'clc'),(0x1a,'inc'),
  (0x1b,'tcs'),(0x28,'plp'),(0x2a,'rol'),(0x2b,'pld'),(0x38,'sec'),(0x3a,'dec'),
  (0x3b,'tsc'),(0x48,'pha'),(0x4a,'lsr'),(0x4b,'phk'),(0x58,'cli'),(0x5a,'phy'),
  (0x5b,'tcd'),(0x68,'pla'),(0x6a,'ror'),(0x6b,'rtl'),(0x78,'sei'),(0x7a,'ply'),
  (0x7b,'tdc'),(0x88,'dey'),(0x8a,'txa'),(0x8b,'phb'),(0x98,'tya'),(0x9a,'txs'),
  (0x9b,'txy'),(0xa8,'tay'),(0xaa,'tax'),(0xab,'plb'),(0xb8,'clv'),(0xba,'tsx'),
  (0xbb,'tyx'),(0xc8,'iny'),(0xca,'dex'),(0xcb,'wai'),(0xd8,'cld'),(0xda,'phx'),
  (0xdb,'stp'),(0xe8,'inx'),(0xea,'nop'),(0xeb,'xba'),(0xf8,'sed'),(0xfa,'plx'),
  (0xfb,'xce')]:
  impl(op, name)

def imm(op, name): T[op] = (name, -1, 'immA')  # M/X dependent, resolved at print time
def dp(op, name): T[op] = (name, 1, 'dp')
def absm(op, name): T[op] = (name, 2, 'abs')
def long(op, name): T[op] = (name, 3, 'long')

imm(0x00,'brk'); opm(0x02,'cop',1)
imm(0x09,'ora'); imm(0x29,'and'); imm(0x49,'eor'); imm(0x69,'adc')
imm(0x89,'bit'); imm(0xa9,'lda'); imm(0xa0,'ldy'); imm(0xa2,'ldx')
imm(0xc0,'cpy'); imm(0xc9,'cmp'); imm(0xe0,'cpx'); imm(0xe9,'sbc')
opm(0xc2,'rep',1); opm(0xe2,'sep',1)



# direct page
for op, name in [(0x04,'tsb'),(0x05,'ora'),(0x06,'asl'),(0x14,'trb'),(0x15,'ora'),
  (0x16,'asl'),(0x24,'bit'),(0x25,'and'),(0x26,'rol'),(0x34,'bit'),(0x35,'and'),
  (0x36,'rol'),(0x45,'eor'),(0x46,'lsr'),(0x55,'eor'),(0x56,'lsr'),(0x64,'stz'),
  (0x65,'adc'),(0x66,'ror'),(0x74,'stz'),(0x75,'adc'),(0x76,'ror'),(0x84,'sty'),
  (0x85,'sta'),(0x86,'stx'),(0x94,'sty'),(0x95,'sta'),(0x96,'stx'),(0xa4,'ldy'),
  (0xa5,'lda'),(0xa6,'ldx'),(0xb4,'ldy'),(0xb5,'lda'),(0xb6,'ldx'),(0xc4,'cpy'),
  (0xc5,'cmp'),(0xc6,'dec'),(0xd4,'pei'),(0xd5,'cmp'),(0xd6,'dec'),(0xe4,'cpx'),
  (0xe5,'sbc'),(0xe6,'inc'),(0xf4,'pea'),(0xf5,'sbc'),(0xf6,'inc')]:
  dp(op, name)

# absolute
for op, name in [(0x0c,'tsb'),(0x0d,'ora'),(0x0e,'asl'),(0x1c,'trb'),(0x1d,'ora'),
  (0x1e,'asl'),(0x2c,'bit'),(0x2d,'and'),(0x2e,'rol'),(0x3c,'bit'),(0x3d,'and'),
  (0x3e,'rol'),(0x4c,'jmp'),(0x4d,'eor'),(0x4e,'lsr'),(0x5d,'eor'),(0x5e,'lsr'),
  (0x6c,'jmp'),(0x6d,'adc'),(0x6e,'ror'),(0x7c,'jmp'),(0x7d,'adc'),(0x7e,'ror'),
  (0x8c,'sty'),(0x8d,'sta'),(0x8e,'stx'),(0x99,'sta'),(0x9c,'stz'),(0x9d,'sta'),
  (0x9e,'stz'),(0xac,'ldy'),(0xad,'lda'),(0xae,'ldx'),(0xbc,'ldy'),(0xbd,'lda'),
  (0xbe,'ldx'),(0xcc,'cpy'),(0xcd,'cmp'),(0xce,'dec'),(0xdc,'jmp'),(0xdd,'cmp'),
  (0xde,'dec'),(0xec,'cpx'),(0xed,'sbc'),(0xee,'inc'),(0xfe,'inc')]:
  absm(op, name)

# long
for op, name in [(0x0f,'ora'),(0x1f,'ora'),(0x2f,'and'),(0x3f,'and'),(0x4f,'eor'),
  (0x5f,'eor'),(0x6f,'adc'),(0x7f,'adc'),(0x8f,'sta'),(0x9f,'sta'),(0xaf,'lda'),
  (0xbf,'lda'),(0xcf,'cmp'),(0xdf,'cmp'),(0xef,'sbc'),(0xff,'sbc')]:
  long(op, name)

# (dp,X) / (dp),Y / (dp) / [dp] / [dp],Y / stack
for op, name in [(0x01,'ora'),(0x11,'ora'),(0x12,'ora'),(0x07,'ora'),(0x17,'ora'),
  (0x21,'and'),(0x31,'and'),(0x32,'and'),(0x27,'and'),(0x37,'and'),
  (0x41,'eor'),(0x51,'eor'),(0x52,'eor'),(0x47,'eor'),(0x57,'eor'),
  (0x61,'adc'),(0x71,'adc'),(0x72,'adc'),(0x67,'adc'),(0x77,'adc'),
  (0x81,'sta'),(0x91,'sta'),(0x92,'sta'),(0x87,'sta'),(0x97,'sta'),
  (0xa1,'lda'),(0xb1,'lda'),(0xb2,'lda'),(0xa7,'lda'),(0xb7,'lda'),
  (0xc1,'cmp'),(0xd1,'cmp'),(0xd2,'cmp'),(0xc7,'cmp'),(0xd7,'cmp'),
  (0xe1,'sbc'),(0xf1,'sbc'),(0xf2,'sbc'),(0xe7,'sbc'),(0xf7,'sbc')]:
  opm(op, name, 1)
for op, name in [(0x03,'ora'),(0x13,'ora'),(0x23,'and'),(0x33,'and'),
  (0x43,'eor'),(0x53,'eor'),(0x63,'adc'),(0x73,'adc'),
  (0x83,'sta'),(0x93,'sta'),(0xa3,'lda'),(0xb3,'lda'),
  (0xc3,'cmp'),(0xd3,'cmp'),(0xe3,'sbc'),(0xf3,'sbc')]:
  opm(op, name, 1)

# branches / misc
for op, name in [(0x10,'bpl'),(0x30,'bmi'),(0x50,'bvc'),(0x70,'bvs'),
  (0x90,'bcc'),(0xb0,'bcs'),(0xd0,'bne'),(0xf0,'beq'),(0x80,'bra')]:
  opm(op, name, 1)
opm(0x82,'brl',2)
T[0x20] = ('jsr', 2, 'abs'); T[0x44] = ('mvp', 2, 'mv'); T[0x54] = ('mvn', 2, 'mv')
T[0x22] = ('jsl', 3, 'long'); T[0x5c] = ('jml', 3, 'long')
T[0xfc] = ('jsr', 2, 'indX')
opm(0x42,'wdm',1)
opm(0x62,'per',2)

# (abs,X) / (abs)
T[0x6c] = ('jmp', 2, 'ind')
T[0x7c] = ('jmp', 2, 'indX')
T[0xdc] = ('jmp', 2, 'indLong')

# absolute,X / absolute,Y for ALU (search by pattern: base 0x00..0xe0 step 0x20)
for base in [0x00, 0x20, 0x40, 0x60, 0xa0, 0xc0, 0xe0]:
    name = {0x00:'ora',0x20:'and',0x40:'eor',0x60:'adc',0xa0:'lda',0xc0:'cmp',0xe0:'sbc'}[base]
    if base == 0xa0:  # lda already has absX via explicit above; avoid dup is fine
        pass
    opm(base+0x19, name, 2)  # abs,y
    opm(base+0x1d, name, 2)  # abs,x
    opm(base+0x1f, name, 3)  # long,x (covered)
    opm(base+0x15, name, 1)  # dp,x
# ora family absX/absY already set; ensure lda/and/etc. all present either way.
# Use explicit sets not loops to avoid mistakes.

# direct,X and absolute,X for memory ops (asl etc.) - add missing
opm(0x16, 'asl', 1); opm(0x1e, 'asl', 2)
opm(0x36, 'rol', 1); opm(0x3e, 'rol', 2)
opm(0x56, 'lsr', 1); opm(0x5e, 'lsr', 2)
opm(0x76, 'ror', 1); opm(0x7e, 'ror', 2)
opm(0x96, 'stx', 1); opm(0x94, 'sty', 1)
opm(0xb6, 'ldx', 1); opm(0xb4, 'ldy', 1)
opm(0xf6, 'inc', 1); opm(0xd6, 'dec', 1)
opm(0x0d, 'ora', 2); opm(0x0f, 'ora', 3)
opm(0x4d, 'eor', 2); opm(0x4f, 'eor', 3)
opm(0x6d, 'adc', 2); opm(0x6f, 'adc', 3)
opm(0xcd, 'cmp', 2); opm(0xcf, 'cmp', 3)
opm(0xed, 'sbc', 2); opm(0xef, 'sbc', 3)
opm(0xad, 'lda', 2); opm(0xaf, 'lda', 3)
# and family
opm(0x2d, 'and', 2); opm(0x2f, 'and', 3)


HEADER = 0

def load(path):
    global HEADER
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) % 1024 == 512:
        HEADER = 512
        print('// copier header detected (512 bytes)', file=sys.stderr)
    return d

def off(data, addr):
    b = (addr >> 16) & 0xff
    base = b & 0x7f
    if (b & 0x40) == 0 and base < 0x40:
        filebase = base * 0x8000
    else:
        filebase = (base - 0x40) * 0x8000 if base >= 0x40 else base * 0x8000
    return HEADER + filebase + (addr & 0x7fff)

def fmt(addr, name, ops, m, x, mode):
    n = len(ops)
    w = (ops[0] | ops[1] << 8) if n >= 2 else (ops[0] if n >= 1 else 0)
    l = (ops[0] | ops[1] << 8 | ops[2] << 16) if n == 3 else 0
    if mode == 'immA':
        o = ('#$%02x' if m == 8 else '#$%04x') % w
    elif mode == 'dp': o = '$%02x' % ops[0]
    elif mode == 'dpX': o = '$%02x,x' % ops[0]
    elif mode == 'dpY': o = '$%02x,y' % ops[0]
    elif mode == 'abs': o = '$%04x' % w
    elif mode == 'absX': o = '$%04x,x' % w
    elif mode == 'absY': o = '$%04x,y' % w
    elif mode == 'long': o = '$%06x' % l
    elif mode == 'longX': o = '$%06x,x' % l
    elif mode == '(dp,X)': o = '($%02x,x)' % ops[0]
    elif mode == '(dp),Y': o = '($%02x),y' % ops[0]
    elif mode == '(dp)': o = '($%02x)' % ops[0]
    elif mode == '[dp]': o = '[$%02x]' % ops[0]
    elif mode == '[dp],Y': o = '[$%02x],y' % ops[0]
    elif mode == 'sr': o = '$%02x,s' % ops[0]
    elif mode == '(sr,S),Y': o = '($%02x,s),y' % ops[0]
    elif mode == 'ind': o = '($%04x)' % w
    elif mode == 'indX': o = '($%04x,x)' % w
    elif mode == 'indLong': o = '[$%04x]' % w
    elif mode == 'mv': o = '$%02x,$%02x' % (ops[0], ops[1])
    elif mode == 'imm': o = '#$%02x' % ops[0]
    elif mode == 'rel':
        r = ops[0]
        if r >= 0x80: r -= 0x100
        return '%s -> %02x:%04x' % (name, (addr >> 16) & 0xff, ((addr & 0xffff) + 2 + r) & 0xffff)
    elif mode == 'rel16':
        r = w
        if r >= 0x8000: r -= 0x10000
        return '%s -> %02x:%04x' % (name, (addr >> 16) & 0xff, ((addr & 0xffff) + 3 + r) & 0xffff)
    elif mode == 'per16':
        r = w
        if r >= 0x8000: r -= 0x10000
        return '%s operand -> %02x:%04x (data)' % (name, (addr >> 16) & 0xff, ((addr & 0xffff) + 3 + r) & 0xffff)
    elif mode == 'impl': o = ''
    else: o = ''
    return ('%s %s' % (name, o)).strip()

def disasm(data, start, count, show_bytes=False):
    m, x = 8, 8  # assume 8-bit accumulator/index at start (emulation mode)
    a = start
    out = []
    for _ in range(count):
        while off(data, a) >= len(data):
            return out
        opcode = data[off(data, a)]
        if opcode not in T:
            out.append(('%06x' % a, 'db $%02x' % opcode, 1))
            a = (a & 0xff0000) | ((a + 1) & 0xffff)
            continue
        name, size, mode = T[opcode]
        if size == -1:
            size = 1 if (m == 8) else 2
            if name in ('ldx',):
                size = 1 if x == 8 else 2
        end = off(data, a) + 1 + size
        if end > len(data): return out
        raw = data[off(data, a):end]
        ops = raw[1:]
        text = fmt(a, name, ops, m, x, mode)
        if name == 'sep':
            m = 8 if ops[0] & 0x20 else m
            x = 8 if ops[0] & 0x10 else x
        if name == 'rep':
            if ops[0] & 0x20: m = 16
            if ops[0] & 0x10: x = 16
        # annotate branch/jump text with hex target (relative)
        n = a & 0xffff
        if name in ('bpl','bmi','bvc','bvs','bcc','bcs','bne','beq','bra'):
            r = ops[0]
            if r >= 0x80: r -= 0x100
            text += '  -> %02x:%04x' % ((a >> 16) & 0xff, (n + 2 + r) & 0xffff)
        elif name == 'brl':
            r = ops[0] | ops[1] << 8
            if r >= 0x8000: r -= 0x10000
            text += '  -> %02x:%04x' % ((a >> 16) & 0xff, (n + 3 + r) & 0xffff)
        if show_bytes:
            bs = ' '.join('%02x' % b for b in raw)
            out.append(('%06x' % a, bs.ljust(11) + text, 1 + size))
        else:
            out.append(('%06x' % a, text, 1 + size))
        a = (a & 0xff0000) | ((n + 1 + size) & 0xffff)
    return out

def post_modes():
    ix = {0x14,0x15,0x16,0x34,0x35,0x36,0x55,0x56,0x74,0x75,0x76,0x94,0x95,0x96,0xb4,0xb5,0xb6,0xd4,0xd5,0xd6,0xf4,0xf5,0xf6}
    ay = {0x11,0x19,0x31,0x39,0x51,0x59,0x71,0x79,0x91,0x99,0xb1,0xb9,0xd1,0xd9,0xf1,0xf9}
    ax = {0x1c,0x1d,0x1e,0x3c,0x3d,0x3e,0x5d,0x5e,0x7d,0x7e,0x9d,0x9e,0xbd,0xbe,0xdd,0xde,0xfd,0xfe}
    for op in ix: T[op] = (T[op][0], T[op][1], 'dpX' if T[op][1]==1 else 'absX')
    for op in ax: T[op] = (T[op][0], T[op][1], 'absX' if T[op][1]==2 else 'longX')
    for op in ay: T[op] = (T[op][0], T[op][1], 'absY' if T[op][1]==2 else 'dpY')
    for op in [0x01,0x21,0x41,0x61,0x81,0xa1,0xc1,0xe1]: T[op] = (T[op][0],1,'(dp,X)')
    for op in [0x11,0x31,0x51,0x71,0x91,0xb1,0xd1,0xf1]: T[op] = (T[op][0],1,'(dp),Y')
    for op in [0x12,0x32,0x52,0x72,0x92,0xb2,0xd2,0xf2]: T[op] = (T[op][0],1,'(dp)')
    for op in [0x07,0x27,0x47,0x67,0x87,0xa7,0xc7,0xe7]: T[op] = (T[op][0],1,'[dp]')
    for op in [0x17,0x37,0x57,0x77,0x97,0xb7,0xd7,0xf7]: T[op] = (T[op][0],1,'[dp],Y')
    for op in [0x03,0x23,0x43,0x63,0x83,0xa3,0xc3,0xe3]: T[op] = (T[op][0],1,'sr')
    for op in [0x13,0x33,0x53,0x73,0x93,0xb3,0xd3,0xf3]: T[op] = (T[op][0],1,'(sr,S),Y')
    T[0x0d] = ('ora',2,'abs'); T[0x0f] = ('ora',3,'long'); T[0x1f] = ('ora',3,'longX')
    T[0x2d] = ('and',2,'abs'); T[0x2f] = ('and',3,'long'); T[0x3f] = ('and',3,'longX')
    T[0x4d] = ('eor',2,'abs'); T[0x4f] = ('eor',3,'long'); T[0x5f] = ('eor',3,'longX')
    T[0x6d] = ('adc',2,'abs'); T[0x6f] = ('adc',3,'long'); T[0x7f] = ('adc',3,'longX')
    T[0xcd] = ('cmp',2,'abs'); T[0xcf] = ('cmp',3,'long'); T[0xdf] = ('cmp',3,'longX')
    T[0xed] = ('sbc',2,'abs'); T[0xef] = ('sbc',3,'long'); T[0xff] = ('sbc',3,'longX')
    T[0xad] = ('lda',2,'abs'); T[0xaf] = ('lda',3,'long'); T[0xbf] = ('lda',3,'longX')
    T[0x2c] = ('bit',2,'abs'); T[0x3c] = ('bit',2,'absX'); T[0x24] = ('bit',1,'dp'); T[0x34] = ('bit',1,'dpX')
    T[0x0c] = ('tsb',2,'abs'); T[0x1c] = ('tsb',2,'absX'); T[0x04] = ('tsb',1,'dp'); T[0x14] = ('trb',1,'dp')
    T[0x8c] = ('sty',2,'abs'); T[0x8e] = ('stx',2,'abs'); T[0x96] = ('stx',1,'dpY')
    T[0xac] = ('ldy',2,'abs'); T[0xae] = ('ldx',2,'abs'); T[0xbc] = ('ldy',2,'absX'); T[0xbe] = ('ldx',2,'absY')
    T[0xcc] = ('cpy',2,'abs'); T[0xec] = ('cpx',2,'abs')
    T[0xa6] = ('ldx',1,'dp'); T[0xb6] = ('ldx',1,'dpY'); T[0x86] = ('stx',1,'dp')
    T[0xa4] = ('ldy',1,'dp'); T[0x84] = ('sty',1,'dp'); T[0x94] = ('sty',1,'dpX')
    T[0x15] = ('ora',1,'dpX'); T[0x35] = ('and',1,'dpX'); T[0x55] = ('eor',1,'dpX')
    T[0x75] = ('adc',1,'dpX'); T[0x95] = ('sta',1,'dpX'); T[0xd5] = ('cmp',1,'dpX'); T[0xf5] = ('sbc',1,'dpX')
    T[0x19] = ('ora',2,'absY'); T[0x39] = ('and',2,'absY'); T[0x59] = ('eor',2,'absY')
    T[0x79] = ('adc',2,'absY'); T[0x99] = ('sta',2,'absY'); T[0xd9] = ('cmp',2,'absY'); T[0xf9] = ('sbc',2,'absY')
    T[0x1d] = ('ora',2,'absX'); T[0x3d] = ('and',2,'absX'); T[0x5d] = ('eor',2,'absX')
    T[0x7d] = ('adc',2,'absX'); T[0x9d] = ('sta',2,'absX'); T[0xdd] = ('cmp',2,'absX'); T[0xfd] = ('sbc',2,'absX')
    T[0x9e] = ('stz',2,'absX'); T[0x9c] = ('stz',2,'abs')

post_modes()
T[0xc2] = ('rep', 1, 'imm')
T[0xe2] = ('sep', 1, 'imm')
T[0x00] = ('brk', 1, 'imm')
T[0x02] = ('cop', 1, 'imm')
T[0x42] = ('wdm', 1, 'imm')
T[0x60] = ('rts', 0, 'impl')
T[0x40] = ('rti', 0, 'impl')
T[0xf4] = ('pea', 2, 'abs')
T[0x62] = ('per', 2, 'per16')
T[0x89] = ('bit', 1, 'immA')
T[0x9b] = ('txy', 0, 'impl')
T[0xbb] = ('tyx', 0, 'impl')
T[0x08] = ('php', 0, 'impl')
T[0x28] = ('plp', 0, 'impl')
T[0x48] = ('pha', 0, 'impl')
T[0x68] = ('pla', 0, 'impl')
T[0x8b] = ('phb', 0, 'impl')
T[0xab] = ('plb', 0, 'impl')
T[0x0b] = ('phd', 0, 'impl')
T[0x2b] = ('pld', 0, 'impl')
T[0xda] = ('phx', 0, 'impl')
T[0xfa] = ('plx', 0, 'impl')
T[0x5a] = ('phy', 0, 'impl')
T[0x7a] = ('ply', 0, 'impl')
T[0x4b] = ('phk', 0, 'impl')
T[0x5b] = ('tcd', 0, 'impl')
T[0x7b] = ('tdc', 0, 'impl')
T[0x1b] = ('tcs', 0, 'impl')
T[0x3b] = ('tsc', 0, 'impl')
T[0x9a] = ('txs', 0, 'impl')
T[0xba] = ('tsx', 0, 'impl')
T[0xaa] = ('tax', 0, 'impl')
T[0xa8] = ('tay', 0, 'impl')
T[0x8a] = ('txa', 0, 'impl')
T[0x98] = ('tya', 0, 'impl')
T[0xcb] = ('wai', 0, 'impl')
T[0xdb] = ('stp', 0, 'impl')
T[0xea] = ('nop', 0, 'impl')
T[0xeb] = ('xba', 0, 'impl')
T[0xfb] = ('xce', 0, 'impl')
T[0x18] = ('clc', 0, 'impl')
T[0x38] = ('sec', 0, 'impl')
T[0x58] = ('cli', 0, 'impl')
T[0x78] = ('sei', 0, 'impl')
T[0xd8] = ('cld', 0, 'impl')
T[0xf8] = ('sed', 0, 'impl')
T[0x88] = ('dey', 0, 'impl')
T[0xc8] = ('iny', 0, 'impl')
T[0xca] = ('dex', 0, 'impl')
T[0xe8] = ('inx', 0, 'impl')
T[0x1a] = ('inc', 0, 'impl')
T[0x3a] = ('dec', 0, 'impl')
T[0x0a] = ('asl', 0, 'impl')
T[0x2a] = ('rol', 0, 'impl')
T[0x4a] = ('lsr', 0, 'impl')
T[0x6a] = ('ror', 0, 'impl')
T[0xb8] = ('clv', 0, 'impl')
T[0x44] = ('mvp', 2, 'mv')
T[0x54] = ('mvn', 2, 'mv')
T[0x22] = ('jsl', 3, 'long')
T[0x5c] = ('jml', 3, 'long')
T[0x20] = ('jsr', 2, 'abs')
T[0x4c] = ('jmp', 2, 'abs')
T[0xfc] = ('jsr', 2, 'indX')
T[0x6c] = ('jmp', 2, 'ind')
T[0x7c] = ('jmp', 2, 'indX')
T[0xdc] = ('jmp', 2, 'indLong')

if __name__ == '__main__':
    path = sys.argv[1]
    b, addr = sys.argv[2].split(':')
    addr = int(addr, 16) | (int(b, 16) << 16)
    count = int(sys.argv[3])
    show = '--step' in sys.argv[4:]
    data = load(path)
    for a, line, _ in disasm(data, addr, count, show):
        print('%s  %s' % (a, line))