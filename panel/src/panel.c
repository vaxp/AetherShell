/*
 * panel.c — AetherShell Panel
 *
 * Bootstrap:
 *   1. gtk_init
 *   2. CSS provider (base + user, with live-reload)
 *   3. Plugin engine init
 *   4. Register all built-in plugins
 *   5. (Optional) scan external plugin directory for .so files
 *   6. Create the Layer-Shell window
 *   7. Build the pill-based layout from panel.json
 *   8. gtk_main loop
 */

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <glib/gstdio.h>
#include <time.h>

#include "plugin_engine.h"
#include "builtin_plugins.h"
#include "layout_builder.h"
#include "css_provider.h"
#include "resource_paths.h"
#include "window_backend.h"
#include "compositor_backend.h"

/* ── Constants ─────────────────────────────────────────────────────────────── */

/* Default panel height (JSON can override this at startup) */
#define DEFAULT_PANEL_HEIGHT  36

/* External plugin directory — scanned for *.so at startup */
#define EXTERNAL_PLUGIN_DIR   "config/aether/plugins"

/* Installed system-wide panel layout JSON */
#define DEFAULT_LAYOUT_JSON       "/usr/local/share/panel/panel.json"

/* Relative path from binary dir used during development (./config/panel.json) */
#define DEV_LAYOUT_JSON_RELPATH   "config/panel.json"

/* ── Default panel.json written when no config is found ───────────────── */
static const char *PANEL_DEFAULT_JSON =
    "{\n"
    "  \"_comment\": \"AetherShell Panel — auto-generated default layout.\",\n"
    "  \"panel\": {\n"
    "    \"height\"  : 36,\n"
    "    \"spacing\" : 8,\n"
    "    \"margin\"  : { \"top\": 4, \"left\": 8, \"right\": 8 }\n"
    "  },\n"
    "  \"layout\": {\n"
    "    \"left\": [\n"
    "      { \"type\": \"pill\", \"id\": \"left-pill\", \"spacing\": 2,\n"
    "        \"plugins\": [\"aether-appmenu\", \"aether-clipboard\", \"aether-workspaces\"] }\n"
    "    ],\n"
    "    \"center\": [\n"
    "      { \"type\": \"pill\", \"id\": \"center-pill\", \"spacing\": 0,\n"
    "        \"plugins\": [\"aether-clock\"] }\n"
    "    ],\n"
    "    \"right\": [\n"
    "      { \"type\": \"pill\", \"id\": \"sni-pill\", \"spacing\": 2,\n"
    "        \"plugins\": [\"aether-sni-tray\"] },\n"
    "      { \"type\": \"pill\", \"id\": \"status-pill\", \"spacing\": 2,\n"
    "        \"plugins\": [\"aether-keyboard\", \"aether-bt\", \"aether-wifi\", \"aether-mic\", \"aether-volume\"] },\n"
    "      { \"type\": \"pill\", \"id\": \"sys-pill\", \"spacing\": 2,\n"
    "        \"plugins\": [\"aether-battery\", \"aether-search\", \"aether-notifs\", \"aether-cc\"] }\n"
    "    ]\n"
    "  }\n"
    "}\n";

/* ── Globals ───────────────────────────────────────────────────────────────── */

static char    *g_executable_path   = NULL;
static guint    g_recovery_source   = 0;
static int      g_panel_height      = DEFAULT_PANEL_HEIGHT;

/* ── Layout live-reload state ───────────────────────────────────────────────── */

static char          *g_layout_json_path = NULL; /* monitored file path        */
static GtkWidget     *g_panel_bar        = NULL; /* outer bar — always alive   */
static GtkWidget     *g_content          = NULL; /* current layout content     */
static GFileMonitor  *g_layout_monitor   = NULL; /* GFileMonitor for panel.json */
static guint          g_reload_idle      = 0;    /* pending idle source id     */

/* ── Monitor helpers ───────────────────────────────────────────────────────── */

