WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)
LIBS=\
	 $(shell pkg-config --cflags --libs wlroots) \
	 $(shell pkg-config --cflags --libs wayland-server) \
	 $(shell pkg-config --cflags --libs xkbcommon)

SOURCES		= $(wildcard *.c)
PROTOCOLS	= xdg-shell wlr-layer-shell-unstable-v1
PROTOCOLS_H	= $(addprefix protocols/,$(addsuffix -protocol.h,$(PROTOCOLS)))
PROTOCOLS_C	= $(addprefix protocols/,$(addsuffix -protocol.c,$(PROTOCOLS)))

protocols/xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

protocols/xdg-shell-protocol.c: protocols/xdg-shell-protocol.h
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@


protocols/wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header protocols/wlr-layer-shell-unstable-v1.xml $@

protocols/wlr-layer-shell-unstable-v1-protocol.c: protocols/wlr-layer-shell-unstable-v1-protocol.h
	$(WAYLAND_SCANNER) private-code protocols/wlr-layer-shell-unstable-v1.xml $@


mwd: $(SOURCES) $(PROTOCOLS_H) $(PROTOCOLS_C)
	$(CC) $(CFLAGS) -g -Werror -I. -I./protocols/ \
		-O0 -ggdb3 \
		-DWLR_USE_UNSTABLE \
		-o $@ $(SOURCES) \
		$(LIBS)

clean:
	rm -f mwd $(PROTOCOLS_H) $(PROTOCOLS_C)

all: mwd

.DEFAULT_GOAL=mwd
.PHONY: clean all

