//
// vchiqsound.h
//
// Sends audio to HDMI or the headphone jack through the VideoCore (VCHIQ).
// Modified for BMA800XL from BMC64's ViceSound (vicesound): renamed, it
// takes the sample rate and buffer size from the emulator and is given
// whole frames of sound rather than fragments from VICE.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _vchiq_sound_h
#define _vchiq_sound_h

#include "vchiqsoundbasedevice.h"
#include <circle/types.h>
#include <vc4/vchiq/vchiqdevice.h>

// Number of 16 bit words sent to the VideoCore at a time. This is also the
// granularity of the "amount buffered" reports coming back.
#define VCHIQ_SOUND_CHUNK_WORDS 1024

class VchiqSound : private VchiqSoundBaseDevice {
public:
  VchiqSound(CVCHIQDevice *pVCHIQDevice, unsigned nSampleRate,
             TVCHIQSoundDestination Destination);
  ~VchiqSound(void);

  // Starts playback. CAPACITY_FRAMES is how much audio may be queued at the
  // VideoCore; it sets the output latency.
  boolean Playback(int volume, int channels, unsigned capacity_frames);
  boolean PlaybackActive(void) const;
  void CancelPlayback(void);
  void SetControl(int nVolume,
                  TVCHIQSoundDestination Destination = VCHIQSoundDestinationUnknown);

  // Queues FRAMES frames of interleaved 16 bit samples.
  void WriteFrames(const s16 *pBuffer, unsigned frames);

  // Frames that can be queued before reaching the capacity.
  unsigned FreeFrames(void) const;

private:
  unsigned GetChunk(s16 *pBuffer, unsigned nChunkSize);
  void AmountBufferedBytes(unsigned);

  volatile unsigned m_nBytesBuffered;
  const s16 *m_pSource;
  unsigned m_nSourceWords;
  unsigned m_nChannels;
  unsigned m_nCapacityFrames;
};

#endif
