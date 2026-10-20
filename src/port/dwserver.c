/*
 * dwserver.c - BMCoco's built-in DriveWire server.
 *
 * DriveWire lets a CoCo use disk images on another computer, through the
 * Becker port (src/port/becker.c). On a bare metal Pi that other computer
 * would have to be a second one on the network, so BMCoco has a server of
 * its own: the Becker port talks to it unless an address is set for a
 * server elsewhere. It serves the disk images chosen in the menu (drives
 * 0 to 3, kept as dw0 to dw3 in bmcoco.cfg), the time, and virtual serial
 * channels to the network; NitrOS-9 (as in its DriveWire builds) and
 * HDB-DOS for DriveWire use it as they would DriveWire 4 or pyDriveWire.
 * Printing and named objects are answered as having nothing.
 *
 * Virtual channels (NitrOS-9's /N0 to /N14, channels 0 to 14) work as in
 * pyDriveWire's dwchannel.py: the first line the CoCo writes to a channel
 * says what it is for.
 *  - "tcp connect host port" (NitrOS-9's lib/net.as, its network
 *    programs): connects, in the background, and answers "OK connected
 *    to host:port", or "FAIL <error> ..." (net.as reads the error number);
 *    then the channel is the connection, both ways, until either end
 *    closes it. "tcp listen", "join" and "kill" (incoming connections)
 *    answer FAIL.
 *  - "AT..." (terminal programs): a Hayes modem, as the RS-232 Pak's
 *    (rs232.c): ATDT host:port calls a telnet BBS (CONNECT, NO CARRIER),
 *    +++ with a second of quiet before and after gets the modem back, ATH,
 *    ATO, ATZ, ATE0/1 (echo, off at first as in pyDriveWire), ATV0/1,
 *    ATQ0/1, ATI; the rest answer OK.
 *  - "dw ..." (NitrOS-9's dw command): dw disk show, dw server show; then
 *    the channel closes.
 * A channel that closes is reported to the CoCo (SERREAD answering 16 and
 * the channel), and then counts as not open. Calls are made with
 * netcall.c: Circle has no non-blocking connect, and a host that doesn't
 * answer takes it minutes to give up on.
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

#include "top-config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>

#include "dwserver.h"
#include "logging.h"

#include "../host.h"
#include "netcall.h"

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

/* The virtual channels' ends (see the top). */
#define CHANNELS 15
#define CH_OUT 4096
#define CH_LINE 128
enum {
	CH_CLOSED,
	CH_NEW,       /* open, its first line not yet come */
	CH_AT,        /* a modem, taking commands */
	CH_ONLINE,    /* a modem, its call connected */
	CH_TCP,       /* the connection, both ways */
	CH_CLOSING,   /* to be reported closed once its output has gone */
};
enum { CALL_AT, CALL_TCP };
static struct channel {
	int state;
	int conn;               /* the connection, or -1 */
	struct netcall *call;   /* being made, or NULL */
	int call_kind;
	char call_address[72];
	char line[CH_LINE];
	unsigned line_len;
	uint8_t out[CH_OUT];    /* for the CoCo */
	unsigned head, tail;
	int echo, verbose, quiet;
	struct telnet telnet;
	double last_in;
	int plus;
	double plus_time;
} channels[CHANNELS];
static unsigned next_channel;   /* where SERREAD looks first */

/* The channels start with no connection (-1): left as C leaves them,
   all 0, closing them would close connection 0, whatever had it (another
   channel's call, the RS-232 Pak's; the PC simulator's standard input). */
static void channels_init(void)
{
	static int done;
	unsigned n;
	if (done)
		return;
	done = 1;
	for (n = 0; n < CHANNELS; n++)
		channels[n].conn = -1;
}

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

/* ---- Virtual channels -------------------------------------------------- */

static unsigned ch_count(const struct channel *c)
{
	return (c->head - c->tail) % CH_OUT;
}

