#include "files_app.h"

#include "../../core/hk_screen.h"
#include "../../storage/fat32_volume.h"
#include "files_actions.h"
#include "files_controller.h"
#include "files_presenter.h"
#include "files_view.h"

void files_enter(const hk_input_snapshot_t *input)
{
    files_view_init();
    files_controller_enter(input);
}

void files_exit(void) { files_controller_exit(); }
void files_tick(const hk_input_snapshot_t *input) { files_controller_tick(input); }
void files_handle_buttons(const hk_input_snapshot_t *input) { files_controller_handle_buttons(input); }

void files_handle_sd_event(hk_sd_event_t event)
{
    uint8_t ready;

    files_controller_reset_input();
    ready = files_on_sd_event(event);
    if(hk_screen_get() != SCREEN_FILES)
        return;
    if(!hk_sd_present() || !hk_fat_mounted())
        files_presenter_show_status("NO SD");
    else if(!ready)
        files_presenter_show_result(FILE_RESULT_READ_ERROR);
    else
        files_presenter_render_list();
}

void files_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    files_view_draw_icon(x, y, color, bg);
}