static gboolean has_available_monitor(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return FALSE;
    if (gdk_display_get_primary_monitor(display)) return TRUE;
    return gdk_display_get_n_monitors(display) > 0;
}

static gboolean get_primary_monitor_geometry(GdkScreen    *screen,
                                             GdkRectangle *geom)
{
    if (!screen || !geom) return FALSE;
    GdkDisplay *display = gdk_screen_get_display(screen);
    if (!display) return FALSE;

    GdkMonitor *mon = gdk_display_get_primary_monitor(display);
    if (!mon && gdk_display_get_n_monitors(display) > 0)
        mon = gdk_display_get_monitor(display, 0);
    if (!mon) return FALSE;

    gdk_monitor_get_geometry(mon, geom);
    return TRUE;
}

static gboolean get_widget_monitor_geometry(GtkWidget    *widget,
                                            GdkRectangle *geom)
{
    if (!widget || !geom) return FALSE;
    GdkDisplay *display = gtk_widget_get_display(widget);
    if (!display) return FALSE;

    GdkMonitor *mon = NULL;
    GdkWindow  *win = gtk_widget_get_window(widget);
    if (win) mon = gdk_display_get_monitor_at_window(display, win);
    if (!mon) mon = gdk_display_get_primary_monitor(display);
    if (!mon && gdk_display_get_n_monitors(display) > 0)
        mon = gdk_display_get_monitor(display, 0);
    if (!mon) return FALSE;

    gdk_monitor_get_geometry(mon, geom);
    return TRUE;
}

/* ── Recovery / restart ────────────────────────────────────────────────────── */

static gboolean restart_panel(void)
{
    if (!g_executable_path || g_executable_path[0] == '\0') {
        g_warning("[Panel] Cannot restart: executable path unavailable");
        return FALSE;
    }
    GError *err  = NULL;
    gchar  *argv[] = { g_executable_path, NULL };
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &err)) {
        g_warning("[Panel] Restart failed: %s", err ? err->message : "?");
        if (err) g_error_free(err);
        return FALSE;
    }
    return TRUE;
}

