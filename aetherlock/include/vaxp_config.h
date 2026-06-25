#ifndef VAXP_CONFIG_H
#define VAXP_CONFIG_H

#include <stdbool.h>

struct aetherlock_state;

struct color_rgba {
    double r;
    double g;
    double b;
    double a;
};

struct vaxp_colors {
    struct color_rgba panel_bg;
    struct color_rgba panel_border;
    struct color_rgba outer_border;
    double panel_border_width;
    double outer_border_width;
    struct color_rgba text_bright;
    struct color_rgba text_dim;
    struct color_rgba accent;
    struct color_rgba accent_dim;
    struct color_rgba background;
    bool hide_notification_content;
};

void config_load(struct aetherlock_state *state);

#endif // VAXP_CONFIG_H
