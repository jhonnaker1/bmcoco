//
// video.cpp
//
// BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
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
// Modified for BMCoco.
//
// See video.h for what this is and the sources it follows.
//

#include "video.h"

#include "../logging.h"

#include <circle/bcmpropertytags.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/synchronize.h>
#include <circle/timer.h>
#include <string.h>

static const char FromVideo[] = "video";

// The wait for vsync starts this long before the vertical blank is due.
static const u64 VsyncMarginTicks = 3 * (CLOCKHZ / 1000);

Pi4Video *Pi4Video::s_pThis = 0;

namespace {

// Writes BYTES (a multiple of 16) from SRC (8-byte aligned) to framebuffer
// memory in aligned 8-byte pairs, ORing OR_MASK into every 8 bytes (the
// alpha bytes of 32-bit pixels). The framebuffer is Device memory (see
// video.h): no unaligned or vector stores, and no DC ZVA. If DST isn't
// 8-byte aligned, the mask goes into SRC and Circle's memcpy(), which
// checks alignment itself, copies it.
void ToFrameBuffer(void *dst, void *src, unsigned bytes, uint64_t or_mask) {
  uint64_t *s = (uint64_t *)src;
  if (((uintptr)dst & 7) != 0 || (bytes & 15) != 0) {
    if (or_mask != 0) {
      for (unsigned i = 0; i < bytes / 8; i++) {
        s[i] |= or_mask;
      }
    }
    memcpy(dst, src, bytes);
    return;
  }
  uint64_t *d = (uint64_t *)dst;
  for (unsigned i = 0; i < bytes / 8; i += 2) {
    uint64_t a = s[i] | or_mask;
    uint64_t b = s[i + 1] | or_mask;
    asm volatile("stp %x0, %x1, [%2]" : : "r"(a), "r"(b), "r"(d + i) : "memory");
  }
}

// Fills BYTES (a multiple of 16, 8-byte aligned) of framebuffer memory.
void FillFrameBuffer(void *dst, uint64_t value, unsigned long bytes) {
  uint64_t *d = (uint64_t *)dst;
  for (unsigned long i = 0; i < bytes / 8; i += 2) {
    asm volatile("stp %x0, %x0, [%1]" : : "r"(value), "r"(d + i) : "memory");
  }
}

} // namespace

Pi4Video::Pi4Video(void)
    : m_pFrameBuffer(0), m_nDepth(0), m_nWidth(0), m_nHeight(0), m_nPitch(0),
      m_nPages(2), m_nBackPage(1), m_nDisplayWidth(0), m_nDisplayHeight(0),
      m_nLastVsync(0), m_nVsyncPeriod(0), m_pSource(0),
      m_nSourceWidth(0), m_nSourceHeight(0), m_nViewX(0), m_nViewY(0),
      m_nViewW(0), m_nViewH(0), m_nDestX(0), m_nDestY(0), m_nDestW(0),
      m_nDestH(0), m_nRepeat(1), m_pColumn(0), m_bFiltered(false),
      m_bCrt(false), m_bSmooth(false), m_bCurvature(false),
      m_bScalerReady(false), m_pJobPage(0), m_pKms(0),
      m_Driver(Driver::Firmware), m_pKmsTarget(0),
      m_nKmsPitch(0), m_pGpuOffered(0), m_pGpu(0), m_bGpuReady(false),
      m_nKmsSrcW(0), m_nKmsSrcH(0), m_fKmsAspect(1.0), m_fKmsScale(1.0),
      m_bKmsResize(false) {
  for (unsigned i = 0; i < 4; i++) {
    m_pLine[i] = 0;
  }
}

Pi4Video::~Pi4Video(void) {
  delete m_pKms;
  delete m_pFrameBuffer;
  delete[] m_pSource;
  delete[] m_pColumn;
  for (unsigned i = 0; i < 4; i++) {
    delete[] m_pLine[i];
  }
}

