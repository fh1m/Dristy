#include "userfs.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lfs.h"

#include "../core/hk_binary.h"
#include "internal_flash.h"

#define USERFS_BLOCK_SIZE 4096U
#define USERFS_CACHE_SIZE 256U
#define USERFS_LOOKAHEAD_SIZE 32U
#define USERFS_BLOCK_CYCLES 500
#define USERFS_STARTUP_FILE ".startup"
#define USERFS_META_TEMP ".meta-upload"
#define USERFS_UPLOAD_PREFIX ".upload-"

typedef struct
{
    uint8_t active;
    uint8_t file_open;
    uint32_t id;
    uint32_t expected_size;
    uint32_t expected_crc;
    uint32_t offset;
    char target[USERFS_NAME_MAX + 1U];
    char temporary[USERFS_NAME_MAX + 1U];
    lfs_file_t file;
} userfs_upload_t;

static lfs_t g_lfs;
static struct lfs_config g_config;
static struct lfs_file_config g_file_config;
static uint8_t g_read_cache[USERFS_CACHE_SIZE] __attribute__((aligned(16)));
static uint8_t g_prog_cache[USERFS_CACHE_SIZE] __attribute__((aligned(16)));
static uint8_t g_lookahead[USERFS_LOOKAHEAD_SIZE] __attribute__((aligned(16)));
static uint8_t g_file_cache[USERFS_CACHE_SIZE] __attribute__((aligned(16)));
static userfs_upload_t g_upload;
static userfs_state_t g_state;
static userfs_result_t g_last_error;
static uint32_t g_next_upload_id;

static userfs_result_t userfs_remember(userfs_result_t result)
{
    g_last_error = result;
    return result;
}

static int userfs_block_read(const struct lfs_config *config,
                             lfs_block_t block, lfs_off_t offset,
                             void *buffer, lfs_size_t size)
{
    internal_flash_result_t result;
    uint32_t address;

    (void)config;
    if(block >= g_config.block_count ||
       offset > USERFS_BLOCK_SIZE || size > USERFS_BLOCK_SIZE - offset)
        return LFS_ERR_INVAL;
    address = (uint32_t)block * USERFS_BLOCK_SIZE + offset;
    result = internal_flash_read(INTERNAL_FLASH_PARTITION_USERFS, address,
                                 (uint8_t *)buffer, size);
    return result == INTERNAL_FLASH_OK ? 0 : LFS_ERR_IO;
}

static int userfs_block_program(const struct lfs_config *config,
                                lfs_block_t block, lfs_off_t offset,
                                const void *buffer, lfs_size_t size)
{
    uint8_t verify[USERFS_CACHE_SIZE];
    internal_flash_result_t result;
    uint32_t address;

    (void)config;
    if(block >= g_config.block_count || size > sizeof(verify) ||
       offset > USERFS_BLOCK_SIZE || size > USERFS_BLOCK_SIZE - offset)
        return LFS_ERR_INVAL;
    address = (uint32_t)block * USERFS_BLOCK_SIZE + offset;
    result = internal_flash_program(INTERNAL_FLASH_PARTITION_USERFS, address,
                                    (const uint8_t *)buffer, size);
    if(result != INTERNAL_FLASH_OK)
        return LFS_ERR_IO;
    result = internal_flash_read(INTERNAL_FLASH_PARTITION_USERFS, address,
                                 verify, size);
    if(result != INTERNAL_FLASH_OK)
        return LFS_ERR_IO;
    return memcmp(verify, buffer, size) == 0 ? 0 : LFS_ERR_CORRUPT;
}

static int userfs_block_erase(const struct lfs_config *config,
                              lfs_block_t block)
{
    internal_flash_result_t result;

    (void)config;
    if(block >= g_config.block_count)
        return LFS_ERR_INVAL;
    result = internal_flash_erase(INTERNAL_FLASH_PARTITION_USERFS,
                                  (uint32_t)block * USERFS_BLOCK_SIZE,
                                  USERFS_BLOCK_SIZE);
    return result == INTERNAL_FLASH_OK ? 0 : LFS_ERR_IO;
}

static int userfs_block_sync(const struct lfs_config *config)
{
    (void)config;
    return 0;
}

