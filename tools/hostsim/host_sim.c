/*
 * host_sim.c - runs the BMCoco emulator port on a PC for testing.
 *
 * Implements src/host.h with a simulated display, sound output and input,
 * so XRoar and the port in src/port/ can be tested without a Raspberry Pi.
 * Time is virtual: each display refresh advances the clock by one frame of
 * a 60 Hz (or BMCOCO_REFRESH Hz) display, so tests run as fast as the PC
 * allows.
 *
 * A script on standard input drives the test, one command per line:
 *
 *   <frame> key <usage> <0|1>     press or release a key (USB HID usage)
 *   <frame> type <text>            type text (US layout, one key per frame)
 *   <frame> pad <n> <bits>         set gamepad n to HOST_PAD_* bits
 *   <frame> axis <n> <x> <y>       gamepad n's analog stick (0-65535, -1 none)
 *   <frame> gpio <port> <bits>     GPIO joystick port to HOST_JOY_* bits
 *   <frame> mouse <dx> <dy> <buttons>  the mouse moves (buttons: bit 0 left)
 *   <frame> shot <file.ppm>        save the visible screen
 *   <frame> audio                  print audio statistics
 *   <frame> webui <reboot|coldstart>  a request from the web UI
 *   <frame> run <path>             the web UI starts a file
 *   <frame> quit                   end the test
 *
 * BMCOCO_SIM_FRAME_MS=N makes each frame's emulation take N ms of the
 * virtual time, as on a computer too slow for it (a Pi 3 with the most
 * accurate artifact colours, say).
 *
 * BMCOCO_SIM_WAV=FILE writes the sound output to FILE (16-bit WAV).
 *
 * BMCOCO_SIM_CPU=1 prints the PC's CPU time a frame, every 60 frames: how
 * hard each screen is to emulate.
 *
 * BMCOCO_SIM_CONNECT_FRAMES=N makes background work (connecting to a
 * DriveWire server) start N frames late, as connecting does on the Pi
 * when the server is slow to answer.
 *
 * With BMCOCO_LIVE=PORT it runs in real time instead, with no script: the
 * screen, keyboard, gamepads and sound are in a web page at
 * http://127.0.0.1:PORT/ (live.c, live.html). tools/hostsim/live.sh starts it.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../src/host.h"
#include "live.h"

/* From src/port/platform.c, called by the web UI on the Pi. */
void bmcoco_request_power(int power_off);
void bmcoco_request_coldstart(void);
void emu_autostart_interrupt(const char *path);

/* ---- Options ---------------------------------------------------------------- */

static const char *option(const char *name)
{
	static char buf[1024];
	const char *cmdline = getenv("BMCOCO_CMDLINE");
	char *p, *save = NULL;
	size_t n = strlen(name);
	if (cmdline == NULL)
		return NULL;
	strncpy(buf, cmdline, sizeof(buf) - 1);
	for (p = strtok_r(buf, " ", &save); p; p = strtok_r(NULL, " ", &save)) {
		if (strncmp(p, name, n) == 0 && p[n] == '=')
			return p + n + 1;
	}
	return NULL;
}

/* Settings changed by the menu, which win over BMCOCO_CMDLINE, as
   bmcoco.cfg does on the Pi. Saved to ./bmcoco.cfg. */
static char set_name[48][24], set_value[48][72];
static int n_set;

int host_option_set(const char *name, const char *value)
{
	int i;
	for (i = 0; i < n_set; i++)
		if (strcmp(set_name[i], name) == 0)
			break;
	if (i == n_set) {
		if (n_set == 48)
			return 0;
		snprintf(set_name[n_set++], sizeof(set_name[0]), "%s", name);
	}
	snprintf(set_value[i], sizeof(set_value[0]), "%s", value);
	return 1;
}

int host_options_save(void)
{
	FILE *f = fopen("bmcoco.cfg", "w");
	int i;
	if (f == NULL)
		return 0;
	for (i = 0; i < n_set; i++)
		fprintf(f, "%s=%s\n", set_name[i], set_value[i]);
	return fclose(f) == 0;
}

/* ./bmcoco.cfg from an earlier run, as the Pi reads it at boot. */
static void load_settings(void)
{
	char line[160];
	FILE *f = fopen("bmcoco.cfg", "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *eq = strchr(line, '=');
		line[strcspn(line, "\r\n")] = '\0';
		if (eq != NULL && line[0] != '#') {
			*eq = '\0';
			host_option_set(line, eq + 1);
		}
	}
	fclose(f);
}