bool Pi4Video::TakeFrameBuffer(unsigned width, unsigned height,
                               const unsigned *depths, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    m_pFrameBuffer = new CBcmFrameBuffer(width, height, depths[i], 0, 0, 0, TRUE);
    if (m_pFrameBuffer->Initialize()) {
      // The depth and size the firmware actually gave, from the pitch and
      // the buffer's size: the Pi 5's firmware keeps the depth set in
      // config.txt, and may leave out the second page.
      unsigned w = m_pFrameBuffer->GetWidth(), h = m_pFrameBuffer->GetHeight();
      unsigned pitch = m_pFrameBuffer->GetPitch();
      unsigned bytes = w != 0 ? pitch / w : 0;
      if ((bytes == 1 || bytes == 2 || bytes == 4) && h != 0) {
        m_nDepth = bytes * 8;
        m_nPages = m_pFrameBuffer->GetSize() >= 2ull * h * pitch ? 2 : 1;
        return true;
      }
    }
    delete m_pFrameBuffer;
    m_pFrameBuffer = 0;
  }
  return false;
}

bool Pi4Video::Initialize(bool crt, bool smooth, bool curvature, Driver driver) {
  m_Driver = driver;
  m_bCrt = crt;
  m_bSmooth = smooth;
  m_bCurvature = crt && curvature;
  // The display's size, as CBcmFrameBuffer finds it when given 0 x 0
  // (with the same limits).
  m_nDisplayWidth = 640;
  m_nDisplayHeight = 480;
  CBcmPropertyTags Tags;
  TPropertyTagDisplayDimensions Dimensions;
  if (Tags.GetTag(PROPTAG_GET_DISPLAY_DIMENSIONS, &Dimensions, sizeof Dimensions) &&
      Dimensions.nWidth >= 640 && Dimensions.nWidth <= 4096 &&
      Dimensions.nHeight >= 480 && Dimensions.nHeight <= 2160) {
    m_nDisplayWidth = Dimensions.nWidth;
    m_nDisplayHeight = Dimensions.nHeight;
  }

  m_bFiltered = crt || smooth;
  bool ok;
  if (!m_bFiltered) {
    // Sharp pixels: RGB565, what XRoar draws (so lines are copied as
    // they are), the size of the display; 32-bit is the fall back if the
    // firmware refuses it.
    static const unsigned depths[] = {16, 32};
    ok = TakeFrameBuffer(m_nDisplayWidth, m_nDisplayHeight, depths, 2);
  } else {
    // Filtered pictures are drawn a pixel at a time by the ARM, so their
    // size is limited; the firmware scales a smaller framebuffer up to fill
    // the display. 32-bit, or 16-bit if that's refused.
    static const unsigned depths[] = {32, 16};
    unsigned max_height = curvature && smooth ? 720 : 1080;
    unsigned width = m_nDisplayWidth, height = m_nDisplayHeight;
    if (height > max_height) {
      width = (width * max_height / height) & ~1u;
      height = max_height;
    }
    ok = TakeFrameBuffer(width, height, depths, 2) ||
         TakeFrameBuffer(m_nDisplayWidth, m_nDisplayHeight, depths, 2);
  }
  if (!ok) {
    CLogger::Get()->Write(FromVideo, LogError, "Cannot allocate a framebuffer");
    return false;
  }
  m_nWidth = m_pFrameBuffer->GetWidth();
  m_nHeight = m_pFrameBuffer->GetHeight();
  m_nPitch = m_pFrameBuffer->GetPitch();
  CLogger::Get()->Write(FromVideo, LogNotice,
                        "Framebuffer %ux%u, %u bit, %u page(s) (display %ux%u), pitch %u, %u bytes at %p",
                        m_nWidth, m_nHeight, m_nDepth, m_nPages, m_nDisplayWidth,
                        m_nDisplayHeight, m_nPitch, m_pFrameBuffer->GetSize(), Page(0));
  FlushStartupLog();

  if (m_bFiltered) {
    m_Scaler.SetMode(crt, smooth, curvature);
    for (unsigned i = 1; i < 4; i++) {
      m_pLine[i] = new uint32_t[m_nWidth];
    }
  }
  ClearPages();
  m_pFrameBuffer->SetVirtualOffset(0, 0);
  m_nBackPage = 1;

  // The direct display driver takes over from the firmware's framebuffer,
  // which stays as the fall back if it can't.
  if (driver != Driver::Firmware) {
    m_pKms = new kms::Display;
#if RASPPI >= 5
    m_pKms->SetFramebuffer(Page(0), (size_t)m_nPages * m_nHeight * m_nPitch, m_nPitch);
#endif
    if (!m_pKms->Attach()) {
      delete m_pKms;
      m_pKms = 0;
    } else {
      // Sharp and smooth are scaled by the HVS; the CRT shader is drawn by
      // the ARM at up to 1080 lines and scaled the rest of the way.
      CLogger::Get()->Write(FromVideo, LogNotice, "Picture by BMCoco's display lists on the HVS");
    }
  }
  FlushStartupLog();
  // Under the direct display driver the HVS does smooth scaling itself, so
  // the ARM only draws the CRT shader.
  if (m_bFiltered && (m_pKms == 0 || crt)) {
    CLogger::Get()->Write(FromVideo, LogNotice, "Drawn by the ARM:%s%s%s",
                          crt ? " CRT shader" : "", smooth ? " smooth" : "",
                          crt && curvature ? " curved" : "");
  }

  __atomic_store_n(&s_pThis, this, __ATOMIC_RELEASE);
  DataSyncBarrier();
  SendEvent();
  return true;
}

