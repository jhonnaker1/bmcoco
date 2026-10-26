#!/bin/sh
#
# version.sh
#
# BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
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
# Modified for BMCoco.
#
# Prints the BMCoco version: the semantic version from the VERSION file
# and the git commit, for example "1.0.0 (288b2f4)".
#   --semver  only the semantic version
#   --git     only the git commit, with "-dirty" if BMCoco's own files
#             have uncommitted changes. Changes inside the submodules
#             (make_all.sh patches Circle) don't count.
#

cd "$(dirname "$0")/.." || exit 1
semver=$(cat VERSION 2>/dev/null) || semver=dev
if git=$(git rev-parse --short HEAD 2>/dev/null); then
    if ! git diff --quiet --ignore-submodules HEAD 2>/dev/null; then
        git="$git-dirty"
    fi
else
    git=unknown
fi
case "$1" in
--semver) echo "$semver" ;;
--git) echo "$git" ;;
*) echo "$semver ($git)" ;;
esac
