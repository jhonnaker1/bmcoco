/*
 * cartbus.c - BMCoco's own things on the cartridge bus, beside the
 * cartridge plugged in: the RS-232 Pak (rs232.c) and the hard disk
 * activity light (activity.c).
 *
 * XRoar shows a cartridge every bus cycle, whatever the address, so these
 * take over the cartridge's read(), write() and reset(), pass each cycle on
 * to it, and then see it themselves. They share one wrapper, put in once a
 * frame when one of them wants it (again, if the cartridge changed) and
 * taken out when none does: two wrapping the same cartridge each in turn
 * would each find the other's in place, wrap it again, and call each other
 * for ever. As the wrapper runs every cycle, it is only put in when needed
 * (the activity light only wants it with a hard disk interface), and the
 * addresses are sorted out before anything else is called.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cart.h"
#include "machine.h"
#include "xroar.h"

#include "activity.h"
#include "bmcoco.h"

static struct {
	struct cart *cart;      /* wrapped, or NULL */
	uint8_t (*read)(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D);
	uint8_t (*write)(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D);
	void (*reset)(struct cart *c, bool hard);
} bus;

/* Everything here is at $FF40-$FF6F: the IDE interface ($FF50), the RS-232
   Pak ($FF68) and the 65SPI ($FF6C). */
#define OURS(A) ((A) >= 0xff40 && (A) < 0xff70)

static uint8_t bus_read(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D)
{
	if (bus.read != NULL)
		D = bus.read(c, A, P2, R2, D);
	if (OURS(A)) {
		D = bmcoco_rs232_bus_read(A, P2, R2, D);
		activity_bus_read(A, P2, R2, D);
	}
	return D;
}

static uint8_t bus_write(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D)
{
	if (bus.write != NULL)
		D = bus.write(c, A, P2, R2, D);
	if (OURS(A)) {
		bmcoco_rs232_bus_write(A, P2, R2, D);
		activity_bus_write(A, P2, R2, D);
	}
	return D;
}

static void bus_reset(struct cart *c, bool hard)
{
	if (bus.reset != NULL)
		bus.reset(c, hard);
	bmcoco_rs232_bus_reset();
	activity_bus_reset();
}

struct cart *cartbus_cart(void)
{
	return bus.cart;
}

void cartbus_frame(void)
{
	struct cart *c = NULL;
	int wanted;
	if (xroar.machine != NULL)
		c = xroar.machine->get_interface(xroar.machine, "cart");
	wanted = c != NULL && (bmcoco_rs232_enabled() || activity_wants_bus(c));
	if (bus.cart != NULL && (bus.cart != c || !wanted)) {
		/* The cartridge wrapped is gone (XRoar freed it), or still
		   there and no longer wanted: its own functions back. */
		if (bus.cart == c && c->read == bus_read) {
			c->read = bus.read;
			c->write = bus.write;
			c->reset = bus.reset;
		}
		bus.cart = NULL;
		bmcoco_rs232_bus_changed();
	}
	if (wanted && (c != bus.cart || c->read != bus_read)) {
		bus.cart = c;
		bus.read = c->read;
		bus.write = c->write;
		bus.reset = c->reset;
		c->read = bus_read;
		c->write = bus_write;
		c->reset = bus_reset;
		activity_bus_reset();
		bmcoco_rs232_bus_changed();
	}
}
