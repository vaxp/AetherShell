#include <gtk/gtk.h>

#include "panel-context-menu.h"

static void on_menu_preferences(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    char *settings_bin = g_find_program_in_path("venom-panel-settings");
    if (settings_bin) {
        g_spawn_command_line_async(settings_bin, NULL);
        g_free(settings_bin);
        return;
    }
    g_spawn_command_line_async("./venom-panel-settings", NULL);
}

static void on_menu_restart(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    char *panel_bin = g_find_program_in_path("venom-panel");
    const char *panel_cmd = panel_bin ? panel_bin : "./venom-panel";
    char *cmd = g_strdup_printf("bash -c 'pidof venom-panel | xargs -r kill; nohup %s >/dev/null 2>&1 &'", panel_cmd);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
    g_free(panel_bin);
}

static gboolean on_panel_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    (void)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkWidget *menu = gtk_menu_new();

        GtkWidget *item_prefs = gtk_menu_item_new_with_label("Panel Preferences...");
        g_signal_connect(item_prefs, "activate", G_CALLBACK(on_menu_preferences), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_prefs);

        GtkWidget *item_restart = gtk_menu_item_new_with_label("Restart Panel");
        g_signal_connect(item_restart, "activate", G_CALLBACK(on_menu_restart), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_restart);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

void panel_context_menu_attach(GtkWidget *panel_window)
{
    if (!panel_window) return;
    g_signal_connect(panel_window, "button-press-event", G_CALLBACK(on_panel_button_press), NULL);
}