uint8_t *Pi4Video::Page(unsigned page) const {
  return (uint8_t *)(uintptr)m_pFrameBuffer->GetBuffer() +
         (size_t)(page % m_nPages) * m_nHeight * m_nPitch;
}

void Pi4Video::ClearPages(void) {
  // RGB565 0 is black; 32-bit black is opaque.
  uint64_t black = m_nDepth == 32 ? 0xFF000000FF000000ull : 0;
  unsigned long bytes = (unsigned long)m_nHeight * m_nPitch;
  for (unsigned page = 0; page < m_nPages; page++) {
    uint8_t *p = Page(page);
    if (((uintptr)p & 7) == 0 && (bytes & 15) == 0) {
      FillFrameBuffer(p, black, bytes);
    } else {
      memset(p, 0, bytes);
    }
  }
}

int Pi4Video::Allocate(int width, int height, uint16_t **pixels, int *pitch) {
  if (m_pFrameBuffer == 0) {
    return -1;
  }
  delete[] m_pSource;
  m_pSource = new uint16_t[(size_t)width * height];
  memset(m_pSource, 0, (size_t)width * height * sizeof *m_pSource);
  m_nSourceWidth = width;
  m_nSourceHeight = height;
  *pixels = m_pSource;
  *pitch = width * (int)sizeof *m_pSource;
  SetView(0, 0, width, height, (double)width / height, 1.0);
  return 0;
}

// Sharp: the view fills the display's height (times SCALE) in whole
// multiples of its own height, so every emulated line is the same number of
// display lines, and the columns are picked nearest neighbour.
// Filtered: the view fills the height (times SCALE) exactly, as on the
// Pi 3. Either way the width follows the aspect ratio, and lines start
// and end on 16-byte boundaries, for ToFrameBuffer().
void Pi4Video::SetView(int x, int y, int w, int h, double aspect,
                       double scale) {
  if (m_pFrameBuffer == 0 || w <= 0 || h <= 0) {
    return;
  }
  if (m_pKms != 0) {
    SetViewKms(x, y, w, h, aspect, scale);
    return;
  }
  if (scale <= 0.1 || scale > 1.0) {
    scale = 1.0;
  }
  m_nViewX = x;
  m_nViewY = y;
  m_nViewW = w;
  m_nViewH = h;

  unsigned dest_h;
  if (m_bFiltered) {
    dest_h = (unsigned)(m_nHeight * scale);
    m_nRepeat = 1;
  } else {
    unsigned repeat = (unsigned)(m_nHeight * scale) / (unsigned)h;
    if (repeat == 0) {
      repeat = 1;
    }
    m_nRepeat = repeat;
    dest_h = repeat * h;
  }
  if (dest_h > m_nHeight) {
    dest_h = m_nHeight;
  }
  if (dest_h == 0) {
    dest_h = 1;
  }
  unsigned dest_w = (unsigned)(dest_h * aspect + 0.5);
  if (dest_w > m_nWidth) {
    dest_w = m_nWidth;
  }
  const unsigned bytes = m_nDepth / 8;
  dest_w &= ~(16 / bytes - 1);
  if (dest_w == 0) {
    dest_w = 16 / bytes;
  }
  m_nDestW = dest_w;
  m_nDestH = dest_h;
  m_nDestX = ((m_nWidth - dest_w) / 2) & ~(8 / bytes - 1);
  m_nDestY = (m_nHeight - dest_h) / 2;

  if (m_bFiltered) {
    m_bScalerReady = m_Scaler.Configure((unsigned)m_nSourceWidth, x, y, w, h,
                                        dest_w, dest_h);
    if (!m_bScalerReady) {
      CLogger::Get()->Write(FromVideo, LogError, "Not enough memory to scale the picture");
    }
  } else {
    BuildColumnMap();
  }
  ClearPages();
  ConfigureGpu();
}

