#include "config.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void config_load(AuthConfig *config) {
    // Set default
    config->theme = THEME_POLKIT;

    const char *home = g_getenv("HOME");
    if (!home) {
        return;
    }

    gchar *config_path = g_build_filename(home, ".config", "vaxp", "auth", "auth.vaxp", NULL);

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;

    if (!g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        // Create directory if it doesn't exist
        gchar *config_dir = g_path_get_dirname(config_path);
        g_mkdir_with_parents(config_dir, 0755);
        g_free(config_dir);

        // Write default configuration
        FILE *f = fopen(config_path, "w");
        if (f) {
            fprintf(f, "[General]\n");
            fprintf(f, "# Available themes: polkit, minimal, terminal\n");
            fprintf(f, "Theme=polkit\n");
            fclose(f);
        }
    }

    if (g_key_file_load_from_file(key_file, config_path, G_KEY_FILE_NONE, &error)) {
        gchar *theme_str = g_key_file_get_string(key_file, "General", "Theme", &error);
        if (theme_str) {
            if (g_ascii_strcasecmp(theme_str, "minimal") == 0) {
                config->theme = THEME_MINIMAL;
            } else if (g_ascii_strcasecmp(theme_str, "terminal") == 0) {
                config->theme = THEME_TERMINAL;
            } else if (g_ascii_strcasecmp(theme_str, "polkit") == 0) {
                config->theme = THEME_POLKIT;
            } else {
                fprintf(stderr, "Unknown theme '%s', falling back to polkit\n", theme_str);
            }
            g_free(theme_str);
        } else if (error) {
            fprintf(stderr, "Warning: Failed to read Theme from [General] section: %s\n", error->message);
            g_clear_error(&error);
        }
    } else {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            fprintf(stderr, "Warning: Failed to load config file: %s\n", error->message);
        }
        g_error_free(error);
    }

    g_key_file_free(key_file);
    g_free(config_path);
}
