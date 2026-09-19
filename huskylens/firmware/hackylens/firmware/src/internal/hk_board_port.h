#ifndef HK_BOARD_PORT_H
#define HK_BOARD_PORT_H

typedef void (*hk_board_prepare_fn)(void);

typedef struct
{
    hk_board_prepare_fn early_init;
    hk_board_prepare_fn display_prepare;
    hk_board_prepare_fn camera_prepare;
    hk_board_prepare_fn buttons_prepare;
    hk_board_prepare_fn lights_prepare;
    hk_board_prepare_fn internal_flash_prepare;
    hk_board_prepare_fn sd_prepare;
    hk_board_prepare_fn external_uart_prepare;
    hk_board_prepare_fn external_i2c_prepare;
} hk_board_ops_t;

/* Exactly one selected BSP defines this object in every firmware build. */
extern const hk_board_ops_t hk_board_ops;

#endif
