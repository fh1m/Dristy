#ifndef HACKYLENS_MICROPYTHON_MPCONFIGPORT_H
#define HACKYLENS_MICROPYTHON_MPCONFIGPORT_H

#include <port/mpconfigport_common.h>

/* Keep the first firmware integration deliberately small and auditable. */
#define MICROPY_CONFIG_ROM_LEVEL MICROPY_CONFIG_ROM_LEVEL_MINIMUM
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_PLATFORM "hackylens-k210"
#define MICROPY_PY_BUILTINS_MIN_MAX (1)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_STACK_CHECK_MARGIN (1024)
#define MICROPY_ENABLE_SCHEDULER (0)
#define MICROPY_PY_THREAD (0)
#define MICROPY_PERSISTENT_CODE_LOAD (0)
#define MICROPY_READER_VFS (0)

/* Implemented by the HackyLens core-1 runtime port.  The VM and iterator
 * hooks may raise KeyboardInterrupt; the GC hook is heartbeat-only and must
 * never raise.  The pinned runtime.c patch invokes the iterator hook from
 * native consumers such as sum(), min/max() and collection constructors. */
void micropython_runtime_vm_hook(void);
void micropython_runtime_gc_hook(void);

#define MICROPY_VM_HOOK_LOOP micropython_runtime_vm_hook();
#define MICROPY_VM_HOOK_RETURN micropython_runtime_vm_hook();
#define MICROPY_PORT_ITERNEXT_HOOK micropython_runtime_vm_hook();
#define MICROPY_GC_HOOK_LOOP(i) do { \
    if(((i) & 0x3fU) == 0U) \
        micropython_runtime_gc_hook(); \
} while(0)

#endif
