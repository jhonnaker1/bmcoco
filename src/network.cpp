//
// network.cpp
//
// Modified for BMA800XL from BMC64's network start up (viceapp.cpp,
// vice_network.cpp): rewritten to take its settings from cmdline.txt and
// start the web UI and network time.
// Changed for BMCoco: the Becker port's connections (host_tcp_*), on
// Circle's own sockets.
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

#include "network.h"

#include "network_time_sync.h"
#include "options.h"
#include "webui/webui.h"

#include <circle/logger.h>
#include <circle_glue.h>
#include <circle/machineinfo.h>
#include <circle/net/dnsclient.h>
#include <circle/net/in.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/netdevice.h>
#include <circle/sched/task.h>
#include <circle/string.h>
#include <fatfs/ff.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "host.h"

static const char FromNetwork[] = "network";
#define HOSTNAME "bmcoco"
#define FIRMWARE_PATH "SD:/firmware/"
#define WPA_CONFIG "SD:/wpa_supplicant.conf"

static CNetSubSystem *s_net = 0;
static CBcm4343Device *s_wlan = 0;
static CWPASupplicant *s_wpa = 0;
static int s_device = 0;  // 0 off, 1 Ethernet, 2 Wi-Fi
static int s_status = NETWORK_DISABLED;

static bool HasOnboardWLAN(TMachineModel model) {
  switch (model) {
  case MachineModelZeroW:
  case MachineModelZero2W:
  case MachineModel3B:
  case MachineModel3APlus:
  case MachineModel3BPlus:
  case MachineModel4B:
  case MachineModel400:
  case MachineModel5:
  case MachineModel500:
    return true;
  default:
    // The Pi 500+ is newer than Circle's list of models.
    return RASPPI >= 5;
  }
}

static bool HasOnboardEthernet(TMachineModel model) {
  switch (model) {
  case MachineModel2B:
  case MachineModel3B:
  case MachineModel3BPlus:
  // The Pi 4's Ethernet is in the SoC (Circle's CBcm54213Device), not on
  // USB; the Pi 5's in its RP1 chip (CMACBDevice).
  case MachineModel4B:
  case MachineModel400:
  case MachineModel5:
  case MachineModel500:
    return true;
  default:
    return RASPPI >= 5;
  }
}

static bool FileExists(const char *path) {
  FILINFO info;
  return f_stat(path, &info) == FR_OK && !(info.fattrib & AM_DIR) &&
         info.fsize > 0;
}

static bool HasWifiFirmware(void) {
  static const char *const files[] = {
      "brcmfmac43430-sdio.bin",  "brcmfmac43430-sdio.txt",
      "brcmfmac43430-sdio.clm_blob", "brcmfmac43436-sdio.bin",
      "brcmfmac43436-sdio.txt",  "brcmfmac43436-sdio.clm_blob",
      "brcmfmac43436s-sdio.bin", "brcmfmac43436s-sdio.txt",
      "brcmfmac43455-sdio.bin",  "brcmfmac43455-sdio.clm_blob",
      "brcmfmac43455-sdio.txt",
      // The Pi 400 (Circle's addon/wlan/ether4330.c: chip 0x4345 rev 9).
      "brcmfmac43456-sdio.bin",  "brcmfmac43456-sdio.txt",
      "brcmfmac43456-sdio.clm_blob",
#if RASPPI >= 5
      // The Pi 5, 500 and 500+ (Circle's addon/wlan/firmware/Makefile:
      // the 43455 files under the board's name).
      "brcmfmac43455-sdio.raspberrypi,5-model-b.bin",
      "brcmfmac43455-sdio.raspberrypi,5-model-b.txt",
      "brcmfmac43455-sdio.raspberrypi,5-model-b.clm_blob",
#endif
  };
  for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++) {
    char path[96];
    snprintf(path, sizeof path, "%s%s", FIRMWARE_PATH, files[i]);
    if (!FileExists(path)) {
      return false;
    }
  }
  return true;
}

static void StartServices(const Options *options) {
  // newlib's sockets (the Becker port's) go to this network.
  CGlueNetworkInit(*s_net);
  StartNetworkTimeSync(s_net);
  if (options->GetBool("webui", true)) {
    WebUiStart(s_net, options->Get("webui_pin", ""));
  }
}

