#include "ui/globals.h"

/* Global state variables */
DockPosition current_dock_position = DOCK_POSITION_BOTTOM;
gchar *current_dock_bg_color = NULL;
gchar *current_context_menu_bg_color = NULL;
gchar *current_indicator_color = NULL;
GdkRGBA current_launch_ring_color = {0.0, 0.99, 0.82, 1.0}; /* Default #00fcd2 */
int current_launch_animation = 1;

gboolean is_wayland_session = FALSE;
struct wl_display *wl_display_conn = NULL;
struct wl_seat *wl_seat_obj = NULL;
struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
GHashTable *wayland_toplevels = NULL;
guint wayland_event_source_id = 0;
guint recovery_source_id = 0;
char *dock_executable_path = NULL;

/* UI Globals definitions */
GtkWidget *main_window = NULL;
GtkWidget *box = NULL;
GtkWidget *system_separator = NULL;
GtkWidget *context_menu_window = NULL;
GdkSeat *context_menu_grab_seat = NULL;

/* App tracking globals definitions */
GHashTable *window_groups = NULL;
GList *pinned_apps = NULL;
GHashTable *launching_apps = NULL;
