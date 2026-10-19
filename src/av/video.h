//
// video.h
//
// The Pi 4 and Pi 5 display: Circle's framebuffer driver (CBcmFrameBuffer), which
// asks the VideoCore firmware for a framebuffer through the mailbox
// property interface. The firmware puts it on the screen; BMCoco scales
// the CoCo's picture into it.
//
// The picture comes from XRoar as RGB565 (BMA800XL's, from atari800, is
// 8-bit indexed with a palette; this is its code, changed for that). Sharp
// pixels (the default) use a 16-bit framebuffer the size of the display. Smooth scaling and the CRT shader (src/av/scaler.h) need
// a 32-bit one; it is at most 1080 lines high (720 with curvature and
// smooth both on), and on bigger displays the firmware scales it up.
// Those pictures are drawn by cores 1, 2 and 3 together: core 1 runs the
// emulator, cores 2 and 3 have nothing else to do.
// With the crt_gpu option the 3D GPU draws them instead
// (src/v3d/crt.h), and the ARM again if the GPU can't.
//
// Sources:
//  - Circle, include/circle/bcmframebuffer.h and lib/bcmframebuffer.cpp
//    (R. Stange, GPL 3): the framebuffer class, its palette, page flipping
//    with SetVirtualOffset() and WaitForVerticalSync(), 32-bit pixels being
//    ARGB8888 (CDisplay, and COLOR32() in include/circle/screen.h), and
//    asking for the display's size (PROPTAG_GET_DISPLAY_DIMENSIONS,
//    include/circle/bcmpropertytags.h), with the same sanity limits.
//  - Circle, lib/translationtable64.cpp and lib/memory64.cpp: memory above
//    the ARM's (where the firmware puts the framebuffer) is mapped as
//    Device-nGnRE, where unaligned accesses fault. Writes to it are 8-byte
//    aligned pairs (STP), as Circle's own memcpy() and memset() do them.
//  - Circle, include/circle/multicore.h: CMultiCoreSupport runs
//    Run(nCore) on cores 1 to 3 (sharing the work: frameshare.h).
//  - Arm Architecture Reference Manual (Armv8-A): the alignment rule for
//    Device memory.
//  - Raspberry Pi firmware wiki, "Mailbox property interface"
//    (https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface):
//    the property tags those methods send (allocate buffer, set depth,
//    set virtual offset, set palette, wait for vsync, get display size).
//  - Circle, lib/sched/scheduler.cpp: CScheduler::Yield() runs Circle's
//    tasks on the core that calls it (core 1 here); addon/linux/
//    semaphore.cpp: Circle's VCHIQ waits yield, which is how the Pi 3 runs
//    them while it waits for a frame.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_AV_VIDEO_H_
#define BMCOCO_AV_VIDEO_H_

#include "frameshare.h"
#include "scaler.h"
#if RASPPI >= 5
#include "../kms/pi5/display.h"
#else
#include "../kms/pi4/display.h"
#endif
#include "../v3d/crt.h"

#include <circle/bcmframebuffer.h>
#include <stdint.h>

class Pi4Video {
public:
  Pi4Video(void);
  ~Pi4Video(void);

  // Which driver puts the picture on the screen (the video_driver option):
  // the firmware's framebuffer, or BMCoco's own display lists on the HVS
  // (src/kms/pi4/ or, on the Pi 5, src/kms/pi5/; docs/PI4.md), on the
  // firmware's video mode or (Kms) with BMCoco setting the mode too.
  enum class Driver { Firmware, Hvs, Kms };

  // Core 0, at start up: takes a double buffered framebuffer and, if asked
  // to, the HVS. CRT, SMOOTH and CURVATURE are the crt_shader, smooth and
  // crt_curvature options.
  bool Initialize(bool crt, bool smooth, bool curvature, Driver driver);

