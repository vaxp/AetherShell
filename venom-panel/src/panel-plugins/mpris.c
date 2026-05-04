#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include "venom-panel-plugin-api.h"

typedef struct {
    GtkWidget *box;
    GtkWidget *btn_prev;
    GtkWidget *btn_play;
    GtkWidget *btn_next;
    GtkWidget *lbl_title;
    
    GDBusConnection *bus;
    guint name_owner_id;
    char *active_player;
    
    char *song_title;
    char *song_artist;
    gboolean is_playing;
    
    guint scroll_timer_id;
    int scroll_offset;
    char scroll_text[256];
} MprisData;

/* --- Helpers لاستخراج البيانات بأمان من DBus --- */

static gchar* extract_string(GVariant *dict, const char *key) {
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

static gchar* extract_first_array_string(GVariant *dict, const char *key) {
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
            gchar *dummy;
            while (g_variant_iter_next(&iter, "s", &dummy)) g_free(dummy);
        }
    }
    g_variant_unref(real_val);
    return result;
}

/* --- تحديث الواجهة --- */

static void update_ui(MprisData *data) {
    if (!data->active_player) {
        gtk_widget_hide(data->box);
        if (data->scroll_timer_id > 0) {
            g_source_remove(data->scroll_timer_id);
            data->scroll_timer_id = 0;
        }
        return;
    }
    
    gtk_widget_show_all(data->box);
    
    /* Update play/pause icon */
    GtkWidget *new_icon;
    if (data->is_playing) {
        new_icon = gtk_image_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_BUTTON);
    } else {
        new_icon = gtk_image_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON);
    }
    gtk_button_set_image(GTK_BUTTON(data->btn_play), new_icon);
    
    if (data->song_title && data->song_artist && strlen(data->song_artist) > 0) {
        snprintf(data->scroll_text, sizeof(data->scroll_text), "%s - %s    ", data->song_artist, data->song_title);
    } else if (data->song_title) {
        snprintf(data->scroll_text, sizeof(data->scroll_text), "%s    ", data->song_title);
    } else {
        snprintf(data->scroll_text, sizeof(data->scroll_text), "No Media    ");
    }
    
    data->scroll_offset = 0;
    
    /* Force immediate draw, or it may stay blank until tick */
    gtk_label_set_text(GTK_LABEL(data->lbl_title), data->scroll_text);
}

static gboolean on_scroll_tick(gpointer user_data) {
    MprisData *data = (MprisData*)user_data;
    if (!GTK_IS_WIDGET(data->lbl_title)) return G_SOURCE_REMOVE;
    if (!data->active_player) return G_SOURCE_CONTINUE;
    
    glong char_len = g_utf8_strlen(data->scroll_text, -1);
    if (char_len == 0) return G_SOURCE_CONTINUE;
    
    int display_len = 25; /* Fixed max chars to show in marquee */
    if (char_len <= display_len) {
        gtk_label_set_text(GTK_LABEL(data->lbl_title), data->scroll_text);
        return G_SOURCE_CONTINUE;
    }
    
    GString *display_str = g_string_new("");
    
    for (int i = 0; i < display_len; i++) {
        int idx = (data->scroll_offset + i) % char_len;
        gchar *ptr = g_utf8_offset_to_pointer(data->scroll_text, idx);
        gunichar c = g_utf8_get_char(ptr);
        g_string_append_unichar(display_str, c);
    }
    
    gtk_label_set_text(GTK_LABEL(data->lbl_title), display_str->str);
    g_string_free(display_str, TRUE);
    
    data->scroll_offset = (data->scroll_offset + 1) % char_len;
    return G_SOURCE_CONTINUE;
}

/* --- DBus Handlers --- */

