/*
 * activity.c - hard disk and DriveWire activity, shown as the floppy
 * disk's is (platform.c): a red light at the bottom left of the picture,
 * with the drive and sector, while one is read or written, and an error in
 * red for a few seconds.
 *
 * The hard disks (the IDE interface, the NX32's and the MOOH's SD card) are
 * seen from the CoCo's side, as it drives them on the cartridge bus
 * (cartbus.c), since XRoar keeps their state to itself:
 *
 *  - IDE ($FF50-$FF57): the sector number registers, the drive selected,
 *    and a read or write command; then the status register as the CoCo
 *    reads it, for an error.
 *  - SD card, through the 65SPI ($FF6C): the command frames, READ_SINGLE_
 *    BLOCK (17) and WRITE_BLOCK (24), as XRoar's SD card has them, the
 *    block being the image's sector. A block written is skipped over.
 *
 * DriveWire is seen in the Becker port (becker.c), as the bytes go to and
 * from the server, the built-in one or one on another computer: a read or
 * write command gives the drive and sector, and the server's reply to it
 * its error. The commands are followed with the server's own rules for
 * their lengths (dwserver_command_length()), so that a sector written,
 * say, isn't taken for commands.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cart.h"
#include "part.h"

#include "../host.h"
#include "activity.h"
#include "dwserver.h"

struct device {
	char text[40];
	const char *error;
	double lit_until, shown_until, error_until;
	double last;            /* when it was last busy */
};

static int enabled = 1;
static struct device hd, dw;

void activity_set_enabled(int on)
{
	enabled = on;
}

int activity_enabled(void)
{
	return enabled;
}

static void busy(struct device *d)
{
	double now = host_time();
	d->lit_until = now + 0.25;
	d->shown_until = now + 2.0;
	d->last = now;
}

static void failed(struct device *d, const char *error)
{
	double now = host_time();
	d->error = error;
	d->error_until = now + 3.0;
	if (d->shown_until < d->error_until)
		d->shown_until = d->error_until;
	d->last = now;
}

/* ---- IDE ------------------------------------------------------------------ */

/* The sector registers of each drive, as XRoar's IDE controller keeps them
   (ide.c): written to the drive selected. */
struct ide_regs {
	uint8_t lba1, lba2, lba3, lba4;
};
static struct ide_regs ide_regs[2];
static int ide_selected;
static int ide_waiting;         /* a read or write was given: its status */

static void ide_write(unsigned reg, uint8_t D)
{
	switch (reg) {
	case 3: ide_regs[ide_selected].lba1 = D; break;
	case 4: ide_regs[ide_selected].lba2 = D; break;
	case 5: ide_regs[ide_selected].lba3 = D; break;
	case 6:
		ide_selected = (D & 0x10) ? 1 : 0;
		ide_regs[ide_selected].lba4 = D & 0x5f;
		break;
	case 7:
		/* READ (with or without retries) and WRITE */
		if ((D & 0xfe) == 0x20 || (D & 0xfe) == 0x30) {
			const struct ide_regs *r = &ide_regs[ide_selected];
			if (r->lba4 & 0x40)
				snprintf(hd.text, sizeof hd.text, "HD%d LSN %lu", ide_selected,
				         ((unsigned long) (r->lba4 & 0x0f) << 24)
				         | ((unsigned long) r->lba3 << 16)
				         | ((unsigned long) r->lba2 << 8) | r->lba1);
			else
				snprintf(hd.text, sizeof hd.text, "HD%d C%u H%u S%u", ide_selected,
				         ((unsigned) r->lba3 << 8) | r->lba2, r->lba4 & 0x0f,
				         (unsigned) r->lba1);
			busy(&hd);
			ide_waiting = 1;
		}
		break;
	default:
		break;
	}
}

static void ide_read(unsigned reg, uint8_t D)
{
	/* The status, once it's no longer busy: ERR set means the command
	   failed (no drive there, or a sector past its end). */
	if (reg == 7 && ide_waiting && !(D & 0x80)) {
		ide_waiting = 0;
		if (D & 0x01)
			failed(&hd, "error");
	}
}

/* ---- SD card ------------------------------------------------------------ */

enum { SD_IDLE, SD_FRAME, SD_TOKEN, SD_BLOCK };
static int sd_state;
static uint8_t sd_cmd, sd_arg[6];
static int sd_count;

static void sd_write(uint8_t D)
{
	unsigned long block;
	switch (sd_state) {
	case SD_IDLE:
		if ((D & 0xc0) == 0x40) {
			sd_cmd = D & 0x3f;
			sd_count = 0;
			sd_state = SD_FRAME;
		}
		break;
	case SD_FRAME:
		sd_arg[sd_count++] = D;
		if (sd_count < 6)
			break;
		sd_state = SD_IDLE;
		if (sd_cmd != 17 && sd_cmd != 24)
			break;
		block = ((unsigned long) sd_arg[0] << 24) | ((unsigned long) sd_arg[1] << 16)
		        | ((unsigned long) sd_arg[2] << 8) | sd_arg[3];
		snprintf(hd.text, sizeof hd.text, "HD0 LSN %lu", block);
		busy(&hd);
		if (sd_cmd == 24)
			sd_state = SD_TOKEN;
		break;
	case SD_TOKEN:
		/* The start of the block written */
		if (D == 0xfe) {
			sd_count = 0;
			sd_state = SD_BLOCK;
		}
		break;
	default:
		/* 512 bytes and the CRC */
		if (++sd_count >= 512 + 2)
			sd_state = SD_IDLE;
		break;
	}
}

