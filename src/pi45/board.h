//
// board.h
//
// BMC64 - A bare metal Commodore emulator for Raspberry Pi
// Copyright (C) 2026 aminch <https://github.com/aminch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// What the Pi 4 and 5 need beyond the display and sound, called from
// BMC64's kernel (src/kernel.cpp) and power code (src/power/power.cpp).
//
// Modified for BMA800XL from BMC64's src/pi45/board.h: only
// Pi45BoardPoll kept, for the Pi 5, called from the kernel's core 0 loop
// (kernel.cpp); the Pi 400's power key stays in src/port/keyboard.c.
//

#ifndef BMC64_PI45_BOARD_H_
#define BMC64_PI45_BOARD_H_

#if RASPPI >= 5

// Core 0, often: the Pi 5's power button (the Pi 500's power key) as Shut
// Down, as the menu does; the fan (fan.h).
void Pi45BoardPoll(void);

#endif

#endif
