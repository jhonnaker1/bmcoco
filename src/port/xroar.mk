#
# xroar.mk - the XRoar sources BMCoco builds.
#
# These come unmodified from the third_party/xroar submodule. The list
# matches what XRoar's own build (src/Makefile.am, portalib/Makefile.am)
# compiles with every machine and cartridge enabled (see config.h), minus
# main_unix.c and the host specific front ends (SDL, GTK, X11, ALSA, ...):
# BMCoco's user interface, video, sound and joysticks are in src/port/.
# becker.c is replaced by src/port/becker.c (see there).
#
# When updating XRoar, check its src/Makefile.am for added or removed files
# (see docs/UPDATING.md).
#

XROAR_PORTALIB_SRCS = \
	c-ctype.c \
	c-strcasecmp.c \
	c-strncasecmp.c \
	delegate.c \
	intfuncs.c \
	pl-estrcpy.c \
	sds.c \
	sdsx.c \
	slist.c \
	xmalloc.c

XROAR_SRCS = \
	ao.c \
	auto_kbd.c \
	ay891x.c \
	blockdev.c \
	breakpoint.c \
	cart.c \
	clock.c \
	colourspace.c \
	crclist.c \
	crc16.c \
	crc32.c \
	debug.c \
	dkbd.c \
	events.c \
	filter.c \
	fs.c \
	hexs19.c \
	hkbd.c \
	hkbd_joystick.c \
	joystick.c \
	keyboard.c \
	logging.c \
	machine.c \
	mc10_cart.c \
	messenger.c \
	module.c \
	mos6551.c \
	ntsc.c \
	null/ui_null.c \
	part.c \
	path.c \
	printer.c \
	ram.c \
	rom.c \
	rombank.c \
	romlist.c \
	screenshot.c \
	serialise.c \
	snapshot.c \
	sndfile_compat.c \
	sound.c \
	symtab.c \
	tape.c \
	tape_cas.c \
	tape_sndfile.c \
	ui.c \
	vdg_pal.c \
	vdg_palette.c \
	vdisk.c \
	vdrive.c \
	vo.c \
	vo_render.c \
	xconfig.c \
	xroar.c \
	dragon/coco.c \
	dragon/immunity.c \
	dragon/deluxecoco.c \
	dragon/dragon.c \
	dragon/dragon32.c \
	dragon/dragon64.c \
	dragon/dragonpro.c \
	coco3.c \
	mc10.c \
	mc6801/mc6801.c \
	mc6809/hd6309.c \
	mc6809/mc6809.c \
	mc6821.c \
	mc6847/font-6847.c \
	mc6847/font-6847t1.c \
	mc6847/mc6847.c \
	mc6883.c \
	samx8.c \
	tcc1014/font-gime.c \
	tcc1014/tcc1014.c \
	deltados.c \
	dragondos.c \
	gmc.c \
	ide.c \
	idecart.c \
	mooh.c \
	mpi.c \
	nx32.c \
	orch90.c \
	rsdos.c \
	sn76489.c \
	spi65.c \
	spi_sdcard.c \
	wd279x.c \
	mcx128.c
