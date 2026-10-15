/*
 * becker.c - XRoar's Becker port, for BMCoco.
 *
 * The Becker port connects the CoCo to a DriveWire server over TCP (a PC
 * running DriveWire 4, or pyDriveWire, for example), which serves it disk
 * images and more. A cartridge with a Becker port (RS-DOS with Becker
 * port, or the IDE interface) uses it with HDB-DOS or NitrOS-9's DriveWire
 * drivers. This is XRoar's becker.c (the same interface, becker.h), made
 * to suit a bare metal Pi:
 *
 *  - Nothing ever waits: the socket is read and written with MSG_DONTWAIT
 *    (Circle has no fcntl() for sockets), so the emulation goes on while
 *    the server is slow, gone, or not there yet.
 *  - It connects once the Pi has a network address, and if the server
 *    can't be reached it tries again every 10 seconds, whenever the CoCo
 *    looks at the port; a server that closes the connection is the same.
 *    What the CoCo wrote before it connected (up to 16 bytes) is sent
 *    then.
 *    (Connecting itself does wait, briefly: Circle has no non-blocking
 *    connect.)
 *
 * The server is becker_ip and becker_port in cmdline.txt (or the menu's
 * Network page): XRoar's -becker-ip and -becker-port.
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
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "xalloc.h"

#include "becker.h"
#include "logging.h"
#include "xroar.h"

#include "../host.h"

#define INPUT_BUFFER_SIZE 262
#define OUTPUT_BUFFER_SIZE 16
#define RETRY_SECONDS 10.0

struct becker {
	int sockfd;
	double next_try;
	int failed_logged;
	uint8_t input_buf[INPUT_BUFFER_SIZE];
	int input_buf_ptr;
	int input_buf_length;
	uint8_t output_buf[OUTPUT_BUFFER_SIZE];
	int output_buf_length;
};

static void disconnect(struct becker *b)
{
	if (b->sockfd >= 0)
		close(b->sockfd);
	b->sockfd = -1;
	b->input_buf_ptr = b->input_buf_length = 0;
	b->output_buf_length = 0;
}

/* Connects to the server, if it isn't connected and it's time to try. */
static void try_connect(struct becker *b)
{
	const char *host = xroar.cfg.becker.ip ? xroar.cfg.becker.ip : BECKER_IP_DEFAULT;
	const char *port = xroar.cfg.becker.port ? xroar.cfg.becker.port : BECKER_PORT_DEFAULT;
	struct addrinfo hints, *info = NULL;
	char address[32];
	double now = host_time();
	int fd;

	if (b->sockfd >= 0 || now < b->next_try)
		return;
	/* No network yet (it starts after the emulator): look again in a
	   second. */
	if (!host_network_address(address, sizeof(address))) {
		b->next_try = now + 1.0;
		return;
	}
	b->next_try = now + RETRY_SECONDS;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &info) != 0 || info == NULL) {
		if (!b->failed_logged)
			LOG_WARN("Becker port: can't find DriveWire server %s:%s\n", host, port);
		b->failed_logged = 1;
		return;
	}
	fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (fd < 0 || connect(fd, info->ai_addr, info->ai_addrlen) < 0) {
		if (!b->failed_logged)
			LOG_WARN("Becker port: can't connect to DriveWire server %s:%s; "
			         "trying again every %.0f s\n", host, port, RETRY_SECONDS);
		b->failed_logged = 1;
		if (fd >= 0)
			close(fd);
		freeaddrinfo(info);
		return;
	}
	freeaddrinfo(info);
	b->sockfd = fd;
	b->failed_logged = 0;
	LOG_PRINT("Becker port: connected to DriveWire server %s:%s\n", host, port);
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
	(void) b;
}

uint8_t becker_read_status(struct becker *b)
{
	try_connect(b);
	write_output(b);   /* anything written before it connected */
	fetch_input(b);
	return b->input_buf_ptr < b->input_buf_length ? 0x02 : 0x00;
}

uint8_t becker_read_data(struct becker *b)
{
	uint8_t r;
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
	try_connect(b);
	if (b->output_buf_length < OUTPUT_BUFFER_SIZE)
		b->output_buf[b->output_buf_length++] = D;
	write_output(b);
}
