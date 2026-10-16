/*
 * becker.c - XRoar's Becker port, for BMCoco.
 *
 * The Becker port connects the CoCo to a DriveWire server, which serves
 * it disk images and more. A cartridge with a Becker port (RS-DOS with
 * Becker port, or the IDE interface) uses it with HDB-DOS or NitrOS-9's
 * DriveWire drivers. This is XRoar's becker.c (the same interface,
 * becker.h), made to suit a bare metal Pi:
 *
 *  - The server is BMCoco's own (src/port/dwserver.c) unless an address
 *    is set for one on another computer (a PC running DriveWire 4, or
 *    pyDriveWire): no address, "builtin", or this Pi's own (127.0.0.1,
 *    localhost: nothing else can listen there) mean the built-in one.
 *  - Nothing ever waits: the socket is read and written with MSG_DONTWAIT
 *    (Circle has no fcntl() for sockets), so the emulation goes on while
 *    the server is slow, gone, or not there yet.
 *  - It connects once the Pi has a network address, and if the server
 *    can't be reached it tries again 10 seconds later, whenever the CoCo
 *    looks at the port; a server that closes the connection is the same.
 *    What the CoCo wrote before it connected (up to 16 bytes) is sent
 *    then.
 *  - Connecting happens in the background (host_run_in_background), as
 *    Circle has no non-blocking connect, and a server that doesn't answer
 *    (no DriveWire server at that address) takes Circle about 3 minutes
 *    to give up on. Until it connects, the port reads as empty, which
 *    NitrOS-9 and HDB-DOS take as no DriveWire server.
 *
 * The server is becker_ip and becker_port in cmdline.txt (or the menu's
 * DriveWire page): XRoar's -becker-ip and -becker-port.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "xalloc.h"

#include "becker.h"
#include "logging.h"
#include "xroar.h"

#include "../host.h"
#include "dwserver.h"

#define INPUT_BUFFER_SIZE 262
#define OUTPUT_BUFFER_SIZE 16
#define RETRY_SECONDS 10.0

/* A connection being made in the background. Whichever finishes with it
   last, the background job or the Becker port giving up on it, frees it:
   state goes from CONNECTING to DONE (the job) or ABANDONED (the port)
   just once. */
enum { CONNECTING, DONE, ABANDONED };
struct connection {
	char host[64];
	char port[16];
	int state;
	int fd;          /* when DONE: the socket, or -1 */
	int no_host;     /* when DONE: the name couldn't be found */
};

struct becker {
	int sockfd;
	struct connection *connecting;
	double next_try;
	int failed_logged;
	uint8_t input_buf[INPUT_BUFFER_SIZE];
	int input_buf_ptr;
	int input_buf_length;
	uint8_t output_buf[OUTPUT_BUFFER_SIZE];
	int output_buf_length;
	int builtin;  /* talking to the built-in server */
};

/* TRUE if the server is the built-in one. Looked at each time, so that
   the menu can change it. */
static int builtin_server(void)
{
	const char *ip = xroar.cfg.becker.ip;
	if (ip == NULL || ip[0] == '\0' || strcasecmp(ip, "builtin") == 0)
		return 1;
#ifndef BMCOCO_HOST_LIBC
	/* On the Pi, nothing else can be here. (On a PC a server can.) */
	if (strcmp(ip, "127.0.0.1") == 0 || strcasecmp(ip, "localhost") == 0)
		return 1;
#endif
	return 0;
}

static void disconnect(struct becker *b);

/* TRUE if B uses the built-in server; starts using it, or stops, if the
   setting changed. */
static int use_builtin(struct becker *b)
{
	int builtin = builtin_server();
	if (builtin != b->builtin) {
		disconnect(b);
		dwserver_reset();
		b->builtin = builtin;
		if (builtin)
			LOG_PRINT("Becker port: the built-in DriveWire server\n");
	}
	return builtin;
}

static void abandon(struct becker *b);

static void disconnect(struct becker *b)
{
	abandon(b);
	if (b->sockfd >= 0)
		close(b->sockfd);
	b->sockfd = -1;
	b->input_buf_ptr = b->input_buf_length = 0;
	b->output_buf_length = 0;
}

/* The background job: looks up the server and connects to it, which may
   wait for minutes. */
static void connect_job(void *arg)
{
	struct connection *c = arg;
	struct addrinfo hints, *info = NULL;
	int fd = -1, no_host = 0, expected = CONNECTING;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(c->host, c->port, &hints, &info) != 0 || info == NULL) {
		no_host = 1;
	} else {
		fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (fd >= 0 && connect(fd, info->ai_addr, info->ai_addrlen) < 0) {
			close(fd);
			fd = -1;
		}
		freeaddrinfo(info);
	}
	c->fd = fd;
	c->no_host = no_host;
	if (!__atomic_compare_exchange_n(&c->state, &expected, DONE, 0,
	                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		/* The Becker port has gone (the cartridge was taken out). */
		if (fd >= 0)
			close(fd);
		free(c);
	}
}

