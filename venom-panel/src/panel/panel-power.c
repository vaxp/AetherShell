#include <gtk/gtk.h>

#include "panel-power.h"
#include "power_actions.h"

static GtkWidget *power_actions_box = NULL;

static void set_widget_weak(GtkWidget **slot, GtkWidget *widget)
{
    if (*slot) g_object_remove_weak_pointer(G_OBJECT(*slot), (gpointer *)slot);
    *slot = widget;
    if (widget) g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *)slot);
}

static void on_power_action_toggle(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    if (!power_actions_box) return;

    if (gtk_widget_get_visible(power_actions_box)) {
        gtk_widget_hide(power_actions_box);
    } else {
        gtk_widget_set_no_show_all(power_actions_box, FALSE);
        gtk_widget_show_all(power_actions_box);
        gtk_widget_set_visible(power_actions_box, TRUE);
    }
}

static void on_power_action_shutdown(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    venom_power_off();
}
static void on_power_action_reboot(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    venom_reboot();
}
static void on_power_action_suspend(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    venom_sleep();
}
static void on_power_action_logout(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    venom_logout();
}

GtkWidget *panel_power_widget_new(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *actions_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *actions_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    struct {
        const char *icon;
        const char *tip;
        GCallback cb;
    } btns[] = {
        {"system-shutdown-symbolic", "Shutdown", G_CALLBACK(on_power_action_shutdown)},
        {"system-reboot-symbolic", "Reboot", G_CALLBACK(on_power_action_reboot)},
        {"system-suspend-symbolic", "Sleep", G_CALLBACK(on_power_action_suspend)},
        {"system-log-out-symbolic", "Logout", G_CALLBACK(on_power_action_logout)},
    };
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = gtk_button_new_from_icon_name(btns[i].icon, GTK_ICON_SIZE_MENU);
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(b, btns[i].tip);
        g_signal_connect(b, "clicked", btns[i].cb, NULL);
        gtk_box_pack_start(GTK_BOX(actions_box), b, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(actions_container), actions_box, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(actions_box, TRUE);
    gtk_widget_hide(actions_box);

    GtkWidget *toggle_btn = gtk_button_new_from_icon_name("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(toggle_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(toggle_btn, "Power Menu");
    g_signal_connect(toggle_btn, "clicked", G_CALLBACK(on_power_action_toggle), NULL);
    gtk_box_pack_start(GTK_BOX(actions_container), toggle_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), actions_container, FALSE, FALSE, 0);

    set_widget_weak(&power_actions_box, actions_box);

    return outer;
}

void panel_power_prepare_reload(void)
{
    set_widget_weak(&power_actions_box, NULL);
}

void panel_power_cleanup(void)
{
    panel_power_prepare_reload();
}
