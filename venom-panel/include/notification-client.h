/*
 * notification-client.h
 *
 * D-Bus client for venom_notify daemon.
 * Interface: org.venom.NotificationHistory
 */

#ifndef NOTIFICATION_CLIENT_H
#define NOTIFICATION_CLIENT_H

#include <glib.h>
#include <gio/gio.h>

/* Notification Item Structure */
typedef struct {
    guint32 id;
    gchar *app_name;
    gchar *icon_path;
    gchar *summary;
    gchar *body;
} NotificationItem;

/* Free a NotificationItem */
void notification_item_free(NotificationItem *item);

/* Free a list of NotificationItems */
void notification_item_list_free(GList *list);

/* Callback for history updates */
typedef void (*NotificationHistoryCallback)(gpointer user_data);

/* Callback for DoNotDisturb changes */
typedef void (*DoNotDisturbCallback)(gboolean enabled, gpointer user_data);

/* Initialize the notification client */
void notification_client_init(void);

/* Cleanup the notification client */
void notification_client_cleanup(void);

/* Check if daemon is available */
gboolean notification_client_is_available(void);

/* Get notification history (returns GList of NotificationItem*) */
GList* notification_client_get_history(void);

/* Clear all notifications */
void notification_client_clear_history(void);

/* Remove a specific notification */
void notification_client_remove(guint32 id);

/* Do Not Disturb mode */
gboolean notification_client_get_dnd(void);
void notification_client_set_dnd(gboolean enabled);

/* Subscribe to history updates */
void notification_client_on_history_update(NotificationHistoryCallback callback, gpointer user_data);

/* Subscribe to DND changes */
void notification_client_on_dnd_change(DoNotDisturbCallback callback, gpointer user_data);

#endif /* NOTIFICATION_CLIENT_H */
