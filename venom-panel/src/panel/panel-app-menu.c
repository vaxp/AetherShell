#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "panel-app-menu.h"
#include "panel-geometry.h"
#include "window-backend.h"

/* ================================================================== */
/*  STYLE                                                              */
/* ================================================================== */

static const char LAUNCHER_CSS[] =
"window.launcher {"
"  background: rgba(0, 0, 0, 0);"
"}"
".launcher-root {"
"  background: rgba(0, 0, 0, 0.39);"
"  border-radius: 12px;"
"  border: 1px solid rgba(255,255,255,0.42);"
"  box-shadow: 0 30px 80px rgba(9, 20, 38, 0.28);"
"  padding: 18px 18px 16px 18px;"
"}"
".launcher-header {"
"  margin-bottom: 12px;"
"}"
".launcher-title {"
"  font-size: 28px;"
"  font-weight: 700;"
"  color: #ffffff;"
"}"
".launcher-subtitle {"
"  margin-top: 3px;"
"  font-size: 11px;"
"  color: #ffffff;"
"}"
".search-row {"
"  background: rgba(66, 65, 65, 0.15);"
"  border-radius: 14px;"
"  border: 1px solid rgba(161,178,198,0.38);"
"  padding: 8px 12px;"
"  margin-bottom: 18px;"
"}"
"entry.search-entry {"
"  background: transparent;"
"  border: none;"
"  box-shadow: none;"
"  font-size: 13px;"
"  color: #ffffff;"
"  caret-color: #ffffff;"
"  padding: 4px 3px;"
"}"
".content-stack {"
"  background: transparent;"
"}"
".section-head {"
"  margin: 0 4px 10px 4px;"
"}"
".section-title {"
"  font-size: 12px;"
"  font-weight: 700;"
"  color: #ffffff;"
"}"
".section-link {"
"  border-radius: 10px;"
"  padding: 5px 12px;"
"  background: rgba(255, 255, 255, 0);"
"  color: #ffffff;"
"  border: 1px solid rgba(164,181,198,0.3);"
"  font-size: 11px;"
"  font-weight: 600;"
"}"
".section-link:hover {"
"  background: rgba(231, 241, 255, 0);"
"  color: #ffffff;"
"}"
".pinned-grid {"
"  margin: 0 0 18px 0;"
"}"
".pinned-app {"
"  border-radius: 0;"
"  min-width: 0;"
"  min-height: 0;"
"  padding: 0;"
"  background: transparent;"
"  border: none;"
"}"
".pinned-app:hover {"
"  background: transparent;"
"  border-color: transparent;"
"}"
".pinned-name {"
"  margin-top: 7px;"
"  font-size: 10px;"
"  color: #ffffff;"
"}"
".all-apps-wrap {"
"  background: rgba(0, 0, 0, 0);"
"  border-radius: 20px;"
"  border: none;"
"  padding: 8px;"
"}"
".all-apps-list {"
"  background: transparent;"
"}"
".all-apps-grid {"
"  margin: 0;"
"  border-radius: 0;"
"  min-width: 0;"
"  min-height: 0;"
"  padding: 0;"
"  background: transparent;"
"  border: none;"
"}"
".all-app-tile {"
"  border-radius: 0;"
"  min-width: 0;"
"  min-height: 0;"
"  padding: 0;"
"  background: transparent;"
"  border: none;"
"}"
".all-app-tile:hover {"
"  background: transparent;"
"  border-color: transparent;"
"}"
".all-app-name {"
"  margin-top: 8px;"
"  font-size: 11px;"
"  font-weight: 600;"
"  color: #ffffff;"
"}"
".all-app-category {"
"  margin-top: 2px;"
"  font-size: 10px;"
"  color: #ffffff;"
"}"
".footer {"
"  margin-top: 18px;"
"  padding: 12px 14px;"
"  background: rgba(0, 0, 0, 0);"
"  border-radius: 18px;"
"  border: 1px solid rgba(174,187,202,0.28);"
"}"
".user-avatar {"
"  min-width: 32px;"
"  min-height: 32px;"
"  border-radius: 50%;"
"  background: rgba(119, 156, 154, 0.38);"
"  color: #ffffff;"
"  font-size: 11px;"
"  font-weight: 700;"
"}"
".username {"
"  font-size: 12px;"
"  font-weight: 600;"
"  color: #ffffff;"
"}"
".footer-subtitle {"
"  font-size: 10px;"
"  color: #ffffff;"
"}"
".pwr-btn {"
"  border-radius: 11px;"
"  padding: 7px 11px;"
"  background: rgba(0, 0, 0, 0);"
"  border: 1px solid rgba(255, 255, 255, 0.61);"
"  color: rgba(93, 170, 151, 0.99);"
"  font-size: 13px;"
"}"
".pwr-btn:hover {"
"  background: rgba(114, 105, 105, 0.49);"
"  color: #0a63d2;"
"}"
"scrolledwindow {"
"  background: transparent;"
"}"
"scrollbar {"
"  opacity: 0;"
"  min-width: 0;"
"  min-height: 0;"
"}";

