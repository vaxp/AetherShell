#include "sni_backend.h"

#include <gio/gio.h>
#include <libdbusmenu-glib/dbusmenu-glib.h>
#include <string.h>
#include <unistd.h>

#define SNI_WATCHER_NAME  "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH  "/StatusNotifierWatcher"
#define SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define SNI_ITEM_IFACE    "org.kde.StatusNotifierItem"
#define DBUS_PROPS_IFACE  "org.freedesktop.DBus.Properties"
#define SNI_LAZY_LOAD_DELAY_MS 1200

struct _SniItem {
    gchar *service;
    gchar *bus_name;
    gchar *object_path;
    gchar *menu_path;

    GDBusProxy *proxy;
    DbusmenuClient *menu_client;

    guint signal_handler_id;
};

static SniBackendCallbacks ui_callbacks = {0};
static gpointer ui_user_data = NULL;

static GHashTable *tray_items = NULL; /* full service => SniItem */
static GHashTable *watcher_items = NULL; /* full service => watch id */
static GHashTable *watcher_hosts = NULL; /* host service => watch id */

static GDBusConnection *session_bus = NULL;
static GDBusProxy *watcher_proxy = NULL;
static GQueue *pending_item_queue = NULL;

static guint watcher_name_id = 0;
static guint watcher_object_id = 0;
static guint watcher_name_watch_id = 0;
static guint host_name_id = 0;
static guint tray_init_timeout_id = 0;
static guint pending_item_source_id = 0;

static gchar *host_name = NULL;
static guint host_counter = 0;
static gboolean tray_backend_started = FALSE;

static const gchar watcher_xml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg direction='in' name='service' type='s'/>"
    "    </method>"
    "    <method name='RegisterStatusNotifierHost'>"
    "      <arg direction='in' name='service' type='s'/>"
    "    </method>"
    "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "    <property name='ProtocolVersion' type='i' access='read'/>"
    "    <signal name='StatusNotifierItemRegistered'>"
    "      <arg name='service' type='s'/>"
    "    </signal>"
    "    <signal name='StatusNotifierItemUnregistered'>"
    "      <arg name='service' type='s'/>"
    "    </signal>"
    "    <signal name='StatusNotifierHostRegistered'/>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *watcher_node_info = NULL;

static void refresh_registered_items_property(void);
static void tray_add_item(const gchar *service);
static void tray_remove_item(const gchar *service);
static void ensure_host_registered(void);
static gboolean start_tray_backend(gpointer user_data);
static void enqueue_tray_item(const gchar *service);
static gboolean process_pending_items(gpointer user_data);
static void on_watcher_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data);

static void bus_watch_id_free(gpointer data) {
    guint watch_id = GPOINTER_TO_UINT(data);
    if (watch_id) g_bus_unwatch_name(watch_id);
}

static gchar* parse_bus_name(const gchar *service) {
    const gchar *slash = strchr(service, '/');
    return slash ? g_strndup(service, slash - service) : g_strdup(service);
}

static gchar* parse_object_path(const gchar *service) {
    const gchar *slash = strchr(service, '/');
    return slash ? g_strdup(slash) : g_strdup("/StatusNotifierItem");
}

static gchar* build_full_service_name(const gchar *sender, const gchar *service) {
    if (!service || !*service) return NULL;
    if (service[0] == '/') return g_strconcat(sender, service, NULL);
    if (strchr(service, '/')) return g_strdup(service);
    return g_strconcat(service, "/StatusNotifierItem", NULL);
}

static GVariant* get_cached_or_fetched_property(SniItem *item, const gchar *property_name) {
    if (!item || !item->proxy) return NULL;
    GVariant *value = g_dbus_proxy_get_cached_property(item->proxy, property_name);
    if (value) return value;

    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(
        item->proxy, "org.freedesktop.DBus.Properties.Get",
        g_variant_new("(ss)", SNI_ITEM_IFACE, property_name),
        G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &error);

    if (!result) {
        if (error) g_error_free(error);
        return NULL;
    }
    GVariant *inner = NULL;
    g_variant_get(result, "(v)", &inner);
    g_dbus_proxy_set_cached_property(item->proxy, property_name, inner);
    g_variant_unref(result);
    return inner;
}

