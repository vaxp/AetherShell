/**
 * plugin-sandbox.c — manages isolated plugin instances for vpanel.
 *
 * X11 mode  : each plugin runs in a dedicated child process
 *             (vpanel-plugin-host).  The panel embeds the plugin UI via
 *             GtkSocket / GtkPlug.  A child-process crash cannot reach the
 *             panel process; the slot shows a recovery indicator and
 *             relaunches automatically.
 *
 * Wayland   : GtkSocket/GtkPlug are X11-only, so plugins run in-process.
 *             A watchdog alarm detects hangs in create_widget(); failed
 *             plugins show a fallback widget without crashing the panel.
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#  include <gdk/gdkx.h>
#  include <gtk/gtkx.h>  /* GtkSocket / GtkPlug */
#endif
#include <glib.h>
#include <glib/gspawn.h>
#include <dlfcn.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>

#include "plugin-sandbox.h"
#include "compositor-backend.h"
#include "window-backend.h"
#include "vpanel-plugin-api.h"

#define MAX_RESTART_ATTEMPTS  5
#define RESTART_DELAY_MS      2500

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct _PluginSandbox {
    char               *so_path;

    PluginSandboxState  state;
    int                 restart_count;

    PluginCrashCallback crash_cb;
    gpointer            crash_cb_data;

    /* ── Slot container (always present, packed into the panel bar) ── */
    GtkWidget          *slot_box;     /* GtkEventBox wrapping everything    */

    /* ── X11 subprocess fields ── */
    GPid                child_pid;
    guint               child_watch_id;
    GtkWidget          *socket_widget;
    GIOChannel         *stdin_chan;   /* pipe to child stdin for config IPC */

    /* ── Wayland in-process fields ── */
    void               *dl_handle;
    GtkWidget          *plugin_widget;
    /* v3 api pointer (needed for on_config_changed) */
    const VenomPanelPluginAPIv3 *api_v3;

    /* ── Restart timer ── */
    guint               restart_timer_id;
};

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static void do_launch_x11(PluginSandbox *sb);
static void do_launch_wayland(PluginSandbox *sb);
static void on_plug_removed(GtkWidget *socket, gpointer user_data);
static void show_crash_widget(PluginSandbox *sb, const char *reason);
static void clear_slot(PluginSandbox *sb);
static gboolean on_restart_timeout(gpointer user_data);

/* =========================================================================
 * Helper: find vpanel-plugin-host binary
 * ========================================================================= */

static char *find_plugin_host_binary(void)
{
    /* 1. Same directory as the running vpanel executable */
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
        char *dir = g_path_get_dirname(self);
        char *candidate = g_build_filename(dir, "vpanel-plugin-host", NULL);
        g_free(dir);
        if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
            return candidate;
        g_free(candidate);
    }

    /* 2. $PATH */
    char *in_path = g_find_program_in_path("vpanel-plugin-host");
    if (in_path) return in_path;

    return NULL;
}

/* =========================================================================
 * Helper: build a styled fallback widget shown when a plugin fails
 * ========================================================================= */

