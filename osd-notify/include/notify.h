#ifndef VAXP_GUI_NOTIFY_H
#define VAXP_GUI_NOTIFY_H

void notify_init(void);
void notify_reload_ui(void);


void notify_pause_timeout(guint32 id);
void notify_resume_timeout(guint32 id);
#endif
