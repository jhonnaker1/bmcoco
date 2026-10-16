/*
 * dwserver.h - BMCoco's built-in DriveWire server.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_DWSERVER_H
#define BMCOCO_DWSERVER_H

#include <stdint.h>

#define DWSERVER_DRIVES 4

/* Forgets a command half sent (the CoCo was reset). */
void dwserver_reset(void);
/* A byte the CoCo wrote to the Becker port. */
void dwserver_write(uint8_t byte);
/* TRUE if the server has a byte for the CoCo, and dwserver_read() gets
   it (0 when there's none). */
int dwserver_ready(void);
uint8_t dwserver_read(void);

/* Puts the disk image PATH in DRIVE (0 to 3), or empties it (NULL or "").
   Returns 0 if the image can't be opened. */
int dwserver_set_drive(int drive, const char *path);
/* The image in DRIVE, or NULL. */
const char *dwserver_drive(int drive);

#endif
