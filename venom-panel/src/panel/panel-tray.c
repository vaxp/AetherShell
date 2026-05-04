#include <gtk/gtk.h>

#include "panel-tray.h"
#include "sni-client.h"
#include "shot-client.h"

static GtkWidget *g_rec_stop_button = NULL;

static void set_rec_button_weak(GtkWidget *widget)
{
    if (g_rec_stop_button)
    {
        g_object_remove_weak_pointer(G_OBJECT(g_rec_stop_button), (gpointer *)&g_rec_stop_button);
    }

    g_rec_stop_button = widget;

    if (widget)
    {
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *)&g_rec_stop_button);
    }
}

static void on_rec_state_changed(gboolean is_recording, gpointer user_data)
{
    (void)user_data;

    if (!g_rec_stop_button)
    {
        return;
    }

    if (is_recording)
    {
        gtk_widget_set_no_show_all(g_rec_stop_button, FALSE);
        gtk_widget_show_all(g_rec_stop_button);
        gtk_widget_set_visible(g_rec_stop_button, TRUE);
    } else
    {
        gtk_widget_hide(g_rec_stop_button);
    }
}

static void on_stop_recording_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    shot_stop_record();
}

GtkWidget *panel_tray_widget_new(void)
{
    shot_client_init();

    GtkWidget *rec_stop_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(rec_stop_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(rec_stop_button, "Stop Recording");

    GtkWidget *rec_icon = gtk_image_new_from_icon_name("media-playback-stop-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(rec_stop_button), rec_icon);

    GtkStyleContext *rec_ctx = gtk_widget_get_style_context(rec_stop_button);
    GtkCssProvider *rec_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(rec_css, "button { color: #ff5555; }", -1, NULL);
    gtk_style_context_add_provider(rec_ctx, GTK_STYLE_PROVIDER(rec_css), 800);
    g_object_unref(rec_css);

    g_signal_connect(rec_stop_button, "clicked", G_CALLBACK(on_stop_recording_clicked), NULL);
    gtk_widget_set_no_show_all(rec_stop_button, TRUE);
    gtk_widget_hide(rec_stop_button);
    shot_client_on_recording_state(on_rec_state_changed, NULL);

    GtkWidget *tray_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *tray_box = create_sni_tray_widget();

    gtk_box_pack_start(GTK_BOX(tray_container), rec_stop_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tray_container), tray_box, FALSE, FALSE, 0);

    set_rec_button_weak(rec_stop_button);
    return tray_container;
}

void panel_tray_prepare_reload(void)
{
    set_rec_button_weak(NULL);
}

void panel_tray_cleanup(void)
{
    panel_tray_prepare_reload();
    shot_client_cleanup();
}
