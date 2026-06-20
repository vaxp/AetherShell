/*
 * plugin_engine.c — AetherShell AetherCore Plugin Engine
 *
 * Manages built-in and external (.so) plugins via the AetherAetherCorePluginAPIv3
 * contract defined in AetherCore-plugin-api.h.
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
static int         g_AetherCore_height    = 36;
static gboolean    g_is_wayland      = TRUE;

/* Override themes set at runtime by Settings UI (plugin_id → AetherPluginTheme*) */
static GHashTable *g_theme_overrides = NULL;

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Forward declaration — implemented in the Theme Engine section below */
static void apply_theme_to_plugin(AetherLoadedPlugin *p);

/* Build a fresh AetherAetherCoreContext for a newly-registered plugin */
static AetherAetherCoreContext make_context(void)
{
    AetherAetherCoreContext ctx = {
        .monitor_index        = g_monitor_index,
        .AetherCore_height         = g_AetherCore_height,
        .is_wayland           = g_is_wayland,
        .request_AetherCore_resize = NULL,   /* wired up by AetherCore.c post-init */
        .show_notification    = NULL,   /* wired up by AetherCore.c post-init */
    };
    return ctx;
}

/* Allocate and populate an AetherLoadedPlugin, call create_widget, and
 * insert into the registry.  Returns the record on success, NULL on error. */
static AetherLoadedPlugin *register_plugin(const char             *id,
                                           AetherAetherCorePluginAPIv3 *api,
                                           void                   *dl_handle)
{
    if (!id || !api) {
        g_warning("[PluginEngine] register_plugin: NULL id or api");
        return NULL;
    }

    /* ABI version check */
    if (api->api_version != AETHER_AetherCore_PLUGIN_API_VERSION) {
        g_warning("[PluginEngine] Plugin '%s' has api_version=%u, expected %u — skipping",
                  id, api->api_version, AETHER_AetherCore_PLUGIN_API_VERSION);
        return NULL;
    }

    /* Struct size check (forward-compat guard) */
    if (api->struct_size < sizeof(AetherAetherCorePluginAPIv3)) {
        g_warning("[PluginEngine] Plugin '%s' struct_size=%zu < %zu — skipping",
                  id, api->struct_size, sizeof(AetherAetherCorePluginAPIv3));
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

    /* Apply theme immediately if the plugin declares one */
    if (api->get_theme)
        apply_theme_to_plugin(p);

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
    g_theme_overrides  = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               g_free,
                                               (GDestroyNotify)g_free);
    g_debug("[PluginEngine] Initialised");
}

static void free_plugin(gpointer data)
{
    AetherLoadedPlugin *p = (AetherLoadedPlugin *)data;
    if (!p) return;

    if (p->widget && p->api && p->api->destroy_widget) {
        p->api->destroy_widget(p->widget);
    }

    if (p->theme_provider) {
        /* Remove from screen before unref so stale rules don't linger */
        GdkScreen *screen = gdk_display_get_default_screen(gdk_display_get_default());
        gtk_style_context_remove_provider_for_screen(
            screen, GTK_STYLE_PROVIDER(p->theme_provider));
        g_object_unref(p->theme_provider);
        p->theme_provider = NULL;
    }

    if (p->dl_handle) {
        dlclose(p->dl_handle);
    }

    g_free(p->window_css_id);
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

    if (g_theme_overrides) {
        g_hash_table_destroy(g_theme_overrides);
        g_theme_overrides = NULL;
    }

    g_debug("[PluginEngine] Shutdown complete");
}

/* ── Registration & Loading ────────────────────────────────────────────────── */

GtkWidget *plugin_engine_register_builtin(const char             *id,
                                          AetherAetherCorePluginAPIv3 *api)
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

    AetherAetherCorePluginInitFnV3 init_fn =
        (AetherAetherCorePluginInitFnV3)dlsym(handle, "aether_AetherCore_plugin_init_v3");

    const char *err = dlerror();
    if (err) {
        g_warning("[PluginEngine] dlsym('%s', aether_AetherCore_plugin_init_v3) failed: %s",
                  path, err);
        dlclose(handle);
        return NULL;
    }

    AetherAetherCorePluginAPIv3 *api = init_fn();
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

