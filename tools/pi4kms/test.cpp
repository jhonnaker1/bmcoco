//
// test.cpp
//
// Tests the parts of the Pi 4's direct display driver (src/kms/pi4/) that
// are pure calculation, on a PC, against the values they must produce. The
// register writes themselves need a Pi 4.
//
//   tools/pi4kms/run_tests.sh
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "../../src/kms/pi4/dlist.h"
#include "../../src/kms/common/filter.h"
#include "../../src/kms/pi4/regs.h"
#include "tests.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_failed = 0;

void Check(bool ok, const char *name, const char *what) {
  printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name, what);
  if (!ok) {
    g_failed++;
  }
}

static void TestKernel(kms::Filter filter, const char *name) {
  int c[32];
  kms::KernelCoefficients(filter, c);
  bool symmetric = true, in_range = true, sums = true;
  for (int i = 0; i < 32; i++) {
    symmetric = symmetric && c[i] == c[31 - i];
    in_range = in_range && c[i] >= -256 && c[i] <= 255;
  }
  for (int p = 0; p < 8; p++) {
    sums = sums && c[p] + c[p + 8] + c[p + 16] + c[p + 24] == 256;
  }
  Check(symmetric, name, "symmetric");
  Check(in_range, name, "coefficients fit 9 signed bits");
  Check(sums, name, "each phase's four taps add up to 256");

  uint32_t words[11];
  kms::KernelWords(filter, words);
  bool packed = true;
  for (int w = 0; w < 5; w++) {
    for (int k = 0; k < 3; k++) {
      int v = (int)((words[w] >> (9 * k)) & 0x1FF);
      if (v & 0x100) {
        v -= 0x200;
      }
      packed = packed && v == c[3 * w + k];
    }
  }
  packed = packed && (words[5] & 0x1FF) == ((uint32_t)c[15] & 0x1FF) &&
           ((words[5] >> 9) & 0x1FF) == ((uint32_t)c[15] & 0x1FF) &&
           (words[5] >> 18) == 0;
  for (int w = 6; w < 11; w++) {
    packed = packed && words[w] == words[10 - w];
  }
  Check(packed, name, "packed 3 per word, words 6-10 mirroring 4-0");
}

static void TestKernels(void) {
  TestKernel(kms::Filter::Smooth, "kernel smooth");
  TestKernel(kms::Filter::Sharp, "kernel sharp");

  int c[32];
  kms::KernelCoefficients(kms::Filter::Smooth, c);
  // Mitchell-Netravali at 1/16 pixel: (7/6 x^3 - 2 x^2 + 8/9) * 256.
  double x = 1.0 / 16;
  double k = ((7.0 * x * x * x - 12.0 * x * x + 16.0 / 3.0) / 6.0) * 256.0;
  char what[96];
  snprintf(what, sizeof what, "centre tap %d, Mitchell-Netravali %.1f", c[15], k);
  Check(abs(c[15] - (int)lround(k)) <= 3, "kernel smooth", what);
  bool negative_lobes = c[4] < 0 && c[5] < 0;
  Check(negative_lobes, "kernel smooth", "negative lobes 1 to 2 pixels out");

  kms::KernelCoefficients(kms::Filter::Sharp, c);
  bool box = true;
  for (int p = 0; p < 8; p++) {
    int big = 0, one = 0, zero = 0;
    for (int t = 0; t < 4; t++) {
      int v = c[p + 8 * t];
      big += v == 255;
      one += v == 1;
      zero += v == 0;
    }
    box = box && big == 1 && one == 1 && zero == 2;
  }
  Check(box, "kernel sharp", "one tap of 255 (and its neighbour 1) per phase");
}

static uint32_t Bits(uint32_t word, unsigned high, unsigned low) {
  return kms::GetField(word, high, low);
}