static void userfs_configure(uint32_t partition_size)
{
    memset(&g_config, 0, sizeof(g_config));
    memset(&g_file_config, 0, sizeof(g_file_config));
    g_config.read = userfs_block_read;
    g_config.prog = userfs_block_program;
    g_config.erase = userfs_block_erase;
    g_config.sync = userfs_block_sync;
    g_config.read_size = 16U;
    g_config.prog_size = 16U;
    g_config.block_size = USERFS_BLOCK_SIZE;
    g_config.block_count = partition_size / USERFS_BLOCK_SIZE;
    g_config.block_cycles = USERFS_BLOCK_CYCLES;
    g_config.cache_size = USERFS_CACHE_SIZE;
    g_config.lookahead_size = USERFS_LOOKAHEAD_SIZE;
    g_config.read_buffer = g_read_cache;
    g_config.prog_buffer = g_prog_cache;
    g_config.lookahead_buffer = g_lookahead;
    g_config.name_max = USERFS_NAME_MAX;
    g_config.file_max = USERFS_FILE_MAX;
    g_config.attr_max = 0U;
    g_config.metadata_max = USERFS_BLOCK_SIZE;
    g_config.inline_max = USERFS_CACHE_SIZE;
    g_file_config.buffer = g_file_cache;
}

static userfs_result_t userfs_from_lfs(int error)
{
    if(error >= 0)
        return USERFS_OK;
    switch(error)
    {
    case LFS_ERR_NOENT:
        return USERFS_ERROR_NOT_FOUND;
    case LFS_ERR_EXIST:
        return USERFS_ERROR_EXISTS;
    case LFS_ERR_NOSPC:
        return USERFS_ERROR_NO_SPACE;
    case LFS_ERR_INVAL:
    case LFS_ERR_NAMETOOLONG:
        return USERFS_ERROR_INVALID_ARGUMENT;
    case LFS_ERR_CORRUPT:
        return USERFS_ERROR_CORRUPT;
    default:
        return USERFS_ERROR_IO;
    }
}

