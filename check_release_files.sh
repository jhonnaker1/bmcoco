#!/bin/bash
#
# check_release_files.sh
#
# BMC64 - A bare metal Commodore emulator for Raspberry Pi
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
# Modified for BMA800XL from BMC64's check_release_files.sh: BMA800XL's
# release files, and --release, which also refuses a kernel built with
# uncommitted changes.
#
# Modified for BMCoco.
#
# Checks that a staged SD card (build_sdcard.sh's build/sdcard by default)
# has everything a release needs, as BMC64's check_release_files.sh does.
# build_sdcard.sh runs it at the end; the release workflow runs it with
# --release, which also refuses a kernel built with uncommitted changes.
#
#   ./check_release_files.sh [--release] [RELEASE_DIRECTORY]
#

SRC_DIR=$(cd "$(dirname "$0")" && pwd)

# The kernels, one per board. A release has them all; a card built for one
# board (build_sdcard.sh pi3) needs only one.
KERNELS=(
    "kernel.img"        # Pi Zero, Zero W
    "kernel7.img"       # Pi 2
    "kernel8-32.img"    # Pi 3, Pi Zero 2 W
    "kernel8-rpi4.img"  # Pi 4 B, Pi 400 (64-bit)
    "kernel_2712.img"   # Pi 5, Pi 500, Pi 500+ (64-bit)
)

# What the Pi 4 boots with besides its kernel: needed when kernel8-rpi4.img
# is on the card.
PI4_FILES=(
    "start4.elf"
    "fixup4.dat"
    "bcm2711-rpi-4-b.dtb"
    "bcm2711-rpi-400.dtb"
    "armstub8-rpi4.bin"
    "COPYING.linux"
    "LICENCE.broadcom"
)

# What the Pi 5 boots with besides its kernel (its firmware is in its
# EEPROM): needed when kernel_2712.img is on the card.
PI5_FILES=(
    "bcm2712-rpi-5-b.dtb"
    "bcm2712d0-rpi-5-b.dtb"
    "bcm2712-rpi-500.dtb"
    "overlays/bcm2712d0.dtbo"
    "COPYING.linux"
)

# Files that must be there and not empty.
REQUIRED_FILES=(
    # Raspberry Pi firmware
    "bootcode.bin"
    "start.elf"
    "fixup.dat"
    "config.txt"
    "cmdline.txt"

    # Documents and settings examples
    "LICENSE.txt"
    "README.txt"
    "licenses/README.txt"
    "licenses/xroar.txt"
    "licenses/LGPL-3.0.txt"
    "licenses/Apache-2.0.txt"
    "licenses/crt-pi.txt"
    "licenses/GPL-2.0.txt"
    "licenses/Linux-V3D.txt"
    "licenses/Mesa-MIT.txt"
    "licenses/newlib.txt"
    "licenses/libgloss.txt"
    "licenses/GCC-Runtime.txt"
    "wpa_supplicant.conf.example"

    # Wi-Fi firmware and redistribution licences (build_sdcard.sh only
    # warns if these could not be fetched)
    "firmware/brcmfmac43430-sdio.bin"
    "firmware/brcmfmac43430-sdio.clm_blob"
    "firmware/brcmfmac43430-sdio.txt"
    "firmware/brcmfmac43436-sdio.bin"
    "firmware/brcmfmac43436-sdio.clm_blob"
    "firmware/brcmfmac43436-sdio.txt"
    "firmware/brcmfmac43436s-sdio.bin"
    "firmware/brcmfmac43436s-sdio.txt"
    "firmware/brcmfmac43455-sdio.bin"
    "firmware/brcmfmac43455-sdio.clm_blob"
    "firmware/brcmfmac43455-sdio.raspberrypi,5-model-b.bin"
    "firmware/brcmfmac43455-sdio.raspberrypi,5-model-b.clm_blob"
    "firmware/brcmfmac43455-sdio.raspberrypi,5-model-b.txt"
    "firmware/brcmfmac43455-sdio.txt"
    "firmware/brcmfmac43456-sdio.bin"
    "firmware/brcmfmac43456-sdio.clm_blob"
    "firmware/brcmfmac43456-sdio.txt"
    "firmware/LICENCE.broadcom_bcm43xx"
    "firmware/LICENCE.cypress"
    "firmware/LICENCE.synaptics"

    # User folders: the READMEs keep them in zip files
    "disks/README.txt"
    "programs/README.txt"
    "carts/README.txt"
    "tapes/README.txt"
    "roms/ROMS.txt"
)