static SniIconData* icon_data_from_sni_data(const guchar *data, gsize len, gint w, gint h) {
    if (!data || w <= 0 || h <= 0) return NULL;
    gsize expected = (gsize)w * (gsize)h * 4;
    if (len < expected) return NULL;

    guchar *rgba = g_new(guchar, expected);
    for (gsize i = 0; i < expected; i += 4) {
        guchar a = data[i];
        rgba[i]     = data[i + 1];
        rgba[i + 1] = data[i + 2];
        rgba[i + 2] = data[i + 3];
        rgba[i + 3] = a;
    }
    
    SniIconData *icon = g_new(SniIconData, 1);
    icon->width = w;
    icon->height = h;
    icon->rowstride = w * 4;
    icon->data = rgba;
    return icon;
}

static SniIconData* icon_data_from_variant(GVariant *variant) {
    if (!variant) return NULL;
    GVariantIter iter;
    GVariant *child = NULL;
    gint best_area = -1;
    SniIconData *best = NULL;

    g_variant_iter_init(&iter, variant);
    while ((child = g_variant_iter_next_value(&iter))) {
        gint width = 0, height = 0;
        GVariant *bytes = NULL;
        g_variant_get(child, "(ii@ay)", &width, &height, &bytes);
        if (bytes) {
            gsize len = 0;
            const guchar *data = g_variant_get_fixed_array(bytes, &len, sizeof(guchar));
            SniIconData *icon = icon_data_from_sni_data(data, len, width, height);
            gint area = width * height;
            if (icon && area > best_area) {
                if (best) {
                    g_free(best->data);
                    g_free(best);
                }
                best = icon;
                best_area = area;
            } else if (icon) {
                g_free(icon->data);
                g_free(icon);
            }
            g_variant_unref(bytes);
        }
        g_variant_unref(child);
    }
    return best;
}

static void sni_item_free(gpointer data) {
    SniItem *item = data;
    if (!item) return;

    if (item->signal_handler_id && item->proxy) {
        g_signal_handler_disconnect(item->proxy, item->signal_handler_id);
    }
    g_clear_object(&item->proxy);
    g_clear_object(&item->menu_client);

    g_free(item->service);
    g_free(item->bus_name);
    g_free(item->object_path);
    g_free(item->menu_path);
    g_free(item);
}

static void setup_item_menu(SniItem *item) {
    GVariant *menu = get_cached_or_fetched_property(item, "Menu");
    if (!menu) return;

    const gchar *menu_path = g_variant_get_string(menu, NULL);
    if (menu_path && *menu_path) {
        g_free(item->menu_path);
        item->menu_path = g_strdup(menu_path);
        g_clear_object(&item->menu_client);
        item->menu_client = dbusmenu_client_new(item->bus_name, item->menu_path);
    }
    g_variant_unref(menu);
}

static void handle_item_signal(GDBusProxy *proxy, const gchar *sender_name,
    const gchar *signal_name, GVariant *parameters, gpointer user_data) {
    SniItem *item = user_data;
    (void)proxy; (void)sender_name; (void)parameters;

    if (!item || !signal_name) return;

    if (g_strcmp0(signal_name, "NewMenu") == 0) {
        setup_item_menu(item);
    }
    
    if (ui_callbacks.on_item_updated) {
        ui_callbacks.on_item_updated(item, ui_user_data);
    }
}

static void tray_add_item(const gchar *service) {
    if (!service || !*service) return;

    if (!tray_items) {
        tray_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, sni_item_free);
    }

    if (g_hash_table_contains(tray_items, service)) return;

    SniItem *item = g_new0(SniItem, 1);
    item->service = g_strdup(service);
    item->bus_name = parse_bus_name(service);
    item->object_path = parse_object_path(service);

    GError *error = NULL;
    item->proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
        item->bus_name, item->object_path, SNI_ITEM_IFACE, NULL, &error);

    if (!item->proxy) {
        if (error) g_error_free(error);
        sni_item_free(item);
        return;
    }

    item->signal_handler_id = g_signal_connect(item->proxy, "g-signal", G_CALLBACK(handle_item_signal), item);
    setup_item_menu(item);

    g_hash_table_insert(tray_items, g_strdup(item->service), item);

    if (ui_callbacks.on_item_added) {
        ui_callbacks.on_item_added(item, ui_user_data);
    }
}

