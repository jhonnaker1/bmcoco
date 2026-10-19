//
// scaler.cpp
//
// See scaler.h for what this is, the sources it follows and where it
// differs from them.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "scaler.h"

#include <math.h>
#include <string.h>

// BMCoco's crt-pi settings (BMC64's defaults, see scaler.h).
const float Pi4Scaler::CurvatureX = 0.10f;
const float Pi4Scaler::CurvatureY = 0.15f;
const float Pi4Scaler::ScanlineWeight = 6.0f;
const float Pi4Scaler::ScanlineGapBrightness = 0.12f;
const float Pi4Scaler::BloomFactor = 1.5f;

namespace {

unsigned Round(float v) { return (unsigned)(v + 0.5f); }

// Where output pixel I of N samples, 0..1 across the picture: its centre,
// as the GPU interpolates TEX0 there.
float TexCoord(unsigned i, unsigned n) { return ((float)i + 0.5f) / (float)n; }

// The two texels a bilinear sample at U mixes, along one axis of a view
// starting at LO, N texels long: I0 and I0 + 1, F the weight of the second.
// Off the ends the edge texel is used alone (F = 0), as GL's
// CLAMP_TO_EDGE does.
void Pair(float u, int lo, int n, int *i0, float *f) {
  float fl = floorf(u);
  int i = (int)fl;
  float fr = u - fl;
  if (i < lo) {
    i = lo;
    fr = 0.0f;
  } else if (i >= lo + n - 1) {
    i = lo + n - 1;
    fr = 0.0f;
  }
  *i0 = i;
  *f = fr;
}

int Clamp(int i, int lo, int n) {
  if (i < lo) {
    return lo;
  }
  if (i > lo + n - 1) {
    return lo + n - 1;
  }
  return i;
}

// crt-pi's CalcScanLineWeight().
float LineWeight(float dist) {
  float w = 1.0f - dist * dist * Pi4Scaler::ScanlineWeight;
  return w > Pi4Scaler::ScanlineGapBrightness ? w
                                              : Pi4Scaler::ScanlineGapBrightness;
}

// crt-pi's row curve, without SHARPER: the row sampled is moved from the
// centre of the row towards the next one by 8 * dy^4 (texels).
float CurvedRow(float py) {
  float centre = floorf(py) + 0.5f;
  float dy = py - centre;
  float sign = dy > 0.0f ? 1.0f : (dy < 0.0f ? -1.0f : 0.0f);
  float d2 = dy * dy;
  return centre + sign * d2 * d2 * 8.0f;
}

// Weights of 0..159 out of 128 on 0xAARRGGBB pixels, with the channels in
// two 32-bit words: 0x00RR00BB and 0x00AA00GG. A channel's sum is at most
// 255 * 159 + 64, which fits its 16 bits.
const uint32_t LowBytes = 0x00FF00FF;
const uint32_t Half = 0x00400040;  // 64, to round, in each half

inline uint32_t Saturate(uint32_t sum) {
  uint32_t v = (sum >> 7) & 0x01FF01FF;          // 9 bits each
  uint32_t over = ((v >> 8) & 0x00010001) * 0xFF;  // 0xFF where >= 256
  return (v | over) & LowBytes;
}

inline uint32_t Scale1(uint32_t p, uint32_t w) {
  uint32_t rb = (p & LowBytes) * w + Half;
  uint32_t ag = ((p >> 8) & LowBytes) * w + Half;
  return Saturate(rb) | (Saturate(ag) << 8);
}

inline uint32_t Scale4(uint32_t p00, uint32_t p01, uint32_t p10, uint32_t p11,
                       uint32_t w) {
  uint32_t w00 = w & 0xFF, w01 = (w >> 8) & 0xFF;
  uint32_t w10 = (w >> 16) & 0xFF, w11 = w >> 24;
  uint32_t rb = (p00 & LowBytes) * w00 + (p01 & LowBytes) * w01 +
                (p10 & LowBytes) * w10 + (p11 & LowBytes) * w11 + Half;
  uint32_t ag = ((p00 >> 8) & LowBytes) * w00 + ((p01 >> 8) & LowBytes) * w01 +
                ((p10 >> 8) & LowBytes) * w10 + ((p11 >> 8) & LowBytes) * w11 +
                Half;
  return Saturate(rb) | (Saturate(ag) << 8);
}

} // namespace

