//
// kernel.cpp
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for the
// Raspberry Pi. See LICENSE for licensing details.
//
// Parts of the USB, frame buffer and sound handling are based on BMC64.
//

#include "kernel.h"
#include "logging.h"
#include "network.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <circle/bcmpropertytags.h>
#include <circle/bcmwatchdog.h>
#include <circle/machineinfo.h>
#include <circle/memio.h>
#if RASPPI >= 5
#include <circle/bcm2712.h>
#endif
#include <circle/startup.h>
#include <circle/synchronize.h>
#include <circle/usb/usbhid.h>
#include <circle_glue.h>

extern "C" {
// From src/port/platform.c: the emulator's entry point, which runs on core
// 1, and its Reboot / Shut Down request.
void bmcoco_main(void);
void bmcoco_request_power(int power_off);
}

static const char FromKernel[] = "bmcoco";

// Sample rate the VideoCore plays audio at.
#define HW_SAMPLE_RATE 44100

// HID usage of the first modifier key (left control); the eight modifier
// bits map to usages 0xE0..0xE7.
#define USAGE_FIRST_MODIFIER 0xE0

// GPIO joystick wiring, the same as BMC64's "GPIO Config 1" (in BMC64's
// code that is config_0_joystickPins; its config_1 is the keyboard PCB).
// Pins are active low with pull ups. Order: up, down, left, right, fire.
static const unsigned kJoyGpio[2][5] = {
    {17, 18, 27, 22, 23}, // Joystick 1 (the CoCo's right port)
    {5, 6, 12, 13, 19},   // Joystick 2 (the CoCo's left port)
};

CKernel *CKernel::s_pThis = 0;

// Core 1 counts up s_Core1Beat while it makes progress and names what it is
// doing in s_Core1Stage, so core 0 can log where it got stuck if it stops.
static volatile unsigned s_Core1Beat = 0;
static const char *volatile s_Core1Stage = "starting";

static inline void Core1Beat(const char *stage) {
  s_Core1Stage = stage;
  s_Core1Beat = s_Core1Beat + 1;
}

#ifdef ARM_ALLOW_MULTI_CORE

//
// CEmulatorCore
//

CEmulatorCore::CEmulatorCore(CMemorySystem *pMemorySystem)
    : CMultiCoreSupport(pMemorySystem), m_bLaunch(false) {}

void CEmulatorCore::Launch(void) {
  DataSyncBarrier();
  m_bLaunch = true;
  DataSyncBarrier();
}

void CEmulatorCore::Run(unsigned nCore) {
  if (nCore == 1) {
    while (!m_bLaunch) {
      DataMemBarrier();
    }
    bmcoco_main();
  }
#if RASPPI >= 4
  // Cores 2 and 3 help draw smoothed and CRT frames (src/pi4/video.h).
  if (nCore >= 2) {
    Pi4Video::WorkerLoop(nCore);
  }
#endif

  // Nothing else runs here.
  for (;;) {
    asm volatile("wfi");
  }
}

#else

//
// CUSBPlugAndPlayTask
//

void CUSBPlugAndPlayTask::Run(void) {
  for (;;) {
    CKernel::Get()->UpdateUSBPlugAndPlay();
    CScheduler::Get()->MsSleep(100);
  }
}

#endif

//
// CKernel
//

CKernel::CKernel(void)
    :
#if RASPPI >= 5
      // Circle's default on the Pi 5 is the 3-pin debug connector, which
      // the Pi 500 lacks; device 0 is GPIO14/15, as on the other boards.
      mSerial(0, FALSE, 0),
#endif
      mLog(&mSerial), mTimer(&mInterrupt), mLogger(mKernelOptions.GetLogLevel(), &mTimer),
#if RASPPI <= 4
      mCPUThrottle(CPUSpeedMaximum),
#endif
      mGPIOManager(&mInterrupt),
#if RASPPI <= 3
      mVCHIQ(CMemorySystem::Get(), &mInterrupt),
#endif
      mEMMC(&mInterrupt, &mTimer, &mActLED),
      mUSBHCI(&mInterrupt, &mTimer, TRUE), mConsole(&mSerial, &mLog),
#ifdef ARM_ALLOW_MULTI_CORE
      mEmulatorCore(CMemorySystem::Get()),
#endif
#if RASPPI <= 3
      mSound(0), mSoundChannels(0), mVolume(VCHIQ_SOUND_VOLUME_DEFAULT),
      mAudioOut(VCHIQSoundDestinationAuto),
#endif
      m_MergedModifiers(0),
      m_KeyQueueHead(0), m_KeyQueueTail(0), m_pMouse(0), m_MouseDx(0),
      m_MouseDy(0), m_MouseButtons(0), m_bGPIOJoysticks(false) {
  s_pThis = this;
  for (unsigned i = 0; i < MAX_KEYBOARDS; i++) {
    m_pKeyboard[i] = 0;
    m_KeyboardIndex[i] = i;
    m_KeyboardModifiers[i] = 0;
    memset(m_KeyboardKeys[i], 0, sizeof m_KeyboardKeys[i]);
  }
  memset(m_KeyDown, 0, sizeof m_KeyDown);
  for (unsigned i = 0; i < HOST_PADS; i++) {
    m_pGamePad[i] = 0;
    m_PadState[i] = -1;
    m_PadAnalog[i][0] = m_PadAnalog[i][1] = -1;
    m_PadAxisX[i] = 0;
    m_PadAxisY[i] = 1;
  }
  memset(m_pJoyPins, 0, sizeof m_pJoyPins);

#if RASPPI <= 3
  // The emulator's display is the lowest dispmanx layer.
  mFrameBuffer.SetLayer(0);
  mFrameBuffer.SetTransparency(false);
#endif
}

CKernel::~CKernel(void) {}

