/*
 * designer/main.c — AetherShell Panel Designer
 * GTK3 window + WebKit2GTK webview with JS↔C bridge.
 */

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <json-glib/json-glib.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include "config_io.h"

/* panel-plugin-api.h lives one level up */
#include "../panel-plugin-api.h"

#define TITLE   "AetherShell Panel Designer"
#define W       1200
#define H       730

static WebKitWebView *g_wv  = NULL;
static char          *g_uid = NULL;   /* absolute path to designer/ui/ */

/* ── Plugin directory monitors ────────────────────────────────────── */
static GFileMonitor *g_plugin_monitors[2]  = { NULL, NULL };
static guint         g_plugins_refresh_id  = 0;

/* ── Built-in plugin descriptors ─────────────────────────────────────── */
typedef struct { const char *id; const char *name; const char *zone; const char *icon; } BuiltinDesc;
static const BuiltinDesc BUILTINS[] = {
  { "aether-appmenu",   "App Menu",       "left",   "🖥️"  },
  { "aether-clipboard", "Clipboard",      "left",   "📋"  },
  { "aether-workspaces","Workspaces",     "left",   "⬛"  },
  { "aether-clock",     "Clock",          "center", "🕐"  },
  { "aether-sni-tray",  "System Tray",    "right",  "📊"  },
  { "aether-keyboard",  "Keyboard",       "right",  "⌨️"  },
  { "aether-wifi",      "Wi-Fi",          "right",  "📶"  },
  { "aether-bt",        "Bluetooth",      "right",  "🔵"  },
  { "aether-mic",       "Microphone",     "right",  "🎙️"  },
  { "aether-volume",    "Volume",         "right",  "🔊"  },
  { "aether-battery",   "Battery",        "right",  "🔋"  },
  { "aether-search",    "Search",         "right",  "🔍"  },
  { "aether-notifs",    "Notifications",  "right",  "🔔"  },
  { "aether-cc",        "Control Center", "right",  "⚙️"  },
};

/* ── Zone name helper ─────────────────────────────────────────────────── */
static const char *zone_name(AetherPluginZone z)
{
    switch (z) {
        case AETHER_PLUGIN_ZONE_LEFT:   return "left";
        case AETHER_PLUGIN_ZONE_CENTER: return "center";
        case AETHER_PLUGIN_ZONE_RIGHT:  return "right";
        default:                        return "right";
    }
}

/* ── JSON-escape a plain ASCII/UTF-8 string (no surrogate pairs needed) ── */
static void json_append_str(GString *out, const char *s)
{
    if (!s) { g_string_append(out, "null"); return; }
    g_string_append_c(out, '"');
    for (; *s; s++) {
        if      (*s == '"')  g_string_append(out, "\\\"");
        else if (*s == '\\') g_string_append(out, "\\\\");
        else if (*s == '\n') g_string_append(out, "\\n");
        else if (*s == '\r') g_string_append(out, "\\r");
        else if (*s == '\t') g_string_append(out, "\\t");
        else                 g_string_append_c(out, *s);
    }
    g_string_append_c(out, '"');
}

/* ── Append one plugin JSON object to *out ────────────────────────────── */
static void append_plugin_obj(GString    *out,
                               const char *id,
                               const char *name,
                               const char *zone,
                               const char *icon,
                               gboolean    is_first)
{
    if (!is_first) g_string_append_c(out, ',');
    g_string_append(out, "{\"id\":");
    json_append_str(out, id);
    g_string_append(out, ",\"name\":");
    json_append_str(out, name ? name : id);
    g_string_append(out, ",\"zone\":");
    json_append_str(out, zone ? zone : "right");
    g_string_append(out, ",\"icon\":");
    json_append_str(out, icon && icon[0] ? icon : "🔌");
    g_string_append_c(out, '}');
}

