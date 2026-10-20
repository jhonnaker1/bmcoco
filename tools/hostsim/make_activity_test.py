#!/usr/bin/env python3
#
# Makes a stand-in HDB-DOS ROM (hdblba.rom) for the host simulator's
# activity light test (src/port/activity.c), in DIR. It starts with "DK",
# so Extended BASIC runs it as it boots, as it does Disk BASIC, and then,
# a second and a half apart:
#
#   1. reads IDE drive 0, sector 7 (LBA)            HD0 LSN 7
#   2. reads IDE drive 0 far past the image's end    HD0 ... error
#   3. reads DriveWire drive 0, sector 5             DW0 LSN 5
#   4. reads DriveWire drive 3, which is empty       DW3 ... no disk
#
# and then waits for ever.
#
#   python3 make_activity_test.py DIR
#

import sys

code = bytearray(b'DK')


def emit(*data):
    code.extend(d & 0xff for d in data)


def sta(addr): emit(0xB7, addr >> 8, addr)
def lda_imm(v): emit(0x86, v)
def lda(addr): emit(0xB6, addr >> 8, addr)


def bsr(label):
    fixups.append((len(code) + 1, label))
    emit(0x8D, 0)


fixups = []
labels = {}

emit(0x1A, 0x50)                      # ORCC #$50: no interrupts


def ide_read(lba_hi):
    lda_imm(0xE0); sta(0xFF56)        # LBA, drive 0
    lda_imm(lba_hi); sta(0xFF55)
    lda_imm(0); sta(0xFF54)
    lda_imm(7); sta(0xFF53)
    lda_imm(1); sta(0xFF52)           # one sector
    lda_imm(0x20); sta(0xFF57)        # READ
    lda(0xFF57)                       # the status


def dw_read(drive):
    for b in (0x52, drive, 0, 0, 5):  # OP_READ drive LSN
        lda_imm(b); sta(0xFF42)
    emit(0x8E, 0x01, 0x03)            # LDX #259: error, checksum, sector
    loop = len(code)
    lda(0xFF42)
    emit(0x30, 0x1F)                  # LEAX -1,X
    emit(0x26, (loop - (len(code) + 2)) & 0xff)   # BNE loop


ide_read(0)
bsr('wait')
ide_read(0xFF)
bsr('wait')
dw_read(0)
bsr('wait')
dw_read(3)
emit(0x20, 0xFE)                      # BRA *

# wait: about a second and a half at 0.89 MHz
labels['wait'] = len(code)
emit(0xC6, 3)                         # LDB #3
outer = len(code)
emit(0x8E, 0, 0)                      # LDX #0 (65536 times round)
inner = len(code)
emit(0x30, 0x1F)                      # LEAX -1,X
emit(0x26, (inner - (len(code) + 2)) & 0xff)
emit(0x5A)                            # DECB
emit(0x26, (outer - (len(code) + 2)) & 0xff)
emit(0x39)                            # RTS

for offset, label in fixups:
    d = labels[label] - (offset + 1)
    assert -128 <= d <= 127
    code[offset] = d & 0xff

open(sys.argv[1] + '/hdblba.rom', 'wb').write(bytes(code) + bytes(8192 - len(code)))
print('%d bytes' % len(code))