void Pi4Video::BuildColumnMap(void) {
  delete[] m_pColumn;
  m_pColumn = new uint16_t[m_nDestW];
  for (unsigned i = 0; i < m_nDestW; i++) {
    m_pColumn[i] = (uint16_t)(m_nViewX + (i * (unsigned)m_nViewW) / m_nDestW);
  }
}

void Pi4Video::DrawSharp(uint8_t *page) {
  const unsigned bytes = m_nDepth / 8;
  // A display line at up to 32 bits a pixel: too big for the stack. Only
  // core 1 draws sharp pictures, so one is enough.
  alignas(16) static uint8_t line[4096 * 4];
  if (m_nDestW * bytes > sizeof line) {
    return;
  }
  for (int y = 0; y < m_nViewH; y++) {
    const uint16_t *src = m_pSource + (size_t)(m_nViewY + y) * m_nSourceWidth;
    if (m_nDepth == 16) {
      uint16_t *line16 = (uint16_t *)line;
      for (unsigned i = 0; i < m_nDestW; i++) {
        line16[i] = src[m_pColumn[i]];
      }
    } else {
      // 32-bit: the firmware's fall back, and the Pi 5's (it gives the
      // depth set in config.txt).
      uint32_t *line32 = (uint32_t *)line;
      for (unsigned i = 0; i < m_nDestW; i++) {
        line32[i] = Pi4Rgb565ToArgb(src[m_pColumn[i]]);
      }
    }
    uint8_t *dst = page + (size_t)(m_nDestY + y * m_nRepeat) * m_nPitch +
                   m_nDestX * bytes;
    for (unsigned r = 0; r < m_nRepeat; r++) {
      ToFrameBuffer(dst, line, m_nDestW * bytes, 0);
      dst += m_nPitch;
    }
  }
}

// One output line, 0xAARRGGBB, into the framebuffer.
void Pi4Video::CopyLine(uint8_t *dst, uint32_t *line) const {
  if (m_nDepth == 32) {
    ToFrameBuffer(dst, line, m_nDestW * 4, 0xFF000000FF000000ull);
    return;
  }
  // RGB565, converted in place (each pixel shrinks).
  uint16_t *line16 = (uint16_t *)line;
  for (unsigned i = 0; i < m_nDestW; i++) {
    uint32_t p = line[i];
    line16[i] = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) |
                           ((p >> 3) & 0x001F));
  }
  ToFrameBuffer(dst, line16, m_nDestW * 2, 0);
}

// Pi4FrameShare's job: the scaler's two steps.
void Pi4Video::Prepare(unsigned item) { m_Scaler.Prepare(m_pSource, item); }

void Pi4Video::Render(unsigned row, unsigned core) {
  if (m_pKmsTarget != 0) {
    // Straight into the direct driver's buffer (ordinary memory; the HVS
    // ignores the alpha byte).
    m_Scaler.Render(m_pSource, row, (uint32_t *)(m_pKmsTarget + (size_t)row * m_nKmsPitch));
    return;
  }
  uint32_t *line = m_pLine[core];
  m_Scaler.Render(m_pSource, row, line);
  CopyLine(m_pJobPage + (size_t)(m_nDestY + row) * m_nPitch +
               m_nDestX * (m_nDepth / 8),
           line);
}

void Pi4Video::WorkerLoop(unsigned core) {
  // Until Initialize() has run there is nothing to help with.
  while (__atomic_load_n(&s_pThis, __ATOMIC_ACQUIRE) == 0) {
    WaitForEvent();
  }
  Pi4Video *video = s_pThis;
  if (!video->m_bFiltered || core >= 4 || video->m_pLine[core] == 0) {
    for (;;) {
      asm volatile("wfi");
    }
  }
  video->m_Share.Help(*video, core);
}

