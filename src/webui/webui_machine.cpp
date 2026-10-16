//
// webui_machine.cpp
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
