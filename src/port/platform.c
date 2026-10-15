/*
 * platform.c - XRoar platform layer for BMCoco.
 *
 * XRoar is built from modules: a user interface module supplies the video
 * output, and can supply sound output, joysticks and file requesters. This
 * file is BMCoco's user interface module ("bmcoco"), with its video and
 * sound modules, on top of the Circle kernel services in src/host.h. It also
 * runs the emulator's main loop. Joysticks are in joystick.c, the menu in
 * menu.c and osd.c.
 *
 * Keyboard: XRoar's own host keyboard code takes USB HID usage codes, which
 * is what the kernel reports, so keys are passed straight to it; XRoar maps
 * them to the CoCo's matrix (or, with "typed characters" on, translates the
 * characters typed using the layout set by keyboard= in cmdline.txt).
 *
 * Timing: when the display refreshes at (close to) the emulated machine's
 * frame rate, every frame is shown on its own display refresh and the
 * emulator waits for vertical blank: scrolling is then perfectly smooth.
 * Otherwise the emulation is paced by a timer. Either way the sound is kept
 * in step by dropping or repeating a sample now and then.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sds.h"
#include "xalloc.h"

#include "ao.h"
#include "events.h"
#include "hkbd.h"
#include "joystick.h"
#include "logging.h"
#include "machine.h"
#include "slist.h"
#include "module.h"
#include "part.h"
#include "romlist.h"
#include "sound.h"
#include "ui.h"
#include "vdisk.h"
#include "vdrive.h"
#include "vo.h"
#include "vo_render.h"
#include "xroar.h"

#include "../host.h"
#include "bmcoco.h"
#include "osd.h"

/* ---- Settings ---------------------------------------------------------------- */

const char *bmcoco_sd_root(void)
{
	return host_option("root", "/");
}

static int option_on(const char *name, int default_value)
{
	const char *value = host_option(name, NULL);
	if (value == NULL || *value == '\0')
		return default_value;
	return strcmp(value, "1") == 0 || strcmp(value, "on") == 0
	    || strcmp(value, "yes") == 0 || strcmp(value, "true") == 0;
}

/* ---- Video ------------------------------------------------------------------- */

/* The frame buffer is big enough for XRoar's largest picture ("underscan"). */
#define FB_W 736
#define FB_H 288

static uint16_t *fb_pixels;
static int fb_pitch_px;

static struct vo_interface *bm_vo;
static int vp_w = 640, vp_h = 240;  /* XRoar's viewport */
static int vp_60hz;                  /* XRoar's 60 Hz scaling is on */
static int osd_view;                 /* the menu is showing */

static double refresh_rate = 60.0;   /* measured display refresh, Hz */
static double machine_rate = 0.0;    /* emulated frame rate, Hz */
static int vsync_lock;
static int turbo;

/* Frame rate tracking: emulated time between vertical syncs. */
static event_ticks last_vsync_tick;
static int have_vsync_tick;

uint16_t *bmcoco_framebuffer(int *pitch_px)
{
	*pitch_px = fb_pitch_px;
	return fb_pixels;
}

static int shown_height(void)
{
	return vp_60hz ? (vp_h * 5) / 6 : vp_h;
}

static void apply_view(void)
{
	double aspect = (4.0 / 3.0) * host_option_double("aspect", 1.0);
	if (osd_view)
		host_video_set_view(0, 0, 640, 240, 4.0 / 3.0);
	else
		host_video_set_view(0, 0, vp_w, shown_height(), aspect);
}

void bmcoco_osd_view(int on)
{
	osd_view = on;
	apply_view();
}

static void clear_framebuffer(void)
{
	int y;
	for (y = 0; y < FB_H; y++)
		memset(fb_pixels + y * fb_pitch_px, 0, FB_W * 2);
}

/* Times a few vertical blanks to find the display's refresh rate. */
static void measure_refresh_rate(void)
{
	enum { SKIP = 3, COUNT = 20 };
	double start = 0.0, rate;
	int i;
	for (i = 0; i < SKIP + COUNT; i++) {
		host_video_present(TRUE);
		if (i == SKIP - 1)
			start = host_time();
	}
	rate = COUNT / (host_time() - start);
	if (rate > 20.0 && rate < 250.0)
		refresh_rate = rate;
	LOG_PRINT("BMCoco: display refresh rate %.3f Hz\n", refresh_rate);
}

/* The first decision is logged, and every change after it. */
static int sync_decided;