void Pi4Video::Present(bool wait_vsync) {
  if (m_pFrameBuffer == 0 || m_pSource == 0) {
    return;
  }
  v3d::Crt *offered = __atomic_load_n(&m_pGpuOffered, __ATOMIC_ACQUIRE);
  if (offered != 0 && m_pGpu == 0) {
    __atomic_store_n(&m_pGpuOffered, (v3d::Crt *)0, __ATOMIC_RELAXED);
    if (DrawnByArm()) {
      m_pGpu = offered;
      m_pGpu->SetMode(m_bCrt, m_bSmooth, m_bCurvature);
      if (m_pKms != 0 && m_nViewW > 0) {
        // The picture's size may change with the GPU drawing it.
        SetViewKms(m_nViewX, m_nViewY, m_nViewW, m_nViewH, m_fKmsAspect, m_fKmsScale);
      } else {
        ConfigureGpu();
      }
      if (m_pGpu != 0) {
        CLogger::Get()->Write(FromVideo, LogNotice, "Drawn by the GPU from now on");
      }
    } else {
      CLogger::Get()->Write(FromVideo, LogNotice,
                            "The GPU isn't needed: this picture isn't drawn by the ARM");
    }
  }
  if (m_pKms != 0) {
    if (m_bKmsResize && m_nViewW > 0) {
      m_bKmsResize = false;
      SetViewKms(m_nViewX, m_nViewY, m_nViewW, m_nViewH, m_fKmsAspect, m_fKmsScale);
    }
    PresentKms(wait_vsync);
    return;
  }
  uint8_t *page = Page(m_nBackPage);
  if (!m_bFiltered) {
    if (m_pColumn != 0) {
      DrawSharp(page);
    }
  } else if (m_pGpu != 0 && m_bGpuReady) {
    v3d::Target target = {page, m_nWidth, m_nHeight, m_nPitch};
    if (!m_pGpu->Draw(m_pSource, target, (int)m_nDestX, (int)m_nDestY)) {
      DropGpu("drawing failed");
    }
  } else if (m_bScalerReady) {
    if (m_pLine[1] == 0) {
      return;
    }
    // Cores 2 and 3 help (see frameshare.h).
    m_pJobPage = page;
    m_Share.Run(*this, 1, m_Scaler.PrepareCount(), m_Scaler.Height());
  }

  // Show the page just drawn, then wait for the vertical blank: after it
  // the other page is off screen and free to draw the next frame into.
  // With one page the frame is drawn where it is shown.
  if (m_nPages == 2) {
    m_pFrameBuffer->SetVirtualOffset(0, m_nBackPage * m_nHeight);
  }
  if (wait_vsync) {
    WaitForVerticalSync();
  }
  m_nBackPage ^= 1;
}

// Circle's tasks (the network, Wi-Fi, the web UI) only run when core 1
// yields. The Pi 3 runs them while it waits for a frame (Circle's VCHIQ
// waits yield); the firmware's wait for vsync is a mailbox call that holds
// core 1 instead, which leaves them one turn a frame, far too little for
// the Wi-Fi driver. So they run until shortly before the vertical blank is
// due, and only the rest of the frame is spent in the mailbox call. The
// period is the shortest seen between two waits: a missed blank only makes
// one longer, and the firmware's mode doesn't change while running.
void Pi4Video::WaitForVerticalSync(void) {
  if (m_nVsyncPeriod > VsyncMarginTicks) {
    u64 due = m_nLastVsync + m_nVsyncPeriod - VsyncMarginTicks;
    while ((s64)(CTimer::GetClockTicks64() - due) < 0) {
      CScheduler::Get()->Yield();
    }
  }
  m_pFrameBuffer->WaitForVerticalSync();
  u64 now = CTimer::GetClockTicks64();
  if (m_nLastVsync != 0) {
    u64 period = now - m_nLastVsync;
    if (m_nVsyncPeriod == 0 || period < m_nVsyncPeriod) {
      m_nVsyncPeriod = period;
    }
  }
  m_nLastVsync = now;
}

