#include "osd_udev.h"
#include "osd_sound.h"
#include <gudev/gudev.h>
#include <glib.h>
#include <stdio.h>

static GUdevClient *udev_client = NULL;
static int prev_online = -1;

static void on_uevent(GUdevClient *client, const gchar *action, GUdevDevice *device, gpointer user_data) {
    (void)client; (void)user_data;
    const gchar *subsystem = g_udev_device_get_subsystem(device);
    const gchar *devtype = g_udev_device_get_devtype(device);
    const gchar *name = g_udev_device_get_name(device);

    if (g_strcmp0(subsystem, "usb") == 0 && g_strcmp0(devtype, "usb_device") == 0) {
        if (g_strcmp0(action, "add") == 0) {
            printf("[UDEV] USB Device Connected: %s\n", name);
            osd_sound_play(OSD_SOUND_USB_CONNECT);
        } else if (g_strcmp0(action, "remove") == 0) {
            printf("[UDEV] USB Device Disconnected: %s\n", name);
            osd_sound_play(OSD_SOUND_USB_DISCONNECT);
        }
    } else if (g_strcmp0(subsystem, "power_supply") == 0) {
        if (g_str_has_prefix(name, "AC") || g_str_has_prefix(name, "ADP") || g_strcmp0(name, "ADP0") == 0) {
            gboolean online = g_udev_device_get_sysfs_attr_as_int(device, "online");
            int online_int = online ? 1 : 0;
            if (online_int != prev_online) {
                prev_online = online_int;
                if (online_int == 1) {
                    printf("[UDEV] Charger Connected (online = %d)\n", online_int);
                    osd_sound_play(OSD_SOUND_CHARGER_CONNECT);
                } else {
                    printf("[UDEV] Charger Disconnected (online = %d)\n", online_int);
                    osd_sound_play(OSD_SOUND_CHARGER_DISCONNECT);
                }
            }
        }
    }
}

void osd_udev_init(void) {
    const gchar *subsystems[] = {"usb", "power_supply", NULL};
    udev_client = g_udev_client_new(subsystems);
    if (!udev_client) {
        printf("[UDEV] Failed to initialize GUdevClient\n");
        return;
    }

    g_signal_connect(udev_client, "uevent", G_CALLBACK(on_uevent), NULL);

    // Initialize initial charger state
    GList *devices = g_udev_client_query_by_subsystem(udev_client, "power_supply");
    for (GList *l = devices; l != NULL; l = l->next) {
        GUdevDevice *device = G_UDEV_DEVICE(l->data);
        const gchar *name = g_udev_device_get_name(device);
        if (g_str_has_prefix(name, "AC") || g_str_has_prefix(name, "ADP") || g_strcmp0(name, "ADP0") == 0) {
            prev_online = g_udev_device_get_sysfs_attr_as_int(device, "online") ? 1 : 0;
            printf("[UDEV] Initial charger state: online = %d\n", prev_online);
            break;
        }
    }
    g_list_free_full(devices, g_object_unref);
}
