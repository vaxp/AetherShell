#include "notify_ui.h"
#include <gtk-layer-shell.h>
#include "config.h"
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif

// --- إعدادات التصميم الأساسية ---
#define NOTIFY_WIDTH 340

typedef struct {
    guint32 notification_id;
    char *action_key;
    void (*action_cb)(guint32 id, const char *action_key, gpointer user_data);
    gpointer user_data;
} ActionData;

static void on_action_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ActionData *data = (ActionData *)user_data;
    if (data->action_cb) {
        data->action_cb(data->notification_id, data->action_key, data->user_data);
    }
}

static gboolean on_notification_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        ActionData *data = (ActionData *)user_data;
        if (data && data->action_cb) {
            data->action_cb(data->notification_id, data->action_key, data->user_data);
        }
        return TRUE;
    }
    return FALSE;
}

static void free_action_data(gpointer data, GClosure *closure) {
    (void)closure;
    ActionData *action_data = (ActionData *)data;
    g_free(action_data->action_key);
    g_free(action_data);
}

static gboolean draw_notification_background(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    gint width = gtk_widget_get_allocated_width(widget);
    gint height = gtk_widget_get_allocated_height(widget);
    const double radius = 8.0;

    if (width <= 0 || height <= 0) {
        return FALSE;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_new_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI / 2.0, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI / 2.0);
    cairo_arc(cr, radius, height - radius, radius, G_PI / 2.0, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, g_config.notify_bg.red, g_config.notify_bg.green, g_config.notify_bg.blue, g_config.notify_bg.alpha);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, g_config.notify_border.red, g_config.notify_border.green, g_config.notify_border.blue, g_config.notify_border.alpha);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    return FALSE;
}

static void update_notification_icon(GtkWidget *image, const char *icon) {
    if (!GTK_IS_IMAGE(image)) {
        return;
    }

    if (icon && strlen(icon) > 0) {
        if (g_path_is_absolute(icon) || g_file_test(icon, G_FILE_TEST_EXISTS)) {
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(icon, 48, 48, TRUE, NULL);
            if (pixbuf) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
                g_object_unref(pixbuf);
                return;
            }
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(image), icon, GTK_ICON_SIZE_DIALOG);
            gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
            return;
        }
    }

    gtk_image_set_from_icon_name(GTK_IMAGE(image), "dialog-information", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
}

static GtkCssProvider *global_provider = NULL;

