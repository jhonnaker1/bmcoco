/*
 * hires.c - the Tandy Hi-Res Joystick Interface, with the USB mouse.
 *
 * The CoCo's joystick ports read a position with the 6 bit DAC and a
 * comparator: 64 steps an axis. Tandy's Hi-Res Joystick Interface (26-3028)
 * gives 640: it sits between the joystick (or Color Mouse) and a joystick
 * port, and also plugs into the cassette jack, which carries the DAC's
 * output. Software reads an axis by timing:
 *
 *  1. it selects the axis with the analog MUX, as for a joystick;
 *  2. it writes $FF to the DAC ($FF20), which resets the interface's
 *     timer (charges its capacitor), and waits;
 *  3. it writes a low value ($02) to the DAC: the timer starts;
 *  4. it counts round a loop until the comparator ($FF00 bit 7) goes from
 *     0 to 1, which it does after a time set by the joystick's position.
 *
 * Here, "hjoy0" (Hi-Res Mouse (USB) in the menu) is a joystick whose axes
 * do just that, with the USB mouse's pointer as the position. The DAC's
 * writes are seen by wrapping the machine's PIA1 port A data_postwrite
 * delegate (the DAC is bits 7-2 of that port), as the disk activity light
 * wraps the floppy controller's (platform.c), so XRoar is left unchanged.
 * The time is the emulated machine's own (XRoar's event ticks, 14.31818
 * MHz), so the reading is right at either CPU speed, and in turbo.
 *
 * The comparator compares the axis with the DAC: an axis reading of 0 makes
 * it read 0, 65535 makes it read 1, whatever the DAC.
 *
 * Timing: from the falling edge of the DAC, the comparator goes to 1 after
 * start + position * (end - start), position 0 (left, top) to 1 (right,
 * bottom). The defaults are the times the HIRESMS reader (the routine of
 * the mouse/cursor code at $10E8) reads across its whole range:
 *
 *   X: 629 to 5792 microseconds, read at 1.79 MHz as 0 to 616
 *   Y: 1257 to 4307 microseconds, read at 0.89 MHz as 0 to 182
 *
 * OS-9's driver (JoyDrv) then reads X as 0 to 639 over most of the width,
 * and Y as 19 to 472. Programs written for other timing can be given their
 * own with hires_mouse_x= and hires_mouse_y= in cmdline.txt (microseconds,
 * "start,end"); OS-9's is 1133,5423 on both axes.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "delegate.h"
#include "events.h"
#include "joystick.h"
#include "machine.h"
#include "mc6821.h"
#include "part.h"
#include "xroar.h"

#include "../host.h"
#include "bmcoco.h"

/* DAC values (bits 7-2 of $FF20) at or above this are high, below it
   low. */
#define DAC_HIGH 0x80

/* The default timing for X and Y, in microseconds (see above). */
static const double default_start_us[2] = {629.0, 1257.0};
static const double default_end_us[2] = {5792.0, 4307.0};

/* The DAC as last written, and when it last went from high to low. */
static uint8_t dac_value = 0xfc;
static event_ticks ramp_start;
static int ramp_started;

/* The PIA whose port A is the DAC, and the machine's own delegate for
   writes to it. */
static struct MC6821 *dac_pia;
static DELEGATE_T0(void) dac_postwrite;

static void hires_dac_postwrite(void *sptr)
{
	struct MC6821 *pia = sptr;
	uint8_t value = pia->a.out_sink & 0xfc;
	DELEGATE_SAFE_CALL(dac_postwrite);
	if (dac_value >= DAC_HIGH && value < DAC_HIGH) {
		ramp_start = event_current_tick;
		ramp_started = 1;
	}
	dac_value = value;
}

void bmcoco_hires_frame(void)
{
	struct MC6821 *pia = NULL;
	if (xroar.machine != NULL)
		pia = (struct MC6821 *) part_component_by_id_is_a(&xroar.machine->part, "PIA1", "MC6821");
	if (pia == NULL) {
		dac_pia = NULL;
		return;
	}
	if (pia != dac_pia || pia->a.data_postwrite.func != hires_dac_postwrite) {
		/* A new machine (or its delegates set again). */
		dac_pia = pia;
		dac_postwrite = pia->a.data_postwrite;
		pia->a.data_postwrite = DELEGATE_AS0(void, hires_dac_postwrite, pia);
		dac_value = pia->a.out_sink & 0xfc;
		ramp_started = 0;
	}
}