static void tray_remove_item(const gchar *service) {
    if (!tray_items || !service) return;

    if (g_hash_table_contains(tray_items, service)) {
        if (ui_callbacks.on_item_removed) {
            ui_callbacks.on_item_removed(service, ui_user_data);
        }
        g_hash_table_remove(tray_items, service);
    }
}

static void enqueue_tray_item(const gchar *service) {
    if (!service || !*service) return;
    if (!pending_item_queue) pending_item_queue = g_queue_new();

    for (GList *it = pending_item_queue->head; it; it = it->next) {
        if (g_strcmp0((const gchar*)it->data, service) == 0) return;
    }

    if (tray_items && g_hash_table_contains(tray_items, service)) return;

    g_queue_push_tail(pending_item_queue, g_strdup(service));
    if (!pending_item_source_id) {
        pending_item_source_id = g_idle_add(process_pending_items, NULL);
    }
}

static gboolean process_pending_items(gpointer user_data) {
    (void)user_data;
    if (!pending_item_queue || g_queue_is_empty(pending_item_queue)) {
        pending_item_source_id = 0;
        return G_SOURCE_REMOVE;
    }

    gchar *service = g_queue_pop_head(pending_item_queue);
    tray_add_item(service);
    g_free(service);

    if (g_queue_is_empty(pending_item_queue)) {
        pending_item_source_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void add_registered_items_from_variant(GVariant *items) {
    if (!items) return;
    GVariantIter iter;
    const gchar *service = NULL;
    g_variant_iter_init(&iter, items);
    while (g_variant_iter_next(&iter, "&s", &service)) {
        enqueue_tray_item(service);
    }
}

static void on_watcher_proxy_signal(GDBusProxy *proxy, const gchar *sender_name,
    const gchar *signal_name, GVariant *parameters, gpointer user_data) {
    const gchar *service = NULL;
    (void)proxy; (void)sender_name; (void)user_data;

    if (g_strcmp0(signal_name, "StatusNotifierItemRegistered") == 0) {
        g_variant_get(parameters, "(&s)", &service);
        enqueue_tray_item(service);
    } else if (g_strcmp0(signal_name, "StatusNotifierItemUnregistered") == 0) {
        g_variant_get(parameters, "(&s)", &service);
        tray_remove_item(service);
    }
}

static void ensure_host_registered(void) {
    if (!watcher_proxy || !host_name) return;
    g_dbus_proxy_call(watcher_proxy, "RegisterStatusNotifierHost",
        g_variant_new("(s)", host_name), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void on_host_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)connection; (void)name; (void)user_data;
    ensure_host_registered();
}

static void watcher_item_vanished(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    gchar *service = user_data;
    (void)connection; (void)name;
    if (watcher_items) g_hash_table_remove(watcher_items, service);
    tray_remove_item(service);
    if (session_bus) {
        g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
            "StatusNotifierItemUnregistered", g_variant_new("(s)", service), NULL);
        refresh_registered_items_property();
    }
}

static void watcher_host_vanished(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    gchar *service = user_data;
    (void)connection; (void)name;
    if (watcher_hosts) g_hash_table_remove(watcher_hosts, service);
    refresh_registered_items_property();
}

static void emit_properties_changed(const gchar *property_name, GVariant *value) {
    GVariantBuilder changed_builder;
    GVariantBuilder invalidated_builder;

    g_variant_builder_init(&changed_builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed_builder, "{sv}", property_name, value);
    g_variant_builder_init(&invalidated_builder, G_VARIANT_TYPE("as"));

    g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, DBUS_PROPS_IFACE,
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)", SNI_WATCHER_IFACE, &changed_builder, &invalidated_builder),
        NULL);
}