Pi4Scaler::Pi4Scaler(void)
    : m_bCrt(false), m_bSmooth(false), m_bCurvature(false), m_nPitch(0),
      m_nViewX(0), m_nViewY(0), m_nViewW(0), m_nViewH(0), m_nDestW(0),
      m_nDestH(0), m_pCol0(0), m_pCol1(0), m_pColW(0), m_pRow0(0), m_pRow1(0),
      m_pRowA(0), m_pRowB(0), m_pLines(0), m_pTexture(0), m_nTexPitch(0),
      m_pIndex(0), m_pWeights(0) {}

Pi4Scaler::~Pi4Scaler(void) { Free(); }

void Pi4Scaler::Free(void) {
  delete[] m_pCol0;
  delete[] m_pCol1;
  delete[] m_pColW;
  delete[] m_pRow0;
  delete[] m_pRow1;
  delete[] m_pRowA;
  delete[] m_pRowB;
  delete[] m_pLines;
  delete[] m_pTexture;
  delete[] m_pIndex;
  delete[] m_pWeights;
  m_pCol0 = m_pCol1 = m_pColW = m_pRow0 = m_pRow1 = 0;
  m_pRowA = m_pRowB = 0;
  m_pLines = m_pTexture = m_pIndex = m_pWeights = 0;
  m_nDestW = m_nDestH = 0;
}

void Pi4Scaler::SetMode(bool crt, bool smooth, bool curvature) {
  m_bCrt = crt;
  m_bSmooth = smooth;
  m_bCurvature = crt && curvature;
}

bool Pi4Scaler::Configure(unsigned src_pitch, int vx, int vy, int vw, int vh,
                          unsigned dw, unsigned dh) {
  Free();
  if (vw <= 0 || vh <= 0 || dw == 0 || dh == 0) {
    return false;
  }
  m_nPitch = src_pitch;
  m_nViewX = vx;
  m_nViewY = vy;
  m_nViewW = vw;
  m_nViewH = vh;
  m_nDestW = dw;
  m_nDestH = dh;
  bool ok = m_bCurvature ? BuildCurved() : BuildSeparable();
  if (!ok) {
    Free();
  }
  return ok;
}

// crt-pi's scan line brightness for an output pixel at source row
// position PY, times BLOOM_FACTOR, square rooted for FAKE_GAMMA.
float Pi4Scaler::ScanlineMultiplier(float py) const {
  float dy = py - (floorf(py) + 0.5f);
  // MULTISAMPLE: filterWidth = (InputSize.y / OutputSize.y) / 3.
  float fw = ((float)m_nViewH / (float)m_nDestH) / 3.0f;
  float w = LineWeight(dy) + LineWeight(dy - fw) + LineWeight(dy + fw);
  w *= 0.3333333f;
  return sqrtf(w * BloomFactor);
}

