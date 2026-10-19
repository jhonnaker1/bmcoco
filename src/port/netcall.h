/*
 * netcall.h - outgoing TCP calls made in the background, and telnet, for
 * the RS-232 Pak's modem (rs232.c) and the DriveWire server's virtual
 * channels (dwserver.c).
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_NETCALL_H_
#define BMCOCO_NETCALL_H_

#include <stdint.h>

/* A call being made. */
struct netcall;

#define NETCALL_DIALING (-3)
#define NETCALL_NO_HOST (-2)
#define NETCALL_FAILED (-1)

/* Starts calling HOST on PORT (a number, as text) in the background:
   connecting can take minutes (host.h). NULL if it can't be started. */
struct netcall *netcall_start(const char *host, const char *port);

/* NETCALL_DIALING while the call is being made; then, once (and *CALL is
   freed and set to NULL): the connection (host_tcp_*), NETCALL_FAILED, or
   NETCALL_NO_HOST. */
int netcall_poll(struct netcall **call);

/* Stops waiting for a call (it is freed when it finishes). */
void netcall_abandon(struct netcall **call);

/* Splits an address, "host", "host:port" or "host port" (port DEFAULT if
   there's none), into HOST and PORT. Returns 0 if it isn't one. */
int netcall_address(const char *address, unsigned default_port,
                    char *host, unsigned host_size, char *port, unsigned port_size);

/* Telnet, from the far end: its option requests turned down (all but
   suppress go-ahead, and the far end's echo), the rest passed to PUT. */
struct telnet {
	int state;
	uint8_t verb;
};
void telnet_receive(struct telnet *t, int conn, const uint8_t *data, int n,
                    void (*put)(void *arg, uint8_t c), void *arg);

#endif
