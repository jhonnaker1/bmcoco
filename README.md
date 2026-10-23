# BMCoco

BMCoco is a bare metal Tandy Color Computer emulator for the Raspberry Pi
Zero, 2, 3, 4 and 5. It runs the [XRoar](https://www.6809.org.uk/xroar/)
emulator directly on the hardware, with no Linux underneath, the same way
[BMC64](https://github.com/randyrossi/bmc64) does for the Commodore 64 and
[BMA800XL](https://github.com/aminch/bma800xl) for the Atari 800XL.

BMCoco is based on [BMA800XL](https://github.com/aminch/bma800xl) by Adam
Minchinton, which is based on [BMC64](https://github.com/randyrossi/bmc64) by
Randy Rossi. It is a fork of BMA800XL with the Atari emulator replaced by
XRoar; the kernel, display, sound, network and web UI come from those two
projects.

BMCoco wouldn't be possible without the work behind the open source projects
it builds on. Thanks to their authors and contributors for making it
available; see the [third-party](docs/THIRD_PARTY.md) acknowledgements.

- Boots straight into Color BASIC in a couple of seconds
- Emulates the CoCo 1, 2 and 3 (and, from the same code, the Dragon 32/64
  and the MC-10)
- Frames locked to the display's refresh for perfectly smooth scrolling
  (60 Hz for an NTSC CoCo), with the sound kept in step
- NTSC artifact colours, as CoCo games expect, or S-Video and RGB
- Disk images (DSK, VDK, JVC, OS9, DMK) with RS-DOS, tapes (CAS), cartridges
  (CCC, ROM) and machine code programs (BIN, HEX, S19)
- Hard disk images on an IDE interface, and a built-in DriveWire server for
  the Becker port (NitrOS-9's DriveWire builds run with no other computer,
  and reach the network through its virtual channels, `/N`)
- A disk light at the bottom of the screen, with the drive and sector being
  read (floppy, hard disk or DriveWire), and errors in red
- USB keyboards; USB gamepads as the CoCo's analog joysticks (an analog stick
  gives the full 0-63 range); a USB mouse as the CoCo's mouse, or through
  Tandy's Hi-Res Joystick Interface; real joysticks wired to the GPIO header
- No shutdown needed: just switch it off (changed disks are written back
  within a few seconds)
- A menu (F12) for everything: programs, disks, tapes, cartridges, machine,
  joysticks, video, keyboard, sound, network
- Optional Ethernet or Wi-Fi with BMC64's web UI: manage the SD card from a
  browser and start programs; and an RS-232 Pak with a virtual modem, to
  call telnet BBSes from CoCo terminal programs (`ATDTbbs.example.com:23`)

## ROMs

The CoCo's ROMs are copyrighted by Tandy and Microsoft and are not included.
Copy your own into the `roms` folder of the SD card: `bas13.rom` and
`extbas11.rom` for a CoCo 2, `disk11.rom` for disks, `coco3.rom` for a
CoCo 3. See [docs/ROMS.txt](docs/ROMS.txt).

All the CoCo ROMs can be downloaded from the Color Computer Archive:
https://colorcomputerarchive.com/repo/ROMs/XRoar/CoCo/. They are already named
the way XRoar, and so BMCoco, looks for them: copy the files from both its
`BASIC_OS` folder (Color BASIC, Extended BASIC, CoCo 3) and its `DOS` folder
(Disk BASIC) into `roms`.

## Supported hardware

Raspberry Pi 3 Model B, B+ and A+, and the Raspberry Pi Zero 2 W
(`kernel8-32.img`), and the Raspberry Pi 2 Model B (`kernel7.img`). The card
has a kernel for each and each Pi starts its own. Video and sound go out over
HDMI; sound can also go to the 3.5mm jack.

The Raspberry Pi 4 Model B and Pi 400 (`kernel8-rpi4.img`, 64-bit) are
supported too, with the display run at the CoCo's exact refresh rate on either
HDMI port, the CRT effect and smooth scaling drawn by its 3D GPU, and sound
over HDMI or the Pi 4 B's 3.5mm jack. See [docs/PI4.md](docs/PI4.md).

The Raspberry Pi 5, Pi 500 and Pi 500+ (`kernel_2712.img`, 64-bit) are
supported too. See [docs/PI5.md](docs/PI5.md).

The Raspberry Pi Zero and Zero W (`kernel.img`) work like the Pi 2 and 3,
with everything on their one, slower core: a CoCo 1 or 2 with the quicker
artifact colours suits them best; when the Pi can't keep up, BMCoco runs as
fast as it can (see the User Guide's *Display and timing*).

Pi 4 support comes from BMA800XL 1.1.0, and the Pi 4's 3D GPU, the Pi 5 and
the Pi Zero from BMA800XL 1.2.0 to 1.4.0.

## Getting started

1. Format an SD card as FAT32.
2. Copy the contents of a release zip (or of `build/sdcard/` after building)
   to the root of the card.
3. Copy your ROMs into `roms`, and your CoCo files to the card. The `disks`,
   `tapes`, `carts` and `programs` folders are there for them, but any folder
   works.
4. Boot the Pi with a USB keyboard connected. You get the `OK` prompt. Press
   **F12** for the menu, then *Run a Program* to start something.

The display is set to 720p at 60 Hz to suit an NTSC CoCo. For a PAL CoCo, see
the [User Guide](docs/USER_GUIDE.md#display-and-timing).

## Keys

| Key                  | Does                                                 |
|----------------------|------------------------------------------------------|
| F12                  | Menu                                                 |
| F11                  | Rate limit off/on (as fast as the Pi can)            |
| F10 / Shift+F10      | Reset / hard reset (power off and on)                |
| Esc                  | BREAK                                                |
| Home                 | CLEAR                                                |
| Arrow keys           | The CoCo's arrow keys                                |
| Alt, Caps Lock or Windows, F1, F2 | The CoCo 3's ALT, CTRL, F1 and F2       |

Keys type the characters printed on them (Shift+' types `"`); turn off
*Typed Characters* in the menu's *Keyboard* page for games that want the
CoCo's own layout.

Gamepads are Joystick 1 and 2: the left stick or D-pad moves, **A** is the
fire button, **B** the CoCo 3's second button (**X** and **Y** fire too;
`pad_keys=1` has them type Space and Enter). **Home**, or **Select+Start** together, opens the menu, where the
D-pad moves, **A** chooses and **B** goes back.

See the [User Guide](docs/USER_GUIDE.md) for all the details, including the
options in `cmdline.txt` and the GPIO joystick wiring. All the documents are
listed in [docs/README.md](docs/README.md).

## Building

```
./build_sdcard.sh
```

builds everything (the kernels for both boards; `./build_sdcard.sh pi3` or
`pi2` builds one) and stages the SD card files in `build/sdcard/`. It works
on Linux and macOS. See [docs/BUILDING.md](docs/BUILDING.md).

BMCoco can also run on a PC, in a simulator, for testing and debugging (with
gdb, lldb or strace): see [docs/SIMULATOR.md](docs/SIMULATOR.md).

## How it fits together

```
src/main.cpp, kernel.cpp   Circle kernel: USB, GPIO, SD card; runs XRoar on core 1
src/fbl.cpp                Display (dispmanx layer, optional CRT shader), from BMC64
src/vchiqsound*.cpp        Sound over HDMI or the headphone jack, from BMC64
src/network.cpp, webui/    Ethernet / Wi-Fi and the web UI, from BMC64
src/host.h                 What the kernel offers the emulator
src/port/platform.c        XRoar's user interface, video and sound modules; main loop
src/port/joystick.c        Gamepads and GPIO joysticks as the CoCo's joysticks
src/port/menu.c, osd.c     The menu, drawn in the CoCo 3's font
src/port/config.h, xroar.mk  XRoar's build options and source list
third_party/xroar          XRoar (submodule)
third_party/circle-stdlib  Circle and newlib (submodule)
src/av/                    Shared Pi 4/5 video, ARM scaling / CRT shader and sound
src/v3d/                   Shared Pi 4/5 GPU driver and CRT rendering
src/kms/common/            Shared Pi 4/5 filter kernels and mode calculations
src/kms/pi4/               Pi 4 direct display driver (docs/PI4.md)
src/kms/pi5/               Pi 5 direct display driver (docs/PI5.md)
tools/hostsim              Runs the emulator port on a PC for testing
tools/pi4scaler            Tests shared Pi 4/5 ARM scaling and CRT rendering on a PC
tools/pi4kms               Tests Pi 4/5 display driver calculations on a PC
```

XRoar is used unmodified: everything specific to BMCoco is in `src/`.

## Licence

BMCoco is free software under the GNU General Public License version 3; see
[LICENSE](LICENSE). It includes software from other projects under their own
licences; see [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).