/* Stops waiting for a connection being made. */
static void abandon(struct becker *b)
{
	struct connection *c = b->connecting;
	int expected = CONNECTING;
	if (c == NULL)
		return;
	b->connecting = NULL;
	if (!__atomic_compare_exchange_n(&c->state, &expected, ABANDONED, 0,
	                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		/* It had finished. */
		if (c->fd >= 0)
			close(c->fd);
		free(c);
	}
}

/* Connects to the server, if it isn't connected and it's time to try:
   starts connecting in the background, and later takes the connection. */
static void try_connect(struct becker *b)
{
	const char *host = xroar.cfg.becker.ip ? xroar.cfg.becker.ip : BECKER_IP_DEFAULT;
	const char *port = xroar.cfg.becker.port ? xroar.cfg.becker.port : BECKER_PORT_DEFAULT;
	char address[32];
	double now = host_time();
	struct connection *c = b->connecting;

	if (b->sockfd >= 0)
		return;
	if (c != NULL) {
		if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) != DONE)
			return;
		b->connecting = NULL;
		b->next_try = now + RETRY_SECONDS;
		if (c->fd >= 0) {
			b->sockfd = c->fd;
			b->failed_logged = 0;
			LOG_PRINT("Becker port: connected to DriveWire server %s:%s\n", c->host, c->port);
		} else if (!b->failed_logged) {
			if (c->no_host)
				LOG_WARN("Becker port: can't find DriveWire server %s:%s\n", c->host, c->port);
			else
				LOG_WARN("Becker port: can't connect to DriveWire server %s:%s; "
				         "trying again every %.0f s\n", c->host, c->port, RETRY_SECONDS);
			b->failed_logged = 1;
		}
		free(c);
		return;
	}
	if (now < b->next_try)
		return;
	/* No network yet (it starts after the emulator): look again in a
	   second. */
	if (!host_network_address(address, sizeof(address))) {
		b->next_try = now + 1.0;
		return;
	}
	b->next_try = now + RETRY_SECONDS;

	c = xmalloc(sizeof(*c));
	*c = (struct connection){0};
	snprintf(c->host, sizeof(c->host), "%s", host);
	snprintf(c->port, sizeof(c->port), "%s", port);
	c->state = CONNECTING;
	c->fd = -1;
	if (!host_run_in_background(connect_job, c)) {
		free(c);
		return;
	}
	b->connecting = c;
}

static void write_output(struct becker *b);

static void fetch_input(struct becker *b)
{
	ssize_t got;
	if (b->sockfd < 0 || b->input_buf_ptr < b->input_buf_length)
		return;
	got = recv(b->sockfd, b->input_buf, INPUT_BUFFER_SIZE, MSG_DONTWAIT);
	if (got > 0) {
		b->input_buf_ptr = 0;
		b->input_buf_length = (int) got;
	} else if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
		LOG_WARN("Becker port: the DriveWire server closed the connection\n");
		disconnect(b);
	}
}

static void write_output(struct becker *b)
{
	ssize_t sent;
	if (b->sockfd < 0 || b->output_buf_length == 0)
		return;
	sent = send(b->sockfd, b->output_buf, (size_t) b->output_buf_length, MSG_DONTWAIT);
	if (sent > 0) {
		memmove(b->output_buf, b->output_buf + sent, (size_t) (b->output_buf_length - sent));
		b->output_buf_length -= (int) sent;
	} else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		LOG_WARN("Becker port: can't send to the DriveWire server\n");
		disconnect(b);
	}
}

struct becker *becker_open(void)
{
	struct becker *b = xmalloc(sizeof(*b));
	*b = (struct becker){0};
	b->sockfd = -1;
	b->builtin = -1;
	if (!use_builtin(b))
		try_connect(b);
	return b;
}

void becker_close(struct becker *b)
{
	if (b == NULL)
		return;
	disconnect(b);
	free(b);
}

void becker_reset(struct becker *b)
{
	if (b->builtin == 1)
		dwserver_reset();
}

uint8_t becker_read_status(struct becker *b)
{
	if (use_builtin(b))
		return dwserver_ready() ? 0x02 : 0x00;
	try_connect(b);
	write_output(b);   /* anything written before it connected */
	fetch_input(b);
	return b->input_buf_ptr < b->input_buf_length ? 0x02 : 0x00;
}

uint8_t becker_read_data(struct becker *b)
{
	uint8_t r;
	if (use_builtin(b))
		return dwserver_read();
	fetch_input(b);
	if (b->input_buf_ptr >= b->input_buf_length)
		return 0x00;
	r = b->input_buf[b->input_buf_ptr++];
	if (b->input_buf_ptr == b->input_buf_length)
		b->input_buf_ptr = b->input_buf_length = 0;
	return r;
}

void becker_write_data(struct becker *b, uint8_t D)
{
	if (use_builtin(b)) {
		dwserver_write(D);
		return;
	}
	try_connect(b);
	if (b->output_buf_length < OUTPUT_BUFFER_SIZE)
		b->output_buf[b->output_buf_length++] = D;
	write_output(b);
}
