#ifndef HK_MICROPYTHON_PROGRAM_H
#define HK_MICROPYTHON_PROGRAM_H

#include <stdint.h>

typedef enum
{
    MICROPYTHON_PROGRAM_OK = 0,
    MICROPYTHON_PROGRAM_INVALID_ARGUMENT,
    MICROPYTHON_PROGRAM_NOT_FOUND,
    MICROPYTHON_PROGRAM_FILESYSTEM,
    MICROPYTHON_PROGRAM_TOO_LARGE,
    MICROPYTHON_PROGRAM_BUSY,
    MICROPYTHON_PROGRAM_IO,
} micropython_program_result_t;

/* Loads a complete, CRC-verified userfs file on core 0, then copies it into
 * the uncached runtime handoff and schedules the VM on core 1. */
micropython_program_result_t micropython_program_run_file(
    const char *name, uint32_t time_limit_ms, uint32_t *run_id);
micropython_program_result_t micropython_program_run_startup(
    uint32_t time_limit_ms, uint32_t *run_id);

const char *micropython_program_result_name(
    micropython_program_result_t result);

#endif
