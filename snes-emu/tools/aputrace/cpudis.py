#!/usr/bin/env python3
"""Minimal 65816 disassembler for reading SMW's CPU-side upload protocol."""
import sys

def load(path):
    with open(path, 'rb') as f:
        return f.read()

# opcode table: name + addressing mode + operand size
# modes: imm8 imm16 immX(by M) immY(by X) dp dpX dpY abs absX absY long longX
#        (dp,X) (dp),Y (dp) [dp] [dp],Y sr sr,Y (sr,S),Y rel rel16 mv
#        ind indX indLong ...
def table():
    t = {}
    # We only need common opcodes for reading SMW upload code.
    def a(op, name, mode, size):
        t[op] = (name, mode, size)
    a(0x00,'brk','imm8',1); a(0x02,'cop','imm8',1); a(0x08,'php','impl',0)
    a(0x0a,'asl','impl',0); a(0x0b,'phd','impl',0); a(0x18,'clc','impl',0)
    a(0x1a,'inc','impl',0); a(0x1b,'tcs','impl',0); a(0x28,'plp','impl',0)
    a(0x2a,'rol','impl',0); a(0x2b,'pld','impl',0); a(0x38,'sec','impl',0)
    a(0x3a,'dec','impl',0); a(0x3b,'tsc','impl',0); a(0x48,'pha','impl',0)
    a(0x4a,'lsr','impl',0); a(0x4b,'phk','impl',0); a(0x58,'cli','impl',0)
    a(0x5a,'phy','impl',0); a(0x5b,'tcd','impl',0); a(0x68,'pla','impl',0)
    a(0x6a,'ror','impl',0); a(0x6b,'rtl','impl',0); a(0x78,'sei','impl',0)
    a(0x7a,'ply','impl',0); a(0x7b,'tdc','impl',0); a(0x88,'dey','impl',0)
    a(0x8a,'txa','impl',0); a(0x8b,'phb','impl',0); a(0x98,'tya','impl',0)
    a(0x9a,'txs','impl',0); a(0x9b,'txy','impl',0); a(0xa8,'tay','impl',0)
    a(0xaa,'tax','impl',0); a(0xab,'plb','impl',0); a(0xb8,'clv','impl',0)
    a(0xba,'tsx','impl',0); a(0xbb,'tyx','impl',0); a(0xc2,'rep','imm8',1)
    a(0xc8,'iny','impl',0); a(0xca,'dex','impl',0); a(0xcb,'wai','impl',0)
    a(0xd8,'cld','impl',0); a(0xda,'phx','impl',0); a(0xdb,'stp','impl',0)
    a(0xe2,'sep','imm8',1); a(0xe8,'inx','impl',0); a(0xea,'nop','impl',0)
    a(0xeb,'xba','impl',0); a(0xf8,'sed','impl',0); a(0xfa,'plx','impl',0)
    a(0xfb,'xce','impl',0)

    # loads
    for op,name in [(0xa9,'lda'),(0xa5,'lda'),(0xad,'lda'),(0xbd,'lda'),(0xb9,'lda'),
                    (0xa1,'lda'),(0xb1,'lda'),(0xb2,'lda'),(0xa3,'lda'),(0xb3,'lda'),
                    (0xaf,'lda'),(0xbf,'lda'),(0xa7,'lda'),(0xb7,'lda')]:
        pass
    a(0xa9,'lda','imm',1); a(0xa5,'lda','dp',1); a(0xad,'lda','abs',2)
    a(0xbd,'lda','absX',2); a(0xb9,'lda','absY',2); a(0xa1,'lda','(dp,X)',1)
    a(0xb1,'lda','(dp),Y',1); a(0xb2,'lda','(dp)',1); a(0xa3,'lda','sr',1)
    a(0xb3,'lda','(sr,S),Y',1); a(0xaf,'lda','long',3); a(0xbf,'lda','longX',3)
    a(0xa7,'lda','[dp]',1); a(0xb7,'lda','[dp],Y',1)
    a(0xa2,'ldx','imm',1); a(0xa6,'ldx','dp',1); a(0xae,'ldx','abs',2)
    a(0xb6,'ldx','dpY',1); a(0xbe,'ldx','absY',2)
    a(0xa0,'ldy','imm',1); a(0xa4,'ldy','dp',1); a(0xac,'ldy','abs',2)
    a(0xb4,'ldy','dpX',1); a(0xbc,'ldy','absX',2)

    # stores
    a(0x85,'sta','dp',1); a(0x8d,'sta','abs',2); a(0x9d,'sta','absX',2)
    a(0x99,'sta','absY',2); a(0x81,'sta','(dp,X)',1); a(0x91,'sta','(dp),Y',1)
    a(0x92,'sta','(dp)',1); a(0x83,'sta','sr',1); a(0x93,'sta','(sr,S),Y',1)
    a(0x8f,'sta','long',3); a(0x9f,'sta','longX',3); a(0x87,'sta','[dp]',1)
    a(0x97,'sta','[dp],Y',1)
    a(0x86,'stx','dp',1); a(0x8e,'stx','abs',2); a(0x96,'stx','dpY',1)
    a(0x84,'sty','dp',1); a(0x8c,'sty','abs',2); a(0x94,'sty','dpX',1)
    a(0x64,'stz','dp',1); a(0x9c,'stz','abs',2); a(0x74,'stz','absX',2); a(0x9e,'stz','absX',2)

    # ALU
    for base,name in [(0x00,'ora'),(0x20,'and'),(0x40,'eor'),(0x60,'adc'),
                      (0x80,'sta'),(0xa0,'lda'),(0xc0,'cmp'),(0xe0,'sbc')]:
        pass
    # cmp
    a(0xc9,'cmp','imm',1); a(0xc5,'cmp','dp',1); a(0xcd,'cmp','abs',2)
    a(0xdd,'cmp','absX',2); a(0xd9,'cmp','absY',2); a(0xc1,'cmp','(dp,X)',1)
    a(0xd1,'cmp','(dp),Y',1); a(0xd2,'cmp','(dp)',1); a(0xc3,'cmp','sr',1)
    a(0xd3,'cmp','(sr,S),Y',1); a(0xcf,'cmp','long',3); a(0xdf,'cmp','longX',3)
    a(0xc7,'cmp','[dp]',1); a(0xd7,'cmp','[dp],Y',1)
    a(0xe0,'cpx','imm',1); a(0xe4,'cpx','dp',1); a(0xec,'cpx','abs',2)
    a(0xc0,'cpy','imm',1); a(0xc4,'cpy','dp',1); a(0xcc,'cpy','abs',2)
    # ora/and/eor/adc/sbc (common)
    for base,name in [(0x00,'ora'),(0x20,'and'),(0x40,'eor'),(0x60,'adc'),(0xe0,'sbc')]:
        a(base+0x09,name,'imm',1); a(base+0x05,name,'dp',1); a(base+0x0d,name,'abs',2)
        a(base+0x1d,name,'absX',2); a(base+0x19,name,'absY',2)
        a(base+0x01,name,'(dp,X)',1); a(base+0x11,name,'(dp),Y',1)
        a(base+0x12,name,'(dp)',1); a(base+0x03,name,'sr',1)
        a(base+0x13,name,'(sr,S),Y',1); a(base+0x0f,name,'long',3)
        a(base+0x1f,name,'longX',3); a(base+0x07,name,'[dp]',1)
        a(base+0x17,name,'[dp],Y',1)
    # inc/dec/rol/ror/lsr/asl mem
    for op,name in [(0xe6,'inc'),(0xc6,'dec'),(0xee,'inc'),(0xce,'dec'),
                    (0xf6,'inc'),(0xd6,'dec'),(0xfe,'inc'),(0xde,'dec')]:
        mode = 'dp' if op in (0xe6,0xc6) else ('dpX' if op in (0xf6,0xd6) else ('abs' if op in (0xee,0xce) else 'absX'))
        a(op,name,mode,1 if mode.startswith('dp') else 2)
    for op,name in [(0x06,'asl'),(0x0e,'asl'),(0x16,'asl'),(0x1e,'asl'),
                    (0x26,'rol'),(0x2e,'rol'),(0x36,'rol'),(0x3e,'rol'),
                    (0x46,'lsr'),(0x4e,'lsr'),(0x56,'lsr'),(0x5e,'lsr'),
                    (0x66,'ror'),(0x6e,'ror'),(0x76,'ror'),(0x7e,'ror')]:
        mode = 'dp' if op in (0x06,0x26,0x46,0x66) else ('dpX' if op in (0x16,0x36,0x56,0x76) else ('abs' if op in (0x0e,0x2e,0x4e,0x6e) else 'absX'))
        a(op,name,mode,1 if mode.startswith('dp') else 2)

    # bit/trb/tsb
    a(0x24,'bit','dp',1); a(0x2c,'bit','abs',2); a(0x89,'bit','imm',1)
    a(0x14,'trb','dp',1); a(0x1c,'trb','abs',2); a(0x04,'tsb','dp',1); a(0x0c,'tsb','abs',2)

    # branches
    for op,name in [(0x10,'bpl'),(0x30,'bmi'),(0x50,'bvc'),(0x70,'bvs'),
                    (0x90,'bcc'),(0xb0,'bcs'),(0xd0,'bne'),(0xf0,'beq'),
                    (0x80,'bra')]:
        a(op,name,'rel',1)
    a(0x82,'brl','rel16',2)

    # jumps
    a(0x4c,'jmp','abs',2); a(0x6c,'jmp','(abs)',2); a(0x7c,'jmp','(abs,X)',2)
    a(0x5c,'jml','long',3); a(0xdc,'jmp','[abs]',2)
    a(0x20,'jsr','abs',2); a(0x22,'jsl','long',3); a(0xfc,'jsr','(abs,X)',2)
    a(0x60,'rts','impl',0); a(0x6b,'rtl','impl',0); a(0x40,'rti','impl',0)
    a(0x44,'mvp','mv',2); a(0x54,'mvn','mv',2)
    a(0xf4,'pea','abs',2); a(0x62,'per','rel16',2); a(0xd4,'pei','(dp)',1)
    return t

