//
// webui_machine.cpp
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
// Modified for BMA800XL from BMC64's src/webui/webui_machine.cpp: the
// Atari 800XL's name, settings file and the file types atari800 can start,
// in place of BMC64's machines.
//
// Modified for BMCoco: the CoCo machine.
//

#include "webui.h"

#if BMC64_WEBUI

#include "webui_machine.h"

#include <stdio.h>

// The machine running (src/port/platform.c).
extern "C" const char *bmcoco_machine_description(void);

namespace {

// What XRoar can start directly (xroar_load_file_by_type): disk images,
// tapes, cartridges and machine code programs.
const char *const kRunTypes[] = {
    "dsk", "vdk", "jvc", "os9", "dmk", "cas", "c10", "k7", "ccc", "rom",
    "bin", "hex", "s19", 0,
};

const WebUiMachine kMachine = {
    "/xroar.conf", kRunTypes,
};

char Lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

}  // namespace

const WebUiMachine *WebUiMachineGet(void) {
  return &kMachine;
}

const char *WebUiMachineName(void) { return bmcoco_machine_description(); }

boolean WebUiMachineCanRun(const char *ext) {
  for (const char *const *type = kMachine.run_types; *type != 0; type++) {
    const char *a = ext;
    const char *b = *type;
    while (*a != '\0' && Lower(*a) == *b) {
      a++;
      b++;
    }
    if (*a == '\0' && *b == '\0') return TRUE;
  }
  return FALSE;
}

int WebUiMachineCapsJson(char *out, unsigned size) {
  char run[128];
  unsigned used = 0;
  for (const char *const *type = kMachine.run_types; *type != 0; type++) {
    int n = snprintf(run + used, sizeof(run) - used, "%s\"%s\"",
                     used == 0 ? "" : ",", *type);
    if (n < 0 || (unsigned) n >= sizeof(run) - used) return -1;
    used += (unsigned) n;
  }
  run[used] = '\0';

  int length = snprintf(out, size,
                        "{\"settings\":\"%s\",\"run\":[%s]}",
                        kMachine.settings, run);
  if (length < 0 || (unsigned) length >= size) return -1;
  return length;
}

#endif  // BMC64_WEBUI
