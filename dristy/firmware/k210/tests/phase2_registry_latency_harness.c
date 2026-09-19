#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "capability_fake_provider.h"
#include "../firmware/src/capabilities/capability_provider.h"

#define SAMPLE_COUNT 101U
#define ITERATIONS_PER_SAMPLE 1000U
#define REGISTRY_P99_LIMIT_NS 100000ULL

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if(!(expression))                                                     \
        {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #expression);                                             \
            exit(1);                                                          \
        }                                                                     \
    } while(0)

static uint64_t monotonic_ns(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    CHECK(QueryPerformanceFrequency(&frequency) != 0);
    CHECK(QueryPerformanceCounter(&counter) != 0);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000000ULL +
           (uint64_t)((counter.QuadPart % frequency.QuadPart) * 1000000000ULL /
                      frequency.QuadPart);
#elif defined(CLOCK_MONOTONIC)
    struct timespec value;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#else
    struct timespec value;
    CHECK(timespec_get(&value, TIME_UTC) == TIME_UTC);
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
#endif
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t first = *(const uint64_t *)left;
    uint64_t second = *(const uint64_t *)right;
    return first < second ? -1 : first > second ? 1 : 0;
}

int main(void)
{
    capability_fake_provider_t fake;
    hk_capability_core_t core;
    hk_capability_info_t info;
    hk_capability_provider_t provider;
    const hk_capability_provider_t *providers[1];
    hk_capability_grant_t grant;
    hk_capability_request_t request;
    hk_owner_t owner;
    hk_lease_t lease;
    uint64_t samples[SAMPLE_COUNT];
    volatile void *context = NULL;

    memset(&core, 0, sizeof(core));
    memset(&info, 0, sizeof(info));
    memset(&provider, 0, sizeof(provider));
    memset(&grant, 0, sizeof(grant));
    memset(&request, 0, sizeof(request));
    capability_fake_provider_reset(&fake);
    info.struct_size = sizeof(info);
    info.struct_version = HK_CAPABILITY_INFO_VERSION;
    info.id = 0x00010001U;
    info.version = (hk_version_t){0U, 1U, 0U, 0U};
    info.features = 1U;
    info.flags = HK_CAPABILITY_FLAG_SHARED;
    info.affinity_core = HK_CAPABILITY_CORE_ANY;
    provider.context = &fake;
    provider.acquire = capability_fake_acquire;
    provider.cleanup = capability_fake_cleanup;
    provider.cleanup_dispatch = capability_fake_cleanup_dispatch;
    provider.recover = capability_fake_recover;
    provider.max_leases = 1U;
    providers[0] = &provider;
    request.struct_size = sizeof(request);
    request.struct_version = HK_CAPABILITY_REQUEST_VERSION;
    request.id = info.id;
    request.minimum = (hk_version_t){0U, 1U, 0U, 0U};
    request.maximum_exclusive = (hk_version_t){0U, 2U, 0U, 0U};
    request.required_features = 1U;
    grant.request = request;

    CHECK(hk_capability_core_init(&core, &info, providers, 1U) == HK_OK);
    CHECK(hk_capability_core_owner_open(&core, &grant, 1U, &owner) == HK_OK);
    CHECK(hk_capability_core_acquire(
        &core, owner, &request, info.id, 0U, &lease) == HK_OK);

    for(uint32_t sample = 0U; sample < SAMPLE_COUNT; sample++)
    {
        uint64_t start = monotonic_ns();
        for(uint32_t iteration = 0U; iteration < ITERATIONS_PER_SAMPLE;
            iteration++)
        {
            void *resolved = NULL;
            CHECK(hk_capability_core_validate_lease(
                &core, owner, &lease, info.id, 0U, &resolved) == HK_OK);
            context = resolved;
        }
        samples[sample] = (monotonic_ns() - start) / ITERATIONS_PER_SAMPLE;
    }
    CHECK(context == &fake);
    qsort(samples, SAMPLE_COUNT, sizeof(samples[0]), compare_u64);
    CHECK(samples[99U] <= REGISTRY_P99_LIMIT_NS);
    printf("PHASE2_REGISTRY_LATENCY_OK host_p99_ns=%llu limit_us=100\n",
           (unsigned long long)samples[99U]);
    return 0;
}