static uint8_t userfs_valid_name(const char *name, uint8_t allow_hidden)
{
    size_t length;

    if(!name)
        return 0U;
    if(name[0] == '/')
        name++;
    length = strlen(name);
    if(!length || length > USERFS_NAME_MAX ||
       (!allow_hidden && name[0] == '.'))
        return 0U;
    for(size_t i = 0; i < length; i++)
    {
        char c = name[i];
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            continue;
        return 0U;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static const char *userfs_normal_name(const char *name)
{
    return name && name[0] == '/' ? name + 1 : name;
}

static uint8_t userfs_blank_media(void)
{
    uint8_t data[USERFS_CACHE_SIZE];
    uint32_t offsets[2] = {0U, USERFS_BLOCK_SIZE};

    for(uint8_t block = 0U; block < 2U; block++)
    {
        for(uint32_t offset = 0U; offset < USERFS_BLOCK_SIZE;
            offset += sizeof(data))
        {
            if(internal_flash_read(INTERNAL_FLASH_PARTITION_USERFS,
                                   offsets[block] + offset,
                                   data, sizeof(data)) != INTERNAL_FLASH_OK)
                return 0U;
            for(size_t i = 0; i < sizeof(data); i++)
            {
                if(data[i] != 0xFFU)
                    return 0U;
            }
        }
    }
    return 1U;
}

static void userfs_clear_upload(void)
{
    memset(&g_upload, 0, sizeof(g_upload));
}

static void userfs_cleanup_uploads(void)
{
    for(;;)
    {
        lfs_dir_t directory;
        struct lfs_info info;
        char stale[USERFS_NAME_MAX + 1U] = {0};
        int result = lfs_dir_open(&g_lfs, &directory, "/");

        if(result < 0)
            return;
        while((result = lfs_dir_read(&g_lfs, &directory, &info)) > 0)
        {
            if(strncmp(info.name, USERFS_UPLOAD_PREFIX,
                       sizeof(USERFS_UPLOAD_PREFIX) - 1U) == 0 ||
               strcmp(info.name, USERFS_META_TEMP) == 0)
            {
                strncpy(stale, info.name, USERFS_NAME_MAX);
                break;
            }
        }
        lfs_dir_close(&g_lfs, &directory);
        if(!stale[0])
            return;
        if(lfs_remove(&g_lfs, stale) < 0)
            return;
    }
}

userfs_result_t userfs_mount(void)
{
    internal_flash_info_t flash;
    internal_flash_partition_info_t partition;
    internal_flash_result_t flash_result;
    int result;

    if(g_state == USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_OK);
    internal_flash_get_info(&flash);
    if(!flash.initialized)
    {
        flash_result = internal_flash_init(10000000U);
        if(flash_result != INTERNAL_FLASH_OK)
        {
            g_state = USERFS_STATE_IO_ERROR;
            return userfs_remember(USERFS_ERROR_IO);
        }
        internal_flash_get_info(&flash);
    }
    flash_result = internal_flash_partition_info(
        INTERNAL_FLASH_PARTITION_USERFS, &partition);
    if(flash_result != INTERNAL_FLASH_OK || !partition.available ||
       flash.capacity != partition.required_capacity)
    {
        g_state = USERFS_STATE_UNSUPPORTED_FLASH;
        return userfs_remember(USERFS_ERROR_UNSUPPORTED_FLASH);
    }

    userfs_configure(partition.size);
    result = lfs_mount(&g_lfs, &g_config);
    if(result == 0)
    {
        g_state = USERFS_STATE_MOUNTED;
        userfs_clear_upload();
        userfs_cleanup_uploads();
        return userfs_remember(USERFS_OK);
    }
    if(result == LFS_ERR_CORRUPT && userfs_blank_media())
    {
        g_state = USERFS_STATE_UNFORMATTED;
        return userfs_remember(USERFS_ERROR_UNFORMATTED);
    }
    g_state = result == LFS_ERR_CORRUPT ? USERFS_STATE_CORRUPT :
                                         USERFS_STATE_IO_ERROR;
    return userfs_remember(result == LFS_ERR_CORRUPT ? USERFS_ERROR_CORRUPT :
                                                     USERFS_ERROR_IO);
}

userfs_result_t userfs_unmount(void)
{
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(g_upload.active)
        userfs_upload_abort(g_upload.id);
    result = lfs_unmount(&g_lfs);
    g_state = result == 0 ? USERFS_STATE_UNINITIALIZED : USERFS_STATE_IO_ERROR;
    return userfs_remember(userfs_from_lfs(result));
}

userfs_result_t userfs_format_explicit(void)
{
    internal_flash_info_t flash;
    internal_flash_partition_info_t partition;
    internal_flash_result_t flash_result;
    int result;

    if(g_state == USERFS_STATE_MOUNTED)
        (void)userfs_unmount();
    internal_flash_get_info(&flash);
    if(!flash.initialized)
    {
        flash_result = internal_flash_init(10000000U);
        if(flash_result != INTERNAL_FLASH_OK)
        {
            g_state = USERFS_STATE_IO_ERROR;
            return userfs_remember(USERFS_ERROR_IO);
        }
    }
    flash_result = internal_flash_partition_info(
        INTERNAL_FLASH_PARTITION_USERFS, &partition);
    if(flash_result != INTERNAL_FLASH_OK || !partition.available)
    {
        g_state = USERFS_STATE_UNSUPPORTED_FLASH;
        return userfs_remember(USERFS_ERROR_UNSUPPORTED_FLASH);
    }
    userfs_configure(partition.size);
    result = lfs_format(&g_lfs, &g_config);
    if(result < 0)
    {
        g_state = USERFS_STATE_IO_ERROR;
        return userfs_remember(userfs_from_lfs(result));
    }
    g_state = USERFS_STATE_UNINITIALIZED;
    return userfs_mount();
}

void userfs_get_status(userfs_status_t *status)
{
    lfs_ssize_t blocks;

    if(!status)
        return;
    memset(status, 0, sizeof(*status));
    status->state = g_state;
    status->last_error = g_last_error;
    status->total_bytes = g_config.block_count * g_config.block_size;
    if(g_state == USERFS_STATE_MOUNTED)
    {
        blocks = lfs_fs_size(&g_lfs);
        if(blocks > 0)
            status->used_bytes = (uint32_t)blocks * g_config.block_size;
    }
    if(g_upload.active)
    {
        status->upload_id = g_upload.id;
        status->upload_offset = g_upload.offset;
        status->upload_size = g_upload.expected_size;
    }
}

userfs_result_t userfs_list(userfs_list_callback_t callback, void *context)
{
    lfs_dir_t directory;
    struct lfs_info info;
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(!callback || g_upload.active)
        return userfs_remember(g_upload.active ? USERFS_ERROR_BUSY :
                                                USERFS_ERROR_INVALID_ARGUMENT);
    result = lfs_dir_open(&g_lfs, &directory, "/");
    if(result < 0)
        return userfs_remember(userfs_from_lfs(result));
    while((result = lfs_dir_read(&g_lfs, &directory, &info)) > 0)
    {
        if(info.type != LFS_TYPE_REG || info.name[0] == '.')
            continue;
        if(!callback(info.name, info.size, context))
            break;
    }
    lfs_dir_close(&g_lfs, &directory);
    return userfs_remember(result < 0 ? userfs_from_lfs(result) : USERFS_OK);
}

userfs_result_t userfs_stat(const char *name, uint32_t *size)
{
    struct lfs_info info;
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(!size || !userfs_valid_name(name, 0U))
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    result = lfs_stat(&g_lfs, userfs_normal_name(name), &info);
    if(result == 0 && info.type != LFS_TYPE_REG)
        result = LFS_ERR_NOENT;
    if(result == 0)
        *size = info.size;
    return userfs_remember(userfs_from_lfs(result));
}

static int userfs_file_open(lfs_file_t *file, const char *name, int flags)
{
    memset(g_file_cache, 0, sizeof(g_file_cache));
    return lfs_file_opencfg(&g_lfs, file, name, flags, &g_file_config);
}

userfs_result_t userfs_read(const char *name, uint32_t offset,
                            uint8_t *data, size_t capacity, size_t *read_size)
{
    lfs_file_t file;
    lfs_soff_t position;
    lfs_ssize_t count;
    int result;

    if(read_size)
        *read_size = 0U;
    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(g_upload.active)
        return userfs_remember(USERFS_ERROR_BUSY);
    if(!read_size || (capacity && !data) || !userfs_valid_name(name, 0U))
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    result = userfs_file_open(&file, userfs_normal_name(name), LFS_O_RDONLY);
    if(result < 0)
        return userfs_remember(userfs_from_lfs(result));
    position = lfs_file_seek(&g_lfs, &file, offset, LFS_SEEK_SET);
    if(position < 0)
    {
        lfs_file_close(&g_lfs, &file);
        return userfs_remember(userfs_from_lfs((int)position));
    }
    count = capacity ? lfs_file_read(&g_lfs, &file, data, capacity) : 0;
    lfs_file_close(&g_lfs, &file);
    if(count < 0)
        return userfs_remember(userfs_from_lfs((int)count));
    *read_size = (size_t)count;
    return userfs_remember(USERFS_OK);
}

userfs_result_t userfs_remove(const char *name)
{
    char startup[USERFS_NAME_MAX + 1U] = {0};
    userfs_result_t startup_result;
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(g_upload.active)
        return userfs_remember(USERFS_ERROR_BUSY);
    if(!userfs_valid_name(name, 0U))
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);

    /* Clear the selection first. A power loss can then leave an unselected
     * file, but never metadata that points at a file already deleted. */
    startup_result = userfs_get_startup(startup, sizeof(startup));
    if(startup_result == USERFS_OK &&
       strcmp(userfs_normal_name(name), startup) == 0)
    {
        result = lfs_remove(&g_lfs, USERFS_STARTUP_FILE);
        if(result < 0 && result != LFS_ERR_NOENT)
            return userfs_remember(userfs_from_lfs(result));
    }
    result = lfs_remove(&g_lfs, userfs_normal_name(name));
    return userfs_remember(userfs_from_lfs(result));
}

userfs_result_t userfs_upload_begin(const char *name, uint32_t size,
                                    uint32_t crc32, uint32_t *upload_id)
{
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(g_upload.active)
        return userfs_remember(USERFS_ERROR_BUSY);
    if(!upload_id || !userfs_valid_name(name, 0U) || size > USERFS_FILE_MAX)
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);

    userfs_clear_upload();
    g_next_upload_id++;
    if(!g_next_upload_id)
        g_next_upload_id = 1U;
    g_upload.id = g_next_upload_id;
    g_upload.expected_size = size;
    g_upload.expected_crc = crc32;
    strncpy(g_upload.target, userfs_normal_name(name), USERFS_NAME_MAX);
    /* 8 hex digits plus prefix fit comfortably in the on-disk name limit. */
    {
        static const char hex[] = "0123456789abcdef";
        size_t prefix = sizeof(USERFS_UPLOAD_PREFIX) - 1U;
        memcpy(g_upload.temporary, USERFS_UPLOAD_PREFIX, prefix);
        for(uint8_t i = 0U; i < 8U; i++)
            g_upload.temporary[prefix + i] =
                hex[(g_upload.id >> ((7U - i) * 4U)) & 0x0FU];
        g_upload.temporary[prefix + 8U] = '\0';
    }
    (void)lfs_remove(&g_lfs, g_upload.temporary);
    result = userfs_file_open(&g_upload.file, g_upload.temporary,
                              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL);
    if(result < 0)
    {
        userfs_clear_upload();
        return userfs_remember(userfs_from_lfs(result));
    }
    g_upload.file_open = 1U;
    g_upload.active = 1U;
    *upload_id = g_upload.id;
    return userfs_remember(USERFS_OK);
}