/* ================================================================== */
/*  DATA TYPES                                                         */
/* ================================================================== */

#define MAX_PINNED_APPS 18
typedef struct {
    GtkWidget *toggle_btn;
    GtkWidget *launcher_win;
    GtkWidget *anchor_button;
    GtkWidget *search_entry;
    GtkWidget *stack;
    GtkWidget *home_page;
    GtkWidget *all_apps_page;
    GtkWidget *pinned_grid;
    GtkWidget *all_apps_list;
    GtkWidget *all_apps_button;
    GtkWidget *back_button;
    gboolean showing_all_apps;
} AppMenuState;

static GList *full_apps = NULL;
static gboolean data_loaded = FALSE;
static gboolean pinned_loaded = FALSE;
static GList *pinned_ids = NULL;
static GList *app_menu_instances = NULL;

/* ================================================================== */
/*  FORWARD DECLARATIONS                                               */
/* ================================================================== */

static void refresh_app_menu_state(AppMenuState *state);
static void populate_pinned_grid(AppMenuState *state);
static void populate_all_apps_list(AppMenuState *state, const char *query);
static void switch_to_home_page(AppMenuState *state);
static void switch_to_all_apps_page(AppMenuState *state);
static void position_launcher_window(AppMenuState *state);

/* ================================================================== */
/*  SMALL HELPERS                                                      */
/* ================================================================== */

static inline void box_append(GtkBox *box, GtkWidget *child)
{
    gtk_box_pack_start(box, child, FALSE, FALSE, 0);
}

static inline void box_append_expand(GtkBox *box, GtkWidget *child)
{
    gtk_box_pack_start(box, child, TRUE, TRUE, 0);
}

static inline void add_css(GtkWidget *widget, const char *cls)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cls);
}

static inline void remove_css(GtkWidget *widget, const char *cls)
{
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget), cls);
}

static gchar *resolve_asset_path(const char *filename)
{
    gchar *path;
    gchar *exe;
    gchar *dir;
    gchar *candidate;

    path = g_build_filename(g_get_current_dir(), filename, NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return path;
    g_free(path);

    path = g_build_filename(g_get_user_data_dir(), "venom-panel", filename, NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return path;
    g_free(path);

    exe = g_file_read_link("/proc/self/exe", NULL);
    if (!exe) return NULL;

    dir = g_path_get_dirname(exe);
    candidate = g_build_filename(dir, filename, NULL);
    g_free(exe);
    g_free(dir);

    if (g_file_test(candidate, G_FILE_TEST_EXISTS)) return candidate;

    g_free(candidate);
    return NULL;
}

static gchar *get_app_menu_config_path(const char *filename)
{
    gchar *dir = g_build_filename(g_get_user_config_dir(), "venom-panel", NULL);
    gchar *path;
    g_mkdir_with_parents(dir, 0755);
    path = g_build_filename(dir, filename, NULL);
    g_free(dir);
    return path;
}

static const char *get_app_id(GAppInfo *app)
{
    const char *id = g_app_info_get_id(app);
    return id ? id : "";
}

static GAppInfo *find_app_by_id(const char *app_id)
{
    if (!app_id) return NULL;

    for (GList *l = full_apps; l; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (g_strcmp0(get_app_id(app), app_id) == 0) {
            return app;
        }
    }

    return NULL;
}

static const char *get_app_category_name(GAppInfo *app)
{
    if (G_IS_DESKTOP_APP_INFO(app)) {
        const char *cats = g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(app));
        if (cats) {
            if (strstr(cats, "Settings")) return "Settings";
            if (strstr(cats, "Utility")) return "Utilities";
            if (strstr(cats, "Development")) return "Development";
            if (strstr(cats, "Graphics")) return "Graphics";
            if (strstr(cats, "Network")) return "Internet";
            if (strstr(cats, "AudioVideo")) return "Multimedia";
            if (strstr(cats, "Office")) return "Office";
            if (strstr(cats, "System")) return "System";
        }
    }

    return "Application";
}

static gboolean app_matches_query(GAppInfo *app, const char *query)
{
    char *lower_query;
    char *name_lower;
    const char *name;
    gboolean match = FALSE;

    if (!query || query[0] == '\0') return TRUE;

    name = g_app_info_get_name(app);
    if (!name) return FALSE;

    lower_query = g_utf8_strdown(query, -1);
    name_lower = g_utf8_strdown(name, -1);
    match = strstr(name_lower, lower_query) != NULL;

    g_free(lower_query);
    g_free(name_lower);
    return match;
}

static gint compare_apps_by_name(gconstpointer a, gconstpointer b)
{
    GAppInfo *app_a = G_APP_INFO((gpointer)a);
    GAppInfo *app_b = G_APP_INFO((gpointer)b);
    const char *name_a = g_app_info_get_name(app_a);
    const char *name_b = g_app_info_get_name(app_b);
    return g_utf8_collate(name_a ? name_a : "", name_b ? name_b : "");
}

static void show_feedback_dialog(AppMenuState *state, GtkMessageType type, const char *primary, const char *secondary)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(state->launcher_win),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               type,
                                               GTK_BUTTONS_OK,
                                               "%s",
                                               primary);
    if (secondary && secondary[0] != '\0') {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
    }
    gtk_window_set_title(GTK_WINDOW(dialog), "App Menu");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* ================================================================== */
