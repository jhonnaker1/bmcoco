# BMCoco User Guide

BMCoco turns a Raspberry Pi into a Tandy Color Computer. It runs the XRoar
emulator by Ciaran Anscomb on the bare hardware, so it starts in a couple of
seconds and can be switched off at any time.

## The SD card

| File or folder   | What it is                                                  |
|------------------|-------------------------------------------------------------|
| `kernel8-32.img`, `kernel7.img`, `kernel.img`, `kernel8-rpi4.img`, `kernel_2712.img` | BMCoco for the Pi 3 and Zero 2 W, the Pi 2, the Pi Zero and Zero W, the Pi 4 and Pi 400, and the Pi 5, 500 and 500+ |
| `bootcode.bin`, `start.elf`, `fixup.dat`, `start4.elf`, `fixup4.dat`, `armstub8-rpi4.bin`, `*.dtb`, `overlays/` | Raspberry Pi firmware |
| `config.txt`     | Raspberry Pi firmware settings: the display mode            |
| `cmdline.txt`    | BMCoco's options (see Options)                              |
| `bmcoco.cfg`     | Settings changed in the menu (written by BMCoco)            |
| `xroar.conf`     | Optional: XRoar's own configuration file, for anything else |
| `roms/`          | The CoCo's ROMs: you supply them (see ROMs)                 |
| `disks/`, `tapes/`, `carts/`, `programs/` | For your CoCo files (any folder works) |
| `firmware/`      | Wi-Fi firmware                                              |

Changed disk images are written back to the card two seconds after the CoCo
last wrote to them (the first time, the original is kept as `.bak`). Wait
that long after saving before switching off.

## ROMs

The CoCo's ROMs are copyrighted, so BMCoco doesn't include them. Copy your
own into `roms` (or the root of the card). XRoar finds them by name:

| Machine            | ROM files                                       |
|--------------------|-------------------------------------------------|
| CoCo 1/2           | `bas13.rom` and `extbas11.rom` (older: `bas10`-`bas12`, `extbas10`) |
| Disks (RS-DOS)     | `disk11.rom` (or `disk10.rom`)                  |
| CoCo 3             | `coco3.rom`                                     |
| MC-10              | `mc10.rom`                                      |
| Prológica CP-400   | `cp400bas.rom` and `cp400ext.rom` (or `cp400extbas.rom`, the Color Computer Archive's name) |
| Dragon 32 / 64     | `d32.rom` / `d64_1.rom`, `d64_2.rom`            |
| Hard disks (IDE)   | `hdblba.rom` (HDB-DOS) or `yados.rom`, for the IDE interface (see Hard disks) |
| Becker port DOS    | `hdbdw3bck.rom` (HDB-DOS for DriveWire), for *RS-DOS with becker port* |

BMCoco starts the first of these it has ROMs for: CoCo 2 (NTSC), CoCo
(NTSC), CoCo 3, ... If it has none, it says which files it is missing.

All the CoCo ROMs can be downloaded from the
[Color Computer Archive](https://colorcomputerarchive.com/repo/ROMs/XRoar/CoCo/),
already named this way. Copy the files from both its `BASIC_OS` folder
(Color BASIC, Extended BASIC, CoCo 3) and its `DOS` folder (Disk BASIC)
into `roms`.

## The menu

**F12** (or a gamepad's **Home** button, or **Select+Start**) opens the
menu; **Esc** goes back a page and F12 closes it. The arrow keys or D-pad
move, **Enter** or **A** chooses. In a list, typing a letter jumps to the
next item starting with it.

| Item           | What it does |
|----------------|--------------|
| Run a Program  | Choose a disk, tape, cartridge or program and start it: XRoar inserts it and types the commands to run it (`RUN"..."` from a disk, `CLOAD` from a tape, ...) |
| Disk Drives    | Insert or eject a disk in drives 0-3, and write protect it; Show Disk Activity (see below) |
| Hard Disks     | Hard disk images for drives 0 and 1 of an IDE interface (see Hard disks) |
| DriveWire      | The DriveWire server for the Becker port, built in or on another computer, and the built-in one's drives 0-3 (see The Becker port) |
| Cassette       | Insert a tape (then type `CLOAD` or `CLOADM`), rewind, eject, fast loading |
| Cartridge      | Plug in a cartridge file, one of XRoar's built in cartridges (Disk BASIC and others), or none |
| Machine        | CoCo 1, 2 or 3, PAL or NTSC, with or without a 6309, and the other machines XRoar emulates. Machines without ROMs are marked |
| Memory         | The machine's RAM: 128K, 512K, 1M or 2M for a CoCo 3, 4K to 64K for a CoCo 1 or 2 (or a Dragon). The machine is reset with it, and it is kept for that machine |
| Joysticks      | What is plugged into each joystick port, which gamepad is which joystick, GPIO joysticks, and a joystick test |
| Video          | TV input (composite with artifact colours, S-Video, or RGB for the CoCo 3), the artifact colour renderer, how much border to show, the screen size (smaller for a TV that cuts off the edges), the frame lock, the resolution (Pi 5), the CRT effect and its curvature, smooth scaling |
| Keyboard       | Typed characters on or off, and the PC keyboard's layout |
| Sound          | Volume, and HDMI or the headphone jack |
| Network        | Ethernet or Wi-Fi, and the web UI (see below) |
| Turbo          | Run as fast as the Pi can (also F11) |
| Reset, Hard Reset | The reset button (F10), or power off and on (Shift+F10) |
| About          | Versions, the machine, ROM status, display, load and network |
| Reboot, Shut Down | Restart BMCoco, or halt the Pi. Shut Down also from a keyboard's power key (Fn+F10 on a Pi 400, which switches off; Fn+F10 starts it again) or the Pi 5's power button |

Settings changed in the menu are kept in `bmcoco.cfg` and used at the next
boot.

## Keyboard

| PC key                 | CoCo                                          |
|------------------------|-----------------------------------------------|
| F12                    | BMCoco menu                                   |
| F11                    | Turbo on/off                                  |
| F10, Shift+F10         | Reset, hard reset                             |
| Esc                    | BREAK                                         |
| Home                   | CLEAR                                         |
| Arrow keys, Tab        | Arrow keys (Tab is right arrow)               |
| Backspace, Delete      | Left arrow (backspace)                        |
| Shift+Backspace        | Erase the line                                |
| Shift+Enter            | Caps lock (SHIFT+0)                           |
| Alt                    | CoCo 3 ALT                                    |
| Caps Lock, Windows     | CoCo 3 CTRL                                   |
| F1, F2                 | CoCo 3 F1, F2                                 |

By default keys type the characters printed on them, in the layout set with
`keyboard=` in `cmdline.txt` (or *Keyboard* in the menu): Shift+' types `"`
and Shift+8 `*`, as you would expect. Some games read the keyboard matrix
directly and want the keys where the CoCo had them: turn off *Typed
Characters* in the menu, and the PC keys then sit in the CoCo's places
(Shift+2 is `"`, the key right of 0 is `:`).

## Joysticks

The CoCo's joysticks are analog. BMCoco has two, Joystick 1 and Joystick 2,
plugged into the CoCo's right and left ports (most games use the right
one). Each takes its input from:

- a USB gamepad (by default gamepad 1 for Joystick 1, gamepad 2 for
  Joystick 2; change it in the menu). Its left analog stick gives the
  position as it is, the whole 0-63 range the CoCo reads; the D-pad pushes
  to the edges.
- a joystick wired to the GPIO header (see below), which also pushes to the
  edges.

| Gamepad button  | CoCo                       |
|-----------------|----------------------------|
| A               | Fire                       |
| B               | Second fire button (CoCo 3)|
| X               | Space                      |
| Y               | Enter                      |
| Home, Select+Start | BMCoco menu             |

In the menu's *Joysticks* page each port can also be given **Arrow keys**:
the arrow keys then move that joystick and left Alt fires (the arrows then
no longer reach the CoCo as keys). *Joystick Test* shows what the CoCo
reads.

**A USB mouse**: give a port **Mouse (USB)** in the *Joysticks* page (or
`joy_right=mjoy0` in `cmdline.txt`). The mouse then moves that joystick as
a pointer over the picture, as the CoCo's own mouse does (OS-9's Multi-Vue,
paint programs, CoCo 3 games), with the left button as fire and the right
as the second button. `mouse_speed` (percent) changes how far it moves.

**Joysticks on the GPIO header**: set `gpio_joysticks=1` in `cmdline.txt`
(or in the menu, then reboot). The wiring is BMC64's "GPIO Config 1". The
pins are active low (each switch connects the pin to ground) and BMCoco
turns on the Pi's pull-ups:

| Signal | Joystick 1 (GPIO) | Joystick 2 (GPIO) |
|--------|-------------------|-------------------|
| Up     | 17                | 5                 |
| Down   | 18                | 6                 |
| Left   | 27                | 12                |
| Right  | 22                | 13                |
| Fire   | 23                | 19                |

Ground is on pins 6, 9, 14, 20, 25, 30, 34 or 39 of the header. These are
digital (switch) joysticks, like an Atari-style stick with an adapter;
the CoCo's own analog joysticks need an analog to digital converter, which
the Pi doesn't have.

## Display and timing

BMCoco emulates an NTSC CoCo (60 Hz), and `config.txt` sets the display to
720p at 60 Hz to match. When the display's refresh rate is within 2% of the
machine's, each CoCo frame is shown on exactly one display refresh, which
gives perfectly smooth scrolling (the CoCo then runs 0.1% fast, which
nothing notices). Otherwise the emulation keeps exact time and frames are
shown as they come. The About page shows which is in use.

For a PAL CoCo or a Dragon (50 Hz), choose it in *Machine*. A Pi 4 or 5
switches the display between 50 and 60 Hz itself, and runs an NTSC CoCo 1
or 2 at its exact rate; the CoCo 3 (59.70 Hz) and PAL machines (50.32 Hz)
are just outside what HDMI allows, so they get the standard 60 or 50 Hz,
and run 0.5 % fast or 0.6 % slow (see [PI4.md](PI4.md)). On a Pi Zero, 2
or 3 (or a Pi 4 with `video_driver=hvs` or `firmware`), set
`hdmi_mode=19` (720p 50 Hz) in `config.txt` for a PAL machine.
`vsync=on` or `vsync=off` in `cmdline.txt` (or *Frame Lock* in the menu)
forces one or the other.

**Artifact colours**: many CoCo 1/2 games draw in PMODE 4 and rely on the
way an NTSC TV smears its black and white dots into red and blue. *TV
Input: Composite* shows them; on a real CoCo which of the two colours
appeared depended on luck at power on, so if a game's colours are swapped
choose the other phase (blue-red or red-blue). *S-Video* shows the dots in
black and white. The *Artifact Colours* renderer chooses between a quick
table (*Simple*, *5-bit*) and XRoar's simulation of an NTSC TV
(*Partial*, *Simulated*: the most accurate, but too much for a Pi Zero,
2 or 3, which then falls behind; use them on a Pi 4 or 5).

**When the Pi can't keep up**: if frames take longer to emulate than the
display shows them for (the most accurate artifact colours on a Pi 3, or a
CoCo 3 running at double speed in a busy game), waiting for the display
would halve the speed. BMCoco then stops waiting for it and runs as fast as
the Pi can, stretching the sound a little (lower in pitch) rather than
leaving gaps, until frames fit again; the log says so, and every 30
seconds it logs how busy the Pi is (`BMCoco: load ...`; logging is off
unless `enable_logging=file` or `uart` is set, see Logging). *About* shows
the load too. If it happens a lot, choose a quicker artifact renderer.

## Hard disks

XRoar's **IDE interface** cartridge takes two hard disk images, which
HDB-DOS or NitrOS-9 can use. Copy HDB-DOS (`hdblba.rom`) or YA-DOS
(`yados.rom`) into `roms`, then in the menu's *Hard Disks* page choose an
image for drive 0 or 1 (`.ide`, `.vhd`, `.img`, `.hdd`, `.os9`, `.dsk`): if
no cartridge with hard disks is plugged in, the IDE interface is, and the
machine is reset. The images and the cartridge are kept for the next boot
(`hd0`, `hd1` and `hd_cart` in `bmcoco.cfg`).

A cartridge of your own set up in `xroar.conf` works too; for NitrOS-9 EOU
on a CoCo 3, for example:

```
cart hdb-dos
  cart-desc "Load Nitros9 EOU w/HDB-DOS"
  cart-type ide
  cart-rom /disks/hdblba.rom
  load-hd0 /disks/63IDE.ide
  ram 2048
```

then choose it in *Cartridge*; *Hard Disks* then changes its images.

## The Becker port (DriveWire)

The Becker port connects the CoCo to a DriveWire server, which serves it
disk images and more. Choose *RS-DOS with becker port* in *Cartridge* (it
needs HDB-DOS for DriveWire, `hdbdw3bck.rom`, in `roms`), or use the IDE
interface, which has one too. NitrOS-9's DriveWire builds (such as the
`dw_becker` disks of NitrOS-9 EOU for the IDE interface) use it at boot.

