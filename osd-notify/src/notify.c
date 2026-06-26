#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gtk-layer-shell.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#include <time.h>
#include "notify_ui.h"
#include "osd_sound.h"

#define DEFAULT_TIMEOUT 5000
#define MAX_HISTORY 50

// --- هيكل بيانات السجل (History Item) ---
typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    char *summary;
    char *body;
    gint64 timestamp;
    char *desktop_entry;
} NotificationItem;

GList *active_notifications = NULL;
GList *history_list = NULL;
guint32 id_counter = 1;
gboolean do_not_disturb = FALSE;
GDBusConnection *dbus_connection = NULL;
gboolean notify_use_layer_shell = FALSE;

// --- واجهات D-Bus ---
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.Notifications'>"
  "    <method name='Notify'>"
  "      <arg type='s' name='app_name' direction='in'/>"
  "      <arg type='u' name='replaces_id' direction='in'/>"
  "      <arg type='s' name='app_icon' direction='in'/>"
  "      <arg type='s' name='summary' direction='in'/>"
  "      <arg type='s' name='body' direction='in'/>"
  "      <arg type='as' name='actions' direction='in'/>"
  "      <arg type='a{sv}' name='hints' direction='in'/>"
  "      <arg type='i' name='expire_timeout' direction='in'/>"
  "      <arg type='u' name='id' direction='out'/>"
  "    </method>"
  "    <method name='GetCapabilities'><arg type='as' name='caps' direction='out'/></method>"
  "    <method name='GetServerInformation'><arg type='s' name='name' direction='out'/><arg type='s' name='vendor' direction='out'/><arg type='s' name='ver' direction='out'/><arg type='s' name='spec' direction='out'/></method>"
  "    <method name='CloseNotification'><arg type='u' name='id' direction='in'/></method>"
  "    <signal name='NotificationClosed'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='u' name='reason'/>"
  "    </signal>"
  "    <signal name='ActionInvoked'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='s' name='action_key'/>"
  "    </signal>"
  "  </interface>"
  "  <interface name='org.venom.NotificationHistory'>"
  "    <method name='GetHistory'><arg type='a(usssss)' name='notifications' direction='out'/></method>"
  "    <method name='ClearHistory'/>"
  "    <method name='InvokeDefaultAction'><arg type='u' name='id' direction='in'/></method>"
  "    <method name='SetDoNotDisturb'><arg type='b' name='enabled' direction='in'/></method>"
  "    <method name='GetDoNotDisturb'><arg type='b' name='enabled' direction='out'/></method>"
  "    <signal name='HistoryUpdated'/>"
  "    <signal name='DoNotDisturbChanged'><arg type='b' name='enabled'/></signal>"
  "  </interface>"
  "</node>";

// --- تعريف مسبق للدوال ---
void close_notification(guint32 id, guint reason);
void emit_history_updated_signal(GDBusConnection *connection);
void add_to_history(guint32 id, const char *app, const char *icon, const char *summary, const char *body, const char *desktop_entry);
void clear_history();
static NotificationItem *find_history_item_by_id(guint32 id);
static VenomNotification *find_active_notification_by_id(guint32 id);
static void on_ui_action(guint32 id, const char *action_key, gpointer user_data);
static guint32 create_new_notification(const char *app_name, guint32 replaces_id, const char *summary, const char *body, const char *icon, GVariant *actions, GVariant *hints, gint timeout, const char *desktop_entry);
static gboolean notify_is_wayland_session(void);

// --- إدارة السجل (History Logic) ---

