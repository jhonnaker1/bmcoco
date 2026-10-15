#!/bin/bash
#
# Runs the emulator port on the PC (see host_sim.c) through a set of
# scripted tests and checks the results.
#
#   tools/hostsim/run_tests.sh
#   BMCOCO_ROMS=/path/to/roms tools/hostsim/run_tests.sh
#
# The tests run a small stand-in ROM (make_test_rom.py), since the real
# ROMs can't be shipped. With BMCOCO_ROMS set to a folder holding real ROMs
# (bas13.rom, extbas11.rom, disk11.rom, coco3.rom), Color BASIC is tested
# too. Output, including screenshots (.ppm), goes to tools/hostsim/out/.
#

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$HERE/out
SIM=$HERE/build/bmcoco_sim

make -s -C "$HERE" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)" || exit 1

rm -rf "$OUT"
mkdir -p "$OUT/testroms"
python3 "$HERE/make_test_rom.py" "$OUT/testroms" > /dev/null || exit 1

FAILED=0
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1: $2"; FAILED=$((FAILED + 1)); }

# run NAME REFRESH ROMS CMDLINE SCRIPT: runs the simulator in out/NAME, a
# stand-in SD card with ROMS as its roms folder.
run() {
    local name=$1 refresh=$2 roms=$3 cmdline=$4 script=$5
    mkdir -p "$OUT/$name"
    ln -s "$roms" "$OUT/$name/roms"
    (cd "$OUT/$name" && printf "$script" | BMCOCO_REFRESH=$refresh \
        BMCOCO_CMDLINE="root=. $cmdline" "$SIM" > log.txt 2>&1)
}

# check_log NAME REGEX MESSAGE: the log must contain REGEX
check_log() {
    if grep -Eq "$2" "$OUT/$1/log.txt"; then
        pass "$1: $3"
    else
        fail "$1" "$3 (see out/$1/log.txt)"
    fi
}

# check_audio NAME: no underruns, sound was made, every frame presented
check_audio() {
    local log=$OUT/$1/log.txt line frame presented changes
    if grep -q "audio underrun" "$log"; then
        fail "$1" "audio underrun (see out/$1/log.txt)"
    else
        pass "$1: no audio underruns"
    fi
    line=$(grep "frames presented" "$log" | tail -1)
    frame=$(echo "$line" | sed 's/.*frame \([0-9]*\):.*/\1/')
    presented=$(echo "$line" | sed 's/.*: \([0-9]*\) frames presented/\1/')
    if [ -n "$frame" ] && [ "${presented:-0}" -ge "$frame" ]; then
        pass "$1: no dropped frames ($presented/$frame)"
    else
        fail "$1" "frames presented: $line"
    fi
    changes=$(grep "changing samples" "$log" | tail -1 | sed 's/.*changing samples \([0-9]*\).*/\1/')
    if [ "${changes:-0}" -gt 0 ]; then
        pass "$1: sound output"
    else
        fail "$1" "no sound reached the output"
    fi
}

# same NAME A B / differ NAME A B: compare two screenshots of out/NAME,
# leaving out the top 60 lines, where the test ROM counts frames.
screens_equal() {
    python3 - "$1" "$2" <<'EOF'
import sys
def body(path):
    data = open(path, 'rb').read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    width = int(fields[1])
    return data[pos + 1 + 60 * width * 3:]
sys.exit(0 if body(sys.argv[1]) == body(sys.argv[2]) else 1)
EOF
}
same() {
    if [ -f "$OUT/$1/$2.ppm" ] && [ -f "$OUT/$1/$3.ppm" ] && screens_equal "$OUT/$1/$2.ppm" "$OUT/$1/$3.ppm"; then
        pass "$1: $2 and $3 are the same"
    else
        fail "$1" "$2 and $3 differ"
    fi
}
differ() {
    if [ -f "$OUT/$1/$2.ppm" ] && [ -f "$OUT/$1/$3.ppm" ] && ! screens_equal "$OUT/$1/$2.ppm" "$OUT/$1/$3.ppm"; then
        pass "$1: $2 and $3 differ"
    else
        fail "$1" "$2 and $3 should differ"
    fi
}

# has_colour NAME FILE R G B MESSAGE: the screenshot has a pixel of that colour
has_colour() {
    if python3 - "$OUT/$1/$2.ppm" "$3" "$4" "$5" <<'EOF'
import sys
data = open(sys.argv[1], 'rb').read()
fields, pos = [], 0
while len(fields) < 4:
    while data[pos:pos + 1].isspace():
        pos += 1
    end = pos
    while not data[end:end + 1].isspace():
        end += 1
    fields.append(data[pos:end])
    pos = end
pixels = data[pos + 1:]
want = bytes(int(v) for v in sys.argv[2:5])
sys.exit(0 if any(pixels[i:i + 3] == want for i in range(0, len(pixels), 3)) else 1)
EOF
    then
        pass "$1: $6"
    else
        fail "$1" "$6 (no $3,$4,$5 pixel in $2.ppm)"
    fi
}