BMCoco has a DriveWire server built in, so no other computer is needed:
choose disk images for its drives 0 to 3 in the menu's *DriveWire*
(NitrOS-9's `/X0` to `/X3`; kept as `dw0` to `dw3` in `bmcoco.cfg`). It
also gives NitrOS-9 the time. It has no virtual serial channels (`/N`,
`/Z`), printing or named objects.

To use a server on another computer instead (DriveWire 4 or pyDriveWire on
a PC, for example), set *Server* to *Another Computer* and type its
address, with `:port` if it isn't 65504 (`becker_ip` and `becker_port` in
`cmdline.txt`), and turn on networking. BMCoco connects once the Pi has an
address, in the background (the CoCo keeps running), and tries again every
10 seconds if the server isn't there; the log says when it connects. An
address of `127.0.0.1` or `localhost`, as in an `xroar.conf` from a PC,
means the built-in server: nothing else can listen there on the Pi.

## Networking and the web UI

BMCoco can join your network over Ethernet or Wi-Fi and serve a web page
for managing the SD card from any browser on the same network, so files can
be copied to and from the Pi without taking the card out. It is the web UI
from BMC64.

Turn it on in the menu's *Network*. For Ethernet, set *Network* to Ethernet
and reboot. For Wi-Fi, choose *Scan for Wi-Fi Networks*: the first time,
it offers to switch Wi-Fi on and reboot; after that it lists the networks
it can hear, strongest first. Pick yours: it fills in *Wi-Fi Network*, and
if the network needs a password, *Wi-Fi Password* is next. Then reboot.
Set *Wi-Fi Country* to your two letter country code too; it decides which
channels are allowed. A hidden network can be typed in under *Wi-Fi
Network* instead. Or, in `cmdline.txt` (on the same single line as the other options):

