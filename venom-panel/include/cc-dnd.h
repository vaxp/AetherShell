/*
 * cc-dnd.h
 *
 * Do Not Disturb logic for Control Center.
 */

#ifndef CC_DND_H
#define CC_DND_H

#include <glib.h>

typedef struct _GtkWidget GtkWidget;

void cc_dnd_init(void);
void cc_dnd_cleanup(void);

gboolean cc_dnd_is_enabled(void);
const char *cc_dnd_subtitle_text(void);

void cc_dnd_attach(GtkWidget *dnd_button);

#endif /* CC_DND_H */

