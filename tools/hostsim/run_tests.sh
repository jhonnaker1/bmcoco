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

# The Arrow keys joystick (kjoy0): left arrow reads as the stick full left,
# and left Alt fires.
run kjoy 60 "$TESTROMS" "joy_right=kjoy0" '140 shot centre.ppm\n150 key 0x50 1\n170 shot left.ppm\n171 key 0x50 0\n180 key 0xe2 1\n200 shot fire.ppm\n201 key 0xe2 0\n220 shot centre2.ppm\n221 quit\n'
check_log kjoy "port 0 = Keyboard" "the arrow keys are plugged in"
cp "$OUT/joy/stick.ppm" "$OUT/kjoy/stick.ppm" 2>/dev/null
same kjoy left stick
differ kjoy centre fire
same kjoy centre centre2

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

echo "GPIO joysticks in the menu"
# Menu, then down and fire on GPIO joystick 1: the same page as Down and
# Enter on the keyboard (Disk Drives).
run gpiomenu 60 "$TESTROMS" "gpio_joysticks=1" '120 key 0x45 1\n121 key 0x45 0\n126 shot menu.ppm\n130 gpio 0 0x02\n132 gpio 0 0\n140 gpio 0 0x10\n142 gpio 0 0\n160 shot gpio.ppm\n161 quit\n'
run keymenu 60 "$TESTROMS" "gpio_joysticks=1" '120 key 0x45 1\n121 key 0x45 0\n130 key 0x51 1\n131 key 0x51 0\n140 key 0x28 1\n141 key 0x28 0\n160 shot keys.ppm\n161 quit\n'
cp "$OUT/keymenu/keys.ppm" "$OUT/gpiomenu/keys.ppm" 2>/dev/null
differ gpiomenu menu gpio
same gpiomenu gpio keys

echo "Screen size"
# Menu, down nine to Video, down three to Screen Size, down two from 100%
# to 90%: saved as scale=0.90 (for a TV that cuts off the edges).
run size 60 "$TESTROMS" "" '120 key 0x45 1\n121 key 0x45 0\n128 key 0x51 1\n129 key 0x51 0\n130 key 0x51 1\n131 key 0x51 0\n132 key 0x51 1\n133 key 0x51 0\n134 key 0x51 1\n135 key 0x51 0\n136 key 0x51 1\n137 key 0x51 0\n138 key 0x51 1\n139 key 0x51 0\n140 key 0x51 1\n141 key 0x51 0\n142 key 0x51 1\n143 key 0x51 0\n144 key 0x51 1\n145 key 0x51 0\n150 key 0x28 1\n151 key 0x28 0\n160 key 0x51 1\n161 key 0x51 0\n162 key 0x51 1\n163 key 0x51 0\n164 key 0x51 1\n165 key 0x51 0\n170 key 0x28 1\n171 key 0x28 0\n175 shot sizes.ppm\n180 key 0x51 1\n181 key 0x51 0\n182 key 0x51 1\n183 key 0x51 0\n190 key 0x28 1\n191 key 0x28 0\n200 shot video.ppm\n300 quit\n'
if grep -q "^scale=0.90" "$OUT/size/bmcoco.cfg" 2>/dev/null; then
    pass "size: Video > Screen Size 90% saved as scale=0.90"
else
    fail size "scale=0.90 not in bmcoco.cfg (see out/size/log.txt)"
fi

echo "Memory"
# Menu, down seven to Memory, up two from 64K to 16K: the machine is reset
# with 16K, the choice saved, and used again at the next boot.
run memory 60 "$TESTROMS" "" '120 key 0x45 1\n121 key 0x45 0\n128 key 0x51 1\n129 key 0x51 0\n130 key 0x51 1\n131 key 0x51 0\n132 key 0x51 1\n133 key 0x51 0\n134 key 0x51 1\n135 key 0x51 0\n136 key 0x51 1\n137 key 0x51 0\n138 key 0x51 1\n139 key 0x51 0\n140 key 0x51 1\n141 key 0x51 0\n150 key 0x28 1\n151 key 0x28 0\n160 key 0x52 1\n161 key 0x52 0\n162 key 0x52 1\n163 key 0x52 0\n170 key 0x28 1\n171 key 0x28 0\n300 quit\n'
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

