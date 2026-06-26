#ifndef VAXP_GUI_NOTIFY_UI_H
#define VAXP_GUI_NOTIFY_UI_H

#include <gtk/gtk.h>
#include <gio/gio.h>

typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    GtkWidget *icon_img;
    GtkWidget *title_lbl;
    GtkWidget *body_lbl;
    GtkWidget *win;
    guint timeout_source;
    gint original_timeout;
    char *desktop_entry;
    gint value;
    GtkWidget *progress_bar;
} VaxpNotification;

void notify_ui_init(void);
void notify_ui_setup_window(VaxpNotification *notification,
                            const char *summary,
                            const char *body,
                            const char *icon,
                            GVariant *actions,
                            gboolean use_layer_shell,
                            gint value,
                            void (*action_cb)(guint32 id, const char *action_key, gpointer user_data),
                            gpointer user_data);
void notify_ui_update_content(VaxpNotification *notification,
                              const char *summary,
                              const char *body,
                              const char *icon,
                              gint value);
void notify_ui_destroy(VaxpNotification *notification);
void notify_ui_reposition(GList *active_notifications, gboolean use_layer_shell);

#endif
