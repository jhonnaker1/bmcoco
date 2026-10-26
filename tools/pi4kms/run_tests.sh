#!/bin/bash
#
# run_tests.sh
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
# Tests the parts of the Pi 4's and Pi 5's direct display drivers
# (src/kms/pi4/, src/kms/pi5/) that are pure calculation, on the PC.
#
#   tools/pi4kms/run_tests.sh
#

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
make -s -C "$HERE"
"$HERE/build/pi4kms_test"
"$HERE/build/pi5kms_test"
