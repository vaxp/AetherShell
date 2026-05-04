/*
 * cc-mpris.c
 *
 * MPRIS logic for the Control Center media card.
 * UI is built elsewhere; this module wires signals, scans players, updates labels/art,
 * and sends playback commands.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "cc-mpris.h"

typedef struct {
    GDBusConnection *bus;
    guint sig_props_id;
    guint sig_owner_id;
    guint scan_timer_id;
    gchar *active_player;

    gchar *song_title;
    gchar *song_artist;
    gchar *art_url;
    gboolean is_playing;

    GtkWidget *title_label;
    GtkWidget *artist_label;
    GtkWidget *play_button;
    GtkWidget *art_image;
} CcMpris;

static gchar *cc_mpris_extract_string(GVariant *dict, const char *key)
{
    if (!dict) return NULL;
    GVariant *v = g_variant_lookup_value(dict, key, NULL);
    if (!v) return NULL;

    GVariant *real_val = v;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
        real_val = g_variant_get_variant(v);
        g_variant_unref(v);
    }

    gchar *result = NULL;
    if (g_variant_is_of_type(real_val, G_VARIANT_TYPE_STRING)) {
        result = g_strdup(g_variant_get_string(real_val, NULL));
    }
    g_variant_unref(real_val);
    return result;
}

static gchar *cc_mpris_extract_first_array_string(GVariant *dict, const char *key)
{
    if (!dict) return NULL;
    GVariant *v = g_variant_lookup_value(dict, key, NULL);
    if (!v) return NULL;

    GVariant *real_val = v;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
        real_val = g_variant_get_variant(v);
        g_variant_unref(v);
    }

    gchar *result = NULL;
    if (g_variant_is_of_type(real_val, G_VARIANT_TYPE_STRING_ARRAY)) {
        GVariantIter iter;
        g_variant_iter_init(&iter, real_val);
        gchar *str = NULL;
        if (g_variant_iter_next(&iter, "s", &str)) {
            result = str; /* takes ownership */
            gchar *dummy = NULL;
            while (g_variant_iter_next(&iter, "s", &dummy)) g_free(dummy);
        }
    }
    g_variant_unref(real_val);
    return result;
}

static void mpris_set_art_from_url(CcMpris *m, const gchar *url)
{
    if (!m || !m->art_image) return;
    if (!url || !url[0]) {
        gtk_image_set_from_icon_name(GTK_IMAGE(m->art_image), "multimedia-player-symbolic", GTK_ICON_SIZE_DIALOG);
        return;
    }

    if (g_str_has_prefix(url, "file://")) {
        GError *error = NULL;
        gchar *path = g_filename_from_uri(url, NULL, &error);
        if (!path) {
            g_clear_error(&error);
            gtk_image_set_from_icon_name(GTK_IMAGE(m->art_image), "multimedia-player-symbolic", GTK_ICON_SIZE_DIALOG);
            return;
        }

        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_size(path, 64, 64, &error);
        g_free(path);
        if (!pix) {
            g_clear_error(&error);
            gtk_image_set_from_icon_name(GTK_IMAGE(m->art_image), "multimedia-player-symbolic", GTK_ICON_SIZE_DIALOG);
            return;
        }

        gtk_image_set_from_pixbuf(GTK_IMAGE(m->art_image), pix);
        g_object_unref(pix);
        return;
    }

    gtk_image_set_from_icon_name(GTK_IMAGE(m->art_image), "multimedia-player-symbolic", GTK_ICON_SIZE_DIALOG);
}

static void cc_mpris_update_ui(CcMpris *m)
{
    if (!m) return;
    if (!m->title_label || !m->artist_label || !m->play_button || !m->art_image) return;

    gtk_label_set_text(GTK_LABEL(m->title_label),
                       (m->song_title && m->song_title[0]) ? m->song_title : "Nothing Playing");
    gtk_label_set_text(GTK_LABEL(m->artist_label),
                       (m->song_artist && m->song_artist[0]) ? m->song_artist : "—");

    gtk_button_set_image(GTK_BUTTON(m->play_button),
                         gtk_image_new_from_icon_name(
                             m->is_playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic",
                             GTK_ICON_SIZE_BUTTON));

    mpris_set_art_from_url(m, m->art_url);
}

