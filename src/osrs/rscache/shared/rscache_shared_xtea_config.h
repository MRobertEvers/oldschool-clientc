#ifndef RSCACHE_RSCACHESHARED_XTEACONFIG_H
#define RSCACHE_RSCACHESHARED_XTEACONFIG_H

#include <stdint.h>

// Load XTEA keys from a JSON file
// Returns number of keys loaded or -1 on error
int RSCacheShared_XteaConfigLoadKeys(const char* filename);

/**
 * The archive should always be 5. The only index with encrypted entries is the map index. Index 5.
 *
 * Group X refers the the Xth archive in the Map Index.
 */
int32_t* RSCacheShared_XteaConfigFindKey(int archive, int group);

// Clean up XTEA key storage
void RSCacheShared_XteaConfigCleanupKeys(void);

#endif