#ifndef VAXP_AUTH_UI_H
#define VAXP_AUTH_UI_H

#include "config.h"

#define UI_RESULT_SUCCESS  1
#define UI_RESULT_CANCEL   0
#define UI_RESULT_ERROR   -1

int auth_ui_init(AuthConfig *config);
void auth_ui_cleanup(void);

void auth_ui_set_theme(AuthConfig *config);

int auth_ui_get_password(const char *message,
                         const char *username,
                         char *password_out,
                         int max_length);

#endif
