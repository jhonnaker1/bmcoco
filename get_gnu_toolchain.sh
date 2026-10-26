#!/bin/bash
#
# get_gnu_toolchain.sh
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
# Modified for BMA800XL from BMC64's get_gnu_toolchain.sh: BMA800XL's
# cache folder, and the 64-bit aarch64-none-elf toolchain for the Pi 4 and
# Pi 5.
#
# Modified for BMCoco.
#
# Source this file to put the Arm GNU Toolchain used by BMCoco on PATH.
#
#   source ./get_gnu_toolchain.sh
#   BMCOCO_AARCH=64 source ./get_gnu_toolchain.sh   (64-bit, for the Pi 4)
#
# The pinned release is downloaded into ~/.cache/bmcoco/toolchains the
# first time. A matching toolchain already on PATH, or in $ARM_HOME
# ($AARCH64_HOME for 64-bit), is used as it is. Set
# BMCOCO_ALLOW_ANY_TOOLCHAIN=1 to accept whatever arm-none-eabi-gcc (or
# aarch64-none-elf-gcc) is on PATH when the pinned release cannot be
# downloaded.
#
# 32-bit kernels use the arm-none-eabi toolchain, 64-bit ones the
# aarch64-none-elf toolchain Circle expects (PREFIX64 in Circle's Rules.mk).
# Both come from the same Arm GNU Toolchain release, built for this
# computer: Linux (x86_64 or aarch64) or macOS (Apple silicon or Intel).
#

TOOLCHAIN_VERSION=15.2.1
TOOLCHAIN_RELEASE=15.2.rel1
if [ "${BMCOCO_AARCH:-32}" = "64" ]
then
    TOOLCHAIN_TARGET=aarch64-none-elf
    TOOLCHAIN_HOME_VAR=AARCH64_HOME
else
    TOOLCHAIN_TARGET=arm-none-eabi
    TOOLCHAIN_HOME_VAR=ARM_HOME
fi
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) TOOLCHAIN_HOST=darwin-arm64 ;;
    Darwin-x86_64) TOOLCHAIN_HOST=darwin-x86_64 ;;
    Linux-aarch64|Linux-arm64) TOOLCHAIN_HOST=aarch64 ;;
    *) TOOLCHAIN_HOST=x86_64 ;;
esac
TOOLCHAIN_NAME=arm-gnu-toolchain-${TOOLCHAIN_RELEASE}-${TOOLCHAIN_HOST}-${TOOLCHAIN_TARGET}
TOOLCHAIN_ARCHIVE=${TOOLCHAIN_NAME}.tar.xz
TOOLCHAIN_URL=https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/${TOOLCHAIN_RELEASE}/${TOOLCHAIN_ARCHIVE}
TOOLCHAIN_CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/bmcoco/toolchains
TOOLCHAIN_HOME=${BMCOCO_TOOLCHAIN_HOME:-$TOOLCHAIN_CACHE/$TOOLCHAIN_NAME}

toolchain_is_valid()
{
    [ -x "$1/bin/$TOOLCHAIN_TARGET-gcc" ] &&
        [ "$("$1/bin/$TOOLCHAIN_TARGET-gcc" -dumpfullversion 2>/dev/null)" = "$TOOLCHAIN_VERSION" ]
}

toolchain_use()
{
    export "$TOOLCHAIN_HOME_VAR=$1"
    local version
    version=$("$1/bin/$TOOLCHAIN_TARGET-gcc" -dumpfullversion)
    case ":$PATH:" in
        *":$1/bin:"*) ;;
        *) export PATH=$1/bin:$PATH ;;
    esac
    echo "Using Arm GNU Toolchain $version ($TOOLCHAIN_TARGET): $1"
}

toolchain_download()
{
    mkdir -p "$TOOLCHAIN_CACHE" || return 1
    local archive_path=$TOOLCHAIN_CACHE/$TOOLCHAIN_ARCHIVE

    if ! tar -tJf "$archive_path" >/dev/null 2>&1
    then
        rm -f "$archive_path"
        echo "Downloading Arm GNU Toolchain $TOOLCHAIN_RELEASE..."
        if command -v curl >/dev/null 2>&1
        then
            curl --fail --location --retry 3 --output "$archive_path" "$TOOLCHAIN_URL" || return 1
        elif command -v wget >/dev/null 2>&1
        then
            wget --output-document="$archive_path" "$TOOLCHAIN_URL" || return 1
        else
            echo "curl or wget is required to download the toolchain." >&2
            return 1
        fi
    fi

    local install_path=$TOOLCHAIN_CACHE/.${TOOLCHAIN_NAME}.installing
    rm -rf "$install_path"
    mkdir -p "$install_path" || return 1
    echo "Installing Arm GNU Toolchain $TOOLCHAIN_RELEASE..."
    tar -xJf "$archive_path" -C "$install_path" || return 1
    rm -rf "$TOOLCHAIN_HOME"
    mv "$install_path/$TOOLCHAIN_NAME" "$TOOLCHAIN_HOME" || return 1
    rmdir "$install_path"
}

if toolchain_is_valid "${!TOOLCHAIN_HOME_VAR:-}"
then
    TOOLCHAIN_HOME=${!TOOLCHAIN_HOME_VAR}
elif command -v $TOOLCHAIN_TARGET-gcc >/dev/null 2>&1
then
    path_toolchain=$(dirname "$(dirname "$(readlink -f "$(command -v $TOOLCHAIN_TARGET-gcc)")")")
    if toolchain_is_valid "$path_toolchain"
    then
        TOOLCHAIN_HOME=$path_toolchain
    fi
fi

if ! toolchain_is_valid "$TOOLCHAIN_HOME"
then
    toolchain_download || true
fi

if toolchain_is_valid "$TOOLCHAIN_HOME"
then
    toolchain_use "$TOOLCHAIN_HOME"
elif [ "${BMCOCO_ALLOW_ANY_TOOLCHAIN:-0}" = "1" ] && command -v $TOOLCHAIN_TARGET-gcc >/dev/null 2>&1
then
    echo "WARNING: Arm GNU Toolchain $TOOLCHAIN_VERSION ($TOOLCHAIN_TARGET) is not available." >&2
    toolchain_use "$(dirname "$(dirname "$(readlink -f "$(command -v $TOOLCHAIN_TARGET-gcc)")")")"
else
    echo "Arm GNU Toolchain $TOOLCHAIN_VERSION ($TOOLCHAIN_TARGET) could not be installed." >&2
    echo "Set BMCOCO_ALLOW_ANY_TOOLCHAIN=1 to build with the $TOOLCHAIN_TARGET-gcc on PATH." >&2
    return 1 2>/dev/null || exit 1
fi
