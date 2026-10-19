#!/bin/bash
#
# Tests the shared Pi 4/5 ARM smooth scaling and CRT shader (src/av/scaler.*)
# and how a frame is shared between cores (src/av/frameshare.h) on the PC.
# The pictures go to tools/pi4scaler/out/.
#
#   tools/pi4scaler/run_tests.sh
#

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
make -s -C "$HERE"
rm -rf "$HERE/out"
mkdir -p "$HERE/out"
"$HERE/build/pi4scaler_test" "$HERE/out"
echo
# ThreadSanitizer stops on some newer Linux kernels' address layout; run it
# without address space randomisation where setarch is available.
if command -v setarch > /dev/null
then
    setarch "$(uname -m)" -R "$HERE/build/share_test"
else
    "$HERE/build/share_test"
fi
