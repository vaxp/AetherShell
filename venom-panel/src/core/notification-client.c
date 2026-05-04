/*
 * notification-client.c
 *
 * D-Bus client for venom_notify daemon.
 * Connects to org.venom.NotificationHistory interface.
 */

#include <gio/gio.h>
#include "notification-client.h"

#define DBUS_NAME "org.freedesktop.Notifications"
#define DBUS_PATH "/org/freedesktop/Notifications"
#define DBUS_IFACE_HISTORY "org.venom.NotificationHistory"

static GDBusProxy *_history_proxy = NULL;
static guint _signal_subscription = 0;
static guint _init_count = 0;

typedef struct {
    NotificationHistoryCallback cb;
    gpointer user_data;
} HistorySubscriber;

typedef struct {
    DoNotDisturbCallback cb;
    gpointer user_data;
} DndSubscriber;

static GSList *_history_subscribers = NULL;
static GSList *_dnd_subscribers = NULL;

/* =====================================================================
 * Memory Management
 * ===================================================================== */

void notification_item_free(NotificationItem *item) {
    if (item) {
        g_free(item->app_name);
        g_free(item->icon_path);
        g_free(item->summary);
        g_free(item->body);
        g_free(item);
    }
}

void notification_item_list_free(GList *list) {
    g_list_free_full(list, (GDestroyNotify)notification_item_free);
}

/* =====================================================================
 * Signal Handlers
 * ===================================================================== */

static void on_history_signal(GDBusConnection *connection,
                              const gchar *sender_name,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name,
                              GVariant *parameters,
                              gpointer user_data) {
    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)parameters;
    (void)user_data;
    
    if (g_strcmp0(signal_name, "HistoryUpdated") == 0) {
        for (GSList *l = _history_subscribers; l; l = l->next) {
            HistorySubscriber *s = (HistorySubscriber *)l->data;
            if (s && s->cb) s->cb(s->user_data);
        }
    } else if (g_strcmp0(signal_name, "DoNotDisturbChanged") == 0) {
        gboolean enabled = FALSE;
        g_variant_get(parameters, "(b)", &enabled);
        for (GSList *l = _dnd_subscribers; l; l = l->next) {
            DndSubscriber *s = (DndSubscriber *)l->data;
            if (s && s->cb) s->cb(enabled, s->user_data);
        }
    }
}

/* =====================================================================
 * Initialization
 * ===================================================================== */

void notification_client_init(void) {
    _init_count++;
    if (_init_count > 1) return;
    
    GError *error = NULL;
    
    g_print("[NotificationClient] Connecting to %s...\n", DBUS_NAME);
    
    /* Create proxy for history interface */
    _history_proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        DBUS_NAME,
        DBUS_PATH,
        DBUS_IFACE_HISTORY,
        NULL, &error);
    
    if (error) {
        g_print("[NotificationClient] ❌ Failed to connect: %s\n", error->message);
        g_clear_error(&error);
    } else if (_history_proxy) {
        g_print("[NotificationClient] ✅ Connected to venom_notify\n");
        
        /* Subscribe to signals */
        GDBusConnection *conn = g_dbus_proxy_get_connection(_history_proxy);
        _signal_subscription = g_dbus_connection_signal_subscribe(
            conn,
            DBUS_NAME,
            DBUS_IFACE_HISTORY,
            NULL,  /* All signals */
            DBUS_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_history_signal,
            NULL,
            NULL);
    }
    
    /* init_count keeps this alive */
}

void notification_client_cleanup(void) {
    if (_init_count == 0) return;
    _init_count--;
    if (_init_count > 0) return;

    if (_signal_subscription > 0 && _history_proxy) {
        GDBusConnection *conn = g_dbus_proxy_get_connection(_history_proxy);
        g_dbus_connection_signal_unsubscribe(conn, _signal_subscription);
        _signal_subscription = 0;
    }
    
    g_clear_object(&_history_proxy);

    g_slist_free_full(_history_subscribers, g_free);
    _history_subscribers = NULL;
    g_slist_free_full(_dnd_subscribers, g_free);
    _dnd_subscribers = NULL;
}

gboolean notification_client_is_available(void) {
    return _history_proxy != NULL;
}

/* =====================================================================
 * Get History
 * ===================================================================== */

