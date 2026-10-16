/*
 * dwserver.c - BMCoco's built-in DriveWire server.
 *
 * DriveWire lets a CoCo use disk images on another computer, through the
 * Becker port (src/port/becker.c). On a bare metal Pi that other computer
 * would have to be a second one on the network, so BMCoco has a server of
 * its own: the Becker port talks to it unless an address is set for a
 * server elsewhere. It serves the disk images chosen in the menu (drives
 * 0 to 3, kept as dw0 to dw3 in bmcoco.cfg) and the time; NitrOS-9 (as in
 * its DriveWire builds) and HDB-DOS for DriveWire use it as they would
 * DriveWire 4 or pyDriveWire. Virtual serial channels, printing and named
 * objects are answered as having nothing.
 *
 * The protocol is DriveWire 4's, after pyDriveWire's dwserver.py
 * (https://github.com/n6il/pyDriveWire): the CoCo sends a command, an
 * opcode and its arguments, and the server answers some of them. As in
 * pyDriveWire, a command for a virtual channel that isn't open ends after
 * the channel number (NitrOS-9 relies on it). Sectors
 * are 256 bytes; a disk image with a header (a JVC header, the file's
 * length over a multiple of 256) has it skipped.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "dwserver.h"

/* Opcodes (pyDriveWire, dwconstants.py) */
#define OP_NOP 0x00
#define OP_NAMEOBJ_MOUNT 0x01
#define OP_NAMEOBJ_CREATE 0x02
#define OP_TIME 0x23
#define OP_SERREAD 0x43
#define OP_SERGETSTAT 0x44
#define OP_SERINIT 0x45
#define OP_PRINTFLUSH 0x46
#define OP_GETSTAT 0x47
#define OP_INIT 0x49
#define OP_PRINT 0x50
#define OP_READ 0x52
#define OP_SETSTAT 0x53
#define OP_TERM 0x54
#define OP_WRITE 0x57
#define OP_DWINIT 0x5A
#define OP_SERREADM 0x63
#define OP_SERWRITEM 0x64
#define OP_REREAD 0x72
#define OP_REWRITE 0x77
#define OP_FASTWRITE 0x80  /* to 0x8F */
#define OP_SERWRITE 0xC3
#define OP_SERSETSTAT 0xC4
#define OP_SERTERM 0xC5
#define OP_READEX 0xD2
#define OP_REREADEX 0xF2
#define OP_RESET3 0xF8
#define OP_RESET2 0xFE
#define OP_RESET1 0xFF

#define SS_COMST 0x28  /* SERSETSTAT code followed by 26 bytes */
#define SS_OPEN 0x29
#define SS_CLOSE 0x2A

/* Errors (OS-9's) */
#define E_OK 0
#define E_EOF 211
#define E_WRPROT 242
#define E_CRC 243
#define E_READ 244
#define E_WRITE 245
#define E_NOTRDY 246

#define SECTOR 256

static struct drive {
	FILE *f;
	char *path;
	int read_only;
	long header;
	long sectors;
} drives[DWSERVER_DRIVES];

/* Virtual channels opened (OP_SERINIT, or SS_OPEN). */
static uint8_t channel_open[256];

/* The command coming in: opcode, arguments. */
static uint8_t in[8 + SECTOR];
static int in_len;
/* After READEX's sector, the CoCo sends its checksum: the error to answer
   with, and the checksum expected. */
static int await_checksum;
static int readex_error;
static unsigned readex_sum;
/* The answer going out. */
static uint8_t out[8 + SECTOR];
static int out_len, out_pos;

static void reply(const uint8_t *data, int len)
{
	if (out_pos == out_len)
		out_pos = out_len = 0;
	if (len > (int) sizeof(out) - out_len)
		len = (int) sizeof(out) - out_len;
	memcpy(out + out_len, data, (size_t) len);
	out_len += len;
}

static void reply_byte(uint8_t b)
{
	reply(&b, 1);
}