void NetworkInitialize(const Options *options) {
  CLogger *log = CLogger::Get();
  const char *device = options->Get("network", "off");
  if (strcmp(device, "ethernet") == 0) {
    s_device = 1;
  } else if (strcmp(device, "wifi") == 0) {
    s_device = 2;
  } else {
    s_device = 0;
    s_status = NETWORK_DISABLED;
    log->Write(FromNetwork, LogNotice, "Networking not enabled");
    return;
  }

  // Minutes east of UTC, for file times from the network clock.
  if (!ConfigureSystemTimeZone(options->GetInt("timezone", 0))) {
    log->Write(FromNetwork, LogWarning, "Cannot configure timezone");
  }

  TMachineModel model = CMachineInfo::Get()->GetMachineModel();

  if (s_device == 1) {
    if (!HasOnboardEthernet(model)) {
      s_status = NETWORK_ETHERNET_UNAVAILABLE;
      log->Write(FromNetwork, LogError,
                 "Ethernet selected, but this Raspberry Pi has none");
      return;
    }
    s_status = NETWORK_ETHERNET_INITIALIZING;
    s_net = new CNetSubSystem(0, 0, 0, 0, HOSTNAME, NetDeviceTypeEthernet);
    if (!s_net->Initialize(FALSE)) {
      s_status = NETWORK_ETHERNET_INIT_FAILED;
      log->Write(FromNetwork, LogError, "Cannot initialize Ethernet");
      delete s_net;
      s_net = 0;
      return;
    }
    s_status = NETWORK_ETHERNET_WAITING_FOR_DHCP;
    log->Write(FromNetwork, LogNotice, "Ethernet initialized");
    StartServices(options);
    return;
  }

  if (!HasOnboardWLAN(model)) {
    s_status = NETWORK_WIFI_UNAVAILABLE;
    log->Write(FromNetwork, LogError,
               "Wi-Fi selected, but this Raspberry Pi has none");
    return;
  }
  if (!HasWifiFirmware()) {
    s_status = NETWORK_WIFI_FIRMWARE_MISSING;
    log->Write(FromNetwork, LogError, "Wi-Fi firmware is missing from %s",
               FIRMWARE_PATH);
    return;
  }
  s_status = NETWORK_WIFI_DEVICE_INITIALIZING;
  s_wlan = new CBcm4343Device(FIRMWARE_PATH);
  if (!s_wlan->Initialize()) {
    s_status = NETWORK_WIFI_DEVICE_INIT_FAILED;
    log->Write(FromNetwork, LogError, "Cannot initialize WLAN");
    delete s_wlan;
    s_wlan = 0;
    return;
  }

  s_net = new CNetSubSystem(0, 0, 0, 0, HOSTNAME, NetDeviceTypeWLAN);
  if (!s_net->Initialize(FALSE)) {
    s_status = NETWORK_WIFI_NETWORK_INIT_FAILED;
    log->Write(FromNetwork, LogError, "Cannot initialize WLAN network stack");
    delete s_net;
    s_net = 0;
    delete s_wlan;
    s_wlan = 0;
    return;
  }
  StartServices(options);

  // Without a network to join, Wi-Fi still starts so the menu can scan for
  // one.
  if (!FileExists(WPA_CONFIG)) {
    s_status = NETWORK_WIFI_CONFIG_MISSING;
    log->Write(FromNetwork, LogWarning,
               "No %s: Wi-Fi started for scanning only", WPA_CONFIG);
    return;
  }

  s_status = NETWORK_WIFI_WPA_INITIALIZING;
  s_wpa = new CWPASupplicant(WPA_CONFIG);
  if (!s_wpa->Initialize()) {
    s_status = NETWORK_WIFI_WPA_INIT_FAILED;
    log->Write(FromNetwork, LogError, "Cannot initialize WPA supplicant");
    delete s_wpa;
    s_wpa = 0;
    return;
  }
  s_status = NETWORK_WIFI_CONNECTING;
  log->Write(FromNetwork, LogNotice, "Wi-Fi initialized");
}

//
// Wi-Fi scan, from BMC64. The chip reports access points in "escan" result
// messages, each holding a list of BSS records.
//

