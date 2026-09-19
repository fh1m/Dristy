#include "debug_camera_controller.h"

#include <stdio.h>

#include "../services/camera_frame.h"
#include "../services/camera_session.h"
#include "../services/camera_persist_settings.h"
#include "../services/camera_status.h"

#include "../core/hk_app.h"

#include "../core/hk_binary.h"
#include "../core/hk_screen.h"
#include "../core/hk_string.h"
#include "../services/camera_debug.h"
#include "../services/debug_console_service.h"

static void debug_uart_send_text(const char *text)
{
    debug_console_write_text(text);
}

static void debug_uart_send_bytes(const uint8_t *data, size_t len)
{
    debug_console_write(data, len);
}

static const char *debug_camera_screen_label(void)
{
    return screen_label(hk_screen_get());
}

static void debug_uart_send_camera_info(void)
{
    char line[512];

    camera_debug_format_camera_info(line, sizeof(line), debug_camera_screen_label());
    debug_uart_send_text(line);
}

static void debug_uart_send_camera_probe(void)
{
    char line[128];

    camera_debug_format_probe(line, sizeof(line), debug_camera_screen_label());
    debug_uart_send_text(line);
}

static void debug_uart_send_camera_dvp(void)
{
    char line[360];

    camera_debug_format_dvp(line, sizeof(line), debug_camera_screen_label());
    debug_uart_send_text(line);
}

static void debug_uart_send_camera_regs(void)
{
    static const uint8_t bank0_regs[] = {0x05, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x57, 0x5A, 0x5B, 0x5C, 0x86, 0x8C, 0xC0, 0xC1, 0xD3, 0xDA, 0xE0};
    static const uint8_t sens_regs[] = {0x03, 0x04, 0x11, 0x12, 0x13, 0x14, 0x17, 0x18, 0x19, 0x1A, 0x24, 0x25, 0x26, 0x32, 0x39};
    char line[40];

    debug_uart_send_text("HKCAMREGS BEGIN\r\n");
    for(size_t i = 0; i < sizeof(bank0_regs); i++)
    {
        snprintf(line, sizeof(line), "HKCAMREG BANK0 %02X %02X\r\n", bank0_regs[i], camera_debug_read_reg(0x00, bank0_regs[i]));
        debug_uart_send_text(line);
    }
    for(size_t i = 0; i < sizeof(sens_regs); i++)
    {
        snprintf(line, sizeof(line), "HKCAMREG SENS %02X %02X\r\n", sens_regs[i], camera_debug_read_reg(0x01, sens_regs[i]));
        debug_uart_send_text(line);
    }
    camera_debug_select_reg_bank(0x00);
    debug_uart_send_text("HKCAMREGS END\r\n");
}

static void debug_uart_send_camera_frame(void)
{
    const uint8_t *src;
    uint32_t bytes;
    uint32_t offset = 0;
    uint32_t crc = 0;
    uint16_t width;
    uint16_t height;
    char header[96];
    char footer[48];

    if(!camera_service_frame_snapshot(1000, &src, &bytes, &width, &height))
    {
        debug_uart_send_text("HKFRAME ERR NOFRAME\n");
        return;
    }

    snprintf(header, sizeof(header), "HKFRAME BEGIN RGB565 %u %u %u\n", width, height, (unsigned)bytes);
    debug_uart_send_text(header);

    while(offset < bytes)
    {
        uint16_t take = (uint16_t)((bytes - offset) > 1024U ? 1024U : (bytes - offset));
        crc = crc32_update(crc, src + offset, take);
        debug_uart_send_bytes(src + offset, take);
        offset += take;
    }
    camera_service_frame_snapshot_done(camera_service_capture_ready());

    snprintf(footer, sizeof(footer), "\nHKFRAME END %08X\n", (unsigned)crc);
    debug_uart_send_text(footer);
}

uint8_t debug_camera_controller_handle_command(const char *cmd)
{
    if(str_eq_ci(cmd, "HKCAMINFO"))
    {
        debug_uart_send_camera_info();
        return 1;
    }
    if(str_eq_ci(cmd, "HKFPS"))
    {
        char line[160];
        camera_service_format_fps(line, sizeof(line));
        debug_uart_send_text(line);
        return 1;
    }
    if(str_eq_ci(cmd, "HKFPSON") || str_eq_ci(cmd, "HKFPS1"))
    {
        camera_service_set_fps_enabled(1);
        debug_uart_send_text("HKFPS on=1\n");
        return 1;
    }
    if(str_eq_ci(cmd, "HKFPSOFF") || str_eq_ci(cmd, "HKFPS0"))
    {
        camera_service_set_fps_enabled(0);
        debug_uart_send_text("HKFPS on=0\n");
        return 1;
    }
    if(str_eq_ci(cmd, "HKFPSTOG"))
    {
        uint8_t enabled = camera_service_toggle_fps();
        printf("[CAM] fps_counter=%u\r\n", enabled);
        return 1;
    }
    if(str_eq_ci(cmd, "HKCAMPROBE"))
    {
        debug_uart_send_camera_probe();
        return 1;
    }
    if(str_eq_ci(cmd, "HKCAMREGS"))
    {
        debug_uart_send_camera_regs();
        return 1;
    }
    if(str_eq_ci(cmd, "HKCAMDVP"))
    {
        debug_uart_send_camera_dvp();
        return 1;
    }
    if(str_eq_ci(cmd, "HKCAMBAR") || str_eq_ci(cmd, "HKCOLORBAR"))
    {
        uint8_t enabled;

        activity_note();
        enabled = camera_service_toggle_colorbar();
        printf("[CAMDBG] colorbar=%u\r\n", enabled);
        return 1;
    }
    if(str_eq_ci(cmd, "HKFRAME"))
    {
        activity_note();
        debug_uart_send_camera_frame();
        return 1;
    }
    return 0;
}