/*  PINNED STORAGE                                                     */
/* ================================================================== */

static gboolean string_list_contains(GList *list, const char *value)
{
    for (GList *l = list; l; l = l->next) {
        if (g_strcmp0((const char *)l->data, value) == 0) return TRUE;
    }
    return FALSE;
}

static void string_list_remove(GList **list, const char *value)
{
    for (GList *l = *list; l; l = l->next) {
        if (g_strcmp0((const char *)l->data, value) == 0) {
            g_free(l->data);
            *list = g_list_delete_link(*list, l);
            return;
        }
    }
}

static void load_string_list_from_file(GList **target, const char *filename, gboolean *loaded_flag)
{
    gchar *path;
    gchar *contents = NULL;
    gchar **lines;
    gsize len = 0;

    if (*loaded_flag) return;
    *loaded_flag = TRUE;

    path = get_app_menu_config_path(filename);
    if (!g_file_get_contents(path, &contents, &len, NULL) || !contents) {
        g_free(path);
        return;
    }

    lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (line[0] != '\0' && !string_list_contains(*target, line)) {
            *target = g_list_append(*target, g_strdup(line));
        }
    }

    g_strfreev(lines);
    g_free(contents);
    g_free(path);
}

static void save_string_list_to_file(GList *list, const char *filename)
{
    gchar *path = get_app_menu_config_path(filename);
    GString *data = g_string_new(NULL);

    for (GList *l = list; l; l = l->next) {
        g_string_append(data, (const char *)l->data);
        g_string_append_c(data, '\n');
    }

    if (!g_file_set_contents(path, data->str, data->len, NULL)) {
        g_warning("[AppMenu] Failed to write %s", path);
    }

    g_string_free(data, TRUE);
    g_free(path);
}

static void ensure_pinned_loaded(void)
{
    if (pinned_loaded) return;

    load_string_list_from_file(&pinned_ids, "app-menu-pinned.conf", &pinned_loaded);
    if (!pinned_ids) {
        static const char *defaults[] = {
            "firefox.desktop",
            "org.gnome.Terminal.desktop",
            "nautilus.desktop",
            "settings.desktop",
            "org.gnome.TextEditor.desktop",
            "code.desktop",
            NULL
        };

        for (int i = 0; defaults[i]; i++) {
            pinned_ids = g_list_append(pinned_ids, g_strdup(defaults[i]));
        }
    }
}

static gboolean is_app_pinned(const char *app_id)
{
    ensure_pinned_loaded();
    return app_id && string_list_contains(pinned_ids, app_id);
}

static void save_pinned_ids(void)
{
    save_string_list_to_file(pinned_ids, "app-menu-pinned.conf");
}

static void pin_app(const char *app_id)
{
    if (!app_id || app_id[0] == '\0' || is_app_pinned(app_id)) return;
    ensure_pinned_loaded();
    pinned_ids = g_list_append(pinned_ids, g_strdup(app_id));
    save_pinned_ids();
}

static void unpin_app(const char *app_id)
{
    if (!app_id || app_id[0] == '\0') return;
    ensure_pinned_loaded();
    string_list_remove(&pinned_ids, app_id);
    save_pinned_ids();
}

/* ================================================================== */
/*  APP DATA                                                           */
/* ================================================================== */

static void load_app_data(void)
{
    GList *apps;

    if (data_loaded) return;

    apps = g_app_info_get_all();
    for (GList *l = apps; l; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (!g_app_info_should_show(app)) continue;
        if (!g_app_info_get_name(app)) continue;

        g_object_ref(app);
        full_apps = g_list_append(full_apps, app);
    }

    full_apps = g_list_sort(full_apps, compare_apps_by_name);
    g_list_free_full(apps, g_object_unref);

    ensure_pinned_loaded();
    data_loaded = TRUE;
}

/* ================================================================== */
/*  ACTIONS                                                            */
/* ================================================================== */

static void refresh_all_instances(void)
{
    for (GList *l = app_menu_instances; l; l = l->next) {
        refresh_app_menu_state((AppMenuState *)l->data);
    }
}

