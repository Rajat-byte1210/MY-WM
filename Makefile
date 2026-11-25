# mywm — minimal student coding window manager
PREFIX    ?= /usr/local
BINDIR    ?= $(PREFIX)/bin
MANDIR    ?= $(PREFIX)/share/man/man1
XSESSIONDIR ?= $(PREFIX)/share/xsessions

CC        ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS    ?= -O2 -g
CFLAGS    += -std=c11 -Wall -Wextra -Wpedantic

XCB_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags xcb xcb-keysyms 2>/dev/null)
XCB_LIBS   ?= $(shell $(PKG_CONFIG) --libs xcb xcb-keysyms 2>/dev/null)
CPPFLAGS   += $(XCB_CFLAGS)
LDLIBS     += $(XCB_LIBS)

.PHONY: all clean install uninstall test asan

all: mywm mywmctl

mywm: mywm.c config.h
	@if [ -z "$(strip $(XCB_CFLAGS))" ]; then \
		echo "mywm: XCB development files not found." >&2; \
		echo "  sudo apt install build-essential pkg-config libxcb1-dev libxcb-keysyms1-dev x11proto-dev" >&2; \
		exit 1; \
	fi
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ mywm.c $(LDFLAGS) $(LDLIBS)

mywmctl: mywmctl.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ mywmctl.c $(LDFLAGS)

install: all
	install -Dm755 mywm $(DESTDIR)$(BINDIR)/mywm
	install -Dm755 mywmctl $(DESTDIR)$(BINDIR)/mywmctl
	install -Dm644 man/mywm.1 $(DESTDIR)$(MANDIR)/mywm.1
	install -Dm644 examples/mywm.desktop $(DESTDIR)$(XSESSIONDIR)/mywm.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mywm $(DESTDIR)$(BINDIR)/mywmctl
	rm -f $(DESTDIR)$(MANDIR)/mywm.1
	rm -f $(DESTDIR)$(XSESSIONDIR)/mywm.desktop

test: all
	@./tests/smoke.sh

asan:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=address,undefined"

clean:
	rm -f mywm mywmctl
