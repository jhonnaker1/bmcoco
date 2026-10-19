//
// crt.cpp
//
// See crt.h.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "crt.h"

#include "shaders.h"

#include "../av/scaler.h"

#include <circle/logger.h>
#include <circle/synchronize.h>
#include <circle/timer.h>

namespace v3d {

static const char FromGpu[] = "v3d";

namespace {

// crt.frag's push constants.
struct CrtParameters {
  float texture_width, texture_height;
  float filter_width;
};

} // namespace

Crt::Crt(void)
    : m_pGpu(0), m_pRenderer(0), m_bReady(false), m_bCrt(false), m_bSmooth(false),
      m_bCurvature(false), m_nPitch(0), m_nViewX(0), m_nViewY(0), m_nViewW(0), m_nViewH(0),
      m_nDestW(0), m_nDestH(0), m_nFrames(0), m_nFillUs(0), m_nBinUs(0), m_nRenderUs(0) {}

bool Crt::Initialize(Gpu *gpu, Renderer *renderer, unsigned max_view_width,
                     unsigned max_view_height) {
  m_pGpu = gpu;
  m_pRenderer = renderer;
  const Program *programs[] = {&CopyProgram, &CrtProgram, &CrtSmoothProgram, &CrtCurveProgram,
                                 &CrtCurveSmoothProgram};
  for (const Program *program : programs) {
    if (!renderer->Load(*program)) {
      return false;
    }
  }
  return m_Texture.Create(gpu, max_view_width, max_view_height);
}

void Crt::SetMode(bool crt, bool smooth, bool curvature) {
  m_bCrt = crt;
  m_bSmooth = smooth;
  m_bCurvature = crt && curvature;
  m_bReady = m_nViewW > 0 && m_Texture.Set(m_nViewW, m_nViewH, m_bSmooth);
}

bool Crt::Configure(unsigned src_pitch, int vx, int vy, int vw, int vh, unsigned dw,
                    unsigned dh) {
  m_nPitch = src_pitch;
  m_nViewX = vx;
  m_nViewY = vy;
  m_nViewW = vw;
  m_nViewH = vh;
  m_nDestW = dw;
  m_nDestH = dh;
  m_bReady = vw > 0 && vh > 0 && m_Texture.Set(vw, vh, m_bSmooth);
  m_nFrames = 0;
  m_nFillUs = m_nBinUs = m_nRenderUs = 0;
  return m_bReady;
}

void Crt::Fill(const uint16_t *src) {
  for (int y = 0; y < m_nViewH; y++) {
    const uint16_t *line = src + (m_nViewY + y) * m_nPitch + m_nViewX;
    for (int x = 0; x < m_nViewW; x++) {
      m_Texture.Pixel(x, y) = Pi4Rgb565ToArgb(line[x]);
    }
  }
  m_Texture.Flush();
}

bool Crt::Draw(const uint16_t *src, const Target &target, int x, int y) {
  if (!m_bReady) {
    return false;
  }
  u64 start = CTimer::GetClockTicks64();
  Fill(src);
  m_nFillUs += (CTimer::GetClockTicks64() - start) * 1000000 / CLOCKHZ;
  bool ok;
  if (!m_bCrt) {
    ok = m_pRenderer->Draw(CopyProgram, m_Texture, target, x, y, m_nDestW, m_nDestH,
                           0xFF000000);
  } else {
    const Program *program;
    if (m_bCurvature) {
      program = m_bSmooth ? &CrtCurveSmoothProgram : &CrtCurveProgram;
    } else {
      program = m_bSmooth ? &CrtSmoothProgram : &CrtProgram;
    }
    CrtParameters parameters = {(float)m_nViewW, (float)m_nViewH,
                                ((float)m_nViewH / (float)m_nDestH) / 3.0f};
    ok = m_pRenderer->Draw(*program, m_Texture, target, x, y, m_nDestW, m_nDestH, 0xFF000000,
                           &parameters, sizeof parameters);
  }
  m_nBinUs += m_pGpu->BinMicroseconds();
  m_nRenderUs += m_pGpu->RenderMicroseconds();
  if (++m_nFrames == 250) {
    CLogger::Get()->Write(FromGpu, LogNotice,
                          "Frame times (average of %u, %ux%u): texture %u us, binning %u us, "
                          "rendering %u us", m_nFrames, m_nDestW, m_nDestH,
                          (unsigned)(m_nFillUs / m_nFrames), (unsigned)(m_nBinUs / m_nFrames),
                          (unsigned)(m_nRenderUs / m_nFrames));
  }
  return ok;
}

// An Atari-sized source with a view scaled by 2.86 across and 3.21 down,
// in all four crt-pi modes. The ARM's floating point reference and the
// GPU differ in rounding, and where a sample falls just either side of a
// texel edge; a few pixels may differ.
bool Crt::Test(void) {
  const unsigned Pitch = 384, Lines = 240;
  const int ViewX = 24, ViewY = 8, ViewW = 336, ViewH = 224;
  const unsigned DestW = 960, DestH = 720;
  const unsigned Tolerance = 2;          // levels of 255
  CLogger *log = CLogger::Get();

  uint16_t *src = new uint16_t[Pitch * Lines];
  uint32_t *pixels = (uint32_t *)m_pGpu->Allocate(DestW * DestH * 4, 4096);
  Pi4Scaler *scaler = new Pi4Scaler;
  if (src == 0 || pixels == 0 || scaler == 0) {
    log->Write(FromGpu, LogWarning, "CRT test: not enough memory");
    delete[] src;
    delete scaler;
    return false;
  }
  // Stripes, steps and single pixels, in varied colours (BMA800XL's test
  // palette, as RGB565: BMCoco's pictures are RGB565).
  for (unsigned y = 0; y < Lines; y++) {
    for (unsigned x = 0; x < Pitch; x++) {
      unsigned i = (((x / 3) * 5 + y * 7) ^ ((x * y) & 0x11)) & 255;
      unsigned r = (i * 37) & 255, g = (i * 91 + 40) & 255, b = (i * 13) & 255;
      src[y * Pitch + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
  }
  Target target = {pixels, DestW, DestH, DestW * 4};

  bool all = true;
  for (unsigned mode = 0; mode < 4; mode++) {
    bool smooth = mode & 1, curvature = mode & 2;
    SetMode(true, smooth, curvature);
    scaler->SetMode(true, smooth, curvature);
    bool ok = Configure(Pitch, ViewX, ViewY, ViewW, ViewH, DestW, DestH) &&
              scaler->Configure(Pitch, ViewX, ViewY, ViewW, ViewH, DestW, DestH);
    CleanAndInvalidateDataCacheRange((uintptr_t)pixels, DestW * DestH * 4);
    ok = ok && Draw(src, target, 0, 0);
    InvalidateDataCacheRange((uintptr_t)pixels, DestW * DestH * 4);

    unsigned off = 0, worst = 0, worst_x = 0, worst_y = 0;
    uint32_t worst_got = 0, worst_expected = 0;
    for (unsigned y = 0; ok && y < DestH; y++) {
      for (unsigned x = 0; x < DestW; x++) {
        uint32_t expected = scaler->Reference(src, x, y);
        uint32_t got = pixels[y * DestW + x] & 0xFFFFFF;
        unsigned diff = 0;
        for (int k = 0; k < 24; k += 8) {
          int d = (int)((got >> k) & 255) - (int)((expected >> k) & 255);
          unsigned a = d < 0 ? -d : d;
          diff = a > diff ? a : diff;
        }
        if (diff > Tolerance) {
          off++;
        }
        if (diff > worst) {
          worst = diff;
          worst_x = x;
          worst_y = y;
          worst_got = got;
          worst_expected = expected;
        }
      }
    }
    // At most 1 in 200 pixels off by more than the tolerance.
    bool passed = ok && off * 200 <= DestW * DestH;
    all = all && passed;
    log->Write(FromGpu, passed ? LogNotice : LogWarning,
               "CRT test%s%s %s: %u of %u pixels off by more than %u; worst %u at %u,%u "
               "(%06X, expected %06X)%s", smooth ? " smooth" : "",
               curvature ? " curved" : "", passed ? "passed" : "failed", off,
               DestW * DestH, Tolerance, worst, worst_x, worst_y, worst_got,
               worst_expected, ok ? "" : " (the GPU didn't finish)");
  }
  delete scaler;
  delete[] src;
  m_bReady = false;
  return all;
}

} // namespace v3d
