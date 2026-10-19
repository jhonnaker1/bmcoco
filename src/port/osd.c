/*
 * osd.c - BMCoco's on-screen display: menus, file browser, text entry.
 *
 * Text is drawn with the CoCo 3's GIME character set (from XRoar), each
 * character 16x10 pixels in a 640x240 area: 40 columns by 24 rows, like a
 * CoCo 3 WIDTH 40 screen, and coloured like the VDG's green text screen.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "hkbd.h"
#include "tcc1014/font-gime.h"

#include "../host.h"
#include "bmcoco.h"
#include "osd.h"

#define OSD_W 640
#define OSD_H 240
#define CELL_W 16
#define CELL_H 10

/* RGB565 colours, after the VDG's. */
#define C_BLACK   0x0000
#define C_GREEN   0x0fe1  /* bright green: text */
#define C_DKGREEN 0x0400  /* dark green: labels and hints */
#define C_ORANGE  0xfc00  /* the selection */
#define C_RED     0xf800
#define C_DKRED   0x5000  /* the disk light, off */

#define REPEAT_DELAY 0.40
#define REPEAT_RATE  0.06

static int is_open;
static int closing;

static void flush_input(void);

/* ---- Drawing --------------------------------------------------------------- */

static uint16_t *osd_fb(int *pitch)
{
	return bmcoco_framebuffer(pitch);
}

/* Rows 0-9 of the GIME's 8x12 glyphs; each pixel is drawn two wide. */
static void draw_char_at(uint16_t *fb, int pitch, int x, int y, unsigned char c,
                         uint16_t fg, uint16_t bg)
{
	const unsigned char *glyph;
	int row, col;
	if (c >= 0x80 || c < 0x20)
		c = '?';
	glyph = &font_gime[c * 12];
	for (row = 0; row < CELL_H; row++) {
		uint16_t *p = fb + (y + row) * pitch + x;
		unsigned bits = glyph[row];
		/* The GIME shows '_' as a left arrow and '^' as an up arrow;
		   file names want the ASCII characters. */
		if (c == '_')
			bits = row == 8 ? 0xfc : 0;
		else if (c == '^')
			bits = row == 1 ? 0x10 : row == 2 ? 0x28 : row == 3 ? 0x44 : 0;
		for (col = 0; col < 8; col++) {
			uint16_t colour = (bits & (0x80 >> col)) ? fg : bg;
			p[0] = colour;
			p[1] = colour;
			p += 2;
		}
	}
}

static void put_text(int col, int row, const char *text, uint16_t fg, uint16_t bg)
{
	int pitch;
	uint16_t *fb = osd_fb(&pitch);
	for (; *text != '\0' && col < OSD_COLS; text++, col++) {
		if (col >= 0)
			draw_char_at(fb, pitch, col * CELL_W, row * CELL_H,
			             (unsigned char) *text, fg, bg);
	}
}

static void fill_row(int row, uint16_t bg)
{
	int pitch, y, x;
	uint16_t *fb = osd_fb(&pitch);
	for (y = row * CELL_H; y < (row + 1) * CELL_H; y++)
		for (x = 0; x < OSD_W; x++)
			fb[y * pitch + x] = bg;
}

static void clear_screen(void)
{
	int row;
	for (row = 0; row < OSD_ROWS; row++)
		fill_row(row, C_BLACK);
}

/* Text of at most WIDTH characters, cut with "..." if longer. */
static void fit(char *dst, size_t size, const char *src, int width)
{
	size_t len = strlen(src);
	if (width < 0)
		width = 0;
	if ((int) len <= width) {
		snprintf(dst, size, "%s", src);
	} else if (width > 3) {
		snprintf(dst, size, "%.*s...", width - 3, src);
	} else {
		snprintf(dst, size, "%.*s", width, src);
	}
}

static void put_centred(int row, const char *text, uint16_t fg, uint16_t bg)
{
	char line[OSD_COLS + 1];
	fit(line, sizeof(line), text, OSD_COLS);
	put_text((OSD_COLS - (int) strlen(line)) / 2, row, line, fg, bg);
}

static void draw_frame(const char *title, const char *hint)
{
	clear_screen();
	fill_row(0, C_GREEN);
	put_centred(0, title, C_BLACK, C_GREEN);
	if (hint != NULL)
		put_centred(OSD_ROWS - 1, hint, C_DKGREEN, C_BLACK);
}

