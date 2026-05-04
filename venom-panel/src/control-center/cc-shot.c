/*
 * cc-shot.c
 */

#include "cc-shot.h"
#include "shot-client.h"

void cc_shot_init(void)
{
    shot_client_init();
}

void cc_shot_cleanup(void)
{
    shot_client_cleanup();
}