  // Core 1, from the emulator (see host.h).
  // PITCH is in bytes.
  int Allocate(int width, int height, uint16_t **pixels, int *pitch);
  void SetView(int x, int y, int w, int h, double aspect, double scale);
  void Present(bool wait_vsync);
  void DisplaySize(int *width, int *height) const;
  // With the Kms driver: runs the display at FPS frames a second if it
  // can. AUDIO_FS: the HDMI sound's sample rate. LINES (the Pi 5 only):
  // 1080 or 720 for that size, 0 for the firmware's. True if the mode
  // changed.
  bool MatchRate(double fps, unsigned audio_fs, unsigned lines = 0);
  // True if MatchRate()'s LINES is used: the Pi 5 with the Kms driver.
  bool ResolutionChoice(void) const;
  // Shows a black screen (before halting).
  void Blank(void);

  // Core 0, once the 3D GPU is running (the crt_gpu option): from the next
  // frame CRT draws the pictures the ARM would (smooth and CRT on the
  // framebuffer, CRT on the HVS). The ARM takes over again if it fails.
  void UseGpu(v3d::Crt *crt);

  // Cores 2 and 3 (CEmulatorCore::Run): help draw frames, forever.
  static void WorkerLoop(unsigned core);

private:
  bool TakeFrameBuffer(unsigned width, unsigned height, const unsigned *depths,
                       unsigned count);
  void ClearPages(void);
  void BuildColumnMap(void);
  uint8_t *Page(unsigned page) const;
  void DrawSharp(uint8_t *page);
  void CopyLine(uint8_t *dst, uint32_t *line) const;
  void SetViewKms(int x, int y, int w, int h, double aspect, double scale);
  void PresentKms(bool wait_vsync);
  void WaitForVerticalSync(void);
  bool DrawnByArm(void) const;
  void ConfigureGpu(void);
  void DropGpu(const char *why);

  // Pi4FrameShare's job.
  friend class Pi4FrameShare;
  void Prepare(unsigned item);
  void Render(unsigned row, unsigned core);

  CBcmFrameBuffer *m_pFrameBuffer;
  unsigned m_nDepth;      // 8 (indexed), 16 (RGB565) or 32 (ARGB8888)
  unsigned m_nWidth;      // framebuffer size in pixels
  unsigned m_nHeight;
  unsigned m_nPitch;      // bytes per framebuffer line
  unsigned m_nPages;      // 2, or 1 if the firmware gave room for one only
  unsigned m_nBackPage;   // page drawn next: 0 or 1
  unsigned m_nDisplayWidth, m_nDisplayHeight;
  u64 m_nLastVsync;       // when the last wait for vsync ended (clock ticks)
  u64 m_nVsyncPeriod;     // shortest time seen between two; 0 if unknown

  // The emulator draws into this RGB565 buffer.
  uint16_t *m_pSource;
  int m_nSourceWidth;
  int m_nSourceHeight;

  // The part of it shown, and where it goes on the screen.
  int m_nViewX, m_nViewY, m_nViewW, m_nViewH;
  unsigned m_nDestX, m_nDestY, m_nDestW, m_nDestH, m_nRepeat;
  uint16_t *m_pColumn;    // sharp: source column for each destination column


  // Smooth scaling and the CRT shader.
  bool m_bFiltered;
  bool m_bCrt, m_bSmooth, m_bCurvature;
  bool m_bScalerReady;
  Pi4Scaler m_Scaler;
  uint32_t *m_pLine[4];   // a line of output per core

  // The frame being drawn by several cores.
  Pi4FrameShare m_Share;
  uint8_t *volatile m_pJobPage;

  // The direct display driver, when it is in use; the CRT shader then
  // draws into its buffer, KMS_PITCH bytes per line, from line 0.
  kms::Display *m_pKms;
  Driver m_Driver;
  uint8_t *m_pKmsTarget;
  unsigned m_nKmsPitch;

  // The GPU, when it draws instead of the ARM: offered by core 0, taken
  // by core 1 at the next frame. KMS_SRC_W x KMS_SRC_H: the size the CRT
  // shader draws at on the HVS.
  v3d::Crt *volatile m_pGpuOffered;
  v3d::Crt *m_pGpu;
  bool m_bGpuReady;
  unsigned m_nKmsSrcW, m_nKmsSrcH;
  double m_fKmsAspect, m_fKmsScale;  // the last SetViewKms()'s
  bool m_bKmsResize;                 // SetViewKms() again at the next frame

  static Pi4Video *s_pThis;
};

#endif