static void update_sync_mode(void)
{
	const char *mode = host_option("vsync", "auto");
	int lock;
	if (strcmp(mode, "off") == 0 || strcmp(mode, "0") == 0)
		lock = FALSE;
	else if (strcmp(mode, "on") == 0 || strcmp(mode, "1") == 0)
		lock = TRUE;
	else {
		/* A 60 Hz display for an NTSC CoCo, 50 Hz for a PAL one;
		   anything else would run it noticeably fast or slow. */
		double ratio = machine_rate > 0.0 ? refresh_rate / machine_rate : 0.0;
		lock = ratio > 0.98 && ratio < 1.02;
	}
	if (lock != vsync_lock || !sync_decided) {
		sync_decided = TRUE;
		LOG_PRINT("BMCoco: display %.2f Hz, machine %.2f Hz, %s\n", refresh_rate,
		          machine_rate, lock ? "synchronised to the display" : "timer paced");
	}
	vsync_lock = lock;
}

/* Called at the emulated machine's vertical sync. */
static void track_machine_rate(void)
{
	event_ticks now = event_current_tick;
	if (have_vsync_tick) {
		event_ticks delta = now - last_vsync_tick;
		if (delta > 0) {
			double rate = (double) EVENT_TICK_RATE / (double) delta;
			/* Ignore odd frames (the VDG resyncing, mode changes). */
			if (rate > 45.0 && rate < 65.0) {
				int was_ntsc = machine_rate > 55.0;
				if (machine_rate == 0.0 || (rate > 55.0) != was_ntsc)
					machine_rate = rate;
				else
					machine_rate += (rate - machine_rate) * 0.01;
				if (machine_rate == rate) {
					/* A new machine, or NTSC/PAL changed: where the
					   host can set the video mode (the Pi 4), run the
					   display at the machine's rate, then time it. */
					if (host_display_match_rate(rate))
						measure_refresh_rate();
					update_sync_mode();
				}
			}
		}
	}
	last_vsync_tick = now;
	have_vsync_tick = TRUE;
}

static double last_present;

/* ---- Load ------------------------------------------------------------------ */

static void reset_pacing(void);

/* How long each frame takes to emulate and draw, against the display's
   refresh. Locked to the display, a frame that takes longer than one
   refresh waits for the next, which would halve the speed (and starve the
   sound) on a Pi too slow for what it's asked: then BMCoco stops waiting
   for the display and paces by the clock instead, as fast as the Pi can,
   until frames fit again. */
static double frame_start;        /* when the last present returned */
static double paced_sleep;        /* time spent sleeping in pace() since */
static double load_avg;           /* smoothed frame time / refresh period */
static int overloaded;
static double stats_start, stats_sum, stats_max;
static unsigned stats_frames, stats_late;

static int display_paces(void)
{
	return vsync_lock && !overloaded;
}

int bmcoco_overloaded(void)
{
	return overloaded;
}

void bmcoco_load(int *average, int *maximum)
{
	*average = (int) (load_avg * 100.0 + 0.5);
	*maximum = (int) (stats_max * 100.0 + 0.5);
}

static void measure_load(double now)
{
	double period = 1.0 / refresh_rate;
	double work = now - frame_start - paced_sleep;
	double load;
	paced_sleep = 0.0;
	if (frame_start == 0.0 || work < 0.0 || work > 0.25)
		return;  /* the first frame, or after the menu or a pause */
	load = work / period;
	load_avg += (load - load_avg) * 0.05;
	stats_sum += load;
	if (load > stats_max)
		stats_max = load;
	stats_frames++;
	if (load > 1.0)
		stats_late++;

	if (vsync_lock && !overloaded && load_avg > 0.97) {
		overloaded = TRUE;
		reset_pacing();
		LOG_PRINT("BMCoco: frames take %.1f ms, longer than the display's %.1f ms: "
		          "timer paced until they fit\n", load_avg * period * 1000.0,
		          period * 1000.0);
	} else if (overloaded && (!vsync_lock || load_avg < 0.85)) {
		overloaded = FALSE;
		LOG_PRINT("BMCoco: frames fit the display again (%.1f ms), synchronised to it\n",
		          load_avg * period * 1000.0);
	}

	/* Every 30 seconds in the log: how busy the Pi is. */
	if (stats_start == 0.0)
		stats_start = now;
	if (now - stats_start >= 30.0 && stats_frames > 0) {
		LOG_PRINT("BMCoco: load %.0f%% of a frame on average, %.0f%% at most, "
		          "%u of %u frames late%s\n", stats_sum * 100.0 / stats_frames,
		          stats_max * 100.0, stats_late, stats_frames,
		          overloaded ? " (timer paced)" : "");
		stats_start = now;
		stats_sum = stats_max = 0.0;
		stats_frames = stats_late = 0;
	}
}

static void bm_draw(void *sptr)
{
	double now;
	(void) sptr;
	if (osd_view)
		return;
	track_machine_rate();
	osd_draw_notification(fb_pixels, fb_pitch_px, vp_w, shown_height());
	now = host_time();
	if (turbo) {
		/* Show a frame now and then; the rest of the time emulate. */
		if (now - last_present < 0.04) {
			host_yield();
			return;
		}
		host_video_present(FALSE);
		frame_start = 0.0;
	} else {
		measure_load(now);
		host_video_present(display_paces());
		frame_start = host_time();
	}
	last_present = now;
}

