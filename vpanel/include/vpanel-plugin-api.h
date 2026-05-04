#pragma once

#include <gtk/gtk.h>
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Zone hint — where in the panel bar the plugin prefers to be placed.
 * NOTE: actual placement is always determined by panel.conf order/zone.
 * ========================================================================= */
typedef enum {
    VENOM_PLUGIN_ZONE_LEFT   = 0,
    VENOM_PLUGIN_ZONE_CENTER = 1,
    VENOM_PLUGIN_ZONE_RIGHT  = 2,
} VenomPluginZone;

/* =========================================================================
 * API v1  (legacy — still loaded for backward compatibility)
 * ========================================================================= */
typedef struct {
    const char       *name;
    const char       *description;
    const char       *author;
    VenomPluginZone   zone;
    int               priority;
    gboolean          expand;
    int               padding;
    GtkWidget* (*create_widget)(void);
} VenomPanelPluginAPI;

typedef VenomPanelPluginAPI* (*VenomPanelPluginInitFn)(void);

/* =========================================================================
 * API v2  (adds ABI version check + destroy hook)
 * ========================================================================= */
#define VENOM_PANEL_PLUGIN_API_VERSION_V2  2u

typedef struct {
    uint32_t          api_version;   /* must == 2                            */
    size_t            struct_size;   /* must >= sizeof(VenomPanelPluginAPIv2)*/

    const char       *name;
    const char       *description;
    const char       *author;
    VenomPluginZone   zone;
    int               priority;
    gboolean          expand;
    int               padding;

    GtkWidget* (*create_widget)(void);
    void       (*destroy_widget)(GtkWidget *widget);  /* NULL = optional */
} VenomPanelPluginAPIv2;

typedef VenomPanelPluginAPIv2* (*VenomPanelPluginInitFnV2)(void);

/* =========================================================================
 * API v3  (current — adds visuals, per-plugin config, watchdog, isolation)
 * ========================================================================= */
#define VENOM_PANEL_PLUGIN_API_VERSION  3u

/* ── Background style ───────────────────────────────────────────────────── */
typedef enum {
    VENOM_PLUGIN_BG_INHERIT      = 0, /* looks like the rest of the panel    */
    VENOM_PLUGIN_BG_SOLID        = 1, /* solid RGBA colour                   */
    VENOM_PLUGIN_BG_TRANSPARENT  = 2, /* fully transparent                   */
    VENOM_PLUGIN_BG_GRADIENT     = 3, /* horizontal linear gradient          */
} VenomPluginBackground;

/**
 * VenomPluginVisuals — per-plugin appearance overrides.
 *
 * Zero-initialise and set only the fields you need.
 * bg_type = 0 (INHERIT) is the default, meaning the slot looks identical
 * to the rest of the panel bar.
 */
typedef struct {
    /* Background */
    VenomPluginBackground bg_type;
    double bg_r,  bg_g,  bg_b,  bg_a;   /* primary colour   [0.0-1.0] */
    double bg_r2, bg_g2, bg_b2, bg_a2;  /* gradient end colour        */

    /* Border */
    gboolean border_enabled;
    double   border_r, border_g, border_b, border_a;
    int      border_width;               /* pixels (0 = none)          */
    int      border_radius;              /* corner radius in pixels     */

    /* Drop shadow (compositing environments only) */
    gboolean shadow_enabled;
    double   shadow_r, shadow_g, shadow_b, shadow_a;
    double   shadow_blur;
    double   shadow_offset_x, shadow_offset_y;
} VenomPluginVisuals;

/**
 * VenomPanelPluginAPIv3 — export `venom_panel_plugin_init_v3` returning
 * a pointer to a statically-allocated instance of this struct.
 *
 * Fields left at zero/NULL are treated as "not set / use panel default".
 */
typedef struct {
    uint32_t          api_version;   /* must == VENOM_PANEL_PLUGIN_API_VERSION (3) */
    size_t            struct_size;   /* must >= sizeof(VenomPanelPluginAPIv3)      */

    /* ── Identity ────────────────────────────────────────────────────────── */
    const char       *name;
    const char       *description;
    const char       *author;
    const char       *version;       /* plugin's own semver, e.g. "1.0.0"         */
    const char       *icon_name;     /* icon-theme name shown in Settings UI       */

    /* ── Layout hints (panel.conf always wins) ───────────────────────────── */
    VenomPluginZone   zone;
    int               priority;
    gboolean          expand;
    int               padding;
    int               min_width;     /* minimum slot width in px; 0 = unconstrained*/
    int               max_width;     /* maximum slot width in px; -1 = none        */

    /* ── Visual overrides ────────────────────────────────────────────────── */
    VenomPluginVisuals visuals;

    /* ── Behaviour ───────────────────────────────────────────────────────── */
    gboolean          singleton;     /* TRUE: panel loads at most one instance     */
    guint             watchdog_ms;   /* create_widget() timeout (0 = disabled)     */

    /* ── Lifecycle ───────────────────────────────────────────────────────── */
    GtkWidget* (*create_widget)(void);
    void       (*destroy_widget)(GtkWidget *widget);  /* NULL = optional */

    /* ── Per-plugin config channel ───────────────────────────────────────── */
    /**
     * on_config_changed — called by the panel whenever a per-plugin key
     * found in panel.conf changes (including at initial load).
     * @widget : the widget returned by create_widget()
     * @key    : config key name (e.g. "refresh_ms")
     * @value  : new value as string  (e.g. "500")
     */
    void (*on_config_changed)(GtkWidget *widget,
                              const char *key,
                              const char *value);

    /**
     * get_config_schema — called once at load time so the Settings UI can
     * enumerate this plugin's options.
     * Fill *keys with a NULL-terminated array of key names and *defaults
     * with the matching default value strings.
     * Both arrays must remain valid for the lifetime of the plugin.
     */
    void (*get_config_schema)(const char ***keys, const char ***defaults);

} VenomPanelPluginAPIv3;

typedef VenomPanelPluginAPIv3* (*VenomPanelPluginInitFnV3)(void);

/* =========================================================================
 * Convenience macros for plugin authors
 * ========================================================================= */

/** Solid background colour initialiser (RGBA 0.0-1.0). */
#define VENOM_VISUALS_SOLID(R,G,B,A) \
    { .bg_type = VENOM_PLUGIN_BG_SOLID, \
      .bg_r = (R), .bg_g = (G), .bg_b = (B), .bg_a = (A) }

/** Horizontal gradient background initialiser. */
#define VENOM_VISUALS_GRADIENT(R1,G1,B1,A1, R2,G2,B2,A2) \
    { .bg_type = VENOM_PLUGIN_BG_GRADIENT, \
      .bg_r=(R1),.bg_g=(G1),.bg_b=(B1),.bg_a=(A1), \
      .bg_r2=(R2),.bg_g2=(G2),.bg_b2=(B2),.bg_a2=(A2) }