/* ---- Opening and closing ------------------------------------------------------ */

void osd_open(void)
{
	if (is_open)
		return;
	is_open = TRUE;
	closing = FALSE;
	flush_input();
	clear_screen();
	bmcoco_osd_view(TRUE);
}

void osd_close(void)
{
	if (!is_open)
		return;
	is_open = FALSE;
	closing = FALSE;
	flush_input();
	clear_screen();
	bmcoco_osd_view(FALSE);
}

int osd_is_open(void)
{
	return is_open;
}

int osd_closing(void)
{
	return closing;
}

/* ---- Input --------------------------------------------------------------------- */

/* Characters for keys when XRoar's keyboard tables aren't set up (US). */
static int us_char(int usage, int shift)
{
	static const char normal[] = "1234567890\n\033\b\t -=[]\\#;'`,./";
	static const char shifted[] = "!@#$%^&*()\n\033\b\t _+{}|~:\"~<>?";
	if (usage >= 0x04 && usage <= 0x1d)
		return (shift ? 'A' : 'a') + usage - 0x04;
	if (usage >= 0x1e && usage <= 0x38) {
		int c = (shift ? shifted : normal)[usage - 0x1e];
		return c >= ' ' ? c : 0;
	}
	return 0;
}

/* The character a key types, using XRoar's table for the keyboard layout
   set with keyboard= in cmdline.txt. */
static int key_char(int usage)
{
	unsigned mods = host_key_modifiers();
	int shift = (mods & 0x22) != 0;
	int altgr = (mods & 0x40) != 0;
	int level = (shift ? HK_LEVEL_SHIFT : 0) | (altgr ? HK_LEVEL_ALTGR : 0);
	unsigned sym;
	if (usage < 0 || usage >= HK_NUM_SCANCODES)
		return 0;
	if (mods & 0x11)  /* Ctrl */
		return 0;
	sym = hkbd.code_to_sym[level][usage];
	if (sym >= 0x20 && sym < 0x7f)
		return (int) sym;
	if (sym == 0 && !altgr)
		return us_char(usage, shift);
	return 0;
}

static int map_usage(int usage)
{
	switch (usage) {
	case USAGE_UP: return OSD_KEY_UP;
	case USAGE_DOWN: return OSD_KEY_DOWN;
	case USAGE_LEFT: return OSD_KEY_LEFT;
	case USAGE_RIGHT: return OSD_KEY_RIGHT;
	case USAGE_PAGEUP: return OSD_KEY_PGUP;
	case USAGE_PAGEDOWN: return OSD_KEY_PGDN;
	case USAGE_HOME: return OSD_KEY_HOME;
	case USAGE_END: return OSD_KEY_END;
	case USAGE_RETURN:
	case USAGE_KP_ENTER: return OSD_KEY_ENTER;
	case USAGE_ESCAPE: return OSD_KEY_BACK;
	case USAGE_BACKSPACE: return OSD_KEY_BACKSPACE;
	case USAGE_DELETE: return OSD_KEY_DELETE;
	case KEY_MENU: return OSD_KEY_MENU;
	default: return key_char(usage);
	}
}

/* The menu also takes the GPIO joysticks, as two more pads: their
   directions move and fire chooses (A). */
#define MENU_PADS (HOST_PADS + 2)

static int pad_state(int pad)
{
	int gpio, state = 0;
	if (pad < HOST_PADS)
		return host_pad_state(pad);
	gpio = host_gpio_joystick(pad - HOST_PADS);
	if (gpio & HOST_JOY_UP) state |= HOST_PAD_UP;
	if (gpio & HOST_JOY_DOWN) state |= HOST_PAD_DOWN;
	if (gpio & HOST_JOY_LEFT) state |= HOST_PAD_LEFT;
	if (gpio & HOST_JOY_RIGHT) state |= HOST_PAD_RIGHT;
	if (gpio & HOST_JOY_FIRE) state |= HOST_PAD_A;
	return state;
}

static int pad_last[MENU_PADS];
static int repeat_usage = -1;
static int repeat_pad_key = OSD_KEY_NONE;
static double repeat_next;

