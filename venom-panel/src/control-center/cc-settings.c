/*
 * cc-settings.c
 */

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>

#include "cc-settings.h"

void cc_settings_open(GtkButton *button, void *user_data)
{
    (void)button;
    (void)user_data;

    const char *prog = "settings";

    GDesktopAppInfo *app_info = g_desktop_app_info_new("settings.desktop");
    if (app_info) {
        GError *error = NULL;
        if (!g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, &error)) {
            g_warning("[Settings] Failed to launch settings.desktop: %s", error ? error->message : "unknown error");
            g_clear_error(&error);
        }
        g_object_unref(app_info);
        return;
    }

    GError *error = NULL;
    if (!g_spawn_command_line_async(prog, &error)) {
        g_warning("[Settings] Failed to launch '%s': %s", prog, error ? error->message : "unknown error");
        g_clear_error(&error);
    }
}