userfs_result_t userfs_upload_write(uint32_t upload_id, uint32_t offset,
                                    const uint8_t *data, size_t length)
{
    lfs_ssize_t written;

    if(!g_upload.active || upload_id != g_upload.id)
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    if(offset != g_upload.offset)
        return userfs_remember(USERFS_ERROR_OUT_OF_ORDER);
    if((length && !data) || length > USERFS_UPLOAD_CHUNK_MAX ||
       length > g_upload.expected_size - g_upload.offset)
        return userfs_remember(USERFS_ERROR_SIZE);
    written = lfs_file_write(&g_lfs, &g_upload.file, data, length);
    if(written < 0 || (size_t)written != length)
        return userfs_remember(written == LFS_ERR_NOSPC ? USERFS_ERROR_NO_SPACE :
                                                       USERFS_ERROR_IO);
    g_upload.offset += (uint32_t)length;
    return userfs_remember(USERFS_OK);
}

static userfs_result_t userfs_verify_upload(void)
{
    uint8_t data[USERFS_CACHE_SIZE];
    lfs_file_t file;
    lfs_ssize_t count;
    uint32_t crc = 0U;
    uint32_t total = 0U;
    int result = userfs_file_open(&file, g_upload.temporary, LFS_O_RDONLY);

    if(result < 0)
        return userfs_from_lfs(result);
    while((count = lfs_file_read(&g_lfs, &file, data, sizeof(data))) > 0)
    {
        crc = crc32_update(crc, data, (size_t)count);
        total += (uint32_t)count;
    }
    lfs_file_close(&g_lfs, &file);
    if(count < 0)
        return userfs_from_lfs((int)count);
    if(total != g_upload.expected_size)
        return USERFS_ERROR_SIZE;
    return crc == g_upload.expected_crc ? USERFS_OK : USERFS_ERROR_CRC;
}