namespace {

struct WifiBssInfo {
  uint32_t version;
  uint32_t length;
  uint8_t bssid[6];
  uint16_t beacon_period;
  uint16_t capability;
  uint8_t ssid_length;
  uint8_t ssid[32];
  struct {
    uint32_t count;
    uint8_t rates[16];
  } rateset;
  uint16_t chanspec;
  uint16_t atim_window;
  uint8_t dtim_period;
  int16_t rssi;
  int8_t phy_noise;
  uint8_t n_cap;
  uint32_t nbss_cap;
  uint8_t ctl_ch;
  uint32_t reserved32[1];
  uint8_t flags;
  uint8_t reserved[3];
  uint8_t basic_mcs[16];
  uint16_t ie_offset;
  uint32_t ie_length;
  uint16_t snr;
};

struct WifiScanResult {
  uint32_t buffer_length;
  uint32_t version;
  uint16_t sync_id;
  uint16_t bss_count;
  WifiBssInfo bss;
};

const uint16_t kWifiCapabilityPrivacy = 0x0010;
const unsigned kWifiScanMs = 4000;

// Adds the access points in one result message, strongest signal kept for
// each name.
unsigned ParseWifiScanResult(const uint8_t *buffer, unsigned length,
                             struct host_wifi_ap *aps, unsigned max,
                             unsigned count) {
  const unsigned minimum_bss_length =
      offsetof(WifiBssInfo, rssi) + sizeof(int16_t);
  if (length < sizeof(WifiScanResult)) {
    return count;
  }
  const WifiScanResult *scan = (const WifiScanResult *)buffer;
  const uint8_t *record = (const uint8_t *)&scan->bss;
  const uint8_t *end = buffer + length;
  for (unsigned index = 0;
       index < scan->bss_count && record + minimum_bss_length <= end;
       index++) {
    const WifiBssInfo *bss = (const WifiBssInfo *)record;
    if (bss->length < minimum_bss_length || record + bss->length > end) {
      break;
    }
    unsigned ssid_length = bss->ssid_length;
    if (ssid_length > sizeof bss->ssid) {
      ssid_length = sizeof bss->ssid;
    }
    if (ssid_length > 0 &&
        ssid_length <= bss->length - offsetof(WifiBssInfo, ssid)) {
      unsigned i;
      for (i = 0; i < count; i++) {
        if (strlen(aps[i].ssid) == ssid_length &&
            memcmp(aps[i].ssid, bss->ssid, ssid_length) == 0) {
          break;
        }
      }
      if (i < count) {
        if (bss->rssi > aps[i].signal) {
          aps[i].signal = bss->rssi;
        }
      } else if (count < max) {
        memcpy(aps[count].ssid, bss->ssid, ssid_length);
        aps[count].ssid[ssid_length] = '\0';
        aps[count].signal = bss->rssi;
        aps[count].secure = (bss->capability & kWifiCapabilityPrivacy) != 0;
        count++;
      }
    }
    record += bss->length;
  }
  return count;
}

unsigned CollectWifiScanResults(struct host_wifi_ap *aps, unsigned max,
                                unsigned count) {
  uint8_t buffer[FRAME_BUFFER_SIZE];
  unsigned length;
  while (s_wlan->ReceiveScanResult(buffer, &length)) {
    count = ParseWifiScanResult(buffer, length, aps, max, count);
  }
  return count;
}

}  // namespace

extern "C" int host_wifi_scan(struct host_wifi_ap *aps, int max) {
  if (s_wlan == 0) {
    return -1;
  }
  uint8_t buffer[FRAME_BUFFER_SIZE];
  unsigned length;
  while (s_wlan->ReceiveScanResult(buffer, &length)) {
  }
  if (!s_wlan->Control("escan 3")) {
    CLogger::Get()->Write(FromNetwork, LogError, "Cannot start Wi-Fi scan");
    return 0;
  }
  unsigned count = 0;
  for (unsigned waited = 0; waited < kWifiScanMs; waited += 100) {
    count = CollectWifiScanResults(aps, (unsigned)max, count);
    host_sleep_us(100000);  // lets Circle's tasks run, and the watchdog see us
  }
  s_wlan->Control("escan 0");
  count = CollectWifiScanResults(aps, (unsigned)max, count);
  CLogger::Get()->Write(FromNetwork, LogNotice,
                        "Wi-Fi scan found %u networks", count);
  return (int)count;
}

//
// Work in the background (host_run_in_background): a task that runs a C
// function once and ends; the scheduler then deletes it.
//

namespace {

class CBackgroundTask : public CTask {
public:
  CBackgroundTask(void (*fn)(void *), void *arg) : mFn(fn), mArg(arg) {}
  void Run(void) override { mFn(mArg); }

private:
  void (*mFn)(void *);
  void *mArg;
};

}  // namespace

extern "C" int host_run_in_background(void (*fn)(void *arg), void *arg) {
  return new CBackgroundTask(fn, arg) != 0;
}

//
// TCP connections for the emulator (host_tcp_*, see host.h): Circle's
// sockets. A received segment is kept here until it has all been read
// (CSocket::Receive() drops what doesn't fit the buffer it is given). All
// on core 1, in the emulator or a background task, which take turns.
//