static void ch_put(void *arg, uint8_t b)
{
	struct channel *c = arg;
	if (ch_count(c) < CH_OUT - 1) {
		c->out[c->head] = b;
		c->head = (c->head + 1) % CH_OUT;
	}
}

static void ch_puts(struct channel *c, const char *text)
{
	while (*text)
		ch_put(c, (uint8_t) *text++);
}

static uint8_t ch_get(struct channel *c)
{
	uint8_t b = 0;
	if (ch_count(c) > 0) {
		b = c->out[c->tail];
		c->tail = (c->tail + 1) % CH_OUT;
	}
	return b;
}

static void ch_hang_up(struct channel *c)
{
	netcall_abandon(&c->call);
	if (c->conn >= 0)
		host_tcp_close(c->conn);
	c->conn = -1;
	c->telnet.state = 0;
	c->plus = 0;
}

/* Opened by the CoCo (SERINIT, SS_OPEN): a new end. */
static void ch_open(unsigned n)
{
	struct channel *c;
	if (n >= CHANNELS)
		return;
	c = &channels[n];
	ch_hang_up(c);
	*c = (struct channel){0};
	c->conn = -1;
	c->state = CH_NEW;
	c->verbose = 1;
}

/* Closed by the CoCo (SERTERM, SS_CLOSE, a reset). */
static void ch_close(unsigned n)
{
	if (n >= CHANNELS)
		return;
	ch_hang_up(&channels[n]);
	channels[n].state = CH_CLOSED;
	channels[n].head = channels[n].tail = 0;
}

enum { R_OK, R_CONNECT, R_RING, R_NO_CARRIER, R_ERROR };

static void ch_reply(struct channel *c, int code)
{
	static const char *const words[] = {
		"OK", "CONNECT", "RING", "NO CARRIER", "ERROR"
	};
	char text[24];
	if (c->quiet)
		return;
	if (c->verbose)
		snprintf(text, sizeof text, "\r\n%s\r\n", words[code]);
	else
		snprintf(text, sizeof text, "%d\r", code);
	ch_puts(c, text);
}

static void ch_call(struct channel *c, const char *address, int kind)
{
	char host[64], port[8];
	ch_hang_up(c);
	c->call_kind = kind;
	if (!netcall_address(address, 23, host, sizeof host, port, sizeof port)) {
		if (kind == CALL_AT) {
			ch_reply(c, R_ERROR);
		} else {
			ch_puts(c, "FAIL 203 usage: tcp connect <host> <port>\r\n");
			c->state = CH_CLOSING;
		}
		return;
	}
	snprintf(c->call_address, sizeof c->call_address, "%s:%s", host, port);
	c->call = netcall_start(host, port);
	if (c->call == NULL) {
		if (kind == CALL_AT) {
			ch_reply(c, R_NO_CARRIER);
		} else {
			ch_puts(c, "FAIL 207 out of memory\r\n");
			c->state = CH_CLOSING;
		}
		return;
	}
	LOG_PRINT("DriveWire channel %d: calling %s\n", (int) (c - channels), c->call_address);
}

/* A call finished. */
static void ch_call_done(struct channel *c, int result)
{
	char text[112];
	if (result >= 0) {
		c->conn = result;
		LOG_PRINT("DriveWire channel %d: connected to %s\n", (int) (c - channels),
		          c->call_address);
		if (c->call_kind == CALL_AT) {
			c->state = CH_ONLINE;
			ch_reply(c, R_CONNECT);
		} else {
			c->state = CH_TCP;
			snprintf(text, sizeof text, "OK connected to %s\r\n", c->call_address);
			ch_puts(c, text);
		}
		return;
	}
	LOG_WARN("DriveWire channel %d: %s %s\n", (int) (c - channels),
	         result == NETCALL_NO_HOST ? "can't find" : "can't connect to", c->call_address);
	if (c->call_kind == CALL_AT) {
		ch_reply(c, R_NO_CARRIER);
	} else {
		/* 216 (path name not found) for no such host, 246 (not
		   ready) for no answer: OS-9's errors, as net.as returns. */
		snprintf(text, sizeof text, "FAIL %d %s %s\r\n",
		         result == NETCALL_NO_HOST ? 216 : 246,
		         result == NETCALL_NO_HOST ? "can't find" : "can't connect to",
		         c->call_address);
		ch_puts(c, text);
		c->state = CH_CLOSING;
	}
}