TESTROMS=$OUT/testroms

echo "Boot, locked to a 60 Hz display"
run boot 60 "$TESTROMS" "" '120 shot a.ppm\n121 shot b.ppm\n600 audio\n601 quit\n'
check_log boot "machine 59\.9[0-9] Hz, synchronised to the display" "NTSC CoCo locked to 60 Hz"
check_log boot "Tandy CoCo 2B \(NTSC|coco2bus|Colour Computer" "a CoCo 2 was chosen"
check_audio boot
if cmp -s "$OUT/boot/a.ppm" "$OUT/boot/b.ppm"; then
    fail boot "the frame counter didn't move from one frame to the next"
else
    pass "boot: a new frame every display refresh"
fi

echo "NTSC CoCo on a 50 Hz display"
run timer 50 "$TESTROMS" "" '600 audio\n601 quit\n'
check_log timer "timer paced" "falls back to timer pacing"
if grep -q "audio underrun" "$OUT/timer/log.txt"; then
    fail timer "audio underrun"
else
    pass "timer: no audio underruns"
fi

echo "PAL CoCo on a 50 Hz display"
run pal 50 "$TESTROMS" "machine=coco2b" '300 audio\n301 quit\n'
check_log pal "machine 50\.[0-9]+ Hz, synchronised to the display" "PAL CoCo locked to 50 Hz"

echo "Keyboard"
run keys 60 "$TESTROMS" "" '120 shot idle.ppm\n130 key 0x04 1\n140 shot a.ppm\n141 key 0x04 0\n150 shot idle2.ppm\n151 quit\n'
differ keys idle a
same keys idle idle2

echo "Joysticks: analog stick, D-pad, GPIO"
# Full left on the stick, the D-pad and a GPIO joystick all read X=0; half
# way on the stick reads in between.
run joy 60 "$TESTROMS" "" '120 pad 0 0\n120 axis 0 32768 32768\n140 shot centre.ppm\n150 axis 0 0 32768\n170 shot stick.ppm\n175 axis 0 16384 32768\n190 shot half.ppm\n200 axis 0 32768 32768\n200 pad 0 0x4000\n220 shot dpad.ppm\n230 pad 0 0\n230 gpio 0 0x04\n250 shot gpio.ppm\n260 gpio 0 0x10\n280 shot fire.ppm\n290 gpio 0 0\n310 shot centre2.ppm\n311 quit\n'
differ joy centre stick
differ joy stick half
differ joy centre half
same joy stick dpad
same joy dpad gpio
differ joy centre fire
same joy centre centre2

echo "Menu"
run menu 60 "$TESTROMS" "" '120 shot before.ppm\n130 key 0x45 1\n131 key 0x45 0\n150 shot menu.ppm\n160 key 0x51 1\n161 key 0x51 0\n170 key 0x28 1\n171 key 0x28 0\n190 shot browse.ppm\n200 key 0x45 1\n201 key 0x45 0\n230 shot after.ppm\n231 quit\n'
has_colour menu menu 255 129 0 "the menu shows its selection bar"
differ menu menu browse
same menu before after

echo "Gamepad opens the menu"
run padmenu 60 "$TESTROMS" "" '120 pad 0 0\n130 pad 0 0x0100\n135 pad 0 0\n150 shot menu.ppm\n151 quit\n'
has_colour padmenu menu 255 129 0 "Home opens the menu"

echo "The power key (Pi 400: Fn+F10) shuts down"
run powerkey 60 "$TESTROMS" "" '150 key 0x66 1\n152 key 0x66 0\n250 quit\n'
check_log powerkey "Shutting down" "the power key while playing"
check_log powerkey "power off requested" "the power key powers off"
run powerkeymenu 60 "$TESTROMS" "" '150 key 0x45 1\n152 key 0x45 0\n180 key 0x66 1\n182 key 0x66 0\n250 quit\n'
check_log powerkeymenu "power off requested" "the power key in the menu powers off"

echo "A Pi too slow for the frame lock"
# Each frame takes 18 ms to emulate, a little over a 60 Hz refresh: locked
# to the display that would be every other refresh (half speed, the sound
# starved). BMCoco paces by the clock instead, and stretches the sound.
(mkdir -p "$OUT/slow" && ln -s "$TESTROMS" "$OUT/slow/roms" && cd "$OUT/slow" && \
    printf '2400 audio\n2401 quit\n' | BMCOCO_REFRESH=60 BMCOCO_SIM_FRAME_MS=18 \
    BMCOCO_CMDLINE="root=." "$SIM" > log.txt 2>&1)
