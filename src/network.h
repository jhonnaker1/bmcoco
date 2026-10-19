//
// network.h
//
// Ethernet or Wi-Fi networking and the web UI, set up from cmdline.txt.
// Modified for BMA800XL from BMC64's network start up (viceapp.cpp,
// vice_network.cpp): rewritten to take its settings from cmdline.txt and
// start the web UI and network time.
// Changed for BMCoco.
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
