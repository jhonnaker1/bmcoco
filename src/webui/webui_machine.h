//
// webui_machine.h
//
// What the web UI can do on the machine this build emulates: the machine
// running, the file types Autostart accepts, and the emulator's settings
// file. GET /api/status reports these as "caps" so the page needs no
// machine knowledge of its own.
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

#ifndef _webui_machine_h
#define _webui_machine_h

#include <circle/types.h>

struct WebUiMachine {
  // The emulator's settings file.
  const char *settings;
  // Lowercase extensions Autostart accepts, ending with a null.
  const char *const *run_types;
};

const WebUiMachine *WebUiMachineGet(void);

// The machine running, as XRoar describes it ("Tandy CoCo 3 (NTSC)"), or ""
// before one has started.
const char *WebUiMachineName(void);

// TRUE if Autostart accepts files with this extension (any case).
boolean WebUiMachineCanRun(const char *ext);

// Writes the "caps" JSON object for /api/status. Returns its length, or
// -1 if it doesn't fit.
int WebUiMachineCapsJson(char *out, unsigned size);

#endif
