//
// logging.cpp
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for the
// Raspberry Pi. See LICENSE for licensing details.
//
// Based on BMC64's CLoggingDevice.
//

#include "logging.h"

#include <circle/timer.h>
#include <stdio.h>

// A log file bigger than this is started again at boot.
#define LOG_FILE_MAX (1024 * 1024)

CLogDevice::CLogDevice(CDevice *pSerial)
    : m_pSerial(pSerial), m_Destination(HOST_LOG_UART), m_pFile(0),
      m_QueueRead(0), m_QueueWrite(0), m_QueueLength(0), m_Dropped(0),
      m_LastSync(0), m_bUnsynced(false) {}

bool CLogDevice::SetDestination(int destination) {
  if (destination == m_Destination) {
    return true;
  }
  if (destination != HOST_LOG_FILE) {
    // Stop queueing first, then write out what is queued.
    m_Destination = destination;
    Close();
    return true;
  }
  Close();
  m_pFile = sd_open(LOG_FILE, SD_APPEND);
  if (m_pFile != 0 && sd_size(m_pFile) > LOG_FILE_MAX) {
    sd_close(m_pFile);
    m_pFile = sd_open(LOG_FILE, SD_WRITE);
  }
  if (m_pFile == 0) {
    m_Destination = HOST_LOG_OFF;
    return false;
  }
  m_LastSync = CTimer::GetClockTicks();
  m_Destination = HOST_LOG_FILE;
  return true;
}

int CLogDevice::Write(const void *pBuffer, size_t nCount) {
  int destination = m_Destination;
  if (destination == HOST_LOG_UART || destination == HOST_LOG_FILE) {
    m_pSerial->Write(pBuffer, nCount);
  }
  if (destination == HOST_LOG_FILE) {
    WriteFile(pBuffer, nCount);
  }
  return (int)nCount;
}

void CLogDevice::WriteFile(const void *pBuffer, size_t nCount) {
  if (m_Destination != HOST_LOG_FILE) {
    return;
  }
  const char *p = static_cast<const char *>(pBuffer);
  m_Lock.Acquire();
  for (size_t i = 0; i < nCount; i++) {
    if (m_QueueLength == QUEUE_SIZE) {
      m_Dropped += nCount - i;
      break;
    }
    m_Queue[m_QueueWrite] = p[i];
    m_QueueWrite = (m_QueueWrite + 1) % QUEUE_SIZE;
    m_QueueLength = m_QueueLength + 1;
  }
  m_Lock.Release();
}

void CLogDevice::Drain(void) {
  if (m_pFile == 0) {
    return;
  }
  if (m_QueueLength == 0 && m_Dropped == 0) {
    // Nothing more to write: what was written goes to the card half a
    // second later, so the last lines aren't lost at power off.
    if (m_bUnsynced && CTimer::GetClockTicks() - m_LastSync >= CLOCKHZ / 2) {
      if (sd_sync(m_pFile) == SD_OK) {
        m_bUnsynced = false;
      }
      m_LastSync = CTimer::GetClockTicks();
    }
    return;
  }

  m_Lock.Acquire();
  unsigned count = m_QueueLength;
  if (count > WRITE_SIZE) {
    count = WRITE_SIZE;
  }
  for (unsigned i = 0; i < count; i++) {
    m_WriteBuffer[i] = m_Queue[m_QueueRead];
    m_QueueRead = (m_QueueRead + 1) % QUEUE_SIZE;
  }
  m_QueueLength = m_QueueLength - count;
  unsigned dropped = count < WRITE_SIZE ? m_Dropped : 0;
  if (dropped != 0) {
    m_Dropped = 0;
  }
  m_Lock.Release();

  bool ok = count == 0 || sd_write(m_pFile, m_WriteBuffer, count) == SD_OK;
  if (ok && dropped != 0) {
    char note[64];
    int len = snprintf(note, sizeof note, "[%u bytes of log lost]\n", dropped);
    ok = sd_write(m_pFile, note, len) == SD_OK;
  }
  // Push it to the card every few seconds: the Pi can be switched off at
  // any time.
  m_bUnsynced = true;
  if (ok && CTimer::GetClockTicks() - m_LastSync >= 3 * CLOCKHZ) {
    ok = sd_sync(m_pFile) == SD_OK;
    m_bUnsynced = !ok;
    m_LastSync = CTimer::GetClockTicks();
  }
  if (!ok) {
    m_Destination = HOST_LOG_OFF;
    sd_close(m_pFile);
    m_pFile = 0;
  }
}

void CLogDevice::Flush(void) {
  while (m_pFile != 0 && (m_QueueLength != 0 || m_Dropped != 0)) {
    Drain();
  }
  if (m_pFile != 0 && sd_sync(m_pFile) == SD_OK) {
    m_LastSync = CTimer::GetClockTicks();
    m_bUnsynced = false;
  }
}

void CLogDevice::Close(void) {
  while (m_pFile != 0 && (m_QueueLength != 0 || m_Dropped != 0)) {
    Drain();
  }
  if (m_pFile != 0) {
    sd_close(m_pFile);
    m_pFile = 0;
  }
  m_Lock.Acquire();
  m_QueueRead = m_QueueWrite = 0;
  m_QueueLength = 0;
  m_Dropped = 0;
  m_Lock.Release();
}