echo "DriveWire drives"
# Menu, down three to DriveWire, down one to Drive 0, the disks folder, the
# image: it is drive 0 of the built-in server, kept in bmcoco.cfg.
mkdir -p "$OUT/dwmenu/disks"
head -c 161280 /dev/zero > "$OUT/dwmenu/disks/nitros9.dsk"
run dwmenu 60 "$TESTROMS" "" '120 key 0x45 1\n121 key 0x45 0\n130 key 0x51 1\n131 key 0x51 0\n132 key 0x51 1\n133 key 0x51 0\n134 key 0x51 1\n135 key 0x51 0\n140 key 0x28 1\n141 key 0x28 0\n150 key 0x51 1\n151 key 0x51 0\n160 key 0x28 1\n161 key 0x28 0\n170 key 0x28 1\n171 key 0x28 0\n180 key 0x28 1\n181 key 0x28 0\n300 quit\n'
if grep -q "^dw0=.*disks/nitros9.dsk" "$OUT/dwmenu/bmcoco.cfg" 2>/dev/null; then
    pass "dwmenu: the image is DriveWire drive 0, saved to bmcoco.cfg"
else
    fail dwmenu "dw0 not in bmcoco.cfg"
fi

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

    echo "CoCo 3 sound on a 60 Hz display"
    # A CoCo 3 (59.69 Hz) locked to 60 Hz runs 0.5% fast: the sound must be
    # slowed to fit, never cut off with the buffer full (which clicks), and
    # PLAY's notes stay in tune (F 349 Hz, E 330 Hz).
    run coco3play 60 "$BMCOCO_ROMS" "machine=coco3" '300 type PLAY"FEFEFE"\\n\n900 audio\n901 quit\n'
    line=$(grep "audio written" "$OUT/coco3play/log.txt" | tail -1)
    max=$(echo "$line" | sed 's/.*after write max \([0-9]*\).*/\1/')
    cap=$(grep "sound open" "$OUT/coco3play/log.txt" | head -1 | sed 's/.*, \([0-9]*\) frames.*/\1/')
    # (with the old one-sample corrections it reached 2645 of 2646)
    if [ -n "$max" ] && [ -n "$cap" ] && [ "$max" -le $((cap * 97 / 100)) ]; then
        pass "coco3play: the sound buffer never fills ($max of $cap)"
    else
        fail coco3play "the sound buffer filled: $line (capacity $cap)"
    fi
    check_audio coco3play

    echo "CoCo 3 sound at 48 kHz (the Pi 4 and 5's HDMI)"
    # The sound must be made at the output's rate: made at 44100 Hz and
    # played at 48000 it ran dry all the time, stretched by 9% (+3900 a
    # second) in jumps. Locked 0.5% fast it should need about -240 (after
    # the first second, which fills the buffer).
    BMCOCO_SIM_RATE=48000 run coco3play48 60 "$BMCOCO_ROMS" "machine=coco3" '300 type PLAY"FEFEFE"\\n\n900 audio\n901 quit\n'
    worst=$(grep "sound rate corrections" "$OUT/coco3play48/log.txt" | tail -n +2 | sed 's/.*corrections [+-]*\([0-9]*\).*/\1/' | sort -n | tail -1)
    if [ "${worst:-0}" -le 1000 ]; then
        pass "coco3play48: the sound made at 48 kHz (corrections at most ${worst:-0} a second)"
    else
        fail coco3play48 "sound rate corrections of $worst a second: not made at the output's rate"
    fi
    check_audio coco3play48

    echo "Disk activity"
    # DOS on a blank disk reads track 34: the light, drive, track, side and
    # sector at the bottom left. On a disk whose sectors are numbered from
    # 2, sector 1 isn't found (DOS retries for ever): that is shown in red.
    mkdir -p "$OUT/disk_ok" "$OUT/disk_rnf"
    python3 -c "open('$OUT/disk_ok/blank.dsk','wb').write(b'\\xff'*35*18*256)"
    python3 -c "open('$OUT/disk_rnf/off.dsk','wb').write(bytes([18,1,1,2])+b'\\xff'*35*18*256)"
    run disk_ok 60 "$BMCOCO_ROMS" "" '200 run blank.dsk\n330 shot busy.ppm\n800 shot idle.ppm\n801 quit\n'
    run disk_rnf 60 "$BMCOCO_ROMS" "" '200 run off.dsk\n900 shot rnf.ppm\n901 quit\n'
    # disk_colours FILE: red and bright green pixels at the bottom left
    disk_colours() {
        python3 - "$1" <<'PYEOF'
import sys
data = open(sys.argv[1], 'rb').read()
parts = data.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
red = green = 0
for y in range(h - 40, h):
    for x in range(0, 420):
        r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
        if r > 200 and g < 60 and b < 60: red += 1
        if g > 230 and r < 60 and b < 60: green += 1
print(red, green)
PYEOF
    }
    read red green <<< "$(disk_colours "$OUT/disk_ok/busy.ppm")"
    if [ "$red" -gt 0 ] && [ "$red" -lt 400 ] && [ "$green" -gt 200 ]; then
        pass "disk_ok: the light and the drive's track and sector while loading"
    else
        fail disk_ok "no disk activity shown (red $red, green $green; see out/disk_ok/busy.ppm)"
    fi
    read red green <<< "$(disk_colours "$OUT/disk_ok/idle.ppm")"
    if [ "$red" -eq 0 ] && [ "$green" -eq 0 ]; then
        pass "disk_ok: gone when the disk is idle"
    else
        fail disk_ok "still shown when idle (see out/disk_ok/idle.ppm)"
    fi
    read red green <<< "$(disk_colours "$OUT/disk_rnf/rnf.ppm")"
    if [ "$red" -gt 1000 ]; then
        pass "disk_rnf: a sector not found shown in red"
    else
        fail disk_rnf "no error shown (red $red; see out/disk_rnf/rnf.ppm)"
    fi

    echo "Becker port (real ROMs, a stand-in DriveWire server)"
    # On the Pi the C library's socket calls hold its open files' lock while
    # they wait: a connect() in the background then hung the Pi as soon as
    # the emulator opened a file. The port must use host_tcp_* instead.
    if grep -nE '(^|[^_a-zA-Z])(connect|recv|send|socket|accept|bind|listen|gethostbyname|getaddrinfo)\(' "$HERE/../../src/port/"*.c > /dev/null; then
        fail sockets "src/port calls the C library's sockets (use host_tcp_*)"
    else
        pass "sockets: the port uses host_tcp_*, not the C library's sockets"
    fi
    # The IDE interface (a stand-in HDB-DOS: only its Becker port is used)
    # in xroar.conf. A BASIC program writes A to the port's data register,
    # waits for the server's answer (Z) and writes it back.
    # becker NAME CONNECT_FRAMES: connecting takes that many frames (as it
    # can on the Pi); the CoCo goes on meanwhile, the port reading empty.
    becker() {
        local name=$1 frames=$2 server
        mkdir -p "$OUT/$name/roms"
        for f in "$BMCOCO_ROMS"/*; do ln -s "$f" "$OUT/$name/roms/" 2>/dev/null; done
        rm -f "$OUT/$name/roms/hdblba.rom"   # a link to the ROM folder's, if it has one
        head -c 8192 /dev/zero > "$OUT/$name/roms/hdblba.rom"
        printf 'machine coco2bus\nmachine-cart ide\n' > "$OUT/$name/xroar.conf"
        python3 - "$OUT/$name/server.log" <<'EOF' &
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
data = c.recv(16)
log.write('got back %s\n' % list(data))
log.flush()
EOF
        server=$!
        sleep 1
        (cd "$OUT/$name" && printf '%s\n' '300 type 10 POKE &HFF42,65\n' '500 type 20 IF PEEK(&HFF41)=0 THEN 20\n' \
            '800 type 30 POKE &HFF42,PEEK(&HFF42)\n' '1100 type RUN\n' '1999 audio' '2000 shot result.ppm' '2001 quit' | \
            BMCOCO_REFRESH=60 BMCOCO_IP=127.0.0.1 BMCOCO_SIM_CONNECT_FRAMES=$frames \
            BMCOCO_CMDLINE="root=. becker_ip=127.0.0.1 becker_port=65511" "$SIM" > log.txt 2>&1)
        wait $server 2>/dev/null
        check_log $name "Becker port: connected to DriveWire server 127.0.0.1:65511" "connects to the server"
        if grep -q "got \[65\]" "$OUT/$name/server.log" 2>/dev/null; then
            pass "$name: the server got what BASIC wrote"
        else
            fail $name "the server didn't get A (see out/$name/server.log)"
        fi
        if grep -q "got back \[90\]" "$OUT/$name/server.log" 2>/dev/null; then
            pass "$name: BASIC read the server's answer"
        else
            fail $name "BASIC didn't read Z (see out/$name/server.log)"
        fi
        # While BASIC waited for Z, the port let the network run between
        # frames (every 0.5 ms), and the CoCo kept every frame.
        yields=$(grep "yields between frames" "$OUT/$name/log.txt" | sed 's/.*\] \([0-9]*\) yields.*/\1/')
        if [ "${yields:-0}" -gt 0 ]; then
            pass "$name: the network polled while waiting for the reply ($yields times)"
        else
            fail $name "no polling while waiting for the reply"
        fi
        line=$(grep "frames presented" "$OUT/$name/log.txt" | tail -1)
        presented=$(echo "$line" | sed 's/.*: \([0-9]*\) frames presented/\1/')
        if [ "${presented:-0}" -ge 1999 ]; then
            pass "$name: no dropped frames ($presented/1999)"
        else
            fail $name "frames presented: $line"
        fi
    }
    echo "The built-in DriveWire server"
    # No server address: the Becker port talks to BMCoco's own server,
    # with test.dsk in drive 0 (sector 1 all 7s). BASIC writes to virtual
    # channel 0, not open (as NitrOS-9 does: just the opcode and channel),
    # reads sector 1 (READEX), sends the checksum, then writes each byte plus one plus
    # the error it got into sector 2 (WRITE): sector 2 of the image must
    # then be all 8s.
    mkdir -p "$OUT/dw/roms" "$OUT/dw/disks"
    for f in "$BMCOCO_ROMS"/*; do ln -s "$f" "$OUT/dw/roms/" 2>/dev/null; done
    rm -f "$OUT/dw/roms/hdblba.rom"
    head -c 8192 /dev/zero > "$OUT/dw/roms/hdblba.rom"
    printf 'machine coco2bus\nmachine-cart ide\n' > "$OUT/dw/xroar.conf"
    python3 -c "import sys; sys.stdout.buffer.write(bytes(256) + bytes([7]) * 256 + bytes(256 * 33))" > "$OUT/dw/disks/test.dsk"
    printf 'dw0=disks/test.dsk\n' > "$OUT/dw/bmcoco.cfg"
    (cd "$OUT/dw" && printf '%s\n' \
        '300 type 10 DIM D(255)\n' \
        '500 type 20 FOR I=0 TO 6:READ A:POKE &HFF42,A:NEXT\n' \
        '900 type 30 S=0:FOR I=0 TO 255:D(I)=PEEK(&HFF42):S=S+D(I):NEXT\n' \
        '1400 type 40 H=INT(S/256):POKE &HFF42,H:POKE &HFF42,S-H*256:E=PEEK(&HFF42)\n' \
        '2000 type 50 FOR I=0 TO 4:READ A:POKE &HFF42,A:NEXT\n' \
        '2400 type 60 T=0:FOR I=0 TO 255:V=D(I)+1+E:POKE &HFF42,V:T=T+V:NEXT\n' \
        '2900 type 70 H=INT(T/256):POKE &HFF42,H:POKE &HFF42,T-H*256:R=PEEK(&HFF42)\n' \
        '3500 type 80 DATA 100,0,210,0,0,0,1,87,0,0,0,2\n' \
        '3900 type RUN\n' \
        '5000 quit' | \
        BMCOCO_REFRESH=60 BMCOCO_CMDLINE="root=." "$SIM" > log.txt 2>&1)
    check_log dw "Becker port: the built-in DriveWire server" "the Becker port uses the built-in server"
    if python3 -c "import sys; d = open(sys.argv[1], 'rb').read(); sys.exit(0 if d[512:768] == bytes([8]) * 256 else 1)" "$OUT/dw/disks/test.dsk"; then
        pass "dw: BASIC read a sector and wrote one through it"
    else
        fail dw "sector 2 of test.dsk isn't all 8s"
    fi

    echo "The built-in DriveWire server's virtual channels"
    # BASIC speaks DriveWire through the Becker port as NitrOS-9 does: opens
    # channel 0 (SERINIT), writes its first line (SERWRITEM), polls SERREAD
    # (fetching what's waiting with SERREADM), sends back what it got, and
    # waits for the channel to be reported closed (SERREAD 16). "tcp
    # connect" must answer OK connected and carry the server's WELCOME (the
    # call made 120 frames late, every frame shown); to a port nobody
    # listens on, FAIL 246; "ATDT" CONNECT and WELCOME; "dw disk show" the
    # drives, then closed.
    dwch_basic() {
        printf '%s\n' '300 type 5 POKE &HFF42,&H45:POKE &HFF42,0\n' \
            "500 type 10 A\$=\"$1\":GOSUB 500\\n" \
            '800 type 20 S$="":FOR K=1 TO 300:POKE &HFF42,&H43:GOSUB 600:A=R:GOSUB 600:IF A=17 THEN N=R:GOSUB 700\n' \
            '1500 type 30 IF A=1 THEN S$=S$+CHR$(R)\n' \
            '1800 type 40 IF A=16 THEN C=1:K=300\n' \
            '2100 type 50 NEXT:IF C=0 THEN A$=S$:GOSUB 500\n' \
            '2500 type 60 FOR K=1 TO 300:POKE &HFF42,&H43:GOSUB 600:A=R:GOSUB 600:IF A=16 THEN C=1:K=300\n' \
            '3200 type 70 NEXT:PRINT S$:IF C THEN PRINT "CLOSED"\n' \
            '3500 type 80 END\n' \
            '3700 type 500 POKE &HFF42,&H64:POKE &HFF42,0:POKE &HFF42,LEN(A$)+1:FOR I=1 TO LEN(A$):POKE &HFF42,ASC(MID$(A$,I,1)):NEXT\n' \
            '4600 type 510 POKE &HFF42,13:RETURN\n' \
            '4900 type 600 IF (PEEK(&HFF41) AND 2)=0 THEN 600\n' \
            '5300 type 610 R=PEEK(&HFF42):RETURN\n' \
            '5600 type 700 POKE &HFF42,&H63:POKE &HFF42,0:POKE &HFF42,N:FOR I=1 TO N:GOSUB 600:S$=S$+CHR$(R):NEXT:RETURN\n' \
            '6500 type RUN\n' '14000 shot result.ppm' '14000 audio' '14001 quit'
    }
    dwch_server() {
        python3 - "$1" "$2" <<'EOF' &
import socket, sys
log = open(sys.argv[1], 'w')
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', int(sys.argv[2])))
s.listen(1)
s.settimeout(120)
c, _ = s.accept()
log.write('connected\n')
log.flush()
c.sendall(b'WELCOME\r\n')
c.settimeout(120)
got = b''
while b'WELCOME' not in got:
    d = c.recv(256)
    if not d:
        break
    got += d
log.write('got back %r\n' % got)
log.flush()
c.close()
EOF
    }
    dwch_run() {
        local name=$1 command=$2
        mkdir -p "$OUT/$name/roms"
        for f in "$BMCOCO_ROMS"/*; do ln -s "$f" "$OUT/$name/roms/" 2>/dev/null; done
        rm -f "$OUT/$name/roms/hdblba.rom"
        head -c 8192 /dev/zero > "$OUT/$name/roms/hdblba.rom"
        printf 'machine coco2bus\nmachine-cart ide\n' > "$OUT/$name/xroar.conf"
        (cd "$OUT/$name" && dwch_basic "$command" | BMCOCO_SIM_CONNECT_FRAMES=120 BMCOCO_IP=127.0.0.1 \
            BMCOCO_CMDLINE="root=." "$SIM" > log.txt 2>&1)
    }
    dwch_server "$OUT/dwch_tcp_server.log" 65519
    server=$!
    sleep 1
    dwch_run dwch_tcp "tcp connect 127.0.0.1 65519"
    kill $server 2>/dev/null; wait $server 2>/dev/null
    check_log dwch_tcp "DriveWire channel 0: connected to 127.0.0.1:65519" "tcp connect connects (in the background)"
    if grep -q "got back .*OK connected to 127.0.0.1:65519.*WELCOME" "$OUT/dwch_tcp_server.log" 2>/dev/null; then
        pass "dwch_tcp: the CoCo got OK connected and the server's WELCOME, and sent them back"
    else
        fail dwch_tcp "the CoCo didn't get OK connected and WELCOME (see out/dwch_tcp_server.log)"
    fi
    check_log dwch_tcp "DriveWire channel 0: the call ended" "the server hanging up is noticed"
    dwch_run dwch_fail "tcp connect 127.0.0.1 65520"
    check_log dwch_fail "DriveWire channel 0: can't connect to 127.0.0.1:65520" "a connection nobody answers fails"
    dwch_server "$OUT/dwch_at_server.log" 65521
    server=$!
    sleep 1
    dwch_run dwch_at "ATDT127.0.0.1:65521"
    kill $server 2>/dev/null; wait $server 2>/dev/null
    if grep -q "got back .*CONNECT.*WELCOME" "$OUT/dwch_at_server.log" 2>/dev/null; then
        pass "dwch_at: ATDT: the CoCo got CONNECT and the server's WELCOME"
    else
        fail dwch_at "ATDT: no CONNECT and WELCOME (see out/dwch_at_server.log)"
    fi
    dwch_run dwch_dw "dw disk show"
    for name in dwch_tcp dwch_fail dwch_at dwch_dw; do
        line=$(grep "frames presented" "$OUT/$name/log.txt" | tail -1)
        presented=$(echo "$line" | sed 's/.*: \([0-9]*\) frames presented/\1/')
        if [ "${presented:-0}" -ge 14000 ]; then
            pass "$name: the CoCo kept going ($presented/14000 frames)"
        else
            fail $name "frames presented: $line"
        fi
    done

    becker becker 0
    echo "Becker port: a server slow to answer doesn't stop the CoCo"
    becker becker_slow 300

    echo "The RS-232 Pak and its virtual modem"
    # Color BASIC dials through the Pak's 6551 ($FF68 data, $FF69 status),
    # as a terminal program does, collects what comes back (its echo, the
    # modem's CONNECT and the server's WELCOME) and sends it to the server,
    # which checks it; then +++ (with quiet before and after) and ATH hang
    # up. The connection is made 120 frames late (in the background), and
    # every frame must still be shown. Then a port nothing listens on (NO
    # CARRIER), and the Pak switched off ($FF68 isn't answered).
    rs232_basic() {
        printf '%s\n' "300 type 10 A\$=\"ATDT127.0.0.1:$1\":GOSUB 100\\n" \
            '700 type 20 B$="":FOR J=1 TO 800:IF PEEK(65385) AND 8 THEN B$=B$+CHR$(PEEK(65384))\n' \
            '1300 type 30 NEXT:A$=B$:GOSUB 100\n' \
            '1600 type 40 FOR J=1 TO 1500:NEXT:POKE 65384,43:POKE 65384,43:POKE 65384,43:FOR J=1 TO 1500:NEXT\n' \
            '2300 type 50 A$="ATH":GOSUB 100:END\n' \
            '2700 type 100 FOR I=1 TO LEN(A$):POKE 65384,ASC(MID$(A$,I,1)):NEXT:POKE 65384,13:RETURN\n' \
            '3400 type RUN\n' '9000 audio' '9001 quit'
    }
    for name in rs232 rs232_nc rs232_off; do
        mkdir -p "$OUT/$name/roms"
        for f in "$BMCOCO_ROMS"/*; do ln -s "$f" "$OUT/$name/roms/" 2>/dev/null; done
    done
    python3 - "$OUT/rs232/server.log" <<'EOF' &
import socket, sys
log = open(sys.argv[1], 'w')
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', 65517))
s.listen(1)
s.settimeout(90)
c, _ = s.accept()
log.write('connected\n')
log.flush()
c.sendall(b'WELCOME\r\n')
c.settimeout(90)
got = b''
while True:
    d = c.recv(256)
    if not d:
        break
    got += d
log.write('got back %r\n' % got)
log.write('closed\n')
log.flush()
EOF
    server=$!
    sleep 1
    (cd "$OUT/rs232" && rs232_basic 65517 | BMCOCO_SIM_CONNECT_FRAMES=120 BMCOCO_IP=127.0.0.1 \
        BMCOCO_CMDLINE="root=. rs232=1" "$SIM" > log.txt 2>&1)
    kill $server 2>/dev/null
    wait $server 2>/dev/null
    check_log rs232 "RS-232 Pak: connected to 127.0.0.1:65517" "ATDT connects (in the background)"
    if grep -q "got back .*CONNECT.*WELCOME" "$OUT/rs232/server.log" 2>/dev/null; then
        pass "rs232: the CoCo got CONNECT and the server's WELCOME, and sent them back"
    else
        fail rs232 "the CoCo didn't get CONNECT and WELCOME (see out/rs232/server.log)"
    fi
    if grep -q "got back .*+++" "$OUT/rs232/server.log" && grep -q "^closed" "$OUT/rs232/server.log" && \
       ! grep -q "the call ended" "$OUT/rs232/log.txt"; then
        pass "rs232: +++ and ATH hung up"
    else
        fail rs232 "+++ and ATH didn't hang up (see out/rs232/server.log and log.txt)"
    fi
    (cd "$OUT/rs232_nc" && rs232_basic 65518 | BMCOCO_SIM_CONNECT_FRAMES=120 BMCOCO_IP=127.0.0.1 \
        BMCOCO_CMDLINE="root=. rs232=1" "$SIM" > log.txt 2>&1)
    check_log rs232_nc "RS-232 Pak: can't connect to 127.0.0.1:65518" "a call nobody answers fails"
    for name in rs232 rs232_nc; do
        line=$(grep "frames presented" "$OUT/$name/log.txt" | tail -1)
        presented=$(echo "$line" | sed 's/.*: \([0-9]*\) frames presented/\1/')
        if [ "${presented:-0}" -ge 9000 ]; then
            pass "$name: the CoCo kept going while dialling ($presented/9000 frames)"
        else
            fail $name "frames presented: $line"
        fi
    done
    (cd "$OUT/rs232_off" && rs232_basic 65518 | BMCOCO_IP=127.0.0.1 \
        BMCOCO_CMDLINE="root=." "$SIM" > log.txt 2>&1)
    if grep -q "RS-232 Pak:" "$OUT/rs232_off/log.txt"; then
        fail rs232_off "the Pak answered while switched off"
    else
        pass "rs232_off: switched off, nothing answers at \$FF68"
    fi

    echo "Becker port: a port that isn't a number"
    mkdir -p "$OUT/badport/roms"
    for f in "$BMCOCO_ROMS"/*; do ln -s "$f" "$OUT/badport/roms/" 2>/dev/null; done
    rm -f "$OUT/badport/roms/hdblba.rom"
    head -c 8192 /dev/zero > "$OUT/badport/roms/hdblba.rom"
    printf 'machine coco2bus\nmachine-cart ide\n' > "$OUT/badport/xroar.conf"
    (cd "$OUT/badport" && printf '%s\n' '300 type PRINT PEEK(&HFF41)\n' '400 quit' | \
        BMCOCO_REFRESH=60 BMCOCO_IP=127.0.0.1 \
        BMCOCO_CMDLINE="root=. becker_ip=127.0.0.1 becker_port=65x11" "$SIM" > log.txt 2>&1)
    check_log badport "port \(65x11\) must be a number from 1 to 65535" "a bad port is reported, not tried"
fi

echo
if [ $FAILED -eq 0 ]; then
    echo "All tests passed."
else
    echo "$FAILED test(s) failed."
fi
exit $((FAILED > 0))