bool CKernel::Initialize(void) {
  if (!mSerial.Initialize(115200)) {
    return false;
  }
  if (!mInterrupt.Initialize()) {
    return false;
  }
  if (!mLogger.Initialize(&mLog)) {
    return false;
  }
  mLogger.RegisterPanicHandler(PanicHandler);
  mLogger.Write(FromKernel, LogNotice, "BMCoco " BMCOCO_VERSION " (" BMCOCO_GIT
                ", XRoar " XROAR_VERSION ")");

  if (!mTimer.Initialize()) {
    return false;
  }
#if RASPPI <= 4
  mLogger.Write(FromKernel, LogNotice, "CPU clock %u MHz (%u to %u MHz)",
                mCPUThrottle.GetClockRate() / 1000000,
                mCPUThrottle.GetMinClockRate() / 1000000,
                mCPUThrottle.GetMaxClockRate() / 1000000);
#endif
  if (!mGPIOManager.Initialize()) {
    return false;
  }
#if RASPPI <= 3
  if (!mVCHIQ.Initialize()) {
    return false;
  }
#endif
  if (!mEMMC.Initialize()) {
    return false;
  }
  if (f_mount(&mFileSystem, "SD:", 1) != FR_OK) {
    mLogger.Write(FromKernel, LogError, "Cannot mount the SD card");
    return false;
  }
  // Settings saved by the BMCoco menu win over cmdline.txt.
  mOptions.LoadSettings("SD:/bmcoco.cfg");

  // Until here the log went to the serial port. No setting means no log.
  const char *logging = mOptions.Get("enable_logging", "off");
  if (strcmp(logging, "file") == 0) {
    SetLogDestination(HOST_LOG_FILE);
  } else if (strcmp(logging, "uart") != 0) {
    SetLogDestination(HOST_LOG_OFF);
  }
  if (!mConsole.Initialize()) {
    return false;
  }
  CGlueStdioInit(mConsole);
  mLog.Flush();

  // Before USB, as BMC64 does: the Pi 3's Ethernet is a USB device and
  // joins the network stack once USB finds it.
  NetworkInitialize(&mOptions);
  mLog.Flush();

  if (!mUSBHCI.Initialize()) {
    return false;
  }
  mLog.Flush();

  const char *audio_out = mOptions.Get("audio_out", "auto");

  // Full scale: the volume (volume= in cmdline.txt and the menu) is
  // XRoar's, so it can change while running.
  const int volume = 100;

#if RASPPI >= 4
  // Display and sound are set up here, on core 0: Circle's sound drivers
  // must be started on core 0 (see src/pi4/sound.h).
  Pi4Sound::Output sound_output = Pi4Sound::Output::Auto;
  if (strcmp(audio_out, "hdmi") == 0) {
    sound_output = Pi4Sound::Output::Hdmi;
  } else if (strcmp(audio_out, "analog") == 0 || strcmp(audio_out, "headphones") == 0) {
    sound_output = Pi4Sound::Output::Headphones;
  }
  // video_driver: kms (the default), hvs or firmware (docs/PI4.md).
  const char *video_driver = mOptions.Get("video_driver", "kms");
  Pi4Video::Driver driver = Pi4Video::Driver::Kms;
  if (strcmp(video_driver, "hvs") == 0) {
    driver = Pi4Video::Driver::Hvs;
  } else if (strcmp(video_driver, "firmware") == 0) {
    driver = Pi4Video::Driver::Firmware;
  }
  if (!mVideo.Initialize(mOptions.GetBool("crt_shader", false),
                         mOptions.GetBool("smooth", false),
                         mOptions.GetBool("crt_curvature", false), driver)) {
    return false;
  }
  mLog.Flush();
  // Without sound the emulator still runs.
  mPi4Sound.Initialize(&mInterrupt, volume, sound_output);
  mLog.Flush();
#else
  if (strcmp(audio_out, "hdmi") == 0) {
    mAudioOut = VCHIQSoundDestinationHDMI;
  } else if (strcmp(audio_out, "analog") == 0 ||
             strcmp(audio_out, "headphones") == 0) {
    mAudioOut = VCHIQSoundDestinationHeadphones;
  }
  // VCHIQ volume is in 1/100 dB: 0 dB.
  (void)volume;
  mVolume = 0;

  FrameBufferLayer::Initialize();
#endif

  if (mOptions.GetBool("gpio_joysticks", false)) {
    SetupGPIOJoysticks();
  }

#ifdef ARM_ALLOW_MULTI_CORE
  bool ok = mEmulatorCore.Initialize();
#else
  bool ok = true;
#endif
  mLog.Flush();
  return ok;
}

void CKernel::UpdateUSBPlugAndPlay(void) {
  if (mUSBHCI.UpdatePlugAndPlay()) {
    SetupUSBKeyboards();
    SetupUSBGamePads();
    SetupUSBMouse();
  }
}

