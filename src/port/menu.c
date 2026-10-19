/*
 * menu.c - the BMCoco menu.
 *
 * F12, a gamepad's Home button or Select+Start open it. Everything about
 * the emulated CoCo (programs, disks, tapes, cartridges, the machine,
 * joysticks, video, keyboard) and BMCoco's own settings (network, sound,
 * power). Settings are saved in bmcoco.cfg and used again at the next boot.
 *
 * XRoar is told about changes through its UI messages (ui_update_state),
 * the same way its GTK and Windows menus do it, and BMCoco listens to the
 * same messages to know the current state.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
 * the Raspberry Pi. See LICENSE for licensing details.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slist.h"

#include "cart.h"
#include "hkbd.h"
#include "joystick.h"
#include "machine.h"
#include "messenger.h"
#include "part.h"
#include "ui.h"
#include "vdisk.h"
#include "vdrive.h"
#include "vo.h"
#include "romlist.h"
#include "xalloc.h"
#include "xconfig.h"
#include "xroar.h"

#include "../host.h"
#include "bmcoco.h"
#include "dwserver.h"
#include "osd.h"

#ifndef BMCOCO_VERSION
#define BMCOCO_VERSION "dev"
#endif

#define WPA_CONFIG "wpa_supplicant.conf"

/* Something changed that only takes effect after a reboot. */
static int reboot_needed;

/* Where the file browser was last ("" until it's first used). */
static char browse_dir[256];

/* ---- XRoar's state, from its UI messages ---------------------------------- */

static struct {
	int machine_id;
	int cart_id;
	int joystick_id[2];  /* 0 right port, 1 left */
	int tv_input;
	int ccr;
	int picture;
	int kbd_translate;
	int hkbd_lang;
	int tape_fast;
	char tape_name[256];
	char hd_name[2][256];
} state;

static void state_notify(void *sptr, int tag, void *smsg)
{
	struct ui_state_message *msg = smsg;
	(void) sptr;
	switch (tag) {
	case ui_tag_machine: state.machine_id = msg->value; break;
	case ui_tag_cartridge: state.cart_id = msg->value; break;
	case ui_tag_joystick_port:
		if (msg->value == 0 || msg->value == 1)
			state.joystick_id[msg->value] = (int) (intptr_t) msg->data;
		break;
	case ui_tag_tv_input: state.tv_input = msg->value; break;
	case ui_tag_ccr: state.ccr = msg->value; break;
	case ui_tag_picture: state.picture = msg->value; break;
	case ui_tag_kbd_translate: state.kbd_translate = msg->value; break;
	case ui_tag_hkbd_lang: state.hkbd_lang = msg->value; break;
	case ui_tag_tape_flag_fast: state.tape_fast = msg->value; break;
	case ui_tag_hd_filename:
		if (msg->value == 0 || msg->value == 1)
			snprintf(state.hd_name[msg->value], sizeof(state.hd_name[0]), "%s",
			         msg->data ? (const char *) msg->data : "");
		break;
	case ui_tag_tape_input_filename:
		snprintf(state.tape_name, sizeof(state.tape_name), "%s",
		         msg->data ? (const char *) msg->data : "");
		break;
	default: break;
	}
}

/* Called from the user interface module when XRoar starts. */
void bmcoco_menu_attach(void)
{
	static const int tags[] = {
		ui_tag_machine, ui_tag_cartridge, ui_tag_joystick_port, ui_tag_tv_input,
		ui_tag_ccr, ui_tag_picture, ui_tag_kbd_translate, ui_tag_hkbd_lang,
		ui_tag_tape_flag_fast, ui_tag_tape_input_filename, ui_tag_hd_filename,
	};
	int client = messenger_client_register();
	unsigned i;
	state.tape_fast = 1;
	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++)
		ui_messenger_join_group(client, tags[i], MESSENGER_NOTIFY_DELEGATE(state_notify, NULL));
}

/* ---- Settings ---------------------------------------------------------- */

static void save_setting(const char *name, const char *value)
{
	if (!host_option_set(name, value) || !host_options_save())
		osd_message("Cannot save bmcoco.cfg", TRUE);
}

static void save_setting_int(const char *name, int value)
{
	char text[16];
	snprintf(text, sizeof(text), "%d", value);
	save_setting(name, text);
}

static int setting_on(const char *name, int default_value)
{
	const char *value = host_option(name, NULL);
	if (value == NULL || *value == '\0')
		return default_value;
	return strcmp(value, "1") == 0 || strcmp(value, "yes") == 0
	    || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
}

static unsigned check(int on)
{
	return OSD_ITEM_CHECK | (on ? OSD_ITEM_CHECKED : 0);
}

/* Lets one of an XRoar option's values be chosen; returns its index in
   LIST, or -1. */
static int choose_enum(const char *title, const struct xconfig_enum *list, int current_value)
{
	const char *choices[48];
	int n, current = 0;
	for (n = 0; list[n].name != NULL && n < 47; n++) {
		choices[n] = list[n].description;
		if (list[n].value == current_value)
			current = n;
	}
	choices[n] = NULL;
	return osd_choose(title, choices, current);
}

static const char *enum_description(const struct xconfig_enum *list, int value)
{
	int i;
	for (i = 0; list[i].name != NULL; i++)
		if (list[i].value == value)
			return list[i].description;
	return "?";
}

