//
// test.cpp
//
// Tests the shared Pi 4/5 ARM smooth scaling and CRT shader (src/av/scaler.*)
// on the PC:
//  - every output pixel of the fast path is compared with
//    Pi4Scaler::Reference(), crt-pi done the slow way in floating point;
//  - a few things that must hold whatever the details (a smooth plain
//    picture stays plain, scan lines are brightest on the line centres);
//  - how long a frame takes on one core of this PC;
//  - the pictures are written to tools/pi4scaler/out/ as PPM files.
//
// BMCoco's source is RGB565 (BMA800XL's is 8-bit indexed): the test card is
// drawn with BMA800XL's palette and converted, so the checks are the same.
//
//   tools/pi4scaler/run_tests.sh
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "../../src/av/scaler.h"

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// BMCoco's frame buffer (src/port/platform.c) and XRoar's usual picture.
static const int SrcW = 736, SrcH = 288;
static const int ViewX = 0, ViewY = 0, ViewW = 640, ViewH = 240;

static int g_failed = 0;

static void Check(bool ok, const char *name, const char *what) {
  printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name, what);
  if (!ok) {
    g_failed++;
  }
}

// A test palette: 16 hues x 16 luminances, hue 0 grey.
static uint32_t PaletteEntry(int i) {
  int hue = i >> 4, lum = i & 15;
  double y = lum / 15.0;
  double r = y, g = y, b = y;
  if (hue != 0) {
    double a = (hue - 1) * 2.0 * M_PI / 15.0;
    double s = 0.35 * (1.0 - fabs(y - 0.5));
    r = y + s * cos(a);
    g = y + s * cos(a - 2.1);
    b = y + s * cos(a + 2.1);
  }
  auto c = [](double v) {
    return (uint32_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0 + 0.5);
  };
  return c(r) << 16 | c(g) << 8 | c(b);
}

static uint16_t ToRgb565(uint32_t rgb) {
  return (uint16_t)((((rgb >> 16) & 0xFF) >> 3) << 11 | (((rgb >> 8) & 0xFF) >> 2) << 5 |
                    ((rgb & 0xFF) >> 3));
}

// An 8-bit picture, in the palette's colours, as RGB565.
static std::vector<uint16_t> Rgb565(const std::vector<uint8_t> &src) {
  std::vector<uint16_t> out(src.size());
  for (size_t i = 0; i < src.size(); i++) {
    out[i] = ToRgb565(PaletteEntry(src[i]));
  }
  return out;
}

// A test card: colour bars, one pixel wide lines, a checkerboard,
// alternate lines, a diagonal, and a ramp.
static void TestCard(std::vector<uint8_t> &src) {
  src.assign((size_t)SrcW * (SrcH + 1) + 1, 0);
  for (int y = 0; y < SrcH; y++) {
    for (int x = 0; x < SrcW; x++) {
      uint8_t c;
      if (y < 40) {
        c = (uint8_t)(((x / 24) & 15) << 4 | 10);
      } else if (y < 80) {
        c = (x % 8) == 0 ? 0x0F : 0x00;
      } else if (y < 120) {
        c = ((x + y) & 1) ? 0x0F : 0x00;
      } else if (y < 160) {
        c = (y & 1) ? 0x4C : 0x02;
      } else if (y < 200) {
        c = (abs(x - (y - 160) * 8) < 2) ? 0x0F : 0x94;
      } else {
        c = (uint8_t)(x * 16 / SrcW);
      }
      src[(size_t)y * SrcW + x] = c;
    }
  }
}

static void WritePPM(const char *path, const std::vector<uint32_t> &img,
                     unsigned w, unsigned h) {
  FILE *f = fopen(path, "wb");
  if (f == 0) {
    return;
  }
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (uint32_t p : img) {
    unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8),
                            (unsigned char)p};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void RenderFrame(Pi4Scaler &s, const uint16_t *src,
                        std::vector<uint32_t> &img) {
  for (unsigned i = 0; i < s.PrepareCount(); i++) {
    s.Prepare(src, i);
  }
  for (unsigned y = 0; y < s.Height(); y++) {
    s.Render(src, y, &img[(size_t)y * s.Width()]);
  }
  for (uint32_t &p : img) {
    p &= 0xFFFFFF;
  }
}

struct Mode {
  const char *name;
  bool crt, smooth, curvature;
};

static const Mode Modes[] = {
    {"smooth", false, true, false},
    {"crt", true, false, false},
    {"crt-smooth", true, true, false},
    {"crt-curved", true, false, true},
    {"crt-curved-smooth", true, true, true},
};

struct Size {
  unsigned w, h;
};

static const Size Sizes[] = {{1440, 1080}, {960, 720}, {1000, 777}, {320, 200}};

