/*
 * builtin_plugins.c — AetherShell Panel Built-in Plugin Registration
 *
 * Each existing panel component is wrapped in a thin AetherPanelPluginAPIv3
 * descriptor and registered with the plugin engine under a stable string ID.
 * The existing implementation files (battery_indicator.c, wifi_indicator.c,
 * etc.) are left completely untouched.
 *
 * Plugins that own "background" windows (app menu, control center, sidebar,
 * notifications) are initialised here as well; the resulting windows are
 * stored as g_object data on the button widget so panel.c can retrieve them
 * if needed.
 */

#include <gtk/gtk.h>
#include <string.h>
#include <time.h>

#include "builtin_plugins.h"
#include "plugin_engine.h"
#include "window_backend.h"

/* ── Existing component headers (unchanged) ─────────────────────────────── */
#include "app_menu.h"
#include "battery_indicator.h"
#include "bt_indicator.h"
#include "control_center_ui.h"
#include "keyboard_layout.h"
#include "mic_indicator.h"
#include "notifications_ui.h"
#include "resource_paths.h"
#include "sidebar_popup.h"
#include "sni_tray.h"
#include "volume_indicator.h"
#include "wifi_indicator.h"
#include "workspaces.h"

/* =========================================================================
 * Helper macro — declares a static APIv3 struct and its create_widget wrapper
 * for components whose create function already matches the
 * GtkWidget* (*)(void) signature.
 *
 * BUILTIN_SIMPLE(token, plugin_name, description, zone_hint, fn)
 *   token       — C identifier token (used to name the struct + wrapper)
 *   plugin_name — human-readable name string
 *   description — human-readable description string
 *   zone_hint   — AetherPluginZone (LEFT/CENTER/RIGHT)
 *   fn          — the existing GtkWidget*(*)(void) function
 * ========================================================================= */
#define BUILTIN_SIMPLE(token, plugin_name, desc, zone_hint, fn)            \
    static GtkWidget *_wrap_##token(AetherPanelContext *ctx) {             \
        (void)ctx;                                                          \
        return fn();                                                        \
    }                                                                       \
    static AetherPanelPluginAPIv3 _api_##token = {                         \
        .api_version  = AETHER_PANEL_PLUGIN_API_VERSION,                   \
        .struct_size  = sizeof(AetherPanelPluginAPIv3),                    \
        .name         = plugin_name,                                        \
        .description  = desc,                                              \
        .author       = "AetherShell",                                     \
        .version      = "1.0.0",                                           \
        .zone         = zone_hint,                                         \
        .create_widget = _wrap_##token,                                    \
    }

/* Same as BUILTIN_SIMPLE but includes a get_theme callback.
 * theme_fn must be: static const AetherPluginTheme *_get_theme_##token(void) */
#define BUILTIN_SIMPLE_THEMED(token, plugin_name, desc, zone_hint, fn, theme_fn) \
    static GtkWidget *_wrap_##token(AetherPanelContext *ctx) {             \
        (void)ctx;                                                          \
        return fn();                                                        \
    }                                                                       \
    static AetherPanelPluginAPIv3 _api_##token = {                         \
        .api_version  = AETHER_PANEL_PLUGIN_API_VERSION,                   \
        .struct_size  = sizeof(AetherPanelPluginAPIv3),                    \
        .name         = plugin_name,                                        \
        .description  = desc,                                              \
        .author       = "AetherShell",                                     \
        .version      = "1.0.0",                                           \
        .zone         = zone_hint,                                         \
        .create_widget = _wrap_##token,                                    \
        .get_theme    = theme_fn,                                          \
    }

/* =========================================================================
 * Per-plugin colour maps (AetherPluginTheme)
 *
 * window_css_id must match the GTK widget name set on the popup window by
 * the indicator source file (e.g. volume_indicator.c sets the top-level
 * window name to "volume-mixer-window").
 *
 * AETHER_THEME_DARK(window_id, root_r, root_g, root_b, min_width) fills in
 * the full dark-glassmorphism palette; only accent colour differs per plugin.
 * ========================================================================= */

/* Volume — cyan accent */
static const AetherPluginTheme _theme_volume_data =
    AETHER_THEME_DARK("volume-mixer-window", "mixer-outer", 0.0, 0.898, 1.0, 300);
