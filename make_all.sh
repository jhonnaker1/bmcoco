#!/bin/bash
#
# make_all.sh
#
# BMC64 - A bare metal Commodore emulator for Raspberry Pi
# Copyright (C) 2019 Randy Rossi <randy.rossi@gmail.com>
# Copyright (C) 2026 aminch <https://github.com/aminch>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# Modified for BMA800XL from BMC64's make_all.sh: rewritten for BMA800XL's
# boards, its patches and the atari800 library.
#
# Modified for BMCoco.
#
#   ./make_all.sh [pi0|pi2|pi3|pi4|pi5] [--skip-patches]
#
#   pi3 (default)  kernel8-32.img: Pi 3 and Pi Zero 2 W
#   pi2            kernel7.img: Pi 2
#   pi0            kernel.img: Pi Zero and Zero W (one core)
#   pi4            kernel8-rpi4.img: Pi 4 B and Pi 400, 64-bit, with
#                  armstub8-rpi4.bin (see docs/PI4.md)
#   pi5            kernel_2712.img: Pi 5, Pi 500 and Pi 500+, 64-bit (see
#                  docs/PI5.md)
#
# Builds the third party libraries (circle-stdlib: Circle and newlib, and the
# Circle addons) and then the BMCoco kernel for one board:
#
# --skip-patches leaves Circle as it is instead of resetting it and applying
# src/patches/ again.
# Run clean_all.sh first for a completely fresh build, and before building
# for a different board: the libraries are built for one board at a time.
#

set -e

SRC_DIR=$(cd "$(dirname "$0")" && pwd)
CIRCLE_STDLIB=$SRC_DIR/third_party/circle-stdlib
CIRCLE_HOME=$CIRCLE_STDLIB/libs/circle

BOARD=""
SKIP_PATCHES=0

for arg in "$@"
do
    case "$arg" in
        pi0|pi2|pi3|pi4|pi5) BOARD=$arg ;;
        --skip-patches) SKIP_PATCHES=1 ;;
        *) echo "Usage: $0 [pi0|pi2|pi3|pi4|pi5] [--skip-patches]" >&2; exit 1 ;;
    esac
done
BOARD=${BOARD:-pi3}
RASPI=${BOARD#pi}
# The Pi Zero is Circle's (and the firmware's) Raspberry Pi 1.
if [ "$RASPI" = "0" ]
then
    RASPI=1
fi
# The Pi 4 and 5 builds are 64-bit (AArch64). Circle names their kernels
# kernel8-rpi4.img and kernel_2712.img (TARGET in Circle's Rules.mk for
# RASPPI=4 and 5, AARCH=64).
case "$RASPI" in
    1) KERNEL=kernel.img ;;
    2) KERNEL=kernel7.img ;;
    3) KERNEL=kernel8-32.img ;;
    4) KERNEL=kernel8-rpi4.img ;;
    5) KERNEL=kernel_2712.img ;;
esac
if [ "$RASPI" -ge 4 ]
then
    export BMCOCO_AARCH=64
    CONFIGURE_PREFIX=(-p aarch64-none-elf-)
else
    export BMCOCO_AARCH=32
    CONFIGURE_PREFIX=()
fi

if [ ! -f "$SRC_DIR/sdcard/config.txt" ]
then
    echo "Must be run from the BMCoco root directory." >&2
    exit 1
fi

# newlib only uses makeinfo for its manuals, which BMCoco doesn't need.
# Without it, a stand-in writes empty ones.
if ! command -v makeinfo >/dev/null 2>&1
then
    STUB_DIR="$SRC_DIR/build/makeinfo-stub"
    mkdir -p "$STUB_DIR"
    cat > "$STUB_DIR/makeinfo" <<'STUB'
#!/bin/sh
# Stand-in for makeinfo (see make_all.sh): writes an empty manual.
while [ $# -gt 0 ]
do
    case "$1" in
        -o) shift; : > "$1" ;;
        --version) echo "makeinfo (GNU texinfo) 7.0"; exit 0 ;;
    esac
    shift
done
exit 0
STUB
    chmod +x "$STUB_DIR/makeinfo"
    export PATH="$STUB_DIR:$PATH"
fi

for tool in make patch
do
    if ! command -v $tool >/dev/null 2>&1
    then
        echo "Missing $tool. On Debian/Ubuntu: sudo apt-get install build-essential texinfo" >&2
        echo "On macOS: xcode-select --install; brew install texinfo" >&2
        exit 1
    fi