static void cc_mpris_get_properties(CcMpris *m)
{
    if (!m || !m->active_player || !m->bus) return;

    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        m->bus,
        m->active_player,
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "GetAll",
        g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
        G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        &error);

    if (!res) {
        g_clear_error(&error);
        return;
    }

    GVariant *dict = g_variant_get_child_value(res, 0);

    gchar *status_str = cc_mpris_extract_string(dict, "PlaybackStatus");
    if (status_str) {
        m->is_playing = (g_strcmp0(status_str, "Playing") == 0);
        g_free(status_str);
    }

    GVariant *meta_v = g_variant_lookup_value(dict, "Metadata", NULL);
    if (meta_v) {
        GVariant *meta_dict = meta_v;
        if (g_variant_is_of_type(meta_v, G_VARIANT_TYPE_VARIANT)) {
            meta_dict = g_variant_get_variant(meta_v);
            g_variant_unref(meta_v);
        }

        gchar *title = cc_mpris_extract_string(meta_dict, "xesam:title");
        if (title) {
            g_free(m->song_title);
            m->song_title = title;
        }

        gchar *artist = cc_mpris_extract_first_array_string(meta_dict, "xesam:artist");
        if (artist) {
            g_free(m->song_artist);
            m->song_artist = artist;
        }

        gchar *art_url = cc_mpris_extract_string(meta_dict, "mpris:artUrl");
        if (art_url) {
            g_free(m->art_url);
            m->art_url = art_url;
        }

        g_variant_unref(meta_dict);
    }

    g_variant_unref(dict);
    g_variant_unref(res);

    cc_mpris_update_ui(m);
}

static gboolean cc_mpris_scan_tick(gpointer data);
static void cc_mpris_scan_players_and_update(CcMpris *m)
{
    if (!m || !m->bus) return;

    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        m->bus,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        NULL,
        G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        &error);
    if (!res) {
        g_clear_error(&error);
        return;
    }

    GVariantIter *iter = NULL;
    g_variant_get(res, "(as)", &iter);

    gchar *name = NULL;
    gchar *best_player = NULL;
    gchar *fallback_player = NULL;

    while (g_variant_iter_next(iter, "s", &name)) {
        if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.") && !g_str_has_suffix(name, ".playerctld")) {
            if (!fallback_player) fallback_player = g_strdup(name);

            /* Check PlaybackStatus */
            GVariant *prop_res = g_dbus_connection_call_sync(
                m->bus,
                name,
                "/org/mpris/MediaPlayer2",
                "org.freedesktop.DBus.Properties",
                "Get",
                g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"),
                G_VARIANT_TYPE("(v)"),
                G_DBUS_CALL_FLAGS_NONE,
                500,
                NULL,
                NULL);

            if (prop_res) {
                GVariant *child = g_variant_get_child_value(prop_res, 0); /* 'v' */
                GVariant *inner = g_variant_get_variant(child);           /* typically 's' */
                if (g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING)) {
                    const gchar *status = g_variant_get_string(inner, NULL);
                    if (g_strcmp0(status, "Playing") == 0) {
                        best_player = g_strdup(name);
                        g_variant_unref(inner);
                        g_variant_unref(child);
                        g_variant_unref(prop_res);
                        g_free(name);
                        break;
                    }
                }
                g_variant_unref(inner);
                g_variant_unref(child);
                g_variant_unref(prop_res);
            }
        }
        g_free(name);
    }

    g_variant_iter_free(iter);
    g_variant_unref(res);

    const gchar *selected = best_player ? best_player : fallback_player;

    if (g_strcmp0(m->active_player, selected) != 0) {
        g_free(m->active_player);
        m->active_player = selected ? g_strdup(selected) : NULL;

        g_free(m->song_title);
        m->song_title = NULL;
        g_free(m->song_artist);
        m->song_artist = NULL;
        g_free(m->art_url);
        m->art_url = NULL;
        m->is_playing = FALSE;
    }

    g_free(best_player);
    g_free(fallback_player);

    if (!m->active_player) {
        if (m->scan_timer_id == 0) {
            m->scan_timer_id = g_timeout_add_seconds(3, cc_mpris_scan_tick, m);
        }
        cc_mpris_update_ui(m);
        return;
    }

    if (m->scan_timer_id != 0) {
        g_source_remove(m->scan_timer_id);
        m->scan_timer_id = 0;
    }

    cc_mpris_get_properties(m);
}

static gboolean cc_mpris_scan_tick(gpointer data)
{
    cc_mpris_scan_players_and_update((CcMpris *)data);
    return G_SOURCE_CONTINUE;
}

