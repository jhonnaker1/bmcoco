//
// test_modes.cpp
//
// Tests of the direct display driver's mode setting calculations (stage 2,
// src/pi4kms/modes.*): EDID, choosing the mode, the AVI InfoFrame, the
// PHY's settings and the audio clock.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "../../src/pi4kms/modes.h"
#include "tests.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace kms;

// BMA800XL's (atari800's) frame rates: PAL 312 lines of 114 cycles at 1.773447 MHz,
// NTSC 262 lines at 1.789790 MHz.
static const double PalFps = 1773447.0 / 114 / 312;
static const double NtscFps = 1789790.0 / 114 / 262;
// XRoar's (BMCoco's): video lines of 912 ticks of its 14.31818 MHz clock,
// 262 a frame for an NTSC CoCo 1/2, 263 for a CoCo 3, 312 for a PAL CoCo.
static const double CocoFps = 14318180.0 / 912 / 262;
static const double Coco3Fps = 14318180.0 / 912 / 263;
static const double CocoPalFps = 14318180.0 / 912 / 312;

static void Checksum(uint8_t *block) {
  uint8_t sum = 0;
  for (int i = 0; i < 127; i++) {
    sum = (uint8_t)(sum + block[i]);
  }
  block[127] = (uint8_t)(0x100 - sum);
}

// A base block and a CTA extension listing VICS, with an HDMI vendor
// block if HDMI.
static void MakeEdid(uint8_t *edid, const uint8_t *vics, unsigned count, bool hdmi) {
  memset(edid, 0, 256);
  static const uint8_t header[8] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
  memcpy(edid, header, 8);
  edid[18] = 1;     // EDID 1.3
  edid[19] = 3;
  edid[126] = 1;    // one extension
  Checksum(edid);

  uint8_t *ext = edid + 128;
  ext[0] = 0x02;    // CTA extension
  ext[1] = 3;
  unsigned i = 4;
  ext[i++] = (uint8_t)((2 << 5) | count);   // video data block
  for (unsigned k = 0; k < count; k++) {
    ext[i++] = vics[k];
  }
  if (hdmi) {
    ext[i++] = (3 << 5) | 5;                // vendor block, 5 bytes
    ext[i++] = 0x03;
    ext[i++] = 0x0C;
    ext[i++] = 0x00;
    ext[i++] = 0x10;                        // physical address 1.0.0.0
    ext[i++] = 0x00;
  }
  ext[2] = (uint8_t)i;                      // no detailed timings follow
  Checksum(ext);
}

static void TestEdid(void) {
  uint8_t edid[256];
  const uint8_t vics[] = {0x80 | 16, 31, 4, 19, 2};
  MakeEdid(edid, vics, 5, true);
  Edid e;
  ParseEdid(edid, 2, &e);
  Check(e.valid && e.hdmi, "edid", "valid, HDMI");
  Check(e.vic_count == 5 && e.Supports(16) && e.Supports(31) && !e.Supports(17),
        "edid", "video data block, native bit ignored");

  MakeEdid(edid, vics, 5, false);
  ParseEdid(edid, 2, &e);
  Check(e.valid && !e.hdmi, "edid", "no HDMI vendor block: DVI");

  edid[20] ^= 1;    // breaks the base block's checksum
  ParseEdid(edid, 2, &e);
  Check(!e.valid, "edid", "bad checksum refused");
}

