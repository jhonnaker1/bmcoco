## 0.5.8 – 2026-10-26

- Fixed: changing the machine (a CoCo 3 to a CoCo 2, say) left NitrOS-9's
  network channels on the built-in DriveWire server connected when the new
  machine had no Becker port, and a modem call on the RS-232 Pak
  connected with nothing to use it. Both are now ended
- The RS-232 Pak's modem hangs up at a reset (F10, Shift+F10), as a modem
  set to AT&D2 does when the reset drops the Pak's DTR
- In the menu, Space ticks or clears a `[ ]` item (*Rate Limit*, *Show
  Disk Activity*, ...), as Enter does
- Tests: Space in the menu; F10 and a change of machine end a modem call; a change of
  machine ends a NitrOS-9 channel's call

## 0.5.7 – 2026-10-26

- Fixed: Reboot and Shut Down left network connections open on the other
  computer: a DriveWire server (pyDriveWire keeps a connection for hours),
  the BBS the modem had called, NitrOS-9's channels. They are now closed,
  and the far end told, before the Pi restarts or halts
- The menu's *Turbo* is *Rate Limit (F11)*, as XRoar calls it (its
  Shift+F12): checked, the CoCo runs at its own speed; F11 turns it off
  and on ("Rate limit off")
- Web UI uploads about 5 times faster: the file is written to the SD card
  64 KB at a time, and Circle takes more of it between turns of its
  network task (a wider TCP receive window). Each upload's speed is
  logged (from BMA800XL, after BMC64)
- The source files that come from BMA800XL now carry its copyright
  notice (aminch) and licence, then "Modified for BMCoco", as BMA800XL's
  own files now do; THIRD_PARTY.md says the Pi 4 and 5's HDMI sound is
  adapted from Circle's, and that some BMC64 files are under its GPL
- Tests: a reboot closes the Becker port's connection and a modem call

## 0.5.6 – 2026-10-23

- Fixed: a gamepad's X button typed Space, and Y Enter. A game that asks
  "keyboard or joystick?" with the space bar for the keyboard (Canyon
  Climber) took the press for the keyboard when the pad's fire button was
  X (pads report their buttons in many orders), and the CoCo's own
  joystick button types nothing. X now fires as A does, and Y is the
  second button as B is; `pad_keys=1` in `cmdline.txt` brings back Space
  and Enter
- Fixed: on a CoCo 3, a terminal program on the bit-banger (the serial
  port) received bytes all the time with nothing connected. XRoar held
  the port's input at 0, an endless start bit; it now idles at 1, as on a
  quiet line (XRoar's CoCo 1 and 2 already did)
- Fixed: the keypad's digits typed nothing (with *Typed Characters*, the
  default), so Glen Hewlett's Asteroids and Joust took no coin on the
  keypad's 5. The keypad's digits, `.`, `-`, `/` and Enter are now the
  main keys'
- Pi 5: the fan is run by the SoC's temperature (off below 50 °C, faster
  in four steps up to 75 °C), as on Raspberry Pi OS; before it stayed off
  (from BMA800XL 1.4.3, after BMC64)
- From BMA800XL 1.4.3: the Pi 5's power button code is in src/pi45, and
  THIRD_PARTY.md says which firmware release the SD card's is. Its USB
  paddles (Atari) are left out, and BMCoco already ran the ARM at its full
  clock and drew RGB565 pictures on the GPU