static void bm_set_viewport(void *sptr, int w, int h)
{
	(void) sptr;
	if (w < 64)
		w = 64;
	if (w > FB_W)
		w = FB_W;
	if (h < 48)
		h = 48;
	if (h > FB_H)
		h = FB_H;
	vp_w = w;
	vp_h = h;
	vo_render_set_viewport(bm_vo->renderer, vp_w, shown_height());
	bm_vo->renderer->buffer_pitch = fb_pitch_px;
	/* The mouse pointer is in the picture's pixels (process_mouse()):
	   XRoar's mouse joystick (mjoy0) measures it against this. */
	bm_vo->picture_area = (struct vo_picture_area){0, 0, vp_w, shown_height()};
	clear_framebuffer();
	apply_view();
}

static void bm_notify_frame_rate(void *sptr, bool is_60hz)
{
	(void) sptr;
	vp_60hz = is_60hz;
	bm_set_viewport(NULL, vp_w, vp_h);
}

static void bm_vo_free(void *sptr)
{
	struct vo_interface *vo = sptr;
	vo_render_free(vo->renderer);
	free(vo);
}

static struct vo_interface *bm_vo_new(void)
{
	struct vo_interface *vo = vo_interface_new(sizeof(*vo));
	struct vo_render *vr;

	vo_interface_init(vo);
	vr = vo_render_new(VO_RENDER_FMT_RGB565);
	vo_set_renderer(vo, vr);
	vo_render_set_buffer(vr, fb_pixels);
	vr->buffer_pitch = fb_pitch_px;
	vr->notify_frame_rate = DELEGATE_AS1(void, bool, bm_notify_frame_rate, vo);

	vo->free = DELEGATE_AS0(void, bm_vo_free, vo);
	vo->set_viewport = DELEGATE_AS2(void, int, int, bm_set_viewport, vo);
	vo->draw = DELEGATE_AS0(void, bm_draw, vo);
	/* The whole display: XRoar's picture area is the 4:3 part of it. */
	vo_set_draw_area(vo, 0, 0, 640, 480);
	bm_vo = vo;
	bm_set_viewport(NULL, vp_w, vp_h);
	return vo;
}

double bmcoco_refresh_rate(void)
{
	return refresh_rate;
}

double bmcoco_machine_rate(void)
{
	return machine_rate;
}

int bmcoco_vsync_locked(void)
{
	return vsync_lock;
}

void bmcoco_video_settings_changed(void)
{
	update_sync_mode();
	apply_view();
}

int bmcoco_turbo(void)
{
	return turbo;
}

/* ---- Sound ------------------------------------------------------------------- */

#define AO_RATE 44100
#define AO_FRAGMENT 512

static int16_t ao_fragment[AO_FRAGMENT];
static unsigned ao_rate;          /* 0 if there's no sound */
static unsigned ao_latency;       /* frames */
static int ao_corrections;
static const int16_t silence[1024];

static void open_sound(void)
{
	unsigned ms = (unsigned) host_option_int("audio_buffer_ms", 60);
	if (ms < 20)
		ms = 20;
	if (ms > 500)
		ms = 500;
	ao_latency = AO_RATE * ms / 1000;
	ao_rate = host_sound_open(AO_RATE, 1, ao_latency);
	if (ao_rate == 0)
		LOG_WARN("BMCoco: no sound\n");
}

void bmcoco_sound_settings_changed(void)
{
	open_sound();
}

/* Keeps the sound output fed while the emulation isn't running. */
static void feed_silence(void)
{
	unsigned frames;
	if (ao_rate == 0)
		return;
	frames = host_sound_free_frames();
	/* Keep it about half full, ready for when the emulation goes on. */
	if (frames > ao_latency / 2) {
		frames -= ao_latency / 2;
		while (frames > 0) {
			unsigned n = frames > 1024 ? 1024 : frames;
			host_sound_write(silence, n);
			frames -= n;
		}
	}
}

/* A fragment from XRoar. The emulation runs at the display's pace or a
   timer's, never quite the sound output's, so the buffer is kept about half
   full by dropping or repeating a sample when it strays. One sample in 512
   is far below what can be heard, and more than covers the 0.1% between an
   NTSC CoCo and a 60 Hz display. */
