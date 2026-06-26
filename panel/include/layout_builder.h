#pragma once

#include <gtk/gtk.h>

/* =========================================================================
 * AetherShell Panel — Layout Builder
 *
 * Reads a panel.json configuration file and constructs the full GTK widget
 * tree for the panel bar.  Each "pill" in the JSON becomes a styled GtkBox;
 * each plugin ID inside a pill is resolved via plugin_engine_get_widget().
 *
 * JSON schema (simplified):
 *
 *   {
 *     "panel": {
 *       "height"  : 36,
 *       "margin"  : { "top": 4, "left": 8, "right": 8 }
 *     },
 *     "layout": {
 *       "left"   : [ <pill>, … ],
 *       "center" : [ <pill>, … ],
 *       "right"  : [ <pill>, … ]
 *     }
 *   }
 *
 *   pill = {
 *     "type"    : "pill",          // "pill" is the only type for now
 *     "id"      : "my-pill",       // CSS class applied to the GtkBox
 *     "spacing" : 4,               // px between plugin widgets (optional)
 *     "plugins" : [ "aether-wifi", "aether-volume", … ]
 *   }
 * ========================================================================= */

typedef enum {
    PANEL_POSITION_TOP,
    PANEL_POSITION_BOTTOM,
    PANEL_POSITION_LEFT,
    PANEL_POSITION_RIGHT
} PanelPosition;

/**
 * PanelLayoutConfig — settings parsed from the "panel" object.
 */
typedef struct {
    int height;        /* panel bar height in pixels  (default 36)          */
    int margin_top;    /* Wayland exclusive-zone top margin  (default 4)    */
    int margin_bottom;
    int margin_left;   /* panel content left margin          (default 8)    */
    int margin_right;  /* panel content right margin         (default 8)    */
    int spacing;       /* default spacing between pills      (default 8)    */
    PanelPosition position;
} PanelLayoutConfig;

/**
 * layout_builder_parse_config:
 * Parse only the "panel" section of @json_path and fill @out_config.
 * Falls back to defaults for any missing key.
 *
 * Returns TRUE on success, FALSE if the file could not be opened/parsed.
 */
gboolean layout_builder_parse_config(const char       *json_path,
                                     PanelLayoutConfig *out_config);

/**
 * layout_builder_build:
 * Read @json_path and construct the full panel content widget.
 *
 * The returned widget is a GtkBox (id "panel-content") ready to be packed
 * into the panel window.  Each pill is a GtkBox with:
 *   - CSS class "aether-pill"
 *   - CSS class equal to the pill's "id" field
 *   - Plugin widgets obtained from plugin_engine_get_widget()
 *
 * Plugins referenced in the JSON but not found in the engine are silently
 * skipped with a g_warning().
 *
 * Returns a new GtkWidget* on success, NULL on failure.
 */
GtkWidget *layout_builder_build(const char *json_path);