TShutdownMode CKernel::Run(void) {
  SetupUSBKeyboards();
  SetupUSBGamePads();
  SetupUSBMouse();
  mLog.Flush();
  mStarting = false;

#ifndef ARM_ALLOW_MULTI_CORE
  // One core (the Pi Zero): the emulator runs here and yields once a frame,
  // which lets the USB task and Circle's other tasks run.
  new CUSBPlugAndPlayTask;
  bmcoco_main();
  return ShutdownHalt;
#else
  mEmulatorCore.Launch();

#if RASPPI >= 4
  // The 3D GPU draws the smooth and CRT pictures instead of the ARM (unless
  // crt_gpu=0 in cmdline.txt; crt_gpu_test=1 also checks its pictures
  // against the ARM's). Started here, with the emulator running on core 1,
  // which also writes the log.
  if (mOptions.GetBool("crt_gpu", true) && mGpu.Initialize() && mGpu.ClearTest() &&
      mRenderer.Initialize(&mGpu, 4096, 2160) &&
      mCrt.Initialize(&mGpu, &mRenderer, 384, 288)) {
    bool ok = true;
    if (mOptions.GetBool("crt_gpu_test", false)) {
      ok = mRenderer.CopyTest() && mCrt.Test();
    }
    if (ok) {
      mVideo.UseGpu(&mCrt);
    }
  }
#endif

  // Core 0 only looks after USB devices coming and going, and watches that
  // the emulator on core 1 keeps going. Interrupts keep being handled here.
  unsigned last_beat = 0;
  unsigned last_change = CTimer::GetClockTicks();
  bool stalled = false;
  for (;;) {
    UpdateUSBPlugAndPlay();

    unsigned beat = s_Core1Beat;
    unsigned now = CTimer::GetClockTicks();
    if (beat != last_beat) {
      if (stalled) {
        mLogger.Write(FromKernel, LogNotice, "Emulator running again after %u ms",
                      (now - last_change) / (CLOCKHZ / 1000));
        stalled = false;
      }
      last_beat = beat;
      last_change = now;
    } else if (beat != 0 && !stalled && now - last_change > 3 * CLOCKHZ) {
      mLogger.Write(FromKernel, LogWarning,
                    "Emulator has not moved for 3 s; last seen: %s",
                    (const char *)s_Core1Stage);
      stalled = true;
      // Core 1 writes the log file; while it is stuck (other than in the
      // file system), core 0 writes it, so the log shows where.
      if (strcmp((const char *)s_Core1Stage, "writing the log file") != 0) {
        mLog.Flush();
      }
    }
#if RASPPI >= 5
    CheckPowerButton();
#endif

    asm volatile("wfi");
  }

  return ShutdownHalt;
#endif
}

#if RASPPI >= 5
// The Pi 5's power button, and the Pi 500's power key, as Shut Down: GPIO
// 20 of the BCM2712's own GPIO block ("gio", Circle's ARM_GPIO1_BASE),
// low while pressed, held for 50 ms (the device trees' debounce time). It
// counts only once it has been seen released.
// Sources: Raspberry Pi Linux device trees, bcm2712-rpi-5-b.dts
// (pwr_button: gpio-keys, KEY_POWER, <&gio 20 GPIO_ACTIVE_LOW>,
// debounce-interval 50, pull-up) and bcm2712-rpi-500.dts (the same key);
// bcm2712.dtsi (gio at 0x7D508500); Circle, include/circle/bcm2712.h
// (ARM_GPIO1_DATA0, the first bank's levels).
void CKernel::CheckPowerButton(void) {
  const u32 Button = 1u << 20;
  bool down = (read32(ARM_GPIO1_DATA0) & Button) == 0;
  unsigned now = CTimer::GetClockTicks();
  if (!down) {
    m_bPowerButtonSeenUp = true;
    m_nPowerButtonDown = 0;
    return;
  }
  if (!m_bPowerButtonSeenUp) {
    return;
  }
  if (m_nPowerButtonDown == 0) {
    m_nPowerButtonDown = now | 1;
  } else if (now - m_nPowerButtonDown >= CLOCKHZ / 20) {
    mLogger.Write(FromKernel, LogNotice, "Power button: Shut Down");
    bmcoco_request_power(1);
    m_bPowerButtonSeenUp = false;
  }
}
#endif

//
// USB keyboards
//

void CKernel::SetupUSBKeyboards(void) {
  for (unsigned i = 0; i < MAX_KEYBOARDS; i++) {
    if (m_pKeyboard[i] != 0) {
      continue;
    }
    char name[16];
    snprintf(name, sizeof name, "ukbd%u", i + 1);
    m_pKeyboard[i] =
        (CUSBKeyboardDevice *)mDeviceNameService.GetDevice(name, FALSE);
    if (m_pKeyboard[i] != 0) {
      m_pKeyboard[i]->RegisterRemovedHandler(KeyboardRemovedHandler,
                                             &m_KeyboardIndex[i]);
      m_pKeyboard[i]->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw, FALSE,
                                                  &m_KeyboardIndex[i]);
      mLogger.Write(FromKernel, LogNotice, "Keyboard %u connected", i + 1);
    }
  }
}

void CKernel::KeyboardRemovedHandler(CDevice *pDevice, void *pContext) {
  CKernel *k = s_pThis;
  unsigned i = *(unsigned *)pContext;
  if (k == 0 || i >= MAX_KEYBOARDS) {
    return;
  }
  // Release everything that keyboard was holding down.
  static const unsigned char no_keys[6] = {0, 0, 0, 0, 0, 0};
  KeyStatusHandlerRaw(0, no_keys, pContext);
  k->m_pKeyboard[i] = 0;
}

void CKernel::PushKeyEvent(int usage, int pressed) {
  unsigned head = m_KeyQueueHead;
  unsigned next = (head + 1) % KEY_QUEUE_SIZE;
  if (next == m_KeyQueueTail) {
    return; // Full: the emulator is not reading. Drop the event.
  }
  m_KeyQueue[head] = (unsigned short)(usage | (pressed ? 0x100 : 0));
  DataMemBarrier();
  m_KeyQueueHead = next;
}

// Recomputes which keys are down across all keyboards and queues an event for
// every key whose state changed.
void CKernel::UpdateMergedKeys(void) {
  unsigned char down[256];
  unsigned char modifiers = 0;
  memset(down, 0, sizeof down);
  for (unsigned i = 0; i < MAX_KEYBOARDS; i++) {
    modifiers |= m_KeyboardModifiers[i];
    for (unsigned j = 0; j < 6; j++) {
      unsigned char key = m_KeyboardKeys[i][j];
      if (key > 3) { // 0 none, 1..3 are error codes
        down[key] = 1;
      }
    }
  }
  for (unsigned bit = 0; bit < 8; bit++) {
    down[USAGE_FIRST_MODIFIER + bit] = (modifiers >> bit) & 1;
  }

  for (unsigned usage = 4; usage < 256; usage++) {
    if (down[usage] != m_KeyDown[usage]) {
      m_KeyDown[usage] = down[usage];
      PushKeyEvent(usage, down[usage]);
    }
  }
  m_MergedModifiers = modifiers;
}