void add_to_history(guint32 id, const char *app, const char *icon, const char *summary, const char *body, const char *desktop_entry) {
    NotificationItem *item = g_new0(NotificationItem, 1);
    item->id = id;
    item->app_name = g_strdup(app);
    item->icon_path = g_strdup(icon);
    item->summary = g_strdup(summary);
    item->body = g_strdup(body);
    item->desktop_entry = g_strdup(desktop_entry);
    item->timestamp = time(NULL);

    // إضافة لأول القائمة (الأحدث أولاً)
    history_list = g_list_prepend(history_list, item);

    // تنظيف القديم إذا تجاوزنا الحد المسموح
    if (g_list_length(history_list) > MAX_HISTORY) {
        GList *last = g_list_last(history_list);
        NotificationItem *old_item = (NotificationItem *)last->data;
        g_free(old_item->app_name);
        g_free(old_item->icon_path);
        g_free(old_item->summary);
        g_free(old_item->body);
        g_free(old_item->desktop_entry);
        g_free(old_item);
        history_list = g_list_delete_link(history_list, last);
    }
}

void clear_history() {
    GList *l;
    for (l = history_list; l != NULL; l = l->next) {
        NotificationItem *item = (NotificationItem *)l->data;
        g_free(item->app_name);
        g_free(item->icon_path);
        g_free(item->summary);
        g_free(item->body);
        g_free(item->desktop_entry);
        g_free(item);
    }
    g_list_free(history_list);
    history_list = NULL;
}

static NotificationItem *find_history_item_by_id(guint32 id) {
    for (GList *l = history_list; l != NULL; l = l->next) {
        NotificationItem *item = (NotificationItem *)l->data;
        if (item->id == id) {
            return item;
        }
    }
    return NULL;
}

static VenomNotification *find_active_notification_by_id(guint32 id) {
    for (GList *l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *notification = (VenomNotification *)l->data;
        if (notification->id == id) {
            return notification;
        }
    }
    return NULL;
}

// دالة مساعدة لإرسال إشارة "تحديث السجل"
void emit_history_updated_signal(GDBusConnection *connection) {
    // استخدم المتغير العام إذا لم نحصل على connection
    if (!connection) connection = dbus_connection;
    if (!connection) return;
    
    g_dbus_connection_emit_signal(connection,
                                  NULL,
                                  "/org/freedesktop/Notifications",
                                  "org.venom.NotificationHistory",
                                  "HistoryUpdated",
                                  NULL, NULL);
}

static gboolean notify_is_wayland_session(void) {
#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return FALSE;
#endif
}

static void launch_app_from_desktop_entry(const char *desktop_entry) {
    if (!desktop_entry || strlen(desktop_entry) == 0) return;
    gchar *desktop_filename = g_strdup_printf("%s.desktop", desktop_entry);
    GDesktopAppInfo *app_info = g_desktop_app_info_new(desktop_filename);
    if (app_info) {
        GError *err = NULL;
        g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, &err);
        if (err) {
            g_printerr("Failed to launch %s: %s\n", desktop_filename, err->message);
            g_error_free(err);
        }
        g_object_unref(app_info);
    }
    g_free(desktop_filename);
}

static void on_ui_action(guint32 id, const char *action_key, gpointer user_data) {
    (void)user_data;
    if (dbus_connection) {
        g_dbus_connection_emit_signal(dbus_connection, NULL, "/org/freedesktop/Notifications",
                                      "org.freedesktop.Notifications", "ActionInvoked",
                                      g_variant_new("(us)", id, action_key), NULL);
    }
    if (g_strcmp0(action_key, "default") == 0) {
        VenomNotification *n = find_active_notification_by_id(id);
        if (n && n->desktop_entry) {
            launch_app_from_desktop_entry(n->desktop_entry);
        } else {
            NotificationItem *item = find_history_item_by_id(id);
            if (item && item->desktop_entry) {
                launch_app_from_desktop_entry(item->desktop_entry);
            }
        }
    }
    close_notification(id, 2); // 2 = Dismissed by user
}

gboolean on_timeout(gpointer data) {
    close_notification(GPOINTER_TO_UINT(data), 1); // 1 = Expired
    return FALSE; 
}