static const AetherPluginTheme *_get_theme_volume(void) { return &_theme_volume_data; }

/* Microphone — red accent */
static const AetherPluginTheme _theme_mic_data =
    AETHER_THEME_DARK("mic-mixer-window", "mic-mixer-outer", 1.0, 0.266, 0.266, 300);
static const AetherPluginTheme *_get_theme_mic(void) { return &_theme_mic_data; }

/* Wi-Fi — light-blue accent */
static const AetherPluginTheme _theme_wifi_data =
    AETHER_THEME_DARK("wifi-indicator-window", "wifi-popup-outer", 0.309, 0.764, 0.968, 300);
static const AetherPluginTheme *_get_theme_wifi(void) { return &_theme_wifi_data; }

/* Bluetooth — indigo-blue accent */
static const AetherPluginTheme _theme_bt_data =
    AETHER_THEME_DARK("bt-indicator-window", "wifi-popup-outer", 0.266, 0.541, 1.0, 300);
static const AetherPluginTheme *_get_theme_bt(void) { return &_theme_bt_data; }

/* Battery — green accent */
static const AetherPluginTheme _theme_battery_data =
    AETHER_THEME_DARK("battery-indicator-window", "battery-popup-outer", 0.411, 0.941, 0.682, 300);
static const AetherPluginTheme *_get_theme_battery(void) { return &_theme_battery_data; }

/* App Menu — neutral-white accent */
static const AetherPluginTheme _theme_appmenu_data =
    AETHER_THEME_DARK("app-menu-popover", "app-menu-box", 0.933, 0.933, 0.933, 300);
static const AetherPluginTheme *_get_theme_appmenu(void) { return &_theme_appmenu_data; }

/* Clock / Sidebar — cyan accent */
static const AetherPluginTheme _theme_clock_data =
    AETHER_THEME_DARK("sidebar", NULL, 0.0, 0.898, 1.0, 300);
static const AetherPluginTheme *_get_theme_clock(void) { return &_theme_clock_data; }

/* Notifications — purple accent */
static const AetherPluginTheme _theme_notifs_data =
    AETHER_THEME_DARK("notifications-popover", "main-box", 0.807, 0.576, 0.847, 300);
static const AetherPluginTheme *_get_theme_notifs(void) { return &_theme_notifs_data; }

/* Control Center — cyan accent (wider popup) */
static const AetherPluginTheme _theme_cc_data =
    AETHER_THEME_DARK("control-center-popover", "main-box", 0.0, 0.898, 1.0, 300);
static const AetherPluginTheme *_get_theme_cc(void) { return &_theme_cc_data; }

/* =========================================================================
 * 1. App Menu button
 *    The menu popup window is initialised inside the wrapper and stored as
 *    "app-menu-window" on the returned button widget.
 * ========================================================================= */
/* ── App Menu toggle ──────────────────────────────────────────────────────── */

static void _on_appmenu_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *menu_w = GTK_WIDGET(user_data);
    if (gtk_widget_get_visible(menu_w))
        gtk_widget_hide(menu_w);
    else {
        panel_window_backend_align_popup(GTK_WINDOW(menu_w), GTK_WIDGET(btn), 220);
        gtk_widget_show_all(menu_w);
    }
}

static GtkWidget *_wrap_appmenu(AetherPanelContext *ctx)
{
    (void)ctx;

    GtkWidget *menu_w = init_app_menu();

    /* Build the menu button */
    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, "Application Menu");

    char *icon_path = panel_resource_path_in("images", "vaxp.png");
    GdkPixbuf *pb   = icon_path
        ? gdk_pixbuf_new_from_file_at_scale(icon_path, 22, 22, TRUE, NULL)
        : NULL;
    g_free(icon_path);

    GtkWidget *img = pb
        ? gtk_image_new_from_pixbuf(pb)
        : gtk_image_new_from_icon_name("start-here-symbolic", GTK_ICON_SIZE_MENU);
    if (pb) g_object_unref(pb);

    gtk_container_add(GTK_CONTAINER(btn), img);

    app_menu_set_relative_to(menu_w, btn);

    /* Wire toggle — opens on first click, closes on second */
    g_signal_connect(btn, "clicked",
                     G_CALLBACK(_on_appmenu_clicked), menu_w);

    /* Stash popup so external code can reach it */
    g_object_set_data(G_OBJECT(btn), "app-menu-window", menu_w);

    return btn;
}

