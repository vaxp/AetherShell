CC = gcc
PKG_DEPS = gtk+-3.0 gio-2.0 libnm dbusmenu-glib-0.4 gtk-layer-shell-0 json-glib-1.0
CFLAGS  = -Wall -Wextra -O2 -Iinclude $(shell pkg-config --cflags $(PKG_DEPS))
LIBS    = $(shell pkg-config --libs $(PKG_DEPS)) -lX11 -lpulse -lpulse-mainloop-glib -lm -ldl -lwayland-client

TARGET_PANEL       = vpanel
TARGET_SETTINGS    = vpanel-settings
TARGET_PLUGIN_HOST = vpanel-plugin-host
SRCDIR  = src
OBJDIR  = obj
INCDIR  = include
PLUGIN_DIR ?= $(HOME)/.config/venom/panel-plugins

# ── Source collections ───────────────────────────────────────────────────────
# All .c files under src/, excluding panel plugins and the plugin-host entry point
ALL_SRC      = $(shell find $(SRCDIR) -name '*.c' \
                   -not -path '$(SRCDIR)/panel-plugins/*' \
                   -not -path '$(SRCDIR)/panel/plugin-host-main.c')

# vpanel: all sources except vpanel-settings.c
PANEL_SRC    = $(filter-out $(SRCDIR)/core/vpanel-settings.c, $(ALL_SRC))
PANEL_OBJ    = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(PANEL_SRC))

# vpanel-settings: single file
SETTINGS_SRC = $(SRCDIR)/core/vpanel-settings.c

# vpanel-plugin-host: its own entry point + minimal shared helpers
HOST_SRC     = $(SRCDIR)/panel/plugin-host-main.c
HOST_OBJ     = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(HOST_SRC))

# ── Targets ──────────────────────────────────────────────────────────────────
all: $(OBJDIR) $(TARGET_PANEL) $(TARGET_SETTINGS) $(TARGET_PLUGIN_HOST)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(TARGET_PANEL): $(PANEL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(TARGET_SETTINGS): $(SETTINGS_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Plugin host: needs GTK + dl only (no layer-shell, no pulse, no nm)
HOST_LIBS = $(shell pkg-config --libs gtk+-3.0 gio-2.0) -ldl -lX11
$(TARGET_PLUGIN_HOST): $(HOST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(HOST_LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Install ───────────────────────────────────────────────────────────────────
install: all
	install -Dm755 $(TARGET_PANEL)       $(DESTDIR)/usr/bin/$(TARGET_PANEL)
	install -Dm755 $(TARGET_SETTINGS)    $(DESTDIR)/usr/bin/$(TARGET_SETTINGS)
	install -Dm755 $(TARGET_PLUGIN_HOST) $(DESTDIR)/usr/lib/vpanel/$(TARGET_PLUGIN_HOST)
	@echo "Installed. Plugin host at $(DESTDIR)/usr/lib/vpanel/$(TARGET_PLUGIN_HOST)"

# ── Panel plugins ─────────────────────────────────────────────────────────────
panel-plugins:
	@mkdir -p $(PLUGIN_DIR)
	@for f in src/panel-plugins/*.c; do \
	    name=$$(basename $$f .c); \
	    echo "Compiling panel plugin: $$name.so"; \
	    $(CC) -shared -fPIC -o $(PLUGIN_DIR)/$$name.so $$f \
	        $(CFLAGS) $(shell pkg-config --libs $(PKG_DEPS)); \
	done
	@echo "Plugins installed to $(PLUGIN_DIR)"

# ── Cleanup ───────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET_PANEL) $(TARGET_SETTINGS) $(TARGET_PLUGIN_HOST)
	rm -rf $(OBJDIR)

.PHONY: all clean install panel-plugins
