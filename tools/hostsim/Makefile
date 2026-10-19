#
# Builds the PC test harness for the emulator port (see host_sim.c).
#
#   make -C tools/hostsim
#   tools/hostsim/run_tests.sh
#

ROOT := ../..
XROAR := $(ROOT)/third_party/xroar
include $(ROOT)/src/port/xroar.mk

XROAR_VERSION := $(shell $(ROOT)/tools/xroar_version.sh)

CC ?= cc
# The simulator runs in a directory standing in for the SD card.
CFLAGS = -O2 -g -std=gnu11 -DHAVE_CONFIG_H -I $(ROOT)/src/port -I $(XROAR) -I $(XROAR)/src \
         -I $(XROAR)/portalib -DXROAR_VERSION=\"$(XROAR_VERSION)\" \
         -DROMPATH=\"roms:.\" -DCONFPATH=\".\" \
         -DBMCOCO_HOST_LIBC -Dregcomp=bmcoco_regcomp -Dregexec=bmcoco_regexec \
         -Dregfree=bmcoco_regfree

OUT := build
OBJS := $(addprefix $(OUT)/portalib/,$(XROAR_PORTALIB_SRCS:.c=.o)) \
        $(addprefix $(OUT)/xroar/,$(XROAR_SRCS:.c=.o)) \
        $(OUT)/port/platform.o $(OUT)/port/joystick.o $(OUT)/port/menu.o \
        $(OUT)/port/osd.o $(OUT)/port/becker.o $(OUT)/port/dwserver.o $(OUT)/port/rs232.o $(OUT)/port/netcall.o $(OUT)/port/posix_extra.o \
        $(OUT)/host_sim.o $(OUT)/live.o

all: $(OUT)/bmcoco_sim

$(OUT)/bmcoco_sim: $(OBJS)
	$(CC) -o $@ $(OBJS) -lm

$(OUT)/portalib/%.o: $(XROAR)/portalib/%.c $(ROOT)/src/port/config.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -c -o $@ $<

$(OUT)/xroar/%.o: $(XROAR)/src/%.c $(ROOT)/src/port/config.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -c -o $@ $<

$(OUT)/port/%.o: $(ROOT)/src/port/%.c $(ROOT)/src/port/config.h $(ROOT)/src/port/bmcoco.h \
                 $(ROOT)/src/port/osd.h $(ROOT)/src/host.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra -Wno-unused-parameter -c -o $@ $<

$(OUT)/host_sim.o: host_sim.c live.h $(ROOT)/src/host.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -c -o $@ $<

$(OUT)/live.o: live.c live.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra -DLIVE_HTML=\"$(abspath live.html)\" -c -o $@ $<

clean:
	rm -rf $(OUT)

.PHONY: all clean
