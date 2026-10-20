# Updating XRoar and Circle

## XRoar

XRoar is a git submodule in `third_party/xroar`, from
`https://www.6809.org.uk/git/xroar.git`, pinned to a release tag. It is used
unmodified; everything specific to BMCoco is in `src/port/`:

| File | What it holds |
| --- | --- |
| `src/port/xroar.mk` | The list of XRoar source files to compile |
| `src/port/config.h` | XRoar's build options (normally made by configure) |
| `src/port/platform.c` | The user interface, video and sound modules |
| `src/port/joystick.c` | The joystick module ("physical" joysticks, the mouse, the keyboard's) |
| `src/port/menu.c` | The menu, which talks to XRoar through its UI messages |
| `src/port/becker.c` | XRoar's Becker port (`becker.h`), in place of XRoar's `becker.c` |
| `src/port/posix_extra.c`, `fswrap.c` | Stand-ins for what newlib and FatFs lack |

The rest of `src/port/` (the built-in DriveWire server, the RS-232 Pak, the
Hi-Res Mouse, the activity light) reaches into XRoar's parts from outside,
through their public structures (`cart.h`, `mc6821.h`, `wd279x.h`,
`joystick.h`), and wraps their functions and delegates. When XRoar changes
those, check them: see the list in [BUILDING.md](BUILDING.md#layout).

To move to a new release:

1. Check out the new tag in the submodule:

   ```
   cd third_party/xroar
   git fetch --tags
   git checkout 1.13           # the new release tag
   cd ../..
   ```

2. Compare the build files between the two tags:

   ```
   cd third_party/xroar
   git diff 1.12.1 1.13 --stat -- src/Makefile.am portalib/Makefile.am configure.ac
   ```

   - Sources added to or removed from `xroar_SOURCES` (or the machine,
     cartridge and part sections BMCoco enables) go in `src/port/xroar.mk`.
     Front ends for other systems (`sdl2/`, `gtk3/`, `x11/`, `alsa/` and so
     on) are not needed.
   - New `AC_DEFINE`s in `configure.ac` may need a line in
     `src/port/config.h`. Never run configure inside `third_party/xroar`:
     the generated `config.h` would be used instead of BMCoco's (the
     Makefile stops if it finds one).
   - Changes to `ui.h`, `vo.h`, `ao.h`, `sound.h`, `joystick.h` or `hkbd.h`
     may need the same in `src/port/`.

3. Build and test on the PC, then for the Pi:

   ```
   tools/hostsim/run_tests.sh
   ./build_sdcard.sh
   ```

4. Commit the submodule and any port changes together:

   ```
   git add third_party/xroar src/port
   git commit -m "Update XRoar to 1.13"
   ```

## circle-stdlib (Circle and newlib)

circle-stdlib is a submodule in `third_party/circle-stdlib`, pinned to the
same revision BMC64 uses. `make_all.sh` applies the patches in `src/patches/`
to Circle before building:

- `circle_sysconfig_patch.diff` enables multi-core and the USB FIQ
- `circle_linux_bug_patch.diff` turns off `BUG_ON`, whose checks in Circle's
  Linux emulation (used by VCHIQ for display and sound) only allow core 0;
  the emulator calls them from core 1, as BMC64 does
- `circle_bolt_keyboard_patch.diff` adds Logitech Bolt keyboard support
  (from BMC64)

To update, move the submodule to a new circle-stdlib release, run
`git submodule update --init --recursive`, then `./build_sdcard.sh`. If a
patch no longer applies, update it from the matching BMC64 patch or by
hand. `src/fbl.cpp` and `src/vchiqsoundbasedevice.cpp` use Circle's VC4
addon and may need the same changes BMC64 makes when it updates Circle.