userfs_result_t userfs_upload_commit(uint32_t upload_id)
{
    userfs_result_t verify;
    int result;

    if(!g_upload.active || upload_id != g_upload.id)
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    if(g_upload.offset != g_upload.expected_size)
        return userfs_remember(USERFS_ERROR_SIZE);
    result = lfs_file_sync(&g_lfs, &g_upload.file);
    {
        int close_result = lfs_file_close(&g_lfs, &g_upload.file);
        if(result == 0)
            result = close_result;
    }
    g_upload.file_open = 0U;
    if(result < 0)
        return userfs_remember(userfs_from_lfs(result));
    verify = userfs_verify_upload();
    if(verify != USERFS_OK)
        return userfs_remember(verify);
    result = lfs_rename(&g_lfs, g_upload.temporary, g_upload.target);
    if(result < 0)
        return userfs_remember(userfs_from_lfs(result));
    userfs_clear_upload();
    return userfs_remember(USERFS_OK);
}

userfs_result_t userfs_upload_abort(uint32_t upload_id)
{
    if(!g_upload.active || upload_id != g_upload.id)
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    if(g_upload.file_open)
        lfs_file_close(&g_lfs, &g_upload.file);
    (void)lfs_remove(&g_lfs, g_upload.temporary);
    userfs_clear_upload();
    return userfs_remember(USERFS_OK);
}

