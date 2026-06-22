#include "ui/dock_config.h"
#include "ui/x11_integration.h"
#include "ui/dock_ui.h"

static GtkCssProvider *dynamic_css_provider = NULL;

static void apply_dock_colors(void) {
    if (!dynamic_css_provider) {
        dynamic_css_provider = gtk_css_provider_new();
        GdkDisplay *display = gdk_display_get_default();
        GdkScreen *screen = gdk_display_get_default_screen(display);
        gtk_style_context_add_provider_for_screen(screen,
                                                  GTK_STYLE_PROVIDER(dynamic_css_provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_USER);
    }

    gchar *bg_color = current_dock_bg_color ? current_dock_bg_color : "rgba(0, 0, 0, 0.300)";
    gchar *ctx_color = current_context_menu_bg_color ? current_context_menu_bg_color : "rgba(8, 10, 14, 0.78)";
    gchar *ind_color = current_indicator_color ? current_indicator_color : "#00fcd2";

    gchar *css = g_strdup_printf(
        "#dock-box { background-color: %s; }\n"
        "menu#dock-context-menu, menu.dock-context-menu, #context-menu-box { background-color: %s; }\n"
        ".running-app #indicator-dot { background-color: %s; box-shadow: 0 0 4px %s; }\n",
        bg_color, ctx_color, ind_color, ind_color
    );

    GError *error = NULL;
    gtk_css_provider_load_from_data(dynamic_css_provider, css, -1, &error);
    if (error) {
        g_warning("Failed to apply dynamic CSS: %s", error->message);
        g_error_free(error);
    }
    g_free(css);

    /* Also trigger a redraw to ensure launch ring updates */
    if (box) {
        gtk_widget_queue_draw(box);
    }
}

void load_dock_config(void) {
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "vaxp/dock", NULL);
    gchar *config_file = g_build_filename(config_dir, "dock_state.vaxp", NULL);

    g_mkdir_with_parents(config_dir, 0755);

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    gboolean write_needed = FALSE;

    if (!g_key_file_load_from_file(key_file, config_file, G_KEY_FILE_NONE, &error)) {
        /* Check if it's the old format (just position) or empty */
        gchar *contents = NULL;
        if (g_file_get_contents(config_file, &contents, NULL, NULL)) {
            g_strstrip(contents);
            if (g_ascii_strcasecmp(contents, "top") == 0 ||
                g_ascii_strcasecmp(contents, "bottom") == 0 ||
                g_ascii_strcasecmp(contents, "left") == 0 ||
                g_ascii_strcasecmp(contents, "right") == 0) {
                g_key_file_set_string(key_file, "Dock", "Position", contents);
            } else {
                g_key_file_set_string(key_file, "Dock", "Position", "bottom");
            }
            g_free(contents);
        } else {
            g_key_file_set_string(key_file, "Dock", "Position", "bottom");
        }
        
        g_key_file_set_string(key_file, "Dock", "BackgroundColor", "rgba(0, 0, 0, 0.300)");
        g_key_file_set_string(key_file, "Dock", "ContextMenuColor", "rgba(8, 10, 14, 0.78)");
        g_key_file_set_string(key_file, "Dock", "IndicatorColor", "#00fcd2");
        g_key_file_set_string(key_file, "Dock", "LaunchRingColor", "#00fcd2");
        g_key_file_set_integer(key_file, "Dock", "LaunchAnimation", 1);
        write_needed = TRUE;
        g_clear_error(&error);
    }

    gchar *pos_str = g_key_file_get_string(key_file, "Dock", "Position", NULL);
    if (pos_str) {
        if (g_ascii_strcasecmp(pos_str, "top") == 0) current_dock_position = DOCK_POSITION_TOP;
        else if (g_ascii_strcasecmp(pos_str, "left") == 0) current_dock_position = DOCK_POSITION_LEFT;
        else if (g_ascii_strcasecmp(pos_str, "right") == 0) current_dock_position = DOCK_POSITION_RIGHT;
        else current_dock_position = DOCK_POSITION_BOTTOM;
        g_free(pos_str);
    }

    g_free(current_dock_bg_color);
    current_dock_bg_color = g_key_file_get_string(key_file, "Dock", "BackgroundColor", NULL);

    g_free(current_context_menu_bg_color);
    current_context_menu_bg_color = g_key_file_get_string(key_file, "Dock", "ContextMenuColor", NULL);

    g_free(current_indicator_color);
    current_indicator_color = g_key_file_get_string(key_file, "Dock", "IndicatorColor", NULL);

    gchar *ring_color_str = g_key_file_get_string(key_file, "Dock", "LaunchRingColor", NULL);
    if (ring_color_str) {
        gdk_rgba_parse(&current_launch_ring_color, ring_color_str);
        g_free(ring_color_str);
    }

    if (g_key_file_has_key(key_file, "Dock", "LaunchAnimation", NULL)) {
        current_launch_animation = g_key_file_get_integer(key_file, "Dock", "LaunchAnimation", NULL);
        if (current_launch_animation < 1 || current_launch_animation > 7) {
            current_launch_animation = 1;
        }
    } else {
        current_launch_animation = 1;
        g_key_file_set_integer(key_file, "Dock", "LaunchAnimation", 1);
        write_needed = TRUE;
    }

    if (write_needed) {
        gchar *data = g_key_file_to_data(key_file, NULL, NULL);
        if (data) {
            g_file_set_contents(config_file, data, -1, NULL);
            g_free(data);
        }
    }

    g_key_file_free(key_file);
    g_free(config_file);
    g_free(config_dir);

    apply_dock_colors();
}