static gboolean create_desktop_shortcut(AppMenuState *state, GAppInfo *app)
{
    const char *desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    const char *source_path;
    gchar *fallback_dir = NULL;
    gchar *dest_path = NULL;
    gchar *basename = NULL;
    GFile *src = NULL;
    GFile *dst = NULL;
    GError *error = NULL;
    gboolean ok = FALSE;

    if (!G_IS_DESKTOP_APP_INFO(app)) {
        show_feedback_dialog(state, GTK_MESSAGE_ERROR, "Desktop shortcut failed", "This application does not expose a desktop entry.");
        return FALSE;
    }

    source_path = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app));
    if (!source_path) {
        show_feedback_dialog(state, GTK_MESSAGE_ERROR, "Desktop shortcut failed", "Desktop entry path is missing.");
        return FALSE;
    }

    if (!desktop_dir || desktop_dir[0] == '\0') {
        fallback_dir = g_build_filename(g_get_home_dir(), "Desktop", NULL);
        desktop_dir = fallback_dir;
    }

    g_mkdir_with_parents(desktop_dir, 0755);
    basename = g_path_get_basename(source_path);
    dest_path = g_build_filename(desktop_dir, basename, NULL);

    src = g_file_new_for_path(source_path);
    dst = g_file_new_for_path(dest_path);
    ok = g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
    if (ok) {
        g_chmod(dest_path, 0755);
        show_feedback_dialog(state, GTK_MESSAGE_INFO, "Desktop shortcut created", dest_path);
    } else {
        show_feedback_dialog(state, GTK_MESSAGE_ERROR, "Desktop shortcut failed", error ? error->message : "Unknown error.");
    }

    if (error) g_error_free(error);
    if (src) g_object_unref(src);
    if (dst) g_object_unref(dst);
    g_free(fallback_dir);
    g_free(basename);
    g_free(dest_path);

    return ok;
}

static void launch_app(GtkWidget *widget, gpointer data)
{
    AppMenuState *state = g_object_get_data(G_OBJECT(widget), "app-menu-state");
    GAppInfo *app = G_APP_INFO(data);
    GError *error = NULL;

    if (!g_app_info_launch(app, NULL, NULL, &error)) {
        g_warning("[AppMenu] Failed to launch: %s", error->message);
        g_error_free(error);
        return;
    }

    refresh_all_instances();

    if (state && state->launcher_win) {
        gtk_widget_hide(state->launcher_win);
    }
}

static void launch_cmd(GtkWidget *widget, gpointer data)
{
    AppMenuState *state = g_object_get_data(G_OBJECT(widget), "app-menu-state");
    g_spawn_command_line_async((const char *)data, NULL);
    if (state && state->launcher_win) gtk_widget_hide(state->launcher_win);
}

/* ================================================================== */
/*  APP CONTEXT MENU                                                   */
/* ================================================================== */

static void on_menu_open(GtkMenuItem *item, gpointer user_data)
{
    GtkWidget *source = GTK_WIDGET(user_data);
    GAppInfo *app = G_APP_INFO(g_object_get_data(G_OBJECT(source), "app-info"));
    (void)item;
    if (app) launch_app(source, app);
}

static void on_menu_pin_toggle(GtkMenuItem *item, gpointer user_data)
{
    GtkWidget *source = GTK_WIDGET(user_data);
    GAppInfo *app = G_APP_INFO(g_object_get_data(G_OBJECT(source), "app-info"));
    const char *app_id;

    (void)item;
    if (!app) return;

    app_id = get_app_id(app);
    if (is_app_pinned(app_id)) unpin_app(app_id);
    else pin_app(app_id);

    refresh_all_instances();
}

static void on_menu_shortcut(GtkMenuItem *item, gpointer user_data)
{
    GtkWidget *source = GTK_WIDGET(user_data);
    AppMenuState *state = g_object_get_data(G_OBJECT(source), "app-menu-state");
    GAppInfo *app = G_APP_INFO(g_object_get_data(G_OBJECT(source), "app-info"));

    (void)item;
    if (state && app) create_desktop_shortcut(state, app);
}

static gboolean on_app_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    GAppInfo *app = G_APP_INFO(user_data);
    GtkWidget *menu;
    GtkWidget *open_item;
    GtkWidget *pin_item;
    GtkWidget *shortcut_item;

    if (event->type != GDK_BUTTON_PRESS || event->button != 3 || !app) return FALSE;

    menu = gtk_menu_new();
    open_item = gtk_menu_item_new_with_label("Open");
    pin_item = gtk_menu_item_new_with_label(is_app_pinned(get_app_id(app)) ? "Unpin from Start" : "Pin to Start");
    shortcut_item = gtk_menu_item_new_with_label("Create Desktop Shortcut");

    g_signal_connect(open_item, "activate", G_CALLBACK(on_menu_open), widget);
    g_signal_connect(pin_item, "activate", G_CALLBACK(on_menu_pin_toggle), widget);
    g_signal_connect(shortcut_item, "activate", G_CALLBACK(on_menu_shortcut), widget);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), pin_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), shortcut_item);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

/* ================================================================== */
/*  ROW BUILDERS                                                       */
/* ================================================================== */

static GtkWidget *create_app_image(GAppInfo *app, int size)
{
    GIcon *icon = g_app_info_get_icon(app);
    GtkWidget *image;

    if (icon) image = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
    else image = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_LARGE_TOOLBAR);

    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    gtk_widget_set_size_request(image, size, size);
    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(image, FALSE);
    gtk_widget_set_vexpand(image, FALSE);
    return image;
}