static const char *base_name(const char *path)
{
	const char *slash;
	if (path == NULL)
		return NULL;
	slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void power(int off)
{
	bmcoco_request_power(off);
	bmcoco_idle();
}

/* ---- Files --------------------------------------------------------------- */

static const char *const run_exts[] = {
	"dsk", "vdk", "jvc", "os9", "dmk", "cas", "c10", "k7", "ccc", "rom",
	"bin", "hex", "s19", "sna", NULL
};
static const char *const disk_exts[] = {"dsk", "vdk", "jvc", "os9", "dmk", NULL};
static const char *const tape_exts[] = {"cas", "c10", "k7", NULL};
static const char *const cart_exts[] = {"ccc", "rom", NULL};

static int browse(const char *title, const char *const exts[], char *path, size_t size)
{
	if (browse_dir[0] == '\0')
		snprintf(browse_dir, sizeof(browse_dir), "%s", bmcoco_sd_root());
	return osd_browse(title, browse_dir, sizeof(browse_dir), exts, path, size);
}

/* Starts a program, disk, tape or cartridge: XRoar works out how. */
static int run_file(void)
{
	char path[512];
	if (!browse("Run a Program", run_exts, path, sizeof(path)))
		return FALSE;
	xroar_load_file_by_type(path, 1);
	return TRUE;
}

/* ---- Disks ----------------------------------------------------------------- */

static struct vdisk *disk_in(int drive)
{
	if (xroar.vdrive_interface == NULL)
		return NULL;
	return vdrive_disk_in_drive(xroar.vdrive_interface, drive);
}

static void drive_menu(int drive)
{
	enum { D_INSERT, D_EJECT, D_PROTECT };
	char title[32], protect_text[8];
	int sel = 0;
	snprintf(title, sizeof(title), "Drive %d", drive);
	for (;;) {
		struct vdisk *disk = disk_in(drive);
		struct osd_item items[5];
		int n = 0, chosen;
		items[n++] = (struct osd_item){
			disk ? base_name(disk->filename) : "(empty)", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"Insert Disk...", NULL, OSD_ITEM_SUBMENU, D_INSERT};
		if (disk != NULL) {
			items[n++] = (struct osd_item){"Eject", NULL, 0, D_EJECT};
			snprintf(protect_text, sizeof(protect_text), "%s",
			         disk->write_protect ? "On" : "Off");
			items[n++] = (struct osd_item){"Write Protect", protect_text, 0, D_PROTECT};
		}
		if (sel == 0)
			sel = 2;
		chosen = osd_menu(title, items, n, &sel);
		switch (chosen) {
		case D_INSERT: {
			char path[512];
			if (browse("Insert Disk", disk_exts, path, sizeof(path))) {
				xroar_insert_disk_file(drive, path);
				return;
			}
			break;
		}
		case D_EJECT:
			xroar_eject_disk(drive);
			return;
		case D_PROTECT:
			ui_update_state(-1, ui_tag_disk_write_enable, UI_NEXT, (void *) (intptr_t) drive);
			break;
		default:
			return;
		}
	}
}

static void disks_menu(void)
{
	static char names[4][48];
	int sel = 0;
	for (;;) {
		enum { D_ACTIVITY = 10 };
		struct osd_item items[8];
		int drive, chosen, activity = setting_on("disk_activity", TRUE);
		for (drive = 0; drive < 4; drive++) {
			struct vdisk *disk = disk_in(drive);
			snprintf(names[drive], sizeof(names[drive]), "%s",
			         disk ? base_name(disk->filename) : "(empty)");
			items[drive].text = drive == 0 ? "Drive 0" : drive == 1 ? "Drive 1"
			                    : drive == 2 ? "Drive 2" : "Drive 3";
			items[drive].value = names[drive];
			items[drive].flags = OSD_ITEM_SUBMENU;
			items[drive].id = drive;
		}
		items[4] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		items[5] = (struct osd_item){"Show Disk Activity", NULL, check(activity), D_ACTIVITY};
		items[6] = (struct osd_item){"Drive, track, side and sector,", NULL, OSD_ITEM_LABEL, -1};
		items[7] = (struct osd_item){"at the bottom left while loading.", NULL, OSD_ITEM_LABEL, -1};
		chosen = osd_menu("Disk Drives", items, 8, &sel);
		if (chosen < 0)
			return;
		if (chosen == D_ACTIVITY) {
			save_setting("disk_activity", activity ? "0" : "1");
			bmcoco_video_settings_changed();
			continue;
		}
		drive_menu(chosen);
	}
}

/* ---- Hard disks ----------------------------------------------------------- */

static const char *const hd_exts[] = {"ide", "vhd", "img", "hdd", "os9", "dsk", NULL};

/* The cartridge plugged in, if it is one with hard disks: an IDE
   interface (the built in one, or one from xroar.conf, such as HDB-DOS),
   or another kind XRoar gives hard disks to. */
static struct cart_config *hd_cart(void)
{
	struct cart_config *cc = state.cart_id > 0 ? cart_config_by_id(state.cart_id) : NULL;
	if (cc == NULL || cc->type == NULL)
		return NULL;
	if (strcmp(cc->type, "ide") == 0 || strcmp(cc->type, "nx32") == 0
	    || strcmp(cc->type, "mooh") == 0)
		return cc;
	return NULL;
}

/* Puts PATH in hard disk DRIVE, first plugging in XRoar's IDE interface if
   no cartridge with hard disks is (then the machine is reset). Kept for
   the next boot (hd0, hd1, hd_cart in bmcoco.cfg). */
static void insert_hd(int drive, const char *path)
{
	struct cart_config *cc = hd_cart();
	char key[8];
	int plugged = FALSE;
	if (cc == NULL) {
		sds rom = romlist_find("@glenside_ide");
		if (rom == NULL) {
			osd_message("The IDE interface needs HDB-DOS (hdblba.rom) or YA-DOS "
			            "(yados.rom) in the roms folder.", TRUE);
			return;
		}
		sdsfree(rom);
		cc = cart_config_by_name("ide");
		if (cc == NULL) {
			osd_message("XRoar has no IDE interface.", TRUE);
			return;
		}
		ui_update_state(-1, ui_tag_cartridge, cc->id, NULL);
		plugged = TRUE;
	}
	xroar_insert_hd_file(drive, path);
	snprintf(key, sizeof key, "hd%d", drive);
	save_setting(key, path);
	save_setting("hd_cart", cc->name);
	if (plugged) {
		xroar_hard_reset();
		osd_notify("IDE interface plugged in");
	}
}

static void eject_hd(int drive)
{
	char key[8];
	xroar_insert_hd_file(drive, NULL);
	snprintf(key, sizeof key, "hd%d", drive);
	save_setting(key, "");
}

static void hard_disks_menu(void)
{
	enum { H_INSERT0, H_INSERT1, H_EJECT0, H_EJECT1 };
	static char names[2][48];
	int sel = 2;
	for (;;) {
		struct cart_config *cc = hd_cart();
		struct osd_item items[11];
		int n = 0, d, chosen;
		char path[512];
		items[n++] = (struct osd_item){cc ? (cc->description ? cc->description : cc->name)
		                                  : "No hard disk interface plugged in",
		                               NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		for (d = 0; d < 2; d++) {
			snprintf(names[d], sizeof names[d], "%s",
			         state.hd_name[d][0] ? base_name(state.hd_name[d]) : "(empty)");
			items[n++] = (struct osd_item){d == 0 ? "Drive 0" : "Drive 1", names[d],
			                               OSD_ITEM_SUBMENU, d == 0 ? H_INSERT0 : H_INSERT1};
		}
		items[n++] = (struct osd_item){"Eject Drive 0", NULL, 0, H_EJECT0};
		items[n++] = (struct osd_item){"Eject Drive 1", NULL, 0, H_EJECT1};
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"Choosing an image plugs in the IDE", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"interface if none is (it needs", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"hdblba.rom in roms).", NULL, OSD_ITEM_LABEL, -1};
		chosen = osd_menu("Hard Disks", items, n, &sel);
		switch (chosen) {
		case H_INSERT0:
		case H_INSERT1:
			d = chosen == H_INSERT0 ? 0 : 1;
			if (browse(d == 0 ? "Hard Disk 0" : "Hard Disk 1", hd_exts, path, sizeof(path))) {
				insert_hd(d, path);
				return;
			}
			break;
		case H_EJECT0:
		case H_EJECT1:
			eject_hd(chosen == H_EJECT0 ? 0 : 1);
			break;
		default:
			return;
		}
	}
}

/* ---- DriveWire ------------------------------------------------------------ */

static const char *const dw_exts[] = {"dsk", "os9", "vhd", "img", "dw", NULL};

/* TRUE if the DriveWire server is BMCoco's own: no address, or this Pi's
   (as src/port/becker.c decides). */
static int dw_builtin(void)
{
	const char *ip = host_option("becker_ip", "");
	return ip[0] == '\0' || strcmp(ip, "builtin") == 0 || strcmp(ip, "127.0.0.1") == 0
	       || strcmp(ip, "localhost") == 0;
}

/* Uses the server at IP:PORT (IP "" for the built-in one) from now on, and
   at the next boot. */
static void set_dw_server(const char *ip, const char *port)
{
	free(xroar.cfg.becker.ip);
	xroar.cfg.becker.ip = ip[0] ? xstrdup(ip) : NULL;
	free(xroar.cfg.becker.port);
	xroar.cfg.becker.port = port[0] ? xstrdup(port) : NULL;
	save_setting("becker_ip", ip);
	save_setting("becker_port", port);
}

static void drivewire_menu(void)
{
	enum { W_SERVER = 10, W_DRIVE0 = 20, W_EJECT0 = 30 };
	static const char *const servers[] = {"Built-in", "Another Computer...", NULL};
	static char names[DWSERVER_DRIVES][48];
	static char server[80];
	int sel = 2;
	for (;;) {
		struct osd_item items[16];
		int n = 0, d, i, chosen;
		char path[512];
		if (dw_builtin())
			snprintf(server, sizeof server, "Built-in");
		else
			snprintf(server, sizeof server, "%s:%s", host_option("becker_ip", ""),
			         host_option("becker_port", "65504"));
		items[n++] = (struct osd_item){"For the Becker port (IDE interface,", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"or RS-DOS with Becker port).", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"Server", server, OSD_ITEM_SUBMENU, W_SERVER};
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		for (d = 0; d < DWSERVER_DRIVES; d++) {
			static const char *const labels[] = {"Drive 0", "Drive 1", "Drive 2", "Drive 3"};
			const char *image = dwserver_drive(d);
			snprintf(names[d], sizeof names[d], "%s", image ? base_name(image) : "(empty)");
			items[n++] = (struct osd_item){labels[d], names[d], OSD_ITEM_SUBMENU, W_DRIVE0 + d};
		}
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"The drives are the built-in", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"server's (NitrOS-9: /X0 to /X3).", NULL, OSD_ITEM_LABEL, -1};
		chosen = osd_menu("DriveWire", items, n, &sel);
		if (chosen == W_SERVER) {
			i = osd_choose("DriveWire Server", servers, dw_builtin() ? 0 : 1);
			if (i == 0) {
				set_dw_server("", "");
			} else if (i == 1) {
				/* ADDRESS or ADDRESS:PORT */
				char text[80];
				snprintf(text, sizeof(text), "%s", dw_builtin() ? "" : server);
				if (osd_edit("DriveWire Server (address:port)", text, sizeof(text), FALSE)) {
					char *colon = strrchr(text, ':');
					const char *port = "65504";
					if (colon != NULL) {
						*colon = '\0';
						if (colon[1])
							port = colon + 1;
					}
					set_dw_server(text, port);
				}
			}
		} else if (chosen >= W_DRIVE0 && chosen < W_DRIVE0 + DWSERVER_DRIVES) {
			static const char *const choices[] = {"Choose Image...", "Eject", NULL};
			char key[8];
			d = chosen - W_DRIVE0;
			snprintf(key, sizeof key, "dw%d", d);
			i = dwserver_drive(d) != NULL ? osd_choose(names[d], choices, 0) : 0;
			if (i == 1) {
				dwserver_set_drive(d, NULL);
				save_setting(key, "");
			} else if (i == 0 && browse(d == 0 ? "DriveWire Drive 0" : d == 1 ? "DriveWire Drive 1"
			                  : d == 2 ? "DriveWire Drive 2" : "DriveWire Drive 3",
			                  dw_exts, path, sizeof(path))) {
				if (dwserver_set_drive(d, path))
					save_setting(key, path);
				else
					osd_message("Can't open that disk image.", TRUE);
			}
		} else {
			return;
		}
	}
}