static unsigned checksum(const uint8_t *data, int len)
{
	unsigned sum = 0;
	int i;
	for (i = 0; i < len; i++)
		sum += data[i];
	return sum & 0xffff;
}

/* Reads sector LSN of DRIVE into DATA (zeroes where there's none).
   Returns an error code. */
static int read_sector(int drive, unsigned lsn, uint8_t *data)
{
	struct drive *d = drive < DWSERVER_DRIVES ? &drives[drive] : NULL;
	memset(data, 0, SECTOR);
	if (d == NULL || d->f == NULL)
		return E_NOTRDY;
	if ((long) lsn >= d->sectors)
		return E_EOF;
	if (fseeko(d->f, (off_t) d->header + (off_t) lsn * SECTOR, SEEK_SET) != 0)
		return E_READ;
	if (fread(data, 1, SECTOR, d->f) != SECTOR)
		return E_READ;
	return E_OK;
}

static int write_sector(int drive, unsigned lsn, const uint8_t *data)
{
	struct drive *d = drive < DWSERVER_DRIVES ? &drives[drive] : NULL;
	if (d == NULL || d->f == NULL)
		return E_NOTRDY;
	if (d->read_only)
		return E_WRPROT;
	if (fseeko(d->f, (off_t) d->header + (off_t) lsn * SECTOR, SEEK_SET) != 0)
		return E_WRITE;
	if (fwrite(data, 1, SECTOR, d->f) != SECTOR || fflush(d->f) != 0)
		return E_WRITE;
	if ((long) lsn >= d->sectors)
		d->sectors = (long) lsn + 1;
	return E_OK;
}

/* The bytes the command in in[] needs, from what has come so far. */
static int command_length(void)
{
	uint8_t op = in[0];
	int open = in_len >= 2 && channel_open[in[1]];
	if (op >= OP_FASTWRITE && op <= OP_FASTWRITE + 15)
		return channel_open[op - OP_FASTWRITE] ? 2 : 1;
	switch (op) {
	case OP_DWINIT: case OP_SERINIT: case OP_SERTERM: case OP_PRINT:
		return 2;
	case OP_GETSTAT: case OP_SETSTAT:
		return 3;
	/* These end after the channel number if it isn't open. */
	case OP_SERGETSTAT: case OP_SERWRITE: case OP_SERREADM:
		return in_len >= 2 && !open ? 2 : 3;
	case OP_SERWRITEM:
		if (in_len >= 2 && !open)
			return 2;
		return in_len >= 3 ? 3 + in[2] : 3;
	case OP_SERSETSTAT:
		return in_len >= 3 && in[2] == SS_COMST ? 3 + 26 : 3;
	case OP_NAMEOBJ_MOUNT: case OP_NAMEOBJ_CREATE:
		return in_len >= 2 ? 2 + in[1] : 2;
	case OP_READ: case OP_REREAD: case OP_READEX: case OP_REREADEX:
		return 5;
	case OP_WRITE: case OP_REWRITE:
		return 5 + SECTOR + 2;
	default:
		/* NOP, INIT, TERM, the resets, TIME, SERREAD, PRINTFLUSH, and
		   ones not known (left out). */
		return 1;
	}
}

