/*
 * cc-mpris.h
 *
 * MPRIS controller logic for Control Center media card.
 * Keeps D-Bus scanning/signals and playback commands out of control-center.c.
 */

#ifndef CC_MPRIS_H
#define CC_MPRIS_H

typedef struct _GtkWidget GtkWidget;

/* Attach MPRIS logic to an existing media card UI. */
void cc_mpris_attach(GtkWidget *window_for_store,
                     GtkWidget *title_label,
                     GtkWidget *artist_label,
                     GtkWidget *art_image,
                     GtkWidget *btn_prev,
                     GtkWidget *btn_play,
                     GtkWidget *btn_next);

#endif /* CC_MPRIS_H */

