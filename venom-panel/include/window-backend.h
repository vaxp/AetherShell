#ifndef WINDOW_BACKEND_H
#define WINDOW_BACKEND_H

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>

/*
 * window-backend.h — Abstraction layer for panel window management.
 *
 * On Wayland this uses gtk-layer-shell to anchor the panel and popups to
 * the correct layer-shell layer.  On X11 it emulates the same behaviour
 * with EWMH struts (panel bar) and override_redirect (popups).
 *
 * All callers should use these helpers instead of touching gtk-layer-shell
 * or X11 atoms directly so that the code compiles and runs on both backends.
 */

/* Detect and cache the display backend (call once before gtk_main). */
void panel_window_backend_detect(void);

/* Returns TRUE when running under a Wayland compositor. */
gboolean panel_window_backend_is_wayland(void);

/* ── Panel bar (DOCK / layer TOP, exclusive zone) ─────────────────────── */

/* Initialise the panel bar window for the current backend.
 * namespace_name is used as the gtk-layer-shell namespace on Wayland. */
void panel_window_backend_init_panel(GtkWindow *window, const char *namespace_name);

/* ── Popup windows (control-center, notification-center, …) ──────────── */

/* Initialise a popup window.
 * On Wayland: layer TOP, no exclusive zone, keyboard_mode as requested.
 * On X11    : override_redirect so the WM never touches position/decoration. */
void panel_window_backend_init_popup(GtkWindow *window,
                                     const char *namespace_name,
                                     GdkWindowTypeHint type_hint,
                                     GtkLayerShellKeyboardMode keyboard_mode);

/* ── Shared geometry helpers (work on both backends) ─────────────────── */

/* Set/unset an anchor edge.
 * On Wayland: delegates to gtk_layer_set_anchor.
 * On X11    : stores the value and schedules a reposition idle. */
void panel_window_backend_set_anchor(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gboolean anchor_to_edge);

/* Set the margin for an edge (pixels).
 * On Wayland: delegates to gtk_layer_set_margin.
 * On X11    : stores the value and schedules a reposition idle. */
void panel_window_backend_set_margin(GtkWindow *window,
                                     GtkLayerShellEdge edge,
                                     gint margin);

/* Enable the auto exclusive zone for the panel bar.
 * On Wayland: delegates to gtk_layer_auto_exclusive_zone_enable.
 * On X11    : tracks panel height and keeps EWMH struts up-to-date. */
void panel_window_backend_auto_exclusive_zone_enable(GtkWindow *window);

/* ── Smart popup anchoring ────────────────────────────────────────────── */

/* Anchor a popup window to the correct edge of the panel.
 *
 * Reads the panel position from panel.conf and applies the matching
 * anchor edge (TOP or BOTTOM) + the supplied horizontal_edge anchor
 * (LEFT or RIGHT) with the given horizontal margin.
 *
 * The vertical margin is always 0 — the compositor's exclusive-zone
 * mechanism already keeps the popup flush with the panel.
 *
 * horizontal_edge : GTK_LAYER_SHELL_EDGE_LEFT or GTK_LAYER_SHELL_EDGE_RIGHT
 * horizontal_margin: pixels from that edge
 *
 * On X11 this is a no-op (gtk_window_move handles positioning). */
void panel_window_backend_anchor_popup_to_panel(GtkWindow *window,
                                                 GtkLayerShellEdge horizontal_edge,
                                                 gint horizontal_margin);

#endif