/* Gamepad buttons that were pressed since last time, as OSD keys. */
static int read_pads(double now)
{
	int pad;
	for (pad = 0; pad < MENU_PADS; pad++) {
		int state = pad_state(pad);
		int pressed;
		if (state < 0)
			state = 0;
		pressed = state & ~pad_last[pad];
		pad_last[pad] = state;
		if ((pressed & HOST_PAD_HOME)
		    || ((pressed & (HOST_PAD_SELECT | HOST_PAD_START))
		        && (state & HOST_PAD_SELECT) && (state & HOST_PAD_START)))
			return OSD_KEY_MENU;
		if (pressed & HOST_PAD_A)
			return OSD_KEY_ENTER;
		if (pressed & HOST_PAD_B)
			return OSD_KEY_BACK;
		if (pressed & HOST_PAD_L)
			return OSD_KEY_PGUP;
		if (pressed & HOST_PAD_R)
			return OSD_KEY_PGDN;
		if (pressed & (HOST_PAD_UP | HOST_PAD_DOWN | HOST_PAD_LEFT | HOST_PAD_RIGHT)) {
			repeat_pad_key = (pressed & HOST_PAD_UP) ? OSD_KEY_UP
			                 : (pressed & HOST_PAD_DOWN) ? OSD_KEY_DOWN
			                 : (pressed & HOST_PAD_LEFT) ? OSD_KEY_LEFT
			                 : OSD_KEY_RIGHT;
			repeat_usage = -1;
			repeat_next = now + REPEAT_DELAY;
			return repeat_pad_key;
		}
	}
	return OSD_KEY_NONE;
}

static int pad_direction_held(int key)
{
	int pad, bit;
	switch (key) {
	case OSD_KEY_UP: bit = HOST_PAD_UP; break;
	case OSD_KEY_DOWN: bit = HOST_PAD_DOWN; break;
	case OSD_KEY_LEFT: bit = HOST_PAD_LEFT; break;
	case OSD_KEY_RIGHT: bit = HOST_PAD_RIGHT; break;
	default: return FALSE;
	}
	for (pad = 0; pad < MENU_PADS; pad++) {
		int state = pad_state(pad);
		if (state > 0 && (state & bit))
			return TRUE;
	}
	return FALSE;
}

/* Forgets gamepad presses, so they don't act when pads count again. */
static void flush_pads(void)
{
	int pad;
	for (pad = 0; pad < MENU_PADS; pad++) {
		int state = pad_state(pad);
		pad_last[pad] = state < 0 ? 0 : state;
	}
}

/* Empties the input queues, so keys pressed before don't act in the menu
   (or in the emulator, after it). */
static void flush_input(void)
{
	int usage, pressed, pad;
	while (host_key_event(&usage, &pressed))
		;
	for (pad = 0; pad < MENU_PADS; pad++) {
		int state = pad_state(pad);
		pad_last[pad] = state < 0 ? 0 : state;
	}
	repeat_usage = -1;
	repeat_pad_key = OSD_KEY_NONE;
}

/* One look at the keyboard and gamepads. */
static int poll_input(int pads)
{
	int usage, pressed, key;
	double now = host_time();

	if (bmcoco_pending_requests()) {
		closing = TRUE;
		return OSD_KEY_MENU;
	}
	while (host_key_event(&usage, &pressed)) {
		if (!pressed) {
			if (usage == repeat_usage)
				repeat_usage = -1;
			continue;
		}
		if (usage == USAGE_POWER) {
			/* Shut Down, as during play (platform.c). */
			bmcoco_request_power(1);
			closing = TRUE;
			return OSD_KEY_MENU;
		}
		key = map_usage(usage);
		if (key == OSD_KEY_NONE)
			continue;
		repeat_usage = usage;
		repeat_pad_key = OSD_KEY_NONE;
		repeat_next = now + REPEAT_DELAY;
		if (key == OSD_KEY_MENU)
			closing = TRUE;
		return key;
	}
	if (pads) {
		key = read_pads(now);
		if (key != OSD_KEY_NONE) {
			if (key == OSD_KEY_MENU)
				closing = TRUE;
			return key;
		}
	}
	if (now >= repeat_next) {
		if (repeat_usage >= 0 && host_key_down(repeat_usage)) {
			key = map_usage(repeat_usage);
			repeat_next = now + REPEAT_RATE;
			/* Only movement and deleting repeat. */
			if (key >= OSD_KEY_UP && key != OSD_KEY_ENTER && key != OSD_KEY_BACK
			    && key != OSD_KEY_MENU)
				return key;
		} else if (pads && repeat_pad_key != OSD_KEY_NONE
		           && pad_direction_held(repeat_pad_key)) {
			repeat_next = now + REPEAT_RATE;
			return repeat_pad_key;
		}
	}
	return OSD_KEY_NONE;
}

