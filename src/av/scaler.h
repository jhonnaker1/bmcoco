//
// scaler.h
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
//     Copyright (C) 2015-2016 davej
//
//     This program is free software; you can redistribute it and/or modify it
//     under the terms of the GNU General Public License as published by the Free
//     Software Foundation; either version 2 of the License, or (at your option)
//     any later version.
//
// Modified for BMCoco.
//
// Smooth scaling and the CRT shader for the Pi 4 and Pi 5, done by the ARM cores.
//
// On the Pi 2 and 3 the VideoCore does both: the dispmanx layer's scaler
// filters the picture ("smooth"), and OpenGL ES runs the crt-pi shader
// ("crt_shader"; src/fbl.cpp and src/crt_pi_idx.c, from BMC64). A 64-bit
// Circle kernel on the Pi 4 and Pi 5 has neither, so this draws the same pictures
// in software, into the 32-bit framebuffer that src/av/video.cpp takes
// from the firmware.
//
// The class doesn't know about Circle or threads: src/av/video.cpp shares
// the work out between cores, and tools/pi4scaler tests it on a PC.
//
// Sources:
//  - crt-pi, "A Raspberry Pi friendly CRT shader", Copyright (C) 2015-2016
//    davej, GPL 2 or later, as BMC64 ships it in src/crt_pi_idx.c (the
//    indexed-colour version): the scan line weight
//    max(1 - d*d*SCANLINE_WEIGHT, SCANLINE_GAP_BRIGHTNESS), MULTISAMPLE
//    (the weight averaged at dy and dy +/- filterWidth, with filterWidth =
//    (InputSize.y / OutputSize.y) / 3), FAKE_GAMMA (squaring the colour
//    before the scan line weight and BLOOM_FACTOR, the square root after),
//    the row curve that sharpens the picture vertically
//    (8 * dy^4 towards the next row), Distort() for CURVATURE, and
//    texture2DBL(), its bilinear sampling, used when BMC64 defines
//    BILINEAR_INTERPOLATION (smooth on).
//  - BMC64, src/kernel.cpp in this repository (CKernel::VideoInit): the
//    parameters BMCoco uses on the Pi 3 (curvature 0.10 x 0.15, no mask,
//    gamma and fake gamma, scan lines, multisample, scan line weight 6.0,
//    gap brightness 0.12, bloom 1.5, not SHARPER) and src/fbl.cpp: the
//    texture is sampled GL_NEAREST, the shader draws the whole destination
//    rectangle, and with curvature the texture is just the visible part.
//  - The OpenGL ES 2.0 specification's LINEAR filter, for where bilinear
//    samples fall: texel i is centred at i + 1/2, so a sample at u
//    mixes texels floor(u - 1/2) and the next one.
//
// What is done differently, and why:
//  - With fake gamma the shader computes sqrt(colour^2 * weight * bloom),
//    which is colour * sqrt(weight * bloom). Everything but the colour
//    depends only on where the pixel is, so it is worked out once per row
//    (or, with curvature, once per pixel) when the view is set, and each
//    frame is a few multiplies per pixel. The result is clamped to 255,
//    as the GPU clamps it to 1.0.
//  - Without curvature crt-pi is separable: a row's scan line weight and
//    its source row(s) depend only on y, a column's source column(s) only
//    on x. Each emulated line is scaled to the output width once per frame;
//    output rows then mix one or two of those lines.
//  - BMC64's bilinear version samples half a texel off the GL convention
//    (its texture2DBL() mixes floor(u) and floor(u) + 1) and moves the
//    scan lines by the same half texel. Here both are centred on the
//    texel centres: the same picture, half an emulated pixel to the right
//    and down.
//  - Weights are fixed point (1/128 and 1/256), so colours may differ
//    from a float shader by up to 3 levels of 255 (4 with curvature and
//    smooth on, where four pixels are mixed).
//

#ifndef BMCOCO_AV_SCALER_H_
#define BMCOCO_AV_SCALER_H_

#include <stdint.h>

// An RGB565 pixel as 0xFFRRGGBB, each channel's top bits repeated into its
// low bits so white stays white.
static inline uint32_t Pi4Rgb565ToArgb(uint16_t c) {
  uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) |
         (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

class Pi4Scaler {
public:
  Pi4Scaler(void);
  ~Pi4Scaler(void);

  // crt: the crt-pi look; smooth: bilinear filtering (with crt, crt-pi's
  // bilinear sampling); curvature: crt-pi's curved screen (with crt only).
  void SetMode(bool crt, bool smooth, bool curvature);

  // The source is RGB565 (what XRoar draws for BMCoco), SRC_PITCH pixels
  // per line; the part shown starts at (VX, VY) and is VW x VH; it is drawn
  // DW x DH. Returns false if out of memory.  (BMA800XL's source is 8-bit
  // indexed, with a palette.)
  bool Configure(unsigned src_pitch, int vx, int vy, int vw, int vh,
                 unsigned dw, unsigned dh);


  // Each frame: first Prepare() every item below PrepareCount() (the
  // source lines in colour, scaled to the output width unless the screen
  // is curved), then Render() every output
  // row. Different items, or rows, may be done on different cores at the
  // same time, but all Prepare()s must be finished before any Render().
  unsigned PrepareCount(void) const;
  void Prepare(const uint16_t *src, unsigned item);
  // OUT gets Width() pixels, 0xAARRGGBB; the alpha byte isn't set.
  void Render(const uint16_t *src, unsigned row, uint32_t *out) const;

  unsigned Width(void) const { return m_nDestW; }
  unsigned Height(void) const { return m_nDestH; }

  // The crt-pi parameters (see above).
  static const float CurvatureX;
  static const float CurvatureY;
  static const float ScanlineWeight;
  static const float ScanlineGapBrightness;
  static const float BloomFactor;

  // What crt-pi does to one pixel, in floating point, the slow way: for
  // tests (tools/pi4scaler) to check the fast path against. Returns
  // 0xRRGGBB for output pixel (X, Y).
  uint32_t Reference(const uint16_t *src, unsigned x, unsigned y) const;

private:
  void Free(void);
  bool BuildSeparable(void);
  bool BuildCurved(void);
  void RenderCurved(unsigned row, uint32_t *out) const;
  float ScanlineMultiplier(float py) const;

  bool m_bCrt, m_bSmooth, m_bCurvature;

  unsigned m_nPitch;
  int m_nViewX, m_nViewY, m_nViewW, m_nViewH;
  unsigned m_nDestW, m_nDestH;


  // Without curvature: per output column, the source columns mixed and
  // the second one's weight (of 256).
  uint16_t *m_pCol0, *m_pCol1, *m_pColW;
  // Per output row, the scaled source lines mixed (view rows) and their
  // weights (of 128; the scan line brightness is in them).
  uint16_t *m_pRow0, *m_pRow1;
  uint8_t *m_pRowA, *m_pRowB;
  // The view's lines scaled to the output width, ViewH x DestW.
  uint32_t *m_pLines;

  // With curvature: the view in colour, (ViewW + 1) x (ViewH + 1), the
  // last column and row black; and per output pixel, the offset in it of
  // the top left texel mixed and the weights (of 128) of it, the one to
  // its right, the one below, and below right, packed low byte first.
  uint32_t *m_pTexture;
  unsigned m_nTexPitch;
  uint32_t *m_pIndex;
  uint32_t *m_pWeights;
};

#endif