void apply_dock_position(void) {
    if (!main_window || !box || !system_separator) return;

    gboolean is_vertical = (current_dock_position == DOCK_POSITION_LEFT || current_dock_position == DOCK_POSITION_RIGHT);

    gtk_orientable_set_orientation(GTK_ORIENTABLE(box), is_vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(system_separator), is_vertical ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);

    if (is_vertical) {
        gtk_widget_set_margin_top(system_separator, 6);
        gtk_widget_set_margin_bottom(system_separator, 8);
        gtk_widget_set_margin_start(system_separator, 0);
        gtk_widget_set_margin_end(system_separator, 0);
        gtk_widget_set_size_request(system_separator, 28, 1);
        gtk_window_set_default_size(GTK_WINDOW(main_window), 60, -1);
    } else {
        gtk_widget_set_margin_start(system_separator, 6);
        gtk_widget_set_margin_end(system_separator, 8);
        gtk_widget_set_margin_top(system_separator, 0);
        gtk_widget_set_margin_bottom(system_separator, 0);
        gtk_widget_set_size_request(system_separator, 1, 28);
        gtk_window_set_default_size(GTK_WINDOW(main_window), -1, 60);
    }

    if (is_wayland_session) {
        if (current_dock_position == DOCK_POSITION_TOP) {
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, 2);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        } else if (current_dock_position == DOCK_POSITION_LEFT) {
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, 2);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
        } else if (current_dock_position == DOCK_POSITION_RIGHT) {
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, 2);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
        } else {
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, 2);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, 0);
            gtk_layer_set_margin(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        }
    } else {
        GdkDisplay *display = gtk_widget_get_display(main_window);
        GdkRectangle primary_geom = {0};
        
        if (get_primary_monitor_geometry(display, &primary_geom)) {
            GtkAllocation alloc;
            gtk_widget_get_allocation(main_window, &alloc);
            int w = is_vertical ? 60 : alloc.width;
            int h = is_vertical ? alloc.height : 60;
            
            int x, y;
            if (current_dock_position == DOCK_POSITION_TOP) {
                x = primary_geom.x + (primary_geom.width - w) / 2;
                y = primary_geom.y + 2;
            } else if (current_dock_position == DOCK_POSITION_LEFT) {
                x = primary_geom.x + 2;
                y = primary_geom.y + (primary_geom.height - h) / 2;
            } else if (current_dock_position == DOCK_POSITION_RIGHT) {
                x = primary_geom.x + primary_geom.width - w - 2;
                y = primary_geom.y + (primary_geom.height - h) / 2;
            } else {
                x = primary_geom.x + (primary_geom.width - w) / 2;
                y = primary_geom.y + primary_geom.height - h - 2;
            }
            gtk_window_move(GTK_WINDOW(main_window), x, y);
        }
        
        on_dock_realize(main_window, NULL);
    }

    if (current_dock_position == DOCK_POSITION_TOP) {
        gtk_window_set_gravity(GTK_WINDOW(main_window), GDK_GRAVITY_NORTH);
    } else if (current_dock_position == DOCK_POSITION_LEFT) {
        gtk_window_set_gravity(GTK_WINDOW(main_window), GDK_GRAVITY_WEST);
    } else if (current_dock_position == DOCK_POSITION_RIGHT) {
        gtk_window_set_gravity(GTK_WINDOW(main_window), GDK_GRAVITY_EAST);
    } else {
        gtk_window_set_gravity(GTK_WINDOW(main_window), GDK_GRAVITY_SOUTH);
    }

    update_window_list();
}

void on_dock_config_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor;
    (void)file;
    (void)other_file;
    (void)user_data;

    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event_type == G_FILE_MONITOR_EVENT_CREATED) {
        DockPosition old_pos = current_dock_position;
        load_dock_config();
        if (current_dock_position != old_pos) {
            apply_dock_position();
        }
    }
}
