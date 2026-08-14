#!/usr/bin/env python3
"""SPC700 disassembler for offline analysis of the SMW APU driver."""
import sys

def load(path):
    with open(path, 'rb') as f:
        return f.read()

# SPC700 opcode table: opcode -> (mnemonic, operand_mode, bytes)
# Modes: imm, dp, dpX, abs, absX, absY, (X), (X)+, [dpX], [dpY],
#        dpdp, dpimm, XY, membit, rel, etc.

def build_table():
    t = {}
    def add(op, m, mode, nbytes):
        t[op] = (m, mode, nbytes)

    # ALU ops: OR/AND/EOR/CMP/ADC/SBC. Each has A,dp / A,abs / A,(X) / A,[dp+X]
    # / A,#imm / dp,dp / (X),(Y) / A,dp+X / A,abs+X / A,abs+Y / A,[dp]+Y
    # / dp,#imm forms.
    for base, name in [(0x00,'or'),(0x20,'and'),(0x40,'eor'),(0x60,'cmp'),
                       (0x80,'adc'),(0xa0,'sbc')]:
        add(base+0x04, name, 'A,dp', 2)
        add(base+0x05, name, 'A,abs', 3)
        add(base+0x06, name, 'A,(X)', 1)
        add(base+0x07, name, 'A,[dp+X]', 2)
        add(base+0x08, name, 'A,#imm', 2)
        add(base+0x09, name, 'dp,dp', 3)   # src,dst
        add(base+0x14, name, 'A,dp+X', 2)
        add(base+0x15, name, 'A,abs+X', 3)
        add(base+0x16, name, 'A,abs+Y', 3)
        add(base+0x17, name, 'A,[dp]+Y', 2)
        add(base+0x18, name, 'dp,#imm', 3) # imm,dp
        add(base+0x19, name, '(X),(Y)', 1)

    # register moves
    t[0xe8]=('mov','A,#imm',2); t[0xcd]=('mov','X,#imm',2); t[0x8d]=('mov','Y,#imm',2)
    t[0x7d]=('mov','A,X',1); t[0x5d]=('mov','X,A',1); t[0xdd]=('mov','A,Y',1)
    t[0xfd]=('mov','Y,A',1); t[0x9d]=('mov','X,SP',1); t[0xbd]=('mov','SP,X',1)
    t[0xe4]=('mov','A,dp',2); t[0xf4]=('mov','A,dp+X',2); t[0xe5]=('mov','A,abs',3)
    t[0xf5]=('mov','A,abs+X',3); t[0xf6]=('mov','A,abs+Y',3); t[0xe6]=('mov','A,(X)',1)
    t[0xbf]=('mov','A,(X)+',1); t[0xf7]=('mov','A,[dp]+Y',2); t[0xe7]=('mov','A,[dp+X]',2)
    t[0xf8]=('mov','X,dp',2); t[0xf9]=('mov','X,dp+Y',2); t[0xe9]=('mov','X,abs',3)
    t[0xeb]=('mov','Y,dp',2); t[0xfb]=('mov','Y,dp+X',2); t[0xec]=('mov','Y,abs',3)
    t[0xba]=('movw','YA,dp',2)
    t[0x8f]=('mov','dp,#imm',3); t[0xfa]=('mov','dp,dp',3)
    t[0xc4]=('mov','dp,A',2); t[0xd8]=('mov','dp,X',2); t[0xcb]=('mov','dp,Y',2)
    t[0xd4]=('mov','dp+X,A',2); t[0xdb]=('mov','dp+X,Y',2); t[0xd9]=('mov','dp+Y,X',2)
    t[0xc5]=('mov','abs,A',3); t[0xc9]=('mov','abs,X',3); t[0xcc]=('mov','abs,Y',3)
    t[0xd5]=('mov','abs+X,A',3); t[0xd6]=('mov','abs+Y,A',3)
    t[0xaf]=('mov','(X)+,A',1); t[0xc6]=('mov','(X),A',1)
    t[0xd7]=('mov','[dp]+Y,A',2); t[0xc7]=('mov','[dp+X],A',2); t[0xda]=('movw','dp,YA',2)

    # push/pop
    t[0x2d]=('push','A',1); t[0x4d]=('push','X',1); t[0x6d]=('push','Y',1)
    t[0x0d]=('push','PSW',1); t[0xae]=('pop','A',1); t[0xce]=('pop','X',1)
    t[0xee]=('pop','Y',1); t[0x8e]=('pop','PSW',1)

    # shift/inc/dec
    for op, name in [(0x0b,'asl'),(0x1b,'asl'),(0x0c,'asl'),
                     (0x2b,'rol'),(0x3b,'rol'),(0x2c,'rol'),
                     (0x4b,'lsr'),(0x5b,'lsr'),(0x4c,'lsr'),
                     (0x6b,'ror'),(0x7b,'ror'),(0x6c,'ror'),
                     (0x8b,'dec'),(0x9b,'dec'),(0x8c,'dec'),
                     (0xab,'inc'),(0xbb,'inc'),(0xac,'inc')]:
        if op & 0x0f == 0x0b: mode='dp'
        elif op & 0x0f == 0x1b: mode='dp+X'
        else: mode='abs'
        t[op]=(name, mode, 2 if mode=='dp' else (2 if mode=='dp+X' else 3))
    t[0x1c]=('asl','A',1); t[0x3c]=('rol','A',1); t[0x5c]=('lsr','A',1); t[0x7c]=('ror','A',1)
    t[0x9c]=('dec','A',1); t[0xbc]=('inc','A',1); t[0x1d]=('dec','X',1); t[0x3d]=('inc','X',1)
    t[0xdc]=('dec','Y',1); t[0xfc]=('inc','Y',1)

    # 16-bit
    t[0x7a]=('addw','YA,dp',2); t[0x9a]=('subw','YA,dp',2); t[0x5a]=('cmpw','YA,dp',2)
    t[0x3a]=('incw','dp',2); t[0x1a]=('decw','dp',2)
    t[0x9e]=('div','YA,X',1); t[0xcf]=('mul','YA',1)

    # bit ops
    for i in range(8):
        t[0x02+(i<<5)]=('set1','dp.%d'%i,2)
        t[0x12+(i<<5)]=('clr1','dp.%d'%i,2)
        t[0x03+(i<<5)]=('bbs','dp.%d,rel'%i,3)
        t[0x13+(i<<5)]=('bbc','dp.%d,rel'%i,3)
    t[0xea]=('not1','membit',3); t[0xca]=('mov1','membit,C',3); t[0xaa]=('mov1','C,membit',3)
    t[0x0a]=('or1','C,membit',3); t[0x2a]=('or1','C,/membit',3)
    t[0x4a]=('and1','C,membit',3); t[0x6a]=('and1','C,/membit',3); t[0x8a]=('eor1','C,membit',3)
    t[0x60]=('clrc','',1); t[0x80]=('setc','',1); t[0xed]=('notc','',1); t[0xe0]=('clrv','',1)

    # special
    t[0xdf]=('daa','',1); t[0xbe]=('das','',1); t[0x9f]=('xcn','A',1)
    t[0x4e]=('tclr1','abs',3); t[0x0e]=('tset1','abs',3)

    # branches
    t[0x10]=('bpl','rel',2); t[0x30]=('bmi','rel',2); t[0x50]=('bvc','rel',2)
    t[0x70]=('bvs','rel',2); t[0x90]=('bcc','rel',2); t[0xb0]=('bcs','rel',2)
    t[0xd0]=('bne','rel',2); t[0xf0]=('beq','rel',2); t[0x2f]=('bra','rel',2)
    t[0x2e]=('cbne','dp,rel',3); t[0xde]=('cbne','dp+X,rel',3)
    t[0xfe]=('dbnz','Y,rel',2); t[0x6e]=('dbnz','dp,rel',3)

    # jumps/calls
    t[0x5f]=('jmp','abs',3); t[0x1f]=('jmp','[abs+X]',3)
    t[0x3f]=('call','abs',3); t[0x4f]=('pcall','uu',2); t[0x6f]=('ret','',1); t[0x7f]=('ret1','',1)
    t[0x0f]=('brk','',1); t[0x00]=('nop','',1); t[0xef]=('sleep','',1); t[0xff]=('stop','',1)
    t[0x20]=('clrp','',1); t[0x40]=('setp','',1); t[0xa0]=('ei','',1); t[0xc0]=('di','',1)

    # TCALL
    for i in range(16):
        t[0x01+(i<<4)]=('tcall','%d'%i,1)

    return t

