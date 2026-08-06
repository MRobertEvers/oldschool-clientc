#ifndef SRC_PLATFORM_PLATFORM_X_IO_JS5_CACHE_H
#define SRC_PLATFORM_PLATFORM_X_IO_JS5_CACHE_H

#include "js5/js5.h"

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Dat2Disk;
struct PlatformXIOJs5Cache;

struct PlatformXIOJs5Cache*
PlatformXIOJs5Cache_New(
    struct RSCache_Dat2Disk* disk,
    const struct Js5Config* config);

void
PlatformXIOJs5Cache_Free(struct PlatformXIOJs5Cache* cache);

int
PlatformXIOJs5Cache_Tick(
    struct PlatformXIOJs5Cache* cache,
    uint64_t now_ms);

enum Js5RequestResult
PlatformXIOJs5Cache_RequestGroup(
    struct PlatformXIOJs5Cache* cache,
    int archive,
    int group);

bool
PlatformXIOJs5Cache_GroupReady(
    const struct PlatformXIOJs5Cache* cache,
    int archive,
    int group);

bool
PlatformXIOJs5Cache_GroupFailed(
    const struct PlatformXIOJs5Cache* cache,
    int archive,
    int group);

bool
PlatformXIOJs5Cache_MetadataReady(const struct PlatformXIOJs5Cache* cache);

enum Js5Error
PlatformXIOJs5Cache_LastError(const struct PlatformXIOJs5Cache* cache);

void
PlatformXIOJs5Cache_GetProgress(
    const struct PlatformXIOJs5Cache* cache,
    struct Js5Progress* progress);

#endif