static void play_fragment(const int16_t *samples, unsigned n)
{
	static int16_t buf[AO_FRAGMENT * 2 + 1];
	unsigned free_frames, used;
	if (ao_rate == 0)
		return;
	free_frames = host_sound_free_frames();
	used = free_frames < ao_latency ? ao_latency - free_frames : 0;

	if (turbo) {
		/* Far too much sound: play what fits. */
		if (n > free_frames)
			n = free_frames;
		if (n > 0)
			host_sound_write(samples, n);
		return;
	}
	if (used < ao_latency / 4 && n > 0 && n <= AO_FRAGMENT) {
		/* Running dry: one sample more, or, the emptier it is (the
		   emulation slower than real time: a Pi too slow for it), the
		   fragment stretched to up to twice its length, lower in
		   pitch, rather than gaps. */
		unsigned quarter = ao_latency / 4;
		unsigned want = n + 1 + n * (quarter - used) / quarter;
		unsigned i;
		if (want > AO_FRAGMENT * 2 + 1)
			want = AO_FRAGMENT * 2 + 1;
		for (i = 0; i < want; i++) {
			/* Linear interpolation, in 16.16 fixed point. */
			uint32_t pos = (uint32_t) (((uint64_t) i * (n - 1) << 16) / (want - 1));
			unsigned j = pos >> 16;
			int frac = (int) (pos & 0xffff);
			int a = samples[j];
			int b = j + 1 < n ? samples[j + 1] : a;
			buf[i] = (int16_t) (a + (((b - a) * frac) >> 16));
		}
		samples = buf;
		n = want;
		ao_corrections++;
	} else if (used > (ao_latency * 3) / 4 && n > 1) {
		/* Getting full: one sample less. */
		n--;
		ao_corrections--;
	}
	if (n > free_frames) {
		/* No room at all (the emulation got far ahead): drop it. */
		n = free_frames;
	}
	if (n > 0)
		host_sound_write(samples, n);
}

static void *ao_write_buffer(void *sptr, void *buffer)
{
	(void) sptr;
	if (buffer != NULL)
		play_fragment(buffer, AO_FRAGMENT);
	return ao_fragment;
}

static void *ao_write_silence(void *sptr, void *buffer)
{
	(void) sptr;
	feed_silence();
	return buffer;
}

static void ao_free(void *sptr)
{
	struct ao_interface *ao = sptr;
	sound_interface_free(ao->sound_interface);
	free(ao);
}

static void *ao_new(void *cfg)
{
	struct ao_interface *ao = xmalloc(sizeof(*ao));
	(void) cfg;
	*ao = (struct ao_interface){0};
	ao->free = DELEGATE_AS0(void, ao_free, ao);
	ao->sound_interface = sound_interface_new(NULL, SOUND_FMT_S16_HE, AO_RATE, 1, AO_FRAGMENT);
	if (ao->sound_interface == NULL) {
		free(ao);
		return NULL;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_write_buffer, ao);
	ao->sound_interface->write_silence = DELEGATE_AS1(voidp, voidp, ao_write_silence, ao);
	return ao;
}

static struct module ao_bmcoco_module = {
	.name = "bmcoco", .description = "BMCoco sound (HDMI or headphones)",
	.new = ao_new,
};

static struct module * const bmcoco_ao_module_list[] = {
	&ao_bmcoco_module, NULL
};

/* ---- The user interface module -------------------------------------------- */

extern struct module filereq_null_module;

static struct module * const bmcoco_filereq_module_list[] = {
	&filereq_null_module, NULL
};

static struct joystick_module * const bmcoco_js_module_list[] = {
	&bmcoco_js_mod, NULL
};

static void ui_free(void *sptr)
{
	free(sptr);
}

/* RAM sizes chosen in the menu (ram_NAME=K, one per machine), onto the
   machines XRoar has set up from xroar.conf, before it makes one. */
static void apply_ram_settings(void)
{
	struct slist *l;
	for (l = machine_config_list(); l != NULL; l = l->next) {
		struct machine_config *mc = l->data;
		char key[64];
		int k;
		snprintf(key, sizeof key, "ram_%s", mc->name);
		k = host_option_int(key, 0);
		if (k > 0) {
			mc->ram = k;
			if (mc->architecture == NULL || strcmp(mc->architecture, "coco3") != 0)
				mc->ram_org = ANY_AUTO;
		}
	}
}

static void *ui_new(void *cfg)
{
	struct ui_interface *ui = xmalloc(sizeof(*ui));
	(void) cfg;
	*ui = (struct ui_interface){0};
	ui->free = DELEGATE_AS0(void, ui_free, ui);
	ui->vo_interface = bm_vo_new();
	/* XRoar's host keyboard tables (each front end sets them up). */
	hk_init();
	bmcoco_menu_attach();
	apply_ram_settings();
	return ui;
}

static struct ui_module ui_bmcoco_module = {
	.common = { .name = "bmcoco", .description = "BMCoco", .new = ui_new },
	.filereq_module_list = bmcoco_filereq_module_list,
	.ao_module_list = bmcoco_ao_module_list,
	.joystick_module_list = bmcoco_js_module_list,
};

static struct ui_module * const bmcoco_ui_module_list[] = {
	&ui_bmcoco_module, NULL
};