/* "dw ..." */
static void ch_dw_command(struct channel *c, const char *line)
{
	char text[320];
	int d;
	const char *arg = line + 2;
	while (*arg == ' ')
		arg++;
	if (strncasecmp(arg, "disk show", 9) == 0 || strncasecmp(arg, "disk", 4) == 0) {
		ch_puts(c, "\r\nDrive  Image\r\n");
		for (d = 0; d < DWSERVER_DRIVES; d++) {
			snprintf(text, sizeof text, "%-6d %s\r\n", d,
			         drives[d].path ? drives[d].path : "(none)");
			ch_puts(c, text);
		}
	} else if (strncasecmp(arg, "server", 6) == 0) {
		ch_puts(c, "\r\nBMCoco's built-in DriveWire server\r\n");
	} else {
		snprintf(text, sizeof text,
		         "\r\nFAIL 208 BMCoco's server doesn't do that: %.200s\r\n", line);
		ch_puts(c, text);
	}
	c->state = CH_CLOSING;
}

static void ch_at_command(struct channel *c, char *p)
{
	int result = R_OK;
	p += 2;
	while (*p) {
		int k = tolower((unsigned char) *p++);
		int arg = 0;
		if (k == ' ')
			continue;
		if (k == 'd') {
			if (tolower((unsigned char) *p) == 't' || tolower((unsigned char) *p) == 'p')
				p++;
			ch_call(c, p, CALL_AT);
			return;     /* the rest of the line is the address */
		}
		if (k == '&' || k == '\\' || k == '%') {
			if (*p)
				p++;
			while (isdigit((unsigned char) *p))
				p++;
			continue;
		}
		if (k == 's') {
			while (isdigit((unsigned char) *p))
				p++;
			if (*p == '=') {
				p++;
				while (isdigit((unsigned char) *p))
					p++;
			} else if (*p == '?') {
				p++;
				ch_puts(c, "\r\n000\r\n");
			}
			continue;
		}
		if (isdigit((unsigned char) *p))
			arg = *p++ - '0';
		while (isdigit((unsigned char) *p))
			p++;
		switch (k) {
		case 'e': c->echo = arg; break;
		case 'v': c->verbose = arg; break;
		case 'q': c->quiet = arg; break;
		case 'h': ch_hang_up(c); break;
		case 'z':
			ch_hang_up(c);
			c->verbose = 1;
			c->quiet = 0;
			break;
		case 'o':
			if (c->conn >= 0) {
				c->state = CH_ONLINE;
				ch_reply(c, R_CONNECT);
				return;
			}
			result = R_NO_CARRIER;
			break;
		case 'a':
			result = R_NO_CARRIER;   /* no incoming calls */
			break;
		case 'i':
			ch_puts(c, "\r\nBMCoco DriveWire virtual modem\r\n");
			break;
		default:
			break;
		}
	}
	ch_reply(c, result);
}

