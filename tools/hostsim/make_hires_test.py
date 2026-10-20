#!/usr/bin/env python3
#
# Makes a stand-in CoCo 3 ROM (coco3.rom) for the host simulator's Hi-Res
# Mouse test (src/port/hires.c), in DIR. A CoCo 3, as the test reads at
# double speed ($FFD9), which a CoCo 2 can't run (no video, no refresh). It reads the right joystick port
# through the Tandy Hi-Res Joystick Interface over and over, with two
# routines, and leaves the results in memory:
#
#   $0600-$0601  Y, OS-9's JoyDrv routine (6809 version) at 1.79 MHz, 0-639
#   $0602-$0603  X, the same
#   $0604        Y, HIRESMS (a stand-alone reader) at 0.89 MHz, 0-182
#   $0606-$0607  X, HIRESMS at 1.79 MHz, 0-616
#   $0608        passes made
#   $0609        the buttons ($FF00 bits 0-3, 1 = pressed)
#
# "HIRS" is at $05FC, for the simulator's peek command.
#
#   python3 make_hires_test.py DIR
#

import sys


class Asm:
    def __init__(self, origin):
        self.origin = origin
        self.code = bytearray()
        self.labels = {}
        self.fixups = []

    @property
    def pc(self):
        return self.origin + len(self.code)

    def label(self, name):
        self.labels[name] = self.pc

    def emit(self, *data):
        for d in data:
            self.code.append(d & 0xff)

    def word(self, value):
        self.emit(value >> 8, value)

    def rel8(self, opcode, label):
        self.emit(opcode)
        self.fixups.append((len(self.code), label))
        self.emit(0)

    def resolve(self):
        for offset, label in self.fixups:
            d = self.labels[label] - (self.origin + offset + 1)
            assert -128 <= d <= 127, label
            self.code[offset] = d & 0xff


ORG = 0x8000
a = Asm(ORG)

BRA, BSR, BNE, BMI, BPL, BCC, BLO = 0x20, 0x8D, 0x26, 0x2B, 0x2A, 0x24, 0x25

a.label('start')
a.emit(0x1A, 0x50)                     # ORCC #$50
a.emit(0x10, 0xCE); a.word(0x0F00)     # LDS #$0F00
a.emit(0x8E); a.word(0xFF00)           # LDX #$FF00
# PIA0: port A inputs, B outputs, MUX selects (CA2, CB2) as outputs.
a.emit(0x86, 0x30); a.emit(0xA7, 0x01) # LDA #$30 ; STA 1,X  (DDR A)
a.emit(0x6F, 0x84)                     # CLR ,X
a.emit(0x86, 0x34); a.emit(0xA7, 0x01) # LDA #$34 ; STA 1,X  (CA2 low)
a.emit(0x86, 0x30); a.emit(0xA7, 0x03) # LDA #$30 ; STA 3,X  (DDR B)
a.emit(0x86, 0xFF); a.emit(0xA7, 0x02) # LDA #$FF ; STA 2,X  (all outputs)
a.emit(0x86, 0x34); a.emit(0xA7, 0x03) # LDA #$34 ; STA 3,X  (CB2 low: right port)
# PIA1: port A bits 7-1 outputs (the DAC and RS-232 out).
a.emit(0x86, 0x30); a.emit(0xA7, 0x88, 0x21)  # LDA #$30 ; STA $21,X
a.emit(0x86, 0xFE); a.emit(0xA7, 0x88, 0x20)  # LDA #$FE ; STA $20,X (DDR)
a.emit(0x86, 0x34); a.emit(0xA7, 0x88, 0x21)  # LDA #$34 ; STA $21,X
a.emit(0x7F); a.word(0x0608)           # CLR $0608
# "HIRS" just before the results, for the simulator's peek command.
a.emit(0xCC); a.word(0x4849)           # LDD #'HI'
a.emit(0xFD); a.word(0x05FC)           # STD $05FC
a.emit(0xCC); a.word(0x5253)           # LDD #'RS'
a.emit(0xFD); a.word(0x05FE)           # STD $05FE

