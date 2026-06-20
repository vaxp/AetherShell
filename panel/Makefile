.DEFAULT_GOAL := all
.PHONY: panel designer clean install uninstall dist all

CC      = gcc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/panel
RESOURCES_DIR ?= $(DATADIR)/resources
PLUGINDIR ?= $(PREFIX)/share/aether-panel/plugins

PKGS = gtk+-3.0 gtk-layer-shell-0 libpulse libpulse-mainloop-glib \
       gio-2.0 json-glib-1.0 dbusmenu-glib-0.4 wayland-client

DESIGNER_PKGS = gtk+-3.0 webkit2gtk-4.1 json-glib-1.0 gio-2.0

PKG_CHECK = $(shell pkg-config --exists $(PKGS) && echo yes || echo no)
ifeq ($(PKG_CHECK),no)
$(error Missing build dependencies. Install: libgtk-3-dev libgtk-layer-shell-dev \
libpulse-dev libjson-glib-dev libdbusmenu-glib-dev)
endif

CFLAGS  = -DVAXPWY_PANEL_RESOURCES_DIR=\"$(RESOURCES_DIR)\" \
          $(shell pkg-config --cflags $(PKGS)) \
          -std=gnu11

LIBS    = $(shell pkg-config --libs $(PKGS)) -lX11 -lXext -lm -ldl

# ── Sources ────────────────────────────────────────────────────────────────── #
# Core engine (new files)
ENGINE_SRC = src/plugin_engine.c \
             src/css_provider.c \
             src/layout_builder.c \
             src/builtin_plugins.c

# Existing panel components (unchanged)
COMPONENT_SRC = src/control_center_ui.c src/app_menu.c \
                src/battery_indicator.c src/pulse_volume.c src/power_profile.c \
                src/mpris_control.c src/power_actions.c src/venom_notifications.c \
                src/workspaces.c src/keyboard_layout.c src/sni_tray.c src/resource_paths.c \
                src/network_actions.c src/brightness_control.c src/volume_indicator.c \
                src/mic_indicator.c src/wifi_indicator.c src/sidebar.c src/notifications_ui.c \
                src/bluetooth_manager.c src/bt_indicator.c src/window_backend.c \
                src/compositor_backend.c src/aether-ipc-v1-client-protocol.c \
                src/aether-ipc-unstable-v2-protocol.c \
                src/workspaces_xorg.c src/kb_indicator_xorg.c

SRC      = src/panel.c $(ENGINE_SRC) $(COMPONENT_SRC)
INCLUDES = -Iinclude -I.

# ── Build ──────────────────────────────────────────────────────────────────── #
panel: $(SRC) designer
	$(CC) -o panel $(SRC) $(CFLAGS) $(INCLUDES) $(LIBS)

# ── Designer ───────────────────────────────────────────────────────────────── #
DESIGNER_SRC = designer/main.c designer/config_io.c
DESIGNER_CFLAGS = $(shell pkg-config --cflags $(DESIGNER_PKGS)) -std=gnu11 -Idesigner
DESIGNER_LIBS   = $(shell pkg-config --libs   $(DESIGNER_PKGS)) -ldl

designer/aether-panel-designer: $(DESIGNER_SRC)
	$(CC) -o designer/aether-panel-designer $(DESIGNER_SRC) \
	      $(DESIGNER_CFLAGS) $(DESIGNER_LIBS)

designer: designer/aether-panel-designer

all: panel designer

clean:
	rm -f panel designer/aether-panel-designer

# ── Install ────────────────────────────────────────────────────────────────── #
install: panel designer
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(BINDIR)/ui
	install -d $(DESTDIR)$(RESOURCES_DIR)
	install -d $(DESTDIR)$(RESOURCES_DIR)/images
	install -d $(DESTDIR)$(PLUGINDIR)
	install -m 755 panel $(DESTDIR)$(BINDIR)/panel
	install -m 755 designer/aether-panel-designer $(DESTDIR)$(BINDIR)/aether-panel-designer
	install -m 644 designer/ui/index.html $(DESTDIR)$(BINDIR)/ui/index.html
	install -m 644 designer/ui/style.css $(DESTDIR)$(BINDIR)/ui/style.css
	install -m 644 designer/ui/app.js $(DESTDIR)$(BINDIR)/ui/app.js
	install -m 644 style.css $(DESTDIR)$(RESOURCES_DIR)/style.css
	install -m 644 config/panel.json $(DESTDIR)$(DATADIR)/panel.json
	install -m 644 images/vaxp.png $(DESTDIR)$(RESOURCES_DIR)/images/vaxp.png
	install -m 644 images/control-center-icon.svg $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.svg
	install -m 644 images/control-center-icon.png $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/panel
	rm -f $(DESTDIR)$(BINDIR)/aether-panel-designer
	rm -f $(DESTDIR)$(BINDIR)/ui/index.html
	rm -f $(DESTDIR)$(BINDIR)/ui/style.css
	rm -f $(DESTDIR)$(BINDIR)/ui/app.js
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(BINDIR)/ui
	rm -f $(DESTDIR)$(RESOURCES_DIR)/style.css
	rm -f $(DESTDIR)$(DATADIR)/panel.json
	rm -f $(DESTDIR)$(RESOURCES_DIR)/images/vaxp.png
	rm -f $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.svg
	rm -f $(DESTDIR)$(RESOURCES_DIR)/images/control-center-icon.png
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(RESOURCES_DIR)/images
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(RESOURCES_DIR)
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(PLUGINDIR)
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(DATADIR)

# ── Dist ───────────────────────────────────────────────────────────────────── #
dist:
	mkdir -p build/dist
	tar -czf build/dist/panel.tar.gz \
		Makefile style.css config images include src