done

# newlib and the addons would keep objects built for the other board.
# (circle-stdlib's configure writes the board into Circle's Config.mk.)
BUILT_FOR=$(sed -n 's/^RASPPI = //p' "$CIRCLE_HOME/Config.mk" 2>/dev/null || true)
if [ -n "$BUILT_FOR" ] && [ "$BUILT_FOR" != "$RASPI" ]
then
    echo "circle-stdlib is built for the Pi $BUILT_FOR. Run ./clean_all.sh first." >&2
    exit 1
fi

source "$SRC_DIR/get_gnu_toolchain.sh"

# On macOS, circle-stdlib's configure needs a newer bash and GNU getopt
# than the system's: brew install bash gnu-getopt
if [ "$(uname -s)" = "Darwin" ]
then
    BREW_PREFIX=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
    if [ ! -x "$BREW_PREFIX/bin/bash" ] || [ ! -x "$BREW_PREFIX/opt/gnu-getopt/bin/getopt" ]
    then
        echo "On macOS the build needs: brew install bash gnu-getopt" >&2
        exit 1
    fi
    export PATH="$BREW_PREFIX/opt/gnu-getopt/bin:$BREW_PREFIX/bin:$PATH"
fi

# nproc is GNU; macOS has sysctl.
if ! command -v nproc >/dev/null 2>&1
then
    nproc() { sysctl -n hw.ncpu; }
fi

if [ ! -f "$CIRCLE_HOME/Rules.mk" ] || [ ! -f "$SRC_DIR/third_party/xroar/src/xroar.c" ]
then
    echo "Fetching submodules..."
    git -C "$SRC_DIR" submodule update --init third_party/xroar third_party/circle-stdlib
    git -C "$CIRCLE_STDLIB" submodule update --init libs/circle libs/circle-newlib
    git -C "$CIRCLE_HOME" submodule update --init addon/wlan/hostap
fi

echo "=============================================================="
echo "APPLY PATCHES"
echo "=============================================================="
if [ "$SKIP_PATCHES" = "1" ]
then
    echo "Skipping patches"
else
    cd "$CIRCLE_HOME"
    git reset -q --hard HEAD
    git clean -q -fd
    for p in "$SRC_DIR"/src/patches/circle_*.diff
    do
        echo "  $(basename "$p")"
        patch -s -p1 < "$p"
    done
fi

echo "=============================================================="
echo "BUILD CIRCLE-STDLIB for Raspberry Pi $RASPI"
echo "=============================================================="
cd "$CIRCLE_STDLIB"
find . -name config.cache -delete
bash ./configure --raspberrypi=$RASPI --kernel-max-size 16 "${CONFIGURE_PREFIX[@]}"
make -C libs/circle clean >/dev/null 2>&1 || true
make -j"$(nproc)"

echo "=============================================================="
echo "BUILD ADDONS"
echo "=============================================================="
# The Pi 2 and 3 show the picture and play sound through the VideoCore
# (VCHIQ: dispmanx and the audio service). The Pi 4 and 5 builds use
# Circle's own framebuffer and HDMI sound drivers instead and don't need
# those addons.
if [ "$RASPI" -ge 4 ]
then
    ADDONS="fatfs wlan wlan/hostap/wpa_supplicant"
else
    ADDONS="fatfs linux vc4/vchiq vc4/interface/bcm_host vc4/interface/khronos
            vc4/interface/vmcs_host vc4/interface/vcos wlan wlan/hostap/wpa_supplicant"
fi
for addon in $ADDONS
do
    if [ "$addon" = "wlan/hostap/wpa_supplicant" ]
    then
        make -C "$CIRCLE_HOME/addon/$addon" -f Makefile.circle -j"$(nproc)"
    else
        make -C "$CIRCLE_HOME/addon/$addon" -j"$(nproc)"
    fi
done

if [ "$RASPI" = "4" ]
then
    # The ARM stub the Pi 4's firmware starts the 64-bit kernel with; built
    # from Circle's source (see Circle's boot/README, "FIQ support on
    # Raspberry Pi 4, 400, CM4").
    echo "=============================================================="
    echo "BUILD ARMSTUB"
    echo "=============================================================="
    make -C "$CIRCLE_HOME/boot" armstub64
fi

echo "=============================================================="
echo "BUILD KERNEL"
echo "=============================================================="
cd "$SRC_DIR"
make clean >/dev/null
make -j"$(nproc)"
echo
echo "Built $KERNEL for $BOARD"