/* ---- Cassette ----------------------------------------------------------- */

static void tape_menu(void)
{
	enum { T_INSERT, T_REWIND, T_EJECT, T_FAST };
	int sel = 2;
	for (;;) {
		const char *name = state.tape_name[0] ? base_name(state.tape_name) : NULL;
		struct osd_item items[] = {
			{name ? name : "(no tape)", NULL, OSD_ITEM_LABEL, -1},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Insert Tape...", NULL, OSD_ITEM_SUBMENU, T_INSERT},
			{"Rewind", NULL, 0, T_REWIND},
			{"Eject", NULL, 0, T_EJECT},
			{"Fast Loading", NULL, check(state.tape_fast), T_FAST},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Type CLOAD or CLOADM to load.", NULL, OSD_ITEM_LABEL, -1},
		};
		int chosen = osd_menu("Cassette", items, sizeof(items) / sizeof(items[0]), &sel);
		switch (chosen) {
		case T_INSERT: {
			char path[512];
			if (browse("Insert Tape", tape_exts, path, sizeof(path))) {
				xroar_insert_input_tape_file(path);
				return;
			}
			break;
		}
		case T_REWIND:
			ui_update_state(-1, ui_tag_action, ui_action_tape_input_rewind, NULL);
			osd_notify("Tape rewound");
			return;
		case T_EJECT:
			xroar_eject_input_tape();
			return;
		case T_FAST:
			ui_update_state(-1, ui_tag_tape_flag_fast, UI_NEXT, NULL);
			break;
		default:
			return;
		}
	}
}

/* ---- Cartridges ---------------------------------------------------------- */

/* Cartridges that fit this machine: XRoar keeps a list of the kinds a
   machine takes, "dragon-cart" for the CoCo and Dragon. */
static const char *cart_arch(void)
{
	const struct partdb_entry *pe;
	if (xroar.machine_config == NULL)
		return NULL;
	pe = partdb_find_entry(xroar.machine_config->architecture);
	if (pe == NULL || !partdb_ent_is_a(pe, "machine"))
		return NULL;
	return ((const struct machine_partdb_entry *) pe)->cart_arch;
}

static const char *cart_name(int id)
{
	struct slist *l;
	for (l = cart_config_list(); l != NULL; l = l->next) {
		struct cart_config *cc = l->data;
		if (cc->id == id)
			return cc->description ? cc->description : cc->name;
	}
	return "None";
}

static void cart_menu(void)
{
	enum { C_NONE = -10, C_FILE = -11, C_RS232 = -12 };
	struct osd_item items[64];
	struct slist *l, *list;
	const char *arch = cart_arch();
	int n = 0, sel = 0, chosen;

	items[n++] = (struct osd_item){"Load Cartridge File...", NULL, OSD_ITEM_SUBMENU, C_FILE};
	items[n++] = (struct osd_item){"RS-232 Pak (Modem)", NULL, check(bmcoco_rs232_enabled()), C_RS232};
	items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
	items[n++] = (struct osd_item){"None", state.cart_id == 0 ? "*" : NULL, 0, C_NONE};
	list = arch ? cart_config_list_is_a(arch) : NULL;
	for (l = list; l != NULL && n < 64; l = l->next) {
		struct cart_config *cc = l->data;
		/* Only the ones built in: ROM files from the user are under
		   "Load Cartridge File". */
		items[n++] = (struct osd_item){
			cc->description ? cc->description : cc->name,
			cc->id == state.cart_id ? "*" : NULL, 0, cc->id};
	}
	slist_free(list);

	chosen = osd_menu("Cartridge", items, n, &sel);
	if (chosen == C_RS232) {
		save_setting("rs232", bmcoco_rs232_enabled() ? "0" : "1");
		bmcoco_rs232_settings_changed();
		if (bmcoco_rs232_enabled())
			osd_message("The RS-232 Pak is at $FF68, beside the cartridge, with a "
			            "virtual modem: ATDT host:port calls a telnet BBS. It needs "
			            "the network.", TRUE);
	} else if (chosen == C_FILE) {
		char path[512];
		if (browse("Load Cartridge", cart_exts, path, sizeof(path)))
			xroar_load_file_by_type(path, 1);
	} else if (chosen == C_NONE) {
		ui_update_state(-1, ui_tag_cartridge, 0, NULL);
		xroar_hard_reset();
	} else if (chosen >= 0) {
		ui_update_state(-1, ui_tag_cartridge, chosen, NULL);
		xroar_hard_reset();
	}
}