/* ---- The cartridge bus (cartbus.c) ----------------------------------------- */

int activity_wants_bus(struct cart *c)
{
	unsigned i;
	if (!enabled || c == NULL)
		return 0;
	for (i = 0; i <= 4; i++) {
		struct cart *slot = c;
		if (i > 0) {
			char id[6];
			snprintf(id, sizeof id, "slot%u", i - 1);
			slot = (struct cart *) part_component_by_id(&c->part, id);
		}
		if (slot != NULL && slot->config != NULL && slot->config->type != NULL
		    && (strcmp(slot->config->type, "ide") == 0
		        || strcmp(slot->config->type, "nx32") == 0
		        || strcmp(slot->config->type, "mooh") == 0))
			return 1;
	}
	return 0;
}

void activity_bus_read(uint16_t A, bool P2, bool R2, uint8_t D)
{
	/* The IDE interface answers once, when P2 is low (idecart.c). */
	if (enabled && !P2 && !R2 && (A & 0xfff8) == 0xff50)
		ide_read(A & 7, D);
}

void activity_bus_write(uint16_t A, bool P2, bool R2, uint8_t D)
{
	if (!enabled || R2)
		return;
	if (!P2 && (A & 0xfff8) == 0xff50)
		ide_write(A & 7, D);
	else if (A == 0xff6c)
		sd_write(D);
}

void activity_bus_reset(void)
{
	memset(ide_regs, 0, sizeof ide_regs);
	ide_selected = 0;
	ide_waiting = 0;
	sd_state = SD_IDLE;
}

/* ---- DriveWire ------------------------------------------------------------ */

#define OP_READ 0x52
#define OP_REREAD 0x72
#define OP_READEX 0xD2
#define OP_REREADEX 0xF2
#define OP_WRITE 0x57
#define OP_REWRITE 0x77
#define OP_INIT 0x49
#define OP_SERINIT 0x45
#define OP_SERTERM 0xC5
#define OP_SERSETSTAT 0xC4
#define SS_OPEN 0x29
#define SS_CLOSE 0x2A

static struct {
	uint8_t in[5];          /* the start of the command */
	int len;                /* its bytes so far */
	int checksum;           /* bytes of READEX's checksum to come */
	int reply;              /* the server's next byte is a command's error */
	uint8_t open[256];      /* virtual channels open */
} dws;

void activity_dw_reset(void)
{
	dws.len = 0;
	dws.checksum = 0;
	dws.reply = 0;
	memset(dws.open, 0, sizeof dws.open);
}

static void dw_command(void)
{
	uint8_t op = dws.in[0];
	switch (op) {
	case OP_READ: case OP_REREAD: case OP_READEX: case OP_REREADEX:
	case OP_WRITE: case OP_REWRITE:
		snprintf(dw.text, sizeof dw.text, "DW%u LSN %lu", (unsigned) dws.in[1],
		         ((unsigned long) dws.in[2] << 16) | ((unsigned long) dws.in[3] << 8)
		         | dws.in[4]);
		busy(&dw);
		/* READEX: the sector, then the CoCo's checksum of it, then the
		   error. READ and WRITE: the error first. */
		if (op == OP_READEX || op == OP_REREADEX)
			dws.checksum = 2;
		else
			dws.reply = 1;
		break;
	case OP_SERINIT:
		dws.open[dws.in[1]] = 1;
		break;
	case OP_SERTERM:
		dws.open[dws.in[1]] = 0;
		break;
	case OP_SERSETSTAT:
		if (dws.in[2] == SS_OPEN)
			dws.open[dws.in[1]] = 1;
		else if (dws.in[2] == SS_CLOSE)
			dws.open[dws.in[1]] = 0;
		break;
	case OP_INIT:
		memset(dws.open, 0, sizeof dws.open);
		break;
	default:
		break;
	}
}

void activity_dw_write(uint8_t byte)
{
	if (!enabled)
		return;
	if (dws.checksum > 0) {
		if (--dws.checksum == 0)
			dws.reply = 1;
		return;
	}
	dws.reply = 0;
	if (dws.len < (int) sizeof dws.in)
		dws.in[dws.len] = byte;
	dws.len++;
	if (dws.len >= dwserver_command_length(dws.in,
	                                        dws.len < 5 ? dws.len : 5, dws.open)) {
		dw_command();
		dws.len = 0;
	}
}

void activity_dw_read(uint8_t byte)
{
	const char *error;
	if (!enabled || !dws.reply)
		return;
	dws.reply = 0;
	switch (byte) {
	case 0: return;
	case 242: error = "write protected"; break;
	case 243: error = "CRC error"; break;
	case 244: error = "read error"; break;
	case 245: error = "write error"; break;
	case 246: error = "no disk"; break;
	default: error = "error"; break;
	}
	failed(&dw, error);
}

/* ---- What's shown --------------------------------------------------------- */

static int shown(struct device *d, double now)
{
	if (d->error != NULL && now > d->error_until)
		d->error = NULL;
	return d->text[0] != '\0' && now <= d->shown_until;
}

int activity_line(double now, char *text, unsigned size, int *lit, int *error,
                  double *last)
{
	struct device *d = NULL;
	if (!enabled)
		return 0;
	if (shown(&hd, now))
		d = &hd;
	if (shown(&dw, now) && (d == NULL || dw.last > d->last))
		d = &dw;
	if (d == NULL)
		return 0;
	snprintf(text, size, "%s%s%s", d->text, d->error ? " " : "",
	         d->error ? d->error : "");
	*lit = now <= d->lit_until;
	*error = d->error != NULL;
	*last = d->last;
	return 1;
}
