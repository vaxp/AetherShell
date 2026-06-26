#ifndef VAXP_NOTIFICATIONS_H
#define VAXP_NOTIFICATIONS_H

#include <glib.h>

// Struct for history items
typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    char *summary;
    char *body;
    char *desktop_entry;
    gint value;
} NotificationData;

typedef void (*NotificationsUpdatedCallback)(GList *history, gpointer user_data);
typedef void (*DndChangedCallback)(gboolean enabled, gpointer user_data);

void vaxp_notifications_init(NotificationsUpdatedCallback history_cb,
                              DndChangedCallback dnd_cb,
                              gpointer user_data);

void vaxp_notifications_set_dnd(gboolean enabled);
void vaxp_notifications_clear_history(void);
void vaxp_notifications_invoke_default_action(guint32 id);

#endif // VAXP_NOTIFICATIONS_H
