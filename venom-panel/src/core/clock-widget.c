#include "clock-widget.h"
#include <time.h>

static gboolean update_clock(gpointer data) {
    GtkLabel *label = GTK_LABEL(data);
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%a %b %d  %I:%M %p", tm_info);
    
    gtk_label_set_text(label, buffer);
    
    return TRUE; /* Continue calling */
}

static void on_clock_label_destroy(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    guint timer_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "venom-clock-timer-id"));
    if (timer_id) {
        g_source_remove(timer_id);
        g_object_set_data(G_OBJECT(widget), "venom-clock-timer-id", NULL);
    }
}

GtkWidget* create_clock_widget(void) {
    GtkWidget *label = gtk_label_new("");
    
    /* Style */
    GtkStyleContext *context = gtk_widget_get_style_context(label);
    gtk_style_context_add_class(context, "clock-label");
    
    /* Initial update */
    update_clock(label);
    
    /* Update every second */
    guint timer_id = g_timeout_add_seconds(1, update_clock, label);
    g_object_set_data(G_OBJECT(label), "venom-clock-timer-id", GUINT_TO_POINTER(timer_id));
    g_signal_connect(label, "destroy", G_CALLBACK(on_clock_label_destroy), NULL);
    
    return label;
}