GList* notification_client_get_history(void) {
    if (!_history_proxy) return NULL;
    
    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(
        _history_proxy, "GetHistory", NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    
    if (error) {
        g_warning("[NotificationClient] GetHistory failed: %s", error->message);
        g_error_free(error);
        return NULL;
    }
    
    /* Result is (a(ussss)) - array of (id, app_name, icon, summary, body) */
    GList *items = NULL;
    GVariantIter *iter;
    g_variant_get(result, "(a(ussss))", &iter);
    
    guint32 id;
    const gchar *app_name, *icon, *summary, *body;
    
    while (g_variant_iter_next(iter, "(u&s&s&s&s)", &id, &app_name, &icon, &summary, &body)) {
        NotificationItem *item = g_new0(NotificationItem, 1);
        item->id = id;
        item->app_name = g_strdup(app_name);
        item->icon_path = g_strdup(icon);
        item->summary = g_strdup(summary);
        item->body = g_strdup(body);
        
        items = g_list_append(items, item);
    }
    
    g_variant_iter_free(iter);
    g_variant_unref(result);
    
    g_print("[NotificationClient] Got %d notifications\n", g_list_length(items));
    return items;
}

/* =====================================================================
 * Actions
 * ===================================================================== */

void notification_client_clear_history(void) {
    if (!_history_proxy) return;
    
    GError *error = NULL;
    g_dbus_proxy_call_sync(
        _history_proxy, "ClearHistory", NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    
    if (error) {
        g_warning("[NotificationClient] ClearHistory failed: %s", error->message);
        g_error_free(error);
    } else {
        g_print("[NotificationClient] History cleared\n");
    }
}

void notification_client_remove(guint32 id) {
    if (!_history_proxy) return;
    
    GError *error = NULL;
    g_dbus_proxy_call_sync(
        _history_proxy, "RemoveNotification",
        g_variant_new("(u)", id),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    
    if (error) {
        g_warning("[NotificationClient] RemoveNotification failed: %s", error->message);
        g_error_free(error);
    }
}

/* =====================================================================
 * Do Not Disturb
 * ===================================================================== */

gboolean notification_client_get_dnd(void) {
    if (!_history_proxy) return FALSE;
    
    GError *error = NULL;
    GVariant *result = g_dbus_proxy_call_sync(
        _history_proxy, "GetDoNotDisturb", NULL,
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
    
    if (error) {
        g_warning("[NotificationClient] GetDoNotDisturb failed: %s", error->message);
        g_error_free(error);
        return FALSE;
    }
    
    gboolean enabled = FALSE;
    g_variant_get(result, "(b)", &enabled);
    g_variant_unref(result);
    return enabled;
}

void notification_client_set_dnd(gboolean enabled) {
    if (!_history_proxy) return;
    
    GError *error = NULL;
    g_dbus_proxy_call_sync(
        _history_proxy, "SetDoNotDisturb",
        g_variant_new("(b)", enabled),
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
    
    if (error) {
        g_warning("[NotificationClient] SetDoNotDisturb failed: %s", error->message);
        g_error_free(error);
    } else {
        g_print("[NotificationClient] DND set to: %d\n", enabled);
    }
}

/* =====================================================================
 * Callbacks
 * ===================================================================== */

void notification_client_on_history_update(NotificationHistoryCallback callback, gpointer user_data) {
    if (!callback) {
        if (!user_data) {
            g_slist_free_full(_history_subscribers, g_free);
            _history_subscribers = NULL;
            return;
        }

        GSList *prev = NULL;
        for (GSList *l = _history_subscribers; l;) {
            HistorySubscriber *s = (HistorySubscriber *)l->data;
            GSList *next = l->next;
            if (s && s->user_data == user_data) {
                g_free(s);
                if (prev) prev->next = next;
                else _history_subscribers = next;
                g_slist_free_1(l);
            } else {
                prev = l;
            }
            l = next;
        }
        return;
    }

    for (GSList *l = _history_subscribers; l; l = l->next) {
        HistorySubscriber *s = (HistorySubscriber *)l->data;
        if (s && s->cb == callback && s->user_data == user_data) return;
    }

    HistorySubscriber *s = g_new0(HistorySubscriber, 1);
    s->cb = callback;
    s->user_data = user_data;
    _history_subscribers = g_slist_prepend(_history_subscribers, s);
}

void notification_client_on_dnd_change(DoNotDisturbCallback callback, gpointer user_data) {
    if (!callback) {
        if (!user_data) {
            g_slist_free_full(_dnd_subscribers, g_free);
            _dnd_subscribers = NULL;
            return;
        }

        GSList *prev = NULL;
        for (GSList *l = _dnd_subscribers; l;) {
            DndSubscriber *s = (DndSubscriber *)l->data;
            GSList *next = l->next;
            if (s && s->user_data == user_data) {
                g_free(s);
                if (prev) prev->next = next;
                else _dnd_subscribers = next;
                g_slist_free_1(l);
            } else {
                prev = l;
            }
            l = next;
        }
        return;
    }

    for (GSList *l = _dnd_subscribers; l; l = l->next) {
        DndSubscriber *s = (DndSubscriber *)l->data;
        if (s && s->cb == callback && s->user_data == user_data) return;
    }

    DndSubscriber *s = g_new0(DndSubscriber, 1);
    s->cb = callback;
    s->user_data = user_data;
    _dnd_subscribers = g_slist_prepend(_dnd_subscribers, s);
}
