//
// board.cpp
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
// See board.h.
//
// Sources:
//  - Raspberry Pi Linux device trees, bcm2712-rpi-5-b.dts (pwr_button:
//    gpio-keys, KEY_POWER, <&gio 20 GPIO_ACTIVE_LOW>, debounce-interval 50,
//    pull-up) and bcm2712-rpi-500.dts (the same key); bcm2712.dtsi (gio at
//    0x7D508500); Circle, include/circle/bcm2712.h (ARM_GPIO1_DATA0, the
//    first bank's levels).
//
// Modified for BMA800XL from BMC64's src/pi45/board.cpp: only
// Pi45BoardPoll's Pi 5 power button and fan kept (the sources of the
// removed parts dropped from the list above); it calls BMA800XL's
// bma_request_power instead of emu_power_interrupt and logs "Shut Down".
// In BMCoco it calls bmcoco_request_power.
//

#include "board.h"
#include "fan.h"

#if RASPPI >= 5

#include <circle/bcm2712.h>
#include <circle/logger.h>
#include <circle/memio.h>
#include <circle/timer.h>

// From src/port/platform.c: the emulator's Reboot / Shut Down request.
// From src/port/platform.c: the Reboot / Shut Down request.
extern "C" void bmcoco_request_power(int power_off);

static const char FromBoard[] = "board";

void Pi45BoardPoll(void) {
  Pi45FanPoll();

  // The power button is GPIO 20 of the BCM2712's own GPIO block ("gio",
  // Circle's ARM_GPIO1_BASE), low while pressed, held for 50 ms (the device
  // trees' debounce time). It counts only once it has been seen released.
  static bool seen_up = false;
  static unsigned down_since = 0;   // clock ticks it went down, or 0
  const u32 Button = 1u << 20;
  bool down = (read32(ARM_GPIO1_DATA0) & Button) == 0;
  unsigned now = CTimer::GetClockTicks();
  if (!down) {
    seen_up = true;
    down_since = 0;
    return;
  }
  if (!seen_up) {
    return;
  }
  if (down_since == 0) {
    down_since = now | 1;
  } else if (now - down_since >= CLOCKHZ / 20) {
    CLogger::Get()->Write(FromBoard, LogNotice, "Power button: Shut Down");
    bmcoco_request_power(1);
    seen_up = false;
  }
}

#endif