int osd_poll_key(int pads)
{
	int key;
	if (closing)
		return OSD_KEY_MENU;
	key = poll_input(pads);
	if (key == OSD_KEY_NONE) {
		/* Waits for the next display refresh. */
		bmcoco_idle();
		host_video_present(TRUE);
		if (!pads)
			flush_pads();
	}
	return key;
}

int osd_read_key(void)
{
	for (;;) {
		int key = osd_poll_key(TRUE);
		if (key != OSD_KEY_NONE)
			return key;
	}
}

/* ---- Menus ----------------------------------------------------------------------- */

#define LIST_TOP 2
#define LIST_ROWS (OSD_ROWS - 4)

static int selectable(const struct osd_item *item)
{
	return !(item->flags & OSD_ITEM_LABEL);
}

static void draw_item(int row, const struct osd_item *item, int highlight)
{
	char line[OSD_COLS + 1], text[OSD_COLS + 1], value[OSD_COLS + 1];
	int text_width, value_len = 0, col = 1;
	uint16_t fg = item->flags & OSD_ITEM_LABEL ? C_DKGREEN : C_GREEN;
	uint16_t bg = C_BLACK;

	if (highlight) {
		fg = C_BLACK;
		bg = C_ORANGE;
	}
	memset(line, ' ', OSD_COLS);
	line[OSD_COLS] = '\0';

	if (item->flags & OSD_ITEM_CHECK) {
		memcpy(line + col, item->flags & OSD_ITEM_CHECKED ? "[X] " : "[ ] ", 4);
		col += 4;
	}
	if (item->value != NULL) {
		fit(value, sizeof(value), item->value, OSD_COLS / 2);
		value_len = (int) strlen(value);
	}
	if (item->flags & OSD_ITEM_SUBMENU)
		value_len = value_len > 0 ? value_len + 2 : 1;
	text_width = OSD_COLS - 1 - col - (value_len > 0 ? value_len + 1 : 0);
	fit(text, sizeof(text), item->text, text_width);
	memcpy(line + col, text, strlen(text));
	if (item->value != NULL)
		memcpy(line + OSD_COLS - 1 - value_len, value, strlen(value));
	if (item->flags & OSD_ITEM_SUBMENU)
		line[OSD_COLS - 2] = '>';

	put_text(0, row, line, fg, bg);
}

int osd_menu(const char *title, const struct osd_item *items, int n, int *selected)
{
	int sel = selected != NULL ? *selected : 0;
	int top = 0;

	if (n <= 0)
		return -1;
	if (sel < 0 || sel >= n)
		sel = 0;
	/* Start on something that can be chosen. */
	if (!selectable(&items[sel])) {
		int i;
		for (i = 0; i < n && !selectable(&items[(sel + i) % n]); i++)
			;
		sel = (sel + i) % n;
	}

	for (;;) {
		int i, key, step = 0;

		if (sel < top)
			top = sel;
		if (sel >= top + LIST_ROWS)
			top = sel - LIST_ROWS + 1;

		draw_frame(title, "Enter: choose   Esc: back   F12: close");
		for (i = 0; i < LIST_ROWS && top + i < n; i++)
			draw_item(LIST_TOP + i, &items[top + i], top + i == sel);
		if (top > 0)
			put_text(OSD_COLS - 1, LIST_TOP - 1, "^", C_DKGREEN, C_BLACK);
		if (top + LIST_ROWS < n)
			put_text(OSD_COLS - 1, LIST_TOP + LIST_ROWS, "v", C_DKGREEN, C_BLACK);

		key = osd_read_key();
		switch (key) {
		case OSD_KEY_UP: step = -1; break;
		case OSD_KEY_DOWN: step = 1; break;
		case OSD_KEY_PGUP: step = -LIST_ROWS; break;
		case OSD_KEY_PGDN: step = LIST_ROWS; break;
		case OSD_KEY_HOME: sel = -1; step = 1; break;
		case OSD_KEY_END: sel = n; step = -1; break;
		case OSD_KEY_ENTER:
		case OSD_KEY_RIGHT:
			if (key == OSD_KEY_RIGHT && !(items[sel].flags & OSD_ITEM_SUBMENU))
				break;
			if (selected != NULL)
				*selected = sel;
			return items[sel].id;
		case OSD_KEY_BACK:
		case OSD_KEY_LEFT:
		case OSD_KEY_BACKSPACE:
		case OSD_KEY_MENU:
			if (selected != NULL)
				*selected = sel;
			return -1;
		default:
			/* A letter jumps to the next item starting with it. */
			if (key > ' ' && key < 0x7f) {
				for (i = 1; i <= n; i++) {
					int j = (sel + i) % n;
					if (selectable(&items[j])
					    && tolower((unsigned char) items[j].text[0]) == tolower(key)) {
						sel = j;
						break;
					}
				}
			}
			break;
		}
		if (step != 0) {
			/* Moves by STEP, then on to the nearest item that can be
			   chosen, staying within the list. */
			int dir = step > 0 ? 1 : -1;
			int target = sel + step;
			if (target < 0)
				target = 0;
			if (target >= n)
				target = n - 1;
			while (target >= 0 && target < n && !selectable(&items[target]))
				target += dir;
			if (target < 0 || target >= n) {
				target = sel + step;
				dir = -dir;
				if (target < 0)
					target = 0;
				if (target >= n)
					target = n - 1;
				while (target >= 0 && target < n && !selectable(&items[target]))
					target += dir;
			}
			if (target >= 0 && target < n)
				sel = target;
		}
	}
}

