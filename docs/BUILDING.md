# Build Instructions

BMCoco builds on Linux (Debian, Ubuntu, WSL) and macOS.

## Prerequisites

On Debian or Ubuntu:

```
sudo apt-get install build-essential git python3 curl xz-utils
```

On macOS (Apple silicon or Intel), with the Xcode command line tools and
[Homebrew](https://brew.sh):

```
xcode-select --install
brew install bash gnu-getopt
```

circle-stdlib's `configure` needs bash 4 or later and GNU `getopt`, which
`make_all.sh` puts first on the PATH on macOS. newlib's build would like
`makeinfo` (texinfo) for its manuals and Circle's Wi-Fi firmware download
uses `wget`; without them, `make_all.sh` and `make_wifi_firmware.sh` use
stand-ins (the manuals aren't needed, and `curl` does the download).

The Arm GNU Toolchain 15.2.rel1 is downloaded into
`~/.cache/bmcoco/toolchains` the first time a build needs it
(`get_gnu_toolchain.sh`), built for your computer (Linux or macOS):
`arm-none-eabi` for the 32-bit Pi Zero, 2 and 3 kernels, `aarch64-none-elf`
for the 64-bit Pi 4 and 5 kernels. If it can't be downloaded,
`BMCOCO_ALLOW_ANY_TOOLCHAIN=1` lets the build use whatever
`arm-none-eabi-gcc` (or `aarch64-none-elf-gcc`) is on the PATH.

## Downloading the files

```
git clone --recursive <BMCoco repository URL>
```

or, in an existing clone, `git submodule update --init --recursive`.

## Building an SD card

```
./build_sdcard.sh
```

builds everything from clean and stages the boot partition in
`build/sdcard`, with a kernel for each board: `kernel8-32.img` for the Pi 3
and Pi Zero 2 W, `kernel7.img` for the Pi 2, `kernel.img` for the Pi Zero
and Zero W, `kernel8-rpi4.img` (with `armstub8-rpi4.bin`) for the Pi 4 B
and Pi 400, and `kernel_2712.img` for the Pi 5, Pi 500 and Pi 500+.
`./build_sdcard.sh pi3`, `./build_sdcard.sh pi2`, `./build_sdcard.sh pi0`,
`./build_sdcard.sh pi4` or `./build_sdcard.sh pi5` builds just one, and
`./build_sdcard.sh pi4 pi5` several. Copy the contents of that folder to the root of a FAT32
formatted SD card.

While developing, `./build_sdcard.sh --skip-patches` keeps the third party
libraries already built and only rebuilds the kernel, which takes seconds.
The libraries are built for one board at a time, so without a board it
rebuilds the kernel for the board built last; a board they aren't built for
is built from clean. With
[networking](USER_GUIDE.md#networking-and-the-web-ui) on, a new kernel can
then be uploaded to the Pi from the web UI and the Pi
rebooted from there, without touching the SD card.

## The individual steps

1. `./clean_all.sh` cleans everything, including the third party builds,
   and resets Circle to its pinned revision.
2. `./make_all.sh [pi0|pi2|pi3|pi4|pi5] [--skip-patches]` applies `src/patches/` to
   Circle, builds circle-stdlib (Circle and newlib) and the Circle addons
   for one board (pi3 if none is given), then the kernel: `kernel8-32.img`
   for pi3, `kernel7.img` for pi2, `kernel.img` for pi0 (Circle's Raspberry
   Pi 1), `kernel8-rpi4.img` (64-bit) for pi4,
   which also builds Circle's `armstub8-rpi4.bin`, `kernel_2712.img`
   (64-bit) for pi5. It only needs running again after
   `clean_all.sh` or a change to the patches or circle-stdlib. To change
   board, run `clean_all.sh` first (make_all.sh refuses otherwise).
3. `source get_gnu_toolchain.sh && make` rebuilds just the kernel (for the
   Pi 4 and 5: `BMCOCO_AARCH=64 source get_gnu_toolchain.sh && make`).
4. `./make_wifi_firmware.sh DIR` puts the Wi-Fi firmware in `DIR/firmware`
   (`build_sdcard.sh` does this).

The Pi 3 kernel also runs on the Pi Zero 2 W. `config.txt` has a section
per board that picks its kernel. The Pi Zero has one core, so its kernel
runs the emulator on core 0 and looks after USB between frames, where the
other boards use core 1 for the emulator. See [PI4.md](PI4.md) and [PI5.md](PI5.md)
for how the Pi 4 and 5 builds differ.

## Testing on a PC

`tools/hostsim` builds the emulator port (XRoar plus `src/port/`) for the
PC, with a simulated display, sound and input in place of the Raspberry Pi.
It runs scripted tests: booting, the display lock and timer pacing, a Pi
too slow for the frame lock (`BMCOCO_SIM_FRAME_MS` makes each frame take
that long), the keyboard, analog and digital joysticks, the mouse, the
menu, memory, hard disks, web UI requests, saved settings, and checking the
sound stays in step with the display.

```
tools/hostsim/run_tests.sh
BMCOCO_ROMS=/path/to/your/roms tools/hostsim/run_tests.sh
```

The CoCo's ROMs can't be distributed, so the tests run a small stand-in
ROM that `tools/hostsim/make_test_rom.py` assembles: it shows the keyboard
matrix, the joysticks and a frame counter, and makes a sound. With
`BMCOCO_ROMS` set, Color BASIC, the CoCo 3, the built-in DriveWire server
and the Becker port (with a stand-in DriveWire server, slow to connect
too) are tested too. Screenshots
(`.ppm`) are written to `tools/hostsim/out/`.

To play with the port on the PC, run it live:

```
tools/hostsim/live.sh /path/to/your/roms
```

and open http://127.0.0.1:8064/ in a browser: the screen, keyboard, sound
and gamepads (through the browser's Gamepad API) are in the page. The
simulator's SD card is `tools/hostsim/out/live`; disks, tapes and programs
copied there can be started from the menu. This runs BMCoco's port and
XRoar on the PC, not the Pi kernel (no emulator shows the Pi's VideoCore
display). The web page is served on 127.0.0.1 only.

The simulator can also be driven by a script of key presses and
screenshots; see the top of `tools/hostsim/host_sim.c` for the commands.
[SIMULATOR.md](SIMULATOR.md) has more: setting it up, debugging it with
gdb, lldb or strace, XRoar's traces, and getting a log from the Pi.

`tools/pi4scaler` tests the shared Pi 4/5 ARM smooth scaling and CRT shader
(`src/av/scaler.*`): every pixel is checked against crt-pi worked out in
floating point, and the pictures are written to `tools/pi4scaler/out/`.
`tools/pi4kms` tests the Pi 4/5 direct display drivers (`src/kms/`, see
[PI4.md](PI4.md) and [PI5.md](PI5.md)): shared filter kernels, EDID, mode
choice, HDMI packets and audio clocks, and each board's display lists and
PHY settings.
Both come from BMA800XL.

```
tools/pi4scaler/run_tests.sh
tools/pi4kms/run_tests.sh
```

## The web UI

The web UI's pages are in `src/webui/assets/`. They are compiled into the
kernel through `src/webui/webui_assets.c`, which the Makefile regenerates
with `tools/gen_webui_assets.py` whenever an asset changes.

## Layout

| Path                         | What                                        |
|------------------------------|---------------------------------------------|
| `src/main.cpp`, `kernel.*`   | Circle kernel; USB, GPIO, SD card, cores    |
| `src/network.*`              | Ethernet / Wi-Fi start up (from BMC64)      |
| `src/network_time_sync.*`    | Clock from the network (from BMC64)         |
| `src/webui/`                 | Web UI server, file manager and pages (from BMC64) |
| `src/sdcard/`                | SD card helpers used by the web UI (from BMC64) |
| `src/fbl.*`, `crt_pi_*`      | Display layer and CRT shaders (from BMC64)  |
| `src/vchiqsound*`            | Sound through the VideoCore (from BMC64)    |
| `src/options.*`              | `cmdline.txt` options                       |
| `src/host.h`                 | Interface between kernel and emulator       |
| `src/port/platform.c`        | XRoar's user interface, video and sound modules; timing; main loop |
| `src/port/joystick.c`        | Gamepads and GPIO joysticks as the CoCo's joysticks |
| `src/port/menu.c`, `osd.c`   | The menu and its on-screen display          |
| `src/port/posix_extra.c`     | regcomp(), basename(), access() for newlib  |
| `src/port/becker.c`          | XRoar's Becker port (DriveWire over TCP, connecting in the background), or the built-in server |
| `src/port/dwserver.c`        | The built-in DriveWire server               |
| `src/port/config.h`          | XRoar's build configuration                 |
| `src/port/xroar.mk`          | Which XRoar sources are built               |
| `src/patches/`               | Changes applied to Circle by `make_all.sh`  |
| `sdcard/`                    | Files for the SD card                       |
| `third_party/xroar`          | XRoar (submodule)                           |
| `third_party/circle-stdlib`  | Circle and newlib (submodule)               |
| `src/av/`                    | Shared Pi 4/5 video, ARM scaling / CRT shader and sound |
| `src/v3d/`                   | Shared Pi 4/5 GPU driver and CRT rendering  |
| `src/kms/common/`            | Shared Pi 4/5 filter kernels and mode calculations |
| `src/kms/pi4/`               | Pi 4 direct display driver and PHY          |
| `src/kms/pi5/`               | Pi 5 direct display driver and PHY          |
| `tools/hostsim`              | PC test harness                             |
| `tools/pi4scaler`            | PC test of shared Pi 4/5 ARM scaling and CRT rendering |
| `tools/pi4kms`               | PC test of the Pi 4 display driver's calculations |