static GtkWidget *make_fallback_widget(const char *so_basename,
                                       const char *reason,
                                       gboolean    crashed)
{
    GtkWidget *box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *icon  = gtk_image_new_from_icon_name(
        crashed ? "dialog-warning-symbolic" : "dialog-error-symbolic",
        GTK_ICON_SIZE_MENU);

    char *tip = g_strdup_printf("%s\n%s", so_basename, reason ? reason : "");
    gtk_widget_set_tooltip_text(box, tip);
    g_free(tip);

    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

    /* Red tint via CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "box { background-color: rgba(220,50,50,0.15);"
        "      border-radius: 4px; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(box),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_widget_show_all(box);
    return box;
}

/* =========================================================================
 * Slot management helpers
 * ========================================================================= */

static void clear_slot(PluginSandbox *sb)
{
    /* Remove all children from slot_box */
    GList *children = gtk_container_get_children(GTK_CONTAINER(sb->slot_box));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    sb->socket_widget  = NULL;
    sb->plugin_widget  = NULL;
}

static void show_crash_widget(PluginSandbox *sb, const char *reason)
{
    clear_slot(sb);
    char *base = g_path_get_basename(sb->so_path);
    GtkWidget *fw = make_fallback_widget(base, reason, TRUE);
    g_free(base);
    gtk_container_add(GTK_CONTAINER(sb->slot_box), fw);
    gtk_widget_show_all(sb->slot_box);
}

/* =========================================================================
 * X11 subprocess path  (GtkSocket / GtkPlug — X11 only)
 * ========================================================================= */
#ifdef GDK_WINDOWING_X11

static void on_child_exit(GPid pid, gint status, gpointer user_data)
{
    PluginSandbox *sb = (PluginSandbox *)user_data;
    (void)pid;

    g_spawn_close_pid(sb->child_pid);
    sb->child_pid       = 0;
    sb->child_watch_id  = 0;

    if (sb->state == PLUGIN_SANDBOX_STATE_DISABLED) return;

    gboolean normal_exit = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (normal_exit) {
        sb->state = PLUGIN_SANDBOX_STATE_IDLE;
        return;
    }

    /* Crashed */
    sb->state = PLUGIN_SANDBOX_STATE_CRASHED;
    sb->restart_count++;

    char *base = g_path_get_basename(sb->so_path);
    g_warning("[Sandbox] Plugin '%s' crashed (attempt %d/%d)",
              base, sb->restart_count, MAX_RESTART_ATTEMPTS);
    g_free(base);

    if (sb->crash_cb)
        sb->crash_cb(sb, sb->crash_cb_data);

    if (sb->restart_count <= MAX_RESTART_ATTEMPTS) {
        show_crash_widget(sb, "Crashed — restarting…");
        sb->state = PLUGIN_SANDBOX_STATE_RESTARTING;
        sb->restart_timer_id = g_timeout_add(RESTART_DELAY_MS,
                                             on_restart_timeout, sb);
    } else {
        show_crash_widget(sb, "Disabled: too many crashes");
        sb->state = PLUGIN_SANDBOX_STATE_DISABLED;
        g_warning("[Sandbox] Plugin disabled after %d crashes", sb->restart_count);
    }
}

static void on_socket_realized(GtkWidget *socket, gpointer user_data)
{
    PluginSandbox *sb = (PluginSandbox *)user_data;

    /* Disconnect so we don't fire again on re-realize */
    g_signal_handlers_disconnect_by_func(socket,
        G_CALLBACK(on_socket_realized), user_data);

    char *host_bin = find_plugin_host_binary();
    if (!host_bin) {
        g_warning("[Sandbox] vpanel-plugin-host not found in PATH");
        show_crash_widget(sb, "vpanel-plugin-host not found");
        return;
    }

    gulong xid = gtk_socket_get_id(GTK_SOCKET(socket));
    char xid_str[32];
    g_snprintf(xid_str, sizeof(xid_str), "%lu", xid);

    const char *argv[] = {
        host_bin, "--socket-id", xid_str, "--plugin", sb->so_path, NULL
    };

    GError  *err   = NULL;
    gint     stdin_fd = -1;
    GPid     pid   = 0;

    gboolean ok = g_spawn_async_with_pipes(
        NULL, (gchar **)argv, NULL,
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
        NULL, NULL,
        &pid,
        &stdin_fd,   /* stdin  */
        NULL,        /* stdout */
        NULL,        /* stderr */
        &err);

    g_free(host_bin);

    if (!ok) {
        g_warning("[Sandbox] Failed to spawn plugin host: %s",
                  err ? err->message : "unknown");
        if (err) g_error_free(err);
        show_crash_widget(sb, "Failed to spawn plugin process");
        return;
    }

    sb->child_pid      = pid;
    sb->state          = PLUGIN_SANDBOX_STATE_RUNNING;

    if (stdin_fd >= 0) {
        sb->stdin_chan = g_io_channel_unix_new(stdin_fd);
        g_io_channel_set_close_on_unref(sb->stdin_chan, TRUE);
    }

    sb->child_watch_id = g_child_watch_add(pid, on_child_exit, sb);

    char *base = g_path_get_basename(sb->so_path);
    g_print("[Sandbox] Plugin '%s' started (pid=%d, xid=%lu)\n",
            base, (int)pid, xid);
    g_free(base);
}

static void on_plug_removed(GtkWidget *socket, gpointer user_data)
{
    (void)socket;
    PluginSandbox *sb = (PluginSandbox *)user_data;
    /* Plug was removed — child likely crashed; on_child_exit handles restart */
    (void)sb;
}

static void do_launch_x11(PluginSandbox *sb)
{
    sb->socket_widget = gtk_socket_new();

    g_signal_connect(sb->socket_widget, "realize",
                     G_CALLBACK(on_socket_realized), sb);
    g_signal_connect(sb->socket_widget, "plug-removed",
                     G_CALLBACK(on_plug_removed), sb);

    gtk_container_add(GTK_CONTAINER(sb->slot_box), sb->socket_widget);
    gtk_widget_show_all(sb->slot_box);

    sb->state = PLUGIN_SANDBOX_STATE_LAUNCHING;
}

#endif /* GDK_WINDOWING_X11 */

/* =========================================================================
 * Wayland in-process path
 * ========================================================================= */

/* Watchdog: set by alarm(), clears after create_widget returns */
static volatile sig_atomic_t g_watchdog_fired = 0;
static void watchdog_handler(int sig) { (void)sig; g_watchdog_fired = 1; }

static void do_launch_wayland(PluginSandbox *sb)
{
    if (!sb->dl_handle) {
        sb->dl_handle = dlopen(sb->so_path, RTLD_NOW | RTLD_LOCAL);
        if (!sb->dl_handle) {
            char *base = g_path_get_basename(sb->so_path);
            g_warning("[Sandbox] dlopen '%s': %s", base, dlerror());
            g_free(base);
            show_crash_widget(sb, "Plugin failed to load");
            sb->state = PLUGIN_SANDBOX_STATE_DISABLED;
            return;
        }
    }

    /* Resolve best available init function: v3 > v2 > v1 */
    VenomPanelPluginInitFnV3 fn3 =
        (VenomPanelPluginInitFnV3)dlsym(sb->dl_handle,
                                        "venom_panel_plugin_init_v3");
    VenomPanelPluginInitFnV2 fn2 =
        (VenomPanelPluginInitFnV2)dlsym(sb->dl_handle,
                                        "venom_panel_plugin_init_v2");
    VenomPanelPluginInitFn fn1 =
        (VenomPanelPluginInitFn)dlsym(sb->dl_handle,
                                      "venom_panel_plugin_init");

    const VenomPanelPluginAPIv3 *api3 = NULL;
    const VenomPanelPluginAPIv2 *api2 = NULL;
    const VenomPanelPluginAPI   *api1 = NULL;

    if (fn3) {
        api3 = fn3();
        const size_t min_sz =
            offsetof(VenomPanelPluginAPIv3, create_widget) +
            sizeof(((VenomPanelPluginAPIv3*)0)->create_widget);
        if (!api3 || api3->api_version != VENOM_PANEL_PLUGIN_API_VERSION
                  || api3->struct_size < min_sz) {
            g_warning("[Sandbox] Invalid v3 API in '%s'", sb->so_path);
            api3 = NULL;
        }
    }
    if (!api3 && fn2) {
        api2 = fn2();
        if (!api2 || api2->api_version != VENOM_PANEL_PLUGIN_API_VERSION_V2) {
            api2 = NULL;
        }
    }
    if (!api3 && !api2 && fn1) api1 = fn1();

    GtkWidget* (*create_fn)(void) =
        api3 ? api3->create_widget :
        api2 ? api2->create_widget :
        api1 ? api1->create_widget : NULL;

    if (!create_fn) {
        show_crash_widget(sb, "No valid init symbol");
        sb->state = PLUGIN_SANDBOX_STATE_DISABLED;
        return;
    }

    sb->api_v3 = api3;

    /* Optional watchdog using SIGALRM */
    guint watchdog_ms = api3 ? api3->watchdog_ms : 0;
    struct sigaction old_sa;
    if (watchdog_ms > 0) {
        g_watchdog_fired = 0;
        struct sigaction sa = { .sa_handler = watchdog_handler,
                                .sa_flags   = SA_RESETHAND };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, &old_sa);
        alarm((watchdog_ms + 999) / 1000);  /* ceil to nearest second */
    }

    GtkWidget *w = create_fn();

    if (watchdog_ms > 0) {
        alarm(0);
        sigaction(SIGALRM, &old_sa, NULL);
        if (g_watchdog_fired) {
            show_crash_widget(sb, "Plugin timed out during initialisation");
            sb->state = PLUGIN_SANDBOX_STATE_DISABLED;
            return;
        }
    }

    if (!w) {
        char *base = g_path_get_basename(sb->so_path);
        show_crash_widget(sb, "create_widget() returned NULL");
        g_free(base);
        sb->state = PLUGIN_SANDBOX_STATE_DISABLED;
        return;
    }

    sb->plugin_widget = w;
    sb->state         = PLUGIN_SANDBOX_STATE_RUNNING;

    gtk_container_add(GTK_CONTAINER(sb->slot_box), w);
    gtk_widget_show_all(sb->slot_box);

    char *base = g_path_get_basename(sb->so_path);
    g_print("[Sandbox] Plugin '%s' loaded in-process (Wayland)\n", base);
    g_free(base);
}