static void refresh_registered_items_property(void) {
    if (!session_bus) return;
    GVariantBuilder items_builder;
    g_variant_builder_init(&items_builder, G_VARIANT_TYPE("as"));

    if (watcher_items) {
        GHashTableIter iter;
        gpointer key = NULL;
        gpointer value = NULL;
        g_hash_table_iter_init(&iter, watcher_items);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            g_variant_builder_add(&items_builder, "s", (const gchar*)key);
        }
    }

    emit_properties_changed("RegisteredStatusNotifierItems", g_variant_builder_end(&items_builder));
    emit_properties_changed("IsStatusNotifierHostRegistered",
        g_variant_new_boolean(watcher_hosts && g_hash_table_size(watcher_hosts) > 0));
}

static void watcher_register_item(const gchar *sender, const gchar *service) {
    gchar *full_service = build_full_service_name(sender, service);
    if (!full_service) return;

    if (!watcher_items) watcher_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bus_watch_id_free);

    if (!g_hash_table_contains(watcher_items, full_service)) {
        gchar *bus_name = parse_bus_name(full_service);
        guint watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, bus_name,
            G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, watcher_item_vanished, g_strdup(full_service), g_free);
        g_hash_table_insert(watcher_items, g_strdup(full_service), GUINT_TO_POINTER(watch_id));

        g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
            "StatusNotifierItemRegistered", g_variant_new("(s)", full_service), NULL);
        refresh_registered_items_property();
        enqueue_tray_item(full_service);
        g_free(bus_name);
    }
    g_free(full_service);
}

static void watcher_register_host(const gchar *service) {
    if (!service || !*service) return;
    if (!watcher_hosts) watcher_hosts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, bus_watch_id_free);

    if (!g_hash_table_contains(watcher_hosts, service)) {
        guint watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, service,
            G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, watcher_host_vanished, g_strdup(service), g_free);
        g_hash_table_insert(watcher_hosts, g_strdup(service), GUINT_TO_POINTER(watch_id));

        g_dbus_connection_emit_signal(session_bus, NULL, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
            "StatusNotifierHostRegistered", NULL, NULL);
        refresh_registered_items_property();
    }
}

static GVariant* watcher_handle_get_property(const gchar *property_name) {
    if (g_strcmp0(property_name, "RegisteredStatusNotifierItems") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        if (watcher_items) {
            GHashTableIter iter;
            gpointer key = NULL;
            gpointer value = NULL;
            g_hash_table_iter_init(&iter, watcher_items);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                g_variant_builder_add(&builder, "s", (const gchar*)key);
            }
        }
        return g_variant_builder_end(&builder);
    }
    if (g_strcmp0(property_name, "IsStatusNotifierHostRegistered") == 0) {
        return g_variant_new_boolean(watcher_hosts && g_hash_table_size(watcher_hosts) > 0);
    }
    if (g_strcmp0(property_name, "ProtocolVersion") == 0) {
        return g_variant_new_int32(0);
    }
    return NULL;
}

static void watcher_method_call(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *method_name,
    GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
    gchar *service = NULL;
    (void)connection; (void)object_path; (void)interface_name; (void)user_data;

    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(s)"))) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.freedesktop.DBus.Error.InvalidArgs", "Expected a single string argument.");
        return;
    }

    g_variant_get(parameters, "(&s)", &service);

    if (g_strcmp0(method_name, "RegisterStatusNotifierItem") == 0) {
        watcher_register_item(sender, service);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    if (g_strcmp0(method_name, "RegisterStatusNotifierHost") == 0) {
        watcher_register_host(service);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    g_dbus_method_invocation_return_dbus_error(invocation,
        "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method.");
}

static GVariant* watcher_get_property(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *property_name,
    GError **error, gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)error; (void)user_data;
    return watcher_handle_get_property(property_name);
}

static const GDBusInterfaceVTable watcher_vtable = {
    watcher_method_call,
    watcher_get_property,
    NULL,
};

