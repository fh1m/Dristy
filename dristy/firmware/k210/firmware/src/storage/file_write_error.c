#include "file_write_error.h"

#include <stddef.h>

static const char *g_file_write_error = "SD WRITE";

void file_write_set_error(const char *error)
{
    g_file_write_error = error;
}

void file_write_clear_error(void)
{
    g_file_write_error = NULL;
}

const char *file_write_last_error(void)
{
    return g_file_write_error;
}
