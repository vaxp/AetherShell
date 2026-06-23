#ifndef OSD_NOTIFY_CONFIG_H
#define OSD_NOTIFY_CONFIG_H

#include <gtk/gtk.h>
#include <glib.h>

typedef struct {
    // Notify Settings
    GdkRGBA notify_bg;
    GdkRGBA notify_border;
    GdkRGBA notify_title_text;
    GdkRGBA notify_body_text;
    GdkRGBA notify_btn_bg;
    GdkRGBA notify_btn_text;
    GdkRGBA notify_btn_hover_bg;
    GdkRGBA notify_btn_hover_text;
    
    int notify_margin_x;
    int notify_margin_y;
    int notify_spacing;
    char *notify_position; // top-right, top-left, bottom-right, bottom-left

    // OSD Settings
    GdkRGBA osd_bg;
    GdkRGBA osd_text;
    GdkRGBA osd_bar_bg;
    GdkRGBA osd_bar_fg;
    GdkRGBA osd_icon_normal;
    GdkRGBA osd_icon_muted;
    
    char *osd_position; // center, top, bottom

    char *sound_notification;
    char *sound_charger_connect;
    char *sound_charger_disconnect;
    char *sound_usb_connect;
    char *sound_usb_disconnect;
    char *sound_limit_high;
    char *sound_limit_low;
    char *sound_error;
} AppConfig;

extern AppConfig g_config;

void config_init(void);
void config_free(void);

typedef void (*ConfigReloadCallback)(void);
void config_monitor_init(ConfigReloadCallback cb);

#endif // OSD_NOTIFY_CONFIG_H