static userfs_result_t userfs_atomic_metadata(const char *data, size_t size)
{
    lfs_file_t file;
    lfs_ssize_t written;
    int result;

    (void)lfs_remove(&g_lfs, USERFS_META_TEMP);
    result = userfs_file_open(&file, USERFS_META_TEMP,
                              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL);
    if(result < 0)
        return userfs_from_lfs(result);
    written = lfs_file_write(&g_lfs, &file, data, size);
    if(written < 0 || (size_t)written != size)
    {
        lfs_file_close(&g_lfs, &file);
        (void)lfs_remove(&g_lfs, USERFS_META_TEMP);
        return written == LFS_ERR_NOSPC ? USERFS_ERROR_NO_SPACE : USERFS_ERROR_IO;
    }
    result = lfs_file_sync(&g_lfs, &file);
    {
        int close_result = lfs_file_close(&g_lfs, &file);
        if(result == 0)
            result = close_result;
    }
    if(result == 0)
        result = lfs_rename(&g_lfs, USERFS_META_TEMP, USERFS_STARTUP_FILE);
    return userfs_from_lfs(result);
}

userfs_result_t userfs_set_startup(const char *name)
{
    struct lfs_info info;
    const char *normal;
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(g_upload.active)
        return userfs_remember(USERFS_ERROR_BUSY);
    if(!name || !name[0])
    {
        result = lfs_remove(&g_lfs, USERFS_STARTUP_FILE);
        if(result == LFS_ERR_NOENT)
            result = 0;
        return userfs_remember(userfs_from_lfs(result));
    }
    if(!userfs_valid_name(name, 0U))
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    normal = userfs_normal_name(name);
    result = lfs_stat(&g_lfs, normal, &info);
    if(result < 0 || info.type != LFS_TYPE_REG)
        return userfs_remember(USERFS_ERROR_NOT_FOUND);
    return userfs_remember(userfs_atomic_metadata(normal, strlen(normal)));
}

userfs_result_t userfs_get_startup(char *name, size_t capacity)
{
    lfs_file_t file;
    lfs_ssize_t count;
    int result;

    if(g_state != USERFS_STATE_MOUNTED)
        return userfs_remember(USERFS_ERROR_NOT_MOUNTED);
    if(!name || capacity < 2U)
        return userfs_remember(USERFS_ERROR_INVALID_ARGUMENT);
    result = userfs_file_open(&file, USERFS_STARTUP_FILE, LFS_O_RDONLY);
    if(result < 0)
        return userfs_remember(userfs_from_lfs(result));
    count = lfs_file_read(&g_lfs, &file, name, capacity - 1U);
    lfs_file_close(&g_lfs, &file);
    if(count < 0)
        return userfs_remember(userfs_from_lfs((int)count));
    name[count] = '\0';
    if(!userfs_valid_name(name, 0U))
        return userfs_remember(USERFS_ERROR_CORRUPT);
    return userfs_remember(USERFS_OK);
}

const char *userfs_result_name(userfs_result_t result)
{
    switch(result)
    {
    case USERFS_OK: return "ok";
    case USERFS_ERROR_INVALID_ARGUMENT: return "invalid-argument";
    case USERFS_ERROR_NOT_MOUNTED: return "not-mounted";
    case USERFS_ERROR_UNSUPPORTED_FLASH: return "unsupported-flash";
    case USERFS_ERROR_UNFORMATTED: return "unformatted";
    case USERFS_ERROR_CORRUPT: return "corrupt";
    case USERFS_ERROR_IO: return "io";
    case USERFS_ERROR_NOT_FOUND: return "not-found";
    case USERFS_ERROR_EXISTS: return "exists";
    case USERFS_ERROR_NO_SPACE: return "no-space";
    case USERFS_ERROR_BUSY: return "busy";
    case USERFS_ERROR_OUT_OF_ORDER: return "out-of-order";
    case USERFS_ERROR_SIZE: return "size";
    case USERFS_ERROR_CRC: return "crc";
    default: return "unknown";
    }
}

#ifdef USERFS_TESTING
void userfs_test_power_cycle(void)
{
    memset(&g_lfs, 0, sizeof(g_lfs));
    memset(&g_config, 0, sizeof(g_config));
    memset(&g_file_config, 0, sizeof(g_file_config));
    userfs_clear_upload();
    g_state = USERFS_STATE_UNINITIALIZED;
    g_last_error = USERFS_OK;
}
#endif
