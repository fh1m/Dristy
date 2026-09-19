#include "files_actions.h"

#include "../../core/hk_events.h"

#include "file_browser_list.h"
#include "file_browser_mode.h"
#include "file_browser_navigation.h"
#include "file_dir.h"
#include "../../storage/file_mount.h"
#include "../../storage/fat32_volume.h"
#include "files_presenter.h"

uint8_t files_on_sd_event(hk_sd_event_t event)
{
    files_presenter_close_image();
    if(event == HK_SD_EVENT_REMOVED || event == HK_SD_EVENT_ERROR)
    {
        files_reset_depth();
        files_set_mode(FILES_MODE_LIST);
        files_delete_state_reset();
        files_reset_list();
        return 0;
    }

    if(event != HK_SD_EVENT_INSERTED && event != HK_SD_EVENT_MOUNTED)
        return hk_fat_mounted();

    files_reset_depth();
    files_set_mode(FILES_MODE_LIST);
    files_delete_state_reset();
    files_set_current_cluster(hk_fat_root_cluster());
    if(!hk_fat_mounted())
        return 0;
    return files_load_dir(files_current_cluster());
}

void files_backend_enter(void)
{
    files_presenter_close_image();
    files_presenter_enter();
    files_reset_depth();
    files_set_mode(FILES_MODE_LIST);
    files_delete_state_reset();
    files_set_current_cluster(hk_fat_root_cluster());

    if(!file_mount_if_needed())
    {
        files_presenter_show_status(hk_sd_present() ? "FAT32 ONLY" : "NO SD");
        return;
    }

    files_set_current_cluster(hk_fat_root_cluster());
    if(!files_load_dir(files_current_cluster()))
    {
        files_presenter_show_result(FILE_RESULT_READ_ERROR);
        return;
    }
    files_presenter_render_list();
}
