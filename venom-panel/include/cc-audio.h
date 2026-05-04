/*
 * cc-audio.h
 *
 * Audio logic for Control Center volume slider.
 */

#ifndef CC_AUDIO_H
#define CC_AUDIO_H

typedef struct _GtkWidget GtkWidget;

void cc_audio_init(void);
void cc_audio_cleanup(void);

GtkWidget *cc_audio_create_volume_scale(void);

#endif /* CC_AUDIO_H */