void notify_ui_init(void) {
    if (!global_provider) {
        global_provider = gtk_css_provider_new();
        GdkScreen *screen = gdk_screen_get_default();
        if (screen) {
            gtk_style_context_add_provider_for_screen(screen,
                                                      GTK_STYLE_PROVIDER(global_provider),
                                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
    }
    
    char *bg_str = gdk_rgba_to_string(&g_config.notify_bg);
    char *border_str = gdk_rgba_to_string(&g_config.notify_border);
    char *title_str = gdk_rgba_to_string(&g_config.notify_title_text);
    char *body_str = gdk_rgba_to_string(&g_config.notify_body_text);
    char *btn_bg_str = gdk_rgba_to_string(&g_config.notify_btn_bg);
    char *btn_text_str = gdk_rgba_to_string(&g_config.notify_btn_text);
    char *btn_hbg_str = gdk_rgba_to_string(&g_config.notify_btn_hover_bg);
    char *btn_htext_str = gdk_rgba_to_string(&g_config.notify_btn_hover_text);

    gchar *css = g_strdup_printf(
        "window.venom-notify {"
        "   background-color: %s;"
        "   background: %s;"
        "   border: 1.5px solid %s;"
        "   border-radius: 8px;"
        "}"
        "label.notify-title {"
        "   color: %s;"
        "   font-weight: bold;"
        "   font-size: 11pt;"
        "}"
        "label.notify-body {"
        "   color: %s;"
        "   font-size: 10pt;"
        "}"
        "button {"
        "   background-color: %s;"
        "   color: %s;"
        "   border: 1px solid %s;"
        "   border-radius: 4px;"
        "   padding: 4px;"
        "}"
        "button:hover {"
        "   background-color: %s;"
        "   color: %s;"
        "}",
        bg_str, bg_str, border_str, title_str, body_str, btn_bg_str, btn_text_str, btn_text_str, btn_hbg_str, btn_htext_str
    );

    g_free(bg_str); g_free(border_str); g_free(title_str); g_free(body_str);
    g_free(btn_bg_str); g_free(btn_text_str); g_free(btn_hbg_str); g_free(btn_htext_str);

    gtk_css_provider_load_from_data(global_provider, css, -1, NULL);
    g_free(css);
}

void notify_ui_setup_window(VenomNotification *notification,
                            const char *summary,
                            const char *body,
                            const char *icon,
                            GVariant *actions,
                            gboolean use_layer_shell,
                            void (*action_cb)(guint32 id, const char *action_key, gpointer user_data),
                            gpointer user_data) {
    if (!notification) {
        return;
    }

    notification->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(notification->win), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(notification->win), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(notification->win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(notification->win), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(notification->win), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_default_size(GTK_WINDOW(notification->win), NOTIFY_WIDTH, -1);
    gtk_widget_set_app_paintable(notification->win, TRUE);
    gtk_widget_set_size_request(notification->win, NOTIFY_WIDTH, -1);

    if (use_layer_shell) {
        gtk_layer_init_for_window(GTK_WINDOW(notification->win));
        gtk_layer_set_namespace(GTK_WINDOW(notification->win), "venom-notify");
        gtk_layer_set_layer(GTK_WINDOW(notification->win), GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(notification->win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        
        gboolean is_top = strstr(g_config.notify_position, "top") != NULL;
        gboolean is_bottom = strstr(g_config.notify_position, "bottom") != NULL;
        gboolean is_right = strstr(g_config.notify_position, "right") != NULL;
        gboolean is_left = strstr(g_config.notify_position, "left") != NULL;
        gboolean is_center = strstr(g_config.notify_position, "center") != NULL;
        
        // إذا لم يتم تحديد أي اتجاه، نعتبره اليمين (ما لم يكن مركزاً)
        if (!is_right && !is_left && !is_center) is_right = TRUE;
        if (!is_top && !is_bottom) is_top = TRUE;

        gtk_layer_set_anchor(GTK_WINDOW(notification->win), GTK_LAYER_SHELL_EDGE_TOP, is_top);
        gtk_layer_set_anchor(GTK_WINDOW(notification->win), GTK_LAYER_SHELL_EDGE_BOTTOM, is_bottom);
        gtk_layer_set_anchor(GTK_WINDOW(notification->win), GTK_LAYER_SHELL_EDGE_RIGHT, is_right);
        gtk_layer_set_anchor(GTK_WINDOW(notification->win), GTK_LAYER_SHELL_EDGE_LEFT, is_left);
    }

    // تفعيل الشفافية
    GdkScreen *screen = gtk_widget_get_screen(notification->win);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(notification->win, visual);

    // ربط CSS بالنافذة
    GtkStyleContext *context = gtk_widget_get_style_context(notification->win);
    gtk_style_context_add_class(context, "venom-notify");

    // تخطيط العناصر
    GtkWidget *panel_surface = gtk_event_box_new();
    gtk_widget_add_events(panel_surface, GDK_BUTTON_PRESS_MASK);
    ActionData *default_data = g_new0(ActionData, 1);
    default_data->notification_id = notification->id;
    default_data->action_key = g_strdup("default");
    default_data->action_cb = action_cb;
    default_data->user_data = user_data;
    g_signal_connect_data(panel_surface, "button-press-event", G_CALLBACK(on_notification_clicked), default_data, free_action_data, 0);

    GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_app_paintable(panel_surface, TRUE);
    g_signal_connect(panel_surface, "draw", G_CALLBACK(draw_notification_background), NULL);
    gtk_container_set_border_width(GTK_CONTAINER(main_hbox), 12);
    gtk_widget_set_size_request(main_hbox, NOTIFY_WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(panel_surface), main_hbox);
    gtk_container_add(GTK_CONTAINER(notification->win), panel_surface);

    // 1. الأيقونة
    GtkWidget *icon_img = NULL;
    if (icon && strlen(icon) > 0) {
        if (g_path_is_absolute(icon) || g_file_test(icon, G_FILE_TEST_EXISTS)) {
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(icon, 48, 48, TRUE, NULL);
            if (pixbuf) {
                icon_img = gtk_image_new_from_pixbuf(pixbuf);
                g_object_unref(pixbuf);
            }
        } else {
            icon_img = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG);
            gtk_image_set_pixel_size(GTK_IMAGE(icon_img), 48);
        }
    }
    if (!icon_img) icon_img = gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_DIALOG);
    notification->icon_img = icon_img;

    gtk_box_pack_start(GTK_BOX(main_hbox), icon_img, FALSE, FALSE, 0);
    gtk_widget_set_valign(icon_img, GTK_ALIGN_START);

    // 2. صندوق النصوص والأزرار
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(vbox, NOTIFY_WIDTH - 96, -1);
    gtk_box_pack_start(GTK_BOX(main_hbox), vbox, TRUE, TRUE, 0);

    // العنوان
    GtkWidget *title_lbl = gtk_label_new(summary);
    notification->title_lbl = title_lbl;
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(title_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(title_lbl), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(title_lbl), 32);
    gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl), "notify-title");
    gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

    // المحتوى
    GtkWidget *body_lbl = gtk_label_new(NULL);
    notification->body_lbl = body_lbl;
    if (body && strlen(body) > 0) {
        // تفعيل قراءة وسوم Pango (مثل البولد والروابط) التي ترسلها بعض التطبيقات
        // التحقق مما إذا كان النص يحتوي على وسوم صالحة
        GError *error = NULL;
        if (pango_parse_markup(body, -1, 0, NULL, NULL, NULL, &error)) {
            // التنسيق سليم، اعرضه مع دعم الخط العريض والروابط
            gtk_label_set_markup(GTK_LABEL(body_lbl), body);
        } else {
            // التنسيق مكسور (يحتوي رموز مثل < أو &)، تراجع واعرضه كنص عادي
            gtk_label_set_text(GTK_LABEL(body_lbl), body);
            g_clear_error(&error);
        }
        gtk_label_set_xalign(GTK_LABEL(body_lbl), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(body_lbl), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(body_lbl), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(body_lbl), 32);
    } else {
        gtk_widget_hide(body_lbl);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(body_lbl), "notify-body");
    gtk_box_pack_start(GTK_BOX(vbox), body_lbl, FALSE, FALSE, 0);

    // 3. الأزرار (Actions)
    if (actions && g_variant_n_children(actions) > 0) {
        GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 4);

        GVariantIter iter;
        gchar *action_key, *action_label;
        g_variant_iter_init(&iter, actions);

        while (g_variant_iter_next(&iter, "s", &action_key) && g_variant_iter_next(&iter, "s", &action_label)) {
            // تجاهل زر الـ default (هو عادة مخصص للنقر على مساحة الإشعار نفسها)
            if (g_strcmp0(action_key, "default") == 0) {
                g_free(action_key); g_free(action_label);
                continue;
            }

            GtkWidget *btn = gtk_button_new_with_label(action_label);
            gtk_box_pack_end(GTK_BOX(btn_box), btn, FALSE, FALSE, 0);

            ActionData *data = g_new0(ActionData, 1);
            data->notification_id = notification->id;
            data->action_key = g_strdup(action_key);
            data->action_cb = action_cb;
            data->user_data = user_data;

            g_signal_connect_data(btn, "clicked", G_CALLBACK(on_action_clicked), data, free_action_data, 0);

            g_free(action_key); g_free(action_label);
        }
    }

    gtk_widget_show_all(notification->win);
    gtk_window_resize(GTK_WINDOW(notification->win), NOTIFY_WIDTH, 1);
}