static void run_command(void)
{
	uint8_t data[SECTOR];
	int drive = in[1];
	unsigned lsn = ((unsigned) in[2] << 16) | ((unsigned) in[3] << 8) | in[4];
	int error;

	switch (in[0]) {
	case OP_DWINIT:
		/* pyDriveWire's answer, which NitrOS-9 takes as DriveWire 4 */
		reply_byte(0xff);
		break;
	case OP_TIME: {
		time_t now = time(NULL);
		struct tm *t = localtime(&now);
		uint8_t answer[6] = {0};
		if (t != NULL) {
			answer[0] = (uint8_t) t->tm_year;
			answer[1] = (uint8_t) (t->tm_mon + 1);
			answer[2] = (uint8_t) t->tm_mday;
			answer[3] = (uint8_t) t->tm_hour;
			answer[4] = (uint8_t) t->tm_min;
			answer[5] = (uint8_t) t->tm_sec;
		}
		reply(answer, sizeof(answer));
		break;
	}
	case OP_READ:
	case OP_REREAD: {
		unsigned sum;
		error = read_sector(drive, lsn, data);
		sum = checksum(data, SECTOR);
		reply_byte((uint8_t) error);
		reply_byte((uint8_t) (sum >> 8));
		reply_byte((uint8_t) sum);
		reply(data, SECTOR);
		break;
	}
	case OP_READEX:
	case OP_REREADEX:
		readex_error = read_sector(drive, lsn, data);
		readex_sum = checksum(data, SECTOR);
		reply(data, SECTOR);
		await_checksum = 1;
		break;
	case OP_WRITE:
	case OP_REWRITE: {
		unsigned sum = ((unsigned) in[5 + SECTOR] << 8) | in[6 + SECTOR];
		if (sum != checksum(in + 5, SECTOR))
			error = E_CRC;
		else
			error = write_sector(drive, lsn, in + 5);
		reply_byte((uint8_t) error);
		break;
	}
	case OP_SERREAD:
		/* No virtual channel has anything. */
		reply_byte(0);
		reply_byte(0);
		break;
	case OP_SERREADM:
		if (in_len == 3) {
			memset(data, 0, sizeof(data));
			reply(data, in[2]);
		}
		break;
	case OP_SERINIT:
		channel_open[in[1]] = 1;
		break;
	case OP_SERTERM:
		channel_open[in[1]] = 0;
		break;
	case OP_SERSETSTAT:
		if (in[2] == SS_OPEN)
			channel_open[in[1]] = 1;
		else if (in[2] == SS_CLOSE)
			channel_open[in[1]] = 0;
		break;
	case OP_NAMEOBJ_MOUNT:
	case OP_NAMEOBJ_CREATE:
		/* No named objects: drive 0 means none. */
		reply_byte(0);
		break;
	default:
		break;
	}
}

void dwserver_reset(void)
{
	in_len = 0;
	await_checksum = 0;
	out_len = out_pos = 0;
	memset(channel_open, 0, sizeof(channel_open));
}

void dwserver_write(uint8_t byte)
{
	if (await_checksum) {
		in[in_len++] = byte;
		if (in_len == 2) {
			unsigned sum = ((unsigned) in[0] << 8) | in[1];
			int error = readex_error;
			if (error == E_OK && sum != readex_sum)
				error = E_CRC;
			reply_byte((uint8_t) error);
			await_checksum = 0;
			in_len = 0;
		}
		return;
	}
	if (in_len < (int) sizeof(in))
		in[in_len++] = byte;
	if (in_len >= command_length()) {
		run_command();
		in_len = 0;
	}
}

int dwserver_ready(void)
{
	return out_pos < out_len;
}

uint8_t dwserver_read(void)
{
	if (out_pos >= out_len)
		return 0;
	return out[out_pos++];
}

int dwserver_set_drive(int drive, const char *path)
{
	struct drive *d;
	off_t size;
	if (drive < 0 || drive >= DWSERVER_DRIVES)
		return 0;
	d = &drives[drive];
	if (d->f != NULL)
		fclose(d->f);
	free(d->path);
	*d = (struct drive){0};
	if (path == NULL || path[0] == '\0')
		return 1;
	d->f = fopen(path, "r+b");
	if (d->f == NULL) {
		d->f = fopen(path, "rb");
		d->read_only = 1;
	}
	if (d->f == NULL)
		return 0;
	d->path = strdup(path);
	fseeko(d->f, 0, SEEK_END);
	size = ftello(d->f);
	if (size < 0)
		size = 0;
	d->header = (long) (size % SECTOR);
	d->sectors = (long) (size / SECTOR);
	return 1;
}

const char *dwserver_drive(int drive)
{
	if (drive < 0 || drive >= DWSERVER_DRIVES)
		return NULL;
	return drives[drive].path;
}
