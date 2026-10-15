/*
 * live.h - the host simulator's live mode (see live.c).
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_LIVE_H_
#define BMCOCO_LIVE_H_

#include <stdint.h>

int live_init(int port);
void live_poll(void);
void live_audio(const int16_t *samples, unsigned n);

/* From host_sim.c */
void sim_push_key(int usage, int pressed);
void sim_set_pad(int pad, int bits, int x, int y);
void sim_mouse(int dx, int dy, int buttons);
const uint16_t *sim_screen(int *w, int *h, int *pitch);

#endif
