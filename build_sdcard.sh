#!/bin/bash
#
# Builds everything and stages the files for the SD card's boot partition in
# build/sdcard. Copy the contents of that folder to the root of a FAT32
# formatted SD card.
#
#   ./build_sdcard.sh [pi0|pi2|pi3|pi4|pi5] [--skip-patches]
#
#   pi3     kernel8-32.img: Pi 3 and Pi Zero 2 W
#   pi2     kernel7.img: Pi 2
#   pi0     kernel.img: Pi Zero and Zero W
#   pi4     kernel8-rpi4.img and armstub8-rpi4.bin: Pi 4 B and Pi 400
#   pi5     kernel_2712.img: Pi 5, Pi 500 and Pi 500+
#   (none)  all of them, as a release has
#
# Without --skip-patches each board is built from clean_all.sh. With it, the
# third party libraries already built are kept and only the kernel is
# rebuilt, which is much quicker while developing. The libraries are built
# for one board at a time, so --skip-patches without a board rebuilds the
# board they were last built for, and a board they aren't built for is
# built from clean.
#

set -euo pipefail

SRC_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$SRC_DIR/build"
STAGING_DIR="$BUILD_DIR/sdcard"
# circle-stdlib's configure writes the board (RASPPI) into Circle's Config.mk.
CIRCLE_CONFIG="$SRC_DIR/third_party/circle-stdlib/libs/circle/Config.mk"
ALL_BOARDS=(pi0 pi2 pi3 pi4 pi5)
BOARDS=()
SKIP_PATCHES=0

for arg in "$@"
do
    case "$arg" in
        pi0|pi2|pi3|pi4|pi5) BOARDS=("$arg") ;;
        --skip-patches) SKIP_PATCHES=1 ;;
        *) echo "Usage: $0 [pi0|pi2|pi3|pi4|pi5] [--skip-patches]" >&2; exit 1 ;;
    esac
done

cd "$SRC_DIR"

# The board circle-stdlib is built for (pi0, pi2, pi3, pi4, pi5), if any.
# Circle calls the Pi Zero the Pi 1.
built_board() {
    local raspi
    raspi=$(sed -n 's/^RASPPI = //p' "$CIRCLE_CONFIG" 2>/dev/null || true)
    if [ "$raspi" = "1" ]
    then
        echo "pi0"
    elif [ -n "$raspi" ]
    then
        echo "pi$raspi"
    fi
}

kernel_name() {
    case "$1" in
        pi0) echo kernel.img ;;
        pi2) echo kernel7.img ;;
        pi3) echo kernel8-32.img ;;
        pi4) echo kernel8-rpi4.img ;;
        pi5) echo kernel_2712.img ;;
    esac
}

if [ "${#BOARDS[@]}" -eq 0 ]
then
    if [ "$SKIP_PATCHES" -eq 1 ] && [ -n "$(built_board)" ]
    then
        BOARDS=("$(built_board)")
    else
        BOARDS=("${ALL_BOARDS[@]}")
    fi
fi

# The SD card is staged once; each board's build adds its kernel.
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

# Raspberry Pi firmware (with the Pi 5's overlays/ folder), Wi-Fi firmware
# and settings.
cp -r "$SRC_DIR"/sdcard/firmware/* "$STAGING_DIR/"
if ! "$SRC_DIR/make_wifi_firmware.sh" "$STAGING_DIR"
then
    echo "Wi-Fi firmware could not be fetched (it is cached once it has been)." >&2
    exit 1
fi
cp "$SRC_DIR/sdcard/config.txt" "$SRC_DIR/sdcard/cmdline.txt" \
   "$SRC_DIR/sdcard/wpa_supplicant.conf.example" "$STAGING_DIR/"
cp "$SRC_DIR/LICENSE" "$STAGING_DIR/LICENSE.txt"
cp "$SRC_DIR/docs/SDCARD_README.txt" "$STAGING_DIR/README.txt"

# Folders for the user's files. The file browser starts at the root. Each
# gets a README, which also keeps it in zip files and CI artifacts (GitHub
# drops empty folders).
folder_readme() {
    mkdir -p "$STAGING_DIR/$1"
    printf '%s\r\n' "$2" > "$STAGING_DIR/$1/README.txt"
}
folder_readme disks    "Disk images: .dsk .vdk .jvc .os9 .dmk (Disk Drives in the menu, F12)"
folder_readme programs "Machine code programs: .bin .hex .s19 (Run a Program in the menu)"
folder_readme carts    "Cartridge images: .ccc .rom (Cartridge in the menu)"
folder_readme tapes    "Cassette images: .cas .c10 .k7 (Cassette in the menu)"
mkdir -p "$STAGING_DIR/roms"
cp "$SRC_DIR/docs/ROMS.txt" "$STAGING_DIR/roms/ROMS.txt"

for board in "${BOARDS[@]}"
do
    kernel=$(kernel_name "$board")
    echo "=============================================================="
    echo "BUILDING $board ($kernel)"
    echo "=============================================================="
    if [ "$SKIP_PATCHES" -eq 1 ] && [ "$(built_board)" = "$board" ]
    then
        if [ "$board" = "pi4" ] || [ "$board" = "pi5" ]
        then
            export BMCOCO_AARCH=64
        else
            export BMCOCO_AARCH=32
        fi
        source "$SRC_DIR/get_gnu_toolchain.sh"
        make -j"$(nproc)"
    else
        ./clean_all.sh --keep-sdcard
        ./make_all.sh "$board"
    fi

    if [ ! -f "$SRC_DIR/$kernel" ]
    then
        echo "Expected kernel was not created: $kernel" >&2
        exit 1
    fi
    cp "$SRC_DIR/$kernel" "$STAGING_DIR/"
    if [ "$board" = "pi4" ]
    then
        # Built by make_all.sh from Circle's boot/armstub; the next board's
        # clean removes it, so it is copied now.
        cp "$SRC_DIR/third_party/circle-stdlib/libs/circle/boot/armstub8-rpi4.bin" "$STAGING_DIR/"
    fi
done

if ! "$SRC_DIR/check_release_files.sh" "$STAGING_DIR"
then
    echo "BUILD FAILED: the SD card files in $STAGING_DIR are not complete." >&2
    exit 1
fi

echo "Staged boot partition files for ${BOARDS[*]} in $STAGING_DIR ($(tools/version.sh))"
