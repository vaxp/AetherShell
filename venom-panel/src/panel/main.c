#include <gtk/gtk.h>

#include "venom-panel.h"
#include "panel-builtins.h"
#include "panel-layout.h"
#include "window-backend.h"
#include "compositor-backend.h"

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    /* Detect X11 vs Wayland once before any windows are created */
    panel_window_backend_detect();

    /* Initialise compositor IPC (Aether / Wayfire / X11 EWMH) */
    panel_compositor_backend_init();

    panel_layout_enable_live_reload();

    GtkWidget *panel = create_venom_panel();
    g_signal_connect(panel, "destroy", G_CALLBACK(panel_builtins_cleanup), NULL);
    g_signal_connect(panel, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(panel);
    gtk_main();
    return 0;
}

