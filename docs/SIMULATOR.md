# Testing and debugging BMCoco on a PC

BMCoco isn't tested in QEMU. On the Pi it runs bare metal, on the Circle
kernel with no Linux underneath, and QEMU's Raspberry Pi machines don't
emulate enough of the Pi (its VideoCore display, HDMI sound, USB and Wi-Fi
as BMCoco uses them) to run it. There would be nothing to strace either:
strace follows a Linux program's system calls, and BMCoco makes none.

What BMCoco has instead is a **PC simulator** (`tools/hostsim`): the same
emulator code that runs on the Pi (XRoar and BMCoco's port of it,
`src/port/`), built as an ordinary program for Linux or macOS, with a
simulated display, sound, keyboard, joysticks and SD card in place of the
Pi's. Because it's an ordinary program, **gdb, lldb, strace and the rest
work on it** as on any other program.

## What it can and can't show

The simulator runs the CoCo emulation, the menu, the disk, tape and hard
disk handling, the Becker port and the built-in DriveWire server, and
BMCoco's pacing and sound logic. A problem in any of those usually shows up
in it too.

It does **not** run the Pi side: the Circle kernel, the Pi's display and
sound drivers, USB, Wi-Fi and Circle's network (TCP) stack, and the SD card
driver. In the simulator the Becker port talks to a DriveWire server
through the PC's own network, so a hang inside Circle's network stack won't
happen there. And a PC is much faster than a Pi; `BMCOCO_SIM_FRAME_MS`
(below) makes each frame slow, to stand in for a Pi that can't keep up.

So the simulator is the first place to look, and if a problem doesn't show
up there, the Pi's log (at the end of this page) is the next.

## Setting it up

You need a C compiler, `make`, `git` and Python 3. No Raspberry Pi
toolchain is needed for the simulator.

**Linux** (Debian, Ubuntu, Raspberry Pi OS):

```
sudo apt install build-essential git python3 gdb strace
```

**macOS**: install Xcode's command line tools (`xcode-select --install`),
which include `clang`, `make`, `git` and the `lldb` debugger.

Get the source, with XRoar (a git submodule):

```
git clone https://github.com/jhonnaker1/bmcoco
cd bmcoco
git submodule update --init third_party/xroar
```

(`git clone --recursive` works too, but also fetches circle-stdlib, which
only the Pi build needs.)

Build and run the tests:

```
tools/hostsim/run_tests.sh
```