/* ── Scan one directory for .so plugins and append descriptors ────────── */
static void scan_external_dir(const char *dir_path,
                               GString    *out,
                               GHashTable *seen,   /* already-added ids */
                               int        *count)  /* running item count */
{
    if (!dir_path) return;

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) return;

    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_suffix(name, ".so")) continue;

        char *full = g_build_filename(dir_path, name, NULL);

        /* Derive plugin id from filename (strip .so) */
        char *id = g_strdup(name);
        char *dot = strchr(id, '.');
        if (dot) *dot = '\0';

        /* Skip if already known (built-in or previous scan dir) */
        if (g_hash_table_contains(seen, id)) {
            g_free(id); g_free(full);
            continue;
        }

        /* Try to load the .so and call aether_panel_plugin_init_v3 */
        void *handle = dlopen(full, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            g_warning("[Designer] dlopen('%s'): %s", full, dlerror());
            g_free(id); g_free(full);
            continue;
        }
        dlerror();
        AetherPanelPluginInitFnV3 fn =
            (AetherPanelPluginInitFnV3)dlsym(handle, "aether_panel_plugin_init_v3");
        const char *dl_err = dlerror();
        if (dl_err || !fn) {
            dlclose(handle);
            g_free(id); g_free(full);
            continue;
        }

        AetherPanelPluginAPIv3 *api = fn();
        if (!api || api->api_version != AETHER_PANEL_PLUGIN_API_VERSION) {
            dlclose(handle);
            g_free(id); g_free(full);
            continue;
        }

        /* Extract metadata */
        const char *pname = api->name && api->name[0] ? api->name : id;
        const char *pzone = zone_name(api->zone);
        const char *picon = api->icon_name && api->icon_name[0] ? api->icon_name : "🔌";

        append_plugin_obj(out, id, pname, pzone, picon, *count == 0);
        (*count)++;

        g_hash_table_add(seen, g_strdup(id));

        dlclose(handle);
        g_free(id);
        g_free(full);
    }
    g_dir_close(dir);
}

/* ── Build the full plugins JSON array (built-ins + externals) ────────── */
static char *build_plugins_json(void)
{
    GString    *out  = g_string_new("[");
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    int         n    = 0;

    /* 1. Built-ins */
    for (guint i = 0; i < G_N_ELEMENTS(BUILTINS); i++) {
        const BuiltinDesc *b = &BUILTINS[i];
        append_plugin_obj(out, b->id, b->name, b->zone, b->icon, n == 0);
        g_hash_table_add(seen, g_strdup(b->id));
        n++;
    }

    /* 2. User plugin dir: ~/.config/vaxp/panel/plugins/ */
    char *user_dir = g_build_filename(g_get_user_config_dir(),
                                       "vaxp", "panel", "plugins", NULL);
    scan_external_dir(user_dir, out, seen, &n);
    g_free(user_dir);

    /* 3. Dev / relative dir next to the designer binary */
    char *exe     = g_file_read_link("/proc/self/exe", NULL);
    char *bin_dir = exe ? g_path_get_dirname(exe) : g_strdup(".");
    g_free(exe);
    char *dev_dir = g_build_filename(bin_dir, "..", "config", "vaxp", "panel", "plugins", NULL);
    g_free(bin_dir);
    scan_external_dir(dev_dir, out, seen, &n);
    g_free(dev_dir);

    g_hash_table_destroy(seen);
    g_string_append_c(out, ']');
    return g_string_free(out, FALSE);
}

/* ── JS helpers ──────────────────────────────────────────────────────── */

static void js(const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    char *s = g_strdup_vprintf(fmt, a);
    va_end(a);
    webkit_web_view_evaluate_javascript(g_wv, s, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(s);
}

/* Escape a raw string for safe embedding in a JS string literal */
static char *js_escape(const char *raw)
{
    if (!raw) return g_strdup("null");
    GString *out = g_string_new(NULL);
    for (const char *c = raw; *c; c++) {
        if      (*c == '\\') g_string_append(out, "\\\\");
        else if (*c == '\'') g_string_append(out, "\\'");
        else if (*c == '\n') g_string_append(out, "\\n");
        else if (*c == '\r') g_string_append(out, "\\r");
        else                 g_string_append_c(out, *c);
    }
    return g_string_free(out, FALSE);
}

/* ── Inject initial data after page load ─────────────────────────────── */

static void inject_data(void)
{
    char *layout      = config_io_read_layout();
    char *state       = config_io_read_designer_state();
    char *plugins_json = build_plugins_json();

    char *layout_esc = js_escape(layout);
    char *state_esc  = js_escape(state);

    js("window._aether.init("
       "  JSON.parse('%s'),"
       "  %s,"
       "  %s"
       ");",
       layout_esc,
       state  ? g_strdup_printf("JSON.parse('%s')", state_esc) : "null",
       plugins_json);

    g_free(plugins_json);
    g_free(layout_esc);
    g_free(state_esc);
    g_free(layout);
    g_free(state);
}

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer _)
{
    (void)wv; (void)_;
    if (ev == WEBKIT_LOAD_FINISHED) inject_data();
}

/* ── Plugin hot-reload ─────────────────────────────────────────────── */

