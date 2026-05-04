#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "venom-panel.h"
#include "panel-geometry.h"
#include "panel-context-menu.h"
#include "panel-layout.h"
#include "window-backend.h"

GtkWidget* create_venom_panel(void) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    /* Transparency */
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
    }

    gtk_window_set_title(GTK_WINDOW(window), "vaxp-panel");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

    /* ── Window backend: handles X11 (DOCK + EWMH struts) or
     *                    Wayland (gtk-layer-shell TOP layer)  ── */
    panel_window_backend_init_panel(GTK_WINDOW(window), "venom-panel");

    /* Anchor the panel to the configured edge + stretch across full width */
    PanelEdge edge = panel_geometry_get_config_edge();

    if (edge == PANEL_EDGE_BOTTOM) {
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,    FALSE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,   TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT,  TRUE);
    } else if (edge == PANEL_EDGE_LEFT) {
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,   TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT,  FALSE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,    TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    } else if (edge == PANEL_EDGE_RIGHT) {
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT,  TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,   FALSE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,    TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    } else {
        /* Default: TOP panel */
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,    TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,   TRUE);
        panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT,  TRUE);
    }

    /* On Wayland: layer-shell auto exclusive zone keeps apps below the panel.
     * On X11: this triggers EWMH strut tracking via size-allocate signal.   */
    panel_window_backend_auto_exclusive_zone_enable(GTK_WINDOW(window));

    /* Zero margins — panel touches the screen edge */
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,    0);
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,   0);
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT,  0);

    /* On X11 only: also set an initial size/position and attach the
     * screen-change listener so the panel relocates on monitor hotplug. */
    panel_geometry_attach(window);

    /* Properties (X11 WM hints — ignored on Wayland but harmless) */
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_set_accept_focus(GTK_WINDOW(window), TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);

    /* Right click menu */
    panel_context_menu_attach(window);

    /* Single container — items placed by config */
    GtkWidget *hbox = gtk_box_new(panel_geometry_edge_orientation(edge), 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);
    gtk_widget_set_margin_start(hbox, 4);
    gtk_widget_set_margin_end(hbox, 4);
    gtk_widget_set_margin_top(hbox, 4);
    gtk_widget_set_margin_bottom(hbox, 4);

    panel_layout_load(hbox);

    /* Main CSS */
    GtkCssProvider *p = gtk_css_provider_new();
    gchar *css_p = g_build_filename(g_get_current_dir(), "style.css", NULL);
    gtk_css_provider_load_from_path(p, css_p, NULL);
    g_free(css_p);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p), 800);
    gtk_style_context_add_class(gtk_widget_get_style_context(window), "venom-panel");

    return window;
}