static void TestChooseMode(void) {
  uint8_t data[256];
  const uint8_t vics[] = {16, 31, 4, 19};
  MakeEdid(data, vics, 4, true);
  Edid edid;
  ParseEdid(data, 2, &edid);

  Timing chosen;
  char what[128];
  bool ok = ChooseMode(*CtaMode(16), PalFps, edid, &chosen);
  snprintf(what, sizeof what, "1080p60 -> VIC %u, %u Hz pixel clock, %.5f Hz",
           chosen.vic, chosen.pixel_hz, chosen.Refresh());
  Check(ok && chosen.vic == 31 && fabs(chosen.Refresh() / PalFps - 1.0) < 1e-6,
        "mode PAL", what);
  Check(chosen.pixel_hz > 148500000 * 0.995 && chosen.pixel_hz < 148500000,
        "mode PAL", "pixel clock within 0.5 % below 148.5 MHz");

  ok = ChooseMode(*CtaMode(31), NtscFps, edid, &chosen);
  snprintf(what, sizeof what, "1080p50 -> VIC %u, %u Hz pixel clock, %.5f Hz",
           chosen.vic, chosen.pixel_hz, chosen.Refresh());
  Check(ok && chosen.vic == 16 && fabs(chosen.Refresh() / NtscFps - 1.0) < 1e-6,
        "mode NTSC", what);

  ok = ChooseMode(*CtaMode(4), PalFps, edid, &chosen);
  Check(ok && chosen.vic == 19, "mode 720p", "720p60 -> 720p50 for PAL");

  // The CoCo 2 (59.92 Hz) is within CTA-861's 0.5 %; the CoCo 3 (59.70 Hz)
  // and a PAL CoCo (50.32 Hz) are just outside, so they get the standard
  // 60 or 50 Hz mode, which BMCoco locks to (as on a Pi 3), even from the
  // other rate.
  ok = ChooseMode(*CtaMode(4), CocoFps, edid, &chosen);
  snprintf(what, sizeof what, "720p60 -> VIC %u, %.5f Hz", chosen.vic, chosen.Refresh());
  Check(ok && chosen.vic == 4 && fabs(chosen.Refresh() / CocoFps - 1.0) < 1e-6,
        "mode CoCo", what);
  ok = ChooseMode(*CtaMode(4), Coco3Fps, edid, &chosen);
  Check(ok && chosen.vic == 4 && chosen.pixel_hz == CtaMode(4)->pixel_hz, "mode CoCo 3",
        "59.70 Hz: 720p60 at its own clock");
  ok = ChooseMode(*CtaMode(4), CocoPalFps, edid, &chosen);
  Check(ok && chosen.vic == 19 && chosen.pixel_hz == CtaMode(19)->pixel_hz, "mode CoCo PAL",
        "50.32 Hz from 720p60: 720p50 at its own clock");
  ok = ChooseMode(*CtaMode(16), CocoPalFps, edid, &chosen);
  Check(ok && chosen.vic == 31 && chosen.pixel_hz == CtaMode(31)->pixel_hz, "mode CoCo PAL",
        "50.32 Hz from 1080p60: 1080p50 at its own clock");

  const uint8_t only60[] = {16, 4};
  MakeEdid(data, only60, 2, true);
  ParseEdid(data, 2, &edid);
  Check(!ChooseMode(*CtaMode(16), PalFps, edid, &chosen), "mode PAL",
        "refused when the display doesn't list 1080p50");

  Check(FindMode(1920, 2640, 1080, 1125) == CtaMode(31) &&
            FindMode(1280, 1650, 720, 750) == CtaMode(4) &&
            FindMode(1024, 1344, 768, 806) == 0,
        "mode find", "known timings recognised, others not");
}

static void TestInfoFrame(void) {
  uint8_t frame[AviLength];
  AviInfoFrame(*CtaMode(31), frame);
  uint8_t sum = 0;
  for (unsigned i = 0; i < AviLength; i++) {
    sum = (uint8_t)(sum + frame[i]);
  }
  Check(sum == 0, "avi", "checksum");
  Check(frame[0] == 0x82 && frame[1] == 2 && frame[2] == 13, "avi",
        "header: type 0x82, version 2, 13 bytes");
  Check(frame[4] == 0x10 && frame[5] == 0x28 && frame[6] == 0x04 && frame[7] == 31,
        "avi", "RGB, 16:9, limited range, VIC 31");

  uint32_t words[PacketWords];
  PacketRamWords(frame, AviLength, words);
  Check(words[0] == 0x000D0282, "avi", "packet RAM: header in the first word's 3 bytes");
  Check(words[1] == ((uint32_t)frame[3] | 0x10u << 8 | 0x28u << 16 | 0x04u << 24),
        "avi", "packet RAM: next 4 bytes");
  Check(words[2] == 31 && words[8] == 0, "avi", "packet RAM: 3 bytes, 4 bytes, ...");
}