int osd_choose(const char *title, const char *const choices[], int current)
{
	struct osd_item items[64];
	int n, sel = current;
	for (n = 0; choices[n] != NULL && n < 64; n++) {
		items[n].text = choices[n];
		items[n].value = n == current ? "*" : NULL;
		items[n].flags = 0;
		items[n].id = n;
	}
	return osd_menu(title, items, n, &sel);
}

int osd_edit(const char *title, char *text, size_t size, int secret)
{
	char buf[128];
	size_t len;
	(void) secret;  /* shown while typed, so it can be checked */
	snprintf(buf, sizeof(buf), "%s", text);
	if (size > sizeof(buf))
		size = sizeof(buf);
	len = strlen(buf);

	for (;;) {
		char line[OSD_COLS + 1];
		int key;
		const char *shown = buf;
		/* The end of the text, if it's too long for the line. */
		if (len > OSD_COLS - 4)
			shown = buf + len - (OSD_COLS - 4);
		draw_frame(title, "Enter: OK   Esc: cancel");
		snprintf(line, sizeof(line), "%s_", shown);
		put_text(2, 5, line, C_GREEN, C_BLACK);

		key = osd_read_key();
		if (key == OSD_KEY_ENTER) {
			snprintf(text, size, "%s", buf);
			return 1;
		}
		if (key == OSD_KEY_BACK || key == OSD_KEY_MENU)
			return 0;
		if (key == OSD_KEY_BACKSPACE || key == OSD_KEY_LEFT) {
			if (len > 0)
				buf[--len] = '\0';
		} else if (key == OSD_KEY_DELETE) {
			len = 0;
			buf[0] = '\0';
		} else if (key >= ' ' && key < 0x7f && len + 1 < size) {
			buf[len++] = (char) key;
			buf[len] = '\0';
		}
	}
}

/* Word wraps TEXT into lines of at most WIDTH characters; returns how many
   lines were drawn from ROW on. */
static int draw_wrapped(int row, int max_rows, const char *text, uint16_t fg)
{
	int drawn = 0;
	while (*text != '\0' && drawn < max_rows) {
		char line[OSD_COLS + 1];
		int len = (int) strcspn(text, "\n");
		int width = OSD_COLS - 2;
		if (len > width) {
			int cut = width;
			while (cut > 0 && text[cut] != ' ')
				cut--;
			len = cut > 0 ? cut : width;
		}
		snprintf(line, sizeof(line), "%.*s", len, text);
		put_text(1, row + drawn, line, fg, C_BLACK);
		drawn++;
		text += len;
		if (*text == ' ' || *text == '\n')
			text++;
	}
	return drawn;
}

