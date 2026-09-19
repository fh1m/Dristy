#ifndef HK_CORE_FILE_LIST_ITEM_H
#define HK_CORE_FILE_LIST_ITEM_H

#include <stdint.h>

typedef enum
{
    FILE_ITEM_REGULAR,
    FILE_ITEM_DIRECTORY
} file_item_kind_t;

typedef struct
{
    const char *name;
    uint32_t size;
    file_item_kind_t kind;
} file_list_item_t;

#endif
