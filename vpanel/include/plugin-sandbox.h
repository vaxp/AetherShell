#pragma once

#include <gtk/gtk.h>
#include <glib.h>

/**
 * PluginSandbox — manages one isolated plugin slot in the panel.
 *
 * On X11   : the plugin runs in a child process connected via GtkSocket/GtkPlug.
 *            A crash in the plugin process cannot take down the panel.
 * On Wayland: the plugin runs in-process; a watchdog timer is used to detect
 *            hangs in create_widget(), and the slot shows a recovery widget on
 *            failure.
 */
typedef struct _PluginSandbox PluginSandbox;

typedef enum {
    PLUGIN_SANDBOX_STATE_IDLE        = 0,
    PLUGIN_SANDBOX_STATE_LAUNCHING   = 1,
    PLUGIN_SANDBOX_STATE_RUNNING     = 2,
    PLUGIN_SANDBOX_STATE_CRASHED     = 3,
    PLUGIN_SANDBOX_STATE_RESTARTING  = 4,
    PLUGIN_SANDBOX_STATE_DISABLED    = 5,  /* max retries exceeded */
} PluginSandboxState;

/**
 * Callback invoked when the plugin process terminates unexpectedly.
 * The panel uses this to trigger a visual recovery indicator.
 */
typedef void (*PluginCrashCallback)(PluginSandbox *sb, gpointer user_data);

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

/**
 * plugin_sandbox_new:
 * @so_path: absolute path to the plugin .so file
 *
 * Allocates a new sandbox structure.  Call plugin_sandbox_launch() to start
 * the plugin.  Free with plugin_sandbox_free().
 */
PluginSandbox *plugin_sandbox_new(const char *so_path);

/**
 * plugin_sandbox_launch:
 *
 * Starts (or restarts) the plugin.
 * Returns TRUE on success, FALSE if the plugin could not be started.
 */
gboolean plugin_sandbox_launch(PluginSandbox *sb);

/**
 * plugin_sandbox_restart:
 *
 * Terminates the current instance and relaunches after a short delay.
 */
void plugin_sandbox_restart(PluginSandbox *sb);

/**
 * plugin_sandbox_terminate:
 *
 * Stops the plugin and marks it disabled (no auto-restart).
 */
void plugin_sandbox_terminate(PluginSandbox *sb);

/**
 * plugin_sandbox_free:
 *
 * Terminates the plugin (if running) and frees all memory.
 */
void plugin_sandbox_free(PluginSandbox *sb);

/* ── Widget access ─────────────────────────────────────────────────────── */

/**
 * plugin_sandbox_get_slot_widget:
 *
 * Returns the GtkWidget that must be packed into the panel bar.
 *   X11   → GtkSocket  (shows plugin UI after process connects)
 *   Wayland → plugin's own GtkWidget (or a fallback on failure)
 *
 * The widget is owned by the sandbox.
 */
GtkWidget *plugin_sandbox_get_slot_widget(PluginSandbox *sb);

/* ── Introspection ─────────────────────────────────────────────────────── */

PluginSandboxState  plugin_sandbox_get_state(PluginSandbox *sb);
int                 plugin_sandbox_get_restart_count(PluginSandbox *sb);
const char         *plugin_sandbox_get_so_path(PluginSandbox *sb);

/* ── Configuration channel ─────────────────────────────────────────────── */

/**
 * plugin_sandbox_send_config:
 * @key:   config key string
 * @value: new value string
 *
 * On X11: writes "key=value\n" to the plugin process stdin.
 * On Wayland: calls on_config_changed() on the live widget directly.
 */
void plugin_sandbox_send_config(PluginSandbox *sb,
                                const char    *key,
                                const char    *value);

/* ── Crash notification ─────────────────────────────────────────────────── */

void plugin_sandbox_set_crash_callback(PluginSandbox      *sb,
                                       PluginCrashCallback cb,
                                       gpointer            user_data);
