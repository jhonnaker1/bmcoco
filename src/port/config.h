/*
 * BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
 * Copyright (C) 2026 aminch <https://github.com/aminch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Modified for BMCoco.
 *
 * config.h - XRoar's build options for BMCoco.
 *
 * XRoar's configure script normally writes this file. BMCoco builds XRoar
 * with its own makefile (src/port/xroar.mk), so the options are set here by
 * hand: every machine and cartridge XRoar has, no host toolkits, and the
 * user interface, video, sound and joystick modules from src/port/.
 */

#ifndef BMCOCO_XROAR_CONFIG_H_
#define BMCOCO_XROAR_CONFIG_H_

#define PACKAGE_NAME "xroar"
#define PACKAGE_TARNAME "xroar"
#define PACKAGE_TEXT "XRoar"
#define PACKAGE_YEAR "2026"
#ifndef XROAR_VERSION
#define XROAR_VERSION "1.12.1"
#endif
#define PACKAGE_VERSION XROAR_VERSION
#define PACKAGE_STRING "XRoar " XROAR_VERSION
#define VERSION XROAR_VERSION
#define RC_REV_MAJOR 1
#define RC_REV_MINOR 12

/* Machines: Dragon 32/64 and the CoCo 1/2 share one architecture. */
#define WANT_MACHINE_ARCH_DRAGON 1
#define WANT_MACHINE_ARCH_COCO3 1
#define WANT_MACHINE_ARCH_MC10 1

/* Cartridges: RS-DOS and the other Dragon/CoCo cartridges, and the MC-10's. */
#define WANT_CART_ARCH_DRAGON 1
#define WANT_CART_ARCH_MC10 1

/* Chips */
#define WANT_PART_MC6801 1
#define WANT_PART_MC6809 1
#define WANT_PART_MC6821 1
#define WANT_PART_MC6847 1
#define WANT_PART_MC6883 1
#define WANT_PART_TCC1014 1

/* The Becker port (DriveWire over the network): src/port/becker.c. */
#define WANT_BECKER 1

/* The emulator's log messages go to BMCoco's log. */
#define LOGGING 1

/* newlib has these. XRoar's configuration parser needs regular
   expressions. */
#define HAVE_STRNLEN 1
#define HAVE_STRSEP 1
#define HAVE_REGEX_H 1

/* GCC features */
#define HAVE___BUILTIN_EXPECT 1
#define HAVE_FUNC_ATTRIBUTE_CONST 1
#define HAVE_FUNC_ATTRIBUTE_FORMAT 1
#define HAVE_FUNC_ATTRIBUTE_MALLOC 1
#define HAVE_FUNC_ATTRIBUTE_NONNULL 1
#define HAVE_FUNC_ATTRIBUTE_NORETURN 1
#define HAVE_FUNC_ATTRIBUTE_PURE 1
#define HAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL 1
#define HAVE_VAR_ATTRIBUTE_PACKED 1

/* Where XRoar looks for ROMs and xroar.conf: the SD card. "~" is not used:
   there is no home directory. */
#ifndef ROMPATH
#define ROMPATH "/roms:/"
#endif
#ifndef CONFPATH
#define CONFPATH "/"
#endif

#endif /* BMCOCO_XROAR_CONFIG_H_ */
