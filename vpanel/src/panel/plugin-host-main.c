/**
 * plugin-host-main.c — standalone process that loads one vpanel plugin.
 *
 * Usage:
 *   vpanel-plugin-host --socket-id <XID> --plugin <path/to/plugin.so>
 *
 * The process:
 *   1. Parses arguments.
 *   2. Opens the plugin .so via dlopen().
 *   3. Calls the best available init function (v3 > v2 > v1).
 *   4. Creates the plugin widget and embeds it in a GtkPlug connected to
 *      the panel's GtkSocket via the supplied X11 window ID.
 *   5. Reads "key=value\n" config lines from stdin and forwards them to
 *      the plugin's on_config_changed() callback.
 *   6. Exits cleanly when stdin is closed or a fatal error occurs.
 *
 * If the plugin crashes (SIGSEGV etc.) the process terminates and the panel's
 * child-watch detects this, showing a recovery widget and scheduling restart.
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#include <glib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vpanel-plugin-api.h"

/* =========================================================================
 * Globals (this process owns exactly one plugin instance)
 * ========================================================================= */

static GtkWidget                    *g_plug          = NULL;
static GtkWidget                    *g_plugin_widget = NULL;
static const VenomPanelPluginAPIv3  *g_api_v3        = NULL;
static const VenomPanelPluginAPIv2  *g_api_v2        = NULL;

/* =========================================================================
 * stdin config reader
 * Receives "key=value\n" lines from the panel and forwards to the plugin.
 * ========================================================================= */

static gboolean on_stdin_data(GIOChannel   *chan,
                               GIOCondition  cond,
                               gpointer      user_data)
{
    (void)user_data;

    if (cond & (G_IO_ERR | G_IO_HUP)) {
        /* Panel closed the pipe — shut down cleanly */
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    gchar  *line  = NULL;
    gsize   len   = 0;
    GError *err   = NULL;
    GIOStatus st  = g_io_channel_read_line(chan, &line, &len, NULL, &err);

    if (st == G_IO_STATUS_EOF) {
        gtk_main_quit();
        g_free(line);
        return G_SOURCE_REMOVE;
    }

    if (st != G_IO_STATUS_NORMAL || !line) {
        if (err) g_error_free(err);
        g_free(line);
        return G_SOURCE_CONTINUE;
    }

    /* Strip trailing newline */
    g_strchomp(line);

    /* Split on first '=' */
    char *eq = strchr(line, '=');
    if (eq) {
        *eq = '\0';
        const char *key   = line;
        const char *value = eq + 1;

        if (g_api_v3 && g_api_v3->on_config_changed && g_plugin_widget)
            g_api_v3->on_config_changed(g_plugin_widget, key, value);
    }

    g_free(line);
    return G_SOURCE_CONTINUE;
}

/* =========================================================================
 * Plug "embedded" signal — plugin connected to the panel socket
 * ========================================================================= */

static void on_plug_embedded(GtkWidget *plug, gpointer user_data)
{
    (void)plug;
    (void)user_data;
    g_print("[plugin-host] Plugin embedded in panel socket.\n");
}

static void on_plug_delete(GtkWidget *plug, GdkEvent *event, gpointer ud)
{
    (void)plug; (void)event; (void)ud;
    gtk_main_quit();
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    /* ── Parse arguments ─────────────────────────────────────────────── */
    gulong      socket_xid = 0;
    const char *plugin_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--socket-id") == 0 && i + 1 < argc) {
            socket_xid = (gulong)g_ascii_strtoull(argv[++i], NULL, 10);
        } else if (g_strcmp0(argv[i], "--plugin") == 0 && i + 1 < argc) {
            plugin_path = argv[++i];
        }
    }

    if (!socket_xid || !plugin_path) {
        g_printerr("Usage: vpanel-plugin-host "
                   "--socket-id <XID> --plugin <path>\n");
        return 1;
    }

    /* ── Load the plugin .so ─────────────────────────────────────────── */
    void *dl = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
    if (!dl) {
        g_printerr("[plugin-host] dlopen '%s': %s\n", plugin_path, dlerror());
        return 1;
    }

    VenomPanelPluginInitFnV3 fn3 =
        (VenomPanelPluginInitFnV3)dlsym(dl, "venom_panel_plugin_init_v3");
    VenomPanelPluginInitFnV2 fn2 =
        (VenomPanelPluginInitFnV2)dlsym(dl, "venom_panel_plugin_init_v2");
    VenomPanelPluginInitFn   fn1 =
        (VenomPanelPluginInitFn)  dlsym(dl, "venom_panel_plugin_init");

    const VenomPanelPluginAPIv3 *api3 = NULL;
    const VenomPanelPluginAPIv2 *api2 = NULL;
    const VenomPanelPluginAPI   *api1 = NULL;

    if (fn3) {
        api3 = fn3();
        size_t min_sz =
            offsetof(VenomPanelPluginAPIv3, create_widget) +
            sizeof(((VenomPanelPluginAPIv3*)0)->create_widget);
        if (!api3 || api3->api_version != VENOM_PANEL_PLUGIN_API_VERSION
                  || api3->struct_size < min_sz) {
            g_printerr("[plugin-host] Invalid v3 API in '%s'\n", plugin_path);
            api3 = NULL;
        }
    }
    if (!api3 && fn2) {
        api2 = fn2();
        if (!api2 || api2->api_version != VENOM_PANEL_PLUGIN_API_VERSION_V2)
            api2 = NULL;
    }
    if (!api3 && !api2 && fn1) api1 = fn1();

    GtkWidget* (*create_fn)(void) =
        api3 ? api3->create_widget :
        api2 ? api2->create_widget :
        api1 ? api1->create_widget : NULL;

    if (!create_fn) {
        g_printerr("[plugin-host] No valid init symbol in '%s'\n", plugin_path);
        dlclose(dl);
        return 1;
    }

    g_api_v3 = api3;
    g_api_v2 = api2;

    /* ── Create GtkPlug and embed plugin widget ───────────────────────── */
    g_plug = gtk_plug_new((Window)socket_xid);

    g_signal_connect(g_plug, "embedded",    G_CALLBACK(on_plug_embedded), NULL);
    g_signal_connect(g_plug, "delete-event",G_CALLBACK(on_plug_delete),   NULL);

    g_plugin_widget = create_fn();
    if (!g_plugin_widget) {
        g_printerr("[plugin-host] create_widget() returned NULL\n");
        dlclose(dl);
        return 1;
    }

    gtk_container_add(GTK_CONTAINER(g_plug), g_plugin_widget);
    gtk_widget_show_all(g_plug);

    /* ── Watch stdin for config updates ──────────────────────────────── */
    GIOChannel *stdin_chan = g_io_channel_unix_new(STDIN_FILENO);
    g_io_channel_set_flags(stdin_chan, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_close_on_unref(stdin_chan, TRUE);
    g_io_add_watch(stdin_chan,
                   G_IO_IN | G_IO_ERR | G_IO_HUP,
                   on_stdin_data, NULL);
    g_io_channel_unref(stdin_chan);

    const char *name = api3 ? api3->name :
                       api2 ? api2->name :
                       api1 ? api1->name : plugin_path;
    g_print("[plugin-host] '%s' running (xid=%lu)\n", name, socket_xid);

    gtk_main();

    /* ── Cleanup ─────────────────────────────────────────────────────── */
    if (api3 && api3->destroy_widget && g_plugin_widget)
        api3->destroy_widget(g_plugin_widget);
    else if (api2 && api2->destroy_widget && g_plugin_widget)
        api2->destroy_widget(g_plugin_widget);

    dlclose(dl);
    return 0;
}