- Ethernet: `network=ethernet`
- Wi-Fi: `network=wifi`, and copy `wpa_supplicant.conf.example` to
  `wpa_supplicant.conf` in the root of the card with your network's name,
  password and country.

Wi-Fi needs the files in the `firmware` folder, which `build_sdcard.sh` puts
there.

Once the Pi has an address, it is shown at the bottom of the screen for a
few seconds (`WEB UI: HTTP://192.168.1.50/`) and written to the log.
Open that address in a browser.

The web UI has:

- **Dashboard**: the Pi's model, address, uptime, temperature, power and
  throttling warnings and free space on the card. Buttons to **Hard Reset**
  the CoCo (power it off and on), **Reboot** BMCoco, **Shut Down** (the
  Pi halts; switch it off, then on again to restart; a Pi 400 switches
  itself off, and its power key starts it again), and **Disable Web UI**
  until the next reboot.
- **Files**: browse the SD card; upload (or drag and drop) files into a
  folder, download, rename, delete, and create folders. Clicking a disk
  image, program, cartridge or tape **starts it** on the CoCo, the same as
  *Run a Program*. `cmdline.txt`, `config.txt`, `bmcoco.cfg`,
  `xroar.conf` and `wpa_supplicant.conf` can be **edited** in the browser; changes take
  effect after a reboot, and the previous version is kept as `.bak`.

Uploads can't overwrite `cmdline.txt`, `config.txt` or `wpa_supplicant.conf`
(edit them instead), or anything in `firmware`. Everything else, including
the kernels, can be replaced, so a new build of BMCoco can be
uploaded and started with **Reboot**. A mistake in `cmdline.txt` or
`config.txt` can stop the Pi starting, and then needs the card in a computer
(the `.bak` copy makes that easy).

