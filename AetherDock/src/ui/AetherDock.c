#include "ui/globals.h"
#include <unistd.h>
#include "ui/dock_config.h"
#include "ui/wayland_integration.h"
#include "ui/x11_integration.h"
#include "ui/app_tracker.h"
#include "ui/dock_ui.h"
#include "ui/context_menu.h"
#include "launcher.h"
#include "trash.h"
#include "drives.h"
#include "utils.h"
#include <gtk-layer-shell.h>

static void setup_dock_window_layer(GtkWidget *window) {
    if (is_wayland_session) {
        gtk_layer_init_for_window(GTK_WINDOW(window));
        gtk_layer_set_namespace(GTK_WINDOW(window), "AetherDock");
        gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);

        if (current_dock_position == DOCK_POSITION_TOP) {
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 2);
        } else if (current_dock_position == DOCK_POSITION_LEFT) {
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, 2);
        } else if (current_dock_position == DOCK_POSITION_RIGHT) {
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, 2);
        } else {
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, 2);
        }

        gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(window));
    }
}

int main(int argc, char *argv[]) {
    /* Suppress accessibility bus warning */
    g_setenv("GTK_A11Y", "none", TRUE);

    gtk_init(&argc, &argv);
    dock_executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (!dock_executable_path && argc > 0 && argv[0] && argv[0][0] != '\0') {
        if (!g_path_is_absolute(argv[0])) {
            gchar *cwd = g_get_current_dir();
            dock_executable_path = g_build_filename(cwd, argv[0], NULL);
            g_free(cwd);
        } else {
            dock_executable_path = g_strdup(argv[0]);
        }
    }
    
    /* Change working directory to home to prevent child processes from inheriting 
       the dock's launch directory (which breaks apps that accidentally read our style.css) */
    if (chdir(g_get_home_dir()) != 0) {
        g_warning("Failed to change directory to home.");
    }

    load_dock_config();
    
    /* Set up file monitor for config */
    GFile *config_file_obj;
    GFileMonitor *monitor;
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "vaxp/dock", NULL);
    gchar *config_path = g_build_filename(config_dir, "dock_state.vaxp", NULL);
    
    config_file_obj = g_file_new_for_path(config_path);
    monitor = g_file_monitor_file(config_file_obj, G_FILE_MONITOR_NONE, NULL, NULL);
    if (monitor) {
        g_signal_connect(monitor, "changed", G_CALLBACK(on_dock_config_changed), NULL);
    }
    g_object_unref(config_file_obj);
    g_free(config_path);
    g_free(config_dir);

    is_wayland_session = TRUE;
    init_wayland_protocols();

    /* Load CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);
    
    gtk_style_context_add_provider_for_screen(screen,
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    GError *error = NULL;
    gchar *css_path = dock_build_resource_path("style.css");
    if (!gtk_css_provider_load_from_path(provider, css_path, &error)) {
        g_warning("Failed to load CSS: %s", error->message);
        g_error_free(error);
    }
    g_free(css_path);
    g_object_unref(provider);

    /* UI Setup */
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "AetherDock");
    
    /* Get screen dimensions via GdkMonitor (non-deprecated) */
    GdkRectangle primary_geom = {0};
    /* Set as DOCK - explicitly declare as dock window */
    gtk_window_set_type_hint(GTK_WINDOW(main_window), GDK_WINDOW_TYPE_HINT_DOCK);
    
    gboolean is_vertical = (current_dock_position == DOCK_POSITION_LEFT || current_dock_position == DOCK_POSITION_RIGHT);
    if (is_vertical) {
        gtk_window_set_default_size(GTK_WINDOW(main_window), 60, -1);
    } else {
        gtk_window_set_default_size(GTK_WINDOW(main_window), -1, 60);
    }

    if (get_primary_monitor_geometry(display, &primary_geom)) {
        if (is_vertical) {
            gtk_window_move(GTK_WINDOW(main_window),
                            current_dock_position == DOCK_POSITION_LEFT ? primary_geom.x : primary_geom.x + primary_geom.width,
                            primary_geom.y + (primary_geom.height / 2));
        } else {
            gtk_window_move(GTK_WINDOW(main_window),
                            primary_geom.x + (primary_geom.width / 2),
                            current_dock_position == DOCK_POSITION_TOP ? primary_geom.y : primary_geom.y + primary_geom.height);
        }
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
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_widget_set_app_paintable(main_window, TRUE);
    
    /* Window properties for dock behavior */
    gtk_window_set_keep_above(GTK_WINDOW(main_window), TRUE);
    gtk_window_stick(GTK_WINDOW(main_window));
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(main_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(main_window), TRUE);
    setup_dock_window_layer(main_window);

    /* Enable transparency */
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(main_window, visual);
    }

    gboolean is_vertical_box = (current_dock_position == DOCK_POSITION_LEFT || current_dock_position == DOCK_POSITION_RIGHT);
    box = gtk_box_new(is_vertical_box ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_name(box, "dock-box"); /* ID for CSS */
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    /* Margins removed from C code to rely strictly on window size */
    gtk_container_add(GTK_CONTAINER(main_window), box);

    g_signal_connect(main_window, "draw", G_CALLBACK(on_window_draw), NULL);
    g_signal_connect(main_window, "realize", G_CALLBACK(on_dock_realize), NULL);
    g_signal_connect(main_window, "size-allocate", G_CALLBACK(on_window_size_allocate), NULL);
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_dock_window_destroy), NULL);

    /* Initialize window groups hash table */
    window_groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    launching_apps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* Load pinned apps */
    load_pinned_apps();

    /* Create right-side buttons (packed with pack_end, so rightmost = launcher) */
    create_launcher_button(box);    /* Rightmost */
    create_trash_button(box);       /* Next to launcher */
    create_drives_area(box);        /* Drives appear to the left of trash */
    system_separator = gtk_separator_new(is_vertical_box ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_name(system_separator, "system-separator");
    gtk_widget_set_valign(system_separator, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(system_separator, GTK_ALIGN_CENTER);
    
    if (is_vertical_box) {
        gtk_widget_set_margin_top(system_separator, 6);
        gtk_widget_set_margin_bottom(system_separator, 8);
        gtk_widget_set_margin_start(system_separator, 0);
        gtk_widget_set_margin_end(system_separator, 0);
        gtk_widget_set_size_request(system_separator, 28, 1);
    } else {
        gtk_widget_set_margin_start(system_separator, 6);
        gtk_widget_set_margin_end(system_separator, 8);
        gtk_widget_set_margin_top(system_separator, 0);
        gtk_widget_set_margin_bottom(system_separator, 0);
        gtk_widget_set_size_request(system_separator, 1, 28);
    }
    gtk_box_pack_end(GTK_BOX(box), system_separator, FALSE, FALSE, 0);
    gtk_widget_show(system_separator);

    /* Initial update */
    update_window_list();

    gtk_widget_show_all(main_window);
    
    gtk_main();

    if (wayland_event_source_id != 0) {
        g_source_remove(wayland_event_source_id);
        wayland_event_source_id = 0;
    }

    if (toplevel_manager) {
        zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
        zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
    }

    if (wl_seat_obj) {
        wl_seat_destroy(wl_seat_obj);
    }

    if (wayland_toplevels) {
        g_hash_table_destroy(wayland_toplevels);
    }

    g_free(dock_executable_path);

    return 0;
}