void close_notification(guint32 id, guint reason) {
    GList *l;
    for (l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *n = (VenomNotification *)l->data;
        if (n->id == id) {
            if (n->timeout_source > 0) g_source_remove(n->timeout_source);
            
            notify_ui_destroy(n);
            active_notifications = g_list_delete_link(active_notifications, l);
            g_free(n->app_name);
            g_free(n->icon_path);
            g_free(n);
            notify_ui_reposition(active_notifications, notify_use_layer_shell);
            
            // إرسال إشارة الإغلاق
            if (dbus_connection) {
                g_dbus_connection_emit_signal(dbus_connection, NULL, "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "NotificationClosed",
                                              g_variant_new("(uu)", id, reason), NULL);
            }
            break;
        }
    }
}

// --- التحقق من كون الإشعار يمثل خطأ أو تحذير ---
static gboolean is_error_or_warning(const char *summary, const char *body, const char *icon, GVariant *hints) {
    // 1. التحقق من نصوص العنوان والمحتوى
    if (summary) {
        gchar *lower_summary = g_utf8_strdown(summary, -1);
        if (g_strrstr(lower_summary, "error") || 
            g_strrstr(lower_summary, "warning") || 
            g_strrstr(lower_summary, "fail") || 
            g_strrstr(lower_summary, "critical") || 
            g_strrstr(lower_summary, "خطأ") || 
            g_strrstr(lower_summary, "تحذير") || 
            g_strrstr(lower_summary, "فشل") || 
            g_strrstr(lower_summary, "مشكلة")) {
            g_free(lower_summary);
            return TRUE;
        }
        g_free(lower_summary);
    }
    if (body) {
        gchar *lower_body = g_utf8_strdown(body, -1);
        if (g_strrstr(lower_body, "error") || 
            g_strrstr(lower_body, "warning") || 
            g_strrstr(lower_body, "fail") || 
            g_strrstr(lower_body, "critical") || 
            g_strrstr(lower_body, "خطأ") || 
            g_strrstr(lower_body, "تحذير") || 
            g_strrstr(lower_body, "فشل") || 
            g_strrstr(lower_body, "مشكلة")) {
            g_free(lower_body);
            return TRUE;
        }
        g_free(lower_body);
    }

    // 2. التحقق من اسم الأيقونة
    if (icon) {
        gchar *lower_icon = g_utf8_strdown(icon, -1);
        if (g_strrstr(lower_icon, "error") || 
            g_strrstr(lower_icon, "warning") || 
            g_strrstr(lower_icon, "alert") || 
            g_strrstr(lower_icon, "dialog-error") || 
            g_strrstr(lower_icon, "dialog-warning") || 
            g_strrstr(lower_icon, "urgent")) {
            g_free(lower_icon);
            return TRUE;
        }
        g_free(lower_icon);
    }

    // 3. التحقق من التلميحات (hints) مثل الأهمية والتصنيف
    if (hints) {
        GVariantIter iter;
        gchar *key;
        GVariant *value;
        g_variant_iter_init(&iter, hints);
        while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {
            if (g_strcmp0(key, "urgency") == 0) {
                guchar urgency = 0;
                if (g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE)) {
                    urgency = g_variant_get_byte(value);
                } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) {
                    urgency = (guchar)g_variant_get_int32(value);
                } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
                    const gchar *urg_str = g_variant_get_string(value, NULL);
                    if (g_strcmp0(urg_str, "critical") == 0 || g_strcmp0(urg_str, "2") == 0) {
                        urgency = 2;
                    }
                }
                if (urgency == 2) { // Critical / Error / Warning
                    g_free(key);
                    g_variant_unref(value);
                    return TRUE;
                }
            } else if (g_strcmp0(key, "category") == 0) {
                if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
                    const gchar *cat = g_variant_get_string(value, NULL);
                    if (g_strrstr(cat, "error") || g_strrstr(cat, "warning")) {
                        g_free(key);
                        g_variant_unref(value);
                        return TRUE;
                    }
                }
            }
            g_free(key);
            g_variant_unref(value);
        }
    }

    return FALSE;
}

