//
// test_share.cpp
//
// BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
// Copyright (C) 2026 aminch <https://github.com/aminch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Modified for BMCoco.
//
// Tests how the Pi 4 shares a frame's drawing between cores
// (src/av/frameshare.h), with threads on the PC in place of cores 1 to 3:
// thousands of frames of random sizes, checking that every item is
// prepared before any row is drawn, that every row is drawn once, and that
// no helper is still at work when Run() returns (the buffers are replaced
// between frames, as SetView() can). Built with ThreadSanitizer, which
// reports any data race.
//

#include "../../src/av/frameshare.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct Job {
  unsigned frame = 0;
  unsigned prepare_total = 0;
  std::vector<unsigned> *prepared = nullptr;  // frame each item was prepared for
  std::vector<unsigned> *rows = nullptr;      // times each row was drawn
  std::atomic<int> busy{0};
  std::atomic<int> errors{0};

  void Prepare(unsigned item) {
    busy++;
    (*prepared)[item] = frame;
    busy--;
  }

  void Render(unsigned row, unsigned core) {
    busy++;
    (void)core;
    for (unsigned i = 0; i < prepare_total; i += 7) {
      if ((*prepared)[i] != frame) {
        errors++;
      }
    }
    if (prepare_total > 0 && (*prepared)[prepare_total - 1] != frame) {
      errors++;
    }
    (*rows)[row]++;
    busy--;
  }
};

int main(void) {
  Pi4FrameShare share;
  Job job;
  srand(1);

  std::vector<std::thread> helpers;
  const unsigned frames = 4000;
  int failed = 0;
  for (unsigned f = 1; f <= frames; f++) {
    // The helpers start while frames are already being drawn.
    if (f == 3 || f == 10) {
      unsigned core = (unsigned)helpers.size() + 2;
      helpers.emplace_back([&share, &job, core] { share.Help(job, core); });
    }
    unsigned prepare = (unsigned)(rand() % 300);
    unsigned rows = 1 + (unsigned)(rand() % 1100);
    delete job.prepared;
    delete job.rows;
    job.prepared = new std::vector<unsigned>(prepare, 0);
    job.rows = new std::vector<unsigned>(rows, 0);
    job.frame = f;
    job.prepare_total = prepare;

    share.Run(job, 1, prepare, rows);

    if (job.busy != 0) {
      printf("FAIL  frame %u: a helper was still working after Run()\n", f);
      failed++;
    }
    for (unsigned y = 0; y < rows; y++) {
      if ((*job.rows)[y] != 1) {
        printf("FAIL  frame %u: row %u drawn %u times\n", f, y, (*job.rows)[y]);
        failed++;
        break;
      }
    }
    if (failed > 10) {
      break;
    }
  }
  if (job.errors != 0) {
    printf("FAIL  rows drawn before their frame was prepared (%d)\n", job.errors.load());
    failed++;
  }
  printf("%s  frame sharing: %u frames, %u helpers\n", failed ? "FAIL" : "PASS",
         frames, share.Helpers());
  fflush(stdout);
  // The helpers never return, as on the Pi.
  _exit(failed ? 1 : 0);
}