static void get_mpris_properties(MprisData *data) {
    if (!data->active_player || !data->bus) return;
    
    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(data->bus,
                                                data->active_player,
                                                "/org/mpris/MediaPlayer2",
                                                "org.freedesktop.DBus.Properties",
                                                "GetAll",
                                                g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
                                                G_VARIANT_TYPE("(a{sv})"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1, NULL, &error);
    if (error) {
        g_error_free(error);
        return;
    }
    
    if (res) {
        /* استخراج القاموس الرئيسي */
        GVariant *dict = g_variant_get_child_value(res, 0);
        
        /* 1. جلب حالة التشغيل */
        gchar *status_str = extract_string(dict, "PlaybackStatus");
        if (status_str) {
            data->is_playing = (g_strcmp0(status_str, "Playing") == 0);
            g_free(status_str);
        }
        
        /* 2. جلب الميتاداتا */
        GVariant *meta_v = g_variant_lookup_value(dict, "Metadata", NULL);
        if (meta_v) {
            GVariant *meta_dict = meta_v;
            if (g_variant_is_of_type(meta_v, G_VARIANT_TYPE_VARIANT)) {
                meta_dict = g_variant_get_variant(meta_v);
                g_variant_unref(meta_v);
            }
            
            /* جلب العنوان */
            gchar *title = extract_string(meta_dict, "xesam:title");
            if (title) {
                g_free(data->song_title);
                data->song_title = title;
            }
            
            /* جلب الفنان (يأتي كمصفوفة) */
            gchar *artist = extract_first_array_string(meta_dict, "xesam:artist");
            if (artist) {
                g_free(data->song_artist);
                data->song_artist = artist;
            }
            
            g_variant_unref(meta_dict);
        }
        
        g_variant_unref(dict);
        g_variant_unref(res);
    }
    
    update_ui(data);
}

static void scan_players_and_update(MprisData *data) {
    if (!data->bus) return;
    
    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(data->bus,
                                                "org.freedesktop.DBus",
                                                "/org/freedesktop/DBus",
                                                "org.freedesktop.DBus",
                                                "ListNames",
                                                NULL,
                                                G_VARIANT_TYPE("(as)"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1, NULL, &error);
    if (error) { g_error_free(error); return; }
    
    GVariantIter *iter;
    g_variant_get(res, "(as)", &iter);
    char *name;
    
    char *best_player = NULL;
    char *fallback_player = NULL;
    
    while (g_variant_iter_next(iter, "s", &name)) {
        if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
            // Check if it's playing
            GVariant *prop_res = g_dbus_connection_call_sync(data->bus,
                                          name,
                                          "/org/mpris/MediaPlayer2",
                                          "org.freedesktop.DBus.Properties",
                                          "Get",
                                          g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"),
                                          G_VARIANT_TYPE("(v)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          500, NULL, NULL);
            if (prop_res) {
                GVariant *v_val;
                g_variant_get(prop_res, "(v)", &v_val);
                
                GVariant *unboxed = v_val;
                if (g_variant_is_of_type(v_val, G_VARIANT_TYPE_VARIANT)) {
                    unboxed = g_variant_get_variant(v_val);
                    g_variant_unref(v_val);
                }
                
                if (g_variant_is_of_type(unboxed, G_VARIANT_TYPE_STRING)) {
                    const gchar *status = g_variant_get_string(unboxed, NULL);
                    if (g_strcmp0(status, "Playing") == 0) {
                        best_player = g_strdup(name);
                        g_variant_unref(unboxed);
                        g_variant_unref(prop_res);
                        g_free(name);
                        break;
                    }
                }
                g_variant_unref(unboxed);
                g_variant_unref(prop_res);
            }
            if (!fallback_player) fallback_player = g_strdup(name);
        }
        g_free(name);
    }
    g_variant_iter_free(iter);
    g_variant_unref(res);
    
    char *selected_player = best_player ? best_player : fallback_player;
    
    if (g_strcmp0(data->active_player, selected_player) != 0) {
        g_free(data->active_player);
        data->active_player = g_strdup(selected_player);
    }
    
    if (best_player) g_free(best_player);
    if (fallback_player) g_free(fallback_player);
    
    if (!data->active_player) {
         g_free(data->song_title); data->song_title = NULL;
         g_free(data->song_artist); data->song_artist = NULL;
         data->is_playing = FALSE;
         update_ui(data);
    } else {
         get_mpris_properties(data);
    }
}

static void on_dbus_signal(GDBusConnection *connection,
                           const gchar *sender_name,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *signal_name,
                           GVariant *parameters,
                           gpointer user_data) {
    (void)connection; (void)sender_name; (void)object_path; (void)interface_name; (void)signal_name;
    MprisData *data = (MprisData*)user_data;
    
    if (g_strcmp0(signal_name, "PropertiesChanged") == 0) {
        if (data->active_player && g_strcmp0(sender_name, data->active_player) == 0) {
            /* We received an update for our currently active player */
            const gchar *iface;
            GVariant *changed_props;
            GVariant *invalidated;
            
            g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_props, &invalidated);
            if (g_strcmp0(iface, "org.mpris.MediaPlayer2.Player") == 0) {
                GVariantIter *iter;
                g_variant_get(changed_props, "a{sv}", &iter);
                const gchar *key;
                GVariant *value;
                gboolean metadata_changed = FALSE;
                
                while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
                    if (g_strcmp0(key, "PlaybackStatus") == 0) {
                        GVariant *unboxed = value;
                        if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
                            unboxed = g_variant_get_variant(value);
                        }
                        if (g_variant_is_of_type(unboxed, G_VARIANT_TYPE_STRING)) {
                            const gchar *status = g_variant_get_string(unboxed, NULL);
                            data->is_playing = (g_strcmp0(status, "Playing") == 0);
                        }
                        if (unboxed != value) g_variant_unref(unboxed);
                    }
                    else if (g_strcmp0(key, "Metadata") == 0) {
                        metadata_changed = TRUE;
                    }
                }
                g_variant_iter_free(iter);
                
                if (metadata_changed) {
                    /* If metadata changed, it's easier to just do a full GetAll to refresh title/artist */
                    get_mpris_properties(data);
                } else {
                    update_ui(data);
                }
            }
            g_variant_unref(changed_props);
            g_variant_unref(invalidated);
            return;
        }
    }
    
    /* Fallback: if we get a signal and it's not handled above, scan */
    scan_players_and_update(data);
}

