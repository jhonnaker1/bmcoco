// The machine BMCoco is running and what the web UI can do on it, from the
// "machine" and "caps" of /api/status (webui_machine.cpp on the device).

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
