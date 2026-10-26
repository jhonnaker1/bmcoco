//
// machine.js
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
// Modified for BMA800XL from BMC64's src/webui/assets/js/machine.js: the
// Atari 800XL, without profiles and the BASIC editor.
//
// Modified for BMCoco.
//
// The machine BMCoco is running and what the web UI can do on it, from the
// "machine" and "caps" of /api/status (webui_machine.cpp on the device).
//

import * as api from "./api.js";

// Nothing is offered until the device has answered.
let current = { name: "", settings: "", run: new Set() };

// Takes the machine from a /api/status answer.
export function setMachine(status) {
  const caps = (status && status.caps) || {};
  current = {
    name: (status && status.machine) || "",
    settings: caps.settings || "",
    run: new Set((caps.run || []).map((t) => String(t).toLowerCase())),
  };
}

// Asks the device once; on failure the status polling fills it in later.
export async function loadMachine() {
  try {
    setMachine(await api.getStatus());
  } catch (e) {
    /* keep what we have */
  }
}

// Machine name, e.g. "Tandy CoCo 3 (NTSC)".
export const machineName = () => current.name;

// Name for display.
export const machineDisplayName = () => current.name || "machine";

// The emulator's settings file, "/xroar.conf".
export const mainSettingsFile = () => current.settings;

// Whether Autostart accepts files with this extension.
export const canRun = (ext) => current.run.has(String(ext).toLowerCase());
