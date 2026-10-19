//
// display.cpp
//
// See display.h.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "display.h"

#include <circle/bcm2835.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/new.h>
#include <circle/sched/scheduler.h>
#include <circle/synchronize.h>
#include <circle/timer.h>
#include <string.h>

namespace kms {

static const char FromDisplay[] = "kms";

// A frame takes at most 1/24 s; more than this and the display has gone.
static const unsigned FrameTimeoutMs = 100;

Display::Display(void)
    : m_bAttached(false), m_nPitch(0), m_nBufferWords(0), m_nBack(0),
      m_bFlipPending(false), m_nFlipCount(0), m_bHavePicture(false),
      m_bTimedOut(false) {
  m_pBuffer[0] = m_pBuffer[1] = 0;
  memset(&m_Plane, 0, sizeof m_Plane);
}

Display::~Display(void) { FreeBuffers(); }

void Display::FreeBuffers(void) {
  for (unsigned i = 0; i < 2; i++) {
    if (m_pBuffer[i] != 0) {
      CMemorySystem::HeapFree(m_pBuffer[i]);
      m_pBuffer[i] = 0;
    }
  }
  m_nBufferWords = 0;
}

bool Display::Attach(void) {
  if (!m_Hvs.Attach()) {
    CLogger::Get()->Write(FromDisplay, LogWarning,
                          "Neither HDMI port is fed by the HVS; using the firmware's framebuffer");
    return false;
  }
  m_ModeSetter.SetPort(m_Hvs.Port());
  m_bAttached = true;
  CLogger::Get()->Write(FromDisplay, LogNotice, "HDMI%u, HVS channel %u, %ux%u",
                        m_Hvs.Port(), m_Hvs.Channel(), m_Hvs.Width(), m_Hvs.Height());
  return true;
}

bool Display::SetPicture(unsigned src_w, unsigned src_h, unsigned x, unsigned y,
                         unsigned w, unsigned h, bool nearest) {
  if (!m_bAttached || src_w == 0 || src_h == 0) {
    return false;
  }
  // Lines start on 64-byte (cache line) boundaries.
  unsigned pitch = (src_w + 15) & ~15u;
  unsigned words = pitch * src_h;
  if (words > m_nBufferWords) {
    // The buffers may be on screen: show the background until the new ones
    // are drawn.
    unsigned now = m_Hvs.FrameCount();
    m_Hvs.Show(0);
    if (m_Hvs.WaitForChange(now, FrameTimeoutMs)) {
      m_Hvs.WaitForChange((now + 1) & 63, FrameTimeoutMs);
    }
    FreeBuffers();
    for (unsigned i = 0; i < 2; i++) {
      m_pBuffer[i] = (uint32_t *)CMemorySystem::HeapAllocate(words * 4, HEAP_DMA30);
      if (m_pBuffer[i] == 0) {
        FreeBuffers();
        m_bHavePicture = false;
        CLogger::Get()->Write(FromDisplay, LogError, "Not enough memory for the picture");
        return false;
      }
      memset(m_pBuffer[i], 0, words * 4);
      CleanAndInvalidateDataCacheRange((uintptr)m_pBuffer[i], words * 4);
    }
    m_nBufferWords = words;
    m_bFlipPending = false;
    m_Plane.bus_address = 0;   // nothing drawn into the new buffers yet
  }
  m_nPitch = pitch;
  m_Plane.pitch = pitch * 4;
  m_Plane.src_width = src_w;
  m_Plane.src_height = src_h;
  m_Plane.x = x;
  m_Plane.y = y;
  m_Plane.width = w;
  m_Plane.height = h;
  m_Plane.nearest = nearest;
  // The HVS must be able to show it (a trial list), with line buffer
  // memory to spare.
  uint32_t trial[MaxWords];
  m_bHavePicture = LbmWords(m_Plane) <= 28 * 1024 && BuildList(m_Plane, 0, 0, trial) != 0;
  CLogger::Get()->Write(FromDisplay, LogNotice, "Picture %ux%u shown at %u,%u %ux%u, %s",
                        src_w, src_h, x, y, w, h, nearest ? "sharp" : "smooth");
  return m_bHavePicture;
}

uint32_t *Display::BeginFrame(unsigned *pitch) {
  if (!m_bHavePicture) {
    return 0;
  }
  // The back buffer was on screen until the frame after the last flip.
  if (m_bFlipPending) {
    if (m_Hvs.FrameCount() == m_nFlipCount &&
        !WaitForFlip(m_nFlipCount) && !m_bTimedOut) {
      m_bTimedOut = true;
      CLogger::Get()->Write(FromDisplay, LogWarning, "The HVS has stopped (display off?)");
    }
    m_bFlipPending = false;
  }
  *pitch = m_nPitch;
  return m_pBuffer[m_nBack];
}

void Display::EndFrame(bool wait_vsync) {
  if (!m_bHavePicture) {
    return;
  }
  uint32_t *buffer = m_pBuffer[m_nBack];
  CleanAndInvalidateDataCacheRange((uintptr)buffer,
                                   (size_t)m_nPitch * m_Plane.src_height * 4);
  m_Plane.bus_address = (uint32_t)BUS_ADDRESS((uintptr)buffer);
  unsigned now = m_Hvs.FrameCount();
  if (!m_Hvs.Show(&m_Plane)) {
    return;
  }
  // The frame being composed now still shows the other buffer; from the
  // next one on, this one. So the other is free once the count moves on.
  m_bFlipPending = true;
  m_nFlipCount = now;
  if (wait_vsync) {
    if (WaitForFlip(now)) {
      m_bFlipPending = false;
    }
  }
  m_nBack ^= 1;
}

// Circle's tasks (the network, Wi-Fi, the web UI) only run when core 1
// yields, so the wait for the next frame runs them, as the Pi 3's wait in
// vc_dispmanx_update_submit_sync() does (Circle's addon/linux semaphores
// yield while they wait). Spinning instead leaves them one turn a frame,
// which is far too little for the Wi-Fi driver.
bool Display::WaitForFlip(unsigned from) {
  u64 start = CTimer::GetClockTicks64();
  u64 limit = (u64)FrameTimeoutMs * (CLOCKHZ / 1000);
  while (m_Hvs.FrameCount() == from) {
    if (CTimer::GetClockTicks64() - start > limit) {
      return false;
    }
    CScheduler::Get()->Yield();
  }
  return true;
}

bool Display::MatchRate(double fps, unsigned audio_fs) {
  if (!m_bAttached) {
    return false;
  }
  Timing current, wanted;
  if (!m_ModeSetter.Current(&current)) {
    return false;
  }
  if (!ChooseMode(current, fps, m_ModeSetter.GetEdid(), &wanted)) {
    CLogger::Get()->Write(FromDisplay, LogNotice,
                          "No %ux%u mode at %.2f Hz the display lists; keeping %.2f Hz",
                          current.h_active, current.v_active, fps, current.Refresh());
    return false;
  }
  if (wanted.vic == current.vic &&
      (wanted.pixel_hz > current.pixel_hz ? wanted.pixel_hz - current.pixel_hz
                                          : current.pixel_hz - wanted.pixel_hz) < 1000) {
    return false;
  }
  m_ModeSetter.Set(m_Hvs, wanted, audio_fs, Reinstall, this);
  return true;
}

void Display::Reinstall(void *param) {
  Display *display = (Display *)param;
  // The buffer last shown is the front one.
  if (display->m_bHavePicture && display->m_Plane.bus_address != 0) {
    display->m_Hvs.Show(&display->m_Plane);
  } else {
    display->m_Hvs.Show(0);
  }
  display->m_bFlipPending = false;
}

void Display::Blank(void) {
  if (m_bAttached) {
    m_Hvs.Show(0);
  }
}

void Display::Release(void) {
  if (m_bAttached) {
    m_Hvs.Restore();
  }
}

} // namespace kms