void osd_message(const char *text, int wait)
{
	int was_open = is_open;
	if (!was_open)
		osd_open();
	draw_frame("BMCoco", wait ? "Press any key" : NULL);
	draw_wrapped(5, OSD_ROWS - 7, text, C_GREEN);
	if (wait) {
		osd_read_key();
	} else {
		/* Make sure it's on the screen before something slow happens. */
		host_video_present(TRUE);
		host_video_present(TRUE);
	}
	if (!was_open && wait)
		osd_close();
}

void osd_show_text(const char *title, const char *text, const char *hint)
{
	draw_frame(title, hint);
	draw_wrapped(LIST_TOP, LIST_ROWS, text, C_GREEN);
}

void osd_info(const char *title, const char *text)
{
	osd_show_text(title, text, "Press any key");
	osd_read_key();
}

int osd_confirm(const char *title, const char *question)
{
	struct osd_item items[4] = {
		{question, NULL, OSD_ITEM_LABEL, -1},
		{"", NULL, OSD_ITEM_LABEL, -1},
		{"Yes", NULL, 0, 1},
		{"No", NULL, 0, 0},
	};
	int sel = 3;
	return osd_menu(title, items, 4, &sel) == 1;
}

/* ---- File browser ------------------------------------------------------------- */

struct entry {
	char *name;
	int is_dir;
};

static int compare_entries(const void *a, const void *b)
{
	const struct entry *ea = a, *eb = b;
	if (ea->is_dir != eb->is_dir)
		return eb->is_dir - ea->is_dir;
	return strcasecmp(ea->name, eb->name);
}

static int has_ext(const char *name, const char *const exts[])
{
	const char *dot = strrchr(name, '.');
	int i;
	if (exts == NULL)
		return TRUE;
	if (dot == NULL)
		return FALSE;
	for (i = 0; exts[i] != NULL; i++)
		if (strcasecmp(dot + 1, exts[i]) == 0)
			return TRUE;
	return FALSE;
}

static void join_path(char *out, size_t size, const char *dir, const char *name)
{
	size_t len = strlen(dir);
	if (len > 0 && dir[len - 1] == '/')
		snprintf(out, size, "%s%s", dir, name);
	else
		snprintf(out, size, "%s/%s", dir, name);
}

static void parent_dir(char *dir)
{
	char *slash;
	size_t len = strlen(dir);
	while (len > 1 && dir[len - 1] == '/')
		dir[--len] = '\0';
	slash = strrchr(dir, '/');
	if (slash == NULL)
		strcpy(dir, "/");
	else if (slash == dir)
		dir[1] = '\0';
	else
		*slash = '\0';
}

/* Reads DIR into a sorted list; returns how many entries, or -1. */
static int read_dir(const char *dir, const char *const exts[], struct entry **out)
{
	DIR *d = opendir(dir);
	struct dirent *de;
	struct entry *list = NULL;
	int n = 0, cap = 0;
	if (d == NULL)
		return -1;
	while ((de = readdir(d)) != NULL) {
		char path[512];
		struct stat st;
		int is_dir;
		if (de->d_name[0] == '.')
			continue;
		join_path(path, sizeof(path), dir, de->d_name);
		is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
		if (!is_dir && !has_ext(de->d_name, exts))
			continue;
		if (n == cap) {
			struct entry *bigger;
			cap = cap ? cap * 2 : 64;
			bigger = realloc(list, cap * sizeof(*list));
			if (bigger == NULL)
				break;
			list = bigger;
		}
		list[n].name = strdup(de->d_name);
		list[n].is_dir = is_dir;
		if (list[n].name == NULL)
			break;
		n++;
	}
	closedir(d);
	qsort(list, n, sizeof(*list), compare_entries);
	*out = list;
	return n;
}

static void free_entries(struct entry *list, int n)
{
	int i;
	for (i = 0; i < n; i++)
		free(list[i].name);
	free(list);
}

