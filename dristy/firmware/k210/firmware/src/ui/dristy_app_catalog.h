#ifndef DRISTY_APP_CATALOG_H
#define DRISTY_APP_CATALOG_H

#include <stdint.h>

typedef enum
{
    DRISTY_CAT_NEURAL = 0,
    DRISTY_CAT_CLASSICAL,
    DRISTY_CAT_HYBRID,
    DRISTY_CAT_TOOLS,
    DRISTY_CAT_SYSTEM,
} dristy_app_category_t;

typedef struct
{
    const char *title;
    const char *subtitle;
    dristy_app_category_t category;
} dristy_app_meta_t;

const dristy_app_meta_t *dristy_app_meta_for_id(const char *app_id);
const char *dristy_app_category_label(dristy_app_category_t cat);

/* Display order: indices into g_menu_items[] */
uint8_t dristy_menu_sorted_count(void);
uint8_t dristy_menu_sorted_reg_index(uint8_t display_index);

dristy_app_category_t dristy_menu_category_at(uint8_t display_index);

uint8_t dristy_menu_display_index_for_reg(uint8_t reg_index);

#endif