/* ---- Machines and ROMs ------------------------------------------------------- */

static int machine_works(struct machine_config *mc)
{
	const struct partdb_entry *pe;
	const struct machine_partdb_entry *mpe;
	if (mc == NULL)
		return FALSE;
	pe = partdb_find_entry(mc->architecture);
	if (pe == NULL || !partdb_ent_is_a(pe, "machine"))
		return FALSE;
	mpe = (const struct machine_partdb_entry *) pe;
	return mpe->is_working_config != NULL && mpe->is_working_config(mc);
}

/* Adds "name" to LIST if the ROM isn't found. */
static void check_rom(char *list, size_t size, const char *rom)
{
	sds path;
	if (rom == NULL)
		return;
	path = romlist_find(rom);
	if (path != NULL) {
		sdsfree(path);
		return;
	}
	/* "@coco" is a list of names: the first is the usual one. */
	if (rom[0] == '@') {
		static const struct { const char *list; const char *file; } usual[] = {
			{"@coco", "bas13.rom"}, {"@coco_ext", "extbas11.rom"},
			{"@coco1", "bas10.rom"}, {"@coco1e", "bas11.rom"},
			{"@coco1e_ext", "extbas10.rom"}, {"@coco2", "bas12.rom"},
			{"@coco2_ext", "extbas11.rom"}, {"@coco2b", "bas13.rom"},
			{"@coco3", "coco3.rom"}, {"@coco3p", "coco3p.rom"},
			{"@mc10", "mc10.rom"}, {"@dragon32", "d32.rom"},
			{"@dragon64", "d64_1.rom"}, {"@dragon64_alt", "d64_2.rom"},
			{"@deluxecoco", "deluxe.rom"},
		};
		unsigned i;
		for (i = 0; i < sizeof(usual) / sizeof(usual[0]); i++) {
			if (strcmp(rom, usual[i].list) == 0) {
				rom = usual[i].file;
				break;
			}
		}
	}
	if (strstr(list, rom) != NULL)
		return;
	if (list[0] != '\0')
		strncat(list, ", ", size - strlen(list) - 1);
	strncat(list, rom[0] == '@' ? rom + 1 : rom, size - strlen(list) - 1);
}

const char *bmcoco_missing_roms(void)
{
	static char missing[128];
	struct machine_config *mc = xroar.machine_config;
	missing[0] = '\0';
	if (mc == NULL || machine_works(mc))
		return NULL;
	check_rom(missing, sizeof(missing), mc->bas_rom);
	check_rom(missing, sizeof(missing), mc->extbas_rom);
	check_rom(missing, sizeof(missing), mc->altbas_rom);
	return missing[0] != '\0' ? missing : "the machine's ROMs";
}

/* Without machine= in cmdline.txt, start the first CoCo there are ROMs for
   (XRoar on its own would prefer a Dragon). */
static void choose_machine(void)
{
	static const char *const preferred[] = {
		"coco2bus", "cocous", "coco3", "coco2b", "coco", "coco3p",
		"deluxecoco", "mc10", "dragon64", "dragon32", NULL
	};
	int i;
	if (host_option("machine", NULL) != NULL)
		return;
	for (i = 0; preferred[i] != NULL; i++) {
		struct machine_config *mc = machine_config_by_name(preferred[i]);
		if (mc != NULL && machine_works(mc)) {
			if (mc != xroar.machine_config)
				ui_update_state(-1, ui_tag_machine, mc->id, NULL);
			return;
		}
	}
	/* None: stay with a CoCo 2, which tells what it's missing. */
	{
		struct machine_config *mc = machine_config_by_name("coco2bus");
		if (mc != NULL && mc != xroar.machine_config)
			ui_update_state(-1, ui_tag_machine, mc->id, NULL);
	}
}

/* ---- Files ---------------------------------------------------------------- */

/* XRoar writes disk images back only when they're ejected, and the SD
   card's file system caches what it can. Since the Pi can be switched off
   at any time, write changed disks once they have been left alone for two
   seconds, and push everything out to the card. */
static double disk_dirty_since;

static int disks_dirty(void)
{
	unsigned drive;
	if (xroar.vdrive_interface == NULL)
		return FALSE;
	for (drive = 0; drive < 4; drive++) {
		struct vdisk *disk = vdrive_disk_in_drive(xroar.vdrive_interface, drive);
		if (disk != NULL && disk->dirty && disk->write_back)
			return TRUE;
	}
	return FALSE;
}

static void sync_files(int force)
{
	int fd;
	if (disks_dirty()) {
		double now = host_time();
		if (disk_dirty_since == 0.0)
			disk_dirty_since = now;
		if (force || now - disk_dirty_since >= 2.0) {
			vdrive_flush(xroar.vdrive_interface);
			disk_dirty_since = 0.0;
		}
	} else {
		disk_dirty_since = 0.0;
	}
	fflush(NULL);
	for (fd = 3; fd < 20; fd++)
		fsync(fd);
}

