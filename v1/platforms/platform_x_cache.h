#ifndef PLATFORM_X_CACHE_H
#define PLATFORM_X_CACHE_H

#include "../libtorirs.h"

#include <stdint.h>

struct LibToriPlatformX_Cache;
struct LibToriRS_IOQueue;

#define CACHE_MODE_DAT1 0
#define CACHE_MODE_DAT2 1

struct LibToriPlatformX_Cache*
LibToriPlatformX_CacheNew(
    struct LibToriRS_Instance* instance,
    int mode,
    char const* directory);

void
LibToriPlatformX_CacheFree(struct LibToriPlatformX_Cache* cache);

int
LibToriPlatformX_GetMode(struct LibToriPlatformX_Cache* cache);

int
LibToriPlatformX_CacheLoadIO(
    struct LibToriPlatformX_Cache* cache,
    struct LibToriRS_IOQueue* io_queue);

#endif