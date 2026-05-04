CC = gcc
PKG_DEPS = gtk+-3.0 gio-2.0 libnm dbusmenu-glib-0.4 gtk-layer-shell-0 json-glib-1.0
CFLAGS = -Wall -Wextra -O2 -Iinclude $(shell pkg-config --cflags $(PKG_DEPS))
LIBS = $(shell pkg-config --libs $(PKG_DEPS)) -lX11 -lpulse -lpulse-mainloop-glib -lm -ldl -lwayland-client

TARGET_PANEL = venom-panel
TARGET_SETTINGS = venom-panel-settings
SRCDIR = src
OBJDIR = obj
INCDIR = include
PLUGIN_DIR ?= $(HOME)/.config/venom/panel-plugins

# Include nested sources (e.g. src/control-center/*.c) but exclude panel plugins
SRC = $(shell find $(SRCDIR) -name '*.c' -not -path '$(SRCDIR)/panel-plugins/*')
# Filter out venom-panel-settings.c because it's built independently
PANEL_SRC = $(filter-out $(SRCDIR)/core/venom-panel-settings.c, $(SRC))
PANEL_OBJ = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(PANEL_SRC))

all: $(OBJDIR) $(TARGET_PANEL) $(TARGET_SETTINGS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(TARGET_PANEL): $(PANEL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(TARGET_SETTINGS): $(SRCDIR)/core/venom-panel-settings.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET_PANEL) $(TARGET_SETTINGS)
	rm -rf $(OBJDIR)

panel-plugins:
	@mkdir -p $(PLUGIN_DIR)
	@for f in src/panel-plugins/*.c; do \
	    name=$$(basename $$f .c); \
	    echo "Compiling panel plugin: $$name.so"; \
	    $(CC) -shared -fPIC -o $(PLUGIN_DIR)/$$name.so $$f \
	        $(CFLAGS) $(shell pkg-config --libs $(PKG_DEPS)); \
	done

.PHONY: all clean panel-plugins