/* ---- Requests from the web UI ------------------------------------------ */

/* The web UI's server runs as a Circle task, which only runs when the
   emulator yields, so these are only ever set between emulation steps. */
static volatile int request_power = -1;   /* 0 reboot, 1 power off */
static volatile int request_coldstart = FALSE;
static volatile int request_autostart = FALSE;
static char autostart_path[512];

void bmcoco_request_power(int power_off)
{
	request_power = power_off ? 1 : 0;
}

/* Called by the web UI. */
void bmcoco_request_coldstart(void)
{
	request_coldstart = TRUE;
}

/* Name kept from BMC64, whose web UI code calls it. */
void emu_autostart_interrupt(const char *path)
{
	snprintf(autostart_path, sizeof(autostart_path), "%s", path);
	request_autostart = TRUE;
}

int bmcoco_pending_requests(void)
{
	return request_power >= 0 || request_coldstart || request_autostart;
}

static void handle_requests(void)
{
	if (request_autostart) {
		request_autostart = FALSE;
		LOG_PRINT("Web UI: starting %s\n", autostart_path);
		xroar_load_file_by_type(autostart_path, 1);
	}
	if (request_coldstart) {
		request_coldstart = FALSE;
		LOG_PRINT("Web UI: hard reset\n");
		xroar_hard_reset();
	}
	if (request_power >= 0) {
		int power_off = request_power;
		request_power = -1;
		LOG_PRINT("%s\n", power_off ? "Shutting down" : "Rebooting");
		/* Ejecting writes back changed disks. */
		if (xroar.vdrive_interface != NULL) {
			unsigned drive;
			for (drive = 0; drive < 4; drive++)
				vdrive_eject_disk(xroar.vdrive_interface, drive);
		}
		sync_files(TRUE);
		if (power_off)
			host_power_off();
		else
			host_reboot();
	}
}

/* ---- Menus waiting -------------------------------------------------------- */

static double last_sync_time;

void bmcoco_idle(void)
{
	double now = host_time();
	feed_silence();
	if (now - last_sync_time >= 1.0) {
		last_sync_time = now;
		sync_files(FALSE);
	}
	if (request_power >= 0)
		handle_requests();
}

/* ---- Timer pacing ----------------------------------------------------------- */

/* Emulated time since the last resync, and the real time it started. */
static double paced_emulated;
static double paced_start;
static event_ticks paced_last_tick;

static void reset_pacing(void)
{
	paced_emulated = 0.0;
	paced_start = host_time();
	paced_last_tick = event_current_tick;
	have_vsync_tick = FALSE;
}

/* After a slice of emulation: wait until real time catches up with it. */
static void pace(void)
{
	double ahead;
	paced_emulated += (double) (event_ticks) (event_current_tick - paced_last_tick)
	                  / (double) EVENT_TICK_RATE;
	paced_last_tick = event_current_tick;
	if (display_paces() || turbo) {
		/* The display does the pacing (or nothing does). */
		paced_emulated = 0.0;
		paced_start = host_time();
		return;
	}
	ahead = paced_emulated - (host_time() - paced_start);
	if (ahead > 0.001) {
		host_sleep_us((unsigned) (ahead * 1000000.0));
		paced_sleep += ahead;
	} else if (ahead < -0.25) {
		/* Fell well behind (loading, a slow moment): don't race to
		   catch up. */
		paced_emulated = 0.0;
		paced_start = host_time();
	}
}

void bmcoco_set_turbo(int on)
{
	turbo = on;
	osd_notify(on ? "Turbo on" : "Turbo off");
	reset_pacing();
}

/* ---- Input --------------------------------------------------------------- */

static int open_menu;
static int pad_last_menu[HOST_PADS];

/* A USB mouse moves a pointer over the picture, which XRoar's Mouse
   joystick (mjoy0, plugged into a port in the menu's Joysticks page) reads
   as a position, as the CoCo's own mouse and the Hi-Res joystick do. One
   step of the mouse is a pixel across, half a line down (the CoCo's lines
   are drawn twice as tall as its pixels are wide); mouse_speed= (percent)
   in cmdline.txt changes both. */
static double mouse_x = -1.0, mouse_y = -1.0;

