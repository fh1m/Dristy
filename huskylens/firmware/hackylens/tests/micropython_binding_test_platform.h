#ifndef HK_MICROPYTHON_BINDING_TEST_PLATFORM_H
#define HK_MICROPYTHON_BINDING_TEST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/lights.h>
#include <hackylens/capability/display.h>
#include <hackylens/capability/external_link.h>
#include <hackylens/capability/time.h>

void external_link_service_suspend(void);
void external_link_service_resume(void);
void settings_lights_suspend(uint32_t channels);
void settings_lights_restore(uint32_t channels);
hk_owner_t capability_client_consumer_owner(const char *consumer_id);
uint32_t hk_input_state(void);

uint8_t micropython_runtime_interrupt_pending(void);
void micropython_runtime_vm_hook(void);

#endif
