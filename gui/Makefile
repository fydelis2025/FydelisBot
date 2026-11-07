CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -I/usr/include/x86_64-linux-gnu
PKG_CONFIG = pkg-config

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 glib-2.0 gthread-2.0)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 glib-2.0 gthread-2.0)

all: fydelisbot-gui

fydelisbot-gui: main.c transport.c transport.h
	$(CC) $(CFLAGS) -o $@ main.c transport.c $(GTK_CFLAGS) $(GTK_LIBS)

clean:
	rm -f fydelisbot-gui

