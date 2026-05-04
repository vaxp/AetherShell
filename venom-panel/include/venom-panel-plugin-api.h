#pragma once

#include <gtk/gtk.h>
#include <stdint.h>
#include <stddef.h>

/* Where in the panel bar to mount the plugin widget.
 * NOTE: This is now a HINT only. The actual placement is determined by
 * the order and zone settings in ~/.config/venom/panel.conf. */
typedef enum {
    VENOM_PLUGIN_ZONE_LEFT   = 0,
    VENOM_PLUGIN_ZONE_CENTER = 1,
    VENOM_PLUGIN_ZONE_RIGHT  = 2,
} VenomPluginZone;

/*
 * Each `.so` plugin must export a symbol `venom_panel_plugin_init`
 * returning a pointer to a statically-allocated VenomPanelPluginAPI.
 *
 * Fields marked "layout hint" are defaults — they are overridden by
 * values in ~/.config/venom/panel.conf.
 */
typedef struct {
    const char       *name;        /* Display/log name                        */
    const char       *description; /* Short description                        */
    const char       *author;      /* Author name                              */
    VenomPluginZone   zone;        /* Layout hint: preferred zone              */
    int               priority;    /* Layout hint: pack order within zone      */
    gboolean          expand;      /* Layout hint: true = fill remaining space */
    int               padding;     /* Layout hint: px padding on each side     */

    /* Factory — return a valid GtkWidget* or NULL to skip. */
    GtkWidget* (*create_widget)(void);
} VenomPanelPluginAPI;

typedef VenomPanelPluginAPI* (*VenomPanelPluginInitFn)(void);

/* -----------------------
 * API v2 (recommended)
 * -----------------------
 *
 * Export a symbol `venom_panel_plugin_init_v2` returning a pointer to a
 * statically-allocated VenomPanelPluginAPIv2.
 *
 * v2 adds:
 * - versioning (`api_version`, `struct_size`) so the panel can validate ABI
 * - an optional `destroy_widget` hook to clean up timers/resources on reload
 */

#define VENOM_PANEL_PLUGIN_API_VERSION 2u

typedef struct {
    uint32_t          api_version;   /* must be VENOM_PANEL_PLUGIN_API_VERSION  */
    size_t            struct_size;   /* must be >= sizeof(VenomPanelPluginAPIv2) */

    const char       *name;          /* Display/log name                         */
    const char       *description;   /* Short description                        */
    const char       *author;        /* Author name                              */
    VenomPluginZone   zone;          /* Layout hint: preferred zone              */
    int               priority;      /* Layout hint: pack order within zone      */
    gboolean          expand;        /* Layout hint: true = fill remaining space */
    int               padding;       /* Layout hint: px padding on each side     */

    GtkWidget* (*create_widget)(void);
    void       (*destroy_widget)(GtkWidget *widget); /* optional */
} VenomPanelPluginAPIv2;

typedef VenomPanelPluginAPIv2* (*VenomPanelPluginInitFnV2)(void);
