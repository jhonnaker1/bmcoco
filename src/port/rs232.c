/*
 * rs232.c - the RS-232 Pak, with a virtual modem on it, for BMCoco.
 *
 * Tandy's RS-232 Pak is a 6551 ACIA at $FF68-$FF6B on the cartridge port.
 * Terminal programs (the Deluxe RS-232 Program Pak, Ultimaterm, Mikey-Term,
 * Greg-E-Term...) and NitrOS-9 (its /T2) talk to it, and on the other end
 * of its cable there was a modem. Here the modem is a virtual one, Hayes
 * compatible: ATDT bbs.example.com:23 opens a telnet connection.
 *
 * XRoar has no RS-232 Pak (its 6551 is a stand-in for the Dragon 64's, with
 * nothing behind it), and its list of cartridges can't be added to from
 * outside. But XRoar shows a cartridge every bus cycle, whatever the
 * address, so the Pak goes in beside the cartridge plugged in, like a
 * Y-cable or a Multi-Pak with both: cartbus.c takes over the cartridge's
 * read() and write(), and the Pak answers at $FF68-$FF6B. So there must be
 * a cartridge (Disk BASIC's controller usually is; with none, the Pak
 * isn't there). The rs232 option (Cartridge, RS-232 Pak) turns it on.
 *
 * The 6551, as far as programs see it:
 *  - $FF68 data: the byte received (reading it empties the receiver), and
 *    the byte to send. Bytes arrive as fast as the program takes them, and
 *    go out at once, whatever the baud rate.
 *  - $FF69 status: bit 3 a byte received, bit 4 ready to send (always),
 *    bit 5 no carrier (DCD high: no call, even when the modem has the
 *    line after +++), bit 6 DSR (always ready),
 *    bit 7 interrupt; writing it resets the chip.
 *  - $FF6A command: bit 0 DTR (off, as after a reset, disables the
 *    interrupts; dropping it hangs up a call, as a modem set to AT&D2
 *    does), bit 1 set disables the receive interrupt, bits 2-3 01 enables
 *    the transmit one, bit 4 echo. Unlike the real chip, the receiver and
 *    transmitter work with DTR off too.
 *  - $FF6B control: the baud rate (shown in CONNECT) and the frame.
 *  The interrupt goes to the cartridge port's CART line (FIRQ).
 *
 * The modem: AT commands (upper or lower case) until a call connects, then
 * everything to the far end, until +++ (with a second of quiet before and
 * after) gets the modem back, or the call ends (NO CARRIER). D[T|P]host
 * [:port] (port 23 if none) dials, in the background, as the Becker port
 * connects (see becker.c): Circle has no non-blocking connect, and a host
 * that doesn't answer takes it minutes to give up on, while the CoCo goes
 * on. H hangs up, O goes back online, Z resets, E0/E1 echo, V0/V1 number
 * or word replies, Q0/Q1 replies on or off, I says what it is; the rest
 * (&F, &C1, &D2, S0=0, X4, L, M...: what terminal programs set modems up
 * with) answer OK. There are no incoming calls: A says NO CARRIER.
 *
 * The far end is spoken to as telnet: its option requests are turned down
 * (all but suppress go-ahead and its echo), and a $FF byte is sent twice.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "machine.h"
#include "part.h"
#include "xroar.h"

#include "../host.h"
#include "bmcoco.h"
#include "netcall.h"

#define RX_SIZE 4096
#define TX_SIZE 1024
#define CMD_SIZE 80
#define GUARD_SECONDS 1.0

/* 6551 status bits */
#define ST_RDRF 0x08
#define ST_TDRE 0x10
#define ST_DCD  0x20    /* set: no carrier */
#define ST_DSR  0x40    /* set: not ready */
#define ST_IRQ  0x80

enum { OFFLINE, ONLINE };   /* the modem: taking commands, or the call's */

static struct {
	int enabled;

	/* The 6551 */
	uint8_t command, control, rx_data;
	int rx_full;
	int irq_flag;    /* status bit 7: something to say */
	int irq_line;

	/* What the modem has for the CoCo (its replies and the call's), and
	   what the CoCo sent the far end. */
	uint8_t rx[RX_SIZE];
	unsigned rx_head, rx_tail;
	uint8_t tx[TX_SIZE];
	unsigned tx_len;

