/*
 * desktop.c
 * UI bootstrap and application entry point.
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <malloc.h>
#include "icons.h"
#include "selection.h"

#include "widgets_manager.h"
#include <gtk-layer-shell.h>

static guint recovery_source_id = 0;
static char *desktop_executable_path = NULL;

GtkWidget *main_window = NULL;
GtkWidget *icon_layout = NULL;
int screen_w = 0;
int screen_h = 0;

static gboolean monitor_signal_handlers_connected = FALSE;

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data);
static gboolean try_recover_desktop(gpointer user_data);
static void build_desktop_ui(void);
static gboolean restart_desktop_process(void);

static gboolean is_wayland_session(void) {
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        const char *name = G_OBJECT_TYPE_NAME(display);
        if (name && g_str_has_prefix(name, "GdkWayland")) {
            return TRUE;
        }
    }
    return FALSE;
}

static GdkMonitor *get_target_monitor(GdkDisplay *display) {
    GdkMonitor *monitor;
    if (!display) return NULL;
    monitor = gdk_display_get_primary_monitor(display);
    if (monitor) return monitor;
    if (gdk_display_get_n_monitors(display) > 0)
        return gdk_display_get_monitor(display, 0);
    return NULL;
}

gboolean desktop_has_available_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return FALSE;
    return get_target_monitor(display) != NULL;
}

static void update_desktop_geometry(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor;
    GdkRectangle r;

    if (!display || !main_window || !icon_layout) return;

    monitor = get_target_monitor(display);
    if (!monitor) return;

    gdk_monitor_get_geometry(monitor, &r);
    if (r.width <= 0 || r.height <= 0) return;

    screen_w = r.width;
    screen_h = r.height;

    if (is_wayland_session()) {
        gtk_layer_set_monitor(GTK_WINDOW(main_window), monitor);
    } else {
        gtk_window_move(GTK_WINDOW(main_window), r.x, r.y);
    }
    gtk_window_set_default_size(GTK_WINDOW(main_window), screen_w, screen_h);
    gtk_window_resize(GTK_WINDOW(main_window), screen_w, screen_h);
    gtk_widget_set_size_request(icon_layout, screen_w, screen_h);
    gtk_layout_set_size(GTK_LAYOUT(icon_layout), screen_w, screen_h);
}

static void on_monitors_changed(GdkDisplay *display, gpointer user_data) {
    (void)display;
    (void)user_data;
    update_desktop_geometry();
}

static gboolean refresh_desktop_after_show(gpointer user_data) {
    (void)user_data;
    update_desktop_geometry();
    if (icon_layout) gtk_widget_queue_draw(icon_layout);
    return G_SOURCE_REMOVE;
}

static void on_main_window_realize(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    update_desktop_geometry();
    g_idle_add(refresh_desktop_after_show, NULL);
}

static gboolean on_layout_draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    (void)data;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    return FALSE;
}

void init_main_window(void) {
    GdkVisual *visual;
    GdkScreen *screen;

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "vaxp Pro Desktop");
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(main_window), FALSE);

    if (is_wayland_session()) {
        gtk_layer_init_for_window(GTK_WINDOW(main_window));
        gtk_layer_set_namespace(GTK_WINDOW(main_window), "desktop");
        gtk_layer_set_layer(GTK_WINDOW(main_window), GTK_LAYER_SHELL_LAYER_BOTTOM);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(main_window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(main_window), -1);
    } else {
        gtk_window_set_type_hint(GTK_WINDOW(main_window), GDK_WINDOW_TYPE_HINT_DESKTOP);
        gtk_window_set_keep_below(GTK_WINDOW(main_window), TRUE);
        gtk_window_stick(GTK_WINDOW(main_window));
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(main_window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(main_window), TRUE);
    }

    screen = gtk_widget_get_screen(main_window);
    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(main_window, visual);
        gtk_widget_set_app_paintable(main_window, TRUE);
    }

    icon_layout = gtk_layout_new(NULL, NULL);
    gtk_widget_set_app_paintable(icon_layout, TRUE);
    gtk_widget_add_events(icon_layout,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    if (!monitor_signal_handlers_connected) {
        GdkDisplay *display = gdk_display_get_default();
        if (display) {
            g_signal_connect(display, "monitor-added", G_CALLBACK(on_monitors_changed), NULL);
            g_signal_connect(display, "monitor-removed", G_CALLBACK(on_monitors_changed), NULL);
            monitor_signal_handlers_connected = TRUE;
        }
    }

    g_signal_connect(main_window, "realize", G_CALLBACK(on_main_window_realize), NULL);
    g_signal_connect(icon_layout, "draw", G_CALLBACK(on_layout_draw_bg), NULL);

    gtk_container_add(GTK_CONTAINER(main_window), icon_layout);
    update_desktop_geometry();
}
static gboolean try_recover_desktop(gpointer user_data);
static void build_desktop_ui(void);
static gboolean restart_desktop_process(void);

static void setup_drag_dest(void) {
    GtkTargetEntry targets[] = { { "text/uri-list", 0, 0 } };

    gtk_drag_dest_set(icon_layout, GTK_DEST_DEFAULT_ALL, targets, 1,
                      GDK_ACTION_MOVE | GDK_ACTION_COPY);
    g_signal_connect(icon_layout, "drag-data-received",
                     G_CALLBACK(on_bg_drag_data_received), NULL);
}

static void setup_layout_events(void) {
    g_signal_connect_after(icon_layout, "draw", G_CALLBACK(on_layout_draw_fg), NULL);
    g_signal_connect(icon_layout, "button-press-event", G_CALLBACK(on_bg_button_press), NULL);
    g_signal_connect(icon_layout, "motion-notify-event", G_CALLBACK(on_bg_motion), NULL);
    g_signal_connect(icon_layout, "button-release-event", G_CALLBACK(on_bg_button_release), NULL);
}

static void apply_desktop_css(void) {
    GdkScreen *screen = gtk_widget_get_screen(main_window);
    GtkCssProvider *css = gtk_css_provider_new();

    gtk_css_provider_load_from_data(css,
        "window { background-color: transparent; }"
        "#desktop-item { background: transparent; border-radius: 5px; padding: 8px; transition: all 0.1s; }"
        "#desktop-item:hover { background: rgba(0, 0, 0, 0); }"
        "#desktop-item.selected { background: rgba(52, 152, 219, 0.4); border: 1px solid rgba(52, 152, 219, 0.8); }"
        "label { color: white; text-shadow: 1px 1px 2px black; font-weight: bold; }"
        "window.popup, window.popup decoration, "
        "window.background.popup, window.background.popup decoration {"
        "  margin: 0;"
        "  padding: 0;"
        "  border: none;"
        "  border-radius: 0;"
        "  box-shadow: none;"
        "  background-color: transparent;"
        "  background-image: none;"
        "}"
        "menu#desktop-context-menu, menu.desktop-context-menu {"
        "  background-color: rgba(12, 12, 12, 0.16);"
        "  background-image: none;"
        "  border: none;"
        "  border-radius: 12px;"
        "  box-shadow: none;"
        "}"
        "menu#desktop-context-menu box, menu.desktop-context-menu box {"
        "  margin: 0;"
        "  padding: 0;"
        "}"
        "menu#desktop-context-menu menuitem, menu.desktop-context-menu menuitem {"
        "  margin: 0;"
        "  padding: 0;"
        "  min-height: 0;"
        "  min-width: 0;"
        "  background-color: transparent;"
        "  background-image: none;"
        "  border-radius: 8px;"
        "}"
        "menu#desktop-context-menu menuitem > label, menu.desktop-context-menu menuitem > label {"
        "  margin: 0;"
        "  padding: 6px 10px;"
        "}"
        "menu#desktop-context-menu menuitem:hover, menu.desktop-context-menu menuitem:hover {"
        "  background-color: rgba(255, 255, 255, 0.10);"
        "}"
        "menu#desktop-context-menu separator, menu.desktop-context-menu separator {"
        "  margin: 2px 0;"
        "  padding: 0;"
        "  min-height: 1px;"
        "  background-color: rgba(255, 255, 255, 0.10);"
        "}"
        "menu#desktop-context-menu label, menu.desktop-context-menu label {"
        "  color: rgba(255, 255, 255, 0.96);"
        "  text-shadow: none;"
        "}"
        "window#desktop-blur-dialog, window#desktop-blur-dialog decoration, "
        "window.desktop-blur-dialog, window.desktop-blur-dialog decoration {"
        "  margin: 0;"
        "  padding: 0;"
        "  border: none;"
        "  box-shadow: none;"
        "  background-color: transparent;"
        "  background-image: none;"
        "}"
        "#desktop-blur-dialog-content {"
        "  background-color: rgba(12, 12, 12, 0.16);"
        "  background-image: none;"
        "  border-radius: 12px;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "}"
        "window#desktop-blur-dialog label, window.desktop-blur-dialog label {"
        "  color: rgba(255, 255, 255, 0.96);"
        "  text-shadow: none;"
        "}"
        "window#desktop-blur-dialog entry, window.desktop-blur-dialog entry {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "  background-image: none;"
        "  color: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid rgba(255, 255, 255, 0.10);"
        "  box-shadow: none;"
        "}"
        "window#desktop-blur-dialog button, window.desktop-blur-dialog button {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "  background-image: none;"
        "  color: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid rgba(255, 255, 255, 0.10);"
        "  box-shadow: none;"
        "  border-radius: 8px;"
        "}"
        "window#desktop-blur-dialog button:hover, window.desktop-blur-dialog button:hover {"
        "  background-color: rgba(255, 255, 255, 0.14);"
        "  background-image: none;"
        "}"
        "window#desktop-blur-dialog button:active, window.desktop-blur-dialog button:active {"
        "  background-color: rgba(255, 255, 255, 0.18);"
        "  background-image: none;"
        "}"
        "window#desktop-blur-dialog button label, window.desktop-blur-dialog button label {"
        "  color: rgba(255, 255, 255, 0.96);"
        "  text-shadow: none;"
        "}",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), 800);
}

static void build_desktop_ui(void) {
    if (main_window || icon_layout) return;

    init_main_window();
    setup_drag_dest();
    setup_layout_events();
    apply_desktop_css();

    load_all_widgets(icon_layout);
    refresh_icons();

    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);
    gtk_widget_show_all(main_window);
    malloc_trim(0);
}

static gboolean try_recover_desktop(gpointer user_data) {
    (void)user_data;

    if (!desktop_has_available_monitor()) return G_SOURCE_CONTINUE;

    recovery_source_id = 0;
    if (restart_desktop_process()) {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_REMOVE;
}

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;

    deselect_all();
    is_selecting = FALSE;
    main_window = NULL;
    icon_layout = NULL;

    if (recovery_source_id == 0) {
        recovery_source_id = g_timeout_add(1000, try_recover_desktop, NULL);
    }
}

static gboolean restart_desktop_process(void) {
    GError *error = NULL;
    gchar *argv[] = { desktop_executable_path, NULL };

    if (!desktop_executable_path || desktop_executable_path[0] == '\0') {
        g_warning("[Desktop] Cannot restart: executable path is unavailable");
        return FALSE;
    }

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("[Desktop] Failed to restart desktop: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return FALSE;
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    desktop_executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (!desktop_executable_path && argc > 0 && argv[0] && argv[0][0] != '\0') {
        desktop_executable_path = g_strdup(argv[0]);
    }

    build_desktop_ui();
    gtk_main();
    g_free(desktop_executable_path);
    return 0;
}
