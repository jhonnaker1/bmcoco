/*
 * joystick.c - the CoCo's joysticks for BMCoco.
 *
 * The CoCo's joysticks are analog: each axis is a position, read by the
 * CoCo's 6 bit DAC. BMCoco offers XRoar two joysticks, "joy0" and "joy1"
 * (Joystick 1 and 2), as XRoar's "physical" joystick submodule. Each takes
 * its input from a USB gamepad and from a joystick wired to the GPIO header:
 *
 *  - a gamepad's left analog stick gives the position as it is, so games
 *    that use the whole range of the CoCo's joystick work properly;
 *  - the D-pad, digital sticks and GPIO joysticks push it to the edges.
 *
 * By default Joystick 1 is plugged into the CoCo's right port (the one most
 * games use) and Joystick 2 into the left; the menu can change that.
 *
 * Gamepad buttons: A is the fire button, B the CoCo 3's second button, X
 * types Space and Y Enter, which most games want at some point.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "hkbd.h"
#include "joystick.h"
#include "xroar.h"
#include "logging.h"
#include "module.h"

#include "../host.h"
#include "bmcoco.h"

#define NUM_JOYSTICKS 2

/* Away from the centre by more than this, a stick is a position rather than
   a push on the D-pad (the kernel reports the stick as D-pad presses past a
   quarter of its range). */
#define STICK_THRESHOLD 8192

/* Which gamepad each joystick reads, from the settings: looked up once
   (the CoCo reads its joysticks thousands of times a frame), again when
   the menu changes them. -2: not looked up yet. */
static int joystick_pad[NUM_JOYSTICKS] = {-2, -2};

/* pad_keys=1: the gamepads' X and Y type Space and Enter rather than fire. */
static int pad_keys;

int bmcoco_joystick_pad(int n)
{
	static const char *const names[NUM_JOYSTICKS] = {"joy1_pad", "joy2_pad"};
	int pad;
	if (n < 0 || n >= NUM_JOYSTICKS)
		return -1;
	if (joystick_pad[n] == -2) {
		/* 0 automatic (gamepad N for joystick N), 1-4 a gamepad, -1 none. */
		pad = host_option_int(names[n], 0);
		if (pad < 0 || pad > HOST_PADS)
			joystick_pad[n] = -1;
		else
			joystick_pad[n] = pad == 0 ? n : pad - 1;
	}
	return joystick_pad[n];
}

void bmcoco_joystick_settings_changed(void)
{
	int n;
	for (n = 0; n < NUM_JOYSTICKS; n++)
		joystick_pad[n] = -2;
}

static int pad_state(int n)
{
	int pad = bmcoco_joystick_pad(n);
	int state = pad >= 0 ? host_pad_state(pad) : 0;
	return state < 0 ? 0 : state;
}

/* ---- Axes ----------------------------------------------------------------- */

struct bm_axis {
	struct joystick_control control;
	int joystick;
	int axis;
};

static int read_axis(void *sptr)
{
	struct bm_axis *a = sptr;
	int state = pad_state(a->joystick);
	int gpio = host_gpio_joystick(a->joystick);
	int pad = bmcoco_joystick_pad(a->joystick);
	int analog = pad >= 0 ? host_pad_axis(pad, a->axis) : -1;
	int neg_pad = a->axis == 0 ? HOST_PAD_LEFT : HOST_PAD_UP;
	int pos_pad = a->axis == 0 ? HOST_PAD_RIGHT : HOST_PAD_DOWN;
	int neg_gpio = a->axis == 0 ? HOST_JOY_LEFT : HOST_JOY_UP;
	int pos_gpio = a->axis == 0 ? HOST_JOY_RIGHT : HOST_JOY_DOWN;

	if (gpio & neg_gpio)
		return 0;
	if (gpio & pos_gpio)
		return 65535;
	if (analog >= 0) {
		int offset = analog - 32768;
		if (offset > STICK_THRESHOLD || offset < -STICK_THRESHOLD
		    || !(state & (neg_pad | pos_pad)))
			return analog;
		/* The stick is near the middle but the D-pad is pressed. */
	}
	if (state & neg_pad)
		return 0;
	if (state & pos_pad)
		return 65535;
	return 32768;
}

static void free_control(void *sptr)
{
	free(sptr);
}

/* Spec: "JOYSTICK,AXIS" */
static struct joystick_control *configure_axis(char *spec, unsigned jaxis)
{
	struct bm_axis *a = xmalloc(sizeof(*a));
	*a = (struct bm_axis){0};
	a->joystick = 0;
	a->axis = jaxis & 1;
	if (spec != NULL && *spec != '\0') {
		char *comma = strchr(spec, ',');
		a->joystick = atoi(spec) & 1;
		if (comma != NULL)
			a->axis = atoi(comma + 1) & 1;
	}
	a->control.read = DELEGATE_AS0(int, read_axis, a);
	a->control.free = DELEGATE_AS0(void, free_control, a);
	return &a->control;
}

/* ---- Buttons --------------------------------------------------------------- */