// USB interrupt context, core 0.
void CKernel::KeyStatusHandlerRaw(unsigned char ucModifiers,
                                  const unsigned char RawKeys[6], void *pArg) {
  CKernel *k = s_pThis;
  unsigned i = *(unsigned *)pArg;
  if (k == 0 || i >= MAX_KEYBOARDS) {
    return;
  }
  // Too many keys at once: the report carries no key information.
  for (unsigned j = 0; j < 6; j++) {
    if (RawKeys[j] == 0x01) {
      return;
    }
  }
  k->m_KeyboardModifiers[i] = ucModifiers;
  memcpy(k->m_KeyboardKeys[i], RawKeys, 6);
  k->UpdateMergedKeys();
}

int CKernel::KeyEvent(int *usage, int *pressed) {
  Core1Beat("reading keys");
  unsigned tail = m_KeyQueueTail;
  if (tail == m_KeyQueueHead) {
    return 0;
  }
  DataMemBarrier();
  unsigned short event = m_KeyQueue[tail];
  m_KeyQueueTail = (tail + 1) % KEY_QUEUE_SIZE;
  *usage = event & 0xff;
  *pressed = (event & 0x100) != 0;
  return 1;
}

int CKernel::KeyDown(int usage) {
  return usage >= 0 && usage < 256 ? m_KeyDown[usage] : 0;
}

unsigned CKernel::KeyModifiers(void) { return m_MergedModifiers; }

//
// USB gamepads
//

// Pads Circle doesn't know whose direction is not on axes 0 and 1, by USB
// vendor and product ID (as BMC64's usb_gamepad_defaults.c).
static const struct {
  const char *vendor;
  int axis_x;
  int axis_y;
} kPadProfiles[] = {
    {"venca3-24", 3, 4}, // 8BitDo M30 2.4G
};

void CKernel::SetupUSBGamePads(void) {
  for (unsigned i = 0; i < HOST_PADS; i++) {
    if (m_pGamePad[i] != 0) {
      continue;
    }
    char name[16];
    snprintf(name, sizeof name, "upad%u", i + 1);
    m_pGamePad[i] =
        (CUSBGamePadDevice *)mDeviceNameService.GetDevice(name, FALSE);
    if (m_pGamePad[i] != 0) {
      m_pGamePad[i]->RegisterRemovedHandler(GamePadRemovedHandler);
      m_pGamePad[i]->RegisterStatusHandler(GamePadStatusHandler);
      m_PadState[i] = 0;
      m_PadAxisX[i] = 0;
      m_PadAxisY[i] = 1;
      CString *vendor = m_pGamePad[i]->GetDevice()->GetName(DeviceNameVendor);
      for (unsigned p = 0; vendor != 0 && p < sizeof kPadProfiles /
                                                  sizeof kPadProfiles[0]; p++) {
        if (strcmp(*vendor, kPadProfiles[p].vendor) == 0) {
          m_PadAxisX[i] = kPadProfiles[p].axis_x;
          m_PadAxisY[i] = kPadProfiles[p].axis_y;
        }
      }
      const char *product = m_pGamePad[i]->GetProperty(CDevice::PropertyProduct);
      if (product == 0) {
        product = "unknown";
      }
      mLogger.Write(FromKernel, LogNotice,
                    "Gamepad %u connected: %s (%s)%s, axes %d/%d", i + 1,
                    product, vendor != 0 ? (const char *)*vendor : "?",
                    (m_pGamePad[i]->GetProperties() & GamePadPropertyIsKnown)
                        ? "" : " (generic)",
                    m_PadAxisX[i], m_PadAxisY[i]);
      delete vendor;
    }
  }
}

void CKernel::GamePadRemovedHandler(CDevice *pDevice, void *pContext) {
  CKernel *k = s_pThis;
  if (k == 0) {
    return;
  }
  for (unsigned i = 0; i < HOST_PADS; i++) {
    if (k->m_pGamePad[i] == pDevice) {
      k->m_pGamePad[i] = 0;
      k->m_PadState[i] = -1;
      k->m_PadAnalog[i][0] = k->m_PadAnalog[i][1] = -1;
    }
  }
}

// Direction of an axis: -1, 0 or 1, with a dead zone of half the range.
static int AxisDirection(const TGamePadState *pState, int axis) {
  if (axis >= pState->naxes) {
    return 0;
  }
  int min = pState->axes[axis].minimum;
  int max = pState->axes[axis].maximum;
  if (max <= min) {
    return 0;
  }
  int centre = (min + max) / 2;
  int threshold = (max - min) / 4;
  int value = pState->axes[axis].value;
  if (value < centre - threshold) {
    return -1;
  }
  if (value > centre + threshold) {
    return 1;
  }
  return 0;
}

// Position of an axis scaled to 0..65535, or -1 if there is no such axis.
static int AxisPosition(const TGamePadState *pState, int axis) {
  if (axis >= pState->naxes) {
    return -1;
  }
  int min = pState->axes[axis].minimum;
  int max = pState->axes[axis].maximum;
  if (max <= min) {
    return -1;
  }
  int value = pState->axes[axis].value;
  if (value < min) {
    value = min;
  } else if (value > max) {
    value = max;
  }
  return (int)(((long long)(value - min) * 65535) / (max - min));
}