void Pi4Video::DisplaySize(int *width, int *height) const {
  if (m_pKms != 0) {
    *width = (int)m_pKms->Width();
    *height = (int)m_pKms->Height();
    return;
  }
  *width = (int)m_nDisplayWidth;
  *height = (int)m_nDisplayHeight;
}

bool Pi4Video::MatchRate(double fps, unsigned audio_fs, unsigned lines) {
  if (m_pKms == 0 || m_Driver != Driver::Kms) {
    return false;
  }
#if RASPPI >= 5
  unsigned width = m_pKms->Width(), height = m_pKms->Height();
  bool changed = m_pKms->MatchRate(fps, audio_fs, lines);
  if (m_pKms->Width() != width || m_pKms->Height() != height) {
    // A new size: the picture is laid out again before the next frame.
    m_bKmsResize = true;
  }
  return changed;
#else
  (void)lines;
  return m_pKms->MatchRate(fps, audio_fs);
#endif
}

bool Pi4Video::ResolutionChoice(void) const {
#if RASPPI >= 5
  return m_pKms != 0 && m_Driver == Driver::Kms;
#else
  return false;
#endif
}

void Pi4Video::Blank(void) {
  if (m_pKms != 0) {
    m_pKms->Blank();
    return;
  }
  if (m_pFrameBuffer == 0) {
    return;
  }
  ClearPages();
}

//
// The direct display driver (src/kms/pi4/, or src/kms/pi5/ on the Pi 5).
//

// As SetView() does for the framebuffer, on the mode's size: sharp in whole
// multiples of the view's height, smooth and CRT filling the height (times
// SCALE). Sharp and smooth hand the HVS the view as it is; the CRT shader
// draws at the size shown, or a whole fraction of it no more than 1080
// lines (720 curved and smooth), which the HVS enlarges sharply.
void Pi4Video::SetViewKms(int x, int y, int w, int h, double aspect,
                          double scale) {
  if (scale <= 0.1 || scale > 1.0) {
    scale = 1.0;
  }
  m_nViewX = x;
  m_nViewY = y;
  m_nViewW = w;
  m_nViewH = h;
  m_fKmsAspect = aspect;
  m_fKmsScale = scale;

  const unsigned width = m_pKms->Width(), height = m_pKms->Height();
  unsigned dest_h;
  if (m_bFiltered) {
    dest_h = (unsigned)(height * scale);
  } else {
    unsigned repeat = (unsigned)(height * scale) / (unsigned)h;
    dest_h = (repeat == 0 ? 1 : repeat) * (unsigned)h;
  }
  if (dest_h > height) {
    dest_h = height;
  }
  if (dest_h == 0) {
    dest_h = 1;
  }
  unsigned dest_w = (unsigned)(dest_h * aspect + 0.5);
  if (dest_w > width) {
    dest_w = width;
  }
  unsigned dest_x = (width - dest_w) / 2;
  unsigned dest_y = (height - dest_h) / 2;

  unsigned src_w = (unsigned)w, src_h = (unsigned)h;
  bool nearest = !m_bSmooth;
  if (m_bCrt) {
    // The ARM can't draw curved and smooth at more than 720 lines; the GPU
    // can.
    unsigned max_height = m_bCurvature && m_bSmooth && m_pGpu == 0 ? 720 : 1080;
    unsigned divide = (dest_h + max_height - 1) / max_height;
    src_w = dest_w / divide;
    src_h = dest_h / divide;
    nearest = true;
    m_nKmsSrcW = src_w;
    m_nKmsSrcH = src_h;
    // While the GPU draws, the ARM's scaler isn't needed (it is set up
    // again if the ARM takes over).
    m_bScalerReady = m_pGpu == 0 && m_Scaler.Configure((unsigned)m_nSourceWidth, x, y, w,
                                                       h, src_w, src_h);
    if (m_pGpu == 0 && !m_bScalerReady) {
      CLogger::Get()->Write(FromVideo, LogError, "Not enough memory to scale the picture");
      return;
    }
  }
  if (!m_pKms->SetPicture(src_w, src_h, dest_x, dest_y, dest_w, dest_h, nearest)) {
    // Back to the firmware's framebuffer.
    CLogger::Get()->Write(FromVideo, LogWarning, "Can't show the picture on the HVS; using the framebuffer");
    m_pKms->Release();
    delete m_pKms;
    m_pKms = 0;
    SetView(x, y, w, h, aspect, scale);
    return;
  }
  ConfigureGpu();
}