/* =========================================================================
 * Restart timer callback
 * ========================================================================= */

static gboolean on_restart_timeout(gpointer user_data)
{
    PluginSandbox *sb = (PluginSandbox *)user_data;
    sb->restart_timer_id = 0;

    char *base = g_path_get_basename(sb->so_path);
    g_print("[Sandbox] Restarting plugin '%s' (attempt %d)…\n",
            base, sb->restart_count);
    g_free(base);

    clear_slot(sb);
    plugin_sandbox_launch(sb);
    return G_SOURCE_REMOVE;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

PluginSandbox *plugin_sandbox_new(const char *so_path)
{
    PluginSandbox *sb = g_new0(PluginSandbox, 1);
    sb->so_path   = g_strdup(so_path);
    sb->state     = PLUGIN_SANDBOX_STATE_IDLE;
    sb->child_pid = 0;

    sb->slot_box  = gtk_event_box_new();
    /* Mark with CSS class for potential theming */
    gtk_style_context_add_class(
        gtk_widget_get_style_context(sb->slot_box),
        "vpanel-plugin-slot");

    return sb;
}

gboolean plugin_sandbox_launch(PluginSandbox *sb)
{
    g_return_val_if_fail(sb != NULL, FALSE);

    if (sb->state == PLUGIN_SANDBOX_STATE_RUNNING ||
        sb->state == PLUGIN_SANDBOX_STATE_LAUNCHING)
        return TRUE;

    if (sb->state == PLUGIN_SANDBOX_STATE_DISABLED)
        return FALSE;

    if (panel_window_backend_is_wayland()) {
        do_launch_wayland(sb);
    } else {
#ifdef GDK_WINDOWING_X11
        do_launch_x11(sb);
#else
        /* No X11 support — fall back to in-process mode */
        do_launch_wayland(sb);
#endif
    }

    return (sb->state != PLUGIN_SANDBOX_STATE_DISABLED);
}

void plugin_sandbox_restart(PluginSandbox *sb)
{
    g_return_if_fail(sb != NULL);

    if (sb->state == PLUGIN_SANDBOX_STATE_DISABLED) return;

    plugin_sandbox_terminate(sb);
    sb->state = PLUGIN_SANDBOX_STATE_IDLE;

    if (sb->restart_timer_id) {
        g_source_remove(sb->restart_timer_id);
        sb->restart_timer_id = 0;
    }

    plugin_sandbox_launch(sb);
}

void plugin_sandbox_terminate(PluginSandbox *sb)
{
    g_return_if_fail(sb != NULL);

    if (sb->restart_timer_id) {
        g_source_remove(sb->restart_timer_id);
        sb->restart_timer_id = 0;
    }

#ifdef GDK_WINDOWING_X11
    /* X11: kill child process */
    if (sb->child_pid > 0) {
        kill(sb->child_pid, SIGTERM);
        if (sb->child_watch_id) {
            g_source_remove(sb->child_watch_id);
            sb->child_watch_id = 0;
        }
        g_spawn_close_pid(sb->child_pid);
        sb->child_pid = 0;
    }

    if (sb->stdin_chan) {
        g_io_channel_shutdown(sb->stdin_chan, TRUE, NULL);
        g_io_channel_unref(sb->stdin_chan);
        sb->stdin_chan = NULL;
    }
#endif /* GDK_WINDOWING_X11 */

    /* Wayland: call destroy_widget if available */
    if (sb->plugin_widget) {
        if (sb->api_v3 && sb->api_v3->destroy_widget)
            sb->api_v3->destroy_widget(sb->plugin_widget);
        sb->plugin_widget = NULL;
        sb->api_v3        = NULL;
    }

    /* MUST destroy the widgets BEFORE dlclose(), otherwise GTK will call
     * dispose/finalize on unloaded code and segfault. */
    clear_slot(sb);

    if (sb->dl_handle) {
        dlclose(sb->dl_handle);
        sb->dl_handle = NULL;
    }

    sb->state = PLUGIN_SANDBOX_STATE_IDLE;
}

void plugin_sandbox_free(PluginSandbox *sb)
{
    if (!sb) return;
    sb->state = PLUGIN_SANDBOX_STATE_DISABLED; /* prevent restart during shutdown */
    plugin_sandbox_terminate(sb);
    if (sb->slot_box) gtk_widget_destroy(sb->slot_box);
    g_free(sb->so_path);
    g_free(sb);
}

GtkWidget *plugin_sandbox_get_slot_widget(PluginSandbox *sb)
{
    g_return_val_if_fail(sb != NULL, NULL);
    return sb->slot_box;
}

PluginSandboxState plugin_sandbox_get_state(PluginSandbox *sb)
{
    g_return_val_if_fail(sb != NULL, PLUGIN_SANDBOX_STATE_IDLE);
    return sb->state;
}

int plugin_sandbox_get_restart_count(PluginSandbox *sb)
{
    g_return_val_if_fail(sb != NULL, 0);
    return sb->restart_count;
}

const char *plugin_sandbox_get_so_path(PluginSandbox *sb)
{
    g_return_val_if_fail(sb != NULL, NULL);
    return sb->so_path;
}

void plugin_sandbox_set_crash_callback(PluginSandbox      *sb,
                                       PluginCrashCallback cb,
                                       gpointer            user_data)
{
    g_return_if_fail(sb != NULL);
    sb->crash_cb      = cb;
    sb->crash_cb_data = user_data;
}

void plugin_sandbox_send_config(PluginSandbox *sb,
                                const char    *key,
                                const char    *value)
{
    g_return_if_fail(sb && key && value);

    if (panel_window_backend_is_wayland()) {
        /* In-process: invoke callback directly on the live widget */
        if (sb->api_v3 && sb->api_v3->on_config_changed && sb->plugin_widget)
            sb->api_v3->on_config_changed(sb->plugin_widget, key, value);
        return;
    }

    /* X11: write "key=value\n" to child stdin */
    if (!sb->stdin_chan) return;

    char *line = g_strdup_printf("%s=%s\n", key, value);
    GError *err = NULL;
    gsize written = 0;
    g_io_channel_write_chars(sb->stdin_chan, line, -1, &written, &err);
    g_io_channel_flush(sb->stdin_chan, NULL);
    g_free(line);

    if (err) { g_error_free(err); }
}