static void on_watcher_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name; (void)user_data;
    session_bus = g_object_ref(connection);
    if (!watcher_node_info) {
        GError *error = NULL;
        watcher_node_info = g_dbus_node_info_new_for_xml(watcher_xml, &error);
        if (!watcher_node_info) {
            if (error) g_error_free(error);
            return;
        }
    }
    watcher_object_id = g_dbus_connection_register_object(connection, SNI_WATCHER_PATH,
        watcher_node_info->interfaces[0], &watcher_vtable, NULL, NULL, NULL);
}

static void clear_watcher_proxy(void) {
    if (watcher_proxy) {
        g_signal_handlers_disconnect_by_func(watcher_proxy, G_CALLBACK(on_watcher_proxy_signal), NULL);
        g_object_unref(watcher_proxy);
        watcher_proxy = NULL;
    }
}

static void on_watcher_name_appeared(GDBusConnection *connection, const gchar *name,
    const gchar *owner, gpointer user_data) {
    (void)connection; (void)name; (void)owner; (void)user_data;
    clear_watcher_proxy();
    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
        SNI_WATCHER_NAME, SNI_WATCHER_PATH, SNI_WATCHER_IFACE,
        NULL, on_watcher_proxy_ready, NULL);
}

static void on_watcher_proxy_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    GVariant *items = NULL;
    (void)source_object; (void)user_data;

    watcher_proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
    if (!watcher_proxy) {
        if (error) g_error_free(error);
        return;
    }

    g_signal_connect(watcher_proxy, "g-signal", G_CALLBACK(on_watcher_proxy_signal), NULL);
    ensure_host_registered();

    items = g_dbus_proxy_get_cached_property(watcher_proxy, "RegisteredStatusNotifierItems");
    add_registered_items_from_variant(items);
    if (items) g_variant_unref(items);
}

static void on_watcher_name_vanished(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)connection; (void)name; (void)user_data;
    clear_watcher_proxy();
    if (tray_items) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, tray_items);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (ui_callbacks.on_item_removed) {
                ui_callbacks.on_item_removed((const gchar *)key, ui_user_data);
            }
        }
        g_hash_table_remove_all(tray_items);
    }
}

static gboolean start_tray_backend(gpointer user_data) {
    (void)user_data;
    tray_init_timeout_id = 0;
    if (tray_backend_started) return G_SOURCE_REMOVE;
    tray_backend_started = TRUE;

    if (!watcher_name_id) {
        watcher_name_id = g_bus_own_name(G_BUS_TYPE_SESSION, SNI_WATCHER_NAME,
            G_BUS_NAME_OWNER_FLAGS_NONE, on_watcher_bus_acquired, NULL, NULL, NULL, NULL);
    }
    if (!host_name_id) {
        host_name_id = g_bus_own_name(G_BUS_TYPE_SESSION, host_name,
            G_BUS_NAME_OWNER_FLAGS_NONE, on_host_name_acquired, NULL, NULL, NULL, NULL);
    }
    if (!watcher_name_watch_id) {
        watcher_name_watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION, SNI_WATCHER_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_watcher_name_appeared, on_watcher_name_vanished, NULL, NULL);
    }
    return G_SOURCE_REMOVE;
}

void sni_backend_init(const SniBackendCallbacks *callbacks, gpointer user_data) {
    if (callbacks) ui_callbacks = *callbacks;
    ui_user_data = user_data;

    if (!tray_items) tray_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, sni_item_free);
    if (!host_name) host_name = g_strdup_printf("org.kde.StatusNotifierHost-%d-%u", getpid(), ++host_counter);

    if (!tray_init_timeout_id) {
        tray_init_timeout_id = g_timeout_add(SNI_LAZY_LOAD_DELAY_MS, start_tray_backend, NULL);
    }
}