void Pi4Video::PresentKms(bool wait_vsync) {
  unsigned pitch;
  uint32_t *buffer = m_pKms->BeginFrame(&pitch);
  if (buffer == 0) {
    return;
  }
  if (m_bCrt && m_pGpu != 0 && m_bGpuReady) {
    v3d::Target target = {buffer, m_nKmsSrcW, m_nKmsSrcH, pitch * 4};
    if (!m_pGpu->Draw(m_pSource, target, 0, 0)) {
      DropGpu("drawing failed");
    }
  } else if (m_bCrt) {
    if (!m_bScalerReady) {
      return;
    }
    // Cores 2 and 3 help, as for the framebuffer.
    m_pKmsTarget = (uint8_t *)buffer;
    m_nKmsPitch = pitch * 4;
    m_Share.Run(*this, 1, m_Scaler.PrepareCount(), m_Scaler.Height());
    m_pKmsTarget = 0;
  } else {
    // The view in colour, as it is; the HVS scales it.
    for (int y = 0; y < m_nViewH; y++) {
      const uint16_t *src = m_pSource + (size_t)(m_nViewY + y) * m_nSourceWidth + m_nViewX;
      uint32_t *dst = buffer + (size_t)y * pitch;
      for (int x = 0; x < m_nViewW; x++) {
        dst[x] = Pi4Rgb565ToArgb(src[x]);
      }
    }
  }
  m_pKms->EndFrame(wait_vsync);
}

//
// The 3D GPU (src/v3d/), with the crt_gpu option.
//

void Pi4Video::UseGpu(v3d::Crt *crt) {
  __atomic_store_n(&m_pGpuOffered, crt, __ATOMIC_RELEASE);
}

// The pictures the ARM draws, which the GPU can draw instead: smooth and
// CRT on the framebuffer (32-bit only), CRT on the HVS.
bool Pi4Video::DrawnByArm(void) const {
  if (m_pKms != 0) {
    return m_bCrt;
  }
  return m_bFiltered && m_nDepth == 32;
}

// After the view changes: the GPU draws the same picture as the scaler, at
// the same size, into memory it can reach (the first 1 GB).
void Pi4Video::ConfigureGpu(void) {
  if (m_pGpu == 0) {
    return;
  }
  m_bGpuReady = false;
  if (m_nViewW <= 0 || m_nViewH <= 0) {
    return;  // no view yet
  }
  if (!DrawnByArm()) {
    DropGpu("not needed for this picture");
    return;
  }
  bool ok;
  if (m_pKms != 0) {
#if RASPPI >= 5
    ok = m_pKms->BuffersBelow(v3d::Gpu::MappedBytes) &&
         m_pGpu->Configure((unsigned)m_nSourceWidth, m_nViewX, m_nViewY, m_nViewW, m_nViewH,
                           m_nKmsSrcW, m_nKmsSrcH);
#else
    ok = m_pGpu->Configure((unsigned)m_nSourceWidth, m_nViewX, m_nViewY, m_nViewW, m_nViewH,
                           m_nKmsSrcW, m_nKmsSrcH);
#endif
  } else {
    ok = v3d::Gpu::Reachable(Page(0), (size_t)m_nPages * m_nHeight * m_nPitch) &&
         m_pGpu->Configure((unsigned)m_nSourceWidth, m_nViewX, m_nViewY, m_nViewW, m_nViewH,
                           m_nDestW, m_nDestH);
  }
  if (!ok) {
    DropGpu("can't draw this picture");
    return;
  }
  m_bGpuReady = true;
}

void Pi4Video::DropGpu(const char *why) {
  CLogger::Get()->Write(FromVideo, LogWarning, "Drawn by the ARM, not the GPU: %s", why);
  m_pGpu = 0;
  m_bGpuReady = false;
  // Back to the size the ARM can draw, from the next frame.
  m_bKmsResize = m_bKmsResize || (m_pKms != 0 && m_bCrt);
}