bool Pi4Scaler::BuildSeparable(void) {
  const unsigned dw = m_nDestW, dh = m_nDestH;
  m_pCol0 = new uint16_t[dw];
  m_pCol1 = new uint16_t[dw];
  m_pColW = new uint16_t[dw];
  m_pRow0 = new uint16_t[dh];
  m_pRow1 = new uint16_t[dh];
  m_pRowA = new uint8_t[dh];
  m_pRowB = new uint8_t[dh];
  m_pLines = new uint32_t[(unsigned long)m_nViewH * dw];
  if (!m_pCol0 || !m_pCol1 || !m_pColW || !m_pRow0 || !m_pRow1 || !m_pRowA ||
      !m_pRowB || !m_pLines) {
    return false;
  }

  // Columns: GL_NEAREST for crt-pi on its own, bilinear when smooth.
  for (unsigned x = 0; x < dw; x++) {
    float px = (float)m_nViewX + TexCoord(x, dw) * (float)m_nViewW;
    if (m_bSmooth) {
      int c;
      float f;
      Pair(px - 0.5f, m_nViewX, m_nViewW, &c, &f);
      unsigned w = Round(f * 256.0f);
      m_pCol0[x] = (uint16_t)c;
      m_pCol1[x] = (uint16_t)(w != 0 ? c + 1 : c);
      m_pColW[x] = (uint16_t)w;
    } else {
      int c = Clamp((int)floorf(px), m_nViewX, m_nViewW);
      m_pCol0[x] = m_pCol1[x] = (uint16_t)c;
      m_pColW[x] = 0;
    }
  }

  // Rows: which scaled lines, and how much of each.
  for (unsigned y = 0; y < dh; y++) {
    float py = (float)m_nViewY + TexCoord(y, dh) * (float)m_nViewH;
    float m = m_bCrt ? ScanlineMultiplier(py) : 1.0f;
    int r0, r1;
    unsigned a, b;
    if (m_bSmooth) {
      float f;
      Pair(py - 0.5f, m_nViewY, m_nViewH, &r0, &f);
      if (m_bCrt) {
        b = Round(m * f * 128.0f);
        a = Round(m * 128.0f) - b;
      } else {
        b = Round(f * 128.0f);
        a = 128 - b;
      }
      r1 = b != 0 ? r0 + 1 : r0;
    } else {
      r0 = r1 = Clamp((int)floorf(CurvedRow(py)), m_nViewY, m_nViewH);
      a = Round(m * 128.0f);
      b = 0;
    }
    m_pRow0[y] = (uint16_t)(r0 - m_nViewY);
    m_pRow1[y] = (uint16_t)(r1 - m_nViewY);
    m_pRowA[y] = (uint8_t)a;
    m_pRowB[y] = (uint8_t)b;
  }
  return true;
}

bool Pi4Scaler::BuildCurved(void) {
  const unsigned dw = m_nDestW, dh = m_nDestH;
  unsigned long n = (unsigned long)dw * dh;
  // The view in colour, with a spare column and row of black: bilinear
  // samples always read a pixel to the right and one below.
  m_nTexPitch = (unsigned)m_nViewW + 1;
  unsigned long texels = (unsigned long)m_nTexPitch * (m_nViewH + 1);
  m_pTexture = new uint32_t[texels];
  m_pIndex = new uint32_t[n];
  m_pWeights = new uint32_t[n];
  if (!m_pTexture || !m_pIndex || !m_pWeights) {
    return false;
  }
  memset(m_pTexture, 0, texels * sizeof(uint32_t));
  // crt-pi's Distort(); screenScale is 1, the texture being just the view.
  const float bx = 1.0f - 0.23f * CurvatureX;
  const float by = 1.0f - 0.23f * CurvatureY;
  for (unsigned y = 0; y < dh; y++) {
    for (unsigned x = 0; x < dw; x++) {
      unsigned long i = (unsigned long)y * dw + x;
      float cx = TexCoord(x, dw) - 0.5f;
      float cy = TexCoord(y, dh) - 0.5f;
      float rsq = cx * cx + cy * cy;
      cx = (cx + cx * CurvatureX * rsq) * bx;
      cy = (cy + cy * CurvatureY * rsq) * by;
      if (fabsf(cx) >= 0.5f || fabsf(cy) >= 0.5f) {
        m_pIndex[i] = 0;
        m_pWeights[i] = 0;  // black
        continue;
      }
      float px = (float)m_nViewX + (cx + 0.5f) * (float)m_nViewW;
      float py = (float)m_nViewY + (cy + 0.5f) * (float)m_nViewH;
      float m = ScanlineMultiplier(py);
      int x0, y0;
      uint32_t w;
      if (m_bSmooth) {
        float fx, fy;
        Pair(px - 0.5f, m_nViewX, m_nViewW, &x0, &fx);
        Pair(py - 0.5f, m_nViewY, m_nViewH, &y0, &fy);
        // The total is rounded once, so the four add up to it.
        unsigned w01 = Round(m * fx * (1.0f - fy) * 128.0f);
        unsigned w10 = Round(m * (1.0f - fx) * fy * 128.0f);
        unsigned w11 = Round(m * fx * fy * 128.0f);
        int w00 = (int)Round(m * 128.0f) - (int)(w01 + w10 + w11);
        w = (unsigned)(w00 > 0 ? w00 : 0) | w01 << 8 | w10 << 16 | w11 << 24;
      } else {
        x0 = Clamp((int)floorf(px), m_nViewX, m_nViewW);
        y0 = Clamp((int)floorf(CurvedRow(py)), m_nViewY, m_nViewH);
        w = Round(m * 128.0f);
      }
      m_pIndex[i] = (uint32_t)(y0 - m_nViewY) * m_nTexPitch + (uint32_t)(x0 - m_nViewX);
      m_pWeights[i] = w;
    }
  }
  return true;
}