static GtkWidget *build_all_apps_tile(AppMenuState *state, GAppInfo *app)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *name = gtk_label_new(g_app_info_get_name(app));
    GtkWidget *category = gtk_label_new(get_app_category_name(app));

    add_css(button, "all-app-tile");
    add_css(name, "all-app-name");
    add_css(category, "all-app-category");

    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_label_set_justify(GTK_LABEL(name), GTK_JUSTIFY_CENTER);
    gtk_label_set_justify(GTK_LABEL(category), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(name), 10);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);

    box_append(GTK_BOX(box), create_app_image(app, 34));
    box_append(GTK_BOX(box), name);
    box_append(GTK_BOX(box), category);
    gtk_container_add(GTK_CONTAINER(button), box);

    g_object_set_data(G_OBJECT(button), "app-menu-state", state);
    g_object_set_data_full(G_OBJECT(button), "app-info", g_object_ref(app), g_object_unref);
    g_signal_connect(button, "clicked", G_CALLBACK(launch_app), app);
    g_signal_connect(button, "button-press-event", G_CALLBACK(on_app_button_press), app);

    gtk_widget_show_all(button);
    return button;
}

static GtkWidget *build_pinned_tile(AppMenuState *state, GAppInfo *app)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *label = gtk_label_new(g_app_info_get_name(app));

    add_css(button, "pinned-app");
    add_css(label, "pinned-name");
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 9);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);

    box_append(GTK_BOX(box), create_app_image(app, 36));
    box_append(GTK_BOX(box), label);
    gtk_container_add(GTK_CONTAINER(button), box);

    g_object_set_data(G_OBJECT(button), "app-menu-state", state);
    g_object_set_data_full(G_OBJECT(button), "app-info", g_object_ref(app), g_object_unref);
    g_signal_connect(button, "clicked", G_CALLBACK(launch_app), app);
    g_signal_connect(button, "button-press-event", G_CALLBACK(on_app_button_press), app);

    gtk_widget_show_all(button);
    return button;
}

static void clear_container(GtkWidget *container)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *l = children; l; l = l->next) {
        gtk_container_remove(GTK_CONTAINER(container), GTK_WIDGET(l->data));
    }
    g_list_free(children);
}

/* ================================================================== */
/*  VIEW POPULATION                                                    */
/* ================================================================== */

static void populate_pinned_grid(AppMenuState *state)
{
    int count = 0;

    clear_container(state->pinned_grid);
    ensure_pinned_loaded();

    for (GList *l = pinned_ids; l && count < MAX_PINNED_APPS; l = l->next) {
        GAppInfo *app = find_app_by_id((const char *)l->data);
        if (!app) continue;
        gtk_flow_box_insert(GTK_FLOW_BOX(state->pinned_grid), build_pinned_tile(state, app), -1);
        count++;
    }

    if (count == 0) {
        for (GList *l = full_apps; l && count < 6; l = l->next) {
            gtk_flow_box_insert(GTK_FLOW_BOX(state->pinned_grid),
                                build_pinned_tile(state, G_APP_INFO(l->data)),
                                -1);
            count++;
        }
    }

    gtk_widget_show_all(state->pinned_grid);
}

static void populate_all_apps_list(AppMenuState *state, const char *query)
{
    clear_container(state->all_apps_list);

    for (GList *l = full_apps; l; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (!app_matches_query(app, query)) continue;
        gtk_flow_box_insert(GTK_FLOW_BOX(state->all_apps_list),
                            build_all_apps_tile(state, app),
                            -1);
    }

    gtk_widget_show_all(state->all_apps_list);
}

static void refresh_app_menu_state(AppMenuState *state)
{
    const char *query;

    if (!state) return;

    populate_pinned_grid(state);

    query = state->search_entry ? gtk_entry_get_text(GTK_ENTRY(state->search_entry)) : "";
    populate_all_apps_list(state, query);
}

/* ================================================================== */
/*  PAGE SWITCHING                                                     */
/* ================================================================== */

static void update_page_buttons(AppMenuState *state)
{
    if (state->showing_all_apps) {
        gtk_widget_hide(state->all_apps_button);
        gtk_widget_show(state->back_button);
    } else {
        gtk_widget_show(state->all_apps_button);
        gtk_widget_hide(state->back_button);
    }
}

static void switch_to_home_page(AppMenuState *state)
{
    state->showing_all_apps = FALSE;
    gtk_stack_set_visible_child(GTK_STACK(state->stack), state->home_page);
    update_page_buttons(state);
}

static void switch_to_all_apps_page(AppMenuState *state)
{
    state->showing_all_apps = TRUE;
    gtk_stack_set_visible_child(GTK_STACK(state->stack), state->all_apps_page);
    update_page_buttons(state);
}

/* ================================================================== */
/*  SEARCH                                                             */
/* ================================================================== */

static void on_search_changed(GtkEditable *editable, gpointer user_data)
{
    AppMenuState *state = (AppMenuState *)user_data;
    const char *query = gtk_entry_get_text(GTK_ENTRY(editable));

    if (!query || query[0] == '\0') {
        refresh_app_menu_state(state);
        if (state->showing_all_apps) switch_to_all_apps_page(state);
        else switch_to_home_page(state);
        return;
    }

    populate_all_apps_list(state, query);
    switch_to_all_apps_page(state);
}