check_log slow "longer than the display's .*timer paced until they fit" "falls back to timer pacing"
check_log slow "BMCoco: load 10[0-9]% of a frame" "logs the load"
line=$(grep "frames presented" "$OUT/slow/log.txt" | tail -1)
frame=$(echo "$line" | sed 's/.*frame \([0-9]*\):.*/\1/')
presented=$(echo "$line" | sed 's/.*: \([0-9]*\) frames presented/\1/')
if [ -n "$frame" ] && [ $((presented * 100 / frame)) -ge 80 ]; then
    pass "slow: runs at $((presented * 100 / frame))% speed (not 50%)"
else
    fail slow "speed: $line"
fi
underruns=$(grep -o 'underruns [0-9]*' "$OUT/slow/log.txt" | tail -1 | grep -o '[0-9]*')
if [ "${underruns:-9999}" -lt 300 ]; then
    pass "slow: the sound keeps going ($underruns underruns)"
else
    fail slow "$underruns sound underruns"
fi

echo "Web UI reboot request"
run reboot 60 "$TESTROMS" "" '120 webui reboot\n200 quit\n'
check_log reboot "reboot requested" "reboots"

echo "Settings are saved"
run settings 60 "$TESTROMS" "" '120 key 0x45 1\n121 key 0x45 0\n130 key 0x0e 1\n131 key 0x0e 0\n140 key 0x28 1\n141 key 0x28 0\n150 key 0x28 1\n151 key 0x28 0\n160 key 0x45 1\n161 key 0x45 0\n200 quit\n'
if grep -q "^typed_keys=0" "$OUT/settings/bmcoco.cfg" 2>/dev/null; then
    pass "settings: Keyboard > Typed Characters saved to bmcoco.cfg"
else
    fail settings "typed_keys=0 not in bmcoco.cfg"
fi

echo "Memory"
# Menu, down six to Memory, up two from 64K to 16K: the machine is reset
# with 16K, the choice saved, and used again at the next boot.
run memory 60 "$TESTROMS" "" '120 key 0x45 1\n121 key 0x45 0\n128 key 0x51 1\n129 key 0x51 0\n130 key 0x51 1\n131 key 0x51 0\n132 key 0x51 1\n133 key 0x51 0\n134 key 0x51 1\n135 key 0x51 0\n136 key 0x51 1\n137 key 0x51 0\n138 key 0x51 1\n139 key 0x51 0\n150 key 0x28 1\n151 key 0x28 0\n160 key 0x52 1\n161 key 0x52 0\n162 key 0x52 1\n163 key 0x52 0\n170 key 0x28 1\n171 key 0x28 0\n300 quit\n'
check_log memory "16K = 16K motherboard RAM" "the menu sets 16K"
if grep -q "^ram_coco2bus=16" "$OUT/memory/bmcoco.cfg" 2>/dev/null; then
    pass "memory: ram_coco2bus=16 saved to bmcoco.cfg"
else
    fail memory "ram_coco2bus=16 not in bmcoco.cfg"
fi
(cd "$OUT/memory" && printf '100 quit\n' | BMCOCO_REFRESH=60 BMCOCO_CMDLINE="root=." "$SIM" > log2.txt 2>&1)
if tail -1 "$OUT/memory/log2.txt" >/dev/null && grep "motherboard RAM" "$OUT/memory/log2.txt" | tail -1 | grep -q "16K = 16K"; then
    pass "memory: 16K again at the next boot"
else
    fail memory "the next boot didn't have 16K (see out/memory/log2.txt)"
fi

echo "Mouse"
# The Mouse in the right port: the pointer to the top left, the bottom
# right, then the left button.
run mouse 60 "$TESTROMS" "joy_right=mjoy0" '100 mouse -2000 -2000 0\n130 shot tl.ppm\n140 mouse 4000 4000 0\n170 shot br.ppm\n180 mouse 0 0 1\n210 shot fire.ppm\n220 mouse -2000 -2000 0\n250 shot tl2.ppm\n251 quit\n'
check_log mouse "port 0 = Mouse" "Mouse plugged into the right port"
differ mouse tl br
differ mouse br fire
same mouse tl tl2

echo "Missing ROMs are named by their files"
run cp400 60 "$TESTROMS" "machine=cp400" '100 quit\n'
check_log cp400 "ROMs not found: cp400bas.rom, cp400extbas.rom" "a CP-400 without its ROMs names them"

