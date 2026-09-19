#include "hk_back_exit.h"

static uint8_t s_back_exit_armed;

uint8_t hk_back_exit_armed(void)
{
    return s_back_exit_armed;
}

void hk_back_exit_set_armed(uint8_t armed)
{
    s_back_exit_armed = armed ? 1 : 0;
}
