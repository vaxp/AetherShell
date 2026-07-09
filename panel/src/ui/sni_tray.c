#include "sni_tray.h"
#include "sni_backend.h"

#include <gtk/gtk.h>
#include <libdbusmenu-glib/dbusmenu-glib.h>

typedef struct {
    GtkWidget *button;
    GtkWidget *image;
    SniItem *item;
} TrayUiItem;

static GtkWidget *tray_box = NULL;
static GHashTable *ui_items = NULL; // service -> TrayUiItem

static void update_ui_item(TrayUiItem *ui) {
    if (!ui || !ui->item) return;

    // Update tooltip
    const char *title = sni_item_get_title(ui->item);
    gtk_widget_set_tooltip_text(ui->button, title ? title : sni_item_get_service(ui->item));

    // Update icon
    GdkPixbuf *pixbuf = sni_item_get_icon_pixbuf(ui->item);
    if (pixbuf) {
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 16, 16, GDK_INTERP_BILINEAR);
        gtk_image_set_from_pixbuf(GTK_IMAGE(ui->image), scaled ? scaled : pixbuf);
        if (scaled) g_object_unref(scaled);
        g_object_unref(pixbuf);
    } else {
        const char *icon_name = sni_item_get_icon_name(ui->item);
        if (icon_name) {
            gtk_image_set_from_icon_name(GTK_IMAGE(ui->image), icon_name, GTK_ICON_SIZE_MENU);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(ui->image), "application-x-executable-symbolic", GTK_ICON_SIZE_MENU);
        }
    }

    // Update visibility
    if (sni_item_is_passive(ui->item)) {
        gtk_widget_hide(ui->button);
    } else {
        gtk_widget_show_all(ui->button);
    }
}

static void on_menu_item_activate(GtkMenuItem *menu_item, gpointer user_data) {
    DbusmenuMenuitem *dbus_item = DBUSMENU_MENUITEM(user_data);
    if (dbus_item) {
        dbusmenu_menuitem_handle_event(dbus_item, DBUSMENU_MENUITEM_EVENT_ACTIVATED,
            g_variant_new_int32(0), gtk_get_current_event_time());
    }
}

static GtkWidget* build_menu_from_dbusmenu(DbusmenuMenuitem *root);

static GtkWidget* build_menu_item_widget(DbusmenuMenuitem *item) {
    const gchar *label = dbusmenu_menuitem_property_get(item, DBUSMENU_MENUITEM_PROP_LABEL);
    const gchar *type = dbusmenu_menuitem_property_get(item, DBUSMENU_MENUITEM_PROP_TYPE);

    if (!label || !*label || g_strcmp0(type, DBUSMENU_CLIENT_TYPES_SEPARATOR) == 0) {
        return gtk_separator_menu_item_new();
    }

    GtkWidget *menu_item = gtk_menu_item_new_with_label(label);
    gboolean enabled = dbusmenu_menuitem_property_get_bool(item, DBUSMENU_MENUITEM_PROP_ENABLED);
    gtk_widget_set_sensitive(menu_item, enabled);

    if (dbusmenu_menuitem_get_children(item)) {
        GtkWidget *submenu = build_menu_from_dbusmenu(item);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
    } else {
        g_signal_connect(menu_item, "activate", G_CALLBACK(on_menu_item_activate), item);
    }

    return menu_item;
}

static GtkWidget* build_menu_from_dbusmenu(DbusmenuMenuitem *root) {
    GtkWidget *menu = gtk_menu_new();
    GList *children = dbusmenu_menuitem_get_children(root);
    for (GList *it = children; it; it = it->next) {
        GtkWidget *menu_item = build_menu_item_widget(DBUSMENU_MENUITEM(it->data));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    }
    gtk_widget_show_all(menu);
    return menu;
}

static GtkWidget* build_context_menu(SniItem *item) {
    DbusmenuMenuitem *root = DBUSMENU_MENUITEM(sni_item_get_dbusmenu_root(item));
    if (!root || !dbusmenu_menuitem_get_children(root)) return NULL;
    return build_menu_from_dbusmenu(root);
}

