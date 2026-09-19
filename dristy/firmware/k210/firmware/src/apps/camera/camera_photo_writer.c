#include "../../storage/file_write_error.h"

#include <stddef.h>

#include <stdio.h>
#include <string.h>

#include "../../core/photo_types.h"

#include "../../config/sd_config.h"
#include "../../config/fat32_config.h"
#include "../../storage/file_mount.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/internal/fat32_state_private.h"
#include "camera_photo_encode.h"
#include "camera_photo_path.h"
#include "camera_photo_writer.h"
#include "../../storage/sd_card.h"

uint8_t photo_write_file_chain(uint32_t first_cluster, photo_format_t format, uint32_t file_size, uint16_t width, uint16_t height)
{
    uint32_t cluster = first_cluster;
    uint32_t remaining = file_size;
    uint32_t offset = 0;
    uint8_t sectors_per_cluster = fat32_sectors_per_cluster();
    uint8_t *sector = fat32_sector_scratch();

    while(remaining && cluster >= 2 && cluster < FAT32_EOC)
    {
        uint32_t base = fat_cluster_lba(cluster);
        for(uint8_t s = 0; s < sectors_per_cluster && remaining; s++)
        {
            uint16_t take = remaining > SD_BLOCK_SIZE ? SD_BLOCK_SIZE : (uint16_t)remaining;
            memset(sector, 0, SD_BLOCK_SIZE);
            photo_encode_fill_file_bytes(format, offset, sector, take, width, height);
            if(!sd_card_write_block_fast(base + s, sector))
                return 0;
            offset += take;
            remaining -= take;
        }
        if(remaining)
            cluster = fat_next_cluster(cluster);
    }

    return remaining == 0;
}

uint8_t photo_save_frame(const photo_source_t *source, char *saved_name, size_t saved_name_size)
{
    uint8_t short_name[11];
    uint32_t first_cluster;
    uint32_t cluster_size;
    uint32_t cluster_count;
    uint32_t file_size;

    if(!source || !source->read_pixel || source->width == 0 || source->height == 0)
    {
        file_write_set_error("NO FRAME");
        return 0;
    }

    photo_encode_begin(source);
    file_size = photo_format_size(source->format, source->width, source->height);

    if(!file_mount_if_needed())
    {
        photo_encode_end();
        file_write_set_error("MOUNT");
        printf("[PHOTO] mount failed\r\n");
        return 0;
    }
    if(!photo_path_prepare(short_name, saved_name, saved_name_size, source->format))
    {
        photo_encode_end();
        if(file_write_last_error() == NULL)
            file_write_set_error("NAME");
        printf("[PHOTO] path prepare failed\r\n");
        return 0;
    }

    cluster_size = fat32_cluster_size();
    if(cluster_size == 0)
    {
        photo_encode_end();
        file_write_set_error("CLUSTER");
        return 0;
    }
    cluster_count = (file_size + cluster_size - 1U) / cluster_size;
    if(!fat_alloc_chain(cluster_count, &first_cluster))
    {
        photo_encode_end();
        file_write_set_error("ALLOC");
        printf("[PHOTO] alloc failed clusters=%u\r\n", (unsigned)cluster_count);
        return 0;
    }
    if(!photo_write_file_chain(first_cluster, source->format, file_size, source->width, source->height))
    {
        photo_encode_end();
        file_write_set_error("WRITE");
        printf("[PHOTO] data write failed cluster=%u\r\n", first_cluster);
        return 0;
    }
    if(!photo_path_add_file_entry(short_name, first_cluster, file_size))
    {
        photo_encode_end();
        if(file_write_last_error() == NULL)
            file_write_set_error("DIRENT");
        printf("[PHOTO] dir entry failed\r\n");
        return 0;
    }

    file_write_clear_error();
    printf("[PHOTO] saved %s format=%s frame=%ux%u size=%u cluster=%u\r\n",
           saved_name,
           photo_format_label(source->format),
           source->width,
           source->height,
           (unsigned)file_size,
           first_cluster);
    photo_encode_end();
    return 1;
}
