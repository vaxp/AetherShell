#ifndef COMPOSITOR_BACKEND_H
#define COMPOSITOR_BACKEND_H

#include <gtk/gtk.h>

/*
 * compositor-backend.h — Abstraction layer for compositor IPC.
 *
 * Provides workspace and keyboard-layout state from three possible backends,
 * chosen automatically at runtime:
 *
 *   1. Aether   — custom Wayland protocol (aether_ipc_manager_v1)
 *   2. Wayfire  — JSON IPC over a Unix socket
 *   3. X11      — EWMH atoms + XKB extension
 *
 * Callers register callbacks and are notified whenever state changes.
 * The abstraction makes the rest of the panel code backend-agnostic.
 */

/* Workspace state snapshot for a single output. */
typedef struct {
    int output_id;
    int x;
    int y;
    int grid_width;
    int grid_height;
} PanelWorkspaceState;

/* Keyboard layout state snapshot. */
typedef struct {
    char layouts[128]; /* newline-separated list of layout names */
    int layout_index;  /* index of the currently active layout   */
} PanelKeyboardState;

typedef void (*PanelWorkspaceStateCallback)(const PanelWorkspaceState *state, gpointer user_data);
typedef void (*PanelKeyboardStateCallback)(const PanelKeyboardState *state, gpointer user_data);

/* Initialise the compositor backend — call once after gtk_init(). */
void panel_compositor_backend_init(void);

/* Return the name of the active backend: "aether", "wayfire", "x11", or "none". */
const char *panel_compositor_backend_name(void);

/* Register a workspace-state callback.
 * The callback is called immediately if state is already available. */
void panel_compositor_backend_set_workspace_callback(PanelWorkspaceStateCallback cb, gpointer user_data);

/* Register a keyboard-state callback.
 * The callback is called immediately if state is already available. */
void panel_compositor_backend_set_keyboard_callback(PanelKeyboardStateCallback cb, gpointer user_data);

/* Request that the compositor switch the workspace on output_id to (x, y).
 * Returns TRUE if the request was sent successfully. */
gboolean panel_compositor_backend_set_workspace(int output_id, int x, int y);

#endif