const char *host_option(const char *name, const char *default_value)
{
	const char *v = option(name);
	int i;
	for (i = 0; i < n_set; i++)
		if (strcmp(set_name[i], name) == 0)
			v = set_value[i];
	static char ret[8][256];
	static int slot;
	if (v == NULL)
		return default_value;
	slot = (slot + 1) % 8;
	strncpy(ret[slot], v, 255);
	return ret[slot];
}

int host_option_int(const char *name, int default_value)
{
	const char *v = host_option(name, NULL);
	return v ? atoi(v) : default_value;
}

double host_option_double(const char *name, double default_value)
{
	const char *v = host_option(name, NULL);
	return v ? atof(v) : default_value;
}

/* ---- Time ----------------------------------------------------------------- */

static double now = 0.0;
static double refresh = 60.0;
static double refresh_later = 0.0;  /* refresh rate after start up */
static double frame_work = 0.0;     /* seconds each frame's emulation takes */
static volatile long frame = 0;
static long connect_frames = 0;
static long presents = 0;

static void tick(void);

/* Live mode: the clock is real. */
static int live;

static double real_time(void)
{
	static double start = -1.0;
	struct timespec ts;
	double t;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	t = ts.tv_sec + ts.tv_nsec / 1e9;
	if (start < 0)
		start = t;
	return t - start;
}

/* A slow computer (BMCOCO_SIM_FRAME_MS): the next frame's emulation, due
   once a frame has been shown, after start up (where BMCoco times the
   display). The first look at the clock after that sees it before. */
static double work_due;

double host_time(void)
{
	double t;
	if (live)
		now = real_time();
	t = now;
	now += work_due;
	work_due = 0.0;
	return t;
}

void host_sleep_us(unsigned us)
{
	if (live)
		usleep(us);
	else
		now += us / 1e6;
	tick();
}

void host_yield(void)
{
	if (live)
		now = real_time();
	else
		now += 1e-7;
}

/* ---- Sound ---------------------------------------------------------------- */

/* The output's rate: the VideoCore's 44100 Hz, or BMCOCO_SIM_RATE (48000
   for the Pi 4 and 5's HDMI, which take the latency at 44100 Hz). */
static unsigned hw_rate = 44100;
#define HW_RATE hw_rate
static int snd_open = 0;
static int snd_channels = 1;
static unsigned snd_capacity = 0;
static double snd_buffered = 0;   /* frames queued at the "VideoCore" */
static double snd_last = 0.0;
static long snd_written = 0, snd_underruns = 0, snd_writes = 0, snd_nonzero = 0;
static double snd_min_fill = 1e9, snd_max_fill = 0;

static void sound_advance(void)
{
	double played;
	if (!snd_open)
		return;
	if (live)
		now = real_time();
	played = (now - snd_last) * HW_RATE;
	snd_last = now;
	if (played > snd_buffered) {
		if (snd_written > HW_RATE) { /* ignore start up */
			snd_underruns++;
			fprintf(stderr, "[sim] frame %ld: audio underrun (%.0f frames short)\n",
			        frame, played - snd_buffered);
		}
		snd_buffered = 0;
	} else {
		snd_buffered -= played;
	}
}

unsigned host_sound_open(unsigned freq, int channels, unsigned latency_frames)
{
	const char *rate = getenv("BMCOCO_SIM_RATE");
	(void) freq;
	if (rate != NULL && atoi(rate) >= 8000)
		hw_rate = (unsigned) atoi(rate);
	snd_open = 1;
	snd_channels = channels;
	snd_capacity = (unsigned) ((unsigned long) latency_frames * hw_rate / 44100);
	snd_last = now;
	fprintf(stderr, "[sim] sound open: %d channel(s), %u frames\n", channels, snd_capacity);
	return HW_RATE;
}

void host_sound_close(void) {}

unsigned host_sound_free_frames(void)
{
	sound_advance();
	double cap = snd_capacity;
	return snd_buffered >= cap ? 0 : (unsigned) (cap - snd_buffered);
}

/* BMCOCO_SIM_WAV: the sound, as written, in a WAV file. */
static FILE *wav;
static unsigned long wav_bytes;

