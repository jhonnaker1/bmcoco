//
// frameshare.h
//
// Shares the drawing of a frame between cores: the core that owns the
// frame (core 1, the emulator's) and helper cores (2 and 3), for smooth
// scaling and the CRT shader (src/av/video.cpp, src/av/scaler.h).
//
// A frame has two steps: PREPARE items (any order, any core), then ROWS
// (any order, any core), and no row may start until every item is
// prepared. Run() posts a frame, does a share of it, and returns once
// every item and row is done and every helper has finished with it, so
// between frames no helper is touching anything. Help() is the helpers'
// loop.
//
// The work is taken in chunks from shared counters (atomic adds), so it
// doesn't matter how many helpers there are, or if one is late. A helper
// registers itself (m_nHelpers) before it waits for its first frame; Run()
// waits for the helpers registered when the frame was posted.
//
// Sources:
//  - GCC, "Built-in Functions for Memory Model Aware Atomic Operations"
//    (__atomic_fetch_add, __atomic_load_n, __atomic_store_n).
//  - Arm Architecture Reference Manual (Armv8-A): WFE waits for an event,
//    SEV sends one to every core; a DSB first makes the writes visible.
//  - Circle, include/circle/synchronize64.h: WaitForEvent(), SendEvent(),
//    DataSyncBarrier().
//
// On a PC (tools/pi4scaler) waiting yields the thread instead.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_AV_FRAMESHARE_H_
#define BMCOCO_AV_FRAMESHARE_H_

#ifdef __circle__
#include <circle/synchronize.h>
#else
#include <sched.h>
#endif

class Pi4FrameShare {
public:
  Pi4FrameShare(void)
      : m_nGeneration(0), m_nHelpers(0), m_nFinished(0), m_nNextPrepare(0),
        m_nPrepared(0), m_nPrepareTotal(0), m_nNextRow(0), m_nRowsDone(0),
        m_nRowTotal(0) {}

  // Items and rows taken at a time.
  static const unsigned Chunk = 8;

  // The owning core: draws a frame with whatever helpers there are. JOB
  // has Prepare(unsigned item) and Render(unsigned row, unsigned core).
  template <class Job>
  void Run(Job &job, unsigned core, unsigned prepare, unsigned rows) {
    Store(&m_nFinished, 0);
    unsigned helpers = Load(&m_nHelpers);
    Store(&m_nPrepared, 0);
    Store(&m_nRowsDone, 0);
    Store(&m_nPrepareTotal, prepare);
    Store(&m_nRowTotal, rows);
    Store(&m_nNextPrepare, 0);
    Store(&m_nNextRow, 0);
    Add(&m_nGeneration, 1);
    Signal();
    Share(job, core);
    while (Load(&m_nRowsDone) < rows || Load(&m_nFinished) < helpers) {
      Wait();
    }
  }

  // A helper core: helps with every frame from now on. Never returns.
  template <class Job> void Help(Job &job, unsigned core) {
    // The generation is read before registering, so a frame posted after
    // this helper is counted is one it sees as new.
    unsigned seen = Load(&m_nGeneration);
    Add(&m_nHelpers, 1);
    for (;;) {
      while (Load(&m_nGeneration) == seen) {
        Wait();
      }
      seen = Load(&m_nGeneration);
      Share(job, core);
      Add(&m_nFinished, 1);
      Signal();
    }
  }

  // For tests: helpers registered.
  unsigned Helpers(void) { return Load(&m_nHelpers); }

private:
  template <class Job> void Share(Job &job, unsigned core) {
    for (;;) {
      unsigned first = Add(&m_nNextPrepare, Chunk);
      unsigned total = Load(&m_nPrepareTotal);
      if (first >= total) {
        break;
      }
      unsigned end = first + Chunk < total ? first + Chunk : total;
      for (unsigned i = first; i < end; i++) {
        job.Prepare(i);
      }
      Add(&m_nPrepared, end - first);
      Signal();
    }
    for (;;) {
      unsigned first = Add(&m_nNextRow, Chunk);
      unsigned total = Load(&m_nRowTotal);
      if (first >= total) {
        break;
      }
      while (Load(&m_nPrepared) < Load(&m_nPrepareTotal)) {
        Wait();
      }
      unsigned end = first + Chunk < total ? first + Chunk : total;
      for (unsigned y = first; y < end; y++) {
        job.Render(y, core);
      }
      Add(&m_nRowsDone, end - first);
      Signal();
    }
  }

  static unsigned Add(volatile unsigned *p, unsigned n) {
    return __atomic_fetch_add(p, n, __ATOMIC_ACQ_REL);
  }
  static unsigned Load(volatile unsigned *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
  }
  static void Store(volatile unsigned *p, unsigned v) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
  }

#ifdef __circle__
  static void Wait(void) { WaitForEvent(); }
  static void Signal(void) {
    DataSyncBarrier();
    SendEvent();
  }
#else
  static void Wait(void) { sched_yield(); }
  static void Signal(void) {}
#endif

  volatile unsigned m_nGeneration;
  volatile unsigned m_nHelpers;
  volatile unsigned m_nFinished;
  volatile unsigned m_nNextPrepare, m_nPrepared, m_nPrepareTotal;
  volatile unsigned m_nNextRow, m_nRowsDone, m_nRowTotal;
};

#endif