The first build takes a minute or two. The CoCo ROMs can't be distributed,
so without any the tests run a small stand-in ROM. To test with Color
BASIC, Disk BASIC and the CoCo 3, point it at a folder with your ROMs (the
same files as in the SD card's `roms` folder):

```
BMCOCO_ROMS=/path/to/your/roms tools/hostsim/run_tests.sh
```

Each test runs in its own folder under `tools/hostsim/out/`, which stands
in for the SD card, with its log (`log.txt`) and screenshots (`.ppm`).

## Using it live

```
tools/hostsim/live.sh /path/to/your/roms
```

then open <http://127.0.0.1:8064/> in a browser. The CoCo's screen, sound,
keyboard and gamepads are in the page, and F12 opens BMCoco's menu as on
the Pi. The simulator's SD card is `tools/hostsim/out/live`: copy disks,
tapes and programs into its `disks`, `tapes` and `programs` folders, and
`xroar.conf` or `bmcoco.cfg` into it if you want the same settings as your
Pi's card. Options from `cmdline.txt` go in `BMCOCO_CMDLINE`:

```
BMCOCO_CMDLINE="machine=coco3" tools/hostsim/live.sh /path/to/your/roms
```

Its messages (the same as the Pi's log) go to the terminal. Ctrl+C stops
it.

## Debugging it

### Where is it stuck? (gdb or lldb)

Start it live as above, get it into the state where it's stuck, and attach
a debugger to it from another terminal.

Linux:

```
sudo gdb -p $(pgrep -f build/bmcoco_sim)
(gdb) thread apply all bt
```

macOS:

```
lldb -p $(pgrep -f build/bmcoco_sim)
(lldb) thread backtrace all
```

The backtrace shows where each thread is, in BMCoco's and XRoar's source
(the simulator is built with debugging information). When the emulation is
running normally, the main thread is somewhere under `xroar_run()` (the
CoCo's CPU) or `host_video_present()` (waiting for the next frame). Type
`continue` to let it go on, or `detach` and `quit` to leave it running.

To run it under the debugger from the start instead, run it from its SD
card folder:

```
cd tools/hostsim/out/live
BMCOCO_LIVE=8064 BMCOCO_CMDLINE="root=." gdb ../../build/bmcoco_sim
(gdb) run < /dev/null
```

and press Ctrl+C in gdb when it's stuck.

### What is it doing on the network? (strace, Linux)

```
strace -f -tt -e trace=network,read,write -p $(pgrep -f build/bmcoco_sim)
```

shows every connection to the DriveWire server and every byte sent and
received, with the time. The browser page uses the network too; the
DriveWire traffic is the connection to the server's port (65504 by
default). On macOS, `sudo dtruss -p PID` is the nearest equivalent, if
System Integrity Protection allows it.

### XRoar's own traces

XRoar can log what its disk controller does and the Becker port's traffic.
Put this in `xroar.conf` on the simulator's SD card (or on the Pi's):

```
debug-fdc 0x0b
```

The value is a sum of these:

| Value  | Logs                                               |
|--------|----------------------------------------------------|
| `0x03` | Each disk controller command and its state         |
| `0x04` | Every byte read from or written to the disk        |
| `0x08` | Every byte through the Becker port (DriveWire)     |
| `0x10` | The controller's timing events (a lot)             |

The registers are printed in hexadecimal: `TR=22 SR=10` is track 34,
sector 16.

### Scripts

The simulator can also run a script of key presses, typing and screenshots
on its standard input, with time measured in frames, so that a problem can
be repeated exactly. For example, with your ROMs in `roms`:

```
cd tools/hostsim/out/live
printf '200 type DIR\\n\n400 shot dir.ppm\n401 quit\n' |
    BMCOCO_CMDLINE="root=." ../../build/bmcoco_sim
```

types `DIR` and Enter at frame 200 and saves the screen at frame 400. The
commands are listed at the top of `tools/hostsim/host_sim.c`, and
`tools/hostsim/run_tests.sh` has many examples.

Useful settings, as environment variables:

| Variable                     | Does                                                        |
|------------------------------|-------------------------------------------------------------|
| `BMCOCO_CMDLINE`             | Options as in `cmdline.txt` (`root=.` is the SD card)       |
| `BMCOCO_REFRESH`             | The display's refresh rate (default 60)                     |
| `BMCOCO_SIM_FRAME_MS`        | Make each frame's emulation take this long, like a slow Pi  |
| `BMCOCO_SIM_CONNECT_FRAMES`  | Make connecting to a DriveWire server take this many frames |
| `BMCOCO_SIM_RATE`            | The sound output's rate (48000 for a Pi 4 or 5's HDMI)      |
| `BMCOCO_SIM_WAV`             | Save the sound to this WAV file                             |
| `BMCOCO_SIM_CPU`             | Print how long each frame takes to emulate on the PC        |
| `BMCOCO_IP`                  | Pretend the network is up with this address (from frame 100): needed to reach a DriveWire server or a BBS |
| `BMCOCO_WIFI`                | Pretend Wi-Fi is running, with a few networks to choose from |
| `BMCOCO_REFRESH_LATER`       | Change the display's refresh rate to this at frame 30: a display slightly off its first measurement |

## On the Pi itself

On the Pi there is no debugger or strace, but there is a log. Add
`enable_logging=file` to the line in `cmdline.txt`, and BMCoco writes
`bmcoco.log` in the root of the SD card (and to the serial port: GPIO14 TX
and GPIO15 RX at 115200 baud, for a USB serial adapter, which keeps working
even when the SD card doesn't).

What the log shows:

- The Pi, the display and sound, the machine set up, and (after 0.5.2) the
  CPU clock speed.
- Every 30 seconds the load: how much of each frame the emulation takes.
  Over 100% means the Pi can't keep up.
- If the emulation stops moving for 3 seconds, `Emulator has not moved for
  3 s; last seen:` and what it was doing (emulating, showing a frame,
  running Circle's tasks such as the network, writing sound...). That line
  is the most useful one for a hang.
- With `debug-fdc` in `xroar.conf` (above), the disk controller or the
  Becker port's traffic.

When something goes wrong, the most helpful report has:

1. What you did, step by step, and what happened.
2. The BMCoco version and which Pi.
3. `bmcoco.log` from the card, with `enable_logging=file` (taking the card
   out right after the problem; the log is written as it goes).
4. Your `cmdline.txt`, `bmcoco.cfg` and `xroar.conf` if you have one.
5. If it involves a disk or program, which one, or the file if it's free
   to share.
