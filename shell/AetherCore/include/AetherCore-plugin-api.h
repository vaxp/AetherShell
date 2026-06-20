#pragma once

#include <gtk/gtk.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* =========================================================================
 * Zone hint — where in the AetherCore bar the plugin prefers to be placed.
 * NOTE: actual placement is always determined by AetherCore.conf order/zone.
 * ========================================================================= */
typedef enum {
    AETHER_PLUGIN_ZONE_LEFT   = 0,
    AETHER_PLUGIN_ZONE_CENTER = 1,
    AETHER_PLUGIN_ZONE_RIGHT  = 2,
} AetherPluginZone;


/* =========================================================================
 * API v3  (current — adds visuals, per-plugin config, watchdog, isolation)
 * ========================================================================= */
#define AETHER_AetherCore_PLUGIN_API_VERSION  3u

/* ── Background style ───────────────────────────────────────────────────── */
typedef enum {
    AETHER_PLUGIN_BG_INHERIT      = 0, /* looks like the rest of the AetherCore    */
    AETHER_PLUGIN_BG_SOLID        = 1, /* solid RGBA colour                   */
    AETHER_PLUGIN_BG_TRANSPARENT  = 2, /* fully transparent                   */
    AETHER_PLUGIN_BG_GRADIENT     = 3, /* horizontal linear gradient          */
} AetherPluginBackground;

/* =========================================================================
 * AetherWindowLayout — layout hints for a plugin's popup/window.
 * ========================================================================= */
typedef struct {
    int padding;        /* outer padding in px          (default: 8)  */
    int inner_spacing;  /* gap between child elements   (default: 8)  */
    int corner_radius;  /* window corner radius in px   (default: 14) */
    int min_width;      /* minimum popup width in px    (0 = none)    */
    int min_height;     /* minimum popup height in px   (0 = none)    */
} AetherWindowLayout;

/* =========================================================================
 * AetherPluginTheme — per-plugin colour map + layout metadata.
 *
 * The AetherCore engine reads this struct and generates SCOPED CSS keyed on
 * window_css_id so that no two plugins' styles can ever interfere with
 * each other.
 *
 * All RGBA channels are in [0.0 – 1.0].  An all-zero colour (including
 * alpha == 0) is treated as "not set / skip this rule".
 * ========================================================================= */
typedef struct {
    /**
     * window_css_id:
     * The GTK widget name (set via gtk_widget_set_name) of the popup or
     * window root widget.  The engine scopes all generated CSS under this
     * id selector, e.g.  "#volume-mixer-window label { color: ... }".
     * If NULL the engine derives one automatically from the plugin_id.
     */
    const char *window_css_id;

    /**
     * outer_css_id:
     * The GTK widget name of the VISIBLE styled container inside the window
     * (e.g. "mixer-outer" inside "volume-mixer-window").  Background colour,
     * border, and corner-radius rules are scoped as:
     *   "#<window_css_id> #<outer_css_id> { background-color: ... }"
     * If NULL the engine applies background rules directly to window_css_id.
     */
    const char *outer_css_id;

    /* ── Root / accent colour ─────────────────────────────────────────── */
    /* Used for active states: slider fill, checked toggle, hover tint.  */
    double root_r, root_g, root_b, root_a;

    /* ── Popup window background ──────────────────────────────────────── */
    double bg_r, bg_g, bg_b, bg_a;

    /* ── Surface colour (cards, inner AetherCores) ─────────────────────────── */
    double surface_r, surface_g, surface_b, surface_a;

    /* ── Interactive element fill (slider trough, toggle background) ───── */
    double element_r, element_g, element_b, element_a;

    /* ── Text / label colour ──────────────────────────────────────────── */
    double text_r, text_g, text_b, text_a;

    /* ── Secondary text colour (subtitles, values, muted labels) ─────── */
    double text2_r, text2_g, text2_b, text2_a;

    /* ── Icon tint (applied to GtkImage / symbolic icons) ────────────── */
    double icon_r, icon_g, icon_b, icon_a;

    /* ── Border ──────────────────────────────────────────────────────── */
    double border_r, border_g, border_b, border_a;
    int    border_width;   /* px; 0 = no border             */
    int    corner_radius;  /* window corner radius in px    */

    /* ── Drop shadow ─────────────────────────────────────────────────── */
    gboolean shadow_enabled;
    double   shadow_r, shadow_g, shadow_b, shadow_a;
    double   shadow_blur;
    double   shadow_offset_x, shadow_offset_y;

    /* ── Layout ──────────────────────────────────────────────────────── */
    AetherWindowLayout layout;

} AetherPluginTheme;

/**
 * AetherPluginVisuals — per-plugin appearance overrides.
 *
 * Zero-initialise and set only the fields you need.
 * bg_type = 0 (INHERIT) is the default, meaning the slot looks identical
 * to the rest of the AetherCore bar.
 */
typedef struct {
    /* Background */
    AetherPluginBackground bg_type;
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
} AetherPluginVisuals;

/* ── AetherCore Context & Events ─────────────────────────────────────────────── */
typedef struct {
    int      monitor_index;          /* Index of the monitor showing this AetherCore */
    int      AetherCore_height;           /* Current height of the AetherCore in pixels   */
    gboolean is_wayland;             /* TRUE if running under Wayland           */

    /* Callbacks for the plugin to interact with the AetherCore */
    void (*request_AetherCore_resize)(int new_width);
    void (*show_notification)(const char *title, const char *body);
} AetherAetherCoreContext;

typedef enum {
    AETHER_EVENT_THEME_CHANGED,
    AETHER_EVENT_MONITOR_LAYOUT_CHANGED,
    AETHER_EVENT_POWER_SAVING_MODE_TOGGLED,
    AETHER_EVENT_ORIENTATION_CHANGED
} AetherSystemEvent;