unsigned Pi4Scaler::PrepareCount(void) const {
  return m_pLines != 0 || m_pTexture != 0 ? (unsigned)m_nViewH : 0;
}

void Pi4Scaler::Prepare(const uint16_t *src, unsigned item) {
  if (item >= (unsigned)m_nViewH) {
    return;
  }
  const uint16_t *__restrict s = src + (unsigned long)(m_nViewY + item) * m_nPitch;
  if (m_pTexture != 0) {
    // With curvature: the line in colour.
    uint32_t *__restrict t = m_pTexture + (unsigned long)item * m_nTexPitch;
    s += m_nViewX;
    for (int x = 0; x < m_nViewW; x++) {
      t[x] = Pi4Rgb565ToArgb(s[x]);
    }
    return;
  }
  if (m_pLines == 0) {
    return;
  }
  uint32_t *__restrict line = m_pLines + (unsigned long)item * m_nDestW;
  const unsigned dw = m_nDestW;
  if (!m_bSmooth) {
    for (unsigned x = 0; x < dw; x++) {
      line[x] = Pi4Rgb565ToArgb(s[m_pCol0[x]]);
    }
    return;
  }
  // Mix two pixels; weights of 256, so no channel can overflow its half.
  for (unsigned x = 0; x < dw; x++) {
    uint32_t p0 = Pi4Rgb565ToArgb(s[m_pCol0[x]]);
    uint32_t p1 = Pi4Rgb565ToArgb(s[m_pCol1[x]]);
    uint32_t w1 = m_pColW[x], w0 = 256 - w1;
    uint32_t rb = ((p0 & LowBytes) * w0 + (p1 & LowBytes) * w1) >> 8;
    uint32_t ag = (((p0 >> 8) & LowBytes) * w0 + ((p1 >> 8) & LowBytes) * w1) >> 8;
    line[x] = (rb & LowBytes) | ((ag & LowBytes) << 8);
  }
}

void Pi4Scaler::Render(const uint16_t *src, unsigned row, uint32_t *out) const {
  if (row >= m_nDestH) {
    return;
  }
  if (m_bCurvature) {
    RenderCurved(row, out);
    return;
  }
  if (m_pLines == 0) {
    return;
  }
  // Every byte (channel) is a * line 0 + b * line 1, out of 128, clamped:
  // simple enough for the compiler to do 16 bytes at a time with NEON.
  const uint8_t *__restrict l0 =
      (const uint8_t *)(m_pLines + (unsigned long)m_pRow0[row] * m_nDestW);
  const uint8_t *__restrict l1 =
      (const uint8_t *)(m_pLines + (unsigned long)m_pRow1[row] * m_nDestW);
  uint8_t *__restrict o = (uint8_t *)out;
  const uint16_t a = m_pRowA[row], b = m_pRowB[row];
  const unsigned n = m_nDestW * 4;
  if (b == 0) {
    for (unsigned i = 0; i < n; i++) {
      uint16_t v = (uint16_t)((l0[i] * a + 64) >> 7);
      o[i] = (uint8_t)(v > 255 ? 255 : v);
    }
  } else {
    for (unsigned i = 0; i < n; i++) {
      uint16_t v = (uint16_t)((l0[i] * a + l1[i] * b + 64) >> 7);
      o[i] = (uint8_t)(v > 255 ? 255 : v);
    }
  }
}