/* Fired by the debounce timer — rebuilds plugin list and sends to JS */
static gboolean do_refresh_plugins(gpointer data)
{
    (void)data;
    g_plugins_refresh_id = 0;
    if (!g_wv) return G_SOURCE_REMOVE;

    char *json = build_plugins_json();
    js("if (window._aether && window._aether.updatePlugins) "
       "window._aether.updatePlugins(%s);", json);
    g_free(json);
    return G_SOURCE_REMOVE;
}

/* GFileMonitor callback — called for every event in the plugin dirs */
static void on_plugin_dir_changed(GFileMonitor      *mon,
                                   GFile             *file,
                                   GFile             *other,
                                   GFileMonitorEvent  event,
                                   gpointer           data)
{
    (void)mon; (void)other; (void)data;

    /* Only react to .so file creation, deletion, or modification */
    char *name = g_file_get_basename(file);
    gboolean is_so = g_str_has_suffix(name, ".so");
    g_free(name);
    if (!is_so) return;

    if (event != G_FILE_MONITOR_EVENT_CREATED  &&
        event != G_FILE_MONITOR_EVENT_DELETED   &&
        event != G_FILE_MONITOR_EVENT_CHANGED) return;

    /* Debounce: coalesce rapid file-system events (e.g. copy-then-chmod) */
    if (g_plugins_refresh_id == 0)
        g_plugins_refresh_id = g_timeout_add(600, do_refresh_plugins, NULL);
}

/* Start watching both plugin directories */
static void start_plugin_monitors(void)
{
    /* ─ 1. User config dir: ~/.config/vaxp/panel/plugins/ ─ */
    char *user_dir = g_build_filename(g_get_user_config_dir(),
                                       "vaxp", "panel", "plugins", NULL);
    g_mkdir_with_parents(user_dir, 0755);   /* ensure dir exists */
    GFile  *f1  = g_file_new_for_path(user_dir);
    GError *err = NULL;
    g_plugin_monitors[0] = g_file_monitor_directory(
        f1, G_FILE_MONITOR_NONE, NULL, &err);
    g_object_unref(f1);
    g_free(user_dir);

    if (g_plugin_monitors[0]) {
        g_file_monitor_set_rate_limit(g_plugin_monitors[0], 300);
        g_signal_connect(g_plugin_monitors[0], "changed",
                         G_CALLBACK(on_plugin_dir_changed), NULL);
        g_debug("[Designer] Monitoring ~/.config/vaxp/panel/plugins/");
    } else {
        if (err) { g_warning("[Designer] Plugin monitor: %s", err->message); g_error_free(err); }
    }

    /* ─ 2. Dev-relative dir (next to binary): ../config/vaxp/panel/plugins/ ─ */
    char *exe     = g_file_read_link("/proc/self/exe", NULL);
    char *bin_dir = exe ? g_path_get_dirname(exe) : g_strdup(".");
    g_free(exe);
    char *dev_dir = g_build_filename(bin_dir, "..", "config", "vaxp", "panel", "plugins", NULL);
    g_free(bin_dir);

    if (g_file_test(dev_dir, G_FILE_TEST_IS_DIR)) {
        GFile *f2 = g_file_new_for_path(dev_dir);
        err = NULL;
        g_plugin_monitors[1] = g_file_monitor_directory(
            f2, G_FILE_MONITOR_NONE, NULL, &err);
        g_object_unref(f2);
        if (g_plugin_monitors[1]) {
            g_file_monitor_set_rate_limit(g_plugin_monitors[1], 300);
            g_signal_connect(g_plugin_monitors[1], "changed",
                             G_CALLBACK(on_plugin_dir_changed), NULL);
            g_debug("[Designer] Monitoring dev plugin dir: %s", dev_dir);
        } else {
            if (err) { g_warning("[Designer] Dev plugin monitor: %s", err->message); g_error_free(err); }
        }
    }
    g_free(dev_dir);
}

/* ── JS → C message handler ──────────────────────────────────────────── */

