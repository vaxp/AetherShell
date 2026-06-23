#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <gio/gio.h>

AppConfig g_config;

static const char *DEFAULT_CONFIG = 
"[Notify]\n"
"bg_color=rgba(0,0,0,0.392)\n"
"border_color=rgba(0,255,255,0.8)\n"
"title_text_color=#00FFFF\n"
"body_text_color=#EEEEEE\n"
"btn_bg_color=#222222\n"
"btn_text_color=#00FFFF\n"
"btn_hover_bg_color=#00FFFF\n"
"btn_hover_text_color=#000000\n"
"margin_x=20\n"
"margin_y=50\n"
"spacing=10\n"
"# Valid positions: top-right, top-left, bottom-right, bottom-left, top-center, bottom-center\n"
"position=top-right\n"
"\n"
"[OSD]\n"
"bg_color=rgba(0,0,0,0.3)\n"
"text_color=#00FFFF\n"
"bar_bg_color=rgba(76,76,76,1.0)\n"
"bar_fg_color=#00FFFF\n"
"icon_normal_color=#FFFFFF\n"
"icon_muted_color=#FF3333\n"
"# Valid positions: center, top, bottom\n"
"position=center\n"
"\n"
"[Sounds]\n"
"notification=\n"
"charger_connect=\n"
"charger_disconnect=\n"
"usb_connect=\n"
"usb_disconnect=\n"
"limit_high=\n"
"limit_low=\n"
"error=\n";

static void parse_color_or_default(GKeyFile *kf, const char *group, const char *key, const char *default_val, GdkRGBA *out_color) {
    gchar *val = g_key_file_get_string(kf, group, key, NULL);
    if (val) {
        if (!gdk_rgba_parse(out_color, val)) {
            gdk_rgba_parse(out_color, default_val);
        }
        g_free(val);
    } else {
        gdk_rgba_parse(out_color, default_val);
    }
}

static int parse_int_or_default(GKeyFile *kf, const char *group, const char *key, int default_val) {
    GError *err = NULL;
    int val = g_key_file_get_integer(kf, group, key, &err);
    if (err) {
        g_error_free(err);
        return default_val;
    }
    return val;
}

static char* parse_string_or_default(GKeyFile *kf, const char *group, const char *key, const char *default_val) {
    gchar *val = g_key_file_get_string(kf, group, key, NULL);
    if (val) {
        return val;
    }
    return g_strdup(default_val);
}

static void create_dir_recursively(const char *dir) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    mkdir(tmp, S_IRWXU);
}

