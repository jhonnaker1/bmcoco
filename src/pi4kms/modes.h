//
// modes.h
//
// Video modes for the direct display driver's mode setting (stage 2), and
// what is needed from the display's EDID to choose one.
//
// Sources:
//  - CTA-861: the timings of the standard modes used (VIC 4, 16, 19, 31),
//    the pixel clock tolerance of +/-0.5 %, the AVI InfoFrame, the EDID
//    extension's video and HDMI vendor data blocks.
//  - VESA Enhanced EDID 1.4: the base block's header, checksum and
//    extension count.
//
// Pure computation, so tools/pi4kms can test it on a PC.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_PI4KMS_MODES_H_
#define BMCOCO_PI4KMS_MODES_H_

#include <stdint.h>

namespace kms {

struct Timing {
  uint8_t vic;                 // CTA-861 video identification code
  uint32_t pixel_hz;           // pixel (TMDS character) clock
  unsigned h_active, h_front, h_sync, h_back;
  unsigned v_active, v_front, v_sync, v_back;
  bool h_positive, v_positive; // sync polarities

  unsigned HTotal(void) const { return h_active + h_front + h_sync + h_back; }
  unsigned VTotal(void) const { return v_active + v_front + v_sync + v_back; }
  double Refresh(void) const { return (double)pixel_hz / HTotal() / VTotal(); }
};

// The CTA-861 timing for VIC, or 0 if it isn't one BMCoco knows.
const Timing *CtaMode(unsigned vic);

// The known mode with these active sizes and totals (as read back from the
// pixel valve), or 0.
const Timing *FindMode(unsigned h_active, unsigned h_total, unsigned v_active,
                       unsigned v_total);

// What the display says it takes.
struct Edid {
  bool valid;                  // a base block with a good checksum
  bool hdmi;                   // an HDMI sink (vendor block 00-0C-03)
  unsigned vic_count;
  uint8_t vics[64];

  bool Supports(unsigned vic) const;
};

// Parses BLOCKS 128-byte blocks of EDID.
void ParseEdid(const uint8_t *data, unsigned blocks, Edid *edid);

// The mode to run at FPS frames a second, with the resolution of CURRENT:
// its 50 Hz or 60 Hz sibling (whichever is nearer FPS), with the pixel
// clock moved (within CTA-861's 0.5 %) to make the refresh exactly FPS.
// When FPS is further off than that (a CoCo 3's 59.70 Hz, a PAL CoCo's
// 50.32 Hz), the sibling as it is: the emulator then locks to its 50 or 60
// Hz, a little off its own rate, as on a Pi 3. False if there is none, or
// the display doesn't list it.
bool ChooseMode(const Timing &current, double fps, const Edid &edid,
                Timing *chosen);

// The AVI InfoFrame for TIMING: RGB, limited range, 16:9; 17 bytes (3 of
// header, then the checksum and 13 of payload).
const unsigned AviLength = 17;
void AviInfoFrame(const Timing &timing, uint8_t frame[AviLength]);

// LENGTH bytes of an InfoFrame laid out as the HDMI packet RAM takes
// them: 7 bytes in each pair of words (3 then 4). Writes WORDS (9) words,
// the unused ones 0.
const unsigned PacketWords = 9;
void PacketRamWords(const uint8_t *frame, unsigned length, uint32_t words[PacketWords]);

// The PHY's settings for a TMDS character rate.
struct PhySettings {
  uint64_t vco_hz;
  unsigned vco_divider;
  bool vco_high;               // VCO select: above 4.5 GHz
  uint32_t rm_offset;          // rate manager offset
  unsigned icp;                // charge pump current
  unsigned vco_gain;
  // Per lane (0, 1, 2, clock): pre-emphasis, main driver, res_sel,
  // termination.
  unsigned preemphasis[4], main_driver[4], res_sel[4], termination[4];
};
// False above 340 MHz (no scrambling).
bool ComputePhy(uint64_t tmds_hz, PhySettings *phy);

// How HDMI PORT's PHY is wired: for each output (TX0, TX1, TX2, clock), the
// lane it carries, 3 being the clock lane. Each output takes that lane's
// PhySettings.
void PhyLanes(unsigned port, unsigned lanes[4]);
// CHANNEL_SWAP for that wiring: 2 bits per output at 1:0, 5:4, 9:8, 13:12.
uint32_t PhyChannelSwap(unsigned port);

// HDMI audio clock regeneration: CTS for N at fs samples a second
// (128 fs = f_TMDS N / CTS).
uint32_t AudioCts(uint64_t tmds_hz, uint32_t n, uint32_t fs);

} // namespace kms

#endif
