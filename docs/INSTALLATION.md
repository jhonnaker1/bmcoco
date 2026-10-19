# Installation

## What you need

- A Raspberry Pi 3 Model B, B+ or A+, a Raspberry Pi Zero 2 W, a
  Raspberry Pi 2 Model B, a Raspberry Pi 4 Model B or Pi 400, a Raspberry
  Pi 5, Pi 500 or Pi 500+, or a Raspberry Pi Zero or Zero W (slower: see
  the User Guide)
- A microSD card (any size; BMCoco itself needs only a few MB)
- An HDMI display, ideally one that accepts 720p at 60 Hz
- Your own Color Computer ROM images (see below)
- A USB keyboard and/or USB gamepad

## Prepare the SD card

1. Download `bmcoco-v<version>.zip` from the releases page, or build it
   yourself (see [BUILDING.md](BUILDING.md)) and use `build/sdcard/`.
2. Format the SD card as FAT32.
3. Extract the zip to the root of the card, so that the kernels
   (`kernel_2712.img`, `kernel8-rpi4.img`, `kernel8-32.img`, `kernel7.img`,
   `kernel.img`) and `config.txt` are at the top level, not in a folder (a
   Pi 4 or 5 with them in a folder shows its firmware's "not found" screen).
4. Copy your ROMs into the `roms` folder (see below).
5. Copy your CoCo files to the card. The `disks`, `tapes`, `carts` and
   `programs` folders are there for them, but any folder works.
6. Put the card in the Pi, connect the display and keyboard, and switch on.

There is no image to write and nothing to install on the card beyond
copying the files.

## ROM files

The Color Computer's ROMs are copyrighted by Tandy and Microsoft, so
BMCoco can't include them. Copy images of your own into the `roms` folder.
XRoar, the emulator inside BMCoco, finds them by file name:

| File | ROM | Needed for |
| --- | --- | --- |
| `bas13.rom` | Color BASIC 1.3 | CoCo 1/2 |
| `extbas11.rom` | Extended Color BASIC 1.1 | CoCo 1/2 |
| `disk11.rom` | Disk Extended Color BASIC 1.1 | Disks on a CoCo 1/2 |
| `coco3.rom` | Super Extended Color BASIC | CoCo 3 |

Older versions (`bas10.rom`-`bas12.rom`, `extbas10.rom`, `disk10.rom`)
work too. Without ROMs BMCoco shows which files it is missing.

All the CoCo ROMs can be downloaded from the
[Color Computer Archive](https://colorcomputerarchive.com/repo/ROMs/XRoar/CoCo/),
already named this way. Copy the files from both its `BASIC_OS` folder
(Color BASIC, Extended BASIC, CoCo 3) and its `DOS` folder (Disk BASIC)
into `roms`.

## First boot

The Pi starts straight into Color BASIC's `OK` prompt (a CoCo 2, or a
CoCo 3 if that is the only ROM you have). Press **F12** (or **Home**, or
**Select+Start** on a gamepad) for the menu, and *Run a Program* to start a
disk, tape, cartridge or program.

USB keyboards and gamepads can be plugged in and out while it runs.

The display is set to 720p at 60 Hz for an NTSC CoCo. If you get no
picture, or you want a PAL CoCo, see
[Display and timing](USER_GUIDE.md#display-and-timing) and
[Troubleshooting](USER_GUIDE.md#troubleshooting) in the User Guide.

## Networking (optional)

To manage the card's files from a browser, turn on Ethernet or Wi-Fi under
*Network* in the menu, then reboot; the web UI's address is shown at the
bottom of the screen. For Wi-Fi, *Scan for Wi-Fi Networks* lists the
networks in range and asks for the password. See
[Networking and the web UI](USER_GUIDE.md#networking-and-the-web-ui).

## Upgrading

Copy the new release's files over the old ones, except for files you have
changed: `config.txt` and `cmdline.txt` if you edited them. Your settings
(`bmcoco.cfg`, `xroar.conf`, `wpa_supplicant.conf`), ROMs and CoCo
files are not in the release zip and stay as they are.

With networking on, a new kernel (`kernel8-32.img` for a Pi 3 or Zero 2 W,
`kernel7.img` for a Pi 2, `kernel.img` for a Pi Zero or Zero W) can also be uploaded in the web UI's file manager
and started with **Reboot**. Updates that change other files (the Pi
firmware in the root or in `firmware/`) need the card in a computer.
