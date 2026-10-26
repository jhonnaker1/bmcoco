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
| Cartridge      | Plug in a cartridge file, one of XRoar's built in cartridges (Disk BASIC and others), or none; and the RS-232 Pak with its virtual modem (see below) |
| Machine        | CoCo 1, 2 or 3, PAL or NTSC, with or without a 6309, and the other machines XRoar emulates. Machines without ROMs are marked |
| Memory         | The machine's RAM: 128K, 512K, 1M or 2M for a CoCo 3, 4K to 64K for a CoCo 1 or 2 (or a Dragon). The machine is reset with it, and it is kept for that machine |
| Joysticks      | What is plugged into each joystick port, which gamepad is which joystick, GPIO joysticks, and a joystick test |
| Video          | TV input (composite with artifact colours, S-Video, or RGB for the CoCo 3), the artifact colour renderer, how much border to show, the screen size (smaller for a TV that cuts off the edges), the frame lock, the resolution (Pi 5), the CRT effect and its curvature, smooth scaling |
| Keyboard       | Typed characters on or off, and the PC keyboard's layout |
| Sound          | Volume, and HDMI or the headphone jack |
| Network        | Ethernet or Wi-Fi, and the web UI (see below) |
| Rate Limit     | The CoCo's own speed; off, as fast as the Pi can (also F11; XRoar's Shift+F12) |
| Reset, Hard Reset | The reset button (F10), or power off and on (Shift+F10) |
| About          | Versions, the machine, ROM status, display, load and network |
| Reboot, Shut Down | Restart BMCoco, or halt the Pi. Shut Down also from a keyboard's power key (Fn+F10 on a Pi 400, which switches off; Fn+F10 starts it again) or the Pi 5's power button |

Settings changed in the menu are kept in `bmcoco.cfg` and used at the next
boot.

## Keyboard

| PC key                 | CoCo                                          |
|------------------------|-----------------------------------------------|
| F12                    | BMCoco menu                                   |
| F11                    | Rate limit off/on (XRoar's Shift+F12)         |
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
| Keypad digits, `.` `-` `/`, Enter | The same keys (the CoCo has no keypad) |

By default keys type the characters printed on them, in the layout set with
`keyboard=` in `cmdline.txt` (or *Keyboard* in the menu): Shift+' types `"`
and Shift+8 `*`, as you would expect. Some games read the keyboard matrix
directly and want the keys where the CoCo had them: turn off *Typed
Characters* in the menu, and the PC keys then sit in the CoCo's places
(Shift+2 is `"`, the key right of 0 is `:`).

F10 is the CoCo's reset button: the program in memory may catch it, as
some games do (Shock Trooper freezes, others restart). Shift+F10 clears the
memory first, as switching off and on does.

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
| X               | Fire (as A)                |
| Y               | Second fire button (as B)  |
| Home, Select+Start | BMCoco menu             |

The buttons type no keys, as a CoCo joystick's don't (a game asking
"keyboard or joystick?" would otherwise see a key). `pad_keys=1` in
`cmdline.txt` has X type Space and Y Enter instead, as before 0.5.6.

In the menu's *Joysticks* page each port can also be given **Arrow keys**:
the arrow keys then move that joystick and left Alt fires (the arrows then
no longer reach the CoCo as keys). *Joystick Test* shows what the CoCo
reads.

**A USB mouse**: give a port **Mouse (USB)** in the *Joysticks* page (or
`joy_right=mjoy0` in `cmdline.txt`). The mouse then moves that joystick as
a pointer over the picture, as the CoCo's own mouse does (OS-9's Multi-Vue,
paint programs, CoCo 3 games), with the left button as fire and the right
as the second button. `mouse_speed` (percent) changes how far it moves.

**A Hi-Res Mouse**: give a port **Hi-Res Mouse (USB)** (or
`joy_right=hjoy0`) for the USB mouse through Tandy's Hi-Res Joystick
Interface (26-3028), which gives 640 positions across instead of the
joystick's 64. Software has to read the interface itself; others see a
joystick stuck at one side, as with the real interface. The interface is
timed: the program drops the DAC ($FF20) from $FF to a low value and counts
until the comparator ($FF00 bit 7) goes to 1. Across the screen that takes
629 to 5792 microseconds for X and 1257 to 4307 for Y, the range of the
HIRESMS reader (X 0-616 at double speed, Y 0-182 at normal speed). Programs
written for other timing can be matched with `hires_mouse_x` and
`hires_mouse_y` (microseconds for the left/top and right/bottom edges):
OS-9's driver reads 0 to 639 with `1133,5423` on both.

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

