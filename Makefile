#
# Makefile for app_audiofork (Enhanced)
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the COPYING file
# at the top of the source tree.
#
# Builds app_audiofork.so as an Asterisk loadable module. The enhanced
# features (configurable codec/sample rate, reconnection backoff, custom
# headers / Bearer auth, WebSocket subprotocols and JSON metadata) rely only
# on APIs already present in Asterisk core (audiohooks, formats, websocket
# client, string fields) plus the standard C library. No external libraries
# are required at link time; the relevant codec modules (codec_g722,
# codec_ulaw, codec_alaw) must simply be loaded at runtime when those codecs
# are selected.

# Point ASTTOPDIR at your Asterisk source tree. The build then picks up the
# standard Asterisk compiler/linker flags, include paths and module dir from
# makeopts/defs.h, exactly like an in-tree module.
ASTTOPDIR?=$(shell if [ -d ../asterisk ]; then echo ../asterisk; else echo; fi)
-include $(ASTTOPDIR)/makeopts
-include $(ASTTOPDIR)/defs.h

MODULE_NAME:=app_audiofork
MODULE_SO:=$(MODULE_NAME).so
MODULE_O:=$(MODULE_NAME).o

# Install location (defaults to the Asterisk modules directory discovered above)
MODULES_DIR?=$(ASTMODDIR)
ifeq ($(strip $(MODULES_DIR)),)
MODULES_DIR:=$(INSTALL_PREFIX)/usr/lib/asterisk/modules
endif

INSTALL:=install
CC?=gcc

# Prefer the flags exported by Asterisk's makeopts; fall back to sane defaults
# so the module still compiles against an Asterisk source tree.
CFLAGS+=$(ASTCFLAGS) -pipe -fPIC -Wall -Wextra -Wstrict-prototypes \
        -Wmissing-prototypes -Wmissing-declarations -D_REENTRANT -D_GNU_SOURCE \
        -DAST_MODULE_SELF_SYM=__internal_app_audiofork_self

LDOPTS+=$(ASTLDFLAGS)

# No extra libraries: JSON metadata is built with standard C string helpers and
# the websocket/format/audiohook APIs belong to Asterisk core.
LIBS+=

all: $(MODULE_SO)
	@echo " +-------- app_audiofork (Enhanced) Build Complete --------+"
	@echo " + app_audiofork.so has been built successfully.          +"
	@echo " + Install it with:                                       +"
	@echo " +              make install                              +"
	@echo " +--------------------------------------------------------+"

$(MODULE_O): $(MODULE_NAME).c
	$(CC) $(CFLAGS) $(DEBUG) $(OPTIMIZE) -c -o $@ $<

$(MODULE_SO): $(MODULE_O)
	$(CC) $(LDOPTS) -o $@ $< $(LIBS)

clean:
	rm -f $(MODULE_O) $(MODULE_SO)

install: $(MODULE_SO)
	$(INSTALL) -m 755 -d $(DESTDIR)$(MODULES_DIR)
	$(INSTALL) -m 755 $(MODULE_SO) $(DESTDIR)$(MODULES_DIR)
	@echo " +---- app_audiofork (Enhanced) Installation Complete -----+"
	@echo " + Load it from the Asterisk CLI with:                    +"
	@echo " +              module load app_audiofork.so              +"
	@echo " +--------------------------------------------------------+"

uninstall:
	rm -f $(DESTDIR)$(MODULES_DIR)/$(MODULE_SO)

.PHONY: all clean install uninstall