void plugin_engine_notify_orientation(const char *plugin_id, GtkOrientation orientation)
{
    if (!plugin_id) return;
    AetherLoadedPlugin *p = plugin_engine_get(plugin_id);
    if (!p || !p->api || !p->api->on_system_event || !p->widget) return;

    p->api->on_system_event(p->widget, AETHER_EVENT_ORIENTATION_CHANGED, GINT_TO_POINTER(orientation));
}

/* =========================================================================
 * Theme Engine — CSS generation + per-plugin scoped providers
 * =========================================================================
 *
 * Design:
 *   Each plugin gets exactly ONE GtkCssProvider stored in p->theme_provider.
 *   The generated CSS uses "#<window_css_id>" as the root selector for every
 *   rule, so it is physically impossible for plugin A's CSS to affect plugin B.
 *
 *   Priority: GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10
 *   (beats the global base CSS and the user override CSS from css_provider.c)
 * ========================================================================= */

/* rgba() string helper — 0.0-1.0 → 0-255 integer channels */
static void append_rgba(GString *s, double r, double g, double b, double a)
{
    g_string_append_printf(s, "rgba(%d,%d,%d,%.3f)",
                           (int)(r * 255 + 0.5),
                           (int)(g * 255 + 0.5),
                           (int)(b * 255 + 0.5),
                           a);
}

/* True if any RGBA component is non-zero (i.e. "set") */
static gboolean colour_set(double r, double g, double b, double a)
{
    return (a > 0.0 || r > 0.0 || g > 0.0 || b > 0.0);
}

/*
 * generate_theme_css:
 * Builds a CSS string scoped entirely under "#<window_css_id>".
 *
 * Background, border and corner-radius are applied to outer_css_id (when set)
 * scoped under window_css_id, e.g.:
 *   "#volume-mixer-window #mixer-outer { background-color: ...; }"
 * When outer_css_id is NULL, rules target window_css_id directly.
 *
 * All other rules (labels, images, sliders…) use window_css_id as scope.
 * The caller owns the returned GString and must g_string_free() it.
 */