/* ── Static callbacks (C-compatible, no lambdas) ────────────────────────── */

static gboolean _clock_tick(gpointer data)
{
    GtkWidget *lbl = GTK_WIDGET(data);
    if (!GTK_IS_LABEL(lbl)) return G_SOURCE_REMOVE;
    time_t     t   = time(NULL);
    struct tm *tm  = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M  %b %d", tm);
    gtk_label_set_text(GTK_LABEL(lbl), buf);
    return G_SOURCE_CONTINUE;
}

/*
 * _on_clock_clicked:
 * Called when the time-button is clicked.
 * user_data = sidebar_w (the popup window).
 * We also need the button itself as relative_to — store it via g_object_data.
 */
static void _on_clock_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *sidebar_w = GTK_WIDGET(user_data);
    GtkWidget *relative  = GTK_WIDGET(btn);
    sidebar_popup_toggle(sidebar_w, relative);
}

static void _on_clipboard_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    GError *err  = NULL;
    gchar  *argv[] = {
        "dbus-send", "--session", "--print-reply",
        "--dest=org.aether.Clipboard",
        "/org/aether/Clipboard",
        "org.aether.Clipboard.Toggle",
        NULL
    };
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) { g_warning("[clipboard] %s", err->message); g_error_free(err); }
}

static void _on_search_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    GError *err  = NULL;
    gchar  *argv[] = {
        "gdbus", "call", "--session",
        "--dest",        "org.vaxp.Basilisk",
        "--object-path", "/org/vaxp/Basilisk",
        "--method",      "org.vaxp.Basilisk.Toggle",
        NULL
    };
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) { g_warning("[search] %s", err->message); g_error_free(err); }
}

static void _on_notifs_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *w = GTK_WIDGET(user_data);
    if (gtk_widget_get_visible(w)) gtk_widget_hide(w);
    else {
        panel_window_backend_align_popup(GTK_WINDOW(w), GTK_WIDGET(btn), 360);
        gtk_widget_show_all(w);
    }
}

static void _on_cc_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *w = GTK_WIDGET(user_data);
    if (GTK_IS_POPOVER(w)) {
        if (gtk_widget_get_visible(w)) gtk_popover_popdown(GTK_POPOVER(w));
        else gtk_popover_popup(GTK_POPOVER(w));
    } else {
        if (gtk_widget_get_visible(w)) gtk_widget_hide(w);
        else {
            panel_window_backend_align_popup(GTK_WINDOW(w), GTK_WIDGET(btn), 300);
            gtk_widget_show_all(w);
        }
    }
}

static AetherPanelPluginAPIv3 _api_appmenu = {
    .api_version   = AETHER_PANEL_PLUGIN_API_VERSION,
    .struct_size   = sizeof(AetherPanelPluginAPIv3),
    .name          = "App Menu",
    .description   = "Application / start-menu button",
    .author        = "AetherShell",
    .version       = "1.0.0",
    .icon_name     = "start-here-symbolic",
    .zone          = AETHER_PLUGIN_ZONE_LEFT,
    .create_widget = _wrap_appmenu,
    .get_theme     = _get_theme_appmenu,
};

/* =========================================================================
 * 2. Clipboard button
 * ========================================================================= */
static GtkWidget *_wrap_clipboard(AetherPanelContext *ctx)
{
    (void)ctx;

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, "Clipboard");

    GtkWidget *icon = gtk_image_new_from_icon_name("edit-paste-symbolic",
                                                    GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(btn), icon);
    g_signal_connect(btn, "clicked", G_CALLBACK(_on_clipboard_clicked), NULL);
    return btn;
}

static AetherPanelPluginAPIv3 _api_clipboard = {
    .api_version   = AETHER_PANEL_PLUGIN_API_VERSION,
    .struct_size   = sizeof(AetherPanelPluginAPIv3),
    .name          = "Clipboard",
    .description   = "Clipboard history toggle",
    .author        = "AetherShell",
    .version       = "1.0.0",
    .icon_name     = "edit-paste-symbolic",
    .zone          = AETHER_PLUGIN_ZONE_LEFT,
    .create_widget = _wrap_clipboard,
};

