#include <gtk/gtk.h>

#include "panel-builtins.h"
#include "panel-control-center.h"
#include "panel-notification-center.h"
#include "panel-power.h"
#include "panel-tray.h"
#include "panel-app-menu.h"

#include "clock-widget.h"
#include "system-icons.h"
#include "venom_kb_indicator.h"
#include "workspaces-widget.h"
#include "volume-indicator.h"
#include "mic-indicator.h"
#include "wifi-indicator.h"
#include "bt-indicator.h"

GtkWidget *panel_builtin_create(const char *name)
{
    if (!name) return NULL;
    if (g_strcmp0(name, "app-menu") == 0) return panel_app_menu_button_new();
    if (g_strcmp0(name, "tray") == 0) return panel_tray_widget_new();
    if (g_strcmp0(name, "power") == 0) return panel_power_widget_new();
    if (g_strcmp0(name, "system-icons") == 0) return create_system_icons();
    if (g_strcmp0(name, "kb-indicator") == 0) return create_kb_indicator_widget();
    if (g_strcmp0(name, "clock") == 0) return create_clock_widget();
    if (g_strcmp0(name, "control-center") == 0) return panel_control_center_button_new();
    if (g_strcmp0(name, "notification-center") == 0) return panel_notification_center_button_new();
    if (g_strcmp0(name, "workspaces") == 0) return create_workspaces_widget();
    if (g_strcmp0(name, "volume") == 0) return create_volume_indicator_widget();
    if (g_strcmp0(name, "mic") == 0) return create_mic_indicator_widget();
    if (g_strcmp0(name, "wifi") == 0) return create_wifi_indicator_widget();
    if (g_strcmp0(name, "bluetooth") == 0) return create_bt_indicator_widget();
    return NULL;
}

void panel_builtins_prepare_reload(void)
{
    panel_tray_prepare_reload();
    panel_power_prepare_reload();
}

void panel_builtins_cleanup(void)
{
    panel_app_menu_cleanup();
    panel_tray_cleanup();
    panel_power_cleanup();
    panel_control_center_cleanup();
    panel_notification_center_cleanup();
}
