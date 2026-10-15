//
// display.h
//
// The Pi 5's direct display driver's picture: two 32-bit pixel buffers,
// shown in turn as one HVS plane, scaled to its place on the screen by the
// BCM2712's HVS (hvs.h). BMCoco draws a frame into the buffer not on
// screen, then flips. The same interface as the Pi 4's (src/pi4kms/).
//
// The buffers are in the ARM's memory when the HVS reads the firmware's
// framebuffer at its ARM address; otherwise they are carved out of the
// firmware's framebuffer (no longer shown), at the same offsets from the
// address the firmware's plane gives the HVS.
//
// Sources:
//  - Circle, include/circle/memory.h: HEAP_DMA30, memory below 1 GB;
//    include/circle/synchronize64.h: CleanAndInvalidateDataCacheRange(),
//    so what the ARM wrote is in memory before the HVS reads it.
//  - Circle, lib/sched/scheduler.cpp: CScheduler::Yield() runs Circle's
//    tasks on the core that calls it (core 1 here); addon/linux/
//    semaphore.cpp: Circle's VCHIQ waits yield, which is how the Pi 3 runs
//    them while it waits for a frame.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_PI5KMS_DISPLAY_H_
#define BMCOCO_PI5KMS_DISPLAY_H_

#include "hvs.h"
#include "modeset.h"

#include <stddef.h>
#include <stdint.h>

namespace kms {

class Display {
public:
  Display(void);
  ~Display(void);

  // The firmware's framebuffer (BYTES at BASE, PITCH bytes a line), shown
  // until Attach().
  void SetFramebuffer(void *base, size_t bytes, unsigned pitch);

  // Takes over the HVS channel feeding the HDMI port with a display
  // plugged in (else HDMI0, else HDMI1). False if it can't.
  bool Attach(void);

  // True if the buffers are below LIMIT (for the 3D GPU).
  bool BuffersBelow(uint64_t limit) const;

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

  // Switches the video mode to run at FPS frames a second, if it can (see
  // modeset.h), at 1920x1080 or 1280x720 if LINES is 1080 or 720 and the
  // display lists it, else at the size the firmware chose. AUDIO_FS is the
  // HDMI sound's sample rate. True if the mode was changed (or a failed
  // change undone). After a change of size there is no picture until
  // SetPicture().
  bool MatchRate(double fps, unsigned audio_fs, unsigned lines);

  // A black screen (before halting).
  void Blank(void);
  // Gives the screen back to the firmware.
  void Release(void);

  Hvs &GetHvs(void) { return m_Hvs; }

private:
  void FreeBuffers(void);
  // The address the HVS reads BUFFER at.
  uint64_t HvsAddress(const uint32_t *buffer) const;
  // As Hvs::WaitForChange(), running Circle's tasks while it waits.
  bool WaitForFlip(unsigned from);
  // Puts the picture's display list back (after the channel restarts).
  static void Reinstall(void *param);

  Hvs m_Hvs;
  ModeSetter m_ModeSetter;
  bool m_bAttached;
  uint8_t *m_pFramebuffer;
  size_t m_nFramebufferBytes;
  unsigned m_nFramebufferPitch;
  bool m_bPool;              // the buffers are in the framebuffer
  uint64_t m_nPoolAddress;   // the framebuffer's start, as the HVS sees it
  uint32_t *m_pBuffer[2];
  unsigned m_nPitch;         // pixels
  unsigned m_nBufferWords;   // allocated, per buffer
  unsigned m_nBack;          // buffer drawn next
  bool m_bFlipPending;       // the back buffer is on screen until...
  unsigned m_nFlipCount;     // ...the frame count moves on from this
  Plane m_Plane;
  bool m_bHavePicture;
  bool m_bTimedOut;
  unsigned m_nFirmwareWidth, m_nFirmwareHeight;   // the firmware's mode
};

} // namespace kms

#endif