void sni_item_activate(SniItem *item, int x, int y) {
    if (!item || !item->proxy) return;
    g_dbus_proxy_call(item->proxy, "Activate", g_variant_new("(ii)", x, y),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

void sni_item_secondary_activate(SniItem *item, int x, int y) {
    if (!item || !item->proxy) return;
    g_dbus_proxy_call(item->proxy, "SecondaryActivate", g_variant_new("(ii)", x, y),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

void sni_item_context_menu(SniItem *item, int x, int y) {
    if (!item || !item->proxy) return;
    g_dbus_proxy_call(item->proxy, "ContextMenu", g_variant_new("(ii)", x, y),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

void sni_item_scroll(SniItem *item, int delta, const char *orientation) {
    if (!item || !item->proxy) return;
    g_dbus_proxy_call(item->proxy, "Scroll", g_variant_new("(is)", delta, orientation),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

const char* sni_item_get_service(SniItem *item) {
    return item ? item->service : NULL;
}

const char* sni_item_get_title(SniItem *item) {
    static gchar title_buffer[256];
    if (!item) return NULL;
    GVariant *title = get_cached_or_fetched_property(item, "Title");
    const char *text = item->service;
    if (title) {
        const char *t = g_variant_get_string(title, NULL);
        if (t && *t) {
            g_strlcpy(title_buffer, t, sizeof(title_buffer));
            text = title_buffer;
        }
        g_variant_unref(title);
    }
    return text;
}

const char* sni_item_get_icon_name(SniItem *item) {
    static gchar name_buffer[256];
    if (!item) return NULL;

    GVariant *status = get_cached_or_fetched_property(item, "Status");
    const gchar *status_value = status ? g_variant_get_string(status, NULL) : "";
    const gchar *icon_prefix = g_strcmp0(status_value, "NeedsAttention") == 0 ? "AttentionIcon" : "Icon";
    gchar *name_prop = g_strdup_printf("%sName", icon_prefix);

    GVariant *icon_name = get_cached_or_fetched_property(item, name_prop);
    const char *ret = NULL;
    if (icon_name) {
        g_strlcpy(name_buffer, g_variant_get_string(icon_name, NULL), sizeof(name_buffer));
        ret = name_buffer;
        g_variant_unref(icon_name);
    }
    
    if (status) g_variant_unref(status);
    g_free(name_prop);
    return ret;
}

SniIconData* sni_item_get_icon_data(SniItem *item) {
    if (!item) return NULL;

    GVariant *status = get_cached_or_fetched_property(item, "Status");
    const gchar *status_value = status ? g_variant_get_string(status, NULL) : "";
    const gchar *icon_prefix = g_strcmp0(status_value, "NeedsAttention") == 0 ? "AttentionIcon" : "Icon";
    gchar *pixmap_prop = g_strdup_printf("%sPixmap", icon_prefix);

    GVariant *icon_pixmap = get_cached_or_fetched_property(item, pixmap_prop);
    SniIconData *icon = icon_data_from_variant(icon_pixmap);

    if (icon_pixmap) g_variant_unref(icon_pixmap);
    if (status) g_variant_unref(status);
    g_free(pixmap_prop);

    return icon;
}

void sni_icon_data_free(SniIconData *icon_data) {
    if (icon_data) {
        g_free(icon_data->data);
        g_free(icon_data);
    }
}

gboolean sni_item_is_passive(SniItem *item) {
    if (!item) return FALSE;
    gboolean passive = FALSE;
    GVariant *status = get_cached_or_fetched_property(item, "Status");
    if (status) {
        passive = g_strcmp0(g_variant_get_string(status, NULL), "Passive") == 0;
        g_variant_unref(status);
    }
    return passive;
}

gboolean sni_item_is_menu(SniItem *item) {
    if (!item) return FALSE;
    gboolean is_menu = FALSE;
    GVariant *prop = get_cached_or_fetched_property(item, "ItemIsMenu");
    if (prop) {
        is_menu = g_variant_get_boolean(prop);
        g_variant_unref(prop);
    }
    return is_menu;
}

gpointer sni_item_get_dbusmenu_root(SniItem *item) {
    if (!item || !item->menu_client) return NULL;
    return dbusmenu_client_get_root(item->menu_client);
}
