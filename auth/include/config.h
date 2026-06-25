#ifndef VAXP_AUTH_CONFIG_H
#define VAXP_AUTH_CONFIG_H

typedef enum {
    THEME_MINIMAL,
    THEME_POLKIT,
    THEME_TERMINAL
} AuthTheme;

typedef struct {
    AuthTheme theme;
} AuthConfig;

// Loads the configuration from ~/.config/vaxp/auth/auth.vaxp
// Falls back to THEME_POLKIT if not found or invalid
void config_load(AuthConfig *config);

#endif // VAXP_AUTH_CONFIG_H