static void on_name_owner_changed(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer user_data) {
    (void)connection; (void)sender_name; (void)object_path; (void)interface_name; (void)signal_name; (void)parameters;
    MprisData *data = (MprisData*)user_data;
    scan_players_and_update(data);
}

static void send_mpris_command(MprisData *data, const char *method) {
    if (!data->active_player || !data->bus) return;
    
    g_dbus_connection_call(data->bus,
                           data->active_player,
                           "/org/mpris/MediaPlayer2",
                           "org.mpris.MediaPlayer2.Player",
                           method,
                           NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
                           NULL, NULL, NULL);
}

static void on_prev_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; send_mpris_command((MprisData*)user_data, "Previous");
}
static void on_play_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; send_mpris_command((MprisData*)user_data, "PlayPause");
}
static void on_next_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; send_mpris_command((MprisData*)user_data, "Next");
}

static void find_initial_player(MprisData *data) {
    scan_players_and_update(data);
    if (data->active_player && data->scroll_timer_id == 0) {
        data->scroll_timer_id = g_timeout_add(250, on_scroll_tick, data);
    }
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    MprisData *data = (MprisData*)user_data;
    if (data->scroll_timer_id > 0) g_source_remove(data->scroll_timer_id);
    if (data->bus && data->name_owner_id > 0) g_dbus_connection_signal_unsubscribe(data->bus, data->name_owner_id);
    
    g_free(data->active_player);
    g_free(data->song_title);
    g_free(data->song_artist);
    
    if (data->bus) g_object_unref(data->bus);
    g_free(data);
}