The web UI is open to anyone on your network. To protect it with a PIN, set
`webui_pin=1234` in `cmdline.txt`; the browser then asks for a user name
(anything) and the PIN. To network without the web UI, set `webui=0`.

The network also sets the Pi's clock, so files saved on the card get the
right date and time. Set `timezone` to your offset from UTC in minutes, for
example `timezone=600` for UTC+10.

## Options

`cmdline.txt` is a single line of `name=value` options separated by spaces.
The menu saves the options it changes to `bmcoco.cfg`, one `name=value` per
line, and those win over `cmdline.txt`. Either file can be edited from the
web UI.

| Option              | Values                       | Default  | What it does |
|---------------------|------------------------------|----------|--------------|
| `machine`           | `coco2bus`, `cocous`, `coco3`, `coco3h`, `coco2b`, `coco`, `coco3p`, `mc10`, `dragon64`, ... | first with ROMs | Machine to emulate |
| `autostart`         | path, e.g. `/disks/game.dsk` | none     | Disk, tape, cartridge or program to start at boot |
| `tv_input`          | `cmp-br`, `cmp-rb`, `cmp`, `rgb` | machine's | Composite (two phases), S-Video, RGB (CoCo 3) |
| `artifacts`         | `none`, `simple`, `5bit`, `partial`, `simulated` | XRoar's | Artifact colour renderer |
| `picture`           | `zoomed`, `title`, `action`, `underscan` | `title` | How much border to show |
| `aspect`            | number                       | `1.0`    | Multiplies the width of the picture |
| `disk_activity`     | `0`, `1`                     | `1`      | While a floppy disk is read or written, show its drive, track, side and sector at the bottom left (Disk Drives, Show Disk Activity) |
| `scale`             | 0.1 to 1.0                   | `1.0`    | Height of the picture as a fraction of the screen (Video, Screen Size; less for a TV that cuts off the edges) |
| `smooth`            | `0`, `1`                     | `0`      | Bilinear filtering instead of sharp pixels |
| `crt_shader`        | `0`, `1`                     | `0`      | CRT look (scanlines) from BMC64 |
| `crt_curvature`     | `0`, `1`                     | `0`      | With `crt_shader`, a curved screen |
| `crt_gpu`           | `0`, `1`                     | `1`      | Pi 4 and 5: the 3D GPU draws the smooth and CRT pictures; `0` has the ARM draw them (see [PI4.md](PI4.md)) |
| `vsync`             | `auto`, `on`, `off`          | `auto`   | Lock frames to the display (see Display and timing) |
| `resolution`        | `auto`, `1920x1080`, `1280x720` | `auto` | Pi 5 only, with `video_driver=kms`: the display's resolution; `auto` keeps the one the firmware chose (the Pi 5's firmware ignores `hdmi_mode`) |
| `video_driver`      | `kms`, `hvs`, `firmware`     | `kms`    | Pi 4 and 5: `kms` runs the display at the CoCo's exact frame rate; `hvs` leaves the mode set in `config.txt`; `firmware` uses the firmware's framebuffer, if the others give trouble (see [PI4.md](PI4.md) and [PI5.md](PI5.md)) |
| `audio_out`         | `auto`, `hdmi`, `headphones` | `auto`   | Where the sound goes (`auto`: HDMI, or the headphone jack if the display takes no sound) |
| `volume`            | 0 to 100                     | `70`     | Sound volume |
| `audio_buffer_ms`   | 20 to 500                    | `60`     | Sound buffer; raise it if the sound breaks up |
| `keyboard`          | `us`, `gb`, `de`, `fr`, ... (XRoar's `-kbd-lang`) | `us` | PC keyboard layout |
| `typed_keys`        | `0`, `1`                     | `1`      | Keys type the characters printed on them (see Keyboard) |
| `joy_right`, `joy_left` | `joy0`, `joy1`, `kjoy0` (arrow keys), `mjoy0` (mouse) | `joy0`, `joy1` | What is plugged into each joystick port |
| `joy1_pad`, `joy2_pad` | `0` automatic, `1`-`4` a gamepad, `-1` none | `0` | Gamepad for Joystick 1 and 2 |
| `mouse_speed`       | percent                      | `100`    | How far the mouse moves the pointer (see Joysticks) |
| `ram_NAME`          | K, e.g. `ram_coco3=2048`     | machine's | The RAM of machine NAME (the menu's *Memory*) |
| `hd0`, `hd1`, `hd_cart` | paths, a cartridge name  | none     | Hard disk images and their cartridge (the menu's *Hard Disks*) |
| `becker_ip`, `becker_port` | address, port         | none (built in), `65504` | The Becker port's DriveWire server on another computer |
| `dw0` ... `dw3`     | paths                        | none     | The built-in DriveWire server's disk images (the menu's *DriveWire*) |
| `gpio_joysticks`    | `0`, `1`                     | `0`      | Joysticks on the GPIO header |
| `enable_logging`    | `off`, `uart`, `file`        | `off`    | Where the log goes (see Logging) |
| `network`           | `off`, `ethernet`, `wifi`    | `off`    | Networking (see Networking and the web UI) |
| `webui`             | `0`, `1`                     | `1`      | Web UI when networking is on |
| `webui_pin`         | digits                       | none     | PIN for the web UI |
| `timezone`          | minutes east of UTC          | `0`      | Time zone for the network clock |
| `usbpowerdelay`     | milliseconds                 | `1000`   | Circle: wait for USB devices to power up |