T = table()

def disasm(data, addr, count):
    out = []
    a = addr
    for _ in range(count):
        if a >= len(data):
            break
        op = data[a]
        if op not in T:
            out.append((a, 'db $%02x' % op))
            a += 1
            continue
        name, mode, size = T[op]
        operands = data[a+1:a+1+size]
        a += 1 + size
        txt = ''
        if mode == 'imm':
            txt = '#$%02x' % operands[0]
        elif mode == 'dp':
            txt = '$%02x' % operands[0]
        elif mode == 'dpX':
            txt = '$%02x,x' % operands[0]
        elif mode == 'dpY':
            txt = '$%02x,y' % operands[0]
        elif mode == 'abs':
            txt = '$%04x' % (operands[0] | operands[1]<<8)
        elif mode == 'absX':
            txt = '$%04x,x' % (operands[0] | operands[1]<<8)
        elif mode == 'absY':
            txt = '$%04x,y' % (operands[0] | operands[1]<<8)
        elif mode == 'long':
            txt = '$%06x' % (operands[0] | operands[1]<<8 | operands[2]<<16)
        elif mode == 'longX':
            txt = '$%06x,x' % (operands[0] | operands[1]<<8 | operands[2]<<16)
        elif mode == '(dp,X)':
            txt = '($%02x,x)' % operands[0]
        elif mode == '(dp),Y':
            txt = '($%02x),y' % operands[0]
        elif mode == '(dp)':
            txt = '($%02x)' % operands[0]
        elif mode == 'sr':
            txt = '$%02x,s' % operands[0]
        elif mode == '(sr,S),Y':
            txt = '($%02x,s),y' % operands[0]
        elif mode == '[dp]':
            txt = '[$%02x]' % operands[0]
        elif mode == '[dp],Y':
            txt = '[$%02x],y' % operands[0]
        elif mode == 'rel':
            off = operands[0]
            if off >= 0x80: off -= 0x100
            txt = '$%04x' % ((a + off) & 0xffff)
        elif mode == 'rel16':
            off = operands[0] | operands[1]<<8
            if off >= 0x8000: off -= 0x10000
            txt = '$%04x' % ((a + off) & 0xffff)
        elif mode == 'mv':
            txt = '$%02x,$%02x' % (operands[0], operands[1])
        elif mode == 'impl':
            txt = ''
        full = (name + ' ' + txt).strip()
        out.append((a - 1 - size, full))
    return out

if __name__ == '__main__':
    data = load(sys.argv[1])
    # assume no header
    start = int(sys.argv[2], 0)
    count = int(sys.argv[3], 0)
    for addr, txt in disasm(data, start, count):
        print('%06x  %s' % (addr, txt))
