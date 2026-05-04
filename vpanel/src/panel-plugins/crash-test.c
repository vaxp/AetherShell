#include <gtk/gtk.h>
#include <signal.h>
#include <stdlib.h>
#include "vpanel-plugin-api.h"

/* 
 * This plugin is designed to intentionally crash when the user clicks its button,
 * or when it receives a specific configuration command, to test the vpanel 
 * isolation (sandbox) system.
 */

static void on_crash_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    (void)user_data;
    
    g_print("[CrashTest] Button clicked. Initiating intentional crash (SIGSEGV)...\n");
    
    /* Intentionally cause a segmentation fault */
    volatile int *null_ptr = NULL;
    *null_ptr = 42;
}

static GtkWidget* crash_test_create_widget(void)
{
    GtkWidget *btn = gtk_button_new_with_label("💥 Crash Me");
    
    /* Make it look slightly warning-ish */
    GtkStyleContext *context = gtk_widget_get_style_context(btn);
    gtk_style_context_add_class(context, "destructive-action");
    
    g_signal_connect(btn, "clicked", G_CALLBACK(on_crash_clicked), NULL);
    
    gtk_widget_show_all(btn);
    return btn;
}

static void crash_test_on_config_changed(GtkWidget *widget, const char *key, const char *value)
{
    (void)widget;
    
    /* Alternative way to crash: via panel config reload */
    if (g_strcmp0(key, "crash_on_load") == 0 && g_strcmp0(value, "true") == 0) {
        g_print("[CrashTest] config crash_on_load=true received. Crashing now...\n");
        abort();
    }
}

VenomPanelPluginAPIv3* venom_panel_plugin_init_v3(void)
{
    static VenomPanelPluginAPIv3 api = {
        .api_version       = VENOM_PANEL_PLUGIN_API_VERSION,
        .struct_size       = sizeof(VenomPanelPluginAPIv3),
        .name              = "Crash Test",
        .description       = "A plugin that intentionally crashes to test the sandbox.",
        .author            = "Antigravity",
        .version           = "1.0",
        .icon_name         = "dialog-error",
        .zone              = VENOM_PLUGIN_ZONE_RIGHT,
        .expand            = FALSE,
        .padding           = 4,
        
        /* Distinct visual style to stand out */
        .visuals = {
            .bg_type       = VENOM_PLUGIN_BG_SOLID,
            .bg_r = 0.5, .bg_g = 0.1, .bg_b = 0.1, .bg_a = 0.8,
            .border_enabled = TRUE,
            .border_r = 1.0, .border_g = 0.0, .border_b = 0.0,
            .border_a = 1.0, .border_width = 1, .border_radius = 4,
        },
        
        .create_widget     = crash_test_create_widget,
        .on_config_changed = crash_test_on_config_changed,
    };
    return &api;
}