RELEASE=0
if [ "${1:-}" = "--release" ]
then
    RELEASE=1
    shift
fi

if [ "$#" -gt 1 ]
then
    echo "Usage: $0 [--release] [RELEASE_DIRECTORY]" >&2
    exit 2
fi

RELEASE_DIRECTORY=${1:-$SRC_DIR/build/sdcard}
RELEASE_DIRECTORY=${RELEASE_DIRECTORY%/}

if [ ! -d "$RELEASE_DIRECTORY" ]
then
    echo "Release directory does not exist: $RELEASE_DIRECTORY" >&2
    exit 2
fi

problems=0

for required_file in "${REQUIRED_FILES[@]}"
do
    if [ ! -f "$RELEASE_DIRECTORY/$required_file" ]
    then
        echo "Missing: $required_file"
        problems=1
    elif [ ! -s "$RELEASE_DIRECTORY/$required_file" ]
    then
        echo "Empty: $required_file"
        problems=1
    fi
done

# Every notice in licenses/ must have text, and the copied ones must be those
# of the circle-stdlib the kernels are built with.
for notice in "$RELEASE_DIRECTORY"/licenses/*.txt
do
    if [ ! -s "$notice" ]
    then
        echo "Empty: licenses/$(basename "$notice")"
        problems=1
    fi
done
for notice in \
    "newlib.txt:third_party/circle-stdlib/libs/circle-newlib/COPYING.NEWLIB" \
    "libgloss.txt:third_party/circle-stdlib/libs/circle-newlib/COPYING.LIBGLOSS"
do
    if [ -f "$SRC_DIR/${notice#*:}" ] &&
       [ -f "$RELEASE_DIRECTORY/licenses/${notice%%:*}" ] &&
       ! cmp -s "$RELEASE_DIRECTORY/licenses/${notice%%:*}" "$SRC_DIR/${notice#*:}"
    then
        echo "licenses/${notice%%:*} differs from ${notice#*:}"
        problems=1
    fi
done

# The kernels must be this version (and for a release, built from committed
# files).
version="BMCoco $("$SRC_DIR/tools/version.sh" --semver) ($("$SRC_DIR/tools/version.sh" --git),"
kernels_found=0
for kernel in "${KERNELS[@]}"
do
    path="$RELEASE_DIRECTORY/$kernel"
    if [ ! -s "$path" ]
    then
        if [ "$RELEASE" -eq 1 ]
        then
            echo "Missing or empty: $kernel"
            problems=1
        fi
        continue
    fi
    kernels_found=$((kernels_found + 1))
    if ! grep -qaF "$version" "$path"
    then
        echo "$kernel is not \"$version ...\": rebuild it"
        problems=1
    fi
    if [ "$RELEASE" -eq 1 ] && grep -qaE "BMCoco [^ ]+ \([0-9a-f]+-dirty," "$path"
    then
        echo "$kernel was built with uncommitted changes (-dirty)"
        problems=1
    fi
done
if [ -s "$RELEASE_DIRECTORY/kernel8-rpi4.img" ]
then
    for pi4_file in "${PI4_FILES[@]}"
    do
        if [ ! -s "$RELEASE_DIRECTORY/$pi4_file" ]
        then
            echo "Missing or empty (needed by the Pi 4): $pi4_file"
            problems=1
        fi
    done
fi

if [ -s "$RELEASE_DIRECTORY/kernel_2712.img" ]
then
    for pi5_file in "${PI5_FILES[@]}"
    do
        if [ ! -s "$RELEASE_DIRECTORY/$pi5_file" ]
        then
            echo "Missing or empty (needed by the Pi 5): $pi5_file"
            problems=1
        fi
    done
fi

if [ "$kernels_found" -eq 0 ]
then
    echo "Missing: a kernel (${KERNELS[*]})"
    problems=1
fi

if [ "$problems" -ne 0 ]
then
    echo "Release files in $RELEASE_DIRECTORY are not complete." >&2
    exit 1
fi

echo "Release files in $RELEASE_DIRECTORY are complete."
