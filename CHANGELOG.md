## Unreleased

- The web UI shows the machine running ("Tandy CoCo 3 (NTSC)"), not always
  "Tandy Color Computer"; the file list has icons for CoCo files (disks,
  tapes, cartridges, programs) and offers "Plug in cartridge" for `.ccc`
  and `.rom` files
- Pi 4 and 5: a PAL CoCo or Dragon gets a 50 Hz display, and the frame
  lock, without `hdmi_mode=19` (on a Pi 5, which ignores `hdmi_mode`, it
  was shown at 60 Hz, unlocked). Rates just outside what HDMI allows (PAL's
  50.32 Hz, the CoCo 3's 59.70 Hz) now get the standard 50 or 60 Hz mode
  instead of keeping whatever mode the display had
- The web UI no longer carries BMC64's BASIC listing editor, which BMCoco
  never used
- Docs: the SD card's README lists every Pi BMCoco runs on and the newer
  menu pages; the User Guide says which machines a Pi 4 or 5 runs at their
  exact rate (NTSC CoCo 1 and 2)

## 0.5.0 – 2026-10-16

- A built-in DriveWire server for the Becker port, so NitrOS-9's DriveWire
  builds (such as NitrOS-9 EOU's `dw_becker` IDE disks) boot on the Pi with
  no other computer: DriveWire in the menu chooses its drives 0-3 (`dw0` to
  `dw3`) and the server (built in, or another computer's address, moved
  there from Network). No address, `127.0.0.1` or `localhost` mean the
  built-in one
- Fix plugging in the IDE interface (or choosing a hard disk image) taking
  minutes, or seeming to hang, and NitrOS-9 hanging at "Drivewire": the IDE
  interface has a Becker port, which connected to a DriveWire server at
  127.0.0.1, where nothing answers on the Pi, and Circle waits about 3
  minutes for that; connecting now happens in the background

## 0.4.0 – 2026-10-15

- From BMA800XL 1.2.0 to 1.4.0:
  - Raspberry Pi 5, Pi 500 and Pi 500+ (`kernel_2712.img`): BMA800XL's
    display driver, run at the CoCo's exact rate; Resolution in the Video
    menu (the Pi 5's firmware ignores `hdmi_mode`); HDMI sound; the power
    button shuts down (docs/PI5.md)
  - Raspberry Pi Zero and Zero W (`kernel.img`), on their one core
  - The Pi 4 and 5's 3D GPU draws the CRT effect and smooth scaling, at the
    full 1080 lines (`crt_gpu=0` for the ARM, as before)
  - CRT Curvature in the Video menu
  - Fix the Pi 2 and 3 starting at 1080p instead of 720p
  - Logging is off unless `enable_logging` is set (`file` to see the load
    reports); File logs to the serial port too, and the log file is
    complete after a crash, a hang or switching off

## 0.3.1 – 2026-10-15

- Missing CP-400 and MX-1600 ROMs are named by their files (cp400bas.rom,
  cp400extbas.rom, ...), not XRoar's list names: a CP-400 without
  cp400bas.rom showed only "cp400", then ran with no Color BASIC (a screen
  of garbage)

## 0.3.0 – 2026-10-15

- A Pi too slow for the frame lock (the most accurate artifact colours on a
  Pi 3, a CoCo 3 at double speed in a busy game) no longer drops to half
  speed: BMCoco paces by the clock until frames fit again, and stretches
  the sound rather than leaving gaps. The load is logged every 30 seconds
  and shown in About
- The two most accurate artifact renderers are marked as slow on a Pi 2/3
- Memory in the menu: a CoCo 3's 128K to 2M, a CoCo 1/2's 4K to 64K
- Hard disks in the menu: images for an IDE interface (HDB-DOS), plugged
  in when needed, kept for the next boot
- The Becker port (DriveWire over the network), with the server set in the
  Network page
- USB mouse, as the CoCo's mouse in a joystick port
- The Color Computer Archive's CP-400 Extended BASIC (cp400extbas.rom) is
  found as it is
- The joysticks no longer look up their settings on every read

## 0.2.0 – 2026-10-13

- From BMA800XL 1.1.0: Raspberry Pi 4 B and Pi 400 support
  - BMA800XL's own display driver, on either HDMI port, with the display
    run at an NTSC CoCo 1/2's exact refresh rate (59.92 Hz); the CoCo 3
    and PAL CoCos lock to 60 or 50 Hz, as on a Pi 3 (docs/PI4.md)
  - CRT shader and smooth scaling
  - Sound on either HDMI port or the headphone jack
  - Pi 400: Shut Down switches it off; Fn+F10 shuts down
- A keyboard's power key shuts down
- Fix the smooth setting being stuck on for the Pi 2 and 3 (from BMA800XL)
- The Pi 4 scaler and display driver tests run in CI

## 0.1.1 – 2026-10-12

- Web UI in Color Computer colours: the VDG's green text screen, its
  orange, and its eight colours as a colour bar (it had BMA800XL's Atari
  colours)

## 0.1.0 – 2026-10-12

- First version: BMCoco, forked from BMA800XL 1.0.1, with the atari800
  emulator replaced by XRoar 1.12.1
- Tandy Color Computer 1, 2 and 3 (and the Dragon and MC-10, from XRoar)
- Frames locked to the display refresh; timer pacing otherwise
- NTSC artifact colours, S-Video and RGB
- Disks (RS-DOS), tapes, cartridges and machine code programs
- USB keyboards (typed characters or the CoCo's layout), gamepads as analog
  joysticks, GPIO joysticks
- On-screen menu (F12) drawn in the CoCo 3's font
- Ethernet / Wi-Fi with BMC64's web UI
- Builds on Linux and macOS