echo "Hard disks"
# A card with a stand-in HDB-DOS ROM and a blank hard disk image. Menu,
# Hard Disks (down two), Drive 0, the disks folder, the image: the IDE
# interface is plugged in with it, and both come back at the next boot.
mkdir -p "$OUT/hd/disks" "$OUT/hdroms"
cp "$TESTROMS"/* "$OUT/hdroms/"
head -c 16384 /dev/zero > "$OUT/hdroms/hdblba.rom"
head -c 1048576 /dev/zero > "$OUT/hd/disks/test.ide"
run hd 60 "$OUT/hdroms" "" '120 key 0x45 1\n121 key 0x45 0\n130 key 0x51 1\n131 key 0x51 0\n132 key 0x51 1\n133 key 0x51 0\n140 key 0x28 1\n141 key 0x28 0\n160 key 0x28 1\n161 key 0x28 0\n170 key 0x28 1\n171 key 0x28 0\n180 key 0x28 1\n181 key 0x28 0\n300 quit\n'
check_log hd "Glenside \| IDE interface" "the IDE interface is plugged in"
check_log hd "FILE hdblba.rom" "with HDB-DOS"
if grep -q "^hd0=.*disks/test.ide" "$OUT/hd/bmcoco.cfg" 2>/dev/null && grep -q "^hd_cart=ide" "$OUT/hd/bmcoco.cfg"; then
    pass "hd: the image and the interface saved to bmcoco.cfg"
else
    fail hd "hd0 / hd_cart not in bmcoco.cfg"
fi
(cd "$OUT/hd" && printf '100 quit\n' | BMCOCO_REFRESH=60 BMCOCO_CMDLINE="root=." "$SIM" > log2.txt 2>&1)
if grep -q "IDE interface" "$OUT/hd/log2.txt" && grep -q "test.ide" "$OUT/hd/log2.txt"; then
    pass "hd: the next boot has them again"
else
    fail hd "the next boot didn't have the hard disk (see out/hd/log2.txt)"
fi

if [ -n "${BMCOCO_ROMS:-}" ]; then
    echo "Color BASIC (real ROMs from $BMCOCO_ROMS)"
    run basic 60 "$BMCOCO_ROMS" "" '200 shot boot.ppm\n210 type PRINT "HELLO"\\n\n400 shot hello.ppm\n401 type SOUND 100,30\\n\n600 audio\n601 quit\n'
    check_log basic "Colour BASIC CRC32 valid" "Color BASIC ROM found"
    check_log basic "Disk Extended Colour BASIC" "Disk BASIC ROM found"
    differ basic boot hello
    check_audio basic

    echo "CoCo 3 (real ROM)"
    run coco3 60 "$BMCOCO_ROMS" "machine=coco3" '300 shot boot.ppm\n301 quit\n'
    check_log coco3 "Super Extended Colour BASIC" "CoCo 3 ROM found"
    check_log coco3 "synchronised to the display" "CoCo 3 locked to 60 Hz"

    echo "Becker port (real ROMs, a stand-in DriveWire server)"
    # The IDE interface (a stand-in HDB-DOS: only its Becker port is used)
    # in xroar.conf. BASIC writes A to the port's data register; the server
    # answers Z, which BASIC then reads: status 2 (data waiting), then 90.
    mkdir -p "$OUT/becker/roms"
    for f in "$BMCOCO_ROMS"/*; do ln -s "$f" "$OUT/becker/roms/" 2>/dev/null; done
    head -c 8192 /dev/zero > "$OUT/becker/roms/hdblba.rom"
    printf 'machine coco2bus\nmachine-cart ide\n' > "$OUT/becker/xroar.conf"
    python3 - "$OUT/becker/server.log" <<'EOF' &
import socket, sys
log = open(sys.argv[1], 'w')
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', 65511))
s.listen(1)
s.settimeout(30)
c, _ = s.accept()
c.settimeout(30)
data = c.recv(16)
log.write('got %s\n' % list(data))
log.flush()
c.sendall(b'Z')
c.recv(16)
EOF
    server=$!
    sleep 1
    (cd "$OUT/becker" && printf '%s\n' '300 type POKE &HFF42,65\n' '500 type PRINT PEEK(&HFF41);PEEK(&HFF42)\n' '800 shot result.ppm' '801 quit' | \
        BMCOCO_REFRESH=60 BMCOCO_IP=127.0.0.1 BMCOCO_CMDLINE="root=. becker_port=65511" "$SIM" > log.txt 2>&1)
    wait $server 2>/dev/null
    check_log becker "Becker port: connected to DriveWire server 127.0.0.1:65511" "connects to the server"
    if grep -q "got \[65\]" "$OUT/becker/server.log" 2>/dev/null; then
        pass "becker: the server got what BASIC wrote"
    else
        fail becker "the server didn't get A (see out/becker/server.log)"
    fi
fi

echo
if [ $FAILED -eq 0 ]; then
    echo "All tests passed."
else
    echo "$FAILED test(s) failed."
fi
exit $((FAILED > 0))