struct bm_button {
	struct joystick_control control;
	int joystick;
	int button;
};

static int read_button(void *sptr)
{
	struct bm_button *b = sptr;
	int state = pad_state(b->joystick);
	/* X and Y fire too (unless pad_keys=1 has them type keys): pads
	   report their buttons in many orders, and a CoCo joystick's button
	   types nothing. */
	int x = pad_keys ? 0 : HOST_PAD_X, y = pad_keys ? 0 : HOST_PAD_Y;
	if (b->button == 0)
		return (state & (HOST_PAD_A | x)) || (host_gpio_joystick(b->joystick) & HOST_JOY_FIRE);
	return (state & (HOST_PAD_B | y)) != 0;
}

/* Spec: "JOYSTICK,BUTTON" */
static struct joystick_control *configure_button(char *spec, unsigned jbutton)
{
	struct bm_button *b = xmalloc(sizeof(*b));
	*b = (struct bm_button){0};
	b->joystick = 0;
	b->button = jbutton & 1;
	if (spec != NULL && *spec != '\0') {
		char *comma = strchr(spec, ',');
		b->joystick = atoi(spec) & 1;
		if (comma != NULL)
			b->button = atoi(comma + 1) & 1;
	}
	b->control.read = DELEGATE_AS0(int, read_button, b);
	b->control.free = DELEGATE_AS0(void, free_control, b);
	return &b->control;
}

/* ---- The submodule ------------------------------------------------------------ */

/* Defines "joy0" and "joy1", which XRoar plugs into the right and left
   ports unless told otherwise. */
static void physical_init(void)
{
	static const char *const descriptions[NUM_JOYSTICKS] = {
		"Joystick 1 (gamepad 1, GPIO 1)",
		"Joystick 2 (gamepad 2, GPIO 2)",
	};
	int n, i;
	pad_keys = host_option_int("pad_keys", 0) != 0;
	for (n = 0; n < NUM_JOYSTICKS; n++) {
		struct joystick_config *jc;
		char text[32];
		snprintf(text, sizeof(text), "joy%d", n);
		if (joystick_config_by_name(text) != NULL)
			continue;
		jc = joystick_config_new();
		jc->name = xstrdup(text);
		jc->description = xstrdup(descriptions[n]);
		for (i = 0; i < JOYSTICK_NUM_AXES; i++) {
			snprintf(text, sizeof(text), "physical:%d,%d", n, i);
			jc->axis_specs[i] = xstrdup(text);
		}
		for (i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
			snprintf(text, sizeof(text), "physical:%d,%d", n, i);
			jc->button_specs[i] = xstrdup(text);
		}
	}
	bmcoco_hires_define();
}

static struct joystick_submodule bmcoco_js_physical = {
	.name = "physical",
	.init = physical_init,
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

/* The USB mouse, for XRoar's Mouse joystick (mjoy0): XRoar reads the
   pointer from the video output (platform.c moves it). */
static struct joystick_control *configure_mouse_axis(char *spec, unsigned jaxis)
{
	return joystick_configure_mouse_axis(xroar.ui_interface, spec, jaxis);
}

static struct joystick_control *configure_mouse_button(char *spec, unsigned jbutton)
{
	return joystick_configure_mouse_button(xroar.ui_interface, spec, jbutton);
}

static struct joystick_submodule bmcoco_js_mouse = {
	.name = "mouse",
	.configure_axis = configure_mouse_axis,
	.configure_button = configure_mouse_button,
};

/* XRoar's keyboard joysticks (kjoy0, the menu's Arrow keys) are read by
   its own submodule, which its desktop versions list with their own. */
extern struct joystick_submodule hkbd_js_keyboard;

static struct joystick_submodule *bmcoco_js_submodules[] = {
	&bmcoco_js_physical, &bmcoco_js_mouse, &bmcoco_js_hires, &hkbd_js_keyboard, NULL
};

struct joystick_module bmcoco_js_mod = {
	.common = { .name = "bmcoco", .description = "Gamepads, GPIO joysticks, the mouse and the keyboard" },
	.submodule_list = bmcoco_js_submodules,
};

/* ---- Buttons that type keys ------------------------------------------------ */

/* With pad_keys=1, X types Space and Y Enter. */
void bmcoco_pad_keys(void)
{
	static const struct { int button; uint8_t key; } keys[] = {
		{HOST_PAD_X, hk_scan_space},
		{HOST_PAD_Y, hk_scan_Return},
	};
	static int last[HOST_PADS];
	int pad;
	unsigned k;
	if (!pad_keys)
		return;
	for (pad = 0; pad < HOST_PADS; pad++) {
		int state = host_pad_state(pad);
		int changed;
		if (state < 0)
			state = 0;
		changed = state ^ last[pad];
		last[pad] = state;
		for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
			if (!(changed & keys[k].button))
				continue;
			if (state & keys[k].button)
				hk_scan_press(keys[k].key);
			else
				hk_scan_release(keys[k].key);
		}
	}
}
