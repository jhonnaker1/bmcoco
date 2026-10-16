BMCoco - bare metal Tandy Color Computer emulator for the Raspberry Pi
=====================================================================

Copy everything here to the root of a FAT32 SD card and boot a Raspberry
Pi 5, 500, 4, 400, 3, Zero 2 W, 2, Zero or Zero W from it, with a USB
keyboard or gamepad connected.

First copy your Color Computer ROMs into the roms folder: bas13.rom and
extbas11.rom for a CoCo 2, disk11.rom for disks, coco3.rom for a CoCo 3
(see roms/ROMS.txt). They are copyrighted, so they are not included.

It starts in Color BASIC. Press F12 for the menu: run a program, disks,
hard disks, DriveWire, cassette, cartridge, machine, memory, joysticks,
video, keyboard, sound, network.

  F12           Menu                F10 / Shift+F10  Reset / hard reset
  F11           Turbo               Esc              BREAK
  Home          CLEAR               Alt, Caps Lock   CoCo 3 ALT, CTRL

Gamepads are the CoCo's joysticks: stick or D-pad to move, A to fire,
X Space, Y Enter, Home (or Select+Start) for the menu.

Put your files in the disks, tapes, carts and programs folders (or
anywhere).

Video is 720p at 60 Hz for an NTSC CoCo. For a PAL CoCo, choose it under
Machine in the menu; on a Pi Zero, 2 or 3 also set hdmi_mode=19 in
config.txt (a Pi 4 or 5 switches to 50 Hz itself).

Networking: choose Ethernet or Wi-Fi under Network in the menu (or add
network=ethernet or network=wifi to cmdline.txt), then open the address
shown on screen in a browser to manage the files on this card.

Options are in cmdline.txt; see docs/USER_GUIDE.md in BMCoco's source for
all of them.