static gboolean try_recover(gpointer data)
{
    (void)data;
    if (!has_available_monitor()) return G_SOURCE_CONTINUE;
    if (restart_panel()) {
        g_recovery_source = 0;
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void on_panel_window_destroy(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    plugin_engine_shutdown();
    panel_css_provider_shutdown();
    if (g_recovery_source == 0)
        g_recovery_source = g_timeout_add(1000, try_recover, NULL);
}

/* ── Screen-change handler ─────────────────────────────────────────────────── */

static void on_screen_changed(GdkScreen *screen, gpointer user_data)
{
    GtkWidget    *window = GTK_WIDGET(user_data);
    GdkRectangle  geom   = {0};
    (void)screen;

    if (get_widget_monitor_geometry(window, &geom)) {
        gtk_widget_set_size_request(window, geom.width, g_panel_height);
        gtk_window_resize(GTK_WINDOW(window), geom.width, g_panel_height);
    }
}

/* ── Layout live-reload ────────────────────────────────────────────────────── */

static gboolean do_reload_layout(gpointer data)
{
    (void)data;
    g_reload_idle = 0;

    if (!g_panel_bar || !g_layout_json_path) return G_SOURCE_REMOVE;

    g_debug("[Panel] Live-reloading layout from %s", g_layout_json_path);

    /* Step 1: Detach plugin widgets from their parents, adding a g_object_ref
     *          so they survive the container destruction below */
    plugin_engine_recreate_widgets();

    /* Step 2: Destroy the old layout — plugin widgets are orphaned but alive */
    if (g_content) {
        gtk_widget_destroy(g_content);
        g_content = NULL;
    }

    /* Step 3: Build a new layout — layout_builder reuses the live widgets */
    GtkWidget *new_content = layout_builder_build(g_layout_json_path);
    if (!new_content) {
        g_warning("[Panel] Layout reload: builder returned NULL");
        plugin_engine_release_saved_refs();
        return G_SOURCE_REMOVE;
    }

    g_content = new_content;
    gtk_box_pack_start(GTK_BOX(g_panel_bar), g_content, TRUE, TRUE, 0);
    gtk_widget_show_all(g_panel_bar);

    /* Step 4: Drop the extra refs — widgets now have parents again */
    plugin_engine_release_saved_refs();

    return G_SOURCE_REMOVE;
}

static void on_layout_file_changed(GFileMonitor      *mon,
                                   GFile             *file,
                                   GFile             *other,
                                   GFileMonitorEvent  event,
                                   gpointer           user_data)
{
    (void)mon; (void)file; (void)other; (void)user_data;

    /* Only react to actual writes */
    if (event != G_FILE_MONITOR_EVENT_CHANGED &&
        event != G_FILE_MONITOR_EVENT_CREATED)
        return;

    /* Coalesce rapid saves — schedule one idle rebuild */
    if (g_reload_idle == 0)
        g_reload_idle = g_timeout_add(120, do_reload_layout, NULL);
}

static void start_layout_monitor(const char *path)
{
    if (!path) return;

    GFile  *f   = g_file_new_for_path(path);
    GError *err = NULL;
    g_layout_monitor = g_file_monitor_file(f,
                           G_FILE_MONITOR_NONE, NULL, &err);
    g_object_unref(f);

    if (!g_layout_monitor) {
        g_warning("[Panel] Cannot monitor layout file: %s",
                  err ? err->message : "?");
        if (err) g_error_free(err);
        return;
    }

    g_file_monitor_set_rate_limit(g_layout_monitor, 200);
    g_signal_connect(g_layout_monitor, "changed",
                     G_CALLBACK(on_layout_file_changed), NULL);
    g_debug("[Panel] Monitoring layout: %s", path);
}

/* ── Window setup ──────────────────────────────────────────────────────────── */

static GtkWidget *create_panel_window(GdkScreen *screen)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);

    /* Layer-shell anchoring */
    panel_window_backend_init_panel(GTK_WINDOW(window), "con-panel");
    panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,   TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,  TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    panel_window_backend_auto_exclusive_zone_enable(GTK_WINDOW(window));
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP,   0);
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT,  0);
    panel_window_backend_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);

    /* RGBA visual for transparency */
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
    }

    /* Initial size */
    GdkRectangle geom = {0};
    if (get_primary_monitor_geometry(screen, &geom)) {
        gtk_widget_set_size_request(window, geom.width, g_panel_height);
        gtk_window_set_default_size(GTK_WINDOW(window), geom.width, g_panel_height);
    }

    /* Background container */
    GtkWidget *panel_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(panel_bar, "panel-bar");
    gtk_container_add(GTK_CONTAINER(window), panel_bar);

    return window;
}

/* ── Resolve (or generate) panel.json path ────────────────────────────────── */

