#ifndef AETHERDOCK_GLOBALS_H
#define AETHERDOCK_GLOBALS_H

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>
#include <gio/gdesktopappinfo.h>
#include <wayland-client.h>
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

/* Enum for dock position */
typedef enum {
    DOCK_POSITION_BOTTOM,
    DOCK_POSITION_TOP,
    DOCK_POSITION_LEFT,
    DOCK_POSITION_RIGHT
} DockPosition;

/* Wayland toplevel state */
typedef struct {
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    gchar *app_id;
    gchar *title;
    guint32 state_flags;
} WaylandToplevel;

enum {
    WAYLAND_TOPLEVEL_STATE_MAXIMIZED = 1u << 0,
    WAYLAND_TOPLEVEL_STATE_MINIMIZED = 1u << 1,
    WAYLAND_TOPLEVEL_STATE_ACTIVATED = 1u << 2,
};

/* Window Group structure for grouping windows by WM_CLASS */
typedef struct {
    char *wm_class;
    GList *windows;  /* List of WaylandToplevel* on Wayland */
    GdkPixbuf *icon;
    GtkWidget *button;
    int active_index;
    char *desktop_file_path;  /* Path to .desktop file */
    gboolean is_pinned;       /* Whether app is pinned */
} WindowGroup;

/* Global state variables */
extern DockPosition current_dock_position;
extern gchar *current_dock_bg_color;
extern gchar *current_context_menu_bg_color;
extern gchar *current_indicator_color;
extern GdkRGBA current_launch_ring_color;
extern int current_launch_animation;

extern gboolean is_wayland_session;
extern struct wl_display *wl_display_conn;
extern struct wl_seat *wl_seat_obj;
extern struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager;
extern GHashTable *wayland_toplevels;
extern guint wayland_event_source_id;
extern guint recovery_source_id;
extern char *dock_executable_path;

/* UI Globals */
extern GtkWidget *main_window;
extern GtkWidget *box;
extern GtkWidget *system_separator;
extern GtkWidget *context_menu_window;
extern GdkSeat *context_menu_grab_seat;

/* App tracking globals */
extern GHashTable *window_groups; /* wm_class -> WindowGroup */
extern GList *pinned_apps; /* List of pinned wm_class strings */
extern GHashTable *launching_apps; /* desktop file path -> active launch marker */

#endif // AETHERDOCK_GLOBALS_H