	/* The modem */
	int mode;
	int conn;               /* the call's connection, or -1 */
	struct netcall *dialing;
	char address[72];       /* host:port called */
	int echo, verbose, quiet;
	char cmd[CMD_SIZE];
	unsigned cmd_len;
	double last_tx;         /* when the CoCo last sent a byte */
	int plus;               /* '+' in a row after a quiet second */
	double plus_time;

	struct telnet telnet;
} rs = { .conn = -1, .echo = 1, .verbose = 1 };

/* ---- The 6551 ------------------------------------------------------------ */

static unsigned rx_count(void)
{
	return (rs.rx_head - rs.rx_tail) % RX_SIZE;
}

static void rx_put(uint8_t c)
{
	if (rx_count() < RX_SIZE - 1) {
		rs.rx[rs.rx_head] = c;
		rs.rx_head = (rs.rx_head + 1) % RX_SIZE;
	}
}

static void rx_puts(const char *s)
{
	while (*s)
		rx_put((uint8_t) *s++);
}

static void update_irq(void)
{
	int irq = 0;
	/* DTR off (command bit 0 clear, as after a reset) disables all the
	   interrupts. (The CART line they go to means "start the cartridge"
	   to Color BASIC.) */
	if ((rs.command & 0x01) && rs.rx_full && !(rs.command & 0x02))
		irq = 1;
	if ((rs.command & 0x01) && (rs.command & 0x0c) == 0x04)
		irq = 1;   /* transmit interrupt: always ready */
	if (irq)
		rs.irq_flag = 1;
	irq = irq && rs.irq_flag;
	if (irq != rs.irq_line && cartbus_cart() != NULL) {
		rs.irq_line = irq;
		DELEGATE_SAFE_CALL(cartbus_cart()->signal_firq, irq);
	}
}

/* The next byte for the CoCo into the receive register, if it's free. */
static void rx_load(void)
{
	if (rs.rx_full || rx_count() == 0)
		return;
	rs.rx_data = rs.rx[rs.rx_tail];
	rs.rx_tail = (rs.rx_tail + 1) % RX_SIZE;
	rs.rx_full = 1;
	update_irq();
}

static void acia_reset(void)
{
	rs.command = 0;
	rs.control = 0;
	rs.rx_full = 0;
	rs.irq_flag = 0;
	update_irq();
}

static const unsigned baud_rates[16] = {
	0, 50, 75, 110, 135, 150, 300, 600, 1200, 1800, 2400, 3600, 4800, 7200,
	9600, 19200
};

static void modem_from_coco(uint8_t c);
static void hang_up(void);

static uint8_t acia_read(unsigned reg, uint8_t D)
{
	switch (reg) {
	case 0:
		D = rs.rx_data;
		rs.rx_full = 0;
		rx_load();
		update_irq();
		return D;
	case 1:
		rx_load();
		D = ST_TDRE | (rs.rx_full ? ST_RDRF : 0)
		    | (rs.conn >= 0 ? 0 : ST_DCD)
		    | (rs.irq_flag ? ST_IRQ : 0);
		rs.irq_flag = 0;
		update_irq();
		return D;
	case 2:
		return rs.command;
	default:
		return rs.control;
	}
}

static void acia_write(unsigned reg, uint8_t D)
{
	switch (reg) {
	case 0:
		if (rs.command & 0x10)
			rx_put(D);      /* echo mode */
		modem_from_coco(D);
		break;
	case 1:
		/* Programmed reset: the command register's low bits cleared
		   (and so DTR dropped). */
		D = rs.command & 0xe0;
		/* fall through */
	case 2:
		if ((rs.command & 0x01) && !(D & 0x01) && rs.conn >= 0)
			hang_up();      /* DTR dropped (AT&D2) */
		rs.command = D;
		update_irq();
		break;
	default:
		rs.control = D;
		break;
	}
}

/* ---- The cartridge's bus, with the Pak beside it (cartbus.c) ------------- */

uint8_t bmcoco_rs232_bus_read(uint16_t A, bool P2, bool R2, uint8_t D)
{
	if (rs.enabled && !P2 && !R2 && (A & 0xfffc) == 0xff68)
		D = acia_read(A & 3, D);
	return D;
}

void bmcoco_rs232_bus_write(uint16_t A, bool P2, bool R2, uint8_t D)
{
	if (rs.enabled && !P2 && !R2 && (A & 0xfffc) == 0xff68)
		acia_write(A & 3, D);
}

