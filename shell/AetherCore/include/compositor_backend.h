#ifndef COMPOSITOR_BACKEND_H
#define COMPOSITOR_BACKEND_H

#include <gtk/gtk.h>

typedef struct {
    int output_id;
    int x;
    int y;
    int grid_width;
    int grid_height;
} AetherCoreWorkspaceState;

typedef struct {
    char layouts[128];
    int layout_index;
} AetherCoreKeyboardState;

typedef void (*AetherCoreWorkspaceStateCallback)(const AetherCoreWorkspaceState *state, gpointer user_data);
typedef void (*AetherCoreKeyboardStateCallback)(const AetherCoreKeyboardState *state, gpointer user_data);

void AetherCore_compositor_backend_init(void);
const char *AetherCore_compositor_backend_name(void);
void AetherCore_compositor_backend_set_workspace_callback(AetherCoreWorkspaceStateCallback cb, gpointer user_data);
void AetherCore_compositor_backend_set_keyboard_callback(AetherCoreKeyboardStateCallback cb, gpointer user_data);
gboolean AetherCore_compositor_backend_set_workspace(int output_id, int x, int y);

#endif