int osd_browse(const char *title, char *dir, size_t dir_size,
               const char *const exts[], char *path, size_t path_size)
{
	char came_from[256] = "";

	for (;;) {
		struct entry *list = NULL;
		struct osd_item *items;
		char heading[OSD_COLS * 2];
		int n = read_dir(dir, exts, &list);
		int i, sel = 1, chosen;

		if (n < 0) {
			if (strcmp(dir, "/") != 0) {
				/* Gone (the card changed): start again at the top. */
				snprintf(dir, dir_size, "/");
				continue;
			}
			osd_message("Cannot read the SD card.", TRUE);
			return 0;
		}

		/* The directory, then "..", then the entries. */
		items = calloc(n + 2, sizeof(*items));
		if (items == NULL) {
			free_entries(list, n);
			return 0;
		}
		snprintf(heading, sizeof(heading), "%s", dir);
		items[0].text = heading;
		items[0].flags = OSD_ITEM_LABEL;
		items[0].id = -2;
		items[1].text = "..";
		items[1].value = "<DIR>";
		items[1].id = -3;
		for (i = 0; i < n; i++) {
			items[i + 2].text = list[i].name;
			items[i + 2].value = list[i].is_dir ? "<DIR>" : NULL;
			items[i + 2].id = i;
			if (came_from[0] != '\0' && strcmp(list[i].name, came_from) == 0)
				sel = i + 2;
		}
		if (n > 0 && came_from[0] == '\0')
			sel = 2;

		chosen = osd_menu(title, items, n + 2, &sel);
		free(items);

		if (chosen == -1) {
			free_entries(list, n);
			return 0;
		}
		if (chosen == -3) {
			char *slash;
			size_t len = strlen(dir);
			if (strcmp(dir, "/") == 0) {
				free_entries(list, n);
				continue;
			}
			while (len > 1 && dir[len - 1] == '/')
				dir[--len] = '\0';
			slash = strrchr(dir, '/');
			snprintf(came_from, sizeof(came_from), "%s", slash ? slash + 1 : "");
			parent_dir(dir);
			free_entries(list, n);
			continue;
		}
		if (list[chosen].is_dir) {
			char next[512];
			join_path(next, sizeof(next), dir, list[chosen].name);
			snprintf(dir, dir_size, "%s", next);
			came_from[0] = '\0';
			free_entries(list, n);
			continue;
		}
		join_path(path, path_size, dir, list[chosen].name);
		free_entries(list, n);
		return 1;
	}
}

/* ---- Notifications ------------------------------------------------------------ */

static char notify_text[OSD_COLS + 1];
static double notify_until;

void osd_notify(const char *text)
{
	fit(notify_text, sizeof(notify_text), text, OSD_COLS - 2);
	notify_until = host_time() + 3.0;
}

void osd_draw_notification(uint16_t *fb, int pitch_px, int w, int h)
{
	int len, x, y, i;
	if (notify_text[0] == '\0')
		return;
	if (host_time() > notify_until) {
		notify_text[0] = '\0';
		return;
	}
	len = (int) strlen(notify_text) + 2;
	if (len * CELL_W > w)
		len = w / CELL_W;
	x = (w - len * CELL_W) / 2;
	y = h - CELL_H - 4;
	if (x < 0 || y < 0)
		return;
	draw_char_at(fb, pitch_px, x, y, ' ', C_BLACK, C_GREEN);
	for (i = 0; i < len - 2; i++)
		draw_char_at(fb, pitch_px, x + (i + 1) * CELL_W, y,
		             (unsigned char) notify_text[i], C_BLACK, C_GREEN);
	draw_char_at(fb, pitch_px, x + (len - 1) * CELL_W, y, ' ', C_BLACK, C_GREEN);
}

/* The disk activity line at the bottom left: a light (lit while the disk
   is being read or written) and TEXT, red for an error. */
void osd_draw_disk_status(uint16_t *fb, int pitch_px, int w, int h,
                          const char *text, int light, int error)
{
	int len = (int) strlen(text), x = 8, y = h - CELL_H - 4, row, col, i;
	uint16_t lamp = light ? C_RED : C_DKRED;
	if (y < 0 || x + (len + 3) * CELL_W > w)
		return;
	draw_char_at(fb, pitch_px, x, y, ' ', C_BLACK, C_BLACK);
	for (row = 2; row < CELL_H - 2; row++)
		for (col = 2; col < CELL_W - 2; col++)
			fb[(y + row) * pitch_px + x + col] = lamp;
	draw_char_at(fb, pitch_px, x + CELL_W, y, ' ', C_BLACK, C_BLACK);
	for (i = 0; i < len; i++)
		draw_char_at(fb, pitch_px, x + (i + 2) * CELL_W, y, (unsigned char) text[i],
		             error ? C_RED : C_GREEN, C_BLACK);
	draw_char_at(fb, pitch_px, x + (len + 2) * CELL_W, y, ' ', C_BLACK, C_BLACK);
}