static void process_mouse(void)
{
	int dx, dy, buttons;
	double speed;
	if (!host_mouse_read(&dx, &dy, &buttons) || bm_vo == NULL)
		return;
	if (mouse_x < 0.0) {
		mouse_x = vp_w / 2.0;
		mouse_y = shown_height() / 2.0;
	}
	speed = host_option_int("mouse_speed", 100) / 100.0;
	mouse_x += dx * speed;
	mouse_y += dy * speed * 0.5;
	if (mouse_x < 0.0)
		mouse_x = 0.0;
	if (mouse_x > vp_w - 1)
		mouse_x = vp_w - 1;
	if (mouse_y < 0.0)
		mouse_y = 0.0;
	if (mouse_y > shown_height() - 1)
		mouse_y = shown_height() - 1;
	bm_vo->mouse.axis[0] = (int) mouse_x;
	bm_vo->mouse.axis[1] = (int) mouse_y;
	bm_vo->mouse.button[0] = (buttons & 1) != 0;
	bm_vo->mouse.button[1] = (buttons & 2) != 0;
	bm_vo->mouse.button[2] = (buttons & 4) != 0;
}

static void process_input(void)
{
	int usage, pressed, pad;
	process_mouse();
	while (host_key_event(&usage, &pressed)) {
		/* The power key: Shut Down. The Pi 400's keyboard sends it for
		   Fn+F10 while the Pi is running (Raspberry Pi, "Have you tried
		   turning it off and on again?", raspberrypi.com news, 2021). */
		if (usage == USAGE_POWER) {
			if (pressed)
				bmcoco_request_power(1);
			continue;
		}
		if (usage == KEY_MENU) {
			if (pressed)
				open_menu = TRUE;
			continue;
		}
		if (usage == KEY_TURBO) {
			if (pressed)
				bmcoco_set_turbo(!turbo);
			continue;
		}
		if (usage == KEY_RESET) {
			if (pressed) {
				if (host_key_modifiers() & 0x22) {
					osd_notify("Hard reset");
					xroar_hard_reset();
				} else {
					osd_notify("Reset");
					xroar_soft_reset();
				}
			}
			continue;
		}
		/* With Ctrl held, XRoar's keyboard code runs emulator commands
		   meant for its desktop versions (Ctrl+Q quits); the CoCo has no
		   Ctrl key (the CoCo 3's is on Caps Lock and the Windows keys). */
		if (usage < 0 || usage > 0xff || usage == USAGE_LCTRL || usage == USAGE_RCTRL)
			continue;
		if (pressed)
			hk_scan_press((uint8_t) usage);
		else
			hk_scan_release((uint8_t) usage);
	}

	/* Gamepad Home, or Select and Start together, open the menu. */
	for (pad = 0; pad < HOST_PADS; pad++) {
		int state = host_pad_state(pad);
		int menu;
		if (state < 0)
			state = 0;
		menu = (state & HOST_PAD_HOME)
		       || ((state & HOST_PAD_SELECT) && (state & HOST_PAD_START));
		if (menu && !pad_last_menu[pad])
			open_menu = TRUE;
		pad_last_menu[pad] = menu;
	}
	bmcoco_pad_keys();
}

/* ---- Main loop --------------------------------------------------------------- */

/* Shows the web UI's address once the network is up. */
static void show_network_address(void)
{
	static int shown;
	char address[32];
	if (shown || !option_on("webui", TRUE))
		return;
	if (host_network_address(address, sizeof(address))) {
		char text[64];
		snprintf(text, sizeof(text), "Web UI: http://%s/", address);
		osd_notify(text);
		shown = TRUE;
	}
}

