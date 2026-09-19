#include "vision_mode_shell.h"

static uint8_t g_shell_active;

void vision_mode_shell_set_active(uint8_t active)
{
    g_shell_active = active ? 1U : 0U;
}

uint8_t vision_mode_shell_active(void)
{
    return g_shell_active;
}
