//
// modes.cpp
//
// See modes.h.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#include "modes.h"

#include <string.h>

namespace kms {

namespace {

// CTA-861 modes BMCoco switches between: 720p and 1080p, at 60 and
// 50 Hz.
const Timing Modes[] = {
    {4, 74250000, 1280, 110, 40, 220, 720, 5, 5, 20, true, true},
    {19, 74250000, 1280, 440, 40, 220, 720, 5, 5, 20, true, true},
    {16, 148500000, 1920, 88, 44, 148, 1080, 4, 5, 36, true, true},
    {31, 148500000, 1920, 528, 44, 148, 1080, 4, 5, 36, true, true},
};

} // namespace

const Timing *CtaMode(unsigned vic) {
  for (const Timing &t : Modes) {
    if (t.vic == vic) {
      return &t;
    }
  }
  return 0;
}

const Timing *FindMode(unsigned h_active, unsigned h_total, unsigned v_active,
                       unsigned v_total) {
  for (const Timing &t : Modes) {
    if (t.h_active == h_active && t.HTotal() == h_total &&
        t.v_active == v_active && t.VTotal() == v_total) {
      return &t;
    }
  }
  return 0;
}

bool Edid::Supports(unsigned vic) const {
  for (unsigned i = 0; i < vic_count; i++) {
    if (vics[i] == vic) {
      return true;
    }
  }
  return false;
}

void ParseEdid(const uint8_t *data, unsigned blocks, Edid *edid) {
  memset(edid, 0, sizeof *edid);
  static const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  if (blocks == 0 || memcmp(data, header, 8) != 0) {
    return;
  }
  uint8_t sum = 0;
  for (unsigned i = 0; i < 128; i++) {
    sum = (uint8_t)(sum + data[i]);
  }
  if (sum != 0) {
    return;
  }
  edid->valid = true;

  unsigned extensions = data[126];
  for (unsigned b = 1; b <= extensions && b < blocks; b++) {
    const uint8_t *ext = data + 128 * b;
    uint8_t ext_sum = 0;
    for (unsigned i = 0; i < 128; i++) {
      ext_sum = (uint8_t)(ext_sum + ext[i]);
    }
    if (ext[0] != 0x02 || ext_sum != 0) {
      continue;   // not a (good) CTA extension
    }
    // Data blocks from byte 4 up to the offset in byte 2: a header byte
    // (tag in bits 7:5, length in 4:0), then the payload.
    unsigned end = ext[2];
    if (end < 4 || end > 127) {
      continue;
    }
    for (unsigned i = 4; i < end;) {
      unsigned tag = ext[i] >> 5, length = ext[i] & 0x1F;
      if (i + 1 + length > end) {
        break;
      }
      const uint8_t *payload = ext + i + 1;
      if (tag == 2) {
        // Video data block: VICs, low 7 bits (bit 7 marks "native").
        for (unsigned k = 0; k < length && edid->vic_count < 64; k++) {
          edid->vics[edid->vic_count++] = payload[k] & 0x7F;
        }
      } else if (tag == 3 && length >= 3 && payload[0] == 0x03 &&
                 payload[1] == 0x0C && payload[2] == 0x00) {
        // Vendor specific data block, IEEE OUI 00-0C-03 (stored least
        // significant byte first): HDMI.
        edid->hdmi = true;
      }
      i += 1 + length;
    }
  }
}

bool ChooseMode(const Timing &current, double fps, const Edid &edid,
                Timing *chosen) {
  if (fps < 45.0 || fps > 65.0) {
    return false;
  }
  bool fifty = fps < 55.0;
  const Timing *base = 0;
  for (const Timing &t : Modes) {
    bool t_fifty = t.Refresh() < 55.0;
    if (t.h_active == current.h_active && t.v_active == current.v_active &&
        t_fifty == fifty) {
      base = &t;
    }
  }
  if (base == 0 || !edid.valid || !edid.Supports(base->vic)) {
    return false;
  }
  *chosen = *base;
  double clock = (double)base->HTotal() * base->VTotal() * fps;
  // CTA-861 allows the pixel clock to be 0.5 % off; further than that, the
  // mode keeps its own clock.
  if (clock >= base->pixel_hz * 0.995 && clock <= base->pixel_hz * 1.005) {
    chosen->pixel_hz = (uint32_t)(clock + 0.5);
  }
  return true;
}

void AviInfoFrame(const Timing &timing, uint8_t frame[AviLength]) {
  memset(frame, 0, AviLength);
  frame[0] = 0x82;   // AVI InfoFrame
  frame[1] = 2;      // version
  frame[2] = 13;     // payload length
  // PB1: Y = 0 (RGB), A0 = 1 (active format present), no bars, S = 0.
  frame[4] = 0x10;
  // PB2: C = 0 (no colorimetry data), M = 2 (16:9), R = 8 (as the picture).
  frame[5] = (2 << 4) | 8;
  // PB3: Q = 1 (limited range RGB).
  frame[6] = 1 << 2;
  // PB4: the VIC.
  frame[7] = timing.vic;
  // Checksum: all 17 bytes add up to 0.
  uint8_t sum = 0;
  for (unsigned i = 0; i < AviLength; i++) {
    sum = (uint8_t)(sum + frame[i]);
  }
  frame[3] = (uint8_t)(0x100 - sum);
}

void PacketRamWords(const uint8_t *frame, unsigned length, uint32_t words[PacketWords]) {
  for (unsigned i = 0; i < PacketWords; i++) {
    words[i] = 0;
  }
  uint8_t bytes[28] = {0};
  for (unsigned i = 0; i < length && i < sizeof bytes; i++) {
    bytes[i] = frame[i];
  }
  unsigned w = 0;
  for (unsigned i = 0; i + 7 <= sizeof bytes && w + 1 < PacketWords; i += 7) {
    words[w++] = bytes[i] | bytes[i + 1] << 8 | bytes[i + 2] << 16;
    words[w++] = bytes[i + 3] | bytes[i + 4] << 8 | bytes[i + 5] << 16 |
                 (uint32_t)bytes[i + 6] << 24;
  }
}

bool ComputePhy(uint64_t tmds_hz, PhySettings *phy) {
  if (tmds_hz == 0 || tmds_hz > 340000000ull) {
    return false;
  }
  // VCO: f x 10 x d, the smallest d reaching 3 GHz.
  unsigned d = 1;
  while (tmds_hz * 10 * d < 3000000000ull) {
    d++;
  }
  phy->vco_divider = d;
  phy->vco_hz = tmds_hz * 10 * d;
  phy->vco_high = phy->vco_hz > 4500000000ull;
  // Rate manager offset: VCO x 2 x 2^22 / 54 MHz, then / 4.
  phy->rm_offset = (uint32_t)(((phy->vco_hz * 2) << 22) / 54000000ull >> 2);
  phy->icp = phy->vco_hz < 3700000000ull ? 0x1C : 0x18;
  if (phy->vco_hz < 3350000000ull) {
    phy->vco_gain = 0xF;
  } else if (phy->vco_hz < 3700000000ull) {
    phy->vco_gain = 0xC;
  } else if (phy->vco_hz < 4050000000ull) {
    phy->vco_gain = 0x6;
  } else if (phy->vco_hz < 4800000000ull) {
    phy->vco_gain = 0x5;
  } else if (phy->vco_hz < 5200000000ull) {
    phy->vco_gain = 0x7;
  } else {
    phy->vco_gain = 0x2;
  }

  // Lane drive settings by rate band.
  struct Band {
    uint64_t max_hz;
    unsigned data[4];   // pre-emphasis, main driver, res_sel, termination
    unsigned clock[4];
  };
  static const Band bands[] = {
      {50000000, {0, 0x0A, 0x12, 0}, {0, 0x0A, 0x18, 0}},
      {75000000, {0, 0x09, 0x12, 0}, {0, 0x0C, 0x18, 3}},
      {165000000, {0, 0x09, 0x12, 0}, {0, 0x0C, 0x18, 3}},
      {250000000, {0, 0x0F, 0x12, 1}, {0, 0x0C, 0x18, 3}},
      {340000000, {2, 0x0D, 0x12, 1}, {0, 0x0C, 0x18, 0xF}},
  };
  const Band *band = &bands[0];
  for (const Band &b : bands) {
    band = &b;
    if (tmds_hz <= b.max_hz) {
      break;
    }
  }
  for (unsigned lane = 0; lane < 4; lane++) {
    const unsigned *s = lane < 3 ? band->data : band->clock;
    phy->preemphasis[lane] = s[0];
    phy->main_driver[lane] = s[1];
    phy->res_sel[lane] = s[2];
    phy->termination[lane] = s[3];
  }
  return true;
}

void PhyLanes(unsigned port, unsigned lanes[4]) {
  static const unsigned wiring[2][4] = {
      {0, 1, 2, 3},   // HDMI0: in order
      {1, 0, 3, 2},   // HDMI1: lane 1, lane 0, clock, lane 2
  };
  for (unsigned out = 0; out < 4; out++) {
    lanes[out] = wiring[port != 0][out];
  }
}

uint32_t PhyChannelSwap(unsigned port) {
  unsigned lanes[4];
  PhyLanes(port, lanes);
  return lanes[0] | lanes[1] << 4 | lanes[2] << 8 | lanes[3] << 12;
}

uint32_t AudioCts(uint64_t tmds_hz, uint32_t n, uint32_t fs) {
  if (fs == 0) {
    return 0;
  }
  return (uint32_t)((tmds_hz * n + 64ull * fs) / (128ull * fs));
}

} // namespace kms