namespace {

struct TcpConnection {
  CSocket *socket;  // 0 when the slot is free
  bool connecting;  // Connect() is waiting, in a background task
  u8 buffer[FRAME_BUFFER_SIZE];
  unsigned pos, length;
};

const int kMaxTcpConnections = 4;
TcpConnection s_tcp[kMaxTcpConnections];

TcpConnection *TcpSlot(int connection) {
  if (connection < 0 || connection >= kMaxTcpConnections ||
      s_tcp[connection].socket == 0) {
    return 0;
  }
  return &s_tcp[connection];
}

}  // namespace

extern "C" int host_tcp_connect(const char *host, const char *port) {
  if (s_net == 0 || !s_net->IsRunning()) {
    return -1;
  }
  CIPAddress address;
  CDNSClient dns(s_net);
  if (!dns.Resolve(host, &address)) {
    return -2;
  }
  int number = atoi(port);
  if (number <= 0 || number > 65535) {
    return -2;
  }
  int connection = -1;
  for (int i = 0; i < kMaxTcpConnections; i++) {
    if (s_tcp[i].socket == 0) {
      connection = i;
      break;
    }
  }
  if (connection < 0) {
    return -1;
  }
  // The slot is taken before Connect() waits, so that another connection
  // made meanwhile gets another.
  CSocket *socket = new CSocket(s_net, IPPROTO_TCP);
  s_tcp[connection].socket = socket;
  s_tcp[connection].pos = s_tcp[connection].length = 0;
  s_tcp[connection].connecting = true;
  int result = socket->Connect(address, (u16)number);
  s_tcp[connection].connecting = false;
  if (result < 0) {
    s_tcp[connection].socket = 0;
    delete socket;
    return -1;
  }
  return connection;
}

extern "C" int host_tcp_recv(int connection, void *buffer, unsigned size) {
  TcpConnection *c = TcpSlot(connection);
  if (c == 0) {
    return -1;
  }
  if (c->pos >= c->length) {
    int got = c->socket->Receive(c->buffer, sizeof c->buffer, MSG_DONTWAIT);
    if (got < 0) {
      return -1;
    }
    c->pos = 0;
    c->length = (unsigned)got;
  }
  unsigned n = c->length - c->pos;
  if (n > size) {
    n = size;
  }
  memcpy(buffer, c->buffer + c->pos, n);
  c->pos += n;
  return (int)n;
}

extern "C" int host_tcp_send(int connection, const void *buffer, unsigned size) {
  TcpConnection *c = TcpSlot(connection);
  if (c == 0) {
    return -1;
  }
  if (size == 0) {
    return 0;
  }
  int sent = c->socket->Send(buffer, size, MSG_DONTWAIT);
  return sent < 0 ? -1 : sent;
}

extern "C" void host_tcp_close(int connection) {
  TcpConnection *c = TcpSlot(connection);
  if (c != 0) {
    CSocket *socket = c->socket;
    c->socket = 0;
    delete socket;
  }
}

extern "C" void host_tcp_close_all(void) {
  // Deleting a socket only queues its FIN: Circle's network task sends it
  // (and retransmits it if need be), on this core, while we sleep.
  int closed = 0;
  for (int i = 0; i < kMaxTcpConnections; i++) {
    if (s_tcp[i].socket != 0 && !s_tcp[i].connecting) {
      host_tcp_close(i);
      closed++;
    }
  }
  if (closed > 0) {
    CLogger::Get()->Write(FromNetwork, LogNotice, "Closed %d connection(s)", closed);
    host_sleep_us(300000);
  }
}

//
// C interface used by the web UI and the emulator.
//

extern "C" int circle_get_network_ip_address(char *address,
                                             unsigned int address_size) {
  if (address == 0 || address_size == 0 || s_net == 0 || !s_net->IsRunning()) {
    return 0;
  }
  CString formatted;
  s_net->GetConfig()->GetIPAddress()->Format(&formatted);
  if (strcmp((const char *)formatted, "0.0.0.0") == 0) {
    return 0;
  }
  strncpy(address, (const char *)formatted, address_size - 1);
  address[address_size - 1] = '\0';
  return 1;
}

extern "C" int circle_get_network_status(void) {
  char ip[32];
  if (circle_get_network_ip_address(ip, sizeof ip)) {
    return s_device == 1 ? NETWORK_ETHERNET_CONNECTED : NETWORK_WIFI_CONNECTED;
  }
  return s_status;
}

extern "C" const char *circle_get_disk_volume(void) { return "SD"; }
