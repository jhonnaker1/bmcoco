//
// crt.h
//
// The picture drawn by the 3D GPU of the Pi 4 or Pi 5: the emulator's
// RGB565 screen is turned into 32-bit colour in a texture by the ARM (it
// was BMA800XL's 8-bit indexed one, with a palette), and the GPU
// scales it to the output, with crt-pi (tools/v3dshader/crt.frag) or as a
// plain copy, sharp or smooth. The same pictures as src/pi4/scaler.h draws
// on the ARM cores, which the self-test checks.
//
// Pixels are 32-bit words 0xAARRGGBB, as the framebuffer and the HVS take
// them. The GPU sees them as red in the low byte, but every channel is
// treated alike, so they come out in the order they went in.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_PI4V3D_CRT_H_
#define BMCOCO_PI4V3D_CRT_H_

#include "draw.h"

#include <stdint.h>

namespace v3d {

class Crt {
public:
  Crt(void);

  // Sets up for views up to MAX_VIEW_WIDTH x MAX_VIEW_HEIGHT.
  bool Initialize(Gpu *gpu, Renderer *renderer, unsigned max_view_width,
                  unsigned max_view_height);

  // As Pi4Scaler: crt: the crt-pi look; smooth: bilinear filtering;
  // curvature: crt-pi's curved screen (with crt only).
  void SetMode(bool crt, bool smooth, bool curvature);

  // The source is RGB565, SRC_PITCH pixels per line; the part shown
  // starts at (VX, VY) and is VW x VH; it is drawn DW x DH.
  bool Configure(unsigned src_pitch, int vx, int vy, int vw, int vh, unsigned dw,
                 unsigned dh);

  // Draws a frame from SRC into TARGET with its top left corner at (X, Y);
  // the rest of TARGET is cleared to black. Waits for the GPU.
  // TARGET's memory must be reachable by the GPU (Gpu::Reachable()).
  bool Draw(const uint16_t *src, const Target &target, int x, int y);

  // Draws a test picture in every crt-pi mode and compares it with
  // Pi4Scaler::Reference(). Logs the results.
  bool Test(void);

private:
  void Fill(const uint16_t *src);

  Gpu *m_pGpu;
  Renderer *m_pRenderer;
  Texture m_Texture;
  bool m_bReady;
  bool m_bCrt, m_bSmooth, m_bCurvature;
  unsigned m_nPitch;
  int m_nViewX, m_nViewY, m_nViewW, m_nViewH;
  unsigned m_nDestW, m_nDestH;

  // Time spent on the frames drawn so far (microseconds), logged once.
  unsigned m_nFrames;
  uint64_t m_nFillUs, m_nBinUs, m_nRenderUs;
};

} // namespace v3d

#endif