In the menus either GPIO joystick moves the cursor and fire chooses, as a
gamepad's D-pad and A do.

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
also gives NitrOS-9 the time, and its network (below). It has no `/Z`
windows, printing or named objects.

### NitrOS-9 on the network

With networking on, the built-in server's virtual channels (NitrOS-9's
`/N`, `/N0` to `/N14`) reach the internet, as with DriveWire 4 or
pyDriveWire, so NitrOS-9's network programs and terminal programs work
with no other computer. The first line a program writes to a channel says
what it is for:

- `tcp connect host port`: what NitrOS-9's network programs send (its
  `lib/net.as`). The server connects, in the background (NitrOS-9 keeps
  running), and answers `OK connected to host:port`, or `FAIL` and an
  error number (246: nothing answered, 216: no such host). From then on
  the channel is the connection, both ways, until either end closes it.
- `ATDT host:port` and the other AT commands: a Hayes modem, as the
  RS-232 Pak's (see below), for terminal programs on `/N`: `CONNECT` or
  `NO CARRIER`, `+++`, `ATH`, `ATO`, `ATZ`, `ATV`, `ATQ`, `ATI`. As with
  pyDriveWire, the modem doesn't echo what's typed until `ATE1`.
- `dw disk show`, `dw server show`: what NitrOS-9's `dw` command shows.

Incoming connections (`tcp listen`, `join`, `kill`, and a modem answering
calls) aren't supported.

To use a server on another computer instead (DriveWire 4 or pyDriveWire on
a PC, for example), set *Server* to *Another Computer* and type its
address, with `:port` if it isn't 65504 (`becker_ip` and `becker_port` in
`cmdline.txt`), and turn on networking. BMCoco connects once the Pi has an
address, in the background (the CoCo keeps running), and tries again every
10 seconds if the server isn't there; the log says when it connects. An
address of `127.0.0.1` or `localhost`, as in an `xroar.conf` from a PC,
means the built-in server: nothing else can listen there on the Pi.

## The RS-232 Pak and calling BBSes

BMCoco can add Tandy's RS-232 Pak, with a virtual modem on its cable, for
calling telnet BBSes from CoCo terminal programs: the Deluxe RS-232
Program Pak, Ultimaterm, Mikey-Term, Greg-E-Term and others written for the
Pak (a 6551 at `$FF68`), and NitrOS-9's `/T2`. Turn on *RS-232 Pak
(Modem)* in the menu's *Cartridge* page (`rs232=1` in `cmdline.txt`), and
networking.

The Pak goes in beside the cartridge plugged in, as if both were in a
Multi-Pak: Disk BASIC's controller usually is, so terminal programs can be
loaded from disk. With no cartridge at all the Pak isn't there either; a
terminal program on a cartridge (the Deluxe RS-232 Program Pak's ROM, from
*Load Cartridge File*) is the cartridge then.

In the terminal program, type:

```
ATDTbbs.example.com:23
```

