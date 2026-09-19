#include "boot_controller.h"

#include <stdio.h>

#include "defaults.h"
#include "hk_config.h"
#include "pins.h"
#include "../config/settings_config.h"

#include "../core/hk_app_registry.h"
#include "../core/hk_dispatch.h"
#include "../core/hk_menu.h"
#include "../ui/boot_view.h"
#include "../ui/hk_ui.h"
#include "sd_event_controller.h"

static const hk_menu_view_t g_menu_view = {
    .draw_chrome = menu_draw_chrome,
    .draw_title = menu_draw_title,
    .draw_item_at = menu_draw_item_at,
};

static void boot_log_banner(void)
{
    printf("[BOOT] Dristy %s vision firmware\r\n", DRISTY_VERSION);
    printf("[BOOT] Comassless — Muhammad Fahim Faisal, Rakibul Islam\r\n");
    printf("[BOOT] vision co-processor firmware ready\r\n");
    printf("[LCD] " IO_LCD_DC_OR_AUX_LABEL
           "=DC via GPIOHS output bit" GPIOHS_LCD_DC_OR_AUX_LABEL ", "
           IO_LCD_RST_LABEL "=RST, " IO_LCD_CS_LABEL "=SPI"
           LCD_SPI_LABEL "_SS" LCD_CS_LABEL "\r\n");
    printf("[BTN] menu: L/R=prev/next OK=open BACK=page up OK+R=page dn\r\n");
    printf("[LED] illum candidate " IO_LED_ILLUM_LABEL " PWM"
           LED_PWM_DEVICE_LABEL "_CH" LED_PWM_CHANNEL_LABEL " default OFF\r\n");
    printf("[RGB] candidate " IO_RGB_PWM0_LABEL "/" IO_RGB_PWM1_LABEL "/"
           IO_RGB_PWM2_LABEL " PWM" RGB_PWM_DEVICE_LABEL "_CH"
           RGB_PWM_CHANNEL0_LABEL "/" RGB_PWM_CHANNEL1_LABEL "/"
           RGB_PWM_CHANNEL2_LABEL " default OFF\r\n");
    printf("[SD] candidate " IO_SD_SCLK_LABEL "=SCLK " IO_SD_D0_LABEL
           "=D0 " IO_SD_D1_LABEL "=D1 " IO_SD_CS_LABEL "=CS\r\n");
    printf("[SHOT] UART command HKSHOT streams BMP screenshot over USB serial\r\n");
    printf("[DEBUG] HKHELP lists commands available in this build\r\n");
    printf("[SETTINGS] flash slots 0x%06X/0x%06X\r\n", (unsigned)SETTINGS_FLASH_SLOT0, (unsigned)SETTINGS_FLASH_SLOT1);
    printf("[APP] count=%u\r\n", (unsigned)MENU_ITEM_COUNT);
    for(uint8_t i = 0; i < MENU_ITEM_COUNT; i++)
        printf("[APP] %u title=%s\r\n", (unsigned)i, g_menu_items[i].title);
}

void boot_controller_startup(void)
{
    menu_view_set(&g_menu_view);
    shell_set_sd_event_handler(sd_event_controller_handle);

    boot_log_banner();
}

void boot_controller_show_boot_screen(void)
{
    /* UART RX is already running. firmware_startup holds this frame and
     * ticks HKSHOT so the logo can be dumped before the menu replaces it. */
    printf("[LCD] draw static boot logo\r\n");
    boot_view_show_logo();
}
