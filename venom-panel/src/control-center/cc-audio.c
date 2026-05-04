/*
 * cc-audio.c — Audio section in the control-center.
 *
 * Now powered by pulse_volume.h (PulseAudio) instead of the old
 * audio-client D-Bus daemon.  The volume scale talks directly to
 * the default PulseAudio sink.
 */

#include <gtk/gtk.h>

#include "cc-audio.h"
#include "pulse_volume.h"

void cc_audio_init(void)
{
    /* pulse_volume is initialised by the volume-indicator widget which calls
     * pulse_volume_init().  If this section is loaded before the indicator,
     * the call below is a safe no-op because pulse_volume_init() guards
     * against double-initialisation. */
    pulse_volume_init(NULL, NULL);
}

void cc_audio_cleanup(void)
{
    /* PulseAudio context is shared; nothing to tear down here. */
}

static void cc_audio_on_volume_changed(GtkRange *range, gpointer data)
{
    (void)data;
    int percent = (int)gtk_range_get_value(range);
    pulse_volume_set(percent);
}

GtkWidget *cc_audio_create_volume_scale(void)
{
    int cur = pulse_volume_get_current();
    if (cur < 0) cur = 0;

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
    gtk_range_set_value(GTK_RANGE(scale), cur);

    /* Mark 100 % (= PA_VOLUME_NORM) as a reference point */
    gtk_scale_add_mark(GTK_SCALE(scale), 100, GTK_POS_BOTTOM, NULL);

    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);

    g_signal_connect(scale, "value-changed", G_CALLBACK(cc_audio_on_volume_changed), NULL);
    return scale;
}