// USB interrupt context, core 0.
void CKernel::GamePadStatusHandler(unsigned nDeviceIndex,
                                   const TGamePadState *pState) {
  CKernel *k = s_pThis;
  if (k == 0 || nDeviceIndex >= HOST_PADS || k->m_pGamePad[nDeviceIndex] == 0) {
    return;
  }

  int state = 0;
  unsigned b = pState->buttons;
  bool known = (k->m_pGamePad[nDeviceIndex]->GetProperties() &
                GamePadPropertyIsKnown) != 0;

  if (known) {
    if (b & GamePadButtonA) state |= HOST_PAD_A;
    if (b & GamePadButtonB) state |= HOST_PAD_B;
    if (b & GamePadButtonX) state |= HOST_PAD_X;
    if (b & GamePadButtonY) state |= HOST_PAD_Y;
    if (b & (GamePadButtonLB | GamePadButtonLT)) state |= HOST_PAD_L;
    if (b & (GamePadButtonRB | GamePadButtonRT)) state |= HOST_PAD_R;
    if (b & (GamePadButtonSelect | GamePadButtonMinus)) state |= HOST_PAD_SELECT;
    if (b & (GamePadButtonStart | GamePadButtonPlus)) state |= HOST_PAD_START;
    if (b & GamePadButtonGuide) state |= HOST_PAD_HOME;
    if (b & GamePadButtonUp) state |= HOST_PAD_UP;
    if (b & GamePadButtonDown) state |= HOST_PAD_DOWN;
    if (b & GamePadButtonLeft) state |= HOST_PAD_LEFT;
    if (b & GamePadButtonRight) state |= HOST_PAD_RIGHT;
  } else {
    // Unknown pads report buttons in their own order. This layout suits most
    // cheap USB pads: 0..3 face buttons, 4/5 shoulders, 8 select, 9 start.
    if (b & (1u << 0)) state |= HOST_PAD_A;
    if (b & (1u << 1)) state |= HOST_PAD_B;
    if (b & (1u << 2)) state |= HOST_PAD_X;
    if (b & (1u << 3)) state |= HOST_PAD_Y;
    if (b & ((1u << 4) | (1u << 6))) state |= HOST_PAD_L;
    if (b & ((1u << 5) | (1u << 7))) state |= HOST_PAD_R;
    if (b & (1u << 8)) state |= HOST_PAD_SELECT;
    if (b & (1u << 9)) state |= HOST_PAD_START;
    if (b & (1u << 12)) state |= HOST_PAD_HOME;
  }

  // Hats: 0 is up, counting clockwise in 45 degree steps; anything else
  // is centred.
  if (pState->nhats > 0) {
    switch (pState->hats[0]) {
    case 0: state |= HOST_PAD_UP; break;
    case 1: state |= HOST_PAD_UP | HOST_PAD_RIGHT; break;
    case 2: state |= HOST_PAD_RIGHT; break;
    case 3: state |= HOST_PAD_DOWN | HOST_PAD_RIGHT; break;
    case 4: state |= HOST_PAD_DOWN; break;
    case 5: state |= HOST_PAD_DOWN | HOST_PAD_LEFT; break;
    case 6: state |= HOST_PAD_LEFT; break;
    case 7: state |= HOST_PAD_UP | HOST_PAD_LEFT; break;
    default: break;
    }
  }

  // The left stick (axes 0 and 1 on most unknown pads), or the D-pad on
  // pads that report it as axes (kPadProfiles).
  int x = AxisDirection(pState, known ? (int)GamePadAxisLeftX
                                      : k->m_PadAxisX[nDeviceIndex]);
  int y = AxisDirection(pState, known ? (int)GamePadAxisLeftY
                                      : k->m_PadAxisY[nDeviceIndex]);
  if (x < 0) state |= HOST_PAD_LEFT;
  if (x > 0) state |= HOST_PAD_RIGHT;
  if (y < 0) state |= HOST_PAD_UP;
  if (y > 0) state |= HOST_PAD_DOWN;

  // The same stick as a position, for the CoCo's analog joysticks. Pads
  // whose D-pad is reported as axes (kPadProfiles) have no stick.
  int ax = -1;
  int ay = -1;
  if (known) {
    ax = AxisPosition(pState, GamePadAxisLeftX);
    ay = AxisPosition(pState, GamePadAxisLeftY);
  } else if (k->m_PadAxisX[nDeviceIndex] == 0) {
    ax = AxisPosition(pState, 0);
    ay = AxisPosition(pState, 1);
  }
  if (ax < 0 || ay < 0) {
    ax = ay = -1;
  }
  k->m_PadAnalog[nDeviceIndex][0] = ax;
  k->m_PadAnalog[nDeviceIndex][1] = ay;
  k->m_PadState[nDeviceIndex] = state;
}

int CKernel::PadState(int pad) {
  if (pad < 0 || pad >= HOST_PADS) {
    return -1;
  }
  return m_PadState[pad];
}

int CKernel::PadAxis(int pad, int axis) {
  if (pad < 0 || pad >= HOST_PADS || axis < 0 || axis > 1 ||
      m_PadState[pad] < 0) {
    return -1;
  }
  return m_PadAnalog[pad][axis];
}

//
// USB mouse
//

void CKernel::SetupUSBMouse(void) {
  if (m_pMouse != 0) {
    return;
  }
  m_pMouse = (CMouseDevice *)mDeviceNameService.GetDevice("mouse1", FALSE);
  if (m_pMouse != 0) {
    m_pMouse->RegisterRemovedHandler(MouseRemovedHandler);
    m_pMouse->RegisterStatusHandler(MouseStatusHandler);
    mLogger.Write(FromKernel, LogNotice, "Mouse connected");
  }
}