static void wav_header(void)
{
	unsigned rate = HW_RATE, ch = (unsigned) snd_channels, bytes = (unsigned) wav_bytes;
	unsigned char h[44] = "RIFF....WAVEfmt \x10\0\0\0\1\0";
	unsigned v[] = {36 + bytes, 0, 0};
	(void) v;
	memcpy(h + 4, &(unsigned){36 + bytes}, 4);
	h[22] = (unsigned char) ch; h[23] = 0;
	memcpy(h + 24, &rate, 4);
	memcpy(h + 28, &(unsigned){rate * ch * 2}, 4);
	h[32] = (unsigned char) (ch * 2); h[33] = 0; h[34] = 16; h[35] = 0;
	memcpy(h + 36, "data", 4);
	memcpy(h + 40, &bytes, 4);
	fseek(wav, 0, SEEK_SET);
	fwrite(h, 1, 44, wav);
	fseek(wav, 0, SEEK_END);
}

static void wav_close(void)
{
	if (wav != NULL) {
		wav_header();
		fclose(wav);
		wav = NULL;
	}
}

static void wav_write(const int16_t *samples, unsigned frames)
{
	const char *name;
	if (wav == NULL) {
		name = getenv("BMCOCO_SIM_WAV");
		if (name == NULL || (wav = fopen(name, "wb")) == NULL)
			return;
		wav_header();
		atexit(wav_close);
	}
	fwrite(samples, 2, frames * snd_channels, wav);
	wav_bytes += frames * snd_channels * 2;
}

void host_sound_write(const int16_t *samples, unsigned frames)
{
	wav_write(samples, frames);

	unsigned i;
	sound_advance();
	if (live)
		live_audio(samples, frames * snd_channels);
	for (i = 0; i < frames * snd_channels; i++)
		if (samples[i] != samples[0])
			snd_nonzero++;
	if (snd_written > HW_RATE * 2) {
		if (snd_buffered < snd_min_fill) snd_min_fill = snd_buffered;
	}
	snd_buffered += frames;
	snd_written += frames;
	snd_writes++;
	if (snd_written > HW_RATE * 2) {
		if (snd_buffered > snd_max_fill) snd_max_fill = snd_buffered;
	}
}

/* ---- Video ---------------------------------------------------------------- */

static uint16_t *fb;
static int fb_w, fb_h, fb_pitch;
static int view_x, view_y, view_w, view_h;

int host_video_init(int width, int height, uint16_t **pixels, int *pitch)
{
	fb_w = width;
	fb_h = height;
	fb_pitch = width;
	fb = calloc(width * height, 2);
	*pixels = fb;
	*pitch = width * 2;
	return 0;
}

void host_video_set_view(int x, int y, int w, int h, double aspect)
{
	view_x = x; view_y = y; view_w = w; view_h = h;
	fprintf(stderr, "[sim] view %dx%d at %d,%d aspect %.3f\n", w, h, x, y, aspect);
}

/* Saves the visible screen with each line twice, so it looks as on a TV. */
static void screenshot(const char *name)
{
	FILE *f = fopen(name, "wb");
	int x, y, r;
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", view_w, view_h * 2);
	for (y = view_y; y < view_y + view_h; y++)
		for (r = 0; r < 2; r++)
			for (x = view_x; x < view_x + view_w; x++) {
				uint16_t c = fb[y * fb_pitch + x];
				fputc(((c >> 11) & 0x1f) * 255 / 31, f);
				fputc(((c >> 5) & 0x3f) * 255 / 63, f);
				fputc((c & 0x1f) * 255 / 31, f);
			}
	fclose(f);
	fprintf(stderr, "[sim] frame %ld: saved %s\n", frame, name);
}

/* ---- Input ---------------------------------------------------------------- */