- The User Guide says what F11 (no rate limit, XRoar's Shift+F12) and F10
  do when a game catches the reset (Shift+F10 clears the memory first)
- Tests: the keypad's 5; the bit-banger's input, and the gamepad's X with
  and without `pad_keys`

## 0.5.5 – 2026-10-20

- Hard disk and DriveWire activity, shown at the bottom left as the floppy
  disk's is (*Show Disk Activity*): `HD0 LSN 1234` for the IDE interface
  and the NX32's and MOOH's SD card, `DW0 LSN 1234` for DriveWire (the
  built-in server or another computer's), with the light on while a
  sector is read or written, and errors in red (`error`; DriveWire's `no
  disk`, `write protected`, ...). Whichever was busy last is shown
- Hi-Res Mouse (USB) in the Joysticks page (`hjoy0`): the USB mouse
  through Tandy's Hi-Res Joystick Interface, 640 positions across for
  software that reads it. Timed for the HIRESMS reader (X 0-616, Y 0-182);
  `hires_mouse_x` and `hires_mouse_y` in `cmdline.txt` change the timing
  for other programs (OS-9's driver: `1133,5423` on both)
- Fixed: the mouse's right button is the CoCo 3's second button, as the
  User Guide says; the middle button (the wheel) was. XRoar numbers the
  buttons left, middle, right; BMCoco gave it the USB order (left, right,
  middle). Both the Mouse and the Hi-Res Mouse
- Fixed: the *Arrow keys* joystick (Joysticks page, `joy_right=kjoy0`) did
  nothing; the port it was given was left with no joystick at all. The
  arrow keys now move it and left Alt fires
- Fixed: hard disks chosen in the menu were lost at the next boot when no
  machine was set (`machine=`): BMCoco then starts the first CoCo it has
  ROMs for, which came with RS-DOS instead of the IDE interface. The
  interface (and its images) now goes back in
- Fixed: the built-in DriveWire server's virtual channels started with
  connection 0 as theirs rather than none, so a reset of the CoCo, or
  NitrOS-9 opening a channel, closed connection 0, whatever had it
  (another channel's call, or the RS-232 Pak's)

## 0.5.4 – 2026-10-19

- NitrOS-9 on the network with the built-in DriveWire server: its virtual
  channels (`/N`) reach the internet, as with DriveWire 4 or pyDriveWire.
  `tcp connect host port` (NitrOS-9's network programs) connects in the
  background and answers `OK connected` or `FAIL`; `ATDT` and the AT
  commands make a channel a Hayes modem, for terminal programs; `dw disk
  show` and `dw server show`. See "NitrOS-9 on the network" in the User
  Guide
- The RS-232 Pak, with a virtual modem: Cartridge, *RS-232 Pak (Modem)*
  (`rs232=1`) puts a 6551 at `$FF68` beside the cartridge plugged in, for
  CoCo terminal programs and NitrOS-9's `/T2`, and `ATDTbbs.example.com:23`
  calls a telnet BBS, in the background (`CONNECT`, or `NO CARRIER`), with
  `+++`, `ATH`, `ATO`, `ATZ` and the rest. XRoar has no RS-232 Pak; it is
  BMCoco's own (`src/port/rs232.c`). See "The RS-232 Pak and calling
  BBSes" in the User Guide
- Tests: the stand-in HDB-DOS ROM is no longer written through a link into
  BMCOCO_ROMS; dialling a stand-in BBS through the RS-232 Pak, +++ and ATH,
  and a call nobody answers
- Merged BMA800XL c33adfd: `build_sdcard.sh` runs the PC tests first
  (`--skip-tests` leaves them out)

## 0.5.3 – 2026-10-19

- Pi Zero, 2, 3 and 4: run the ARM at its full clock. Without an operating
  system asking for more the firmware left it at its lowest (600 MHz on a
  Pi 3 B+, not 1.4 GHz), too slow for busy CoCo 3 games such as Rick
  Dangerous, whose sound then broke up. The log shows the clock
- Pi 4 and 5: fix the sound breaking up. Their HDMI plays at 48 kHz, but
  XRoar made its sound at 44.1 kHz, so the buffer ran dry all the time and
  the sound was stretched in jumps. XRoar now makes it at the output's rate
- Video, Screen Size (100% down to 80%): makes the picture and the menu
  smaller, for a TV that cuts off the edges (overscan). The same as the
  `scale` option, now changed without editing `cmdline.txt`
- Disk activity: while a floppy disk is read or written, a red light and
  the drive, track, side and sector (`D0 T17 H0 S03`) at the bottom left
  of the picture, and in red for a few seconds an empty drive, a sector not
  found, a CRC error or a seek error, so that a program stuck loading (the
  wrong disk in the drive, a bad image) can be seen to be. Disk Drives,
  Show Disk Activity (`disk_activity=0`) turns it off
- Fix the Pi hanging when the Becker port is used with a DriveWire server
  on another computer (the built-in server was fine). Circle's C library
  holds the lock on its table of open files for as long as a socket call
  waits, so while the Becker port was connecting in the background, the
  emulator opening a file (the cartridge's ROM, at the reset that follows)
  waited for it for ever, and the network with it (no ping). The Becker
  port now uses Circle's own sockets
- Becker port with a DriveWire server on another computer: replies come
  back within the frame. Circle runs the network once a frame, so each
  request and reply took one or two frames (two of them per sector for
  HDB-DOS); while the CoCo waits for a reply the network now runs every
  half millisecond. A port that isn't a number from 1 to 65535 is reported
  in the log rather than tried. Both from a tester's patch
- From BMA800XL 1.4.1 and 1.4.2:
  - The menus can be used with GPIO joysticks: either one moves, fire
    chooses
  - `cmdline.txt` leaves `usbpowerdelay` to Circle (300 ms, 510 ms behind a
    hub) rather than 1000 ms, for a quicker start; add `usbpowerdelay=1000`
    for a keyboard that needs longer
  - `./build_sdcard.sh pi4 pi5` builds several boards at once
  - The SD card's `licenses` folder: the notices and licences of the code
    from other projects in the kernels (XRoar, Circle and newlib, BMC64's
    Apache 2.0 code, crt-pi, Mesa and Linux's V3D code, the GCC runtime);
    docs/THIRD_PARTY.md says where each is used
  - The Pi 4 and 5's display and GPU code is in `src/av/`, `src/v3d/` and
    `src/kms/`, as in BMA800XL
  - Troubleshooting: an 8BitDo M30 2.4G whose receiver is in Switch mode
- Simulator: BMCOCO_SIM_RATE=48000 plays the sound at the Pi 4 and 5's rate
- docs/SIMULATOR.md: testing and debugging BMCoco on a PC (with gdb, lldb
  or strace), XRoar's traces, and getting a log from the Pi

## 0.5.2 – 2026-10-16

- Fix the sound clicking or sounding wrong (PLAY on a CoCo 3, for one): a
  CoCo 3 (59.69 Hz) locked to a 60 Hz display, or a PAL CoCo (50.32 Hz) to
  50 Hz, runs up to 0.6% fast, more than BMCoco's corrections (one sample in
  512) could take up, so the sound buffer filled and what didn't fit was cut
  off. The sound is now squeezed or stretched smoothly, by up to 2%
- Simulator: BMCOCO_SIM_WAV saves the sound, BMCOCO_SIM_CPU shows how hard
  each screen is to emulate

## 0.5.1 – 2026-10-16

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
