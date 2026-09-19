#include "micropython_program.h"

#include <stddef.h>
#include <stdint.h>

#include "micropython_runtime.h"
#include "../storage/userfs.h"

static char g_program_source[MICROPYTHON_RUNTIME_SOURCE_MAX + 1U];

static micropython_program_result_t micropython_program_from_userfs(
    userfs_result_t result)
{
    switch(result)
    {
    case USERFS_OK:
        return MICROPYTHON_PROGRAM_OK;
    case USERFS_ERROR_NOT_FOUND:
        return MICROPYTHON_PROGRAM_NOT_FOUND;
    case USERFS_ERROR_SIZE:
        return MICROPYTHON_PROGRAM_TOO_LARGE;
    case USERFS_ERROR_BUSY:
        return MICROPYTHON_PROGRAM_BUSY;
    case USERFS_ERROR_IO:
    case USERFS_ERROR_CORRUPT:
    case USERFS_ERROR_CRC:
        return MICROPYTHON_PROGRAM_IO;
    default:
        return MICROPYTHON_PROGRAM_FILESYSTEM;
    }
}

micropython_program_result_t micropython_program_run_file(
    const char *name, uint32_t time_limit_ms, uint32_t *run_id)
{
    micropython_runtime_status_t status;
    userfs_result_t fs_result;
    uint32_t size;
    size_t read_size;

    if(run_id)
        *run_id = 0U;
    if(!name || !name[0])
        return MICROPYTHON_PROGRAM_INVALID_ARGUMENT;
    fs_result = userfs_mount();
    if(fs_result != USERFS_OK)
        return micropython_program_from_userfs(fs_result);
    fs_result = userfs_stat(name, &size);
    if(fs_result != USERFS_OK)
        return micropython_program_from_userfs(fs_result);
    if(size == 0U || size > MICROPYTHON_RUNTIME_SOURCE_MAX)
        return MICROPYTHON_PROGRAM_TOO_LARGE;
    fs_result = userfs_read(name, 0U, (uint8_t *)g_program_source,
                            size, &read_size);
    if(fs_result != USERFS_OK)
        return micropython_program_from_userfs(fs_result);
    if(read_size != size)
        return MICROPYTHON_PROGRAM_IO;
    g_program_source[size] = '\0';

    if(!micropython_runtime_start(g_program_source, size, time_limit_ms))
        return MICROPYTHON_PROGRAM_BUSY;
    micropython_runtime_get_status(&status);
    if(run_id)
        *run_id = status.run_id;
    return MICROPYTHON_PROGRAM_OK;
}

micropython_program_result_t micropython_program_run_startup(
    uint32_t time_limit_ms, uint32_t *run_id)
{
    char name[USERFS_NAME_MAX + 1U];
    userfs_result_t result = userfs_mount();

    if(result != USERFS_OK)
        return micropython_program_from_userfs(result);
    result = userfs_get_startup(name, sizeof(name));
    if(result != USERFS_OK)
        return micropython_program_from_userfs(result);
    return micropython_program_run_file(name, time_limit_ms, run_id);
}

const char *micropython_program_result_name(
    micropython_program_result_t result)
{
    switch(result)
    {
    case MICROPYTHON_PROGRAM_OK: return "ok";
    case MICROPYTHON_PROGRAM_INVALID_ARGUMENT: return "invalid-argument";
    case MICROPYTHON_PROGRAM_NOT_FOUND: return "not-found";
    case MICROPYTHON_PROGRAM_FILESYSTEM: return "filesystem";
    case MICROPYTHON_PROGRAM_TOO_LARGE: return "too-large";
    case MICROPYTHON_PROGRAM_BUSY: return "busy";
    case MICROPYTHON_PROGRAM_IO: return "io";
    default: return "unknown";
    }
}
