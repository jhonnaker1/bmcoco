//
// logging.h
//
// Where the log (Circle's logger and stdio, so XRoar's messages too)
// goes: the serial port (UART), the serial port and /bmcoco.log on the
// SD card (file), or nowhere.
// Set with enable_logging=off|uart|file, as in BMC64, or in the menu.
// Based on BMC64's CLoggingDevice (third_party/common/menu_logging_device.cpp).
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for the
// Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_LOGGING_H_
#define BMCOCO_LOGGING_H_

#include <circle/device.h>
#include <circle/spinlock.h>

#include "host.h"
#include "sdcard/sd_fs.h"

#define LOG_FILE "/bmcoco.log"

// During start up, writes the log to the card now if it goes to the file
// (see CLogDevice::Flush()); afterwards does nothing. Implemented by the
// kernel.
void FlushStartupLog(void);

class CLogDevice : public CDevice {
public:
  CLogDevice(CDevice *pSerial);

  // HOST_LOG_OFF, HOST_LOG_UART or HOST_LOG_FILE. Opening the file uses
  // FatFs, so this runs at start up or on core 1. Returns false (and logging
  // is off) if the file cannot be opened.
  bool SetDestination(int destination);
  int GetDestination(void) const { return m_Destination; }

  // Any core, also from interrupts. For the file the text is queued.
  int Write(const void *pBuffer, size_t nCount) override;
  // Queues text for the file only.
  void WriteFile(const void *pBuffer, size_t nCount);

  // Writes queued text to the file. Core 1 only (FatFs).
  void Drain(void);
  // Writes everything queued to the card, for start up (on core 0, before
  // core 1 uses FatFs), so a start up that stops leaves its log.
  void Flush(void);
  // Writes everything queued and closes the file, before a reboot.
  void Close(void);

private:
  enum { QUEUE_SIZE = 32768, WRITE_SIZE = 1024 };

  CDevice *m_pSerial;
  volatile int m_Destination;
  sd_file *m_pFile;
  CSpinLock m_Lock;
  char m_Queue[QUEUE_SIZE];
  char m_WriteBuffer[WRITE_SIZE];
  unsigned m_QueueRead;
  unsigned m_QueueWrite;
  volatile unsigned m_QueueLength;
  unsigned m_Dropped;
  unsigned m_LastSync;
  bool m_bUnsynced;          // written to the file since the last sync
};

#endif