/* ---- Machine ------------------------------------------------------------ */

static int machine_has_roms(struct machine_config *mc)
{
	const struct partdb_entry *pe = partdb_find_entry(mc->architecture);
	const struct machine_partdb_entry *mpe;
	if (pe == NULL || !partdb_ent_is_a(pe, "machine"))
		return FALSE;
	mpe = (const struct machine_partdb_entry *) pe;
	return mpe->is_working_config != NULL && mpe->is_working_config(mc);
}

static const char *machine_name(void)
{
	struct machine_config *mc = xroar.machine_config;
	if (mc == NULL)
		return "?";
	return mc->description ? mc->description : mc->name;
}

static void machine_menu(void)
{
	struct osd_item items[48];
	struct machine_config *configs[48];
	struct slist *l;
	int n = 0, sel = 0, chosen;

	for (l = machine_config_list(); l != NULL && n < 48; l = l->next) {
		struct machine_config *mc = l->data;
		configs[n] = mc;
		if (mc == xroar.machine_config)
			sel = n;
		items[n] = (struct osd_item){
			mc->description ? mc->description : mc->name,
			mc == xroar.machine_config ? "*" : machine_has_roms(mc) ? NULL : "no ROM",
			0, n};
		n++;
	}
	chosen = osd_menu("Machine", items, n, &sel);
	if (chosen < 0 || configs[chosen] == xroar.machine_config)
		return;
	if (!machine_has_roms(configs[chosen])) {
		osd_message("The ROMs for this machine are not on the SD card. "
		            "See ROMS.txt in the roms folder.", TRUE);
		return;
	}
	ui_update_state(-1, ui_tag_machine, configs[chosen]->id, NULL);
	save_setting("machine", configs[chosen]->name);
}

/* ---- Memory ---------------------------------------------------------------- */

/* The RAM sizes XRoar has for a machine, in K: a CoCo 3 128K to 2M (as
   with a 512K upgrade or a 2M board), a CoCo 1 or 2 (or a Dragon) 4K to
   64K. The MC-10's is fixed. */
static const int *ram_sizes(const struct machine_config *mc)
{
	static const int coco3[] = {128, 512, 1024, 2048, 0};
	static const int coco[] = {4, 16, 32, 64, 0};
	if (mc == NULL || mc->architecture == NULL || strcmp(mc->architecture, "mc10") == 0)
		return NULL;
	return strcmp(mc->architecture, "coco3") == 0 ? coco3 : coco;
}

static const char *ram_text(int k, char *buf, size_t size)
{
	if (k >= 1024 && k % 1024 == 0)
		snprintf(buf, size, "%dM", k / 1024);
	else
		snprintf(buf, size, "%dK", k);
	return buf;
}

static const char *memory_value(void)
{
	static char buf[16];
	struct machine_config *mc = xroar.machine_config;
	if (mc == NULL || mc->ram <= 0)
		return NULL;
	return ram_text(mc->ram, buf, sizeof buf);
}

/* Chooses the machine's RAM: kept for that machine (ram_NAME in
   bmcoco.cfg), and the machine is set up again and hard reset, as
   choosing a machine does. */
static void memory_menu(void)
{
	struct machine_config *mc = xroar.machine_config;
	const int *sizes = ram_sizes(mc);
	char names[8][16];
	const char *choices[8];
	char key[64];
	int n, current = 0, i;
	if (sizes == NULL) {
		osd_message("This machine's memory can't be changed.", TRUE);
		return;
	}
	for (n = 0; sizes[n] != 0 && n < 7; n++) {
		choices[n] = ram_text(sizes[n], names[n], sizeof names[n]);
		if (sizes[n] <= mc->ram)
			current = n;
	}
	choices[n] = NULL;
	i = osd_choose("Memory", choices, current);
	if (i < 0 || sizes[i] == mc->ram)
		return;
	mc->ram = sizes[i];
	if (strcmp(mc->architecture, "coco3") != 0)
		mc->ram_org = ANY_AUTO;     /* the chips for that size */
	snprintf(key, sizeof key, "ram_%s", mc->name);
	save_setting_int(key, sizes[i]);
	ui_update_state(-1, ui_tag_machine, mc->id, NULL);
	osd_notify("Machine reset with the new memory");
}

/* ---- Joysticks ---------------------------------------------------------- */

static const char *joystick_name(int id)
{
	struct joystick_config *jc = id ? joystick_config_by_id(id) : NULL;
	if (jc == NULL)
		return "None";
	if (strcmp(jc->name, "joy0") == 0)
		return "Joystick 1";
	if (strcmp(jc->name, "joy1") == 0)
		return "Joystick 2";
	if (strcmp(jc->name, "kjoy0") == 0)
		return "Arrow keys";
	if (strcmp(jc->name, "mjoy0") == 0)
		return "Mouse";
	return jc->description ? jc->description : jc->name;
}

/* Plugs joystick NAME into PORT (0 right, 1 left). */
static void set_port(int port, const char *name)
{
	struct joystick_config *jc = joystick_config_by_name(name);
	if (jc == NULL)
		return;
	ui_update_state(-1, ui_tag_joystick_port, port, (void *) (intptr_t) jc->id);
	save_setting(port == 0 ? "joy_right" : "joy_left", name);
}

static void choose_port(int port)
{
	static const char *const names[] = {"joy0", "joy1", "kjoy0", "mjoy0", NULL};
	static const char *const choices[] = {
		"Joystick 1", "Joystick 2", "Arrow keys + Alt (fire)", "Mouse (USB)", NULL
	};
	struct joystick_config *jc = joystick_config_by_id(state.joystick_id[port]);
	int current = 0, i;
	for (i = 0; jc != NULL && names[i] != NULL; i++)
		if (strcmp(jc->name, names[i]) == 0)
			current = i;
	i = osd_choose(port == 0 ? "Right Joystick Port" : "Left Joystick Port", choices, current);
	if (i >= 0)
		set_port(port, names[i]);
}

static void choose_pad(int n)
{
	static const char *const choices[] = {
		"Automatic", "Gamepad 1", "Gamepad 2", "Gamepad 3", "Gamepad 4", "None", NULL
	};
	const char *name = n == 0 ? "joy1_pad" : "joy2_pad";
	int value = host_option_int(name, 0);
	int current = value < 0 ? 5 : value > 4 ? 0 : value;
	int i = osd_choose(n == 0 ? "Gamepad for Joystick 1" : "Gamepad for Joystick 2",
	                   choices, current);
	if (i >= 0) {
		save_setting_int(name, i == 5 ? -1 : i);
		bmcoco_joystick_settings_changed();
	}
}

