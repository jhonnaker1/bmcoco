//
// webui.h
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
// Modified for BMA800XL from BMC64's src/webui/webui.h: always built, with
// the BMA800XL host name, version and power and reset requests; BMC64's
// profiles are removed.
//
// Modified for BMCoco.
//
// Optional LAN web UI, from BMC64. Serves a small single-page app that
// shows status, manages the files on the SD card and can reboot the
// machine. All web UI code lives under src/webui/.
//

#ifndef _webui_h
#define _webui_h

// BMCoco always builds the web UI.
#define BMC64_WEBUI 1

class CNetSubSystem;

// Start the web UI HTTP server as a Circle scheduler task (runs on the
// kernel/networking core, never the emulator core). Call once, after the
// network subsystem has been created. Safe to call with a null pointer
// and safe to call more than once; only the first call has an effect.
//
// pin: optional HTTP Basic Auth PIN. When null or empty the server is
// open; otherwise every request must carry Authorization: Basic
// base64(<anything>:<pin>).
void WebUiStart(CNetSubSystem *network, const char *pin);

#endif