/**
 * AetherAetherCorePluginAPIv3 — export `aether_AetherCore_plugin_init_v3` returning
 * a pointer to a statically-allocated instance of this struct.
 *
 * Fields left at zero/NULL are treated as "not set / use AetherCore default".
 */
typedef struct {
    uint32_t          api_version;   /* must == AETHER_AetherCore_PLUGIN_API_VERSION (3) */
    size_t            struct_size;   /* must >= sizeof(AetherAetherCorePluginAPIv3)      */

    /* ── Identity ────────────────────────────────────────────────────────── */
    const char       *name;
    const char       *description;
    const char       *author;
    const char       *version;       /* plugin's own semver, e.g. "1.0.0"         */
    const char       *icon_name;     /* icon-theme name shown in Settings UI       */

    /* ── Layout hints (AetherCore.conf always wins) ───────────────────────────── */
    AetherPluginZone   zone;
    int               priority;
    gboolean          expand;
    int               padding;
    int               min_width;     /* minimum slot width in px; 0 = unconstrained*/
    int               max_width;     /* maximum slot width in px; -1 = none        */

    /* ── Visual overrides ────────────────────────────────────────────────── */
    AetherPluginVisuals visuals;

    /* ── Behaviour ───────────────────────────────────────────────────────── */
    gboolean          singleton;     /* TRUE: AetherCore loads at most one instance     */
    guint             watchdog_ms;   /* create_widget() timeout (0 = disabled)     */

    /* ── Lifecycle ───────────────────────────────────────────────────────── */
    GtkWidget* (*create_widget)(AetherAetherCoreContext *ctx);
    void       (*destroy_widget)(GtkWidget *widget);  /* NULL = optional */

    /* ── Integration & Popovers ──────────────────────────────────────────── */
    GtkWidget* (*create_popover)(GtkWidget *plugin_widget); /* NULL = none */
    GtkWidget* (*get_context_menu)(void);                   /* NULL = none */

    /* ── System Events ───────────────────────────────────────────────────── */
    void       (*on_system_event)(GtkWidget *widget, AetherSystemEvent event, void *data);

    /* ── Per-plugin config channel ───────────────────────────────────────── */
    /**
     * on_config_changed — called by the AetherCore whenever a per-plugin key
     * found in AetherCore.conf changes (including at initial load).
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

    /* ── Per-plugin Theme Map ─────────────────────────────────────────── */
    /**
     * get_theme — returns the plugin's colour/layout map.
     *
     * Called once at load time.  If non-NULL the engine generates scoped
     * CSS using window_css_id as the root selector and applies it in its
     * own GtkCssProvider so this plugin's popup is completely independent
     * of every other plugin.
     *
     * Return NULL to inherit the AetherCore's global stylesheet unchanged.
     * The returned pointer must remain valid for the plugin's lifetime.
     */
    const AetherPluginTheme *(*get_theme)(void);

} AetherAetherCorePluginAPIv3;

typedef AetherAetherCorePluginAPIv3* (*AetherAetherCorePluginInitFnV3)(void);

/* =========================================================================
 * Convenience macros for plugin authors
 * ========================================================================= */

/** Solid background colour initialiser (RGBA 0.0-1.0). */
#define AETHER_VISUALS_SOLID(R,G,B,A) \
    { .bg_type = AETHER_PLUGIN_BG_SOLID, \
      .bg_r = (R), .bg_g = (G), .bg_b = (B), .bg_a = (A) }

/** Horizontal gradient background initialiser. */
#define AETHER_VISUALS_GRADIENT(R1,G1,B1,A1, R2,G2,B2,A2) \
    { .bg_type = AETHER_PLUGIN_BG_GRADIENT, \
      .bg_r=(R1),.bg_g=(G1),.bg_b=(B1),.bg_a=(A1), \
      .bg_r2=(R2),.bg_g2=(G2),.bg_b2=(B2),.bg_a2=(A2) }

/* =========================================================================
 * Convenience macros for AetherPluginTheme
 * ========================================================================= */

/**
 * AETHER_THEME_DARK(win_id, root_r,root_g,root_b, min_w)
 * Quick-initialiser for the common dark-glassmorphism popup theme.
 * Caller sets root colour (accent) and min_width; everything else is
 * derived from the standard AetherShell dark palette.
 */
#define AETHER_THEME_DARK(win_id, outer_id, Rr,Rg,Rb, min_w) \
    { .window_css_id  = (win_id), \
      .outer_css_id   = (outer_id), \
      .root_r=(Rr),   .root_g=(Rg),   .root_b=(Rb),   .root_a=1.0, \
      .bg_r=0.047,    .bg_g=0.047,    .bg_b=0.055,    .bg_a=0.92, \
      .surface_r=0.094,.surface_g=0.094,.surface_b=0.110,.surface_a=0.85, \
      .element_r=0.165,.element_g=0.165,.element_b=0.188,.element_a=1.0, \
      .text_r=0.8,    .text_g=0.8,    .text_b=0.8,    .text_a=1.0, \
      .text2_r=0.533, .text2_g=0.533, .text2_b=0.533, .text2_a=1.0, \
      .icon_r=0.533,  .icon_g=0.533,  .icon_b=0.533,  .icon_a=1.0, \
      .border_r=1.0,  .border_g=1.0,  .border_b=1.0,  .border_a=0.10, \
      .border_width=1, .corner_radius=14, \
      .layout={ .padding=0, .inner_spacing=4, \
                .corner_radius=14, .min_width=(min_w) } }
