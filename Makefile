CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

MHD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libmicrohttpd 2>/dev/null)
MHD_LIBS := $(shell $(PKG_CONFIG) --libs libmicrohttpd 2>/dev/null)

.PHONY: all clean install

all: blockyd-httpd

blockyd-httpd: src/blockyd-httpd.c
	$(PKG_CONFIG) --exists libmicrohttpd || { echo "missing libmicrohttpd development package"; exit 1; }
	$(CC) $(CFLAGS) $(MHD_CFLAGS) -o $@ $< $(MHD_LIBS)

install: blockyd-httpd
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 blockyd-httpd $(DESTDIR)$(BINDIR)/blockyd-httpd

clean:
	rm -f blockyd-httpd