/* A line the CoCo wrote to a channel in its command state. */
static void ch_line(struct channel *c)
{
	char *p = c->line;
	char *end;
	c->line[c->line_len] = '\0';
	c->line_len = 0;
	while (*p == ' ')
		p++;
	end = p + strlen(p);
	while (end > p && end[-1] == ' ')
		*--end = '\0';
	if (*p == '\0')
		return;
	if (tolower((unsigned char) p[0]) == 'a' && tolower((unsigned char) p[1]) == 't') {
		c->state = CH_AT;
		ch_at_command(c, p);
	} else if (c->state == CH_AT) {
		/* not a command: a modem ignores it */
	} else if (strncasecmp(p, "tcp connect ", 12) == 0) {
		ch_call(c, p + 12, CALL_TCP);
	} else if (strncasecmp(p, "tcp ", 4) == 0) {
		ch_puts(c, "FAIL 208 incoming connections aren't supported\r\n");
		c->state = CH_CLOSING;
	} else if (strncasecmp(p, "dw", 2) == 0 && (p[2] == ' ' || p[2] == '\0')) {
		ch_dw_command(c, p);
	} else {
		ch_puts(c, "FAIL 208 unknown command\r\n");
		c->state = CH_CLOSING;
	}
}

/* A byte the CoCo wrote to channel N. */
static void ch_write(unsigned n, uint8_t b)
{
	struct channel *c;
	double now;
	if (n >= CHANNELS || channels[n].state == CH_CLOSED || channels[n].state == CH_CLOSING)
		return;
	c = &channels[n];
	now = host_time();
	if (c->call != NULL) {
		/* A key while a modem dials stops it. */
		if (c->call_kind == CALL_AT) {
			netcall_abandon(&c->call);
			ch_reply(c, R_NO_CARRIER);
		}
		c->last_in = now;
		return;
	}
	if (c->state == CH_TCP || c->state == CH_ONLINE) {
		uint8_t two[2] = {b, 0xff};
		if (c->state == CH_ONLINE) {
			/* +++ after a quiet second: back to the modem (once a
			   second more has passed). */
			if ((b & 0x7f) == '+' && (c->plus > 0 || now - c->last_in >= 1.0)) {
				c->plus++;
				c->plus_time = now;
			} else {
				c->plus = 0;
			}
		}
		c->last_in = now;
		if (c->conn >= 0 && host_tcp_send(c->conn, two,
		                                  c->state == CH_ONLINE && b == 0xff ? 2 : 1) < 0) {
			/* noticed when it is read (ch_service) */
		}
		return;
	}
	c->last_in = now;
	b &= 0x7f;
	if (b == '\r' || b == '\n') {
		if (c->echo && b == '\r')
			ch_puts(c, "\r\n");
		ch_line(c);
	} else if (b == 8 || b == 127) {
		if (c->line_len > 0) {
			c->line_len--;
			if (c->echo)
				ch_puts(c, "\b \b");
		}
	} else if (b >= ' ') {
		if (c->line_len < CH_LINE - 1)
			c->line[c->line_len++] = (char) b;
		if (c->echo)
			ch_put(c, b);
	}
}

/* A channel's calls and connection looked at: what has come in. */
static void ch_service(struct channel *c)
{
	if (c->call != NULL) {
		int result = netcall_poll(&c->call);
		if (result != NETCALL_DIALING)
			ch_call_done(c, result);
	}
	if (c->state == CH_ONLINE && c->plus >= 3 && host_time() - c->plus_time >= 1.0) {
		c->plus = 0;
		c->state = CH_AT;
		ch_reply(c, R_OK);
	}
	if (c->conn >= 0 && (c->state == CH_TCP || c->state == CH_ONLINE || c->state == CH_AT)) {
		uint8_t buf[512];
		int got;
		while (ch_count(c) < CH_OUT - 1 - sizeof buf
		       && (got = host_tcp_recv(c->conn, buf, sizeof buf)) != 0) {
			if (got < 0) {
				/* The far end hung up. */
				LOG_PRINT("DriveWire channel %d: the call ended\n", (int) (c - channels));
				ch_hang_up(c);
				if (c->state == CH_TCP) {
					c->state = CH_CLOSING;
				} else {
					c->state = CH_AT;
					ch_reply(c, R_NO_CARRIER);
				}
				break;
			}
			if (c->state == CH_TCP) {
				int i;
				for (i = 0; i < got; i++)
					ch_put(c, buf[i]);
			} else if (c->state == CH_ONLINE) {
				telnet_receive(&c->telnet, c->conn, buf, got, ch_put, c);
			}
			/* (after +++, what comes is dropped) */
		}
	}
}

