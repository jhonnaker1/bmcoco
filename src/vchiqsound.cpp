//
// vchiqsound.cpp
//
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

#include "vchiqsound.h"

#include <assert.h>
#include <string.h>

#include <circle/sched/scheduler.h>

VchiqSound::VchiqSound(CVCHIQDevice *pVCHIQDevice, unsigned nSampleRate,
                       TVCHIQSoundDestination Destination)
    : VchiqSoundBaseDevice(pVCHIQDevice, nSampleRate, VCHIQ_SOUND_CHUNK_WORDS,
                           Destination),
      m_nBytesBuffered(0), m_pSource(0), m_nSourceWords(0), m_nChannels(1),
      m_nCapacityFrames(2048) {}

VchiqSound::~VchiqSound(void) {}

boolean VchiqSound::Playback(int volume, int channels,
                             unsigned capacity_frames) {
  assert(!IsActive());
  m_nChannels = channels;
  m_nCapacityFrames = capacity_frames;
  SetVolume(volume);
  SetChannels(channels);
  return Start();
}

boolean VchiqSound::PlaybackActive(void) const { return IsActive(); }

void VchiqSound::CancelPlayback(void) { Cancel(); }

void VchiqSound::SetControl(int nVolume, TVCHIQSoundDestination Destination) {
  VchiqSoundBaseDevice::SetControl(nVolume, Destination);
}

void VchiqSound::WriteFrames(const s16 *pBuffer, unsigned frames) {
  unsigned words = frames * m_nChannels;
  m_pSource = pBuffer;
  while (words > 0) {
    // Each WriteChunk() call pulls one chunk through GetChunk() below.
    m_nSourceWords = words > VCHIQ_SOUND_CHUNK_WORDS ? VCHIQ_SOUND_CHUNK_WORDS
                                                     : words;
    WriteChunk();
    m_pSource += m_nSourceWords;
    words -= m_nSourceWords;
  }
  m_pSource = 0;
  m_nSourceWords = 0;
}

unsigned VchiqSound::GetChunk(s16 *pBuffer, unsigned nChunkSize) {
  assert(pBuffer != 0);
  assert(nChunkSize > 0);

  if (m_pSource == 0 || m_nSourceWords == 0) {
    // Playback is starting: prime the VideoCore with a little silence.
    unsigned words = 256 * m_nChannels;
    memset(pBuffer, 0, words * sizeof(s16));
    return words;
  }

  memcpy(pBuffer, m_pSource, m_nSourceWords * sizeof(s16));
  return m_nSourceWords;
}

// Called from the VCHIQ callback with how many bytes are still queued.
void VchiqSound::AmountBufferedBytes(unsigned nBytes) {
  m_nBytesBuffered = nBytes;
}

unsigned VchiqSound::FreeFrames(void) const {
  unsigned buffered = m_nBytesBuffered / sizeof(s16) / m_nChannels;
  return buffered >= m_nCapacityFrames ? 0 : m_nCapacityFrames - buffered;
}
