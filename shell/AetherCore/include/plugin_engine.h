#pragma once

#include <gtk/gtk.h>
#include "AetherCore-plugin-api.h"

/* =========================================================================
 * AetherShell AetherCore — Plugin Engine
 *
 * Manages both built-in (statically-registered) and external (.so) plugins.
 * Built-in plugins are registered at startup via plugin_engine_register_builtin().
 * External plugins are discovered by scanning a directory with
 * plugin_engine_scan_dir() and loaded on demand with dlopen/dlsym.
 * ========================================================================= */

/**
 * AetherLoadedPlugin — internal record of a loaded / registered plugin.
 * Callers should treat this as opaque and use the accessor helpers below.
 */
typedef struct {
    char                   *plugin_id;   /* unique key, e.g. "aether-battery"   */
    AetherAetherCorePluginAPIv3 *api;         /* pointer to the plugin's API struct  */
    GtkWidget              *widget;      /* widget returned by create_widget()   */
    void                   *dl_handle;  /* dlopen handle; NULL for built-ins    */
    AetherAetherCoreContext      ctx;         /* context passed to create_widget()   */
    GtkCssProvider         *theme_provider; /* per-plugin scoped CSS provider   */
    char                   *window_css_id;  /* derived popup CSS id             */
} AetherLoadedPlugin;

/* ── Lifecycle ─────────────────────────────────────────────────────────────── */

/**
 * plugin_engine_init:
 * Must be called once before any other engine function.
 * Initialises the internal plugin registry.
 */
void plugin_engine_init(void);

/**
 * plugin_engine_shutdown:
 * Calls destroy_widget() on every loaded plugin, then dlclose()s any .so handles.
 * Call before gtk_main_quit().
 */
void plugin_engine_shutdown(void);

/* ── Registration & Loading ────────────────────────────────────────────────── */

/**
 * plugin_engine_register_builtin:
 * Registers a plugin whose API struct is allocated statically inside the AetherCore
 * binary itself (no .so needed).  create_widget() is called immediately to
 * produce the GTK widget.
 *
 * @id  : unique plugin identifier (e.g. "aether-clock").  Copied internally.
 * @api : pointer to a statically-allocated AetherAetherCorePluginAPIv3.
 *
 * Returns the newly-created GtkWidget* on success, NULL on failure.
 */
GtkWidget *plugin_engine_register_builtin(const char             *id,
                                          AetherAetherCorePluginAPIv3 *api);

/**
 * plugin_engine_load_so:
 * Loads an external plugin from a shared library (.so) file.
 * The .so must export the symbol "aether_AetherCore_plugin_init_v3" which returns
 * a pointer to a valid AetherAetherCorePluginAPIv3.
 *
 * @path : absolute path to the .so file.
 *
 * Returns the newly-created GtkWidget* on success, NULL on failure.
 */
GtkWidget *plugin_engine_load_so(const char *path);

/**
 * plugin_engine_scan_dir:
 * Scans a directory for *.so files and calls plugin_engine_load_so() on each.
 * Silently skips files that fail to load (logs a warning).
 *
 * @dir_path : absolute path to the plugin directory.
 */
void plugin_engine_scan_dir(const char *dir_path);

/* ── Query ─────────────────────────────────────────────────────────────────── */

/**
 * plugin_engine_get:
 * Returns a pointer to the internal AetherLoadedPlugin record for @id,
 * or NULL if no plugin with that id has been registered / loaded.
 */
AetherLoadedPlugin *plugin_engine_get(const char *id);

/**
 * plugin_engine_get_widget:
 * Convenience wrapper — returns the GtkWidget* for @id, or NULL.
 */
GtkWidget *plugin_engine_get_widget(const char *id);

/**
 * plugin_engine_foreach:
 * Iterates over every registered plugin, calling @func(plugin, user_data)
 * for each one.
 */
/**
 * plugin_engine_recreate_widgets:
 * Calls create_widget() again for every registered plugin and updates the
 * internal widget pointer.  Call this before rebuilding the layout so that
 * layout_builder can get fresh, live widgets instead of destroyed ones.
 */
void plugin_engine_recreate_widgets(void);

/**
 * plugin_engine_release_saved_refs:
 * Drops the extra g_object_ref() added by plugin_engine_recreate_widgets().
 * Must be called after layout_builder_build() completes.
 */
void plugin_engine_release_saved_refs(void);

typedef void (*AetherPluginForeachFunc)(AetherLoadedPlugin *plugin,
                                        gpointer            user_data);
void plugin_engine_foreach(AetherPluginForeachFunc func, gpointer user_data);

/* ── System Events ─────────────────────────────────────────────────────────── */

/**
 * plugin_engine_broadcast_event:
 * Delivers a system event to every plugin that has registered an
 * on_system_event callback.
 *
 * @event     : the event type (see AetherSystemEvent in AetherCore-plugin-api.h)
 * @event_data: optional payload pointer; semantics are event-type-specific.
 */
void plugin_engine_broadcast_event(AetherSystemEvent event,
                                   void             *event_data);

/**
 * plugin_engine_notify_orientation:
 * Delivers an orientation change event to a specific plugin instance.
 */
void plugin_engine_notify_orientation(const char *plugin_id,
                                      GtkOrientation orientation);

/* ── Theme Management ─────────────────────────────────────────────────── */

/**
 * plugin_engine_apply_theme:
 * Reads the plugin's get_theme() result and generates scoped CSS keyed
 * on window_css_id, then installs it in a dedicated GtkCssProvider for
 * that plugin only.  Other plugins are completely unaffected.
 * Safe to call multiple times — the provider is updated in place.
 */
void plugin_engine_apply_theme(const char *plugin_id);

/**
 * plugin_engine_apply_theme_all:
 * Calls plugin_engine_apply_theme() for every registered plugin that
 * exposes a get_theme() callback.
 */
void plugin_engine_apply_theme_all(void);

/**
 * plugin_engine_get_theme:
 * Returns the AetherPluginTheme currently reported by the plugin, or
 * NULL if the plugin has no get_theme callback.
 * The pointer is owned by the plugin and must not be freed.
 */
const AetherPluginTheme *plugin_engine_get_theme(const char *plugin_id);

/**
 * plugin_engine_set_theme:
 * Overrides a plugin's theme at runtime with a caller-supplied struct.
 * The engine makes an internal copy; the caller may free theirs afterward.
 * Passing NULL restores the plugin's own get_theme() result.
 * Intended for use by the Settings / Designer UI.
 */
void plugin_engine_set_theme(const char           *plugin_id,
                             const AetherPluginTheme *theme);