static void cc_mpris_on_signal(GDBusConnection *connection,
                               const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name,
                               GVariant *parameters,
                               gpointer user_data)
{
    (void)connection;
    (void)object_path;
    (void)interface_name;
    CcMpris *m = (CcMpris *)user_data;
    if (!m) return;

    if (g_strcmp0(signal_name, "PropertiesChanged") == 0) {
        if (m->active_player && sender_name && g_strcmp0(sender_name, m->active_player) == 0) {
            const gchar *iface = NULL;
            GVariant *changed_props = NULL;
            GVariant *invalidated = NULL;

            g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_props, &invalidated);
            if (g_strcmp0(iface, "org.mpris.MediaPlayer2.Player") == 0) {
                GVariantIter iter;
                const gchar *key = NULL;
                GVariant *value = NULL;
                gboolean metadata_changed = FALSE;

                g_variant_iter_init(&iter, changed_props);
                while (g_variant_iter_loop(&iter, "{&sv}", &key, &value)) {
                    if (g_strcmp0(key, "PlaybackStatus") == 0) {
                        GVariant *unboxed = value;
                        if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
                            unboxed = g_variant_get_variant(value);
                        }
                        if (g_variant_is_of_type(unboxed, G_VARIANT_TYPE_STRING)) {
                            const gchar *status = g_variant_get_string(unboxed, NULL);
                            m->is_playing = (g_strcmp0(status, "Playing") == 0);
                        }
                        if (unboxed != value) g_variant_unref(unboxed);
                    } else if (g_strcmp0(key, "Metadata") == 0) {
                        metadata_changed = TRUE;
                    }
                }

                if (metadata_changed) cc_mpris_get_properties(m);
                else cc_mpris_update_ui(m);
            }

            g_variant_unref(changed_props);
            g_variant_unref(invalidated);
            return;
        }
    }

    /* Fallback: rescan when signals aren't actionable */
    cc_mpris_scan_players_and_update(m);
}

static void cc_mpris_on_name_owner_changed(GDBusConnection *connection,
                                          const gchar *sender_name,
                                          const gchar *object_path,
                                          const gchar *interface_name,
                                          const gchar *signal_name,
                                          GVariant *parameters,
                                          gpointer user_data)
{
    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    (void)parameters;
    cc_mpris_scan_players_and_update((CcMpris *)user_data);
}

static void mpris_send_method(GtkButton *button, gpointer user_data)
{
    (void)button;
    CcMpris *m = (CcMpris *)user_data;
    const char *method = g_object_get_data(G_OBJECT(button), "cc-mpris-method");
    if (!m || !m->bus || !method) return;
    if (!m->active_player) cc_mpris_scan_players_and_update(m);
    if (!m->active_player) return;

    g_dbus_connection_call(m->bus,
                           m->active_player,
                           "/org/mpris/MediaPlayer2",
                           "org.mpris.MediaPlayer2.Player",
                           method,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           NULL,
                           NULL);
}

static void mpris_free(CcMpris *m)
{
    if (!m) return;
    if (m->scan_timer_id) g_source_remove(m->scan_timer_id);
    if (m->bus && m->sig_props_id) g_dbus_connection_signal_unsubscribe(m->bus, m->sig_props_id);
    if (m->bus && m->sig_owner_id) g_dbus_connection_signal_unsubscribe(m->bus, m->sig_owner_id);
    g_clear_object(&m->bus);
    g_free(m->active_player);
    g_free(m->song_title);
    g_free(m->song_artist);
    g_free(m->art_url);
    g_free(m);
}

void cc_mpris_attach(GtkWidget *window_for_store,
                     GtkWidget *title_label,
                     GtkWidget *artist_label,
                     GtkWidget *art_image,
                     GtkWidget *btn_prev,
                     GtkWidget *btn_play,
                     GtkWidget *btn_next)
{
    if (!window_for_store) return;
    if (!title_label || !artist_label || !art_image || !btn_prev || !btn_play || !btn_next) return;

    CcMpris *m = g_new0(CcMpris, 1);
    m->title_label = title_label;
    m->artist_label = artist_label;
    m->play_button = btn_play;
    m->art_image = art_image;
    m->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

    g_object_set_data(G_OBJECT(btn_prev), "cc-mpris-method", (gpointer) "Previous");
    g_object_set_data(G_OBJECT(btn_play), "cc-mpris-method", (gpointer) "PlayPause");
    g_object_set_data(G_OBJECT(btn_next), "cc-mpris-method", (gpointer) "Next");
    g_signal_connect(btn_prev, "clicked", G_CALLBACK(mpris_send_method), m);
    g_signal_connect(btn_play, "clicked", G_CALLBACK(mpris_send_method), m);
    g_signal_connect(btn_next, "clicked", G_CALLBACK(mpris_send_method), m);

    /* Store lifetime on the parent window so everything cleans up with it. */
    g_object_set_data_full(G_OBJECT(window_for_store), "cc-mpris", m, (GDestroyNotify) mpris_free);

    if (m->bus) {
        m->sig_props_id = g_dbus_connection_signal_subscribe(m->bus,
                                                            NULL,
                                                            "org.freedesktop.DBus.Properties",
                                                            "PropertiesChanged",
                                                            NULL,
                                                            "org.mpris.MediaPlayer2.Player",
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            cc_mpris_on_signal,
                                                            m,
                                                            NULL);

        m->sig_owner_id = g_dbus_connection_signal_subscribe(m->bus,
                                                            "org.freedesktop.DBus",
                                                            "org.freedesktop.DBus",
                                                            "NameOwnerChanged",
                                                            "/org/freedesktop/DBus",
                                                            NULL,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            cc_mpris_on_name_owner_changed,
                                                            m,
                                                            NULL);

        cc_mpris_scan_players_and_update(m);
    }

    cc_mpris_update_ui(m);
}