with the BBS's name or IP address and its port (23, telnet's, if there is
none). The call is made in the background, so the CoCo keeps running
while it connects: `CONNECT` (with the Pak's baud rate) when it does, `NO
CARRIER` if nobody answers (which can take a few minutes for an address
where nothing is there at all). Any key while it dials stops it.

Other commands: `+++` (with a second of quiet before and after) to get
back to the modem with the call kept, `ATH` to hang up, `ATO` to go back
online, `ATZ` to reset the modem, `ATE0` / `ATE1` echo off and on, `ATV0` /
`ATV1` number or word replies, `ATQ1` no replies, `ATI` what it is. The
settings terminal programs set modems up with (`AT&F`, `&C1`, `&D2`,
`S0=0`, `X4` and so on) answer `OK`. Dropping DTR (as many programs do to
hang up) hangs up too, and so does a reset (F10), which drops it, or
changing the machine. The modem doesn't answer calls (`ATA` says `NO
CARRIER`).

The Pak takes bytes as fast as the program reads them and sends them at
once, whatever the baud rate set. Its interrupt goes to the cartridge
port's CART line, as on the real Pak; programs that use it (rather than
watching the status register) haven't been tried yet.

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
| `disk_activity`     | `0`, `1`                     | `1`      | While a floppy disk, a hard disk or a DriveWire drive is read or written, show the drive and sector at the bottom left (Disk Drives, Show Disk Activity) |
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
| `joy_right`, `joy_left` | `joy0`, `joy1`, `kjoy0` (arrow keys), `mjoy0` (mouse), `hjoy0` (Hi-Res Mouse) | `joy0`, `joy1` | What is plugged into each joystick port |
| `joy1_pad`, `joy2_pad` | `0` automatic, `1`-`4` a gamepad, `-1` none | `0` | Gamepad for Joystick 1 and 2 |
| `pad_keys`          | `0`, `1`                     | `0`      | The gamepads' X and Y type Space and Enter rather than fire (see Joysticks) |
| `mouse_speed`       | percent                      | `100`    | How far the mouse moves the pointer (see Joysticks) |
| `hires_mouse_x`, `hires_mouse_y` | start,end (microseconds) | `629,5792`, `1257,4307` | The Hi-Res Mouse's timing for each axis (see Joysticks) |
| `ram_NAME`          | K, e.g. `ram_coco3=2048`     | machine's | The RAM of machine NAME (the menu's *Memory*) |
| `hd0`, `hd1`, `hd_cart` | paths, a cartridge name  | none     | Hard disk images and their cartridge (the menu's *Hard Disks*) |
| `rs232`             | `0`, `1`                     | `0`      | The RS-232 Pak with a virtual modem, beside the cartridge (Cartridge, RS-232 Pak) |
| `becker_ip`, `becker_port` | address, port         | none (built in), `65504` | The Becker port's DriveWire server on another computer |
| `dw0` ... `dw3`     | paths                        | none     | The built-in DriveWire server's disk images (the menu's *DriveWire*) |
| `gpio_joysticks`    | `0`, `1`                     | `0`      | Joysticks on the GPIO header |
| `enable_logging`    | `off`, `uart`, `file`        | `off`    | Where the log goes (see Logging) |
| `network`           | `off`, `ethernet`, `wifi`    | `off`    | Networking (see Networking and the web UI) |
| `webui`             | `0`, `1`                     | `1`      | Web UI when networking is on |
| `webui_pin`         | digits                       | none     | PIN for the web UI |
| `timezone`          | minutes east of UTC          | `0`      | Time zone for the network clock |
| `usbpowerdelay`     | milliseconds                 | Circle's | Circle: wait for USB devices to power up (by default 300 ms, 510 ms behind a hub) |

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
`usbpowerdelay` (add `usbpowerdelay=1000` to `cmdline.txt`).

**8BitDo M30 2.4G buttons are wrong**: its receiver can be left in Switch
mode, where it acts as a Switch Pro controller. With the pad connected,
hold Home (the heart) and D-pad Down for 5 seconds until the LEDs flash; it
goes back to its own mode and keeps it.

**The TV cuts off the top and bottom of the picture or the menu**: many TVs
enlarge the picture and crop its edges (overscan). Make *Video*, *Screen
Size* smaller (90% usually does), or turn overscan off on the TV ("Just
Scan", "Screen Fit" or "1:1").

**NitrOS-9 can't reach the network**: check that networking is on (the
address shows at the bottom of the screen at start up) and that the Becker
port uses the built-in server (*DriveWire*, *Server*); the log shows each
channel's calls (`DriveWire channel 0: connected to ...`).

**A terminal program can't reach a BBS**: check that *RS-232 Pak (Modem)*
is on in *Cartridge*, that a cartridge is plugged in (Disk BASIC's
controller, say), that the network is on, and the BBS's address and port:
`ATDT` then the name or IP address, a colon, and the port. `NO CARRIER`
means nothing answered there.

**A program seems stuck loading**: watch the bottom left of the screen.
While a floppy disk is read, a red light shows there with the drive (D),
track (T), side (H) and sector (S), as in `D0 T17 H0 S03`. A light that
stays on with the track and sector not moving, or a red message (`no disk`:
the drive is empty; `not found` or `CRC error`: a sector the program wants
isn't on the disk, or is damaged; `seek error`), usually means the wrong
disk is in the drive or the disk image is bad. Many DOSes retry for ever
rather than give up.

Hard disks and DriveWire drives are shown there the same way, whichever
was busy last:

- `HD0 LSN 1234`: hard disk 0 or 1 of the IDE interface (HDB-DOS,
  NitrOS-9), or the NX32's or MOOH's SD card, at sector 1234. A program
  that uses the drive's cylinders and heads instead shows `HD0 C12 H3 S4`.
  `error` in red: the drive turned the command down (a sector past the
  end of the image, or no drive there).
- `DW0 LSN 1234`: DriveWire drive 0 at sector 1234, with the built-in
  server or one on another computer. In red, what the server answered:
  `no disk` (nothing in that drive), `write protected`, `CRC error`,
  `read error` or `write error`.

**A game's colours are wrong**: choose the other composite phase in *Video*,
*TV Input*.

**A game doesn't respond to keys**: turn off *Typed Characters* in
*Keyboard*.

**A disk won't load**: Disk BASIC needs `disk11.rom` in `roms`. `DIR` lists
the disk; `RUN"NAME"` starts a BASIC program and `LOADM"NAME":EXEC` a
machine code one.