/* =========================================================================
 * 3. Workspaces
 * ========================================================================= */
BUILTIN_SIMPLE(workspaces,
               "Workspaces",
               "Workspace dot indicator",
               AETHER_PLUGIN_ZONE_LEFT,
               create_workspaces_widget);

/* =========================================================================
 * 4. Clock / Date  (opens the calendar sidebar on click)
 * ========================================================================= */
static GtkWidget *_wrap_clock(AetherPanelContext *ctx)
{
    (void)ctx;

    GtkWidget *sidebar_w = init_sidebar_popup();

    GtkWidget *time_label = gtk_label_new("--:-- --- --");

    /* Live clock update */
    _clock_tick(time_label);
    g_timeout_add_seconds(1, _clock_tick, time_label);

    GtkWidget *btn = gtk_button_new();
    gtk_widget_set_name(btn, "time-button");
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(btn), time_label);

    sidebar_popup_set_relative_to(sidebar_w, btn);

    g_signal_connect(btn, "clicked",
                     G_CALLBACK(_on_clock_clicked), sidebar_w);

    return btn;
}

static AetherPanelPluginAPIv3 _api_clock = {
    .api_version   = AETHER_PANEL_PLUGIN_API_VERSION,
    .struct_size   = sizeof(AetherPanelPluginAPIv3),
    .name          = "Clock",
    .description   = "Current time and date; click to open calendar",
    .author        = "AetherShell",
    .version       = "1.0.0",
    .icon_name     = "appointment-soon-symbolic",
    .zone          = AETHER_PLUGIN_ZONE_CENTER,
    .singleton     = TRUE,
    .create_widget = _wrap_clock,
    .get_theme     = _get_theme_clock,
};

/* =========================================================================
 * 5–10. Simple wrappers for the right-side indicators
 * ========================================================================= */
/* sni_tray and keyboard have no popup windows — no theme needed */
BUILTIN_SIMPLE(sni_tray,  "System Tray",         "SNI status-notifier tray",
               AETHER_PLUGIN_ZONE_RIGHT, create_sni_tray_widget);

BUILTIN_SIMPLE(keyboard,  "Keyboard Layout",     "Keyboard layout indicator",
               AETHER_PLUGIN_ZONE_RIGHT, create_keyboard_layout_widget);

/* wifi, bt, mic, volume, battery each own a popup window → themed */
BUILTIN_SIMPLE_THEMED(wifi,    "Wi-Fi",      "Wi-Fi status indicator",
               AETHER_PLUGIN_ZONE_RIGHT, create_wifi_indicator_widget,
               _get_theme_wifi);

BUILTIN_SIMPLE_THEMED(bt,      "Bluetooth",  "Bluetooth status indicator",
               AETHER_PLUGIN_ZONE_RIGHT, create_bt_indicator_widget,
               _get_theme_bt);

BUILTIN_SIMPLE_THEMED(mic,     "Microphone", "Microphone status indicator",
               AETHER_PLUGIN_ZONE_RIGHT, create_mic_indicator_widget,
               _get_theme_mic);

BUILTIN_SIMPLE_THEMED(volume,  "Volume",     "Volume indicator",
               AETHER_PLUGIN_ZONE_RIGHT, create_volume_indicator_widget,
               _get_theme_volume);

BUILTIN_SIMPLE_THEMED(battery, "Battery",   "Battery level indicator",
               AETHER_PLUGIN_ZONE_RIGHT, get_battery_widget,
               _get_theme_battery);

/* =========================================================================
 * 11. Search button
 * ========================================================================= */
static GtkWidget *_wrap_search(AetherPanelContext *ctx)
{
    (void)ctx;

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, "Search");

    GtkWidget *icon = gtk_image_new_from_icon_name("system-search-symbolic",
                                                    GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(btn), icon);

    g_signal_connect(btn, "clicked", G_CALLBACK(_on_search_clicked), NULL);
    return btn;
}

static AetherPanelPluginAPIv3 _api_search = {
    .api_version   = AETHER_PANEL_PLUGIN_API_VERSION,
    .struct_size   = sizeof(AetherPanelPluginAPIv3),
    .name          = "Search",
    .description   = "Opens the Basilisk application launcher",
    .author        = "AetherShell",
    .version       = "1.0.0",
    .icon_name     = "system-search-symbolic",
    .zone          = AETHER_PLUGIN_ZONE_RIGHT,
    .create_widget = _wrap_search,
};

