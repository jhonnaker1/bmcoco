/*
 * live.c - the host simulator's live mode: BMCoco in a web page.
 *
 * A small HTTP server on 127.0.0.1, polled from the simulator's main loop
 * (no threads), so the emulator can be seen and played on a PC:
 *
 *   GET /             live.html
 *   GET /frame        the screen: width, height (16 bit little endian each),
 *                     then RGB565 pixels, little endian
 *   GET /audio        16 bit mono samples at 44100 Hz made since last asked
 *   GET /key?u=U&p=P  key with USB HID usage U pressed (P=1) or released
 *   GET /pad?n=N&b=B&x=X&y=Y  gamepad N: HOST_PAD_* bits, left stick
 *   GET /mouse?x=DX&y=DY&b=B  the mouse moved DX, DY; B its buttons
 *
 * Only for testing on your own computer: it listens on 127.0.0.1 only.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "live.h"

#ifndef LIVE_HTML
#define LIVE_HTML "live.html"
#endif

static int listen_fd = -1;

/* Sound waiting to be fetched: up to two seconds. */
#define AUDIO_MAX (44100 * 2)
static int16_t audio[AUDIO_MAX];
static unsigned audio_len;

int live_init(int port)
{
	struct sockaddr_in addr;
	int one = 1;

	signal(SIGPIPE, SIG_IGN);
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket");
		return 0;
	}
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short) port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(listen_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0
	    || listen(listen_fd, 16) < 0) {
		perror("bind");
		return 0;
	}
	fcntl(listen_fd, F_SETFL, O_NONBLOCK);
	fprintf(stderr, "[live] BMCoco at http://127.0.0.1:%d/\n", port);
	return 1;
}

void live_audio(const int16_t *samples, unsigned n)
{
	if (n > AUDIO_MAX)
		n = AUDIO_MAX;
	if (audio_len + n > AUDIO_MAX) {
		/* Nobody is listening: keep the newest. */
		unsigned drop = audio_len + n - AUDIO_MAX;
		memmove(audio, audio + drop, (audio_len - drop) * sizeof(*audio));
		audio_len -= drop;
	}
	memcpy(audio + audio_len, samples, n * sizeof(*samples));
	audio_len += n;
}

static void send_all(int fd, const void *data, size_t len)
{
	const char *p = data;
	while (len > 0) {
		ssize_t n = send(fd, p, len, 0);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			return;
		}
		p += n;
		len -= (size_t) n;
	}
}

static void reply(int fd, const char *type, const void *body, size_t len)
{
	char head[256];
	int n = snprintf(head, sizeof(head),
	                 "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
	                 "Cache-Control: no-store\r\nConnection: close\r\n\r\n", type, len);
	send_all(fd, head, (size_t) n);
	if (len > 0)
		send_all(fd, body, len);
}

static void not_found(int fd)
{
	static const char msg[] = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	send_all(fd, msg, sizeof(msg) - 1);
}

/* The number after NAME= in the query, or DEFAULT. */
static int param(const char *query, const char *name, int dflt)
{
	size_t len = strlen(name);
	const char *p = query;
	while (p != NULL && *p != '\0') {
		if (strncmp(p, name, len) == 0 && p[len] == '=')
			return atoi(p + len + 1);
		p = strchr(p, '&');
		if (p != NULL)
			p++;
	}
	return dflt;
}

static void serve_page(int fd)
{
	FILE *f = fopen(LIVE_HTML, "rb");
	char *buf;
	long len;
	if (f == NULL) {
		not_found(fd);
		return;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t) len);
	if (buf != NULL && fread(buf, 1, (size_t) len, f) == (size_t) len)
		reply(fd, "text/html; charset=utf-8", buf, (size_t) len);
	else
		not_found(fd);
	free(buf);
	fclose(f);
}

static void serve_frame(int fd)
{
	int w, h, pitch, y;
	const uint16_t *src = sim_screen(&w, &h, &pitch);
	size_t size;
	uint8_t *buf;
	if (src == NULL || w <= 0 || h <= 0) {
		reply(fd, "application/octet-stream", NULL, 0);
		return;
	}
	size = 4 + (size_t) w * h * 2;
	buf = malloc(size);
	if (buf == NULL) {
		not_found(fd);
		return;
	}
	buf[0] = (uint8_t) w;
	buf[1] = (uint8_t) (w >> 8);
	buf[2] = (uint8_t) h;
	buf[3] = (uint8_t) (h >> 8);
	for (y = 0; y < h; y++) {
		const uint16_t *row = src + y * pitch;
		uint8_t *out = buf + 4 + (size_t) y * w * 2;
		int x;
		for (x = 0; x < w; x++) {
			out[2 * x] = (uint8_t) row[x];
			out[2 * x + 1] = (uint8_t) (row[x] >> 8);
		}
	}
	reply(fd, "application/octet-stream", buf, size);
	free(buf);
}

static void serve_audio(int fd)
{
	reply(fd, "application/octet-stream", audio, audio_len * sizeof(*audio));
	audio_len = 0;
}

static void handle(int fd)
{
	char req[2048], path[512], *query;
	size_t got = 0;
	struct timeval tv = {0, 200000};

	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	while (got < sizeof(req) - 1) {
		ssize_t n = recv(fd, req + got, sizeof(req) - 1 - got, 0);
		if (n <= 0)
			break;
		got += (size_t) n;
		req[got] = '\0';
		if (strstr(req, "\r\n\r\n") != NULL)
			break;
	}
	req[got] = '\0';
	if (sscanf(req, "GET %511s", path) != 1) {
		not_found(fd);
		return;
	}
	query = strchr(path, '?');
	if (query != NULL)
		*query++ = '\0';

	if (strcmp(path, "/") == 0) {
		serve_page(fd);
	} else if (strcmp(path, "/frame") == 0) {
		serve_frame(fd);
	} else if (strcmp(path, "/audio") == 0) {
		serve_audio(fd);
	} else if (strcmp(path, "/key") == 0) {
		sim_push_key(param(query, "u", 0), param(query, "p", 0));
		reply(fd, "text/plain", NULL, 0);
	} else if (strcmp(path, "/mouse") == 0) {
		sim_mouse(param(query, "x", 0), param(query, "y", 0), param(query, "b", 0));
		reply(fd, "text/plain", NULL, 0);
	} else if (strcmp(path, "/pad") == 0) {
		sim_set_pad(param(query, "n", 0), param(query, "b", -1),
		            param(query, "x", -1), param(query, "y", -1));
		reply(fd, "text/plain", NULL, 0);
	} else {
		not_found(fd);
	}
}

void live_poll(void)
{
	int i;
	if (listen_fd < 0)
		return;
	for (i = 0; i < 16; i++) {
		int fd = accept(listen_fd, NULL, NULL);
		if (fd < 0)
			break;
		fcntl(fd, F_SETFL, 0);
		handle(fd);
		close(fd);
	}
}