static char *resolve_layout_json_path(void)
{
    /* 1. User override: ~/.config/aether/panel.json */
    char *user_path = g_build_filename(g_get_user_config_dir(),
                                       "aether", "panel.json", NULL);
    if (g_file_test(user_path, G_FILE_TEST_EXISTS)) {
        g_debug("[Panel] Using user layout: %s", user_path);
        return user_path;
    }
    g_free(user_path);

    /* 2. Installed system path */
    if (g_file_test(DEFAULT_LAYOUT_JSON, G_FILE_TEST_EXISTS)) {
        g_debug("[Panel] Using system layout: %s", DEFAULT_LAYOUT_JSON);
        return g_strdup(DEFAULT_LAYOUT_JSON);
    }

    /* 3. Development path — next to the binary (./config/panel.json) */
    if (g_executable_path) {
        char *bin_dir  = g_path_get_dirname(g_executable_path);
        char *dev_path = g_build_filename(bin_dir, DEV_LAYOUT_JSON_RELPATH, NULL);
        g_free(bin_dir);
        if (g_file_test(dev_path, G_FILE_TEST_EXISTS)) {
            g_debug("[Panel] Using dev layout: %s", dev_path);
            return dev_path;
        }
        g_free(dev_path);
    }

    /* 4. Nothing found — auto-generate at ~/.config/aether/panel.json */
    char *gen_path = g_build_filename(g_get_user_config_dir(),
                                      "aether", "panel.json", NULL);
    char *gen_dir  = g_path_get_dirname(gen_path);
    g_mkdir_with_parents(gen_dir, 0755);
    g_free(gen_dir);

    GError *err = NULL;
    if (g_file_set_contents(gen_path, PANEL_DEFAULT_JSON, -1, &err)) {
        g_message("[Panel] Generated default panel.json at: %s", gen_path);
        return gen_path;
    }

    g_warning("[Panel] Could not generate panel.json: %s",
              err ? err->message : "unknown error");
    if (err) g_error_free(err);
    g_free(gen_path);
    return NULL;
}

/* ── main ──────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
    gtk_init(&argc, &argv);

    panel_window_backend_detect();
    panel_compositor_backend_init();

    /* Stash executable path for crash-recovery restart */
    g_executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (!g_executable_path && argc > 0 && argv[0] && argv[0][0] != '\0')
        g_executable_path = g_strdup(argv[0]);

    /* ── CSS ──────────────────────────────────────────────────────────────── */
    char *base_css = panel_resource_path_in("", "style.css");
    panel_css_provider_init(base_css);
    g_free(base_css);

    /* ── Plugin engine ────────────────────────────────────────────────────── */
    plugin_engine_init();
    builtin_plugins_register_all();

    /* Scan for external .so plugins — check both the dev-relative dir and
     * the canonical user config location (~/.config/aether/plugins/).     */
    plugin_engine_scan_dir(EXTERNAL_PLUGIN_DIR);   /* relative (dev builds) */

    char *user_plugin_dir = g_build_filename(g_get_user_config_dir(),
                                             "aether", "plugins", NULL);
    plugin_engine_scan_dir(user_plugin_dir);
    g_free(user_plugin_dir);

    /* ── Create window ────────────────────────────────────────────────────── */
    GdkScreen  *screen = gdk_screen_get_default();
    GtkWidget  *window = create_panel_window(screen);

    g_signal_connect(screen, "size-changed",     G_CALLBACK(on_screen_changed), window);
    g_signal_connect(screen, "monitors-changed", G_CALLBACK(on_screen_changed), window);
    g_signal_connect(window, "destroy",          G_CALLBACK(on_panel_window_destroy), NULL);

    /* ── Build layout from JSON ───────────────────────────────────────────── */
    g_layout_json_path = resolve_layout_json_path();

    /* Parse height from JSON before we build (so window sizing is correct) */
    if (g_layout_json_path) {
        PanelLayoutConfig cfg;
        if (layout_builder_parse_config(g_layout_json_path, &cfg))
            g_panel_height = cfg.height;
    }

    g_content = g_layout_json_path
        ? layout_builder_build(g_layout_json_path)
        : gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    /* Keep a reference to panel_bar for live-reload */
    g_panel_bar = gtk_bin_get_child(GTK_BIN(window));
    if (g_panel_bar && g_content)
        gtk_box_pack_start(GTK_BOX(g_panel_bar), g_content, TRUE, TRUE, 0);

    /* ── Start monitoring panel.json for live layout reload ──────────────── */
    start_layout_monitor(g_layout_json_path);

    gtk_widget_show_all(window);
    gtk_main();

    /* Cleanup */
    if (g_layout_monitor) {
        g_file_monitor_cancel(g_layout_monitor);
        g_object_unref(g_layout_monitor);
    }
    g_free(g_layout_json_path);
    g_free(g_executable_path);
    return 0;
}
