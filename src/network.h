//
// network.h
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
// Modified for BMA800XL from BMC64's src/vice_network.h: rewritten for
// BMA800XL's network start up from cmdline.txt.
//
// Modified for BMCoco.
//
// Ethernet or Wi-Fi networking and the web UI, set up from cmdline.txt.
//
// Ethernet or Wi-Fi networking and the web UI, set up from cmdline.txt.
// Modified for BMA800XL from BMC64's network start up (viceapp.cpp,
// vice_network.cpp): rewritten to take its settings from cmdline.txt and
// start the web UI and network time.
// Changed for BMCoco.
//

#ifndef BMCOCO_NETWORK_H_
#define BMCOCO_NETWORK_H_

class Options;

// Same values as BMC64's CIRCLE_NETWORK_* (the web UI shows them).
enum {
  NETWORK_DISABLED,
  NETWORK_ETHERNET_INITIALIZING,
  NETWORK_ETHERNET_WAITING_FOR_DHCP,
  NETWORK_ETHERNET_CONNECTED,
  NETWORK_ETHERNET_UNAVAILABLE,
  NETWORK_ETHERNET_INIT_FAILED,
  NETWORK_WIFI_CONFIG_MISSING,
  NETWORK_WIFI_DEVICE_INITIALIZING,
  NETWORK_WIFI_DEVICE_INIT_FAILED,
  NETWORK_WIFI_NETWORK_INIT_FAILED,
  NETWORK_WIFI_WPA_INITIALIZING,
  NETWORK_WIFI_WPA_INIT_FAILED,
  NETWORK_WIFI_CONNECTING,
  NETWORK_WIFI_CONNECTED,
  NETWORK_WIFI_CONNECTION_TIMEOUT,
  NETWORK_WIFI_UNAVAILABLE,
  NETWORK_WIFI_DEVICE_NOT_INITIALIZED,
  NETWORK_WIFI_UNSUPPORTED,
  NETWORK_WIFI_FIRMWARE_MISSING,
};

// Starts networking as cmdline.txt asks (network=ethernet|wifi, webui=,
// webui_pin=, timezone=). Call once, on core 0, before USB is initialised.
void NetworkInitialize(const Options *options);

extern "C" {
// The IP address as text, if the network is up (returns 0 if not).
int circle_get_network_ip_address(char *address, unsigned int address_size);
// One of the NETWORK_* values above.
int circle_get_network_status(void);
}

#endif
