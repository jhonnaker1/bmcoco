/*
 * activity.h - hard disk and DriveWire activity (activity.c), shown with
 * the floppy disk's.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_ACTIVITY_H
#define BMCOCO_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

struct cart;

/* The disk_activity option (Disk Drives, Show Disk Activity). */
void activity_set_enabled(int on);
int activity_enabled(void);

/* The cartridge bus, from cartbus.c: TRUE if cartridge C has a hard disk
   interface to watch (in a Multi-Pak too); and its cycles. */
int activity_wants_bus(struct cart *c);
void activity_bus_read(uint16_t A, bool P2, bool R2, uint8_t D);
void activity_bus_write(uint16_t A, bool P2, bool R2, uint8_t D);
void activity_bus_reset(void);

/* The Becker port's bytes, from becker.c: written to the DriveWire server,
   and read from it. Reset when the CoCo is. */
void activity_dw_write(uint8_t byte);
void activity_dw_read(uint8_t byte);
void activity_dw_reset(void);

/* What to show at NOW, the hard disk's or DriveWire's, whichever was busy
   last: its TEXT, whether its light is LIT, whether it's an ERROR, and when
   it was LAST busy. 0 if neither is shown. */
int activity_line(double now, char *text, unsigned size, int *lit, int *error,
                  double *last);

#endif
