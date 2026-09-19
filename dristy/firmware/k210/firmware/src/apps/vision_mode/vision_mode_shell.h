#ifndef HK_VISION_MODE_SHELL_H
#define HK_VISION_MODE_SHELL_H

#include <stdint.h>

/* When set, legacy vision apps run under the unified vision_mode shell:
 * no legacy screen IDs, no legacy compose overlays (result bus draws on LCD). */
void vision_mode_shell_set_active(uint8_t active);
uint8_t vision_mode_shell_active(void);

#endif
