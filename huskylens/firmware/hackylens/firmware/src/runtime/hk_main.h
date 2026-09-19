#ifndef RUNTIME_HK_MAIN_H
#define RUNTIME_HK_MAIN_H

#include "../core/hk_app.h"

typedef struct
{
    void (*startup)(void);
    void (*debug_tick)(void);
    void (*system_tick)(const hk_input_snapshot_t *input);
} hk_main_hooks_t;

void hk_main_set_hooks(const hk_main_hooks_t *hooks);
int hk_main(void);

#endif