int main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : ".";
  std::vector<uint8_t> card;
  TestCard(card);
  std::vector<uint16_t> src = Rgb565(card);

  for (const Mode &m : Modes) {
    for (const Size &sz : Sizes) {
      char name[64];
      snprintf(name, sizeof name, "%s %ux%u", m.name, sz.w, sz.h);
      Pi4Scaler s;
      s.SetMode(m.crt, m.smooth, m.curvature);
      if (!s.Configure(SrcW, ViewX, ViewY, ViewW, ViewH, sz.w, sz.h)) {
        Check(false, name, "configure");
        continue;
      }
      std::vector<uint32_t> img((size_t)sz.w * sz.h);
      RenderFrame(s, src.data(), img);

      // Against the floating point crt-pi. See scaler.h: fixed point
      // weights, so a few levels either way.
      const int tolerance = m.curvature && m.smooth ? 4 : 3;
      // Sharp pixels on a curved screen: where a sample point falls exactly
      // on the edge between two texels, the fast path and the reference can
      // pick different ones, depending on the CPU's floating point (x86 and
      // ARM differ). Those few are counted, not failed.
      int maxd = 0, edge = 0;
      double sum = 0;
      for (unsigned y = 0; y < sz.h; y++) {
        for (unsigned x = 0; x < sz.w; x++) {
          uint32_t a = img[(size_t)y * sz.w + x];
          uint32_t b = s.Reference(src.data(), x, y);
          int pd = 0;
          for (int k = 0; k < 24; k += 8) {
            int d = abs((int)((a >> k) & 255) - (int)((b >> k) & 255));
            pd = d > pd ? d : pd;
            sum += d;
          }
          if (pd > tolerance && m.curvature && !m.smooth) {
            edge++;
          } else {
            maxd = pd > maxd ? pd : maxd;
          }
        }
      }
      char what[160];
      snprintf(what, sizeof what,
               "matches crt-pi in floating point (max %d, mean %.3f, %d texel edge pixel(s))",
               maxd, sum / (3.0 * sz.w * sz.h), edge);
      Check(maxd <= tolerance && edge <= 8, name, what);

      if (sz.w == 1440) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s.ppm", out, m.name);
        WritePPM(path, img, sz.w, sz.h);

        // Speed, one core of this PC.
        const int frames = 20;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; i++) {
          RenderFrame(s, src.data(), img);
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
        printf("TIME  %s: %.2f ms a frame on one core of this PC\n", name, ms);
      }
    }
  }

  // A plain picture stays plain when smoothed, and is untouched by the
  // scan lines' weights where they are 1.
  {
    std::vector<uint16_t> plain((size_t)SrcW * (SrcH + 1) + 1, ToRgb565(PaletteEntry(0x4C)));
    Pi4Scaler s;
    s.SetMode(false, true, false);
    s.Configure(SrcW, ViewX, ViewY, ViewW, ViewH, 1000, 777);
    std::vector<uint32_t> img((size_t)1000 * 777);
    RenderFrame(s, plain.data(), img);
    bool ok = true;
    for (uint32_t p : img) {
      ok = ok && p == (Pi4Rgb565ToArgb(plain[0]) & 0xFFFFFF);
    }
    Check(ok, "smooth plain", "a plain picture stays the same colour");
  }

  // Scan lines on white, 1080 lines (4.5 per emulated line): brightest on the
  // line centres (clamped to white), darkest between them, never black.
  {
    std::vector<uint16_t> white((size_t)SrcW * (SrcH + 1) + 1, 0xFFFF);
    Pi4Scaler s;
    s.SetMode(true, false, false);
    s.Configure(SrcW, ViewX, ViewY, ViewW, ViewH, 1440, 1080);
    std::vector<uint32_t> img((size_t)1440 * 1080);
    RenderFrame(s, white.data(), img);
    unsigned lo = 255, hi = 0;
    bool centre_white = true;
    for (unsigned y = 0; y < 1080; y++) {
      unsigned v = img[(size_t)y * 1440 + 700] & 255;
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
      double py = (y + 0.5) * ViewH / 1080.0;
      if (fabs(py - (floor(py) + 0.5)) < 0.05 && v != 255) {
        centre_white = false;
      }
    }
    char what[128];
    snprintf(what, sizeof what, "white scan lines from %u to %u", lo, hi);
    Check(hi == 255 && lo > 60 && lo < 200 && centre_white, "crt scan lines", what);
  }

  // Curvature leaves the corners black.
  {
    std::vector<uint16_t> white((size_t)SrcW * (SrcH + 1) + 1, 0xFFFF);
    Pi4Scaler s;
    s.SetMode(true, false, true);
    s.Configure(SrcW, ViewX, ViewY, ViewW, ViewH, 1440, 1080);
    std::vector<uint32_t> img((size_t)1440 * 1080);
    RenderFrame(s, white.data(), img);
    bool ok = img[0] == 0 && img[1439] == 0 && img[(size_t)1079 * 1440] == 0 &&
              (img[(size_t)540 * 1440 + 720] & 255) > 0;
    Check(ok, "crt curvature", "corners are black, the centre isn't");
  }

  if (g_failed) {
    printf("\n%d test(s) failed.\n", g_failed);
    return 1;
  }
  printf("\nAll tests passed.\n");
  return 0;
}
