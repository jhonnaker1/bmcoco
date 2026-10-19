#!/bin/bash
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