void bmcoco_rs232_bus_reset(void)
{
	acia_reset();
}

void bmcoco_rs232_bus_changed(void)
{
	/* Beside another cartridge, or none: its CART line isn't ours. */
	rs.irq_line = 0;
}

/* ---- The modem --------------------------------------------------------- */

enum { R_OK, R_CONNECT, R_RING, R_NO_CARRIER, R_ERROR };

static void reply(int code)
{
	static const char *const words[] = {
		"OK", "CONNECT", "RING", "NO CARRIER", "ERROR"
	};
	char text[32];
	if (rs.quiet)
		return;
	if (!rs.verbose) {
		snprintf(text, sizeof text, "%d\r", code);
	} else if (code == R_CONNECT && baud_rates[rs.control & 15] > 0) {
		snprintf(text, sizeof text, "\r\nCONNECT %u\r\n", baud_rates[rs.control & 15]);
	} else {
		snprintf(text, sizeof text, "\r\n%s\r\n", words[code]);
	}
	rx_puts(text);
}

static void hang_up(void)
{
	netcall_abandon(&rs.dialing);
	if (rs.conn >= 0) {
		host_tcp_close(rs.conn);
		rs.conn = -1;
	}
	rs.mode = OFFLINE;
	rs.tx_len = 0;
	rs.telnet.state = 0;
}

static void dial(const char *where)
{
	char host[64], port[8];
	if (!netcall_address(where, 23, host, sizeof host, port, sizeof port)) {
		reply(R_ERROR);
		return;
	}
	hang_up();
	rs.dialing = netcall_start(host, port);
	if (rs.dialing == NULL) {
		reply(R_NO_CARRIER);
		return;
	}
	snprintf(rs.address, sizeof rs.address, "%s:%s", host, port);
	LOG_PRINT("RS-232 Pak: dialling %s\n", rs.address);
}

/* A dial finished: CONNECT, or NO CARRIER. */
static void dial_poll(void)
{
	int result;
	if (rs.dialing == NULL)
		return;
	result = netcall_poll(&rs.dialing);
	if (result == NETCALL_DIALING)
		return;
	if (result >= 0) {
		rs.conn = result;
		rs.mode = ONLINE;
		rs.telnet.state = 0;
		LOG_PRINT("RS-232 Pak: connected to %s\n", rs.address);
		reply(R_CONNECT);
	} else {
		LOG_WARN("RS-232 Pak: %s %s\n", result == NETCALL_NO_HOST ? "can't find" : "can't connect to",
		         rs.address);
		reply(R_NO_CARRIER);
	}
}

/* One command line ("AT..."), case and high bits ignored. */
static void command_line(void)
{
	char *p = rs.cmd;
	int result = R_OK;
	rs.cmd[rs.cmd_len] = '\0';
	rs.cmd_len = 0;
	while (*p == ' ')
		p++;
	if (tolower((unsigned char) p[0]) != 'a' || tolower((unsigned char) p[1]) != 't')
		return;     /* not a command: ignored, as a modem does */
	p += 2;
	while (*p) {
		int c = tolower((unsigned char) *p++);
		int arg = 0;
		if (c == ' ')
			continue;
		if (c == 'd') {
			if (tolower((unsigned char) *p) == 't' || tolower((unsigned char) *p) == 'p')
				p++;
			dial(p);
			return;     /* the rest of the line is the address */
		}
		if (c == '&' || c == '\\' || c == '%') {
			/* &F, &C1, &D2...: settings that don't matter here */
			if (*p)
				p++;
			while (isdigit((unsigned char) *p))
				p++;
			continue;
		}
		if (c == 's') {
			/* S0=0, S7=60, S0?: registers that don't matter here */
			while (isdigit((unsigned char) *p))
				p++;
			if (*p == '=') {
				p++;
				while (isdigit((unsigned char) *p))
					p++;
			} else if (*p == '?') {
				p++;
				rx_puts("\r\n000\r\n");
			}
			continue;
		}
		if (isdigit((unsigned char) *p))
			arg = *p++ - '0';
		while (isdigit((unsigned char) *p))
			p++;
		switch (c) {
		case 'e': rs.echo = arg; break;
		case 'v': rs.verbose = arg; break;
		case 'q': rs.quiet = arg; break;
		case 'h': hang_up(); break;
		case 'z':
			hang_up();
			rs.echo = rs.verbose = 1;
			rs.quiet = 0;
			break;
		case 'o':
			if (rs.conn >= 0) {
				rs.mode = ONLINE;
				reply(R_CONNECT);
				return;
			}
			result = R_NO_CARRIER;
			break;
		case 'a':
			/* No incoming calls. */
			result = R_NO_CARRIER;
			break;
		case 'i':
			rx_puts("\r\nBMCoco virtual modem\r\n");
			break;
		default:
			/* B, L, M, X...: fine */
			break;
		}
	}
	reply(result);
}