void CKernel::MouseStatusHandler(unsigned nButtons, int nDisplacementX,
                                 int nDisplacementY, int nWheelMove) {
  CKernel *k = s_pThis;
  if (k == 0) {
    return;
  }
  __atomic_add_fetch(&k->m_MouseDx, nDisplacementX, __ATOMIC_RELAXED);
  __atomic_add_fetch(&k->m_MouseDy, nDisplacementY, __ATOMIC_RELAXED);
  __atomic_store_n(&k->m_MouseButtons, (int)(nButtons & 7), __ATOMIC_RELEASE);
}

void CKernel::MouseRemovedHandler(CDevice *pDevice, void *pContext) {
  CKernel *k = s_pThis;
  if (k == 0) {
    return;
  }
  k->m_pMouse = 0;
  k->m_MouseButtons = 0;
}

int CKernel::MouseRead(int *dx, int *dy, int *buttons) {
  *dx = __atomic_exchange_n(&m_MouseDx, 0, __ATOMIC_RELAXED);
  *dy = __atomic_exchange_n(&m_MouseDy, 0, __ATOMIC_RELAXED);
  *buttons = __atomic_load_n(&m_MouseButtons, __ATOMIC_ACQUIRE);
  return m_pMouse != 0;
}

//
// GPIO joysticks
//

void CKernel::SetupGPIOJoysticks(void) {
  for (unsigned port = 0; port < 2; port++) {
    for (unsigned i = 0; i < 5; i++) {
      m_pJoyPins[port][i] = new CGPIOPin(kJoyGpio[port][i],
                                         GPIOModeInputPullUp, &mGPIOManager);
    }
  }
  m_bGPIOJoysticks = true;
  mLogger.Write(FromKernel, LogNotice, "GPIO joysticks enabled");
}

int CKernel::GpioJoystick(int port) {
  if (!m_bGPIOJoysticks || port < 0 || port > 1) {
    return 0;
  }
  static const int bits[5] = {HOST_JOY_UP, HOST_JOY_DOWN, HOST_JOY_LEFT,
                              HOST_JOY_RIGHT, HOST_JOY_FIRE};
  int state = 0;
  for (unsigned i = 0; i < 5; i++) {
    if (m_pJoyPins[port][i]->Read() == LOW) {
      state |= bits[i];
    }
  }
  return state;
}

//
// Video
//

#if RASPPI >= 4

int CKernel::VideoInit(int width, int height, uint16_t **pixels, int *pitch) {
  return mVideo.Allocate(width, height, pixels, pitch);
}

void CKernel::VideoSetView(int x, int y, int w, int h, double aspect) {
  mVideo.SetView(x, y, w, h, aspect, mOptions.GetDouble("scale", 1.0));
}

void CKernel::DisplaySize(int *width, int *height) {
  mVideo.DisplaySize(width, height);
}

bool CKernel::DisplayMatchRate(double fps) {
  Core1Beat("setting the video mode");
  // The Pi 5's firmware ignores hdmi_mode: the size is chosen here.
  unsigned lines = 0;
  const char *resolution = mOptions.Get("resolution", "auto");
  if (strcmp(resolution, "1920x1080") == 0) {
    lines = 1080;
  } else if (strcmp(resolution, "1280x720") == 0) {
    lines = 720;
  }
  bool changed = mVideo.MatchRate(fps, Pi4Sound::SAMPLE_RATE, lines);
  Core1Beat("emulating");
  return changed;
}

bool CKernel::DisplayResolutionChoice(void) { return mVideo.ResolutionChoice(); }

void CKernel::VideoPresent(int wait_vsync) {
  // Let Circle's tasks (the network, the web UI) run once per frame.
  Core1Beat("running Circle tasks");
  CScheduler::Get()->Yield();
  Core1Beat("writing the log file");
  mLog.Drain();
  Core1Beat("showing a frame (framebuffer)");
  mVideo.Present(wait_vsync != 0);
  Core1Beat("emulating");
}

//
// Sound
//

unsigned CKernel::SoundOpen(unsigned freq, int channels,
                            unsigned latency_frames) {
  (void)freq; // HDMI runs at Pi4Sound::SAMPLE_RATE.
  return mPi4Sound.Open(channels, latency_frames);
}

void CKernel::SoundClose(void) {
  // HDMI sound keeps running (silent when nothing is written); it can only
  // be started on core 0.
}

unsigned CKernel::SoundFreeFrames(void) { return mPi4Sound.FreeFrames(); }

void CKernel::SoundWrite(const int16_t *samples, unsigned frames) {
  Core1Beat("writing sound (HDMI)");
  mPi4Sound.Write(samples, frames);
  Core1Beat("emulating");
}

#else

int CKernel::VideoInit(int width, int height, uint16_t **pixels, int *pitch) {
  if (mOptions.GetBool("crt_shader", false)) {
    mFrameBuffer.SetUsesShader(true);
  }
  // The firmware's own scaling kernel is soft, so sharp pixels need the
  // nearest neighbour kernel set explicitly (as BMC64 does).
  FrameBufferLayer::SetInterpolation(mOptions.GetBool("smooth", false) ? 1 : 0);
  int ret = mFrameBuffer.Allocate(1 /* RGB565 */, (uint8_t **)pixels, width,
                                  height, pitch);
  if (ret != 0) {
    mLogger.Write(FromKernel, LogError, "Cannot allocate frame buffer");
    return ret;
  }
  if (mFrameBuffer.UsesShader()) {
    // BMC64's default CRT look: scanlines and fake gamma, optional curvature.
    mFrameBuffer.SetShaderParams(
        mOptions.GetBool("crt_curvature", false), 0.10f, 0.15f, 0 /* mask */,
        0.70f, true /* gamma */, true /* fake gamma */, true /* scanlines */,
        true /* multisample */, 6.0f, 0.12f, 1.5f, 2.4f, 2.2f,
        false /* sharper */, mOptions.GetBool("smooth", false));
  }
  return 0;
}