/* ================================================================== */
/*  BUTTON HANDLERS                                                    */
/* ================================================================== */

static void on_show_all_clicked(GtkButton *button, gpointer user_data)
{
    AppMenuState *state = (AppMenuState *)user_data;
    (void)button;
    populate_all_apps_list(state, gtk_entry_get_text(GTK_ENTRY(state->search_entry)));
    switch_to_all_apps_page(state);
}

static void on_back_clicked(GtkButton *button, gpointer user_data)
{
    AppMenuState *state = (AppMenuState *)user_data;
    (void)button;
    switch_to_home_page(state);
}

static gboolean on_launcher_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return FALSE;
}

static void enable_transparent_window(GtkWidget *window)
{
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
    }
}

static void on_launcher_hide(GtkWidget *widget, gpointer user_data)
{
    AppMenuState *state = (AppMenuState *)user_data;
    (void)widget;
    if (state && GTK_IS_TOGGLE_BUTTON(state->toggle_btn)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->toggle_btn), FALSE);
    }
}

static void position_launcher_window(AppMenuState *state)
{
    GtkWidget *button;
    GtkWidget *top;
    GdkDisplay *display;
    GdkMonitor *monitor;
    GdkRectangle workarea = {0};
    GtkAllocation alloc;

    if (!state || !GTK_IS_WINDOW(state->launcher_win)) return;

    button = state->anchor_button ? state->anchor_button : state->toggle_btn;
    if (!button) return;

    top = gtk_widget_get_toplevel(button);
    gtk_widget_get_allocation(button, &alloc);

    display = gdk_display_get_default();
    if (!display) return;

    monitor = NULL;
    GdkWindow *top_window = gtk_widget_get_window(top);
    if (top_window) {
        monitor = gdk_display_get_monitor_at_window(display, top_window);
    }
    if (!monitor) monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) return;

#if GTK_CHECK_VERSION(3, 22, 0)
    gdk_monitor_get_workarea(monitor, &workarea);
#else
    gdk_monitor_get_geometry(monitor, &workarea);
#endif

    int win_w = gtk_widget_get_allocated_width(state->launcher_win);
    int win_h = gtk_widget_get_allocated_height(state->launcher_win);
    if (win_w <= 1 || win_h <= 1) {
        gtk_window_get_size(GTK_WINDOW(state->launcher_win), &win_w, &win_h);
    }
    if (win_w <= 1) win_w = 560;
    if (win_h <= 1) win_h = 620;

    const int margin = 16;
    const int panel_gap = 2;
    const int panel_height = 25;
    int x = workarea.x + workarea.width - win_w - margin;
    int y = workarea.y + margin + panel_height + panel_gap;

    if (GTK_IS_WIDGET(top) && top_window) {
        int top_x = 0;
        int top_y = 0;
        int bx = alloc.x;
        int by = alloc.y;

        gdk_window_get_origin(top_window, &top_x, &top_y);
        gtk_widget_translate_coordinates(button, top, 0, 0, &bx, &by);
        x = top_x + bx + (alloc.width / 2) - (win_w / 2);
    }

    if (panel_geometry_get_config_edge() == PANEL_EDGE_BOTTOM) {
        y = workarea.y + workarea.height - win_h - margin - panel_height - panel_gap;
    }

    if (x < workarea.x + margin) x = workarea.x + margin;
    if (y < workarea.y + margin) y = workarea.y + margin;
    if (x + win_w > workarea.x + workarea.width - margin) x = workarea.x + workarea.width - win_w - margin;
    if (y + win_h > workarea.y + workarea.height - margin) y = workarea.y + workarea.height - win_h - margin;
    if (x < workarea.x) x = workarea.x;
    if (y < workarea.y) y = workarea.y;

    gtk_window_move(GTK_WINDOW(state->launcher_win), x, y);
}

static gboolean launcher_deferred_position(gpointer data)
{
    AppMenuState *state = data;
    position_launcher_window(state);
    return G_SOURCE_REMOVE;
}

static void on_launcher_show(GtkWidget *widget, gpointer user_data)
{
    AppMenuState *state = (AppMenuState *)user_data;
    (void)widget;
    g_idle_add(launcher_deferred_position, state);
}

static void show_launcher(GtkWidget *button)
{
    AppMenuState *state = g_object_get_data(G_OBJECT(button), "app-menu-state");

    if (!state) return;

    load_app_data();
    refresh_app_menu_state(state);
    gtk_entry_set_text(GTK_ENTRY(state->search_entry), "");
    switch_to_home_page(state);
    state->anchor_button = button;

    gtk_widget_show_all(state->launcher_win);
    gtk_window_present(GTK_WINDOW(state->launcher_win));
    gtk_widget_grab_focus(state->search_entry);
}