void config_init(void) {
    const gchar *config_dir = g_get_user_config_dir();
    gchar *vaxp_dir = g_build_filename(config_dir, "vaxp", "osd-notify", NULL);
    gchar *config_file = g_build_filename(vaxp_dir, "osd-notify.vaxp", NULL);

    if (!g_file_test(config_file, G_FILE_TEST_EXISTS)) {
        create_dir_recursively(vaxp_dir);
        GError *err = NULL;
        if (!g_file_set_contents(config_file, DEFAULT_CONFIG, -1, &err)) {
            g_printerr("Failed to create default config: %s\n", err->message);
            g_error_free(err);
        }
    }

    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, config_file, G_KEY_FILE_NONE, NULL)) {
        g_printerr("Failed to load config from %s, using defaults.\n", config_file);
        g_key_file_load_from_data(kf, DEFAULT_CONFIG, -1, G_KEY_FILE_NONE, NULL);
    }

    parse_color_or_default(kf, "Notify", "bg_color", "rgba(0,0,0,0.392)", &g_config.notify_bg);
    parse_color_or_default(kf, "Notify", "border_color", "rgba(0,255,255,0.8)", &g_config.notify_border);
    parse_color_or_default(kf, "Notify", "title_text_color", "#00FFFF", &g_config.notify_title_text);
    parse_color_or_default(kf, "Notify", "body_text_color", "#EEEEEE", &g_config.notify_body_text);
    parse_color_or_default(kf, "Notify", "btn_bg_color", "#222222", &g_config.notify_btn_bg);
    parse_color_or_default(kf, "Notify", "btn_text_color", "#00FFFF", &g_config.notify_btn_text);
    parse_color_or_default(kf, "Notify", "btn_hover_bg_color", "#00FFFF", &g_config.notify_btn_hover_bg);
    parse_color_or_default(kf, "Notify", "btn_hover_text_color", "#000000", &g_config.notify_btn_hover_text);
    g_config.notify_margin_x = parse_int_or_default(kf, "Notify", "margin_x", 20);
    g_config.notify_margin_y = parse_int_or_default(kf, "Notify", "margin_y", 50);
    g_config.notify_spacing = parse_int_or_default(kf, "Notify", "spacing", 10);
    if (g_config.notify_position) g_free(g_config.notify_position);
    g_config.notify_position = parse_string_or_default(kf, "Notify", "position", "top-right");

    parse_color_or_default(kf, "OSD", "bg_color", "rgba(0,0,0,0.3)", &g_config.osd_bg);
    parse_color_or_default(kf, "OSD", "text_color", "#00FFFF", &g_config.osd_text);
    parse_color_or_default(kf, "OSD", "bar_bg_color", "rgba(76,76,76,1.0)", &g_config.osd_bar_bg);
    parse_color_or_default(kf, "OSD", "bar_fg_color", "#00FFFF", &g_config.osd_bar_fg);
    parse_color_or_default(kf, "OSD", "icon_normal_color", "#FFFFFF", &g_config.osd_icon_normal);
    parse_color_or_default(kf, "OSD", "icon_muted_color", "#FF3333", &g_config.osd_icon_muted);
    if (g_config.osd_position) g_free(g_config.osd_position);
    g_config.osd_position = parse_string_or_default(kf, "OSD", "position", "center");

    if (g_config.sound_notification) g_free(g_config.sound_notification);
    g_config.sound_notification = parse_string_or_default(kf, "Sounds", "notification", "");

    if (g_config.sound_charger_connect) g_free(g_config.sound_charger_connect);
    g_config.sound_charger_connect = parse_string_or_default(kf, "Sounds", "charger_connect", "");

    if (g_config.sound_charger_disconnect) g_free(g_config.sound_charger_disconnect);
    g_config.sound_charger_disconnect = parse_string_or_default(kf, "Sounds", "charger_disconnect", "");

    if (g_config.sound_usb_connect) g_free(g_config.sound_usb_connect);
    g_config.sound_usb_connect = parse_string_or_default(kf, "Sounds", "usb_connect", "");

    if (g_config.sound_usb_disconnect) g_free(g_config.sound_usb_disconnect);
    g_config.sound_usb_disconnect = parse_string_or_default(kf, "Sounds", "usb_disconnect", "");

    if (g_config.sound_limit_high) g_free(g_config.sound_limit_high);
    g_config.sound_limit_high = parse_string_or_default(kf, "Sounds", "limit_high", "");

    if (g_config.sound_limit_low) g_free(g_config.sound_limit_low);
    g_config.sound_limit_low = parse_string_or_default(kf, "Sounds", "limit_low", "");

    if (g_config.sound_error) g_free(g_config.sound_error);
    g_config.sound_error = parse_string_or_default(kf, "Sounds", "error", "");

    g_key_file_free(kf);
    g_free(config_file);
    g_free(vaxp_dir);
}

void config_free(void) {
    if (g_config.notify_position) g_free(g_config.notify_position);
    if (g_config.osd_position) g_free(g_config.osd_position);
    if (g_config.sound_notification) g_free(g_config.sound_notification);
    if (g_config.sound_charger_connect) g_free(g_config.sound_charger_connect);
    if (g_config.sound_charger_disconnect) g_free(g_config.sound_charger_disconnect);
    if (g_config.sound_usb_connect) g_free(g_config.sound_usb_connect);
    if (g_config.sound_usb_disconnect) g_free(g_config.sound_usb_disconnect);
    if (g_config.sound_limit_high) g_free(g_config.sound_limit_high);
    if (g_config.sound_limit_low) g_free(g_config.sound_limit_low);
    if (g_config.sound_error) g_free(g_config.sound_error);
}

static ConfigReloadCallback reload_cb = NULL;
static GFileMonitor *config_monitor = NULL;

static void on_config_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor; (void)file; (void)other_file; (void)user_data;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event_type == G_FILE_MONITOR_EVENT_CREATED) {
        config_init();
        if (reload_cb) reload_cb();
    }
}

void config_monitor_init(ConfigReloadCallback cb) {
    reload_cb = cb;
    const gchar *config_dir = g_get_user_config_dir();
    gchar *vaxp_dir = g_build_filename(config_dir, "vaxp", "osd-notify", NULL);
    gchar *config_file = g_build_filename(vaxp_dir, "osd-notify.vaxp", NULL);

    GFile *gfile = g_file_new_for_path(config_file);
    config_monitor = g_file_monitor_file(gfile, G_FILE_MONITOR_NONE, NULL, NULL);
    if (config_monitor) {
        g_signal_connect(config_monitor, "changed", G_CALLBACK(on_config_file_changed), NULL);
    }
    g_object_unref(gfile);
    g_free(config_file);
    g_free(vaxp_dir);
}