a.label('loop')
# OS-9 JoyDrv, double speed.
a.emit(0xB7); a.word(0xFFD9)           # STA $FFD9   fast
a.emit(0xA6, 0x01); a.emit(0x8A, 0x08) # LDA 1,X ; ORA #$08   Y axis
a.rel8(BSR, 'os9rd')
a.emit(0xFD); a.word(0x0600)           # STD $0600
a.emit(0xA6, 0x01); a.emit(0x84, 0xF7) # LDA 1,X ; ANDA #$F7  X axis
a.rel8(BSR, 'os9rd')
a.emit(0xFD); a.word(0x0602)           # STD $0602
a.emit(0xB7); a.word(0xFFD8)           # STA $FFD8   slow
# HIRESMS: Y at normal speed, X at double speed.
a.emit(0xA6, 0x01); a.emit(0x8A, 0x08); a.emit(0xA7, 0x01)  # Y axis
a.emit(0x10, 0x8E); a.word(0xFF4A)     # LDY #$FF4A
a.rel8(BSR, 'hiread')
a.emit(0x1F, 0x20)                     # TFR Y,D
a.emit(0x83); a.word(0xFF4A)           # SUBD #$FF4A
a.emit(0xF7); a.word(0x0604)           # STB $0604
a.emit(0xA6, 0x01); a.emit(0x84, 0xF7); a.emit(0xA7, 0x01)  # X axis
a.emit(0xB7); a.word(0xFFD9)           # STA $FFD9   fast
a.emit(0x10, 0x8E); a.word(0xFD98)     # LDY #$FD98
a.rel8(BSR, 'hiread')
a.emit(0x1F, 0x20)                     # TFR Y,D
a.emit(0x83); a.word(0xFD98)           # SUBD #$FD98
a.emit(0xFD); a.word(0x0606)           # STD $0606
a.emit(0xB7); a.word(0xFFD8)           # STA $FFD8   slow
a.emit(0x86, 0xFF); a.emit(0xA7, 0x02) # LDA #$FF ; STA 2,X   no keyboard column
a.emit(0xA6, 0x84); a.emit(0x43)       # LDA ,X ; COMA
a.emit(0x84, 0x0F)                     # ANDA #$0F
a.emit(0xB7); a.word(0x0609)           # STA $0609
a.emit(0x7C); a.word(0x0608)           # INC $0608
a.rel8(BRA, 'loop')

# OS-9 Level 2 JoyDrv, the 6809 hi-res read (L0097). In: A = $FF01 with
# the axis selected. Out: D = 0-639.
a.label('os9rd')
a.emit(0x34, 0x01)                     # PSHS CC
a.emit(0xA7, 0x01)                     # STA 1,X
a.emit(0x86, 0xFF); a.emit(0xA7, 0x88, 0x20)  # LDA #$FF ; STA $20,X
a.emit(0x86, 90)                       # LDA #90
a.label('os9d')
a.emit(0x4A)                           # DECA
a.rel8(BNE, 'os9d')
a.emit(0xCC); a.word(0x0329)           # LDD #$329
a.emit(0x34, 0x02)                     # PSHS A
a.emit(0x86, 0x02)                     # LDA #$02
a.emit(0x1A, 0x50)                     # ORCC #$50
a.emit(0xA7, 0x88, 0x20)               # STA $20,X
a.label('os9l')
a.emit(0xA6, 0x84)                     # LDA ,X
a.rel8(BMI, 'os9hit')
a.emit(0x5A)                           # DECB
a.rel8(BNE, 'os9l')
a.emit(0x6A, 0xE4)                     # DEC ,S
a.rel8(BPL, 'os9l')
a.emit(0x35, 0x02)                     # PULS A
a.rel8(BRA, 'os9max')
a.label('os9hit')
a.emit(0x35, 0x02)                     # PULS A
a.emit(0x5A)                           # DECB
a.emit(0x34, 0x06)                     # PSHS D
a.emit(0xCC); a.word(640)              # LDD #640
a.emit(0xA3, 0xE1)                     # SUBD ,S++
a.rel8(BCC, 'os9pos')
a.emit(0x4F, 0x5F)                     # CLRA ; CLRB
a.emit(0x35, 0x81)                     # PULS CC,PC
a.label('os9pos')
a.emit(0x10, 0x83); a.word(639)        # CMPD #639
a.rel8(BLO, 'os9ret')
a.label('os9max')
a.emit(0xCC); a.word(639)              # LDD #639
a.label('os9ret')
a.emit(0x35, 0x81)                     # PULS CC,PC

# HIRESMS's HIREAD. In: Y = counter start. Out: Y = start + passes.
a.label('hiread')
a.emit(0x86, 0xFF); a.emit(0xA7, 0x88, 0x20)  # LDA #$FF ; STA $20,X
a.emit(0x86, 0x96)                     # LDA #$96
a.label('hrd1')
a.emit(0x4A)                           # DECA
a.rel8(BNE, 'hrd1')
a.emit(0xCC); a.word(0x02E1)           # LDD #$02E1
a.emit(0xA7, 0x88, 0x20)               # STA $20,X
a.label('hrd2')
a.emit(0x5A)                           # DECB
a.rel8(BNE, 'hrd2')
a.label('hrd3')
a.emit(0xA6, 0x84)                     # LDA ,X
a.rel8(BMI, 'hrd4')
a.emit(0x31, 0x21)                     # LEAY 1,Y
a.rel8(BNE, 'hrd3')
a.label('hrd4')
a.emit(0x39)                           # RTS

a.resolve()

# A 32K ROM at $8000, its reset vector (seen at $FFFE) to the start.
rom = bytearray(a.code) + bytearray(0x8000 - len(a.code))
for top in (0x3FFE, 0x7FFE):
    rom[top] = ORG >> 8
    rom[top + 1] = ORG & 0xff
open(sys.argv[1] + '/coco3.rom', 'wb').write(rom)
print('%d bytes' % len(a.code))
