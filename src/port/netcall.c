/*
 * netcall.c - outgoing TCP calls made in the background, and telnet.
 *
 * A call is made by a background job (host_run_in_background): Circle has
 * no non-blocking connect, and a host that doesn't answer takes it minutes
 * to give up on, while the CoCo must go on (as the Becker port connects,
 * becker.c). Whichever finishes with a call last, the job or the caller
 * giving up on it, frees it: its state goes from DIALING to DONE (the job)
 * or ABANDONED (the caller) just once.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host.h"
#include "netcall.h"

enum { DIALING, DONE, ABANDONED };

struct netcall {
	char host[64];
	char port[8];
	int state;
	int result;     /* when DONE: the connection, NETCALL_FAILED or _NO_HOST */
};

static void call_job(void *arg)
{
	struct netcall *c = arg;
	int expected = DIALING;
	int fd = host_tcp_connect(c->host, c->port);
	c->result = fd >= 0 ? fd : fd == -2 ? NETCALL_NO_HOST : NETCALL_FAILED;
	if (!__atomic_compare_exchange_n(&c->state, &expected, DONE, 0,
	                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		/* given up on */
		if (fd >= 0)
			host_tcp_close(fd);
		free(c);
	}
}

struct netcall *netcall_start(const char *host, const char *port)
{
	struct netcall *c = calloc(1, sizeof(*c));
	if (c == NULL)
		return NULL;
	snprintf(c->host, sizeof c->host, "%s", host);
	snprintf(c->port, sizeof c->port, "%s", port);
	c->state = DIALING;
	c->result = NETCALL_FAILED;
	if (!host_run_in_background(call_job, c)) {
		free(c);
		return NULL;
	}
	return c;
}

int netcall_poll(struct netcall **call)
{
	struct netcall *c = *call;
	int result;
	if (c == NULL)
		return NETCALL_FAILED;
	if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) != DONE)
		return NETCALL_DIALING;
	result = c->result;
	free(c);
	*call = NULL;
	return result;
}

void netcall_abandon(struct netcall **call)
{
	struct netcall *c = *call;
	int expected = DIALING;
	if (c == NULL)
		return;
	*call = NULL;
	if (!__atomic_compare_exchange_n(&c->state, &expected, ABANDONED, 0,
	                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		/* it had finished */
		if (c->result >= 0)
			host_tcp_close(c->result);
		free(c);
	}
}

int netcall_address(const char *address, unsigned default_port,
                    char *host, unsigned host_size, char *port, unsigned port_size)
{
	const char *sep, *p;
	unsigned long number = default_port;
	size_t n;
	while (*address == ' ')
		address++;
	sep = strchr(address, ':');
	if (sep == NULL)
		sep = strchr(address, ' ');
	n = sep ? (size_t) (sep - address) : strlen(address);
	while (n > 0 && address[n - 1] == ' ')
		n--;
	if (n == 0 || n >= host_size)
		return 0;
	memcpy(host, address, n);
	host[n] = '\0';
	if (sep != NULL) {
		char *end;
		p = sep + 1;
		while (*p == ' ')
			p++;
		if (*p != '\0') {
			number = strtoul(p, &end, 10);
			while (*end == ' ' || *end == '\r' || *end == '\n')
				end++;
			if (end == p || *end != '\0' || number == 0 || number > 65535)
				return 0;
		}
	}
	snprintf(port, port_size, "%lu", number);
	return 1;
}

static void telnet_reply(int conn, uint8_t verb, uint8_t option)
{
	uint8_t out[3] = {0xff, verb, option};
	if (conn >= 0)
		host_tcp_send(conn, out, 3);
}

void telnet_receive(struct telnet *t, int conn, const uint8_t *data, int n,
                    void (*put)(void *arg, uint8_t c), void *arg)
{
	int i;
	for (i = 0; i < n; i++) {
		uint8_t c = data[i];
		switch (t->state) {
		case 0:
			if (c == 0xff)
				t->state = 1;
			else
				put(arg, c);
			break;
		case 1:          /* after IAC */
			if (c == 0xff) {
				put(arg, 0xff);
				t->state = 0;
			} else if (c >= 251 && c <= 254) {
				t->verb = c;
				t->state = 2;
			} else if (c == 250) {
				t->state = 3;   /* SB ... IAC SE */
			} else {
				t->state = 0;
			}
			break;
		case 2:          /* WILL, WONT, DO, DONT and the option */
			/* Suppress go-ahead (3) both ways, and the far end's echo
			   (1); nothing else. */
			if (t->verb == 251)          /* WILL */
				telnet_reply(conn, c == 1 || c == 3 ? 253 : 254, c);
			else if (t->verb == 253)     /* DO */
				telnet_reply(conn, c == 3 ? 251 : 252, c);
			t->state = 0;
			break;
		case 3:          /* in a subnegotiation */
			if (c == 0xff)
				t->state = 4;
			break;
		default:
			t->state = c == 240 ? 0 : 3;
			break;
		}
	}
}