void notify_ui_update_content(VenomNotification *notification,
                              const char *summary,
                              const char *body,
                              const char *icon) {
    if (!notification) {
        return;
    }

    update_notification_icon(notification->icon_img, icon);
    gtk_label_set_text(GTK_LABEL(notification->title_lbl), summary ? summary : "");

    if (notification->body_lbl) {
        if (body && strlen(body) > 0) {
            GError *error = NULL;
            if (pango_parse_markup(body, -1, 0, NULL, NULL, NULL, &error)) {
                gtk_label_set_markup(GTK_LABEL(notification->body_lbl), body);
            } else {
                gtk_label_set_text(GTK_LABEL(notification->body_lbl), body);
                g_clear_error(&error);
            }
            gtk_widget_show(notification->body_lbl);
        } else {
            gtk_label_set_text(GTK_LABEL(notification->body_lbl), "");
            gtk_widget_hide(notification->body_lbl);
        }
    }

    gtk_widget_show_all(notification->win);
    gtk_window_resize(GTK_WINDOW(notification->win), NOTIFY_WIDTH, 1);
}

void notify_ui_destroy(VenomNotification *notification) {
    if (!notification || !notification->win) {
        return;
    }
    gtk_widget_destroy(notification->win);
    notification->win = NULL;
}

void notify_ui_reposition(GList *active_notifications, gboolean use_layer_shell) {
    int count = 0;
    GdkRectangle workarea = {0};
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (monitor) {
        gdk_monitor_get_workarea(monitor, &workarea);
    }

    gboolean is_top = strstr(g_config.notify_position, "top") != NULL;
    gboolean is_bottom = strstr(g_config.notify_position, "bottom") != NULL;
    gboolean is_right = strstr(g_config.notify_position, "right") != NULL;
    gboolean is_left = strstr(g_config.notify_position, "left") != NULL;
    gboolean is_center = strstr(g_config.notify_position, "center") != NULL;
    
    // Default to top-right if invalid
    if (!is_top && !is_bottom) is_top = TRUE;
    if (!is_right && !is_left && !is_center) is_right = TRUE;

    for (GList *l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *n = (VenomNotification *)l->data;
        gint width = NOTIFY_WIDTH;
        gint height = 0;
        gtk_window_get_size(GTK_WINDOW(n->win), &width, &height);
        if (height <= 0) {
            GtkRequisition min_req;
            gtk_widget_get_preferred_size(n->win, &min_req, NULL);
            height = min_req.height;
        }

        int y = g_config.notify_margin_y + (count * (height + g_config.notify_spacing));
        if (use_layer_shell) {
            if (is_top) gtk_layer_set_margin(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_TOP, y);
            if (is_bottom) gtk_layer_set_margin(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_BOTTOM, y);
            if (is_right) gtk_layer_set_margin(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_RIGHT, g_config.notify_margin_x);
            if (is_left) gtk_layer_set_margin(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_LEFT, g_config.notify_margin_x);
        } else {
            int x;
            if (is_center) {
                x = workarea.x + (workarea.width - width) / 2;
            } else {
                x = is_right ? (workarea.width - width - g_config.notify_margin_x) : g_config.notify_margin_x;
            }
            int actual_y = is_top ? y : (workarea.height - y - height);
            gtk_window_move(GTK_WINDOW(n->win), x, actual_y);
        }
        count++;
    }
}
