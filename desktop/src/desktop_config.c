/*
 * desktop_config.c
 * Shared desktop configuration and persisted state.
 */

#include "desktop_config.h"
#include <glib/gstdio.h>
#include <string.h>

char *get_vaxp_config_path(const char *filename) {
    return g_build_filename(g_get_user_config_dir(), "vaxp", filename, NULL);
}

char *get_vaxp_cache_path(const char *filename) {
    return g_build_filename(g_get_user_cache_dir(), "vaxp-thumbnails", filename, NULL);
}

void ensure_config_dir(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "vaxp", NULL);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
}

DesktopMode get_current_desktop_mode(void) {
    char *contents = NULL;
    gsize length = 0;
    char *mode_config = get_vaxp_config_path("desktop-mode");
    DesktopMode mode = MODE_NORMAL;

    if (g_file_get_contents(mode_config, &contents, &length, NULL)) {
        if (g_str_has_prefix(contents, "work")) {
            mode = MODE_WORK;
        } else if (g_str_has_prefix(contents, "widgets")) {
            mode = MODE_WIDGETS;
        }
        g_free(contents);
    }

    g_free(mode_config);
    return mode;
}

void set_current_desktop_mode(DesktopMode mode) {
    const char *str = "normal";

    ensure_config_dir();

    if (mode == MODE_WORK) str = "work";
    else if (mode == MODE_WIDGETS) str = "widgets";

    char *mode_config = get_vaxp_config_path("desktop-mode");
    g_file_set_contents(mode_config, str, -1, NULL);
    g_free(mode_config);
}

DesktopSortMode get_current_sort_mode(void) {
    char *contents = NULL;
    gsize length = 0;
    char *sort_config = get_vaxp_config_path("desktop-sort");
    DesktopSortMode mode = SORT_MANUAL;

    if (g_file_get_contents(sort_config, &contents, &length, NULL)) {
        g_strstrip(contents);
        if (g_strcmp0(contents, "name") == 0) {
            mode = SORT_NAME;
        } else if (g_strcmp0(contents, "type") == 0) {
            mode = SORT_TYPE;
        } else if (g_strcmp0(contents, "date-modified") == 0) {
            mode = SORT_DATE_MODIFIED;
        } else if (g_strcmp0(contents, "size") == 0) {
            mode = SORT_SIZE;
        }
        g_free(contents);
    }

    g_free(sort_config);
    return mode;
}

void set_current_sort_mode(DesktopSortMode mode) {
    const char *str = "manual";

    ensure_config_dir();

    if (mode == SORT_NAME) str = "name";
    else if (mode == SORT_TYPE) str = "type";
    else if (mode == SORT_DATE_MODIFIED) str = "date-modified";
    else if (mode == SORT_SIZE) str = "size";

    char *sort_config = get_vaxp_config_path("desktop-sort");
    g_file_set_contents(sort_config, str, -1, NULL);
    g_free(sort_config);
}

void sort_mode_to_markup(DesktopSortMode target_mode, GtkWidget *item) {
    if (get_current_sort_mode() != target_mode) return;

    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    if (GTK_IS_LABEL(child)) {
        const char *text = gtk_menu_item_get_label(GTK_MENU_ITEM(item));
        char *markup = g_markup_printf_escaped("<b>%s</b>", text ? text : "");
        gtk_label_set_markup(GTK_LABEL(child), markup);
        g_free(markup);
    }
}

char *get_current_desktop_path(void) {
    DesktopMode mode = get_current_desktop_mode();
    const char *home = g_get_home_dir();

    if (mode == MODE_WIDGETS) return NULL;

    if (mode == MODE_WORK) {
        char *work_path = g_strdup_printf("%s/Work", home);
        if (!g_file_test(work_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
            g_mkdir_with_parents(work_path, 0755);
        }
        return work_path;
    }

    return g_strdup_printf("%s/Desktop", home);
}

void save_item_position(const char *filename, int x, int y) {
    GKeyFile *key_file = g_key_file_new();
    char *y_key = g_strdup_printf("%s_y", filename);
    char *config_file = get_vaxp_config_path("desktop-items.ini");

    ensure_config_dir();
    g_key_file_load_from_file(key_file, config_file, G_KEY_FILE_NONE, NULL);
    g_key_file_set_integer(key_file, "Positions", filename, x);
    g_key_file_set_integer(key_file, "Positions", y_key, y);
    g_key_file_save_to_file(key_file, config_file, NULL);

    g_free(config_file);
    g_free(y_key);
    g_key_file_free(key_file);
}

gboolean get_item_position(const char *filename, int *x, int *y) {
    GKeyFile *key_file = g_key_file_new();
    GError *err = NULL;
    char *y_key = g_strdup_printf("%s_y", filename);
    char *config_file = get_vaxp_config_path("desktop-items.ini");
    int px;
    int py;

    if (!g_key_file_load_from_file(key_file, config_file, G_KEY_FILE_NONE, NULL)) {
        g_free(config_file);
        g_free(y_key);
        g_key_file_free(key_file);
        return FALSE;
    }

    px = g_key_file_get_integer(key_file, "Positions", filename, &err);
    if (err) {
        g_error_free(err);
        g_free(config_file);
        g_free(y_key);
        g_key_file_free(key_file);
        return FALSE;
    }

    err = NULL;
    py = g_key_file_get_integer(key_file, "Positions", y_key, &err);
    if (err) {
        g_error_free(err);
        g_free(config_file);
        g_free(y_key);
        g_key_file_free(key_file);
        return FALSE;
    }

    *x = px;
    *y = py;

    g_free(config_file);
    g_free(y_key);
    g_key_file_free(key_file);
    return TRUE;
}
