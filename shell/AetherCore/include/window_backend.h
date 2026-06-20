#ifndef WINDOW_BACKEND_H
#define WINDOW_BACKEND_H

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>

void AetherCore_window_backend_detect(void);
gboolean AetherCore_window_backend_is_wayland(void);

void AetherCore_window_backend_init_AetherCore(GtkWindow *window, const char *namespace_name);
void AetherCore_window_backend_init_popup(GtkWindow *window,
                                     const char *namespace_name,
                                     GdkWindowTypeHint type_hint,
                                     GtkLayerShellKeyboardMode keyboard_mode);
void AetherCore_window_backend_set_anchor(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gboolean anchor_to_edge);
void AetherCore_window_backend_set_margin(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gint margin);
void AetherCore_window_backend_auto_exclusive_zone_enable(GtkWindow *window);
void AetherCore_window_backend_align_popup(GtkWindow *popup, GtkWidget *relative_to, gint popup_width);

#endif