static const char *pad_name(int n)
{
	static char text[2][32];
	int pad = bmcoco_joystick_pad(n);
	if (pad < 0)
		return "None";
	snprintf(text[n], sizeof(text[n]), "Gamepad %d%s", pad + 1,
	         host_pad_state(pad) < 0 ? " (-)" : "");
	return text[n];
}

/* Shows the joysticks as the CoCo sees them, until a key is pressed on the
   keyboard (the gamepads' buttons are being tested). */
static void joystick_test(void)
{
	for (;;) {
		char text[512];
		int port, len = 0, key;
		int buttons = joystick_read_buttons();
		for (port = 0; port < 2; port++) {
			int x = joystick_read_axis(port, 0) >> 10;
			int y = joystick_read_axis(port, 1) >> 10;
			int b1 = buttons & (port == 0 ? 1 : 2);
			int b2 = buttons & (port == 0 ? 4 : 8);
			len += snprintf(text + len, sizeof(text) - len,
			                "%s port: %s\n  X %2d  Y %2d  Fire %s  Fire 2 %s\n\n",
			                port == 0 ? "Right" : "Left",
			                joystick_name(state.joystick_id[port]),
			                x, y, b1 ? "ON " : "off", b2 ? "ON " : "off");
		}
		snprintf(text + len, sizeof(text) - len,
		         "Positions go from 0 to 63, as the CoCo reads them.");
		osd_show_text("Joystick Test", text, "Press a key to go back");
		key = osd_poll_key(FALSE);
		if (key != OSD_KEY_NONE)
			return;
	}
}