Anything else XRoar can do can go in `xroar.conf` in the root of the card,
one option per line without the leading `-` (see XRoar's manual), for
example `ram 64` or `machine-cpu 6309`.

## Logging

BMCoco logs what it is doing, which helps when something doesn't work:
the version, the display and sound set up, gamepads coming and going,
network status, and XRoar's own messages (which ROMs it found, disks
inserted). Choose where it goes with `enable_logging` in `cmdline.txt`:

- **Off** (the default, also when `enable_logging` isn't set): no log.
- **UART**: the serial port, GPIO14 TX and GPIO15 RX at 115200 baud
  (all boards, the Pi 5 too). Needs a USB serial adapter.
- **File**: the serial port, and `bmcoco.log` in the root of the SD card.
  The file starts with what was logged since boot, is written to the card
  every few seconds (and is complete after a crash, a hang or switching
  off), and starts again at boot once it is over 1 MB. Download it from
  the web UI's file manager to send with a bug report. Writing it costs a
  little time on each frame, so switch it off once you have what you need.

## Troubleshooting

**"ROMs not found"**: see ROMs. The names must match (`bas13.rom`, not
`BASIC13.ROM`, although case doesn't matter on the card).

**No picture**: uncomment `hdmi_safe=1` in `config.txt`. If that works, your
display doesn't accept 720p at 60 Hz; try another `hdmi_mode`.

**No sound over HDMI**: try `hdmi_drive=2` in `config.txt`, or
`audio_out=hdmi` in `cmdline.txt`.

**Sound breaks up, or games run slowly**: the Pi may be falling behind
(see Display and timing): *About* shows the load. Choose a quicker
artifact renderer (*Simple* or *5-bit*). Otherwise raise
`audio_buffer_ms` in `cmdline.txt`, e.g. to 100.

**Keyboard or gamepad not working**: unplug it and plug it in again; devices
can come and go while the emulator runs. Some keyboards need a longer
`usbpowerdelay`.

**The TV cuts off the top and bottom of the picture or the menu**: many TVs
enlarge the picture and crop its edges (overscan). Make *Video*, *Screen
Size* smaller (90% usually does), or turn overscan off on the TV ("Just
Scan", "Screen Fit" or "1:1").

**A program seems stuck loading**: watch the bottom left of the screen.
While a floppy disk is read, a red light shows there with the drive (D),
track (T), side (H) and sector (S), as in `D0 T17 H0 S03`. A light that
stays on with the track and sector not moving, or a red message (`no disk`:
the drive is empty; `not found` or `CRC error`: a sector the program wants
isn't on the disk, or is damaged; `seek error`), usually means the wrong
disk is in the drive or the disk image is bad. Many DOSes retry for ever
rather than give up.

**A game's colours are wrong**: choose the other composite phase in *Video*,
*TV Input*.

**A game doesn't respond to keys**: turn off *Typed Characters* in
*Keyboard*.

**A disk won't load**: Disk BASIC needs `disk11.rom` in `roms`. `DIR` lists
the disk; `RUN"NAME"` starts a BASIC program and `LOADM"NAME":EXEC` a
machine code one.