#define QSIZE 1024
static int q_usage[QSIZE], q_pressed[QSIZE];
static int q_head, q_tail;
static unsigned char key_state[256];
static int pads[HOST_PADS] = {-1, -1, -1, -1};
static int pad_axes[HOST_PADS][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
static int gpio[2];

static void push_key(int usage, int pressed)
{
	q_usage[q_head] = usage;
	q_pressed[q_head] = pressed;
	q_head = (q_head + 1) % QSIZE;
}

int host_key_event(int *usage, int *pressed)
{
	tick();
	if (q_tail == q_head)
		return 0;
	*usage = q_usage[q_tail];
	*pressed = q_pressed[q_tail];
	key_state[*usage] = (unsigned char) *pressed;
	q_tail = (q_tail + 1) % QSIZE;
	return 1;
}

int host_key_down(int usage) { return key_state[usage & 0xff]; }

unsigned host_key_modifiers(void)
{
	unsigned m = 0;
	int i;
	for (i = 0; i < 8; i++)
		if (key_state[0xe0 + i])
			m |= 1u << i;
	return m;
}

int host_pad_state(int pad) { return pad >= 0 && pad < HOST_PADS ? pads[pad] : -1; }
int host_pad_axis(int pad, int axis)
{
	if (pad < 0 || pad >= HOST_PADS || axis < 0 || axis > 1 || pads[pad] < 0)
		return -1;
	return pad_axes[pad][axis];
}

int host_gpio_joystick(int port) { return port >= 0 && port < 2 ? gpio[port] : 0; }

static int mouse_dx, mouse_dy, mouse_buttons, mouse_seen;

int host_mouse_read(int *dx, int *dy, int *buttons)
{
	if (!mouse_seen)
		return 0;
	*dx = mouse_dx;
	*dy = mouse_dy;
	*buttons = mouse_buttons;
	mouse_dx = mouse_dy = 0;
	return 1;
}

/* For live.c, and the script's mouse command. */
void sim_mouse(int dx, int dy, int buttons)
{
	mouse_dx += dx;
	mouse_dy += dy;
	mouse_buttons = buttons;
	mouse_seen = 1;
}

/* US layout: character to (usage, shift). */
static int char_usage(int c, int *shift)
{
	static const char *row = "1234567890";
	static const char *shifted_row = "!@#$%^&*()";
	static const struct { char c, s; int u; } sym[] = {
		{'-', '_', 0x2d}, {'=', '+', 0x2e}, {'[', '{', 0x2f}, {']', '}', 0x30},
		{'\\', '|', 0x31}, {';', ':', 0x33}, {'\'', '"', 0x34}, {',', '<', 0x36},
		{'.', '>', 0x37}, {'/', '?', 0x38}, {0, 0, 0}
	};
	const char *p;
	int i;
	*shift = 0;
	if (c >= 'a' && c <= 'z') return 0x04 + c - 'a';
	if (c >= 'A' && c <= 'Z') { *shift = 1; return 0x04 + c - 'A'; }
	if (c == ' ') return 0x2c;
	if (c == '\n') return 0x28;
	if ((p = strchr(row, c)) != NULL) return 0x1e + (int) (p - row);
	if ((p = strchr(shifted_row, c)) != NULL) { *shift = 1; return 0x1e + (int) (p - shifted_row); }
	for (i = 0; sym[i].c; i++) {
		if (sym[i].c == c) return sym[i].u;
		if (sym[i].s == c) { *shift = 1; return sym[i].u; }
	}
	return 0;
}

/* Pending typed text: each key held for 3 frames and released for 3, about
   as fast as a quick typist, and slow enough for Color BASIC's keyboard
   scan. */
static char typing[1024];
static int typing_pos = -1, typing_up = 0, typing_wait = 0;

static void type_step(void)
{
	int shift, u;
	if (typing_pos < 0)
		return;
	if (typing_wait > 0) {
		typing_wait--;
		return;
	}
	typing_wait = 2;
	if (typing_up) {
		u = char_usage(typing[typing_pos], &shift);
		push_key(u, 0);
		if (shift) push_key(0xe1, 0);
		typing_up = 0;
		if (typing[++typing_pos] == '\0')
			typing_pos = -1;
		return;
	}
	u = char_usage(typing[typing_pos], &shift);
	if (shift) push_key(0xe1, 1);
	push_key(u, 1);
	typing_up = 1;
}

/* ---- Script ----------------------------------------------------------------- */

static char line[1100];
static int have_line = 0;
static long line_frame = -1;

static void run_script(void)
{
	for (;;) {
		char cmd[32];
		int n;
		if (!have_line) {
			if (!fgets(line, sizeof(line), stdin)) {
				line_frame = -1;
				return;
			}
			line[strcspn(line, "\r\n")] = '\0';
			if (line[0] == '#' || line[0] == '\0')
				continue;
			have_line = 1;
			line_frame = atol(line);
		}
		if (frame < line_frame)
			return;
		have_line = 0;
		if (sscanf(line, "%*ld %31s%n", cmd, &n) != 1)
			continue;
		if (strcmp(cmd, "key") == 0) {
			int usage, pressed;
			sscanf(line + n, "%i %d", &usage, &pressed);
			push_key(usage, pressed);
		} else if (strcmp(cmd, "type") == 0) {
			strncpy(typing, line + n + 1, sizeof(typing) - 2);
			/* "\n" in the script means Return */
			{
				char *p;
				while ((p = strstr(typing, "\\n")) != NULL) {
					*p = '\n';
					memmove(p + 1, p + 2, strlen(p + 2) + 1);
				}
			}
			typing_pos = 0;
			typing_up = 0;
		} else if (strcmp(cmd, "pad") == 0) {
			int pad, bits;
			sscanf(line + n, "%d %i", &pad, &bits);
			pads[pad] = bits;
		} else if (strcmp(cmd, "axis") == 0) {
			int pad, x, y;
			sscanf(line + n, "%d %d %d", &pad, &x, &y);
			pad_axes[pad][0] = x;
			pad_axes[pad][1] = y;
		} else if (strcmp(cmd, "mouse") == 0) {
			int dx, dy, b;
			sscanf(line + n, "%d %d %d", &dx, &dy, &b);
			sim_mouse(dx, dy, b);
		} else if (strcmp(cmd, "gpio") == 0) {
			int port, bits;
			sscanf(line + n, "%d %i", &port, &bits);
			gpio[port & 1] = bits;
		} else if (strcmp(cmd, "run") == 0) {
			emu_autostart_interrupt(line + n + 1);
		} else if (strcmp(cmd, "shot") == 0) {
			screenshot(line + n + 1);
		} else if (strcmp(cmd, "webui") == 0) {
			if (strstr(line + n, "reboot"))
				bmcoco_request_power(0);
			else if (strstr(line + n, "coldstart"))
				bmcoco_request_coldstart();
		} else if (strcmp(cmd, "audio") == 0) {
			fprintf(stderr, "[sim] frame %ld: audio written %ld frames in %ld writes, "
			        "queue before write min %.0f, after write max %.0f, underruns %ld, "
			        "changing samples %ld\n",
			        frame, snd_written, snd_writes, snd_min_fill, snd_max_fill, snd_underruns,
			        snd_nonzero);
			fprintf(stderr, "[sim] frame %ld: %ld frames presented\n", frame, presents);
		} else if (strcmp(cmd, "quit") == 0) {
			fprintf(stderr, "[sim] quit at frame %ld (%.2f s)\n", frame, now);
			exit(0);
		}
	}
}

/* The script runs on display frames counted from the virtual clock, so it
   keeps going while the emulator sleeps (in its menus, for example). */
static void tick(void)
{
	if (live) {
		now = real_time();
		live_poll();
		return;
	}
	long f = (long) (now * refresh + 1e-6);
	while (frame < f) {
		frame++;
		type_step();
		run_script();
	}
	if (frame > 2000000)
		exit(1);
}

static void cpu_report(void)
{
	static double last, sum;
	static int frames, on = -1;
	struct timespec ts;
	double t;
	if (on < 0)
		on = getenv("BMCOCO_SIM_CPU") != NULL;
	if (!on)
		return;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	t = ts.tv_sec + ts.tv_nsec / 1e9;
	if (last > 0.0)
		sum += t - last;
	last = t;
	if (++frames == 60) {
		fprintf(stderr, "[sim] frame %ld: %.2f ms CPU a frame\n", frame, sum * 1000.0 / frames);
		sum = 0.0;
		frames = 0;
	}
}

void host_video_present(int wait_vsync)
{
	presents++;
	cpu_report();
	if (live) {
		if (wait_vsync) {
			/* Wait for the next refresh of a 60 Hz display. */
			double period = 1.0 / refresh;
			double t = real_time();
			double next = ((long) (t / period) + 1) * period;
			usleep((useconds_t) ((next - t) * 1e6));
		}
		frame++;
		tick();
		return;
	}
	if (refresh_later > 0 && frame == 30)
		refresh = refresh_later;
	if (wait_vsync) {
		/* Wait for the next vertical blank of the virtual display. */
		double period = 1.0 / refresh;
		now = ((long) (now / period + 1e-6) + 1) * period;
	} else {
		now += 1e-4;
	}
	if (frame >= 30)
		work_due = frame_work;
	tick();
}

/* ---- System ----------------------------------------------------------------- */

void host_reboot(void)
{
	fprintf(stderr, "[sim] reboot requested at frame %ld\n", frame);
	exit(0);
}

void host_power_off(void)
{
	fprintf(stderr, "[sim] power off requested at frame %ld\n", frame);
	exit(0);
}

int host_network_address(char *address, unsigned size)
{
	const char *ip = getenv("BMCOCO_IP");
	if (ip == NULL || frame < 100)
		return 0;
	snprintf(address, size, "%s", ip);
	return 1;
}

/* A thread here (a Circle task on the Pi). */
struct background {
	void (*fn)(void *);
	void *arg;
};

static void *background_thread(void *p)
{
	struct background job = *(struct background *)p;
	long due = frame + connect_frames;
	free(p);
	while (frame < due)
		usleep(1000);
	job.fn(job.arg);
	return NULL;
}

int host_run_in_background(void (*fn)(void *arg), void *arg)
{
	struct background *job = malloc(sizeof(*job));
	pthread_t thread;
	if (job == NULL)
		return 0;
	job->fn = fn;
	job->arg = arg;
	if (pthread_create(&thread, NULL, background_thread, job) != 0) {
		free(job);
		return 0;
	}
	pthread_detach(thread);
	return 1;
}

void host_network_status(char *text, unsigned size)
{
	char ip[32];
	if (host_network_address(ip, sizeof(ip)))
		snprintf(text, size, "Wi-Fi %s", ip);
	else
		snprintf(text, size, "Off");
}

/* BMCOCO_WIFI=1 pretends Wi-Fi is running, with a few networks around. */
int host_wifi_scan(struct host_wifi_ap *aps, int max)
{
	static const struct host_wifi_ap found[] = {
		{"CoffeeShop", -71, 0},
		{"StdWiFiPoint", -48, 1},
		{"A very long network name, 32 ch", -80, 1},
		{"CoCoNet", -60, 0},
	};
	int i, n = (int) (sizeof(found) / sizeof(found[0]));
	if (getenv("BMCOCO_WIFI") == NULL)
		return -1;
	for (i = 0; i < n && i < max; i++)
		aps[i] = found[i];
	return i;
}

const char *host_model_name(void)
{
	return "Host simulator";
}

void host_display_size(int *width, int *height)
{
	*width = 1280;
	*height = 720;
}

int host_display_match_rate(double fps)
{
	(void) fps;
	return 0;
}

int host_display_resolution_choice(void)
{
	return 0;
}

void host_log(const char *message)
{
	fprintf(stderr, "[log] %s\n", message);
}

static int log_destination = HOST_LOG_OFF;

int host_log_destination(void)
{
	return log_destination;
}

int host_set_log_destination(int destination)
{
	static const char *const names[] = {"off", "uart", "file"};
	fprintf(stderr, "[sim] logging: %s\n", names[destination]);
	log_destination = destination;
	return 1;
}

extern void bmcoco_main(void);

/* For live.c */
void sim_push_key(int usage, int pressed)
{
	push_key(usage & 0xff, pressed);
}

void sim_set_pad(int pad, int bits, int x, int y)
{
	if (pad < 0 || pad >= HOST_PADS)
		return;
	pads[pad] = bits;
	pad_axes[pad][0] = x;
	pad_axes[pad][1] = y;
}

const uint16_t *sim_screen(int *w, int *h, int *pitch)
{
	*w = view_w;
	*h = view_h;
	*pitch = fb_pitch;
	return fb ? fb + view_y * fb_pitch + view_x : NULL;
}

int main(void)
{
	const char *r = getenv("BMCOCO_LIVE");
	if (r) {
		live = 1;
		if (!live_init(atoi(r)))
			return 1;
	}
	r = getenv("BMCOCO_REFRESH");
	if (r)
		refresh = atof(r);
	r = getenv("BMCOCO_SIM_FRAME_MS");
	if (r != NULL)
		frame_work = atof(r) / 1000.0;
	r = getenv("BMCOCO_SIM_CONNECT_FRAMES");
	if (r)
		connect_frames = atol(r);
	r = getenv("BMCOCO_REFRESH_LATER");
	if (r)
		refresh_later = atof(r);
	load_settings();
	bmcoco_main();
	return 0;
}