/* Builds XRoar's command line from cmdline.txt options. */
static int build_args(char **argv, int max)
{
	int argc = 0;
	const char *value;

	argv[argc++] = "xroar";

	value = host_option("machine", NULL);
	if (value != NULL && *value != '\0') {
		argv[argc++] = "-default-machine";
		argv[argc++] = (char *) value;
	}

	/* The Color Computer Archive's CP-400 Extended BASIC is
	   cp400extbas.rom; XRoar looks for cp400ext.rom. */
	argv[argc++] = "-romlist";
	argv[argc++] = "cp400ext=cp400ext,cp400extbas";

	/* Hard disk images chosen in the menu, and the cartridge they were
	   used with (an IDE interface). Copied: argv outlives the lookups. */
	{
		static char hd_cart[64], hd[2][512];
		static const char *const hd_keys[2] = {"hd0", "hd1"};
		static const char *const hd_args[2] = {"-load-hd0", "-load-hd1"};
		int d, any = FALSE;
		snprintf(hd_cart, sizeof hd_cart, "%s", host_option("hd_cart", ""));
		for (d = 0; d < 2; d++) {
			snprintf(hd[d], sizeof hd[d], "%s", host_option(hd_keys[d], ""));
			any |= hd[d][0] != '\0';
		}
		if (hd_cart[0] != '\0' && any) {
			argv[argc++] = "-cart";
			argv[argc++] = hd_cart;
			for (d = 0; d < 2; d++) {
				if (hd[d][0] != '\0') {
					argv[argc++] = (char *) hd_args[d];
					argv[argc++] = hd[d];
				}
			}
		}
	}

	/* The Becker port's DriveWire server (src/port/becker.c). Copied:
	   argv outlives the lookups. */
	{
		static char becker_ip[64], becker_port[16];
		snprintf(becker_ip, sizeof becker_ip, "%s", host_option("becker_ip", ""));
		snprintf(becker_port, sizeof becker_port, "%s", host_option("becker_port", ""));
		if (becker_ip[0] != '\0') {
			argv[argc++] = "-becker-ip";
			argv[argc++] = becker_ip;
		}
		if (becker_port[0] != '\0') {
			argv[argc++] = "-becker-port";
			argv[argc++] = becker_port;
		}
	}

	/* Keyboard layout for "typed characters" mode and the menus. */
	value = host_option("keyboard", "us");
	argv[argc++] = "-kbd-lang";
	argv[argc++] = (char *) value;

	/* Keys type the characters printed on them (Shift+' types "), rather
	   than being the CoCo key in the same place. The menu turns it off
	   for games that want the CoCo's layout. */
	argv[argc++] = option_on("typed_keys", TRUE) ? "-kbd-translate" : "-no-kbd-translate";

	/* Artifact colour renderer: none, simple, 5bit, partial, simulated. */
	value = host_option("artifacts", NULL);
	if (value != NULL && *value != '\0') {
		argv[argc++] = "-ccr";
		argv[argc++] = (char *) value;
	}

	/* Picture area: zoomed, title, action, underscan. */
	value = host_option("picture", NULL);
	if (value != NULL && *value != '\0') {
		argv[argc++] = "-vo-picture";
		argv[argc++] = (char *) value;
	}

	/* cmp (S-Video), cmp-br, cmp-rb (composite: artifact colours) or rgb. */
	value = host_option("tv_input", NULL);
	if (value != NULL && *value != '\0') {
		argv[argc++] = "-tv-input";
		argv[argc++] = (char *) value;
	}

	/* Which joystick is plugged into each port: joy0, joy1, kjoy0. */
	value = host_option("joy_right", NULL);
	if (value != NULL && *value != '\0') {
		argv[argc++] = "-joy-right";
		argv[argc++] = (char *) value;
	}
	value = host_option("joy_left", NULL);
	if (value != NULL && *value != '\0') {
		argv[argc++] = "-joy-left";
		argv[argc++] = (char *) value;
	}

	argv[argc++] = "-volume";
	argv[argc++] = (char *) host_option("volume", "70");

	/* A disk, tape, cartridge or program to start: autostart=/games/x.dsk */
	value = host_option("autostart", NULL);
	if (value != NULL && *value != '\0' && argc + 2 <= max) {
		argv[argc++] = "-run";
		argv[argc++] = (char *) value;
	}

	argv[argc] = NULL;
	return argc;
}

static void fatal(const char *text)
{
	LOG_ERROR("%s\n", text);
	osd_open();
	osd_message(text, FALSE);
	for (;;) {
		bmcoco_idle();
		host_video_present(TRUE);
	}
}

void bmcoco_main(void)
{
	static char *argv[64];
	int argc, pitch;
	double last_second;

	if (host_video_init(FB_W, FB_H, &fb_pixels, &pitch) != 0) {
		LOG_ERROR("BMCoco: no display\n");
		for (;;)
			host_sleep_us(1000000);
	}
	fb_pitch_px = pitch / 2;
	clear_framebuffer();
	apply_view();
	measure_refresh_rate();
	open_sound();

	argc = build_args(argv, 62);
	ui_module_list = bmcoco_ui_module_list;
	if (xroar_init(argc, argv) == NULL)
		fatal("XRoar did not start. The log (see the User Guide) says why.");
	xroar_init_finish();
	choose_machine();

	{
		const char *missing = bmcoco_missing_roms();
		if (missing != NULL) {
			char text[320];
			snprintf(text, sizeof(text),
			         "ROMs not found: %s\n\n"
			         "Copy the Color Computer ROM files into the roms folder "
			         "on the SD card (see ROMS.txt there), then restart.\n\n"
			         "Press F12 for the menu.", missing);
			LOG_WARN("BMCoco: ROMs not found: %s\n", missing);
			osd_message(text, TRUE);
		}
	}

	reset_pacing();
	last_second = host_time();

	for (;;) {
		double now;

		process_input();
		if (open_menu) {
			open_menu = FALSE;
			hk_focus_in();
			bmcoco_menu();
			/* Keys held when the menu closed must not stick. */
			hk_focus_in();
			reset_pacing();
		}

		xroar_run(EVENT_MS(10));
		pace();

		now = host_time();
		if (now - last_second >= 1.0) {
			last_second = now;
			sync_files(FALSE);
			show_network_address();
			if (ao_corrections > 200 || ao_corrections < -200) {
				LOG_DEBUG(1, "BMCoco: sound rate corrections %+d\n", ao_corrections);
			}
			ao_corrections = 0;
		}
		handle_requests();
	}
}
