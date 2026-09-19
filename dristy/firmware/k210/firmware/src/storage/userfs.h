#ifndef HK_USERFS_H
#define HK_USERFS_H

#include <stddef.h>
#include <stdint.h>

#define USERFS_NAME_MAX 63U
#define USERFS_FILE_MAX (256U * 1024U)
#define USERFS_UPLOAD_CHUNK_MAX 1024U

typedef enum
{
    USERFS_STATE_UNINITIALIZED = 0,
    USERFS_STATE_UNSUPPORTED_FLASH,
    USERFS_STATE_UNFORMATTED,
    USERFS_STATE_CORRUPT,
    USERFS_STATE_IO_ERROR,
    USERFS_STATE_MOUNTED,
} userfs_state_t;

typedef enum
{
    USERFS_OK = 0,
    USERFS_ERROR_INVALID_ARGUMENT,
    USERFS_ERROR_NOT_MOUNTED,
    USERFS_ERROR_UNSUPPORTED_FLASH,
    USERFS_ERROR_UNFORMATTED,
    USERFS_ERROR_CORRUPT,
    USERFS_ERROR_IO,
    USERFS_ERROR_NOT_FOUND,
    USERFS_ERROR_EXISTS,
    USERFS_ERROR_NO_SPACE,
    USERFS_ERROR_BUSY,
    USERFS_ERROR_OUT_OF_ORDER,
    USERFS_ERROR_SIZE,
    USERFS_ERROR_CRC,
} userfs_result_t;

typedef struct
{
    userfs_state_t state;
    userfs_result_t last_error;
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t upload_id;
    uint32_t upload_offset;
    uint32_t upload_size;
} userfs_status_t;

typedef uint8_t (*userfs_list_callback_t)(const char *name, uint32_t size,
                                          void *context);

userfs_result_t userfs_mount(void);
userfs_result_t userfs_unmount(void);
/* Never called implicitly. UI/protocol must obtain explicit user confirmation. */
userfs_result_t userfs_format_explicit(void);
void userfs_get_status(userfs_status_t *status);

userfs_result_t userfs_list(userfs_list_callback_t callback, void *context);
userfs_result_t userfs_stat(const char *name, uint32_t *size);
userfs_result_t userfs_read(const char *name, uint32_t offset,
                            uint8_t *data, size_t capacity, size_t *read_size);
userfs_result_t userfs_remove(const char *name);

userfs_result_t userfs_upload_begin(const char *name, uint32_t size,
                                    uint32_t crc32, uint32_t *upload_id);
userfs_result_t userfs_upload_write(uint32_t upload_id, uint32_t offset,
                                    const uint8_t *data, size_t length);
userfs_result_t userfs_upload_commit(uint32_t upload_id);
userfs_result_t userfs_upload_abort(uint32_t upload_id);

userfs_result_t userfs_set_startup(const char *name);
userfs_result_t userfs_get_startup(char *name, size_t capacity);

const char *userfs_result_name(userfs_result_t result);

#ifdef USERFS_TESTING
/* Host fault-injection hook: drops all RAM state without touching flash. */
void userfs_test_power_cycle(void);
#endif

#endif
