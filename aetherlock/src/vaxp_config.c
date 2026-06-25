#include "vaxp_config.h"
#include "aetherlock.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void parse_color(const char *hex, struct color_rgba *color, struct color_rgba fallback) {
    if (!hex || hex[0] != '#') {
        *color = fallback;
        return;
    }
    
    unsigned int r = 0, g = 0, b = 0, a = 255;
    int len = strlen(hex);
    
    if (len == 9) { // #RRGGBBAA
        sscanf(hex, "#%02x%02x%02x%02x", &r, &g, &b, &a);
    } else if (len == 7) { // #RRGGBB
        sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    } else {
        *color = fallback;
        return;
    }
    
    color->r = r / 255.0;
    color->g = g / 255.0;
    color->b = b / 255.0;
    color->a = a / 255.0;
}

void config_load(struct aetherlock_state *state) {
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "vaxp", "aetherlock", NULL);
    gchar *config_file = g_build_filename(config_dir, "aetherlock.vaxp", NULL);
    
    // Set absolute defaults
    struct color_rgba def_panel_bg = {20.0/255.0, 28.0/255.0, 30.0/255.0, 0.55};
    struct color_rgba def_panel_border = {1.0, 1.0, 1.0, 0.07};
    struct color_rgba def_outer_border = {1.0, 1.0, 1.0, 0.07};
    double def_panel_border_width = 1.0;
    double def_outer_border_width = 1.0;
    struct color_rgba def_text_bright = {230.0/255.0, 245.0/255.0, 240.0/255.0, 1.0};
    struct color_rgba def_text_dim = {159.0/255.0, 179.0/255.0, 176.0/255.0, 1.0};
    struct color_rgba def_accent = {126.0/255.0, 224.0/255.0, 201.0/255.0, 1.0};
    struct color_rgba def_accent_dim = {126.0/255.0, 224.0/255.0, 201.0/255.0, 0.15};
    struct color_rgba def_background = {17.0/255.0, 17.0/255.0, 17.0/255.0, 1.0};
    bool def_hide_notification_content = false;
    
    if (!g_file_test(config_file, G_FILE_TEST_EXISTS)) {
        g_mkdir_with_parents(config_dir, 0755);
        
        gchar *default_loc = NULL;
        char tz_buf[512] = {0};
        ssize_t len = readlink("/etc/localtime", tz_buf, sizeof(tz_buf)-1);
        if (len != -1) {
            char *city = strrchr(tz_buf, '/');
            if (city) {
                default_loc = g_strdup(city + 1);
                g_strdelimit(default_loc, "_", ' ');
            }
        }
        
        gchar *default_content = g_strdup_printf(
            "[Weather]\n"
            "# Enter your city here (e.g., Karbala). Leave empty for auto-detect based on IP.\n"
            "Location=%s\n\n"
            "[Colors]\n"
            "PanelBackground=#141c1e8c\n"
            "PanelBorder=#ffffff12\n"
            "PanelBorderWidth=1.0\n"
            "OuterBorder=#ffffff12\n"
            "OuterBorderWidth=1.0\n"
            "TextBright=#e6f5f0ff\n"
            "TextDim=#9fb3b0ff\n"
            "Accent=#7ee0c9ff\n"
            "AccentDim=#7ee0c926\n"
            "Background=#111111ff\n\n"
            "[Notifications]\n"
            "HideContent=false\n",
            default_loc ? default_loc : ""
        );
        
        g_file_set_contents(config_file, default_content, -1, NULL);
        g_free(default_content);
        g_free(default_loc);
    }
    
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_file, G_KEY_FILE_NONE, NULL)) {
        gchar *loc = g_key_file_get_string(kf, "Weather", "Location", NULL);
        if (loc) {
            state->weather.custom_location = g_strdup(loc);
            g_free(loc);
        } else {
            state->weather.custom_location = NULL;
        }
        
        gchar *c_panel_bg = g_key_file_get_string(kf, "Colors", "PanelBackground", NULL);
        gchar *c_panel_border = g_key_file_get_string(kf, "Colors", "PanelBorder", NULL);
        gchar *c_outer_border = g_key_file_get_string(kf, "Colors", "OuterBorder", NULL);
        gchar *c_text_bright = g_key_file_get_string(kf, "Colors", "TextBright", NULL);
        gchar *c_text_dim = g_key_file_get_string(kf, "Colors", "TextDim", NULL);
        gchar *c_accent = g_key_file_get_string(kf, "Colors", "Accent", NULL);
        gchar *c_accent_dim = g_key_file_get_string(kf, "Colors", "AccentDim", NULL);
        gchar *c_background = g_key_file_get_string(kf, "Colors", "Background", NULL);
        
        if (g_key_file_has_key(kf, "Colors", "PanelBorderWidth", NULL)) {
            state->vaxp_colors.panel_border_width = g_key_file_get_double(kf, "Colors", "PanelBorderWidth", NULL);
        } else {
            state->vaxp_colors.panel_border_width = def_panel_border_width;
        }
        
        if (g_key_file_has_key(kf, "Colors", "OuterBorderWidth", NULL)) {
            state->vaxp_colors.outer_border_width = g_key_file_get_double(kf, "Colors", "OuterBorderWidth", NULL);
        } else {
            state->vaxp_colors.outer_border_width = def_outer_border_width;
        }
        
        if (g_key_file_has_key(kf, "Notifications", "HideContent", NULL)) {
            state->vaxp_colors.hide_notification_content = g_key_file_get_boolean(kf, "Notifications", "HideContent", NULL);
        } else {
            state->vaxp_colors.hide_notification_content = def_hide_notification_content;
        }
        
        parse_color(c_panel_bg, &state->vaxp_colors.panel_bg, def_panel_bg);
        parse_color(c_panel_border, &state->vaxp_colors.panel_border, def_panel_border);
        parse_color(c_outer_border, &state->vaxp_colors.outer_border, def_outer_border);
        parse_color(c_text_bright, &state->vaxp_colors.text_bright, def_text_bright);
        parse_color(c_text_dim, &state->vaxp_colors.text_dim, def_text_dim);
        parse_color(c_accent, &state->vaxp_colors.accent, def_accent);
        parse_color(c_accent_dim, &state->vaxp_colors.accent_dim, def_accent_dim);
        parse_color(c_background, &state->vaxp_colors.background, def_background);
        
        g_free(c_panel_bg);
        g_free(c_panel_border);
        g_free(c_outer_border);
        g_free(c_text_bright);
        g_free(c_text_dim);
        g_free(c_accent);
        g_free(c_accent_dim);
        g_free(c_background);
    } else {
        // Fallback to defaults
        state->vaxp_colors.panel_bg = def_panel_bg;
        state->vaxp_colors.panel_border = def_panel_border;
        state->vaxp_colors.outer_border = def_outer_border;
        state->vaxp_colors.panel_border_width = def_panel_border_width;
        state->vaxp_colors.outer_border_width = def_outer_border_width;
        state->vaxp_colors.text_bright = def_text_bright;
        state->vaxp_colors.text_dim = def_text_dim;
        state->vaxp_colors.accent = def_accent;
        state->vaxp_colors.accent_dim = def_accent_dim;
        state->vaxp_colors.background = def_background;
        state->vaxp_colors.hide_notification_content = def_hide_notification_content;
    }
    
    g_key_file_free(kf);
    g_free(config_file);
    g_free(config_dir);
}
