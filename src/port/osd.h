/*
 * osd.h - BMCoco's on-screen display: menus, file browser, text entry.
 *
 * XRoar's own menus belong to its GTK, Windows and Mac front ends, so BMCoco
 * draws its own, straight into the emulator's frame buffer, in the style of
 * a CoCo text screen. While the display is open the emulation is paused.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_OSD_H_
#define BMCOCO_OSD_H_

#include <stddef.h>
#include <stdint.h>

/* The text grid. */
#define OSD_COLS 40
#define OSD_ROWS 24

/* Keys from osd_read_key(): printable characters are themselves. */
enum {
	OSD_KEY_NONE = 0,
	OSD_KEY_UP = 0x100,
	OSD_KEY_DOWN,
	OSD_KEY_LEFT,
	OSD_KEY_RIGHT,
	OSD_KEY_PGUP,
	OSD_KEY_PGDN,
	OSD_KEY_HOME,
	OSD_KEY_END,
	OSD_KEY_ENTER,
	OSD_KEY_BACK,       /* Esc, gamepad B: leave */
	OSD_KEY_BACKSPACE,
	OSD_KEY_DELETE,
	OSD_KEY_MENU        /* the menu key again: close all menus */
};

/* Menu items. */
#define OSD_ITEM_LABEL    0x01  /* text only, can't be chosen */
#define OSD_ITEM_CHECK    0x02  /* shows a check box */
#define OSD_ITEM_CHECKED  0x04
#define OSD_ITEM_SUBMENU  0x08  /* shows ">" */

struct osd_item {
	const char *text;
	const char *value;  /* shown on the right, or NULL */
	unsigned flags;
	int id;             /* returned when chosen */
};

/* Opens the display (pauses video output of the emulator) and closes it. */
void osd_open(void);
void osd_close(void);
int osd_is_open(void);

/* Returns the next key, waiting for one. While waiting, the display is
   refreshed and the sound output kept fed. */
int osd_read_key(void);
/* Returns a key if one was pressed, or OSD_KEY_NONE, after waiting for the
   next display refresh. PADS: gamepad buttons count as keys. */
int osd_poll_key(int pads);
/* Draws a page of text without waiting. */
void osd_show_text(const char *title, const char *text, const char *hint);
/* TRUE once the menu key or gamepad Home was pressed in a menu: every menu
   returns -1 until osd_close(). */
int osd_closing(void);

/* Shows ITEMS and returns the id of the one chosen, or -1 to go back.
   *SELECTED is the item (index) to start on, and is updated. */
int osd_menu(const char *title, const struct osd_item *items, int n, int *selected);
/* A list of CHOICES (NULL-terminated); returns the index, or -1. */
int osd_choose(const char *title, const char *const choices[], int current);
/* Edits TEXT (SIZE bytes). Returns 1 if accepted with Enter. */
int osd_edit(const char *title, char *text, size_t size, int secret);
/* Shows a message. WAIT: wait for a key before returning. Without WAIT the
   message stays on the screen, and the display open, until the next
   drawing or osd_close(). */
void osd_message(const char *text, int wait);
/* A page of text (lines separated by \n), until a key is pressed. */
void osd_info(const char *title, const char *text);
/* Asks a yes/no question; returns 1 for yes. */
int osd_confirm(const char *title, const char *question);
/* Lets a file be chosen, starting in DIR (updated to the directory the file
   was in). Only files with one of EXTS (NULL-terminated, lower case, no dot)
   are listed; NULL lists everything. Returns 1 with the path in PATH. */
int osd_browse(const char *title, char *dir, size_t dir_size,
               const char *const exts[], char *path, size_t path_size);

/* Draws a one line message over the emulator's picture, for a few seconds. */
void osd_notify(const char *text);
/* Called by the video output each frame: draws the notification, if any. */
void osd_draw_notification(uint16_t *fb, int pitch_px, int w, int h);

/* The disk activity line (see platform.c), drawn on the picture. */
void osd_draw_disk_status(uint16_t *fb, int pitch_px, int w, int h,
                          const char *text, int light, int error);

#endif /* BMCOCO_OSD_H_ */