static void on_message(WebKitUserContentManager *m,
                       WebKitJavascriptResult   *res,
                       gpointer                  _)
{
    (void)m; (void)_;
    JSCValue *v   = webkit_javascript_result_get_js_value(res);
    char     *str = jsc_value_to_string(v);
    if (!str) return;

    GError     *err    = NULL;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, str, -1, &err)) {
        g_warning("[Designer] bad msg: %s", str);
        goto done;
    }

    JsonObject *msg    = json_node_get_object(json_parser_get_root(parser));
    const char *action = json_object_get_string_member(msg, "action");

    if (!action) goto done;

    if (g_strcmp0(action, "save") == 0) {
        const char *layout = json_object_has_member(msg, "layout")
            ? json_object_get_string_member(msg, "layout") : NULL;
        const char *css    = json_object_has_member(msg, "css")
            ? json_object_get_string_member(msg, "css") : NULL;
        const char *state  = json_object_has_member(msg, "state")
            ? json_object_get_string_member(msg, "state") : NULL;

        if (layout) config_io_write_layout(layout);
        if (css)    config_io_write_user_css(css);
        if (state)  config_io_write_designer_state(state);

        js("window._aether.onSaved();");

    } else if (g_strcmp0(action, "apply_css") == 0) {
        const char *css = json_object_has_member(msg, "css")
            ? json_object_get_string_member(msg, "css") : NULL;
        if (css) config_io_write_user_css(css);

    } else if (g_strcmp0(action, "restart_panel") == 0) {
        config_io_restart_panel();
    }

done:
    if (err) g_error_free(err);
    g_object_unref(parser);
    g_free(str);
}

/* ── URI scheme — serve local UI files ───────────────────────────────── */

static void on_uri_request(WebKitURISchemeRequest *req, gpointer _)
{
    (void)_;
    const char *uri  = webkit_uri_scheme_request_get_uri(req);
    /* aether://ui/foo.css  →  <ui_dir>/foo.css */
    const char *rel  = strstr(uri, "://ui/");
    char       *path = rel
        ? g_build_filename(g_uid, rel + 6, NULL)
        : g_build_filename(g_uid, "index.html", NULL);

    char   *buf  = NULL;
    gsize   len  = 0;
    GError *err  = NULL;
    if (!g_file_get_contents(path, &buf, &len, &err)) {
        webkit_uri_scheme_request_finish_error(req, err);
        g_error_free(err);
        g_free(path);
        return;
    }

    const char *mime = "text/html";
    if (g_str_has_suffix(path, ".css")) mime = "text/css";
    if (g_str_has_suffix(path, ".js"))  mime = "application/javascript";

    GInputStream *stream = g_memory_input_stream_new_from_data(buf, (gssize)len, g_free);
    webkit_uri_scheme_request_finish(req, stream, (gint64)len, mime);
    g_object_unref(stream);
    g_free(path);
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    /* Resolve UI directory */
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *dir = exe ? g_path_get_dirname(exe) : g_strdup(".");
    g_free(exe);
    g_uid = g_build_filename(dir, "ui", NULL);
    g_free(dir);

    /* GTK window */
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), TITLE);
    gtk_window_set_default_size(GTK_WINDOW(win), W, H);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Enable RGBA visual for transparency */
    GdkScreen *screen = gtk_widget_get_screen(win);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(win, visual);
    }

    /* Apply custom background color */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window { background-color: rgba(0, 0, 0, 0.300); }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* WebKit context + URI scheme */
    WebKitWebContext *ctx = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(ctx, "aether", on_uri_request, NULL, NULL);

    /* User content manager for JS messages */
    WebKitUserContentManager *ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm, "panelDesigner");
    g_signal_connect(ucm, "script-message-received::panelDesigner",
                     G_CALLBACK(on_message), NULL);

    /* WebView */
    g_wv = WEBKIT_WEB_VIEW(webkit_web_view_new_with_user_content_manager(ucm));

    /* Make WebView background transparent */
    GdkRGBA rgba = {0.0, 0.0, 0.0, 0.0};
    webkit_web_view_set_background_color(g_wv, &rgba);

    /* Settings */
    WebKitSettings *s = webkit_web_view_get_settings(g_wv);
    webkit_settings_set_enable_developer_extras(s, TRUE);
    webkit_settings_set_javascript_can_access_clipboard(s, TRUE);

    g_signal_connect(g_wv, "load-changed", G_CALLBACK(on_load_changed), NULL);

    gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(g_wv));
    gtk_widget_show_all(win);

    /* Load UI via the custom aether:// scheme (message handlers need a trusted origin) */
    webkit_web_view_load_uri(g_wv, "aether://ui/index.html");

    /* Watch plugin directories for hot-reload */
    start_plugin_monitors();

    gtk_main();

    /* Cleanup monitors */
    for (int i = 0; i < 2; i++) {
        if (g_plugin_monitors[i]) {
            g_file_monitor_cancel(g_plugin_monitors[i]);
            g_object_unref(g_plugin_monitors[i]);
            g_plugin_monitors[i] = NULL;
        }
    }
    if (g_plugins_refresh_id) {
        g_source_remove(g_plugins_refresh_id);
        g_plugins_refresh_id = 0;
    }

    g_free(g_uid);
    return 0;
}
