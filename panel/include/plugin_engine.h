#pragma once

#include <gtk/gtk.h>
#include "panel-plugin-api.h"

/* =========================================================================
 * AetherShell Panel — Plugin Engine
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
    AetherPanelPluginAPIv3 *api;         /* pointer to the plugin's API struct  */
    GtkWidget              *widget;      /* widget returned by create_widget()   */
    void                   *dl_handle;  /* dlopen handle; NULL for built-ins    */
    AetherPanelContext      ctx;         /* context passed to create_widget()   */
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
 * Registers a plugin whose API struct is allocated statically inside the panel
 * binary itself (no .so needed).  create_widget() is called immediately to
 * produce the GTK widget.
 *
 * @id  : unique plugin identifier (e.g. "aether-clock").  Copied internally.
 * @api : pointer to a statically-allocated AetherPanelPluginAPIv3.
 *
 * Returns the newly-created GtkWidget* on success, NULL on failure.
 */
GtkWidget *plugin_engine_register_builtin(const char             *id,
                                          AetherPanelPluginAPIv3 *api);

/**
 * plugin_engine_load_so:
 * Loads an external plugin from a shared library (.so) file.
 * The .so must export the symbol "aether_panel_plugin_init_v3" which returns
 * a pointer to a valid AetherPanelPluginAPIv3.
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
 * @event     : the event type (see AetherSystemEvent in panel-plugin-api.h)
 * @event_data: optional payload pointer; semantics are event-type-specific.
 */
void plugin_engine_broadcast_event(AetherSystemEvent event,
                                   void             *event_data);