/* =========================================================================
 * 12. Notifications button
 * ========================================================================= */
static GtkWidget *_wrap_notifs(AetherPanelContext *ctx)
{
    (void)ctx;

    GtkWidget *notif_w = init_notifications_ui();

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, "Notifications");

    GtkWidget *icon = gtk_image_new_from_icon_name(
        "preferences-system-notifications-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(btn), icon);

    notifications_ui_set_relative_to(notif_w, btn);

    g_signal_connect(btn, "clicked", G_CALLBACK(_on_notifs_clicked), notif_w);
    return btn;
}

static AetherPanelPluginAPIv3 _api_notifs = {
    .api_version   = AETHER_PANEL_PLUGIN_API_VERSION,
    .struct_size   = sizeof(AetherPanelPluginAPIv3),
    .name          = "Notifications",
    .description   = "Notifications history panel",
    .author        = "AetherShell",
    .version       = "1.0.0",
    .icon_name     = "preferences-system-notifications-symbolic",
    .zone          = AETHER_PLUGIN_ZONE_RIGHT,
    .create_widget = _wrap_notifs,
    .get_theme     = _get_theme_notifs,
};

/* =========================================================================
 * 13. Control Center button
 * ========================================================================= */
static GtkWidget *_wrap_cc(AetherPanelContext *ctx)
{
    (void)ctx;

    GtkWidget *cc_w = init_control_center();

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, "Control Center");

    char      *icon_path = panel_resource_path_in("images", "control-center-icon.svg");
    GdkPixbuf *pb        = icon_path
        ? gdk_pixbuf_new_from_file_at_scale(icon_path, 22, 22, TRUE, NULL)
        : NULL;
    g_free(icon_path);

    GtkWidget *img = pb
        ? gtk_image_new_from_pixbuf(pb)
        : gtk_image_new_from_icon_name("preferences-system-symbolic",
                                       GTK_ICON_SIZE_MENU);
    if (pb) g_object_unref(pb);

    gtk_container_add(GTK_CONTAINER(btn), img);

    control_center_set_relative_to(cc_w, btn);

    g_signal_connect(btn, "clicked", G_CALLBACK(_on_cc_clicked), cc_w);
    return btn;
}

static AetherPanelPluginAPIv3 _api_cc = {
    .api_version   = AETHER_PANEL_PLUGIN_API_VERSION,
    .struct_size   = sizeof(AetherPanelPluginAPIv3),
    .name          = "Control Center",
    .description   = "Quick settings / control center panel",
    .author        = "AetherShell",
    .version       = "1.0.0",
    .icon_name     = "preferences-system-symbolic",
    .zone          = AETHER_PLUGIN_ZONE_RIGHT,
    .singleton     = TRUE,
    .create_widget = _wrap_cc,
    .get_theme     = _get_theme_cc,
};

/* =========================================================================
 * Registration entry point
 * ========================================================================= */
void builtin_plugins_register_all(void)
{
    /* Left zone */
    plugin_engine_register_builtin("aether-appmenu",    &_api_appmenu);
    plugin_engine_register_builtin("aether-clipboard",  &_api_clipboard);
    plugin_engine_register_builtin("aether-workspaces", &_api_workspaces);

    /* Center zone */
    plugin_engine_register_builtin("aether-clock",      &_api_clock);

    /* Right zone */
    plugin_engine_register_builtin("aether-sni-tray",   &_api_sni_tray);
    plugin_engine_register_builtin("aether-keyboard",   &_api_keyboard);
    plugin_engine_register_builtin("aether-wifi",       &_api_wifi);
    plugin_engine_register_builtin("aether-bt",         &_api_bt);
    plugin_engine_register_builtin("aether-mic",        &_api_mic);
    plugin_engine_register_builtin("aether-volume",     &_api_volume);
    plugin_engine_register_builtin("aether-battery",    &_api_battery);
    plugin_engine_register_builtin("aether-search",     &_api_search);
    plugin_engine_register_builtin("aether-notifs",     &_api_notifs);
    plugin_engine_register_builtin("aether-cc",         &_api_cc);

    g_debug("[BuiltinPlugins] All built-in plugins registered");
}
