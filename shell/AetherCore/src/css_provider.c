/*
 * css_provider.c — AetherShell AetherCore CSS Provider
 *
 * Loads the base stylesheet shipped with the AetherCore, then layers a user-
 * editable override on top.  A GFileMonitor watches the user file and
 * triggers a hot-reload whenever the file changes — no restart required.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include "css_provider.h"

/* ── State ─────────────────────────────────────────────────────────────────── */

static GtkCssProvider *g_base_provider  = NULL;
static GtkCssProvider *g_user_provider  = NULL;
static GFileMonitor   *g_css_monitor    = NULL;
static char           *g_user_css_path  = NULL;

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Returns the path to ~/.config/aether/AetherCore-user.css (caller must g_free) */
static char *get_user_css_path(void)
{
    return g_build_filename(g_get_user_config_dir(),
                            "aether", "AetherCore-user.css", NULL);
}

/* Ensure ~/.config/aether/ exists and create a stub AetherCore-user.css if absent */
static void ensure_user_css(const char *path)
{
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        const char *stub =
            "/* AetherShell AetherCore — user stylesheet\n"
            " *\n"
            " * Override any AetherCore styles here.  Changes are applied live;\n"
            " * no need to restart the AetherCore.\n"
            " *\n"
            " * Examples:\n"
            " *\n"
            " *   .aether-pill {\n"
            " *       background-color: rgba(0, 0, 0, 0);\n"
            " *       border-radius: 18px;\n"
            " *   }\n"
            " *\n"
            " *   .center-pill {\n"
            " *       background-color: rgba(0, 0, 0, 0);\n"
            " *   }\n"
            " */\n";

        GError *err = NULL;
        if (!g_file_set_contents(path, stub, -1, &err)) {
            g_warning("[CSSProvider] Could not create stub user CSS at '%s': %s",
                      path, err ? err->message : "?");
            if (err) g_error_free(err);
        } else {
            g_debug("[CSSProvider] Created stub user CSS: %s", path);
        }
    }
}

/* Load (or reload) the user CSS provider from disk */
static void load_user_provider(void)
{
    if (!g_user_provider || !g_user_css_path) return;

    GError *err = NULL;
    gtk_css_provider_load_from_path(g_user_provider, g_user_css_path, &err);
    if (err) {
        g_warning("[CSSProvider] User CSS parse error in '%s': %s",
                  g_user_css_path, err->message);
        g_error_free(err);
    } else {
        g_debug("[CSSProvider] User CSS loaded: %s", g_user_css_path);
    }
}

/* GFileMonitor callback — fired when the user stylesheet changes on disk */
static void on_user_css_changed(GFileMonitor      *monitor,
                                GFile             *file,
                                GFile             *other_file,
                                GFileMonitorEvent  event_type,
                                gpointer           user_data)
{
    (void)monitor;
    (void)file;
    (void)other_file;
    (void)user_data;

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED  ||
        event_type == G_FILE_MONITOR_EVENT_CREATED  ||
        event_type == G_FILE_MONITOR_EVENT_RENAMED) {
        g_debug("[CSSProvider] User stylesheet changed — hot-reloading");
        load_user_provider();
    }
}

/* ── Public API ────────────────────────────────────────────────────────────── */

void AetherCore_css_provider_init(const char *base_css_path)
{
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen  *screen  = gdk_display_get_default_screen(display);

    /* ── 1. Base stylesheet ─────────────────────────────────────────────── */
    if (base_css_path) {
        g_base_provider = gtk_css_provider_new();
        GError *err = NULL;
        gtk_css_provider_load_from_path(g_base_provider, base_css_path, &err);
        if (err) {
            g_warning("[CSSProvider] Base CSS parse error in '%s': %s",
                      base_css_path, err->message);
            g_error_free(err);
        }
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(g_base_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_debug("[CSSProvider] Base CSS loaded: %s", base_css_path);
    }

    /* ── 2. User stylesheet ─────────────────────────────────────────────── */
    g_user_css_path = get_user_css_path();
    ensure_user_css(g_user_css_path);

    g_user_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(g_user_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);  /* beats APPLICATION priority */

    load_user_provider();

    /* ── 3. File monitor for live reload ───────────────────────────────── */
    GFile  *user_file = g_file_new_for_path(g_user_css_path);
    GError *mon_err   = NULL;
    g_css_monitor = g_file_monitor_file(user_file,
                                         G_FILE_MONITOR_NONE,
                                         NULL, &mon_err);
    if (g_css_monitor) {
        g_signal_connect(g_css_monitor, "changed",
                         G_CALLBACK(on_user_css_changed), NULL);
        g_debug("[CSSProvider] Watching '%s' for changes", g_user_css_path);
    } else {
        g_warning("[CSSProvider] Could not monitor '%s': %s",
                  g_user_css_path,
                  mon_err ? mon_err->message : "?");
        if (mon_err) g_error_free(mon_err);
    }
    g_object_unref(user_file);
}

void AetherCore_css_provider_reload_user(void)
{
    load_user_provider();
}

void AetherCore_css_provider_shutdown(void)
{
    if (g_css_monitor) {
        g_file_monitor_cancel(g_css_monitor);
        g_object_unref(g_css_monitor);
        g_css_monitor = NULL;
    }

    if (g_base_provider) {
        g_object_unref(g_base_provider);
        g_base_provider = NULL;
    }

    if (g_user_provider) {
        g_object_unref(g_user_provider);
        g_user_provider = NULL;
    }

    g_free(g_user_css_path);
    g_user_css_path = NULL;

    g_debug("[CSSProvider] Shutdown complete");
}