void CKernel::VideoSetView(int x, int y, int w, int h, double aspect) {
  bool showing = mFrameBuffer.Showing();
  if (showing) {
    mFrameBuffer.Hide();
  }
  mFrameBuffer.SetSrcRect(x, y, w, h);
  // Fill the display's height (times "scale"), with the width following
  // the aspect ratio.
  double scale = mOptions.GetDouble("scale", 1.0);
  if (scale <= 0.1 || scale > 1.0) {
    scale = 1.0;
  }
  mFrameBuffer.SetStretch(aspect, scale, 0, 0, 0, 0);
  mFrameBuffer.Show();
}

void CKernel::DisplaySize(int *width, int *height) {
  int fb_w, fb_h, src_w, src_h, dst_w, dst_h;
  mFrameBuffer.GetDimensions(width, height, &fb_w, &fb_h, &src_w, &src_h,
                             &dst_w, &dst_h);
}

bool CKernel::DisplayMatchRate(double fps) {
  (void)fps; // The mode is the firmware's (config.txt).
  return false;
}

bool CKernel::DisplayResolutionChoice(void) { return false; }

void CKernel::VideoPresent(int wait_vsync) {
  // Let Circle's tasks (VCHIQ in particular) run once per frame.
  Core1Beat("running Circle tasks");
  CScheduler::Get()->Yield();
  Core1Beat("writing the log file");
  mLog.Drain();
  Core1Beat("showing a frame (dispmanx)");
  mFrameBuffer.FrameReady(wait_vsync ? 1 : 0);
  FrameBufferLayer::SwapResources(wait_vsync != 0, &mFrameBuffer, nullptr);
  Core1Beat("emulating");
}

//
// Sound
//

unsigned CKernel::SoundOpen(unsigned freq, int channels,
                            unsigned latency_frames) {
  (void)freq; // The VideoCore always runs at HW_SAMPLE_RATE.
  if (mSound == 0) {
    mSound = new VchiqSound(&mVCHIQ, HW_SAMPLE_RATE, mAudioOut);
  } else if (mSound->PlaybackActive()) {
    if (channels == mSoundChannels) {
      return HW_SAMPLE_RATE;
    }
    mSound->CancelPlayback();
  }
  mSoundChannels = channels;
  if (!mSound->Playback(mVolume, channels, latency_frames)) {
    mLogger.Write(FromKernel, LogError, "Cannot start sound");
    return 0;
  }
  return HW_SAMPLE_RATE;
}

void CKernel::SoundClose(void) {
  // The VCHIQ audio service is left open; reopening it is slow.
}

unsigned CKernel::SoundFreeFrames(void) {
  return mSound != 0 && mSound->PlaybackActive() ? mSound->FreeFrames() : 0;
}

void CKernel::SoundWrite(const int16_t *samples, unsigned frames) {
  if (mSound != 0 && mSound->PlaybackActive()) {
    Core1Beat("writing sound (VCHIQ)");
    mSound->WriteFrames(samples, frames);
    Core1Beat("emulating");
  }
}

#endif

//
// System
//

double CKernel::Time(void) {
  return (double)CTimer::GetClockTicks64() / CLOCKHZ;
}

void CKernel::Reboot(void) {
  mLogger.Write(FromKernel, LogNotice, "Rebooting");
  mLog.Close();
  reboot();
}

void CKernel::PowerOff(void) {
#if RASPPI >= 4
  mLogger.Write(FromKernel, LogNotice, "Powering off");
  mLog.Close();
  mVideo.Blank();
  CTimer::SimpleMsDelay(100);   // the log out of the UART
  // First tell the firmware, as Linux does before every reboot, halt or
  // power off: the mailbox tag RPI_FIRMWARE_NOTIFY_REBOOT, 0x00030048, no
  // data (drivers/firmware/raspberrypi.c, rpi_firmware_notify_reboot and
  // rpi_firmware_shutdown; include/soc/bcm2835/raspberrypi-firmware.h).
  // Circle's GetTag() needs room for at least one value, so the simple tag.
  {
    CBcmPropertyTags tags;
    TPropertyTagSimple tag;
    tags.GetTag(0x00030048, &tag, sizeof tag, 0);
  }
  // Then a restart into "partition 63", which the firmware takes as a halt:
  // the same as Linux's power off on the Raspberry Pi (bcm2835_wdt.c,
  // bcm2835_power_off). The Pi 4 B's bootloader then halts, waking on
  // GPIO3 (WAKE_ON_GPIO=1, its default; with 0 and POWER_OFF_ON_HALT=1 it
  // switches its outputs off); the Pi 400's switches off, and its power key
  // turns it back on (rpi-eeprom firmware-2711/release-notes.md). The Pi 5,
  // 500 and 500+ halt until their power button or key is pressed (Raspberry
  // Pi documentation, "Raspberry Pi 5 power button"). Circle:
  // CBcmWatchdog.
  CBcmWatchdog watchdog;
  watchdog.Restart(CBcmWatchdog::PartitionHalt);
#else
  mLogger.Write(FromKernel, LogNotice, "Halting; safe to switch off");
  mLog.Close();
  // Blank the picture so it is clear the Pi has stopped.
  mFrameBuffer.Hide();
  halt();
#endif
}

// A crash during start up: the log so far, with the crash report, goes to
// the card before the Pi halts.
void CKernel::PanicHandler(void) { FlushStartupLog(); }

void FlushStartupLog(void) {
  CKernel *kernel = CKernel::Get();
  if (kernel != 0 && kernel->mStarting) {
    kernel->mLog.Flush();
  }
}

