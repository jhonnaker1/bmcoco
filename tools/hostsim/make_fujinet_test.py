#!/usr/bin/env python3
#
# Makes a stand-in HDB-DOS ROM (hdblba.rom) that talks to a FujiNet through
# the IDE interface's Becker port, in DIR. It starts with "DK", so Extended
# BASIC runs it as it boots, and then:
#
#   1. sends FujiNet's DEVICE_READY ($E2 $00) and reads the answer
#   2. unless --plain: sends $E2 $52 $00 $00 $07 $05, a FujiNet command
#      whose bytes look like a DriveWire READ, and reads its one byte answer
#      (the host simulator's stand-in FujiNet answers $F6, DriveWire's "not
#      ready": taken for READ's error, the activity light would show "no
#      disk" in red)
#   3. reads DriveWire drive 0's sector LSN (0 unless --lsn N) with READEX,
#      sends the checksum and reads the error
#
# and puts "FNOK", DEVICE_READY's answer, READEX's error and the sector at
# $0E00 for the simulator's peek command, and waits for ever. --plain is
# for a real FujiNet (FujiNet-PC), which has no command $52 to answer.
#
#   python3 make_fujinet_test.py DIR [--plain] [--lsn N]
#

import sys

ROM = 0xC000
code = bytearray(b'DK')
fixups = []
labels = {}


def emit(*data):
    code.extend(d & 0xff for d in data)


def lbsr(label):
    fixups.append((len(code) + 1, label))
    emit(0x17, 0, 0)


def send(*data):
    for b in data:
        emit(0x86, b)                     # LDA #b
        emit(0xB7, 0xFF, 0x42)            # STA $FF42


SUM = 0x0D00                              # the sector's checksum
OUT = 0x0E00                              # "FNOK", ready, error, sector

emit(0x1A, 0x50)                          # ORCC #$50: no interrupts

send(0xE2, 0x00)                          # DEVICE_READY
lbsr('get')
emit(0xB7, (OUT + 4) >> 8, OUT + 4)       # STA ready

if '--plain' not in sys.argv:
    send(0xE2, 0x52, 0x00, 0x00, 0x07, 0x05)
    lbsr('get')

lsn = int(sys.argv[sys.argv.index('--lsn') + 1]) if '--lsn' in sys.argv else 0
send(0xD2, 0, lsn >> 16, lsn >> 8, lsn)   # READEX drive 0, LSN
emit(0x4F, 0x5F)                          # CLRA, CLRB
emit(0xFD, SUM >> 8, SUM)                 # STD sum
emit(0x8E, (OUT + 6) >> 8, OUT + 6)       # LDX #sector
loop = len(code)
lbsr('get')
emit(0xA7, 0x80)                          # STA ,X+
emit(0x1F, 0x89)                          # TFR A,B
emit(0x4F)                                # CLRA
emit(0xF3, SUM >> 8, SUM)                 # ADDD sum
emit(0xFD, SUM >> 8, SUM)                 # STD sum
emit(0x8C, (OUT + 6 + 256) >> 8, OUT + 6 + 256)   # CMPX #end
emit(0x26, (loop - (len(code) + 2)) & 0xff)       # BNE loop
emit(0xB6, SUM >> 8, SUM)                 # LDA sum (high)
emit(0xB7, 0xFF, 0x42)
emit(0xB6, (SUM + 1) >> 8, SUM + 1)       # LDA sum+1 (low)
emit(0xB7, 0xFF, 0x42)
lbsr('get')
emit(0xB7, (OUT + 5) >> 8, OUT + 5)       # STA error
for i, c in enumerate(b'FNOK'):
    emit(0x86, c)
    emit(0xB7, (OUT + i) >> 8, OUT + i)
emit(0x20, 0xFE)                          # BRA *

# get: waits for the server's next byte, into A
labels['get'] = len(code)
wait = len(code)
emit(0xF6, 0xFF, 0x41)                    # LDB $FF41
emit(0xC4, 0x02)                          # ANDB #2
emit(0x27, (wait - (len(code) + 2)) & 0xff)       # BEQ wait
emit(0xB6, 0xFF, 0x42)                    # LDA $FF42
emit(0x39)                                # RTS

for offset, label in fixups:
    d = labels[label] - (offset + 2)
    code[offset] = (d >> 8) & 0xff
    code[offset + 1] = d & 0xff

open(sys.argv[1] + '/hdblba.rom', 'wb').write(bytes(code) + bytes(8192 - len(code)))
print('%d bytes' % len(code))