static void TestDlist(void) {
  using namespace kms;
  uint32_t w[MaxWords];

  // Unity: no scaling words.
  Plane unity = {0xC1000000, 1920 * 4, 1920, 1080, 0, 0, 1920, 1080, false};
  unsigned n = BuildList(unity, 0, 0, w);
  Check(n == 9, "dlist unity", "8 words and the end word");
  Check(Bits(w[0], 30, 30) == 1 && Bits(w[0], 29, 24) == 8 &&
            Bits(w[0], 15, 15) == 1 && Bits(w[0], 4, 0) == 7 &&
            Bits(w[0], 14, 13) == 2 && Bits(w[0], 12, 11) == 3,
        "dlist unity", "control: valid, size 8, unity, RGBA8888, ARGB, expand");
  Check(w[2] == ((1u << 30) | (0xFFFu << 4)), "dlist unity", "fixed opaque alpha");
  Check(w[3] == (1920u | (1080u << 16)), "dlist unity", "source size");
  Check(w[4] == 0xC0C0C0C0 && w[5] == 0xC1000000 && w[6] == 0xC0C0C0C0 &&
            w[7] == 1920 * 4,
        "dlist unity", "context, pointer, context, pitch");
  Check(w[8] == 0x80000000, "dlist unity", "end word");
  Check(LbmWords(unity) == 0, "dlist unity", "no line buffer memory");

  // An Atari view, 336 x 240, sharp, 3 lines per line at 720p.
  Plane sharp = {0xC2000000, 336 * 4, 336, 240, 160, 0, 960, 720, true};
  n = BuildList(sharp, 32768, 3083, w);
  Check(n == 18, "dlist sharp", "17 words and the end word");
  Check(Bits(w[0], 29, 24) == 17 && Bits(w[0], 15, 15) == 0 &&
            Bits(w[0], 7, 5) == 0 && Bits(w[0], 10, 8) == 0,
        "dlist sharp", "control: size 17, scaled, PPF both ways in both channels");
  Check(w[1] == (160u | (0u << 16)), "dlist sharp", "position");
  Check(w[3] == (960u | (720u << 16)), "dlist sharp", "size on screen");
  Check(w[4] == (336u | (240u << 16)), "dlist sharp", "source size");
  Check(w[9] == 32768, "dlist sharp", "line buffer memory address");
  // Step 336/960 = 0.35 in 16.16: 22937. Phase -32/64 plus half the step
  // (11/64) = -21/64; the error (576/65536 of a pixel) adds nothing.
  uint32_t hppf = w[10];
  Check(Bits(hppf, 31, 31) == 1 && Bits(hppf, 30, 30) == 1 &&
            Bits(hppf, 24, 8) == 22937 && Bits(hppf, 6, 0) == 0x6B,
        "dlist sharp", "horizontal PPF: no interpolation, AGC, step, phase -21/64");
  // Step 1/3: 21845, phase -32/64 plus 10/64 = -22/64.
  uint32_t vppf = w[11];
  Check(Bits(vppf, 24, 8) == 21845 && Bits(vppf, 6, 0) == 0x6A && w[12] == 0xC0C0C0C0,
        "dlist sharp", "vertical PPF (step 1/3) and its context word");
  Check(w[13] == 3083 && w[14] == 3083 && w[15] == 3083 && w[16] == 3083,
        "dlist sharp", "four kernel words");
  Check(w[17] == 0x80000000, "dlist sharp", "end word");
  // 336 x 16 = 5376 bytes, a multiple of 128; / 4 = 1344 words; already a
  // multiple of 64.
  Check(LbmWords(sharp) == 1344, "dlist sharp", "line buffer memory 1344 words");

  // Smooth: no "no interpolation" bit.
  Plane smooth = sharp;
  smooth.nearest = false;
  BuildList(smooth, 32768, 3072, w);
  Check(Bits(w[10], 31, 31) == 0 && Bits(w[11], 31, 31) == 0 && w[13] == 3072,
        "dlist smooth", "interpolating PPF, smooth kernel");

  // Only vertical scaling: mode 5, no horizontal PPF word.
  Plane vertical = {0xC3000000, 1280 * 4, 1280, 360, 0, 0, 1280, 720, true};
  n = BuildList(vertical, 100, 7, w);
  Check(Bits(w[0], 7, 5) == 5 && Bits(w[0], 10, 8) == 5 && n == 17,
        "dlist vertical", "vertical PPF only: mode 5, 16 words");

  // Shrinking to less than 2/3 can't be done with the PPF.
  Plane shrink = {0xC4000000, 1920 * 4, 1920, 1080, 0, 0, 640, 360, true};
  Check(BuildList(shrink, 0, 0, w) == 0, "dlist shrink", "refused");
}

int main(void) {
  TestKernels();
  TestDlist();
  RunModeTests();
  if (g_failed) {
    printf("\n%d test(s) failed.\n", g_failed);
    return 1;
  }
  printf("\nAll tests passed.\n");
  return 0;
}
