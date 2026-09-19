#include "hk_board_port.h"

static void cube_early_init(void)
{
    /* Conformance-only: no unqualified Cube electrical setup is performed. */
}

const hk_board_ops_t hk_board_ops = {
    .early_init = cube_early_init,
};