/* One axis of the interface: the USB mouse's axis (XRoar's own mouse
   joystick, 0 to 65535 across the picture) and the times, in event ticks,
   that position 0 and position 65535 take. */
struct hires_axis {
	struct joystick_control control;
	struct joystick_control *mouse;
	double start;
	double span;
};

static int read_hires_axis(void *sptr)
{
	struct hires_axis *axis = sptr;
	int position, elapsed;
	double due;
	/* While the DAC is high the timer is held: the comparator reads 0. */
	if (!ramp_started || dac_value >= DAC_HIGH)
		return 0;
	position = DELEGATE_CALL(axis->mouse->read);
	if (position < 0)
		position = 0;
	if (position > 65535)
		position = 65535;
	due = axis->start + axis->span * position / 65535.0;
	elapsed = event_tick_delta(event_current_tick, ramp_start);
	/* A long time ago (or the tick count wrapped): long past. */
	if (elapsed < 0)
		return 65535;
	return elapsed >= due ? 65535 : 0;
}

static void free_hires_axis(void *sptr)
{
	struct hires_axis *axis = sptr;
	if (axis->mouse)
		DELEGATE_SAFE_CALL(axis->mouse->free);
	free(axis);
}

/* The times for axis JAXIS from cmdline.txt (hires_mouse_x=, hires_mouse_y=:
   "start,end" in microseconds), in event ticks. */
static void axis_timing(unsigned jaxis, double *start, double *span)
{
	const char *text = host_option(jaxis == 0 ? "hires_mouse_x" : "hires_mouse_y", "");
	double start_us = default_start_us[jaxis], end_us = default_end_us[jaxis], a, b;
	if (text[0] != '\0' && sscanf(text, "%lf,%lf", &a, &b) == 2 && a >= 0.0 && b > a) {
		start_us = a;
		end_us = b;
	}
	*start = start_us * EVENT_TICK_RATE / 1000000.0;
	*span = (end_us - start_us) * EVENT_TICK_RATE / 1000000.0;
}

static struct joystick_control *configure_hires_axis(char *spec, unsigned jaxis)
{
	struct hires_axis *axis;
	struct joystick_control *mouse;
	if (jaxis >= 2)
		return NULL;
	mouse = joystick_configure_mouse_axis(xroar.ui_interface, spec, jaxis);
	if (mouse == NULL)
		return NULL;
	axis = xmalloc(sizeof(*axis));
	*axis = (struct hires_axis){0};
	axis->mouse = mouse;
	axis_timing(jaxis, &axis->start, &axis->span);
	axis->control.read = DELEGATE_AS0(int, read_hires_axis, axis);
	axis->control.free = DELEGATE_AS0(void, free_hires_axis, axis);
	return &axis->control;
}

/* The buttons are the mouse's: the interface passes them straight through. */
static struct joystick_control *configure_hires_button(char *spec, unsigned jbutton)
{
	return joystick_configure_mouse_button(xroar.ui_interface, spec, jbutton);
}

/* Defines "hjoy0", the Hi-Res Mouse (called with joy0 and joy1's
   definitions: XRoar only starts its own submodules by name). */
void bmcoco_hires_define(void)
{
	struct joystick_config *jc;
	int i;
	if (joystick_config_by_name("hjoy0") != NULL)
		return;
	jc = joystick_config_new();
	jc->name = xstrdup("hjoy0");
	jc->description = xstrdup("Hi-Res Mouse (USB mouse, Tandy Hi-Res Joystick Interface)");
	for (i = 0; i < 2; i++) {
		jc->axis_specs[i] = xstrdup("hires:");
		jc->button_specs[i] = xstrdup("hires:");
	}
}

struct joystick_submodule bmcoco_js_hires = {
	.name = "hires",
	.configure_axis = configure_hires_axis,
	.configure_button = configure_hires_button,
};