/* SERREAD: what a channel has for the CoCo, one at a time. */
static void serread(void)
{
	unsigned i;
	for (i = 0; i < CHANNELS; i++) {
		unsigned n = (next_channel + i) % CHANNELS;
		struct channel *c = &channels[n];
		unsigned count;
		if (c->state == CH_CLOSED)
			continue;
		ch_service(c);
		count = ch_count(c);
		if (count == 0 && c->state == CH_CLOSING) {
			/* Closed: told once, then it isn't open. */
			reply_byte(16);
			reply_byte((uint8_t) n);
			c->state = CH_CLOSED;
			channel_open[n] = 0;
			next_channel = (n + 1) % CHANNELS;
			return;
		}
		if (count == 0)
			continue;
		next_channel = (n + 1) % CHANNELS;
		if (count < 3) {
			reply_byte((uint8_t) (1 + n));
			reply_byte(ch_get(c));
		} else {
			reply_byte((uint8_t) (17 + n));
			reply_byte((uint8_t) (count > 255 ? 255 : count));
		}
		return;
	}
	reply_byte(0);
	reply_byte(0);
}

int dwserver_command_length(const uint8_t *in, int in_len, const uint8_t *open_channels)
{
	uint8_t op = in[0];
	int open = in_len >= 2 && open_channels[in[1]];
	if (op >= OP_FASTWRITE && op <= OP_FASTWRITE + 15)
		return open_channels[op - OP_FASTWRITE] ? 2 : 1;
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
		serread();
		break;
	case OP_SERREADM:
		if (in_len == 3) {
			int i;
			for (i = 0; i < in[2]; i++)
				data[i] = in[1] < CHANNELS ? ch_get(&channels[in[1]]) : 0;
			reply(data, in[2]);
		}
		break;
	case OP_SERWRITE:
		if (in_len == 3)
			ch_write(in[1], in[2]);
		break;
	case OP_SERWRITEM:
		if (in_len >= 3) {
			int i;
			for (i = 0; i < in[2]; i++)
				ch_write(in[1], in[3 + i]);
		}
		break;
	case OP_SERINIT:
		channel_open[in[1]] = 1;
		ch_open(in[1]);
		break;
	case OP_SERTERM:
		channel_open[in[1]] = 0;
		ch_close(in[1]);
		break;
	case OP_SERSETSTAT:
		if (in[2] == SS_OPEN) {
			channel_open[in[1]] = 1;
			ch_open(in[1]);
		} else if (in[2] == SS_CLOSE) {
			channel_open[in[1]] = 0;
			ch_close(in[1]);
		}
		break;
	case OP_INIT: {
		unsigned n;
		for (n = 0; n < CHANNELS; n++)
			ch_close(n);
		memset(channel_open, 0, sizeof(channel_open));
		break;
	}
	case OP_NAMEOBJ_MOUNT:
	case OP_NAMEOBJ_CREATE:
		/* No named objects: drive 0 means none. */
		reply_byte(0);
		break;
	default:
		if (in[0] >= OP_FASTWRITE && in[0] <= OP_FASTWRITE + 15 && in_len == 2)
			ch_write(in[0] - OP_FASTWRITE, in[1]);
		break;
	}
}

void dwserver_reset(void)
{
	unsigned n;
	channels_init();
	in_len = 0;
	await_checksum = 0;
	out_len = out_pos = 0;
	memset(channel_open, 0, sizeof(channel_open));
	for (n = 0; n < CHANNELS; n++)
		ch_close(n);
}

void dwserver_write(uint8_t byte)
{
	channels_init();
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
	if (in_len >= dwserver_command_length(in, in_len, channel_open)) {
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
