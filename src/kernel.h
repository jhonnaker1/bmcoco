//
// kernel.h
//
// The Circle kernel for BMCoco: brings up the hardware, then runs the
// XRoar emulator on core 1 while core 0 services USB.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for the
// Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_KERNEL_H_
#define BMCOCO_KERNEL_H_

#include <SDCard/emmc.h>
#include <circle/actled.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/input/console.h>
#include <circle/input/mouse.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/nulldevice.h>
#include <circle/sched/scheduler.h>
#include <circle/serial.h>
#include <circle/spinlock.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/usb/usbgamepad.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <fatfs/ff.h>

#include "host.h"
#include "logging.h"
#include "options.h"

// The Pi 2 and 3 show the picture and play sound through the VideoCore
// (dispmanx and VCHIQ, from BMC64); the Pi 4 through Circle's framebuffer
// and HDMI sound drivers (src/pi4/).
#if RASPPI >= 4
#include "pi4/sound.h"
#include "pi4/video.h"
#else
#include <vc4/vchiq/vchiqdevice.h>

#include "fbl.h"
#include "vchiqsound.h"
#endif

#define MAX_KEYBOARDS 4

enum TShutdownMode { ShutdownNone, ShutdownHalt, ShutdownReboot };

// Runs the emulator on core 1. Cores 2 and 3 are left idle.
class CEmulatorCore : public CMultiCoreSupport {
public:
  CEmulatorCore(CMemorySystem *pMemorySystem);
  void Run(unsigned nCore) override;
  void Launch(void);

private:
  volatile bool m_bLaunch;
};

class CKernel {
public:
  CKernel(void);
  ~CKernel(void);

  bool Initialize(void);
  TShutdownMode Run(void);

  // Called (through host.h) from the emulator on core 1.
  int VideoInit(int width, int height, uint16_t **pixels, int *pitch);
  void DisplaySize(int *width, int *height);
  bool DisplayMatchRate(double fps);
  void VideoSetView(int x, int y, int w, int h, double aspect);
  void VideoPresent(int wait_vsync);
  unsigned SoundOpen(unsigned freq, int channels, unsigned latency_frames);
  void SoundClose(void);
  unsigned SoundFreeFrames(void);
  void SoundWrite(const int16_t *samples, unsigned frames);
  int KeyEvent(int *usage, int *pressed);
  int KeyDown(int usage);
  unsigned KeyModifiers(void);
  int PadState(int pad);
  int PadAxis(int pad, int axis);
  int MouseRead(int *dx, int *dy, int *buttons);
  int GpioJoystick(int port);
  double Time(void);
  void Reboot(void);
  void PowerOff(void);
  bool SetLogDestination(int destination);
  int GetLogDestination(void) const { return mLog.GetDestination(); }
  // Core 1: writes queued log text to the log file.
  void DrainLog(void) { mLog.Drain(); }

  static CKernel *Get(void) { return s_pThis; }

private:
  static void KeyStatusHandlerRaw(unsigned char ucModifiers,
                                  const unsigned char RawKeys[6], void *pArg);
  static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);
  static void GamePadStatusHandler(unsigned nDeviceIndex,
                                   const TGamePadState *pState);
  static void GamePadRemovedHandler(CDevice *pDevice, void *pContext);
  static void MouseStatusHandler(unsigned nButtons, int nDisplacementX,
                                 int nDisplacementY, int nWheelMove);
  static void MouseRemovedHandler(CDevice *pDevice, void *pContext);

  void SetupUSBKeyboards(void);
  void SetupUSBGamePads(void);
  void SetupUSBMouse(void);
  void SetupGPIOJoysticks(void);
  void PushKeyEvent(int usage, int pressed);
  void UpdateMergedKeys(void);

private:
  // Order matters: members are constructed in this order.
  CActLED mActLED;
  CKernelOptions mKernelOptions;
  Options mOptions;
  CDeviceNameService mDeviceNameService;
  CNullDevice mNullDevice;
  CExceptionHandler mExceptionHandler;
  CInterruptSystem mInterrupt;
  CSerialDevice mSerial;
  // The logger and stdio write here; it passes it on to mSerial, a file or
  // nowhere.
  CLogDevice mLog;
  CTimer mTimer;
  CLogger mLogger;
  CScheduler mScheduler;
  CGPIOManager mGPIOManager;
#if RASPPI <= 3
  CVCHIQDevice mVCHIQ;
#endif
  CEMMCDevice mEMMC;
  FATFS mFileSystem;
  CUSBHCIDevice mUSBHCI;
  CConsole mConsole;
  CEmulatorCore mEmulatorCore;

#if RASPPI >= 4
  Pi4Video mVideo;
  Pi4Sound mPi4Sound;
#else
  FrameBufferLayer mFrameBuffer;
  VchiqSound *mSound;
  int mSoundChannels;
  int mVolume;
  TVCHIQSoundDestination mAudioOut;
#endif

  // Keyboards. Each reports its own set of pressed keys; the emulator sees
  // the union of all of them.
  CUSBKeyboardDevice *m_pKeyboard[MAX_KEYBOARDS];
  unsigned m_KeyboardIndex[MAX_KEYBOARDS];
  unsigned char m_KeyboardModifiers[MAX_KEYBOARDS];
  unsigned char m_KeyboardKeys[MAX_KEYBOARDS][6];
  unsigned char m_KeyDown[256];
  unsigned char m_MergedModifiers;

  // Keyboard events from the USB interrupt (core 0) to the emulator (core 1).
  // Single producer, single consumer.
  enum { KEY_QUEUE_SIZE = 128 };
  volatile unsigned m_KeyQueueHead;
  volatile unsigned m_KeyQueueTail;
  unsigned short m_KeyQueue[KEY_QUEUE_SIZE];

  CUSBGamePadDevice *m_pGamePad[HOST_PADS];
  volatile int m_PadState[HOST_PADS];
  // Left analog stick, 0..65535 per axis, or -1 without one.
  volatile int m_PadAnalog[HOST_PADS][2];
  // Axes the direction is read from on pads Circle doesn't know.
  int m_PadAxisX[HOST_PADS];
  int m_PadAxisY[HOST_PADS];

  // The mouse (all USB mice are "mouse1"). The USB interrupt adds up how
  // far it moves; the emulator takes the sums.
  CMouseDevice *m_pMouse;
  volatile int m_MouseDx;
  volatile int m_MouseDy;
  volatile int m_MouseButtons;

  bool m_bGPIOJoysticks;
  CGPIOPin *m_pJoyPins[2][5];

  CSpinLock m_Lock;

  static CKernel *s_pThis;
};

#endif