static void joysticks_menu(void)
{
	enum { J_RIGHT, J_LEFT, J_SWAP, J_PAD1, J_PAD2, J_GPIO, J_TEST };
	int sel = 0;
	for (;;) {
		struct osd_item items[] = {
			{"Right Port", joystick_name(state.joystick_id[0]), OSD_ITEM_SUBMENU, J_RIGHT},
			{"Left Port", joystick_name(state.joystick_id[1]), OSD_ITEM_SUBMENU, J_LEFT},
			{"Swap Ports", NULL, 0, J_SWAP},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Joystick 1 from", pad_name(0), OSD_ITEM_SUBMENU, J_PAD1},
			{"Joystick 2 from", pad_name(1), OSD_ITEM_SUBMENU, J_PAD2},
			{"GPIO Joysticks (reboot)", NULL, check(setting_on("gpio_joysticks", FALSE)), J_GPIO},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Joystick Test", NULL, OSD_ITEM_SUBMENU, J_TEST},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Gamepad: A fire, B fire 2,", NULL, OSD_ITEM_LABEL, -1},
			{"X Space, Y Enter.", NULL, OSD_ITEM_LABEL, -1},
		};
		int chosen = osd_menu("Joysticks", items, sizeof(items) / sizeof(items[0]), &sel);
		switch (chosen) {
		case J_RIGHT: choose_port(0); break;
		case J_LEFT: choose_port(1); break;
		case J_SWAP: {
			struct joystick_config *r = joystick_config_by_id(state.joystick_id[0]);
			struct joystick_config *l = joystick_config_by_id(state.joystick_id[1]);
			ui_update_state(-1, ui_tag_joystick_port, 0, (void *) (intptr_t) state.joystick_id[1]);
			ui_update_state(-1, ui_tag_joystick_port, 1, (void *) (intptr_t) (r ? r->id : 0));
			if (l != NULL)
				save_setting("joy_right", l->name);
			if (r != NULL)
				save_setting("joy_left", r->name);
			break;
		}
		case J_PAD1: choose_pad(0); break;
		case J_PAD2: choose_pad(1); break;
		case J_GPIO:
			save_setting("gpio_joysticks", setting_on("gpio_joysticks", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case J_TEST: joystick_test(); break;
		default: return;
		}
	}
}

/* ---- Video -------------------------------------------------------------- */

static const char *vsync_mode(void)
{
	const char *mode = host_option("vsync", "auto");
	if (strcmp(mode, "on") == 0 || strcmp(mode, "1") == 0)
		return "On";
	if (strcmp(mode, "off") == 0 || strcmp(mode, "0") == 0)
		return "Off";
	if (bmcoco_vsync_locked() && bmcoco_overloaded())
		return "Auto (Pi too slow)";
	return bmcoco_vsync_locked() ? "Auto (locked)" : "Auto (timer)";
}

/* The picture's height as a percentage of the screen's (the scale option):
   less for a TV that cuts off the edges (overscan). */
static int screen_size(void)
{
	double scale = host_option_double("scale", 1.0);
	if (scale <= 0.1 || scale > 1.0)
		scale = 1.0;
	return (int) (scale * 100.0 + 0.5);
}

static void choose_screen_size(void)
{
	static const char *const choices[] = {
		"100%: fill the screen", "95%", "90%", "85%", "80%", NULL
	};
	static const int sizes[] = {100, 95, 90, 85, 80};
	int size = screen_size(), current = 0, k, i;
	char text[16];
	for (k = 1; k < 5; k++)
		if (abs(sizes[k] - size) < abs(sizes[current] - size))
			current = k;
	i = osd_choose("Screen Size", choices, current);
	if (i < 0)
		return;
	snprintf(text, sizeof text, "%d.%02d", sizes[i] / 100, sizes[i] % 100);
	save_setting("scale", text);
	bmcoco_video_settings_changed();
}

static void video_menu(void)
{
	enum { V_TV, V_CCR, V_PICTURE, V_SIZE, V_VSYNC, V_RESOLUTION, V_SHADER, V_CURVATURE, V_SMOOTH };
	/* The Pi 5 only: its firmware ignores hdmi_mode (from BMA800XL). */
	static const char *const resolutions[] = {"auto", "1920x1080", "1280x720", NULL};
	static const char *const resolution_names[] = {"Auto", "1920x1080", "1280x720", NULL};
	int sel = 0;
	for (;;) {
		const char *res = host_option("resolution", "auto");
		int res_index = 0, n_items, k;
		char size_text[8];
		snprintf(size_text, sizeof size_text, "%d%%", screen_size());
		for (k = 0; resolutions[k] != NULL; k++)
			if (strcmp(res, resolutions[k]) == 0)
				res_index = k;
		struct osd_item items[] = {
			{"TV Input", enum_description(machine_tv_input_list, state.tv_input),
			 OSD_ITEM_SUBMENU, V_TV},
			{"Artifact Colours", enum_description(vo_cmp_ccr_list, state.ccr),
			 OSD_ITEM_SUBMENU, V_CCR},
			{"Picture Area", enum_description(vo_viewport_list, state.picture),
			 OSD_ITEM_SUBMENU, V_PICTURE},
			{"Screen Size", size_text, OSD_ITEM_SUBMENU, V_SIZE},
			{"Frame Lock", vsync_mode(), OSD_ITEM_SUBMENU, V_VSYNC},
			{"Resolution", resolution_names[res_index], OSD_ITEM_SUBMENU, V_RESOLUTION},
			{"CRT Effect (reboot)", NULL, check(setting_on("crt_shader", FALSE)), V_SHADER},
			{"CRT Curvature (reboot)", NULL, check(setting_on("crt_curvature", FALSE)), V_CURVATURE},
			{"Smooth Scaling (reboot)", NULL, check(setting_on("smooth", FALSE)), V_SMOOTH},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Composite shows the artifact colours", NULL, OSD_ITEM_LABEL, -1},
			{"CoCo games use; if red and blue are", NULL, OSD_ITEM_LABEL, -1},
			{"swapped, choose the other phase.", NULL, OSD_ITEM_LABEL, -1},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"If your TV cuts off the edges, make", NULL, OSD_ITEM_LABEL, -1},
			{"the Screen Size smaller.", NULL, OSD_ITEM_LABEL, -1},
		};
		int chosen, i;
		n_items = sizeof(items) / sizeof(items[0]);
		if (!host_display_resolution_choice()) {
			/* Resolution is only for the Pi 5 (with video_driver=kms). */
			for (k = 5; k < n_items - 1; k++)
				items[k] = items[k + 1];
			n_items--;
		}
		chosen = osd_menu("Video", items, n_items, &sel);
		switch (chosen) {
		case V_SIZE:
			choose_screen_size();
			break;
		case V_RESOLUTION:
			i = osd_choose("Resolution", resolution_names, res_index);
			if (i >= 0 && i != res_index) {
				save_setting("resolution", resolutions[i]);
				bmcoco_display_mode_changed();
			}
			break;
		case V_CURVATURE:
			save_setting("crt_curvature", setting_on("crt_curvature", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case V_TV:
			i = choose_enum("TV Input", machine_tv_input_list, state.tv_input);
			if (i >= 0) {
				ui_update_state(-1, ui_tag_tv_input, machine_tv_input_list[i].value, NULL);
				save_setting("tv_input", machine_tv_input_list[i].name);
			}
			break;
		case V_CCR: {
			/* The two most accurate are too much for a Pi 2 or 3
			   (they make it fall behind): say so. */
			static char names[8][40];
			const char *choices[8];
			int n, current = 0;
			for (n = 0; vo_cmp_ccr_list[n].name != NULL && n < 7; n++) {
				int slow = vo_cmp_ccr_list[n].value == VO_CMP_CCR_PARTIAL
				           || vo_cmp_ccr_list[n].value == VO_CMP_CCR_SIMULATED;
				snprintf(names[n], sizeof names[n], "%s%s",
				         vo_cmp_ccr_list[n].description, slow ? " (slow on Pi 2/3)" : "");
				choices[n] = names[n];
				if (vo_cmp_ccr_list[n].value == state.ccr)
					current = n;
			}
			choices[n] = NULL;
			i = osd_choose("Artifact Colours", choices, current);
			if (i >= 0) {
				ui_update_state(-1, ui_tag_ccr, vo_cmp_ccr_list[i].value, NULL);
				save_setting("artifacts", vo_cmp_ccr_list[i].name);
			}
			break;
		}
		case V_PICTURE: {
			/* "auto" isn't a picture area of its own. */
			i = choose_enum("Picture Area", vo_viewport_list + 1, state.picture);
			if (i >= 0) {
				ui_update_state(-1, ui_tag_picture, vo_viewport_list[i + 1].value, NULL);
				save_setting("picture", vo_viewport_list[i + 1].name);
			}
			break;
		}
		case V_VSYNC: {
			static const char *const choices[] = {
				"Automatic", "On: follow the display", "Off: keep exact time", NULL
			};
			static const char *const values[] = {"auto", "on", "off"};
			const char *mode = host_option("vsync", "auto");
			int current = strcmp(mode, "on") == 0 ? 1 : strcmp(mode, "off") == 0 ? 2 : 0;
			i = osd_choose("Frame Lock", choices, current);
			if (i >= 0) {
				save_setting("vsync", values[i]);
				bmcoco_video_settings_changed();
			}
			break;
		}
		case V_SHADER:
			save_setting("crt_shader", setting_on("crt_shader", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case V_SMOOTH:
			save_setting("smooth", setting_on("smooth", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		default:
			return;
		}
	}
}

/* ---- Keyboard ------------------------------------------------------------- */

static void keyboard_menu(void)
{
	enum { K_TRANSLATE, K_LAYOUT };
	int sel = 0;
	for (;;) {
		struct osd_item items[] = {
			{"Typed Characters", NULL, check(state.kbd_translate), K_TRANSLATE},
			{"Layout", enum_description(hkbd_lang_list, state.hkbd_lang),
			 OSD_ITEM_SUBMENU, K_LAYOUT},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Off: keys are where the CoCo's are", NULL, OSD_ITEM_LABEL, -1},
			{"(best for games).", NULL, OSD_ITEM_LABEL, -1},
			{"On: Shift+2 types \", and so on, as", NULL, OSD_ITEM_LABEL, -1},
			{"printed on your keyboard.", NULL, OSD_ITEM_LABEL, -1},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Esc BREAK, Home CLEAR, Alt ALT,", NULL, OSD_ITEM_LABEL, -1},
			{"Caps Lock/Windows CTRL, F1 F2.", NULL, OSD_ITEM_LABEL, -1},
			{"F10 Reset, F11 Turbo, F12 Menu.", NULL, OSD_ITEM_LABEL, -1},
		};
		int chosen = osd_menu("Keyboard", items, sizeof(items) / sizeof(items[0]), &sel);
		int i;
		switch (chosen) {
		case K_TRANSLATE:
			ui_update_state(-1, ui_tag_kbd_translate, UI_NEXT, NULL);
			save_setting("typed_keys", state.kbd_translate ? "1" : "0");
			break;
		case K_LAYOUT:
			i = choose_enum("Keyboard Layout", hkbd_lang_list, state.hkbd_lang);
			if (i >= 0) {
				ui_update_state(-1, ui_tag_hkbd_lang, hkbd_lang_list[i].value, NULL);
				save_setting("keyboard", hkbd_lang_list[i].name);
			}
			break;
		default:
			return;
		}
	}
}

/* ---- Sound ---------------------------------------------------------------- */

static void sound_menu(void)
{
	enum { S_VOLUME, S_OUTPUT };
	static const char *const outputs[] = {"auto", "hdmi", "analog"};
	static const char *const output_names[] = {"Automatic", "HDMI", "Headphone jack", NULL};
	int sel = 0;
	for (;;) {
		char volume[16];
		const char *out = host_option("audio_out", "auto");
		int out_index = strcmp(out, "hdmi") == 0 ? 1
		                : strcmp(out, "analog") == 0 || strcmp(out, "headphones") == 0 ? 2 : 0;
		struct osd_item items[] = {
			{"Volume", volume, OSD_ITEM_SUBMENU, S_VOLUME},
			{"Output (reboot)", output_names[out_index], OSD_ITEM_SUBMENU, S_OUTPUT},
		};
		int chosen, i;
		snprintf(volume, sizeof(volume), "%d%%", host_option_int("volume", 70));
		chosen = osd_menu("Sound", items, 2, &sel);
		switch (chosen) {
		case S_VOLUME: {
			static const char *const levels[] = {
				"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%",
				"90%", "100%", NULL
			};
			i = osd_choose("Volume", levels, (host_option_int("volume", 70) + 5) / 10);
			if (i >= 0) {
				ui_update_state(-1, ui_tag_gain, i * 10, NULL);
				save_setting_int("volume", i * 10);
			}
			break;
		}
		case S_OUTPUT:
			i = osd_choose("Sound Output", output_names, out_index);
			if (i >= 0 && i != out_index) {
				save_setting("audio_out", outputs[i]);
				reboot_needed = TRUE;
			}
			break;
		default:
			return;
		}
	}
}

/* ---- Network --------------------------------------------------------------- */

static char wifi_ssid[33], wifi_psk[64], wifi_country[3];

static void wpa_path(char *path, size_t size)
{
	const char *root = bmcoco_sd_root();
	size_t len = strlen(root);
	snprintf(path, size, "%s%s%s", root, len > 0 && root[len - 1] == '/' ? "" : "/", WPA_CONFIG);
}

static void copy_quoted(char *dst, size_t size, const char *src)
{
	const char *end;
	if (*src == '"')
		src++;
	end = strchr(src, '"');
	if (end == NULL)
		end = src + strcspn(src, "\r\n");
	if ((size_t) (end - src) >= size)
		end = src + size - 1;
	memcpy(dst, src, end - src);
	dst[end - src] = '\0';
}

static void read_wifi_config(void)
{
	char path[256], line[160];
	FILE *f;
	wifi_ssid[0] = wifi_psk[0] = '\0';
	strcpy(wifi_country, "US");
	wpa_path(path, sizeof(path));
	f = fopen(path, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *p = line + strspn(line, " \t");
		if (strncmp(p, "country=", 8) == 0)
			copy_quoted(wifi_country, sizeof(wifi_country), p + 8);
		else if (strncmp(p, "ssid=", 5) == 0 && wifi_ssid[0] == '\0')
			copy_quoted(wifi_ssid, sizeof(wifi_ssid), p + 5);
		else if (strncmp(p, "psk=", 4) == 0 && wifi_psk[0] == '\0')
			copy_quoted(wifi_psk, sizeof(wifi_psk), p + 4);
	}
	fclose(f);
}

static int write_wifi_config(void)
{
	char path[256];
	FILE *f;
	wpa_path(path, sizeof(path));
	f = fopen(path, "w");
	if (f == NULL)
		return FALSE;
	fprintf(f, "country=%s\n\nnetwork={\n\tssid=\"%s\"\n", wifi_country, wifi_ssid);
	if (wifi_psk[0] != '\0')
		fprintf(f, "\tpsk=\"%s\"\n\tkey_mgmt=WPA-PSK\n", wifi_psk);
	else
		fprintf(f, "\tkey_mgmt=NONE\n");
	fprintf(f, "}\n");
	return fclose(f) == 0;
}

static void edit_wifi(const char *title, char *value, size_t size, int secret)
{
	char text[64];
	snprintf(text, sizeof(text), "%s", value);
	if (osd_edit(title, text, size < sizeof(text) ? size : sizeof(text), secret)) {
		snprintf(value, size, "%s", text);
		if (write_wifi_config())
			reboot_needed = TRUE;
		else
			osd_message("Cannot write " WPA_CONFIG, TRUE);
	}
}

static int compare_signal(const void *a, const void *b)
{
	return ((const struct host_wifi_ap *) b)->signal - ((const struct host_wifi_ap *) a)->signal;
}

/* Returns 1 if a network was chosen, 2 if it also needs a password. */
static int scan_wifi(void)
{
	enum { MAX_APS = 20 };
	static struct host_wifi_ap aps[MAX_APS];
	static char signal[MAX_APS][16];
	struct osd_item items[MAX_APS];
	int n, i, sel = 0, chosen;

	osd_message("Scanning for Wi-Fi networks...", FALSE);
	n = host_wifi_scan(aps, MAX_APS);
	if (n < 0) {
		if (osd_confirm("Wi-Fi is Off", "Switch to Wi-Fi and reboot?")) {
			save_setting("network", "wifi");
			power(0);
		}
		return 0;
	}
	if (n == 0) {
		osd_message("No Wi-Fi networks found.", TRUE);
		return 0;
	}
	qsort(aps, n, sizeof(aps[0]), compare_signal);
	for (i = 0; i < n; i++) {
		snprintf(signal[i], sizeof(signal[i]), "%s%d", aps[i].secure ? "* " : "", aps[i].signal);
		items[i] = (struct osd_item){aps[i].ssid, signal[i], 0, i};
	}
	chosen = osd_menu("Choose a Wi-Fi Network", items, n, &sel);
	if (chosen < 0)
		return 0;
	if (!aps[chosen].secure || strcmp(wifi_ssid, aps[chosen].ssid) != 0)
		wifi_psk[0] = '\0';
	snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", aps[chosen].ssid);
	if (!write_wifi_config()) {
		osd_message("Cannot write " WPA_CONFIG, TRUE);
		return 0;
	}
	save_setting("network", "wifi");
	reboot_needed = TRUE;
	return aps[chosen].secure && wifi_psk[0] == '\0' ? 2 : 1;
}

static void network_menu(void)
{
	enum { N_DEVICE, N_SCAN, N_SSID, N_PASSWORD, N_COUNTRY, N_WEBUI, N_PIN, N_TIMEZONE,
	       N_REBOOT };
	static const char *const devices[] = {"off", "ethernet", "wifi", NULL};
	static const char *const device_names[] = {"Off", "Ethernet", "Wi-Fi", NULL};
	int sel = 2;
	for (;;) {
		char status[64], tz[16];
		const char *net = host_option("network", "off");
		const char *pin = host_option("webui_pin", "");
		int device = strcmp(net, "ethernet") == 0 ? 1 : strcmp(net, "wifi") == 0 ? 2 : 0;
		int minutes = host_option_int("timezone", 0);
		int chosen, i;

		read_wifi_config();
		host_network_status(status, sizeof(status));
		if (minutes == 0)
			strcpy(tz, "UTC");
		else
			snprintf(tz, sizeof(tz), "UTC%+d", minutes / 60);
		{
			struct osd_item items[] = {
				{status, NULL, OSD_ITEM_LABEL, -1},
				{"", NULL, OSD_ITEM_LABEL, -1},
				{"Network", device_names[device], OSD_ITEM_SUBMENU, N_DEVICE},
				{"Scan for Wi-Fi Networks", NULL, OSD_ITEM_SUBMENU, N_SCAN},
				{"Wi-Fi Network", wifi_ssid[0] ? wifi_ssid : "(none)", OSD_ITEM_SUBMENU, N_SSID},
				{"Wi-Fi Password", wifi_psk[0] ? "********" : "(none)", OSD_ITEM_SUBMENU, N_PASSWORD},
				{"Wi-Fi Country", wifi_country, OSD_ITEM_SUBMENU, N_COUNTRY},
				{"Web UI", NULL, check(setting_on("webui", TRUE)), N_WEBUI},
				{"Web UI PIN", pin[0] ? pin : "(none)", OSD_ITEM_SUBMENU, N_PIN},
				{"Time Zone", tz, OSD_ITEM_SUBMENU, N_TIMEZONE},
				{"", NULL, OSD_ITEM_LABEL, -1},
				{"Changes apply after a reboot.", NULL, OSD_ITEM_LABEL, -1},
				{"Reboot Now", NULL, 0, N_REBOOT},
			};
			chosen = osd_menu("Network", items, sizeof(items) / sizeof(items[0]), &sel);
		}
		switch (chosen) {
		case N_DEVICE:
			i = osd_choose("Network", device_names, device);
			if (i >= 0 && i != device) {
				save_setting("network", devices[i]);
				reboot_needed = TRUE;
			}
			break;
		case N_SCAN:
			if (scan_wifi() == 2) {
				wifi_psk[0] = '\0';
				edit_wifi("Wi-Fi Password", wifi_psk, sizeof(wifi_psk), TRUE);
			}
			break;
		case N_SSID:
			edit_wifi("Wi-Fi Network Name", wifi_ssid, sizeof(wifi_ssid), FALSE);
			break;
		case N_PASSWORD:
			wifi_psk[0] = '\0';
			edit_wifi("Wi-Fi Password (blank: none)", wifi_psk, sizeof(wifi_psk), TRUE);
			break;
		case N_COUNTRY:
			edit_wifi("Two Letter Country Code", wifi_country, sizeof(wifi_country), FALSE);
			break;
		case N_WEBUI:
			save_setting("webui", setting_on("webui", TRUE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case N_PIN: {
			char text[9];
			snprintf(text, sizeof(text), "%s", pin);
			if (osd_edit("Web UI PIN (blank: none)", text, sizeof(text), FALSE)) {
				save_setting("webui_pin", text);
				reboot_needed = TRUE;
			}
			break;
		}
		case N_TIMEZONE: {
			static char labels[27][8];
			static const char *zones[28];
			int h, current = 12;
			for (h = -12; h <= 14; h++) {
				if (h == 0)
					strcpy(labels[h + 12], "UTC");
				else
					snprintf(labels[h + 12], sizeof(labels[0]), "UTC%+d", h);
				zones[h + 12] = labels[h + 12];
				if (h * 60 == minutes)
					current = h + 12;
			}
			zones[27] = NULL;
			h = osd_choose("Time Zone", zones, current);
			if (h >= 0 && h != current) {
				save_setting_int("timezone", (h - 12) * 60);
				reboot_needed = TRUE;
			}
			break;
		}
		case N_REBOOT:
			power(0);
			return;
		default:
			return;
		}
	}
}

/* ---- Information ----------------------------------------------------------- */

static void about(void)
{
	char text[640], status[64];
	const char *missing = bmcoco_missing_roms();
	int w, h, load_avg, load_max;
	host_display_size(&w, &h);
	bmcoco_load(&load_avg, &load_max);
	host_network_status(status, sizeof(status));
	snprintf(text, sizeof(text),
	         "BMCoco " BMCOCO_VERSION "\n"
	         "Bare metal Tandy Color Computer\n"
	         "emulator for the Raspberry Pi.\n\n"
	         "Emulation: XRoar " XROAR_VERSION "\n"
	         "by Ciaran Anscomb (GPL 3).\n"
	         "Based on BMA800XL and BMC64.\n\n"
	         "Machine: %s\n"
	         "ROMs: %s\n"
	         "%s\n"
	         "Display: %dx%d %.2f Hz\n"
	         "Machine: %.2f Hz, %s\n"
	         "Load: %d%% of a frame (most %d%%)\n"
	         "Network: %s",
	         machine_name(), missing ? "MISSING" : "OK", missing ? missing : "",
	         w, h, bmcoco_refresh_rate(), bmcoco_machine_rate(),
	         !bmcoco_vsync_locked() ? "timer paced"
	         : bmcoco_overloaded() ? "Pi too slow: timer paced" : "locked to display",
	         load_avg, load_max, status);
	osd_info("About BMCoco", text);
}

static void offer_reboot(void)
{
	if (osd_confirm("Reboot?", "Settings changed that need a reboot. Reboot now?"))
		power(0);
}

/* ---- Main menu ---------------------------------------------------------------- */

void bmcoco_menu(void)
{
	enum {
		M_RUN, M_DISKS, M_HD, M_DW, M_TAPE, M_CART, M_MACHINE, M_MEMORY, M_JOYSTICKS, M_VIDEO, M_KEYBOARD,
		M_SOUND, M_NETWORK, M_TURBO, M_RESET, M_HARD_RESET, M_ABOUT, M_REBOOT, M_OFF
	};
	static int sel = 2;
	char title[48];

	snprintf(title, sizeof(title), "BMCoco %s", BMCOCO_VERSION);
	reboot_needed = FALSE;
	osd_open();
	for (;;) {
		struct osd_item items[] = {
			{machine_name(), NULL, OSD_ITEM_LABEL, -1},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Run a Program", NULL, OSD_ITEM_SUBMENU, M_RUN},
			{"Disk Drives", NULL, OSD_ITEM_SUBMENU, M_DISKS},
			{"Hard Disks", NULL, OSD_ITEM_SUBMENU, M_HD},
			{"DriveWire", NULL, OSD_ITEM_SUBMENU, M_DW},
			{"Cassette", NULL, OSD_ITEM_SUBMENU, M_TAPE},
			{"Cartridge", cart_name(state.cart_id), OSD_ITEM_SUBMENU, M_CART},
			{"Machine", NULL, OSD_ITEM_SUBMENU, M_MACHINE},
			{"Memory", memory_value(), OSD_ITEM_SUBMENU, M_MEMORY},
			{"Joysticks", NULL, OSD_ITEM_SUBMENU, M_JOYSTICKS},
			{"Video", NULL, OSD_ITEM_SUBMENU, M_VIDEO},
			{"Keyboard", NULL, OSD_ITEM_SUBMENU, M_KEYBOARD},
			{"Sound", NULL, OSD_ITEM_SUBMENU, M_SOUND},
			{"Network", NULL, OSD_ITEM_SUBMENU, M_NETWORK},
			{"Turbo (F11)", NULL, check(bmcoco_turbo()), M_TURBO},
			{"Reset (F10)", NULL, 0, M_RESET},
			{"Hard Reset (Shift+F10)", NULL, 0, M_HARD_RESET},
			{"About", NULL, OSD_ITEM_SUBMENU, M_ABOUT},
			{"Reboot", NULL, 0, M_REBOOT},
			{"Shut Down", NULL, 0, M_OFF},
		};
		int chosen = osd_menu(title, items, sizeof(items) / sizeof(items[0]), &sel);
		int leave = FALSE;

		switch (chosen) {
		case M_RUN: leave = run_file(); break;
		case M_DISKS: disks_menu(); break;
		case M_HD: hard_disks_menu(); break;
		case M_DW: drivewire_menu(); break;
		case M_TAPE: tape_menu(); break;
		case M_CART: cart_menu(); break;
		case M_MACHINE: machine_menu(); break;
		case M_MEMORY: memory_menu(); break;
		case M_JOYSTICKS: joysticks_menu(); break;
		case M_VIDEO: video_menu(); break;
		case M_KEYBOARD: keyboard_menu(); break;
		case M_SOUND: sound_menu(); break;
		case M_NETWORK: network_menu(); break;
		case M_TURBO: bmcoco_set_turbo(!bmcoco_turbo()); leave = TRUE; break;
		case M_RESET: xroar_soft_reset(); leave = TRUE; break;
		case M_HARD_RESET: xroar_hard_reset(); leave = TRUE; break;
		case M_ABOUT: about(); break;
		case M_REBOOT:
			if (osd_confirm("Reboot", "Reboot BMCoco?"))
				power(0);
			break;
		case M_OFF:
			if (osd_confirm("Shut Down", "Shut down? It's then safe to switch off."))
				power(1);
			break;
		default:
			leave = TRUE;
			break;
		}
		if (leave || osd_closing())
			break;
	}
	if (reboot_needed && !osd_closing())
		offer_reboot();
	osd_close();
}
