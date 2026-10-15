/*
 * bmcoco.h - shared declarations for the BMCoco XRoar port.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_PORT_H_
#define BMCOCO_PORT_H_

#include <stdint.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* USB HID usage codes (keyboard page) that BMCoco itself uses. */
enum {
	USAGE_A = 0x04,
	USAGE_RETURN = 0x28, USAGE_ESCAPE = 0x29, USAGE_BACKSPACE = 0x2a,
	USAGE_TAB = 0x2b, USAGE_SPACE = 0x2c,
	USAGE_F1 = 0x3a, USAGE_F10 = 0x43, USAGE_F11 = 0x44, USAGE_F12 = 0x45,
	USAGE_PAUSE = 0x48, USAGE_HOME = 0x4a, USAGE_PAGEUP = 0x4b,
	USAGE_DELETE = 0x4c, USAGE_END = 0x4d, USAGE_PAGEDOWN = 0x4e,
	USAGE_RIGHT = 0x4f, USAGE_LEFT = 0x50, USAGE_DOWN = 0x51, USAGE_UP = 0x52,
	USAGE_KP_ENTER = 0x58, USAGE_POWER = 0x66,
	USAGE_LCTRL = 0xe0, USAGE_LSHIFT = 0xe1, USAGE_LALT = 0xe2,
	USAGE_RCTRL = 0xe4, USAGE_RSHIFT = 0xe5, USAGE_RALT = 0xe6
};

/* Keys BMCoco keeps for itself; everything else goes to the CoCo. */
#define KEY_MENU   USAGE_F12
#define KEY_TURBO  USAGE_F11
#define KEY_RESET  USAGE_F10

/* platform.c */
void bmcoco_main(void);
const char *bmcoco_sd_root(void);
/* The emulator's frame buffer, for the on-screen display. */
uint16_t *bmcoco_framebuffer(int *pitch_px);
/* Shows the on-screen display's 640x240 area (TRUE) or the emulator's
   picture (FALSE). */
void bmcoco_osd_view(int on);
/* Called while a menu waits: keeps sound fed and runs network requests. */
void bmcoco_idle(void);
double bmcoco_refresh_rate(void);
double bmcoco_machine_rate(void);
int bmcoco_vsync_locked(void);
/* TRUE while frames take longer than the display's refresh and the clock
   paces instead; and how long they take, in % of a refresh (the smoothed
   average, and the most in the last 30 seconds). */
int bmcoco_overloaded(void);
void bmcoco_load(int *average, int *maximum);
void bmcoco_video_settings_changed(void);
void bmcoco_sound_settings_changed(void);
int bmcoco_turbo(void);
void bmcoco_set_turbo(int on);
/* Names of the ROM files the current machine needs and that are missing,
   or NULL if it has them all. */
const char *bmcoco_missing_roms(void);
/* Reboot or power off, closing files first. */
void bmcoco_request_power(int power_off);
/* Web UI requests waiting to be done; menus close for them. */
int bmcoco_pending_requests(void);

/* joystick.c: the CoCo's joysticks from gamepads and the GPIO header. */
extern struct joystick_module bmcoco_js_mod;
/* Gamepad buttons that type keys (X Space, Y Enter); called each frame. */
void bmcoco_pad_keys(void);
/* Which gamepad (0-3, -1 none) feeds joystick N (0 or 1). */
int bmcoco_joystick_pad(int n);
void bmcoco_joystick_settings_changed(void);

/* menu.c */
void bmcoco_menu(void);
/* Starts following XRoar's state; from the user interface module. */
void bmcoco_menu_attach(void);

#endif /* BMCOCO_PORT_H_ */
