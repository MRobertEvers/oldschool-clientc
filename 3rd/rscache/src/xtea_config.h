#ifndef RSCACHE_XTEA_CONFIG_H
#define RSCACHE_XTEA_CONFIG_H

#include <stdint.h>

int
RSCache_XteaConfigLoadKeys(const char* filename);

int32_t*
RSCache_XteaConfigFindKey(
    int archive,
    int group);

void
RSCache_XteaConfigCleanupKeys(void);

#endif