static void TestPhy(void) {
  PhySettings phy;
  Check(ComputePhy(148500000, &phy) && phy.vco_divider == 3 &&
            phy.vco_hz == 4455000000ull && !phy.vco_high,
        "phy 148.5", "VCO 4.455 GHz, divider 3");
  // 4.455 GHz x 2 x 2^22 / 54 MHz / 4 = 4.455e9 x 2^21 / 54e6.
  double offset = 4455000000.0 * 2097152.0 / 54000000.0;
  Check(phy.rm_offset == (uint32_t)offset, "phy 148.5", "rate manager offset");
  Check(phy.icp == 0x18 && phy.vco_gain == 5, "phy 148.5",
        "charge pump 0x18, VCO gain 5");
  Check(phy.main_driver[0] == 0x09 && phy.res_sel[0] == 0x12 &&
            phy.termination[0] == 0 && phy.main_driver[3] == 0x0C &&
            phy.res_sel[3] == 0x18 && phy.termination[3] == 3,
        "phy 148.5", "75-165 MHz lane settings");

  Check(ComputePhy(74250000, &phy) && phy.vco_divider == 5 && phy.vco_gain == 6 &&
            phy.icp == 0x18,
        "phy 74.25", "VCO 3.7125 GHz, divider 5, gain 6");
  Check(ComputePhy(25175000, &phy) && phy.vco_divider == 12 && phy.main_driver[0] == 0x0A,
        "phy 25.175", "divider 12, slowest band");
  Check(!ComputePhy(594000000, &phy), "phy 594", "refused (needs scrambling)");
}

static void TestPhyLanes(void) {
  unsigned lanes[4];
  PhyLanes(0, lanes);
  Check(lanes[0] == 0 && lanes[1] == 1 && lanes[2] == 2 && lanes[3] == 3, "phy lanes",
        "HDMI0 in order");
  Check(PhyChannelSwap(0) == 0x3210, "phy lanes", "HDMI0 CHANNEL_SWAP 0x3210");
  PhyLanes(1, lanes);
  Check(lanes[0] == 1 && lanes[1] == 0 && lanes[2] == 3 && lanes[3] == 2, "phy lanes",
        "HDMI1: lane 1, lane 0, clock, lane 2");
  Check(PhyChannelSwap(1) == 0x2301, "phy lanes", "HDMI1 CHANNEL_SWAP 0x2301");
  // HDMI1's TX2 output carries the clock, so takes the clock lane's
  // settings; its clock output carries lane 2, a data lane.
  PhySettings phy;
  ComputePhy(148500000, &phy);
  Check(phy.main_driver[lanes[2]] == 0x0C && phy.res_sel[lanes[2]] == 0x18 &&
            phy.main_driver[lanes[3]] == 0x09 && phy.res_sel[lanes[3]] == 0x12,
        "phy lanes", "HDMI1 TX2 gets the clock's settings, the clock output a data lane's");
}

static void TestAudio(void) {
  // HDMI's recommended pair for 148.5 MHz at 48 kHz: N 6144, CTS 148500.
  Check(AudioCts(148500000, 6144, 48000) == 148500, "audio", "CTS 148500 at 148.5 MHz");
  Check(AudioCts(74250000, 6144, 48000) == 74250, "audio", "CTS 74250 at 74.25 MHz");
}

void RunModeTests(void) {
  TestEdid();
  TestChooseMode();
  TestInfoFrame();
  TestPhy();
  TestPhyLanes();
  TestAudio();
}
