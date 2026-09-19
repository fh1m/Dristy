#ifndef TEST_K210_ATOMIC_H
#define TEST_K210_ATOMIC_H

typedef struct
{
    int lock;
} spinlock_t;

#define SPINLOCK_INIT {0}

extern unsigned int g_test_lock_depth;
extern unsigned int g_test_lock_calls;
extern unsigned int g_test_unlock_calls;
extern unsigned int g_test_lock_violations;

static inline void spinlock_lock(spinlock_t *lock)
{
    if(!lock || lock->lock || g_test_lock_depth)
        g_test_lock_violations++;
    if(lock)
        lock->lock = 1;
    g_test_lock_depth++;
    g_test_lock_calls++;
}

static inline void spinlock_unlock(spinlock_t *lock)
{
    if(!lock || !lock->lock || g_test_lock_depth != 1U)
        g_test_lock_violations++;
    if(lock)
        lock->lock = 0;
    if(g_test_lock_depth)
        g_test_lock_depth--;
    g_test_unlock_calls++;
}

#endif