TABLE = build_table()

def disasm(data, start, count):
    out = []
    addr = start
    for _ in range(count):
        if addr >= len(data):
            break
        op = data[addr]
        if op not in TABLE:
            out.append((addr, op, '???', 'db $%02x'%op))
            addr += 1
            continue
        m, mode, nb = TABLE[op]
        operands = data[addr+1:addr+nb]
        # format operands
        if mode == 'imm' or mode == 'A,#imm':
            pass
        elif mode == 'dp,#imm':
            operands = [operands[1], operands[0]]  # imm then dp in memory; show imm first
        # build text
        txt = ''
        if 'membit' in mode:
            a = operands[0] | ((operands[1] & 0x1f) << 8)
            bit = operands[1] >> 5
            txt = '$%04x.%d' % (a, bit)
        elif mode == 'dp,#imm':
            txt = '$%02x,#$%02x' % (operands[1], operands[0])
        elif mode == 'dp,dp':
            txt = '$%02x,$%02x' % (operands[1], operands[0])  # src,dst in memory
        elif mode in ('A,dp','X,dp','Y,dp','dp,A','dp,X','dp,Y','YA,dp','dp,YA'):
            txt = '$%02x' % operands[0]
        elif mode in ('A,dp+X','Y,dp+X','dp+X,A','dp+X,Y'):
            txt = '$%02x+X' % operands[0]
        elif mode == 'X,dp+Y':
            txt = '$%02x+Y' % operands[0]
        elif mode == 'dp+Y,X':
            txt = '$%02x+Y' % operands[0]
        elif mode in ('A,abs','X,abs','Y,abs','abs,A','abs,X','abs,Y'):
            txt = '$%04x' % (operands[0] | operands[1]<<8)
        elif mode in ('A,abs+X','abs+X,A'):
            txt = '$%04x+X' % (operands[0] | operands[1]<<8)
        elif mode in ('A,abs+Y','abs+Y,A'):
            txt = '$%04x+Y' % (operands[0] | operands[1]<<8)
        elif mode == 'A,(X)':
            txt = '(X)'
        elif mode == 'A,(X)+':
            txt = '(X)+'
        elif mode == '(X),A':
            txt = '(X)'
        elif mode == '(X)+,A':
            txt = '(X)+'
        elif mode == 'A,[dp+X]':
            txt = '[$%02x+X]' % operands[0]
        elif mode == 'A,[dp]+Y':
            txt = '[$%02x]+Y' % operands[0]
        elif mode == '[dp+X],A':
            txt = '[$%02x+X]' % operands[0]
        elif mode == '[dp]+Y,A':
            txt = '[$%02x]+Y' % operands[0]
        elif mode == 'dp.%d' or mode.startswith('dp.') or mode.startswith('dp'):
            pass
        elif 'rel' in mode:
            if mode.startswith('dp.') or mode.startswith('dp'):
                txt = '$%02x' % operands[0]
            else:
                txt = '$%02x' % operands[0]
        elif mode == 'abs':
            txt = '$%04x' % (operands[0] | operands[1]<<8)
        elif mode == 'uu':
            txt = '$%02x' % operands[0]
        elif mode == 'A,#imm':
            txt = '#$%02x' % operands[0]
        elif mode == '':
            txt = ''
        else:
            txt = ' '.join('$%02x'%b for b in operands)
        full = '%s %s' % (m, txt) if txt else m
        # handle set1/clr1/bbs/bbc bit mode
        if mode.startswith('dp.') or ('.' in mode and mode.split('.')[0] in ('set1','clr1','bbs','bbc')):
            full = mode
        # show raw bytes
        bs = ' '.join('%02x'%b for b in data[addr:addr+nb])
        out.append((addr, op, bs, full))
        addr += nb
    return out

if __name__ == '__main__':
    data = load(sys.argv[1])
    start = int(sys.argv[2], 0)
    count = int(sys.argv[3], 0)
    for addr, op, bs, txt in disasm(data, start, count):
        print('%04x: %-8s %s' % (addr, bs, txt))