/* A byte the CoCo sent. */
static void modem_from_coco(uint8_t c)
{
	double now = host_time();
	if (rs.dialing != NULL) {
		/* A key while dialling stops it. */
		netcall_abandon(&rs.dialing);
		reply(R_NO_CARRIER);
		rs.last_tx = now;
		return;
	}
	if (rs.mode == ONLINE) {
		/* +++ after a quiet second: back to the modem (once a second
		   more has passed with nothing else). */
		if ((c & 0x7f) == '+' && (rs.plus > 0 || now - rs.last_tx >= GUARD_SECONDS)) {
			rs.plus++;
			rs.plus_time = now;
		} else {
			rs.plus = 0;
		}
		rs.last_tx = now;
		if (rs.tx_len + 2 <= TX_SIZE) {
			rs.tx[rs.tx_len++] = c;
			if (c == 0xff)
				rs.tx[rs.tx_len++] = 0xff;   /* telnet: $FF twice */
		}
		return;
	}
	rs.last_tx = now;
	c &= 0x7f;
	if (c == '\r') {
		if (rs.echo)
			rx_puts("\r\n");
		command_line();
	} else if (c == 8 || c == 127) {
		if (rs.cmd_len > 0) {
			rs.cmd_len--;
			if (rs.echo)
				rx_puts("\b \b");
		}
	} else if (c >= ' ') {
		if (rs.cmd_len < CMD_SIZE - 1)
			rs.cmd[rs.cmd_len++] = (char) c;
		if (rs.echo)
			rx_put(c);
	}
}

static void rx_put_cb(void *arg, uint8_t c)
{
	(void) arg;
	rx_put(c);
}

/* ---- Once a frame -------------------------------------------------------- */

void bmcoco_rs232_settings_changed(void)
{
	rs.enabled = host_option_int("rs232", 0) != 0;
	if (!rs.enabled) {
		hang_up();
		if (rs.irq_line && cartbus_cart() != NULL)
			DELEGATE_SAFE_CALL(cartbus_cart()->signal_firq, 0);
		rs.irq_line = 0;
	}
}

int bmcoco_rs232_enabled(void)
{
	return rs.enabled;
}

void bmcoco_rs232_frame(void)
{
	double now;
	if (!rs.enabled)
		return;
	dial_poll();
	now = host_time();
	if (rs.mode == ONLINE && rs.plus >= 3 && now - rs.plus_time >= GUARD_SECONDS) {
		/* +++ and a quiet second: the modem, the call kept. The +++
		   went to the far end too, as a real modem's do. */
		rs.plus = 0;
		rs.mode = OFFLINE;
		reply(R_OK);
	}
	if (rs.conn >= 0) {
		uint8_t buf[512];
		int got;
		if (rs.tx_len > 0) {
			int sent = host_tcp_send(rs.conn, rs.tx, rs.tx_len);
			if (sent > 0) {
				memmove(rs.tx, rs.tx + sent, rs.tx_len - (unsigned) sent);
				rs.tx_len -= (unsigned) sent;
			} else if (sent < 0) {
				goto closed;
			}
		}
		while (rx_count() < RX_SIZE - sizeof buf
		       && (got = host_tcp_recv(rs.conn, buf, sizeof buf)) != 0) {
			if (got < 0)
				goto closed;
			if (rs.mode == ONLINE)
				telnet_receive(&rs.telnet, rs.conn, buf, got, rx_put_cb, NULL);
		}
	}
	rx_load();
	return;
closed:
	LOG_PRINT("RS-232 Pak: the call ended\n");
	hang_up();
	reply(R_NO_CARRIER);
	rx_load();
}
