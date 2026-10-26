#ifndef RSCACHE_XTEA_CONFIG_H
#define RSCACHE_XTEA_CONFIG_H

#include <stdint.h>

int
RSCache_XteaConfigLoadKeys(const char* filename);

int32_t*
RSCache_XteaConfigFindKey(
    int archive,
    int group);

/* A map square's loc key as a server sent it (REBUILD_* below rev 237), for a
 * square the loaded keys lack. A loaded key wins: it is the one THIS cache's
 * archive was encrypted with, and Jagex re-keys a square whenever its locs
 * change, so a server older than the cache sends a key that no longer opens
 * it. Keyed by square until BindSquare names its group. */
void
RSCache_XteaConfigPutSquareKey(
    int map_x,
    int map_z,
    const int32_t key[4]);

/* Name the (archive, group) a square's loc key decrypts, once the reference
 * table has resolved it, so FindKey answers for it. 0: no key for the square. */
int
RSCache_XteaConfigBindSquare(
    int map_x,
    int map_z,
    int archive,
    int group);

void
RSCache_XteaConfigCleanupKeys(void);

#endif
