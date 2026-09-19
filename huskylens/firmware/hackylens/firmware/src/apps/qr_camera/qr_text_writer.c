#include "../../storage/file_write_error.h"

#include <stddef.h>

#include <stdio.h>
#include <string.h>

#include "../../config/sd_config.h"
#include "../../config/fat32_config.h"
#include "../../storage/file_mount.h"
#include "../../storage/fat32_allocation.h"
#include "../../storage/fat32_volume.h"
#include "../../storage/internal/fat32_state_private.h"
#include "qr_text_path.h"
#include "qr_text_writer.h"
#include "../../storage/sd_card.h"

static void qr_text_fill_bytes(const char *text, uint32_t offset, uint8_t *dst, uint16_t len)
{
    uint32_t text_len = (uint32_t)strlen(text);

    for(uint16_t i = 0; i < len; i++)
    {
        uint32_t pos = offset + i;
        if(pos < text_len)
            dst[i] = (uint8_t)text[pos];
        else if(pos == text_len)
            dst[i] = '\r';
        else if(pos == text_len + 1U)
            dst[i] = '\n';
        else
            dst[i] = 0;
    }
}

static uint8_t qr_text_write_file_chain(uint32_t first_cluster, const char *text, uint32_t file_size)
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
            qr_text_fill_bytes(text, offset, sector, take);
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

uint8_t qr_text_save_payload_text(const char *payload, char *saved_name, size_t saved_name_size)
{
    uint8_t short_name[11];
    uint32_t first_cluster;
    uint32_t cluster_size;
    uint32_t cluster_count;
    uint32_t file_size;

    if(payload == NULL || payload[0] == '\0')
    {
        file_write_set_error("NO DATA");
        return 0;
    }
    if(!file_mount_if_needed())
    {
        file_write_set_error("MOUNT");
        return 0;
    }
    if(!qr_text_path_prepare(short_name, saved_name, saved_name_size))
    {
        if(file_write_last_error() == NULL)
            file_write_set_error("NAME");
        return 0;
    }

    file_size = (uint32_t)strlen(payload) + 2U;
    cluster_size = fat32_cluster_size();
    if(cluster_size == 0)
    {
        file_write_set_error("CLUSTER");
        return 0;
    }
    cluster_count = (file_size + cluster_size - 1U) / cluster_size;
    if(!fat_alloc_chain(cluster_count, &first_cluster))
    {
        file_write_set_error("ALLOC");
        return 0;
    }
    if(!qr_text_write_file_chain(first_cluster, payload, file_size))
    {
        file_write_set_error("WRITE");
        return 0;
    }
    if(!qr_text_path_add_file_entry(short_name, first_cluster, file_size))
    {
        if(file_write_last_error() == NULL)
            file_write_set_error("DIRENT");
        return 0;
    }

    file_write_clear_error();
    printf("[QR] saved text %s size=%u cluster=%u\r\n", saved_name, (unsigned)file_size, first_cluster);
    return 1;
}