// --- إنشاء الإشعار وعرضه ---
static guint32 create_new_notification(const char *app_name, guint32 replaces_id, const char *summary, const char *body, const char *icon, GVariant *actions, GVariant *hints, gint timeout, const char *desktop_entry) {
    // تحديد نوع الصوت المناسب (تنبيه عادي أم تحذير/خطأ)
    OsdSoundEvent sound_event = is_error_or_warning(summary, body, icon, hints) ? OSD_SOUND_ERROR : OSD_SOUND_NOTIFICATION;

    // إذا كان وضع عدم الإزعاج مفعل، أضف للسجل بشكل صامت فقط
    if (do_not_disturb) {
        NotificationItem *existing_item = replaces_id ? find_history_item_by_id(replaces_id) : NULL;
        if (existing_item) {
            g_free(existing_item->app_name);
            g_free(existing_item->icon_path);
            g_free(existing_item->summary);
            g_free(existing_item->body);
            g_free(existing_item->desktop_entry);
            existing_item->app_name = g_strdup(app_name);
            existing_item->icon_path = g_strdup(icon);
            existing_item->summary = g_strdup(summary);
            existing_item->body = g_strdup(body);
            existing_item->desktop_entry = g_strdup(desktop_entry);
            existing_item->timestamp = time(NULL);
            emit_history_updated_signal(NULL);
            return existing_item->id;
        }

        guint32 new_id = id_counter++;
        add_to_history(new_id, app_name, icon, summary, body, desktop_entry);
        emit_history_updated_signal(NULL);
        return new_id;
    }

    VenomNotification *existing_notification = replaces_id ? find_active_notification_by_id(replaces_id) : NULL;
    if (existing_notification) {
        NotificationItem *existing_item = find_history_item_by_id(existing_notification->id);
        if (existing_item) {
            g_free(existing_item->app_name);
            g_free(existing_item->icon_path);
            g_free(existing_item->summary);
            g_free(existing_item->body);
            g_free(existing_item->desktop_entry);
            existing_item->app_name = g_strdup(app_name);
            existing_item->icon_path = g_strdup(icon);
            existing_item->summary = g_strdup(summary);
            existing_item->body = g_strdup(body);
            existing_item->desktop_entry = g_strdup(desktop_entry);
            existing_item->timestamp = time(NULL);
        }

        g_free(existing_notification->app_name);
        existing_notification->app_name = g_strdup(app_name);
        g_free(existing_notification->icon_path);
        existing_notification->icon_path = g_strdup(icon);
        g_free(existing_notification->desktop_entry);
        existing_notification->desktop_entry = g_strdup(desktop_entry);
        notify_ui_update_content(existing_notification, summary, body, icon);
        notify_ui_reposition(active_notifications, notify_use_layer_shell);

        if (existing_notification->timeout_source > 0) {
            g_source_remove(existing_notification->timeout_source);
        }
        if (timeout <= 0) timeout = DEFAULT_TIMEOUT;
        existing_notification->timeout_source = g_timeout_add(timeout, on_timeout, GUINT_TO_POINTER(existing_notification->id));
        osd_sound_play(sound_event);
        emit_history_updated_signal(NULL);
        return existing_notification->id;
    }

    VenomNotification *n = g_new0(VenomNotification, 1);
    n->id = id_counter++;
    n->app_name = g_strdup(app_name);
    n->icon_path = g_strdup(icon);
    n->desktop_entry = g_strdup(desktop_entry);

    notify_ui_setup_window(n, summary, body, icon, actions, notify_use_layer_shell, on_ui_action, NULL);
    
    active_notifications = g_list_append(active_notifications, n);
    
    // إضافة الإشعار للسجل
    add_to_history(n->id, app_name, icon, summary, body, desktop_entry);
    
    notify_ui_reposition(active_notifications, notify_use_layer_shell);
    osd_sound_play(sound_event);
    
    // إرسال إشارة تحديث السجل
    emit_history_updated_signal(NULL);

    if (timeout <= 0) timeout = DEFAULT_TIMEOUT;
    n->timeout_source = g_timeout_add(timeout, on_timeout, GUINT_TO_POINTER(n->id));
    return n->id;
}