static GString *generate_theme_css(const AetherPluginTheme *t,
                                   const char              *plugin_id)
{
    GString    *css = g_string_new(NULL);
    const char *wid = t->window_css_id;

    /* Build the selector for the visible styled container.
     * When outer_css_id is set:  "#window-id #outer-id"
     * Otherwise:                 "#window-id"               */
    gchar *outer_sel = (t->outer_css_id && t->outer_css_id[0])
                       ? g_strdup_printf("#%s #%s", wid, t->outer_css_id)
                       : g_strdup_printf("#%s", wid);

    /* ── Outer container: background ──────────────────────────────────── */
    if (colour_set(t->bg_r, t->bg_g, t->bg_b, t->bg_a)) {
        g_string_append_printf(css, "%s { background-color: ", outer_sel);
        append_rgba(css, t->bg_r, t->bg_g, t->bg_b, t->bg_a);
        g_string_append(css, "; }\n");
    }

    /* ── Outer container: border + corner radius ────────────────────── */
    if (t->border_width > 0 && colour_set(t->border_r, t->border_g,
                                           t->border_b, t->border_a)) {
        g_string_append_printf(css,
            "%s { border: %dpx solid ", outer_sel, t->border_width);
        append_rgba(css, t->border_r, t->border_g, t->border_b, t->border_a);
        g_string_append_printf(css, "; border-radius: %dpx; }\n",
                               t->corner_radius > 0 ? t->corner_radius
                                                     : t->layout.corner_radius);
    } else if (t->corner_radius > 0) {
        g_string_append_printf(css,
            "%s { border-radius: %dpx; }\n", outer_sel, t->corner_radius);
    }

    /* ── Outer container: min-width / layout ────────────────────────── */
    if (t->layout.min_width > 0)
        g_string_append_printf(css,
            "%s { min-width: %dpx; }\n", outer_sel, t->layout.min_width);
    if (t->layout.min_height > 0)
        g_string_append_printf(css,
            "%s { min-height: %dpx; }\n", outer_sel, t->layout.min_height);

    g_free(outer_sel);

    /* ── Primary text / labels ──────────────────────────────────────────── */
    if (colour_set(t->text_r, t->text_g, t->text_b, t->text_a)) {
        g_string_append_printf(css, "#%s label { color: ", wid);
        append_rgba(css, t->text_r, t->text_g, t->text_b, t->text_a);
        g_string_append(css, "; }\n");
    }

    /* ── Secondary text ─────────────────────────────────────────────────── */
    if (colour_set(t->text2_r, t->text2_g, t->text2_b, t->text2_a)) {
        /* Applied via a helper class; individual components can use .muted */
        g_string_append_printf(css, "#%s .muted, #%s .subtitle { color: ",
                               wid, wid);
        append_rgba(css, t->text2_r, t->text2_g, t->text2_b, t->text2_a);
        g_string_append(css, "; }\n");
    }

    /* ── Icon tint ──────────────────────────────────────────────────────── */
    if (colour_set(t->icon_r, t->icon_g, t->icon_b, t->icon_a)) {
        g_string_append_printf(css, "#%s image { color: ", wid);
        append_rgba(css, t->icon_r, t->icon_g, t->icon_b, t->icon_a);
        g_string_append(css, "; }\n");
    }

    /* ── Surface / card background ──────────────────────────────────────── */
    if (colour_set(t->surface_r, t->surface_g, t->surface_b, t->surface_a)) {
        g_string_append_printf(css, "#%s .card { background-color: ", wid);
        append_rgba(css, t->surface_r, t->surface_g, t->surface_b, t->surface_a);
        g_string_append(css, "; }\n");
    }

    /* ── Interactive element (slider trough, toggle bg) ─────────────────── */
    if (colour_set(t->element_r, t->element_g, t->element_b, t->element_a)) {
        g_string_append_printf(css,
            "#%s scale trough { background-color: ", wid);
        append_rgba(css, t->element_r, t->element_g, t->element_b, t->element_a);
        g_string_append(css, "; }\n");

        g_string_append_printf(css,
            "#%s switch { background-color: ", wid);
        append_rgba(css, t->element_r, t->element_g, t->element_b, t->element_a);
        g_string_append(css, "; }\n");
    }

    /* ── Accent (root) colour — active slider, checked toggle, hover ────── */
    if (colour_set(t->root_r, t->root_g, t->root_b, t->root_a)) {
        g_string_append_printf(css,
            "#%s scale highlight { background-color: ", wid);
        append_rgba(css, t->root_r, t->root_g, t->root_b, t->root_a);
        g_string_append(css, "; }\n");

        g_string_append_printf(css,
            "#%s switch:checked { background-color: ", wid);
        append_rgba(css, t->root_r, t->root_g, t->root_b, t->root_a);
        g_string_append(css, "; }\n");

        /* Control Center active buttons override */
        g_string_append_printf(css,
            "#%s .active-cyan { color: ", wid);
        append_rgba(css, t->root_r, t->root_g, t->root_b, t->root_a);
        g_string_append(css, "; background-color: ");
        append_rgba(css, t->root_r, t->root_g, t->root_b, 0.15);
        g_string_append(css, "; border-color: ");
        append_rgba(css, t->root_r, t->root_g, t->root_b, 0.3);
        g_string_append(css, "; box-shadow: 0 0 12px ");
        append_rgba(css, t->root_r, t->root_g, t->root_b, 0.15);
        g_string_append(css, "; }\n");

        g_string_append_printf(css,
            "#%s .active-cyan-text { color: ", wid);
        append_rgba(css, t->root_r, t->root_g, t->root_b, t->root_a);
        g_string_append(css, "; }\n");

        /* AetherCore bar widget hover tint — uses plugin CSS class (.aether-<id>) */
        g_string_append_printf(css,
            ".%s button:hover { background-color: rgba(%d,%d,%d,0.12); }\n",
            plugin_id,
            (int)(t->root_r * 255 + 0.5),
            (int)(t->root_g * 255 + 0.5),
            (int)(t->root_b * 255 + 0.5));
    }

    return css;
}

