#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WLAN_FIRMWARE_DIR="$SCRIPT_DIR/third_party/circle-stdlib/libs/circle/addon/wlan/firmware"
FIRMWARE_CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/bmcoco/wlan-firmware"
FIRMWARE_LICENSE_URL_BASE="https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree"

WLAN_FIRMWARE=(
    brcmfmac43430-sdio.bin
    brcmfmac43430-sdio.txt
    brcmfmac43430-sdio.clm_blob
    brcmfmac43436-sdio.bin
    brcmfmac43436-sdio.txt
    brcmfmac43436-sdio.clm_blob
    brcmfmac43436s-sdio.bin
    brcmfmac43436s-sdio.txt
    brcmfmac43455-sdio.bin
    brcmfmac43455-sdio.clm_blob
    brcmfmac43455-sdio.txt
    brcmfmac43456-sdio.bin
    brcmfmac43456-sdio.txt
    brcmfmac43456-sdio.clm_blob
)

WLAN_LICENSES=(
    LICENCE.broadcom_bcm43xx
    LICENCE.cypress
    LICENCE.synaptics
)

if [ "$#" -ne 1 ]
then
    echo "Usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi

OUTPUT_DIRECTORY=${1%/}
firmware_revision=$(sed -n 's/^FIRMWARE[[:space:]]*?=[[:space:]]*//p' \
    "$WLAN_FIRMWARE_DIR/Makefile")

if [ -z "$firmware_revision" ]
then
    echo "Could not determine the WLAN firmware revision." >&2
    exit 1
fi

# The firmware is cached outside the source tree: clean_all.sh resets the
# Circle submodule, where Circle's Makefile would download it.
firmware_cache_dir="$FIRMWARE_CACHE_DIR/$firmware_revision/firmware"
license_cache_dir="$FIRMWARE_CACHE_DIR/$firmware_revision/licenses"
copyright_cache_file="$license_cache_dir/copyright"
firmware_missing=0

for firmware in "${WLAN_FIRMWARE[@]}"
do
    if [ ! -s "$firmware_cache_dir/$firmware" ]
    then
        firmware_missing=1
        break
    fi
done

if [ "$firmware_missing" -eq 1 ]
then
    # Circle's Makefile downloads into the current folder. A temporary one,
    # so a failed download leaves nothing half done in the cache.
    mkdir -p "$FIRMWARE_CACHE_DIR"
    download_dir=$(mktemp -d "$FIRMWARE_CACHE_DIR/.downloading.XXXXXX")
    trap 'rm -rf "$download_dir"' EXIT
    echo "Downloading the Wi-Fi firmware ($firmware_revision)"
    # Circle's Makefile uses wget, which macOS doesn't have: a stand-in
    # passes its "wget -q -O FILE URL" to curl.
    if ! command -v wget >/dev/null 2>&1
    then
        wget_dir=$(mktemp -d "$FIRMWARE_CACHE_DIR/.wget.XXXXXX")
        cat > "$wget_dir/wget" <<'WGET'
#!/bin/sh
# Stand-in for wget (see make_wifi_firmware.sh): wget -q -O FILE URL
out=
while [ $# -gt 1 ]
do
    case "$1" in
        -O) shift; out=$1 ;;
    esac
    shift
done
exec curl --fail --silent --show-error --location --retry 3 -o "$out" "$1"
WGET
        chmod +x "$wget_dir/wget"
        PATH="$wget_dir:$PATH"
        trap 'rm -rf "$download_dir" "$wget_dir"' EXIT
    fi
    make -s -C "$download_dir" -f "$WLAN_FIRMWARE_DIR/Makefile" \
        FIRMWARE="$firmware_revision" firmware
    mkdir -p "$(dirname "$firmware_cache_dir")"
    rm -rf "$firmware_cache_dir"
    mv "$download_dir" "$firmware_cache_dir"
    rm -rf "${wget_dir:-}"
    trap - EXIT
fi

license_missing=0
for license in "${WLAN_LICENSES[@]}"
do
    if [ ! -s "$license_cache_dir/$license" ]
    then
        license_missing=1
        break
    fi
done

if [ "$license_missing" -eq 1 ]
then
    mkdir -p "$license_cache_dir"
    if [ ! -s "$copyright_cache_file" ]
    then
        curl -fsSL --user-agent "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" \
            "$FIRMWARE_LICENSE_URL_BASE/$firmware_revision/debian/copyright" \
            -o "$copyright_cache_file"
    fi

    extract_license()
    {
        local license_name=$1
        local output_file=$2
        awk -v license_name="$license_name" '
            $0 == "License: " license_name {
                printing = 1
                license_text = $0 ORS
                next
            }
            printing && /^License:/ { printing = 0 }
            printing { license_text = license_text $0 ORS }
            END { printf "%s", license_text }
        ' "$copyright_cache_file" > "$output_file"
        test -s "$output_file"
    }

    extract_license "binary-redist-Broadcom-wifi" \
        "$license_cache_dir/LICENCE.broadcom_bcm43xx"
    extract_license "binary-redist-Cypress" \
        "$license_cache_dir/LICENCE.cypress"
    extract_license "Synaptics" "$license_cache_dir/LICENCE.synaptics"
fi

mkdir -p "$OUTPUT_DIRECTORY/firmware"

for firmware in "${WLAN_FIRMWARE[@]}"
do
    cp "$firmware_cache_dir/$firmware" "$OUTPUT_DIRECTORY/firmware/"
done

# Circle looks for the Pi 5's (and Pi 500's) firmware under its own name:
# the 43455 files (Circle's firmware5 target downloads the same ones).
for ext in bin txt clm_blob
do
    cp "$firmware_cache_dir/brcmfmac43455-sdio.$ext" \
        "$OUTPUT_DIRECTORY/firmware/brcmfmac43455-sdio.raspberrypi,5-model-b.$ext"
done

for license in "${WLAN_LICENSES[@]}"
do
    cp "$license_cache_dir/$license" "$OUTPUT_DIRECTORY/firmware/"
done