#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlatformMemoryInfo
{
    size_t heap_used;
    size_t heap_total;
    size_t heap_peak;
};

bool
platform_get_memory_info(struct PlatformMemoryInfo* info);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_MEMORY_H */