static GtkWidget* create_mpris_widget(void) {
    MprisData *data = g_new0(MprisData, 1);
    
    data->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(data->box, 4);
    gtk_widget_set_margin_end(data->box, 4);
    
    data->btn_prev = gtk_button_new_from_icon_name("media-skip-backward-symbolic", GTK_ICON_SIZE_BUTTON);
    data->btn_play = gtk_button_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON);
    data->btn_next = gtk_button_new_from_icon_name("media-skip-forward-symbolic", GTK_ICON_SIZE_BUTTON);
    
    gtk_button_set_relief(GTK_BUTTON(data->btn_prev), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(data->btn_play), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(data->btn_next), GTK_RELIEF_NONE);
    
    g_signal_connect(data->btn_prev, "clicked", G_CALLBACK(on_prev_clicked), data);
    g_signal_connect(data->btn_play, "clicked", G_CALLBACK(on_play_clicked), data);
    g_signal_connect(data->btn_next, "clicked", G_CALLBACK(on_next_clicked), data);
    
    gtk_box_pack_start(GTK_BOX(data->box), data->btn_prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(data->box), data->btn_play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(data->box), data->btn_next, FALSE, FALSE, 0);
    
    data->lbl_title = gtk_label_new("");
    /* Force a strict fixed width so long titles don't push panel buttons */
    gtk_label_set_ellipsize(GTK_LABEL(data->lbl_title), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(data->lbl_title), 25);
    gtk_label_set_max_width_chars(GTK_LABEL(data->lbl_title), 25);
    gtk_label_set_xalign(GTK_LABEL(data->lbl_title), 0.0);
    gtk_box_pack_start(GTK_BOX(data->box), data->lbl_title, FALSE, FALSE, 4);
    
    GError *error = NULL;
    data->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!error && data->bus) {
        g_dbus_connection_signal_subscribe(data->bus,
                                           NULL,
                                           "org.freedesktop.DBus.Properties",
                                           "PropertiesChanged",
                                           NULL,
                                           "org.mpris.MediaPlayer2.Player",
                                           G_DBUS_SIGNAL_FLAGS_NONE,
                                           on_dbus_signal,
                                           data, NULL);
                                           
        data->name_owner_id = g_dbus_connection_signal_subscribe(data->bus,
                                           "org.freedesktop.DBus",
                                           "org.freedesktop.DBus",
                                           "NameOwnerChanged",
                                           "/org/freedesktop/DBus",
                                           NULL,
                                           G_DBUS_SIGNAL_FLAGS_NONE,
                                           on_name_owner_changed,
                                           data, NULL);
                                           
        find_initial_player(data);
    } else if (error) {
        g_error_free(error);
    }
    
    g_signal_connect(data->box, "destroy", G_CALLBACK(on_widget_destroy), data);
    
    if (!data->active_player) {
        gtk_widget_hide(data->box);
    }
    
    return data->box;
}

VenomPanelPluginAPI* venom_panel_plugin_init(void) {
    static VenomPanelPluginAPI api;
    api.name          = "Media Controls";
    api.description   = "Displays MPRIS media controls (Play/Pause/Skip) bulletproof for Chrome.";
    api.author        = "Venom";
    api.expand        = FALSE;
    api.padding       = 4;
    api.create_widget = create_mpris_widget;
    return &api;
}

VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api = {
        .api_version = VENOM_PANEL_PLUGIN_API_VERSION,
        .struct_size = sizeof(VenomPanelPluginAPIv2),
        .name = "Media Controls",
        .description = "Displays MPRIS media controls (Play/Pause/Skip) bulletproof for Chrome.",
        .author = "Venom",
        .zone = VENOM_PLUGIN_ZONE_RIGHT,
        .priority = 0,
        .expand = FALSE,
        .padding = 4,
        .create_widget = create_mpris_widget,
        .destroy_widget = NULL,
    };
    return &api;
}
