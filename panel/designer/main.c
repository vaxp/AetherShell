/*
 * designer/main.c — AetherShell Panel Designer
 * GTK3 window + WebKit2GTK webview with JS↔C bridge.
 */

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <json-glib/json-glib.h>
#include <stdarg.h>
#include <string.h>
#include "config_io.h"

#define TITLE   "AetherShell Panel Designer"
#define W       1200
#define H       730

static WebKitWebView *g_wv  = NULL;
static char          *g_uid = NULL;   /* absolute path to designer/ui/ */

/* ── Built-in plugin registry sent to JS ─────────────────────────────── */
static const char PLUGINS_JSON[] =
  "[{\"id\":\"aether-appmenu\",   \"name\":\"App Menu\",       \"zone\":\"left\",   \"icon\":\"🖥️\"},"
   "{\"id\":\"aether-clipboard\", \"name\":\"Clipboard\",      \"zone\":\"left\",   \"icon\":\"📋\"},"
   "{\"id\":\"aether-workspaces\",\"name\":\"Workspaces\",     \"zone\":\"left\",   \"icon\":\"⬛\"},"
   "{\"id\":\"aether-clock\",     \"name\":\"Clock\",          \"zone\":\"center\", \"icon\":\"🕐\"},"
   "{\"id\":\"aether-sni-tray\",  \"name\":\"System Tray\",    \"zone\":\"right\",  \"icon\":\"📊\"},"
   "{\"id\":\"aether-keyboard\",  \"name\":\"Keyboard\",       \"zone\":\"right\",  \"icon\":\"⌨️\"},"
   "{\"id\":\"aether-wifi\",      \"name\":\"Wi-Fi\",          \"zone\":\"right\",  \"icon\":\"📶\"},"
   "{\"id\":\"aether-bt\",        \"name\":\"Bluetooth\",      \"zone\":\"right\",  \"icon\":\"🔵\"},"
   "{\"id\":\"aether-mic\",       \"name\":\"Microphone\",     \"zone\":\"right\",  \"icon\":\"🎙️\"},"
   "{\"id\":\"aether-volume\",    \"name\":\"Volume\",         \"zone\":\"right\",  \"icon\":\"🔊\"},"
   "{\"id\":\"aether-battery\",   \"name\":\"Battery\",        \"zone\":\"right\",  \"icon\":\"🔋\"},"
   "{\"id\":\"aether-search\",    \"name\":\"Search\",         \"zone\":\"right\",  \"icon\":\"🔍\"},"
   "{\"id\":\"aether-notifs\",    \"name\":\"Notifications\",  \"zone\":\"right\",  \"icon\":\"🔔\"},"
   "{\"id\":\"aether-cc\",        \"name\":\"Control Center\", \"zone\":\"right\",  \"icon\":\"⚙️\"}]";

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
    char *layout = config_io_read_layout();
    char *state  = config_io_read_designer_state();

    char *layout_esc = js_escape(layout);
    char *state_esc  = js_escape(state);

    js("window._aether.init("
       "  JSON.parse('%s'),"
       "  %s,"
       "  %s"
       ");",
       layout_esc,
       state  ? g_strdup_printf("JSON.parse('%s')", state_esc) : "null",
       PLUGINS_JSON);

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

    gtk_main();
    g_free(g_uid);
    return 0;
}
