#include "camera_debug.h"

#include <stdio.h>

#include "../core/hk_camera_sizes.h"

#include "camera_persist_settings.h"
#include "camera_status.h"

#include "../drivers/ov2640_sensor.h"
#include "hal_dvp.h"
#include "camera_fps.h"

void camera_debug_format_camera_info(char *line, size_t line_size, const char *screen)
{
    camera_service_status_t status;
    uint32_t fps_x10 = camera_fps_current_x10();
    uint32_t avg_x10 = camera_fps_average_x10();

    camera_service_status(&status);
    snprintf(line, line_size,
             "HKCAMINFO screen=%s init=%u ready=%u frame=%u size=%ux%u safe=%s fps_on=%u fps=%u.%u avg=%u.%u colorbar=%u mid=0x%04X pid=0x%04X sts=0x%08X state=%u timeout=%u fail=%s\r\n",
             screen,
             status.initialized,
             status.ready,
             status.have_frame,
             status.width,
             status.height,
             camera_size_label(camera_service_size()),
             camera_fps_enabled_state(),
             (unsigned)(fps_x10 / 10U),
             (unsigned)(fps_x10 % 10U),
             (unsigned)(avg_x10 / 10U),
             (unsigned)(avg_x10 % 10U),
             status.colorbar_enabled,
             status.mid,
             status.pid,
             (unsigned)hal_dvp_status(),
             status.capture_state,
             status.frame_timeout_shown,
             status.fail_reason ? status.fail_reason : "NONE");
}

void camera_debug_format_probe(char *line, size_t line_size, const char *screen)
{
    uint32_t actual_xclk;
    uint32_t actual_sccb;
    uint16_t mid;
    uint16_t pid;
    uint8_t ok;

    ov2640_init_bus(&actual_xclk, &actual_sccb);
    ov2640_probe_id(&mid, &pid);
    camera_service_note_probe_result(mid, pid);
    ok = (mid == 0x7FA2 && pid == 0x2642) ? 1 : 0;
    snprintf(line, line_size,
             "HKCAMPROBE screen=%s xclk=%u sccb=%u mid=0x%04X pid=0x%04X ok=%u\r\n",
             screen,
             actual_xclk,
             actual_sccb,
             mid,
             pid,
             ok);
}

void camera_debug_format_dvp(char *line, size_t line_size, const char *screen)
{
    hal_dvp_regs_t regs;
    camera_service_status_t status;

    hal_dvp_read_regs(&regs);
    camera_service_status(&status);
    snprintf(line, line_size,
             "HKCAMDVP screen=%s cfg=0x%08X cmos=0x%08X sccb=0x%08X axi=0x%08X sts=0x%08X last=0x%08X rgb=0x%08X irq_start=%u converts=%u irq_finish=%u captured=%u ready_drop=%u busy_drop=%u timeouts=%u state=%u wait_ms=%u frame=%u\r\n",
             screen,
             (unsigned)regs.cfg,
             (unsigned)regs.cmos_cfg,
             (unsigned)regs.sccb_cfg,
             (unsigned)regs.axi,
             (unsigned)regs.sts,
             (unsigned)status.last_sts,
             (unsigned)regs.rgb_addr,
             (unsigned)status.frame_start_count,
             (unsigned)status.convert_count,
             (unsigned)status.frame_finish_count,
             (unsigned)status.captured_count,
             (unsigned)status.ready_drop_count,
             (unsigned)status.busy_drop_count,
             (unsigned)status.timeout_count,
             status.capture_state,
             status.frame_wait_ms,
             status.have_frame);
}

void camera_debug_select_reg_bank(uint8_t bank)
{
    ov2640_write_reg(0xFF, bank);
}

uint8_t camera_debug_read_reg(uint8_t bank, uint8_t reg)
{
    camera_debug_select_reg_bank(bank);
    return ov2640_read_reg(reg);
}
