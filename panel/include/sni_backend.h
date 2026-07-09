#ifndef AETHER_SNI_BACKEND_H
#define AETHER_SNI_BACKEND_H

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

/**
 * Opaque type representing a single SNI item managed by the backend.
 */
typedef struct _SniItem SniItem;

/**
 * Callbacks that the UI provides to the backend to receive updates.
 */
typedef struct {
    void (*on_item_added)(SniItem *item, gpointer user_data);
    void (*on_item_removed)(const char *service, gpointer user_data);
    void (*on_item_updated)(SniItem *item, gpointer user_data);
} SniBackendCallbacks;

/**
 * Initialize the SNI backend, register the watcher, and start receiving items.
 */
void sni_backend_init(const SniBackendCallbacks *callbacks, gpointer user_data);

/**
 * Actions that the UI can request on a specific item.
 */
void sni_item_activate(SniItem *item, int x, int y);
void sni_item_secondary_activate(SniItem *item, int x, int y);
void sni_item_context_menu(SniItem *item, int x, int y);
void sni_item_scroll(SniItem *item, int delta, const char *orientation);

/**
 * Property getters for the UI to read item data.
 */
const char* sni_item_get_service(SniItem *item);
const char* sni_item_get_title(SniItem *item);
const char* sni_item_get_icon_name(SniItem *item);
GdkPixbuf*  sni_item_get_icon_pixbuf(SniItem *item);
gboolean    sni_item_is_passive(SniItem *item);
gboolean    sni_item_is_menu(SniItem *item);
gpointer    sni_item_get_dbusmenu_root(SniItem *item); /* returns DbusmenuMenuitem* */

G_END_DECLS

#endif /* AETHER_SNI_BACKEND_H */
