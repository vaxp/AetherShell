/*
 * plugin_engine.c — AetherShell Panel Plugin Engine
 *
 * Manages built-in and external (.so) plugins via the AetherPanelPluginAPIv3
 * contract defined in panel-plugin-api.h.
 */

#include <dlfcn.h>
#include <dirent.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "plugin_engine.h"

/* ── Internal State ────────────────────────────────────────────────────────── */

/* GHashTable<char* id  →  AetherLoadedPlugin*> */
static GHashTable *g_plugin_registry = NULL;

/* Monotonically-increasing monitor index counter (multi-monitor future use) */
static int         g_monitor_index   = 0;
static int         g_panel_height    = 36;
static gboolean    g_is_wayland      = TRUE;

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Build a fresh AetherPanelContext for a newly-registered plugin */
static AetherPanelContext make_context(void)
{
    AetherPanelContext ctx = {
        .monitor_index        = g_monitor_index,
        .panel_height         = g_panel_height,
        .is_wayland           = g_is_wayland,
        .request_panel_resize = NULL,   /* wired up by panel.c post-init */
        .show_notification    = NULL,   /* wired up by panel.c post-init */
    };
    return ctx;
}

/* Allocate and populate an AetherLoadedPlugin, call create_widget, and
 * insert into the registry.  Returns the record on success, NULL on error. */
static AetherLoadedPlugin *register_plugin(const char             *id,
                                           AetherPanelPluginAPIv3 *api,
                                           void                   *dl_handle)
{
    if (!id || !api) {
        g_warning("[PluginEngine] register_plugin: NULL id or api");
        return NULL;
    }

    /* ABI version check */
    if (api->api_version != AETHER_PANEL_PLUGIN_API_VERSION) {
        g_warning("[PluginEngine] Plugin '%s' has api_version=%u, expected %u — skipping",
                  id, api->api_version, AETHER_PANEL_PLUGIN_API_VERSION);
        return NULL;
    }

    /* Struct size check (forward-compat guard) */
    if (api->struct_size < sizeof(AetherPanelPluginAPIv3)) {
        g_warning("[PluginEngine] Plugin '%s' struct_size=%zu < %zu — skipping",
                  id, api->struct_size, sizeof(AetherPanelPluginAPIv3));
        return NULL;
    }

    /* Duplicate check */
    if (g_hash_table_contains(g_plugin_registry, id)) {
        g_warning("[PluginEngine] Plugin '%s' is already registered — skipping duplicate", id);
        return NULL;
    }

    if (!api->create_widget) {
        g_warning("[PluginEngine] Plugin '%s' has no create_widget() — skipping", id);
        return NULL;
    }

    AetherLoadedPlugin *p = g_new0(AetherLoadedPlugin, 1);
    p->plugin_id = g_strdup(id);
    p->api       = api;
    p->dl_handle = dl_handle;
    p->ctx       = make_context();

    /* Watchdog: if watchdog_ms > 0, a real implementation would spawn a
     * timeout here and destroy/replace the widget if create_widget() hangs.
     * For now we just honour the contract and call create_widget(). */
    p->widget = api->create_widget(&p->ctx);

    if (!p->widget) {
        g_warning("[PluginEngine] Plugin '%s' create_widget() returned NULL", id);
        g_free(p->plugin_id);
        g_free(p);
        return NULL;
    }

    /* Make sure the widget carries a CSS class equal to the plugin id
     * so authors can style it from CSS (e.g. .aether-battery { … }) */
    GtkStyleContext *sc = gtk_widget_get_style_context(p->widget);
    gtk_style_context_add_class(sc, id);

    g_hash_table_insert(g_plugin_registry, g_strdup(id), p);
    g_debug("[PluginEngine] Registered plugin '%s' (%s v%s)",
            id,
            api->name        ? api->name    : "?",
            api->version     ? api->version : "?");

    return p;
}

/* ── Lifecycle ─────────────────────────────────────────────────────────────── */

void plugin_engine_init(void)
{
    if (g_plugin_registry) {
        g_warning("[PluginEngine] plugin_engine_init() called more than once");
        return;
    }

    g_plugin_registry = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               g_free, NULL);
    g_debug("[PluginEngine] Initialised");
}

static void free_plugin(gpointer data)
{
    AetherLoadedPlugin *p = (AetherLoadedPlugin *)data;
    if (!p) return;

    if (p->widget && p->api && p->api->destroy_widget) {
        p->api->destroy_widget(p->widget);
    }

    if (p->dl_handle) {
        dlclose(p->dl_handle);
    }

    g_free(p->plugin_id);
    g_free(p);
}

void plugin_engine_shutdown(void)
{
    if (!g_plugin_registry) return;

    /* Collect all values and free them */
    GList *values = g_hash_table_get_values(g_plugin_registry);
    for (GList *l = values; l; l = l->next) {
        free_plugin(l->data);
    }
    g_list_free(values);

    g_hash_table_destroy(g_plugin_registry);
    g_plugin_registry = NULL;
    g_debug("[PluginEngine] Shutdown complete");
}

/* ── Registration & Loading ────────────────────────────────────────────────── */

GtkWidget *plugin_engine_register_builtin(const char             *id,
                                          AetherPanelPluginAPIv3 *api)
{
    if (!g_plugin_registry) {
        g_error("[PluginEngine] plugin_engine_init() was not called!");
        return NULL;
    }

    AetherLoadedPlugin *p = register_plugin(id, api, NULL /* no .so */);
    return p ? p->widget : NULL;
}