static void on_panel_btn_toggled(GtkToggleButton *button, gpointer user_data)
{
    AppMenuState *state = g_object_get_data(G_OBJECT(button), "app-menu-state");
    (void)user_data;

    if (gtk_toggle_button_get_active(button)) show_launcher(GTK_WIDGET(button));
    else if (state && state->launcher_win) gtk_widget_hide(state->launcher_win);
}

static void on_panel_btn_destroy(GtkWidget *widget, gpointer user_data)
{
    AppMenuState *state = (AppMenuState *)user_data;
    (void)widget;

    if (!state) return;

    app_menu_instances = g_list_remove(app_menu_instances, state);

    if (state->launcher_win) {
        gtk_widget_destroy(state->launcher_win);
        state->launcher_win = NULL;
    }

    g_free(state);
}

/* ================================================================== */
/*  UI BUILD                                                           */
/* ================================================================== */

static GtkWidget *create_header_actions(AppMenuState *state, const char *title, const char *button_label, GCallback callback)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new(title);
    GtkWidget *button = gtk_button_new_with_label(button_label);

    add_css(row, "section-head");
    add_css(label, "section-title");
    add_css(button, "section-link");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    box_append(GTK_BOX(row), label);
    box_append_expand(GTK_BOX(row), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    box_append(GTK_BOX(row), button);
    g_signal_connect(button, "clicked", callback, state);

    if (callback == G_CALLBACK(on_show_all_clicked)) state->all_apps_button = button;
    if (callback == G_CALLBACK(on_back_clicked)) state->back_button = button;

    return row;
}

static GtkWidget *build_home_page(AppMenuState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    box_append(GTK_BOX(page), create_header_actions(state, "Pinned", "All apps", G_CALLBACK(on_show_all_clicked)));

    state->pinned_grid = gtk_flow_box_new();
    add_css(state->pinned_grid, "pinned-grid");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(state->pinned_grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->pinned_grid), 6);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(state->pinned_grid), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(state->pinned_grid), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(state->pinned_grid), 8);
    box_append(GTK_BOX(page), state->pinned_grid);

    return page;
}

static GtkWidget *build_all_apps_page(AppMenuState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);

    box_append(GTK_BOX(page), create_header_actions(state, "All apps", "Back", G_CALLBACK(on_back_clicked)));

    add_css(wrap, "all-apps-wrap");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    gtk_widget_set_hexpand(wrap, TRUE);
    gtk_widget_set_vexpand(wrap, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    state->all_apps_list = gtk_flow_box_new();
    add_css(state->all_apps_list, "all-apps-list");
    add_css(state->all_apps_list, "all-apps-grid");
    gtk_widget_set_hexpand(state->all_apps_list, TRUE);
    gtk_widget_set_vexpand(state->all_apps_list, TRUE);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(state->all_apps_list), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->all_apps_list), 5);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(state->all_apps_list), 5);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(state->all_apps_list), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(state->all_apps_list), 8);
    gtk_container_add(GTK_CONTAINER(scroll), state->all_apps_list);
    gtk_container_add(GTK_CONTAINER(wrap), scroll);
    box_append_expand(GTK_BOX(page), wrap);

    return page;
}

