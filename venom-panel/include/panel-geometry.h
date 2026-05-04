#ifndef PANEL_GEOMETRY_H
#define PANEL_GEOMETRY_H

#include <gtk/gtk.h>

typedef enum {
    PANEL_EDGE_TOP,
    PANEL_EDGE_BOTTOM,
    PANEL_EDGE_LEFT,
    PANEL_EDGE_RIGHT,
} PanelEdge;

PanelEdge panel_geometry_get_config_edge(void);
GtkOrientation panel_geometry_edge_orientation(PanelEdge edge);
void panel_geometry_apply(GtkWidget *panel_window);
void panel_geometry_attach(GtkWidget *panel_window);

#endif /* PANEL_GEOMETRY_H */