GtkWidget *plugin_engine_load_so(const char *path)
{
    if (!g_plugin_registry) {
        g_error("[PluginEngine] plugin_engine_init() was not called!");
        return NULL;
    }

    if (!path) return NULL;

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        g_warning("[PluginEngine] dlopen('%s') failed: %s", path, dlerror());
        return NULL;
    }

    /* Clear any stale dlerror */
    dlerror();

    AetherPanelPluginInitFnV3 init_fn =
        (AetherPanelPluginInitFnV3)dlsym(handle, "aether_panel_plugin_init_v3");

    const char *err = dlerror();
    if (err) {
        g_warning("[PluginEngine] dlsym('%s', aether_panel_plugin_init_v3) failed: %s",
                  path, err);
        dlclose(handle);
        return NULL;
    }

    AetherPanelPluginAPIv3 *api = init_fn();
    if (!api) {
        g_warning("[PluginEngine] '%s' init function returned NULL", path);
        dlclose(handle);
        return NULL;
    }

    /* Derive an id from the filename (strip dir and ".so" suffix) */
    gchar *basename = g_path_get_basename(path);
    gchar *id       = g_strdup(basename);
    g_free(basename);

    /* Strip trailing .so (and optionally .so.X.Y.Z) */
    char *dot = strchr(id, '.');
    if (dot) *dot = '\0';

    AetherLoadedPlugin *p = register_plugin(id, api, handle);
    g_free(id);

    if (!p) {
        dlclose(handle);
        return NULL;
    }

    return p->widget;
}

void plugin_engine_scan_dir(const char *dir_path)
{
    if (!dir_path) return;

    GError *err = NULL;
    GDir   *dir = g_dir_open(dir_path, 0, &err);
    if (!dir) {
        g_debug("[PluginEngine] scan_dir('%s'): %s", dir_path,
                err ? err->message : "unknown error");
        if (err) g_error_free(err);
        return;
    }

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_suffix(name, ".so")) continue;

        gchar *full = g_build_filename(dir_path, name, NULL);
        g_debug("[PluginEngine] Loading external plugin: %s", full);
        plugin_engine_load_so(full);
        g_free(full);
    }

    g_dir_close(dir);
}

/* ── Query ─────────────────────────────────────────────────────────────────── */

AetherLoadedPlugin *plugin_engine_get(const char *id)
{
    if (!g_plugin_registry || !id) return NULL;
    return (AetherLoadedPlugin *)g_hash_table_lookup(g_plugin_registry, id);
}

/* ── Recreate all plugin widgets (used on layout hot-reload) ───────────────── */

void plugin_engine_recreate_widgets(void)
{
    if (!g_plugin_registry) return;

    GHashTableIter iter;
    gpointer       key, value;

    g_hash_table_iter_init(&iter, g_plugin_registry);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        AetherLoadedPlugin *p = (AetherLoadedPlugin *)value;
        if (!p || !p->api || !p->api->create_widget) continue;

        /* Detach from any parent so gtk_widget_destroy on the layout container
         * does NOT destroy the plugin widget itself */
        if (p->widget && gtk_widget_get_parent(p->widget)) {
            g_object_ref(p->widget);  /* keep alive */
            gtk_container_remove(
                GTK_CONTAINER(gtk_widget_get_parent(p->widget)), p->widget);
        } else if (p->widget) {
            g_object_ref(p->widget);  /* unparented but keep ref consistent */
        }
    }
}

/* Call this AFTER layout_builder_build() to drop the extra ref added by
 * plugin_engine_recreate_widgets.  By now every widget has a parent again. */
void plugin_engine_release_saved_refs(void)
{
    if (!g_plugin_registry) return;

    GHashTableIter iter;
    gpointer       key, value;

    g_hash_table_iter_init(&iter, g_plugin_registry);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        AetherLoadedPlugin *p = (AetherLoadedPlugin *)value;
        if (p && p->widget)
            g_object_unref(p->widget);
    }
}

GtkWidget *plugin_engine_get_widget(const char *id)
{
    AetherLoadedPlugin *p = plugin_engine_get(id);
    return p ? p->widget : NULL;
}

void plugin_engine_foreach(AetherPluginForeachFunc func, gpointer user_data)
{
    if (!g_plugin_registry || !func) return;

    GHashTableIter iter;
    gpointer       key, value;

    g_hash_table_iter_init(&iter, g_plugin_registry);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        func((AetherLoadedPlugin *)value, user_data);
    }
}

/* ── System Events ─────────────────────────────────────────────────────────── */

static void broadcast_one(AetherLoadedPlugin *p, gpointer user_data)
{
    if (!p || !p->api || !p->api->on_system_event || !p->widget) return;

    /* Pack event + event_data into a small struct to pass through the
     * gpointer user_data slot */
    struct { AetherSystemEvent event; void *data; } *args = user_data;
    p->api->on_system_event(p->widget, args->event, args->data);
}

void plugin_engine_broadcast_event(AetherSystemEvent event, void *event_data)
{
    struct { AetherSystemEvent event; void *data; } args = { event, event_data };
    plugin_engine_foreach(broadcast_one, &args);
}