/*
 * apply_theme_to_plugin:
 * Resolve the effective theme (override > plugin's own > NULL),
 * generate scoped CSS, and install/update the per-plugin GtkCssProvider.
 * Forward-declared so register_plugin() can call it before the public API.
 */
static void apply_theme_to_plugin(AetherLoadedPlugin *p)
{
    if (!p) return;

    /* Resolve effective theme */
    const AetherPluginTheme *theme = NULL;

    /* 1. Runtime override (from Settings UI) */
    if (g_theme_overrides)
        theme = (const AetherPluginTheme *)
                g_hash_table_lookup(g_theme_overrides, p->plugin_id);

    /* 2. Plugin's own declaration */
    if (!theme && p->api && p->api->get_theme)
        theme = p->api->get_theme();

    if (!theme) return;  /* plugin inherits global CSS — nothing to do */

    /* Derive window_css_id if not yet set */
    if (!p->window_css_id) {
        if (theme->window_css_id && theme->window_css_id[0])
            p->window_css_id = g_strdup(theme->window_css_id);
        else
            p->window_css_id = g_strdup_printf("%s-popup", p->plugin_id);
    }

    /* Build scoped CSS */
    GString *css = generate_theme_css(theme, p->plugin_id);

    /* Create or reuse the per-plugin provider */
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen  *screen  = gdk_display_get_default_screen(display);

    if (!p->theme_provider) {
        p->theme_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(p->theme_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);
    }

    GError *err = NULL;
    gtk_css_provider_load_from_data(p->theme_provider,
                                    css->str, (gssize)css->len, &err);
    if (err) {
        g_warning("[PluginEngine] Theme CSS error for '%s': %s",
                  p->plugin_id, err->message);
        g_error_free(err);
    } else {
        g_debug("[PluginEngine] Theme applied: plugin='%s'  window='#%s'",
                p->plugin_id, p->window_css_id);
    }

    g_string_free(css, TRUE);
}

/* ── Public Theme API ──────────────────────────────────────────────────────── */

void plugin_engine_apply_theme(const char *plugin_id)
{
    AetherLoadedPlugin *p = plugin_engine_get(plugin_id);
    if (!p) {
        g_warning("[PluginEngine] apply_theme: unknown plugin '%s'", plugin_id);
        return;
    }
    apply_theme_to_plugin(p);
}

static void apply_theme_foreach_cb(AetherLoadedPlugin *p, gpointer user_data)
{
    (void)user_data;
    if (p && p->api && p->api->get_theme)
        apply_theme_to_plugin(p);
}

void plugin_engine_apply_theme_all(void)
{
    plugin_engine_foreach(apply_theme_foreach_cb, NULL);
}

const AetherPluginTheme *plugin_engine_get_theme(const char *plugin_id)
{
    if (!plugin_id) return NULL;

    /* Check runtime override first */
    if (g_theme_overrides) {
        const AetherPluginTheme *ov =
            (const AetherPluginTheme *)
            g_hash_table_lookup(g_theme_overrides, plugin_id);
        if (ov) return ov;
    }

    AetherLoadedPlugin *p = plugin_engine_get(plugin_id);
    if (!p || !p->api || !p->api->get_theme) return NULL;
    return p->api->get_theme();
}

void plugin_engine_set_theme(const char           *plugin_id,
                             const AetherPluginTheme *theme)
{
    if (!plugin_id || !g_theme_overrides) return;

    if (!theme) {
        /* Remove override → fall back to plugin's own get_theme() */
        g_hash_table_remove(g_theme_overrides, plugin_id);
    } else {
        /* Store a deep copy of the supplied theme */
        AetherPluginTheme *copy = g_new(AetherPluginTheme, 1);
        memcpy(copy, theme, sizeof(AetherPluginTheme));
        /* window_css_id: if the caller set one, keep it; else clear so the
         * engine re-derives it from plugin_id on next apply */
        copy->window_css_id = theme->window_css_id;  /* static string OK */
        g_hash_table_insert(g_theme_overrides, g_strdup(plugin_id), copy);
    }

    /* Re-apply immediately */
    plugin_engine_apply_theme(plugin_id);
}