bool CKernel::SetLogDestination(int destination) {
  if (!mLog.SetDestination(destination)) {
    mSerial.Write("Cannot open " LOG_FILE "\n", sizeof("Cannot open " LOG_FILE "\n") - 1);
    return false;
  }
  if (destination == HOST_LOG_FILE) {
    // The file starts with what was logged since boot.
    char text[512];
    int len;
    while ((len = mLogger.Read(text, sizeof text)) > 0) {
      mLog.WriteFile(text, len);
    }
  }
  return true;
}

//
// host.h: C entry points for the emulator.
//

extern "C" {

const char *host_option(const char *name, const char *default_value) {
  Options *o = Options::Get();
  return o ? o->Get(name, default_value) : default_value;
}

int host_option_int(const char *name, int default_value) {
  Options *o = Options::Get();
  return o ? o->GetInt(name, default_value) : default_value;
}

double host_option_double(const char *name, double default_value) {
  Options *o = Options::Get();
  return o ? o->GetDouble(name, default_value) : default_value;
}

int host_option_set(const char *name, const char *value) {
  Options *o = Options::Get();
  return o && o->Set(name, value);
}

int host_options_save(void) {
  Options *o = Options::Get();
  return o && o->SaveSettings();
}

const char *host_model_name(void) {
  return CMachineInfo::Get()->GetMachineName();
}

void host_display_size(int *width, int *height) {
  CKernel::Get()->DisplaySize(width, height);
}

int host_display_match_rate(double fps) {
  return CKernel::Get()->DisplayMatchRate(fps) ? 1 : 0;
}

int host_display_resolution_choice(void) {
  return CKernel::Get()->DisplayResolutionChoice() ? 1 : 0;
}

void host_network_status(char *text, unsigned size) {
  static const char *const kStatus[] = {
      "Off",
      "Ethernet starting",
      "Ethernet: waiting for address",
      "Ethernet connected",
      "Ethernet not found",
      "Ethernet failed",
      "Wi-Fi: no network chosen",
      "Wi-Fi starting",
      "Wi-Fi failed",
      "Wi-Fi failed",
      "Wi-Fi starting",
      "Wi-Fi failed",
      "Wi-Fi connecting",
      "Wi-Fi connected",
      "Wi-Fi: no connection",
      "Wi-Fi not found",
      "Wi-Fi not started",
      "Wi-Fi not supported",
      "Wi-Fi: firmware missing",
  };
  char ip[32];
  int status = circle_get_network_status();
  if (circle_get_network_ip_address(ip, sizeof ip)) {
    snprintf(text, size, "%s %s",
             status == NETWORK_ETHERNET_CONNECTED ? "Ethernet" : "Wi-Fi", ip);
  } else if (status >= 0 &&
             status < (int)(sizeof kStatus / sizeof kStatus[0])) {
    snprintf(text, size, "%s", kStatus[status]);
  } else {
    snprintf(text, size, "Unknown");
  }
}

double host_time(void) { return CKernel::Get()->Time(); }

void host_sleep_us(unsigned us) {
  // Sleep in small steps so Circle's tasks keep running.
  double end = host_time() + us / 1000000.0;
  for (;;) {
    Core1Beat("sleeping");
    CScheduler::Get()->Yield();
    CKernel::Get()->DrainLog();
    double left = end - host_time();
    if (left <= 0) {
      break;
    }
    unsigned step = left > 0.001 ? 1000 : (unsigned)(left * 1000000.0);
    if (step > 0) {
      CTimer::SimpleusDelay(step);
    }
  }
}

void host_yield(void) { CScheduler::Get()->Yield(); }

int host_video_init(int width, int height, uint16_t **pixels, int *pitch) {
  return CKernel::Get()->VideoInit(width, height, pixels, pitch);
}

void host_video_set_view(int x, int y, int w, int h, double aspect) {
  CKernel::Get()->VideoSetView(x, y, w, h, aspect);
}

void host_video_present(int wait_vsync) {
  CKernel::Get()->VideoPresent(wait_vsync);
}

unsigned host_sound_open(unsigned freq, int channels, unsigned latency_frames) {
  return CKernel::Get()->SoundOpen(freq, channels, latency_frames);
}

void host_sound_close(void) { CKernel::Get()->SoundClose(); }

unsigned host_sound_free_frames(void) {
  return CKernel::Get()->SoundFreeFrames();
}

void host_sound_write(const int16_t *samples, unsigned frames) {
  CKernel::Get()->SoundWrite(samples, frames);
}

int host_key_event(int *usage, int *pressed) {
  return CKernel::Get()->KeyEvent(usage, pressed);
}

int host_key_down(int usage) { return CKernel::Get()->KeyDown(usage); }

unsigned host_key_modifiers(void) { return CKernel::Get()->KeyModifiers(); }

int host_pad_state(int pad) { return CKernel::Get()->PadState(pad); }

int host_pad_axis(int pad, int axis) { return CKernel::Get()->PadAxis(pad, axis); }

int host_mouse_read(int *dx, int *dy, int *buttons) {
  return CKernel::Get()->MouseRead(dx, dy, buttons);
}

int host_gpio_joystick(int port) { return CKernel::Get()->GpioJoystick(port); }

void host_reboot(void) { CKernel::Get()->Reboot(); }

void host_power_off(void) { CKernel::Get()->PowerOff(); }

extern int circle_get_network_ip_address(char *address, unsigned int size);

int host_network_address(char *address, unsigned size) {
  return circle_get_network_ip_address(address, size);
}

void host_log(const char *message) {
  CLogger::Get()->Write(FromKernel, LogNotice, "%s", message);
}

int host_log_destination(void) { return CKernel::Get()->GetLogDestination(); }

int host_set_log_destination(int destination) {
  return CKernel::Get()->SetLogDestination(destination);
}

} // extern "C"
