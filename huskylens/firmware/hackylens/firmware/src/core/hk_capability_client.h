#ifndef HK_CAPABILITY_CLIENT_H
#define HK_CAPABILITY_CLIENT_H

#include <hackylens/capability/owner.h>

/* Private static-firmware owner delivery; not part of the App Runtime ABI. */
hk_owner_t capability_client_current_owner(void);
hk_owner_t capability_client_consumer_owner(const char *consumer_id);

#endif