static gboolean on_tray_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    TrayUiItem *ui = user_data;
    if (!ui || !ui->item || event->type != GDK_BUTTON_PRESS) return FALSE;

    gint x = (gint)event->x_root;
    gint y = (gint)event->y_root;

    if (event->button == 1) {
        if (sni_item_is_menu(ui->item)) {
            GtkWidget *menu = build_context_menu(ui->item);
            if (menu) {
                gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);
            } else {
                sni_item_context_menu(ui->item, x, y);
            }
        } else {
            sni_item_activate(ui->item, x, y);
        }
        return TRUE;
    }

    if (event->button == 2) {
        sni_item_secondary_activate(ui->item, x, y);
        return TRUE;
    }

    if (event->button == 3) {
        GtkWidget *menu = build_context_menu(ui->item);
        if (menu) {
            gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent*)event);
        } else {
            sni_item_context_menu(ui->item, x, y);
        }
        return TRUE;
    }

    return FALSE;
}

static gboolean on_tray_button_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    TrayUiItem *ui = user_data;
    (void)widget;

    if (!ui || !ui->item) return FALSE;

    gint delta = 0;
    const gchar *orientation = "vertical";

    if (event->direction == GDK_SCROLL_UP) delta = 1;
    else if (event->direction == GDK_SCROLL_DOWN) delta = -1;
    else if (event->direction == GDK_SCROLL_LEFT) { delta = -1; orientation = "horizontal"; }
    else if (event->direction == GDK_SCROLL_RIGHT) { delta = 1; orientation = "horizontal"; }
    else return FALSE;

    sni_item_scroll(ui->item, delta, orientation);
    return TRUE;
}

static void tray_ui_item_free(gpointer data) {
    TrayUiItem *ui = data;
    if (ui) {
        if (ui->button) gtk_widget_destroy(ui->button);
        g_free(ui);
    }
}

static void on_item_added(SniItem *item, gpointer user_data) {
    (void)user_data;
    const char *service = sni_item_get_service(item);
    if (!service || !tray_box) return;

    if (!ui_items) ui_items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tray_ui_item_free);

    TrayUiItem *ui = g_new0(TrayUiItem, 1);
    ui->item = item;
    ui->button = gtk_button_new();
    ui->image = gtk_image_new_from_icon_name("application-x-executable-symbolic", GTK_ICON_SIZE_MENU);

    gtk_button_set_relief(GTK_BUTTON(ui->button), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(ui->button), ui->image);
    gtk_widget_add_events(ui->button, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    
    g_signal_connect(ui->button, "button-press-event", G_CALLBACK(on_tray_button_press), ui);
    g_signal_connect(ui->button, "scroll-event", G_CALLBACK(on_tray_button_scroll), ui);

    GtkStyleContext *ctx = gtk_widget_get_style_context(ui->button);
    gtk_style_context_add_class(ctx, "tray-icon-btn");

    update_ui_item(ui);

    gtk_box_pack_start(GTK_BOX(tray_box), ui->button, FALSE, FALSE, 0);
    g_hash_table_insert(ui_items, g_strdup(service), ui);
}

static void on_item_removed(const char *service, gpointer user_data) {
    (void)user_data;
    if (ui_items && service) {
        g_hash_table_remove(ui_items, service);
    }
}

static void on_item_updated(SniItem *item, gpointer user_data) {
    (void)user_data;
    const char *service = sni_item_get_service(item);
    if (ui_items && service) {
        TrayUiItem *ui = g_hash_table_lookup(ui_items, service);
        if (ui) update_ui_item(ui);
    }
}

static SniBackendCallbacks callbacks = {
    .on_item_added = on_item_added,
    .on_item_removed = on_item_removed,
    .on_item_updated = on_item_updated
};

GtkWidget* create_sni_tray_widget(void) {
    if (tray_box) return tray_box;

    tray_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    sni_backend_init(&callbacks, NULL);

    return tray_box;
}
