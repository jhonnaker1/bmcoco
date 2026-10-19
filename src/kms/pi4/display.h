//
// display.h
//
// The direct display driver's picture: two 32-bit pixel buffers in the
// ARM's memory, shown in turn as one HVS plane, scaled to its place on the
// screen by the HVS (hvs.h). BMCoco draws a frame into the buffer not
// on screen, then flips.
//
// Sources:
//  - Circle, include/circle/memory.h: HEAP_DMA30, memory below 1 GB that
//    the VideoCore can read; include/circle/bcm2835.h: BUS_ADDRESS(), the
//    address the VideoCore sees it at; include/circle/synchronize64.h:
//    CleanAndInvalidateDataCacheRange(), so what the ARM wrote is in
//    memory before the HVS reads it.
//  - Circle, lib/sched/scheduler.cpp: CScheduler::Yield() runs Circle's
//    tasks on the core that calls it (core 1 here); addon/linux/
//    semaphore.cpp: Circle's VCHIQ waits yield, which is how the Pi 3 runs
//    them while it waits for a frame.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_KMS_PI4_DISPLAY_H_
#define BMCOCO_KMS_PI4_DISPLAY_H_

#include "hvs.h"
#include "modeset.h"

#include <stdint.h>

namespace kms {

class Display {
public:
  Display(void);
  ~Display(void);

  // Takes over the HVS channel feeding HDMI0, or else HDMI1. False if it
  // can't.
  bool Attach(void);

  // The mode's size.
  unsigned Width(void) const { return m_Hvs.Width(); }
  unsigned Height(void) const { return m_Hvs.Height(); }

  // Each frame is SRC_W x SRC_H pixels, shown at (X, Y), W x H on the
  // screen, sharp (NEAREST) or smooth. False if out of memory or the HVS
  // can't scale it so.
  bool SetPicture(unsigned src_w, unsigned src_h, unsigned x, unsigned y,
                  unsigned w, unsigned h, bool nearest);

  // The buffer to draw the next frame into (waits until it is off the
  // screen), PITCH pixels per line; 0 if there is no picture.
  uint32_t *BeginFrame(unsigned *pitch);
  // Shows it from the next frame on; with WAIT_VSYNC, waits until it is.
  void EndFrame(bool wait_vsync);

  // Stage 2: switches the video mode to run at FPS frames a second, if it
  // can (see modeset.h). AUDIO_FS is the HDMI sound's sample rate. True if
  // the mode was changed (or a failed change undone).
  bool MatchRate(double fps, unsigned audio_fs);

  // A black screen (before halting).
  void Blank(void);
  // Gives the screen back to the firmware.
  void Release(void);

  Hvs &GetHvs(void) { return m_Hvs; }

private:
  void FreeBuffers(void);
  // As Hvs::WaitForChange(), running Circle's tasks while it waits.
  bool WaitForFlip(unsigned from);
  // Puts the picture's display list back (after the channel restarts).
  static void Reinstall(void *param);

  Hvs m_Hvs;
  ModeSetter m_ModeSetter;
  bool m_bAttached;
  uint32_t *m_pBuffer[2];
  unsigned m_nPitch;         // pixels
  unsigned m_nBufferWords;   // allocated, per buffer
  unsigned m_nBack;          // buffer drawn next
  bool m_bFlipPending;       // the back buffer is on screen until...
  unsigned m_nFlipCount;     // ...the frame count moves on from this
  Plane m_Plane;
  bool m_bHavePicture;
  bool m_bTimedOut;
};

} // namespace kms

#endif
