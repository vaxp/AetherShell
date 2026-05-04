#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>

#include "panel-geometry.h"
#include "window-backend.h"

static char *panel_config_file(void)
{
    return g_build_filename(g_get_user_config_dir(), "venom", "panel.conf", NULL);
}

PanelEdge panel_geometry_get_config_edge(void)
{
    PanelEdge edge = PANEL_EDGE_TOP;

    char *cfg_file = panel_config_file();
    char *contents = NULL;
    if (!g_file_get_contents(cfg_file, &contents, NULL, NULL)) {
        g_free(cfg_file);
        return edge;
    }
    g_free(cfg_file);

    gboolean in_panel = FALSE;
    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (!line[0] || line[0] == '#') continue;
        if (g_strcmp0(line, "[panel]") == 0) {
            in_panel = TRUE;
            continue;
        }
        if (line[0] == '[') {
            in_panel = FALSE;
            continue;
        }
        if (!in_panel) continue;

        char **kv = g_strsplit(line, "=", 2);
        if (kv[0] && kv[1]) {
            char *k = g_strstrip(kv[0]);
            char *v = g_strstrip(kv[1]);
            if (g_strcmp0(k, "position") == 0) {
                if (g_strcmp0(v, "bottom") == 0) edge = PANEL_EDGE_BOTTOM;
                else if (g_strcmp0(v, "left") == 0) edge = PANEL_EDGE_LEFT;
                else if (g_strcmp0(v, "right") == 0) edge = PANEL_EDGE_RIGHT;
                else edge = PANEL_EDGE_TOP;
                g_strfreev(kv);
                break;
            }
        }
        g_strfreev(kv);
    }

    g_strfreev(lines);
    g_free(contents);
    return edge;
}

GtkOrientation panel_geometry_edge_orientation(PanelEdge edge)
{
    return (edge == PANEL_EDGE_LEFT || edge == PANEL_EDGE_RIGHT) ? GTK_ORIENTATION_VERTICAL
                                                                 : GTK_ORIENTATION_HORIZONTAL;
}

/*
 * update_panel_geometry — X11-only: move/resize the panel window and update
 * EWMH struts so the window manager reserves space for it.
 *
 * On Wayland, gtk-layer-shell handles positioning and exclusive-zone
 * automatically, so this function is a no-op.
 */
static void update_panel_geometry(GtkWindow *window, GdkDisplay *display)
{
    GdkMonitor *monitor;
    GdkRectangle geometry;
    const int thickness = 40;
    PanelEdge edge;
    int win_x, win_y, win_w, win_h;

    /* Nothing to do on Wayland — layer-shell manages everything. */
    if (panel_window_backend_is_wayland()) return;

    monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) return;

    gdk_monitor_get_geometry(monitor, &geometry);

    edge  = panel_geometry_get_config_edge();
    win_x = geometry.x;
    win_y = geometry.y;
    win_w = geometry.width;
    win_h = thickness;

    if (edge == PANEL_EDGE_BOTTOM) {
        win_y = geometry.y + geometry.height - thickness;
    } else if (edge == PANEL_EDGE_LEFT) {
        win_w = thickness;
        win_h = geometry.height;
    } else if (edge == PANEL_EDGE_RIGHT) {
        win_x = geometry.x + geometry.width - thickness;
        win_w = thickness;
        win_h = geometry.height;
    }

    gtk_window_resize(window, win_w, win_h);
    gtk_window_move(window, win_x, win_y);

    /*
     * EWMH struts are now managed by window-backend (via the size-allocate
     * signal connected in panel_window_backend_auto_exclusive_zone_enable).
     * We only set the initial position/size here on X11 so the panel
     * occupies the right area before the first layout pass.
     */
}

static void on_panel_realize(GtkWidget *widget, gpointer data)
{
    (void)data;

    /* On Wayland, layer-shell handles everything at realize time. */
    if (panel_window_backend_is_wayland()) return;

    GdkDisplay *display = gtk_widget_get_display(widget);
    update_panel_geometry(GTK_WINDOW(widget), display);
}

static void on_screen_size_changed(GdkScreen *screen, gpointer user_data)
{
    /* On Wayland, layer-shell repositions automatically. */
    if (panel_window_backend_is_wayland()) return;

    GdkDisplay *display = gdk_screen_get_display(screen);
    update_panel_geometry(GTK_WINDOW(user_data), display);
}

void panel_geometry_apply(GtkWidget *panel_window)
{
    if (!GTK_IS_WINDOW(panel_window)) return;
    if (panel_window_backend_is_wayland()) return;
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    update_panel_geometry(GTK_WINDOW(panel_window), display);
}

void panel_geometry_attach(GtkWidget *panel_window)
{
    if (!panel_window) return;

    g_signal_connect(panel_window, "realize", G_CALLBACK(on_panel_realize), NULL);

    GdkScreen *screen_ev = gtk_widget_get_screen(panel_window);
    g_signal_connect(screen_ev, "size-changed", G_CALLBACK(on_screen_size_changed), panel_window);
}