// --- معالجة اتصالات D-Bus ---
static void handle_method_call(GDBusConnection *connection, const gchar *sender,
                               const gchar *object_path, const gchar *interface_name,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;
    if (g_strcmp0(method_name, "Notify") == 0) {
        gchar *app_name, *app_icon, *summary, *body;
        guint32 replaces_id;
        GVariant *actions = NULL;
        GVariant *hints = NULL;
        gint32 expire_timeout;

        g_variant_get(parameters, "(susss@as@a{sv}i)", 
                      &app_name, &replaces_id, &app_icon, 
                      &summary, &body, &actions, &hints, &expire_timeout);

        gchar *desktop_entry = NULL;
        if (hints) {
            GVariantIter iter;
            gchar *key;
            GVariant *val;
            g_variant_iter_init(&iter, hints);
            while (g_variant_iter_next(&iter, "{sv}", &key, &val)) {
                if (g_strcmp0(key, "desktop-entry") == 0) {
                    if (g_variant_is_of_type(val, G_VARIANT_TYPE_STRING)) {
                        desktop_entry = g_strdup(g_variant_get_string(val, NULL));
                    }
                }
                g_free(key);
                g_variant_unref(val);
            }
        }

        if (!desktop_entry && app_name && strlen(app_name) > 0) {
            gchar *desktop_filename = g_strdup_printf("%s.desktop", app_name);
            GDesktopAppInfo *app_info = g_desktop_app_info_new(desktop_filename);
            if (app_info) {
                desktop_entry = g_strdup(app_name);
                g_object_unref(app_info);
            } else {
                gchar *guessed_app = g_utf8_strdown(app_name, -1);
                for (int i = 0; guessed_app[i] != '\0'; i++) {
                    if (guessed_app[i] == ' ') {
                        guessed_app[i] = '-';
                    }
                }
                gchar *guessed_filename = g_strdup_printf("%s.desktop", guessed_app);
                GDesktopAppInfo *guessed_info = g_desktop_app_info_new(guessed_filename);
                if (guessed_info) {
                    desktop_entry = guessed_app;
                    g_object_unref(guessed_info);
                } else {
                    g_free(guessed_app);
                }
                g_free(guessed_filename);
            }
            g_free(desktop_filename);
        }

        gchar *final_icon = g_strdup(app_icon);
        if (!final_icon || strlen(final_icon) == 0) {
            gboolean found = FALSE;
            GtkIconTheme *theme = gtk_icon_theme_get_default();

            if (desktop_entry) {
                if (gtk_icon_theme_has_icon(theme, desktop_entry)) {
                    g_free(final_icon);
                    final_icon = g_strdup(desktop_entry);
                    found = TRUE;
                } else {
                    gchar *desktop_filename = g_strdup_printf("%s.desktop", desktop_entry);
                    GDesktopAppInfo *app_info = g_desktop_app_info_new(desktop_filename);
                    if (app_info) {
                        gchar *icon_str = g_desktop_app_info_get_string(app_info, "Icon");
                        if (icon_str) {
                            if (g_path_is_absolute(icon_str) || gtk_icon_theme_has_icon(theme, icon_str)) {
                                g_free(final_icon);
                                final_icon = icon_str; 
                                found = TRUE;
                            } else {
                                g_free(icon_str);
                            }
                        }
                        g_object_unref(app_info);
                    }
                    g_free(desktop_filename);
                }
            }

            if (!found && app_name && strlen(app_name) > 0) {
                gchar *guessed_icon = g_utf8_strdown(app_name, -1);
                for (int i = 0; guessed_icon[i] != '\0'; i++) {
                    if (guessed_icon[i] == ' ') {
                        guessed_icon[i] = '-';
                    }
                }
                
                if (gtk_icon_theme_has_icon(theme, guessed_icon)) {
                    g_free(final_icon);
                    final_icon = guessed_icon;
                } else {
                    g_free(guessed_icon);
                }
            }
        }

        guint32 notification_id = create_new_notification(app_name, replaces_id, summary, body, final_icon, actions, hints, expire_timeout, desktop_entry);
        g_free(final_icon);
        g_free(desktop_entry);
        
        // إخبار الكونترول سنتر بتحديث السجل
        emit_history_updated_signal(connection);
        
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", notification_id));

        g_free(app_name); g_free(app_icon); g_free(summary); g_free(body);
        if (actions) g_variant_unref(actions);
        if (hints) g_variant_unref(hints);
    }
    // طلب السجل (للكونترول سنتر)
    else if (g_strcmp0(method_name, "GetHistory") == 0) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(usssss)"));
        
        for (GList *l = history_list; l != NULL; l = l->next) {
            NotificationItem *item = (NotificationItem *)l->data;
            g_variant_builder_add(builder, "(usssss)", 
                                  item->id, 
                                  item->app_name ? item->app_name : "",
                                  item->icon_path ? item->icon_path : "",
                                  item->summary ? item->summary : "",
                                  item->body ? item->body : "",
                                  item->desktop_entry ? item->desktop_entry : "");
        }
        
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(usssss))", builder));
        g_variant_builder_unref(builder);
    }
    // استدعاء الحدث الافتراضي (للكونترول سنتر)
    else if (g_strcmp0(method_name, "InvokeDefaultAction") == 0) {
        guint32 id;
        g_variant_get(parameters, "(u)", &id);
        on_ui_action(id, "default", NULL);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // مسح السجل
    else if (g_strcmp0(method_name, "ClearHistory") == 0) {
        clear_history();
        emit_history_updated_signal(connection);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // تفعيل/تعطيل وضع عدم الإزعاج
    else if (g_strcmp0(method_name, "SetDoNotDisturb") == 0) {
        gboolean enabled;
        g_variant_get(parameters, "(b)", &enabled);
        do_not_disturb = enabled;
        
        // إرسال إشارة التغيير
        g_dbus_connection_emit_signal(connection,
                                      NULL,
                                      "/org/freedesktop/Notifications",
                                      "org.venom.NotificationHistory",
                                      "DoNotDisturbChanged",
                                      g_variant_new("(b)", do_not_disturb), NULL);
        
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // الحصول على حالة وضع عدم الإزعاج
    else if (g_strcmp0(method_name, "GetDoNotDisturb") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", do_not_disturb));
    }
    else if (g_strcmp0(method_name, "GetCapabilities") == 0) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
        g_variant_builder_add(builder, "s", "body");
        g_variant_builder_add(builder, "s", "actions");
        g_variant_builder_add(builder, "s", "body-markup");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", builder));
        g_variant_builder_unref(builder);
    }
    else if (g_strcmp0(method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ssss)", "Venom", "Vaxp", "1.0", "1.2"));
    }
    else if (g_strcmp0(method_name, "CloseNotification") == 0) {
        guint32 id;
        g_variant_get(parameters, "(u)", &id);
        close_notification(id, 3);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name;
    (void)user_data;
    dbus_connection = connection;
    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                      node_info->interfaces[0], &interface_vtable, NULL, NULL, NULL);
    g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                      node_info->interfaces[1], &interface_vtable, NULL, NULL, NULL);
}

#include "notify.h"

void notify_init(void) {
    notify_use_layer_shell = notify_is_wayland_session() && gtk_layer_is_supported();
    notify_ui_init(); // تحميل الاستايل

    g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
                   G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, NULL, NULL, NULL, NULL);
}

void notify_reload_ui(void) {
    notify_ui_init(); // Reload CSS
    notify_ui_reposition(active_notifications, notify_use_layer_shell);
    for (GList *l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *n = (VenomNotification *)l->data;
        gtk_widget_queue_draw(n->win);
    }
}