static GtkWidget *build_footer(AppMenuState *state)
{
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *user_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *avatar;
    GtkWidget *username;
    GtkWidget *subtitle;
    char avatar_text[8] = {0};
    const char *user = g_get_user_name();
    static const struct { const char *label; const char *cmd; } actions[] = {
        {"⏾", "systemctl suspend"},
        {"↺", "systemctl reboot"},
        {"⏻", "systemctl poweroff"},
    };

    g_utf8_strncpy(avatar_text, user, 1);
    avatar = gtk_label_new(avatar_text);
    username = gtk_label_new(user);
    subtitle = gtk_label_new("Signed in");

    add_css(footer, "footer");
    add_css(avatar, "user-avatar");
    add_css(username, "username");
    add_css(subtitle, "footer-subtitle");

    gtk_widget_set_size_request(avatar, 32, 32);
    gtk_widget_set_valign(avatar, GTK_ALIGN_CENTER);
    gtk_label_set_xalign(GTK_LABEL(username), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);

    box_append(GTK_BOX(footer), avatar);
    box_append(GTK_BOX(footer), user_box);
    box_append(GTK_BOX(user_box), username);
    box_append(GTK_BOX(user_box), subtitle);
    box_append_expand(GTK_BOX(footer), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

    for (int i = 0; i < 3; i++) {
        GtkWidget *button = gtk_button_new_with_label(actions[i].label);
        add_css(button, "pwr-btn");
        gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
        g_object_set_data(G_OBJECT(button), "app-menu-state", state);
        g_signal_connect(button, "clicked", G_CALLBACK(launch_cmd), (gpointer)actions[i].cmd);
        box_append(GTK_BOX(footer), button);
    }

    return footer;
}

static GtkWidget *build_launcher(AppMenuState *state)
{
    GdkGeometry geometry = {0};
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *search_icon = gtk_image_new_from_icon_name("system-search-symbolic", GTK_ICON_SIZE_MENU);

    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 620);
    geometry.min_width  = 560;
    geometry.max_width  = 560;
    geometry.min_height = 620;
    geometry.max_height = 620;
    gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &geometry,
                                  GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
    enable_transparent_window(window);

    /* ── Wayland: layer-shell anchored to left edge, just below panel ──
     * On X11 the existing gtk_window_move() logic in position_launcher_window
     * handles placement; we skip layer-shell there entirely. */
    panel_window_backend_init_popup(GTK_WINDOW(window),
                                   "venom-app-menu",
                                   GDK_WINDOW_TYPE_HINT_NORMAL,
                                   GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    if (panel_window_backend_is_wayland()) {
        panel_window_backend_anchor_popup_to_panel(
            GTK_WINDOW(window),
            GTK_LAYER_SHELL_EDGE_LEFT, 8);
    }

    add_css(window, "launcher");
    add_css(root, "launcher-root");
    add_css(header, "launcher-header");
    add_css(search_row, "search-row");


    gtk_image_set_pixel_size(GTK_IMAGE(search_icon), 16);

    state->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->search_entry), "Search for apps, settings, and files");
    add_css(state->search_entry, "search-entry");


    box_append(GTK_BOX(root), header);

    box_append(GTK_BOX(search_row), search_icon);
    box_append_expand(GTK_BOX(search_row), state->search_entry);
    box_append(GTK_BOX(root), search_row);

    state->stack = gtk_stack_new();
    add_css(state->stack, "content-stack");
    gtk_widget_set_hexpand(state->stack, TRUE);
    gtk_widget_set_vexpand(state->stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(state->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(state->stack), 140);

    state->home_page = build_home_page(state);
    state->all_apps_page = build_all_apps_page(state);
    gtk_stack_add_named(GTK_STACK(state->stack), state->home_page, "home");
    gtk_stack_add_named(GTK_STACK(state->stack), state->all_apps_page, "all");
    box_append_expand(GTK_BOX(root), state->stack);
    box_append(GTK_BOX(root), build_footer(state));

    gtk_container_add(GTK_CONTAINER(window), root);
    gtk_widget_show_all(root);

    g_signal_connect(window, "hide", G_CALLBACK(on_launcher_hide), state);
    g_signal_connect(window, "show", G_CALLBACK(on_launcher_show), state);
    g_signal_connect(window, "draw", G_CALLBACK(on_launcher_draw), state);
    g_signal_connect(state->search_entry, "changed", G_CALLBACK(on_search_changed), state);

    state->launcher_win = window;
    return window;
}

/* ================================================================== */
/*  BUTTON ICON AND CSS                                                */
/* ================================================================== */

static GtkWidget *create_app_menu_icon(void)
{
    gchar *icon_path = resolve_asset_path("launchpad.svg");
    GdkPixbuf *pixbuf = NULL;
    GError *error = NULL;
    GtkWidget *image;

    if (icon_path) {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(icon_path, 20, 20, TRUE, &error);
    }

    if (pixbuf) {
        image = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);
    } else {
        if (error) {
            g_warning("[AppMenu] Failed to load %s: %s", icon_path, error->message);
            g_error_free(error);
        }
        image = gtk_image_new_from_icon_name("start-here-symbolic", GTK_ICON_SIZE_MENU);
    }

    g_free(icon_path);
    return image;
}

static void ensure_launcher_css(void)
{
    static gboolean css_loaded = FALSE;
    GtkCssProvider *provider;
    GError *error = NULL;

    if (css_loaded) return;
    css_loaded = TRUE;

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, LAUNCHER_CSS, -1, &error);
    if (error) {
        g_warning("[AppMenu] CSS error: %s", error->message);
        g_error_free(error);
    }
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* ================================================================== */
/*  PLUGIN ENTRY                                                       */
/* ================================================================== */

GtkWidget *panel_app_menu_button_new(void)
{
    AppMenuState *state = g_new0(AppMenuState, 1);

    ensure_launcher_css();
    load_app_data();

    state->toggle_btn = gtk_toggle_button_new();
    gtk_button_set_relief(GTK_BUTTON(state->toggle_btn), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(state->toggle_btn), create_app_menu_icon());
    g_object_set_data(G_OBJECT(state->toggle_btn), "app-menu-state", state);

    build_launcher(state);
    switch_to_home_page(state);
    refresh_app_menu_state(state);

    app_menu_instances = g_list_append(app_menu_instances, state);

    g_signal_connect(state->toggle_btn, "toggled", G_CALLBACK(on_panel_btn_toggled), NULL);
    g_signal_connect(state->toggle_btn, "destroy", G_CALLBACK(on_panel_btn_destroy), state);

    gtk_widget_show_all(state->toggle_btn);
    return state->toggle_btn;
}

void panel_app_menu_cleanup(void)
{
    GList *instances = app_menu_instances;

    while (instances) {
        AppMenuState *state = instances->data;
        instances = instances->next;

        if (state->launcher_win) {
            gtk_widget_destroy(state->launcher_win);
            state->launcher_win = NULL;
        }
    }
}
