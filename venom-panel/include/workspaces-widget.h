#ifndef WORKSPACES_WIDGET_H
#define WORKSPACES_WIDGET_H

#include <gtk/gtk.h>

/*
 * workspaces-widget.h — Workspace switcher builtin widget.
 *
 * Uses compositor_backend to receive workspace state from:
 *   - Wayfire (Wayland, via IPC socket)
 *   - Aether  (Wayland, via custom protocol)
 *   - X11     (via EWMH _NET_CURRENT_DESKTOP)
 *
 * Register this as a builtin named "workspaces" in panel.conf.
 */

GtkWidget *create_workspaces_widget(void);

#endif /* WORKSPACES_WIDGET_H */
