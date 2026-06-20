#ifndef DESIGNER_CONFIG_IO_H
#define DESIGNER_CONFIG_IO_H

#include <glib.h>

char *config_io_read_layout(void);
char *config_io_read_designer_state(void);
void  config_io_write_layout(const char *json);
void  config_io_write_user_css(const char *css);
void  config_io_write_designer_state(const char *json);
void  config_io_restart_AetherCore(void);

#endif