void Pi4Scaler::RenderCurved(unsigned row, uint32_t *out) const {
  if (m_pIndex == 0) {
    return;
  }
  const unsigned dw = m_nDestW;
  const uint32_t *idx = m_pIndex + (unsigned long)row * dw;
  const uint32_t *wt = m_pWeights + (unsigned long)row * dw;
  const uint32_t *tex = m_pTexture;
  const unsigned pitch = m_nTexPitch;
  if (!m_bSmooth) {
    for (unsigned x = 0; x < dw; x++) {
      out[x] = Scale1(tex[idx[x]], wt[x]);
    }
    return;
  }
  for (unsigned x = 0; x < dw; x++) {
    const uint32_t *t = tex + idx[x];
    out[x] = Scale4(t[0], t[1], t[pitch], t[pitch + 1], wt[x]);
  }
}

uint32_t Pi4Scaler::Reference(const uint16_t *src, unsigned x,
                              unsigned y) const {
  // Output pixel centre in texture coordinates, 0..1 across the view.
  float tx = TexCoord(x, m_nDestW);
  float ty = TexCoord(y, m_nDestH);
  if (m_bCurvature) {
    float cx = tx - 0.5f, cy = ty - 0.5f;
    float rsq = cx * cx + cy * cy;
    cx += cx * (CurvatureX * rsq);
    cy += cy * (CurvatureY * rsq);
    cx *= 1.0f - 0.23f * CurvatureX;
    cy *= 1.0f - 0.23f * CurvatureY;
    if (fabsf(cx) >= 0.5f || fabsf(cy) >= 0.5f) {
      return 0;
    }
    tx = cx + 0.5f;
    ty = cy + 0.5f;
  }
  float px = (float)m_nViewX + tx * (float)m_nViewW;
  float py = (float)m_nViewY + ty * (float)m_nViewH;

  float c[3];
  if (m_bSmooth) {
    int x0, y0;
    float fx, fy;
    Pair(px - 0.5f, m_nViewX, m_nViewW, &x0, &fx);
    Pair(py - 0.5f, m_nViewY, m_nViewH, &y0, &fy);
    // At the edges the second texel's weight is 0.
    int x1 = x0 + 1 < m_nViewX + m_nViewW ? x0 + 1 : x0;
    int y1 = y0 + 1 < m_nViewY + m_nViewH ? y0 + 1 : y0;
    uint32_t p[4] = {Pi4Rgb565ToArgb(src[y0 * m_nPitch + x0]),
                     Pi4Rgb565ToArgb(src[y0 * m_nPitch + x1]),
                     Pi4Rgb565ToArgb(src[y1 * m_nPitch + x0]),
                     Pi4Rgb565ToArgb(src[y1 * m_nPitch + x1])};
    for (int k = 0; k < 3; k++) {
      int shift = 16 - 8 * k;
      float tl = (float)((p[0] >> shift) & 255) / 255.0f;
      float tr = (float)((p[1] >> shift) & 255) / 255.0f;
      float bl = (float)((p[2] >> shift) & 255) / 255.0f;
      float br = (float)((p[3] >> shift) & 255) / 255.0f;
      float top = tl + (tr - tl) * fx;
      float bottom = bl + (br - bl) * fx;
      c[k] = top + (bottom - top) * fy;
    }
  } else {
    int sx = Clamp((int)floorf(px), m_nViewX, m_nViewW);
    int sy = Clamp((int)floorf(m_bCrt ? CurvedRow(py) : py), m_nViewY, m_nViewH);
    uint32_t p = Pi4Rgb565ToArgb(src[sy * m_nPitch + sx]);
    for (int k = 0; k < 3; k++) {
      c[k] = (float)((p >> (16 - 8 * k)) & 255) / 255.0f;
    }
  }

  if (m_bCrt) {
    // As the shader has it: SCANLINES with MULTISAMPLE, GAMMA with
    // FAKE_GAMMA.
    float dy = py - (floorf(py) + 0.5f);
    float fw = ((float)m_nViewH / (float)m_nDestH) / 3.0f;
    float w = LineWeight(dy);
    w += LineWeight(dy - fw);
    w += LineWeight(dy + fw);
    w *= 0.3333333f;
    for (int k = 0; k < 3; k++) {
      float v = c[k] * c[k];
      v *= w * BloomFactor;
      c[k] = sqrtf(v);
    }
  }

  uint32_t rgb = 0;
  for (int k = 0; k < 3; k++) {
    float v = c[k] > 1.0f ? 1.0f : c[k];
    rgb = rgb << 8 | Round(v * 255.0f);
  }
  return rgb;
}
