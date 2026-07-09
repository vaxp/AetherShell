#include "battery_backend.h"
#include <stdio.h>

#define UPOWER_BUS "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower/devices/DisplayDevice"
#define UPOWER_IFACE "org.freedesktop.UPower.Device"

static GDBusProxy *upower_proxy = NULL;
static BatteryInfo battery_info = {0};
static void (*update_cb)(const BatteryInfo *info) = NULL;

static void update_battery_info(GVariant *properties) {
    if (!properties) return;

    GVariant *value;

    value = g_variant_lookup_value(properties, "Percentage", G_VARIANT_TYPE_DOUBLE);
    if (value) { battery_info.percentage = g_variant_get_double(value); g_variant_unref(value); }

    value = g_variant_lookup_value(properties, "State", G_VARIANT_TYPE_UINT32);
    if (value) { battery_info.state = g_variant_get_uint32(value); g_variant_unref(value); }

    value = g_variant_lookup_value(properties, "Energy", G_VARIANT_TYPE_DOUBLE);
    if (value) { battery_info.energy = g_variant_get_double(value); g_variant_unref(value); }

    value = g_variant_lookup_value(properties, "EnergyFull", G_VARIANT_TYPE_DOUBLE);
    if (value) { battery_info.energy_full = g_variant_get_double(value); g_variant_unref(value); }

    value = g_variant_lookup_value(properties, "EnergyFullDesign", G_VARIANT_TYPE_DOUBLE);
    if (value) { battery_info.energy_full_design = g_variant_get_double(value); g_variant_unref(value); }

    value = g_variant_lookup_value(properties, "Capacity", G_VARIANT_TYPE_DOUBLE);
    if (value) { battery_info.capacity = g_variant_get_double(value); g_variant_unref(value); }

    value = g_variant_lookup_value(properties, "UpdateTime", G_VARIANT_TYPE_UINT64);
    if (value) { battery_info.update_time = g_variant_get_uint64(value); g_variant_unref(value); }

    if (update_cb) {
        update_cb(&battery_info);
    }
}

static void fetch_initial_battery_state(void) {
    if (!upower_proxy) return;

    GVariant *result = g_dbus_proxy_call_sync(upower_proxy,
                                    "org.freedesktop.DBus.Properties.GetAll",
                                    g_variant_new("(s)", UPOWER_IFACE),
                                    G_DBUS_CALL_FLAGS_NONE,
                                    -1,
                                    NULL,
                                    NULL);
    if (!result) return;

    GVariant *props = g_variant_get_child_value(result, 0);
    update_battery_info(props);
    g_variant_unref(props);
    g_variant_unref(result);
}

static void on_upower_properties_changed(GDBusProxy *proxy,
                                         GVariant *changed_properties,
                                         GStrv invalidated_properties,
                                         gpointer user_data) {
    (void)proxy; (void)invalidated_properties; (void)user_data;
    update_battery_info(changed_properties);
}

static void on_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    (void)source_object; (void)user_data;

    upower_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
    if (error) {
        g_printerr("Error creating UPower proxy: %s\n", error->message);
        g_error_free(error);
        return;
    }

    g_signal_connect(upower_proxy, "g-properties-changed", G_CALLBACK(on_upower_properties_changed), NULL);
    fetch_initial_battery_state();
}

void battery_backend_init(void (*on_update)(const BatteryInfo *info)) {
    update_cb = on_update;

    g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM,
                             G_DBUS_PROXY_FLAGS_NONE,
                             NULL,
                             UPOWER_BUS,
                             UPOWER_PATH,
                             UPOWER_IFACE,
                             NULL,
                             on_proxy_ready,
                             NULL);
}

const BatteryInfo* battery_backend_get_info(void) {
    return &battery_info;
}
