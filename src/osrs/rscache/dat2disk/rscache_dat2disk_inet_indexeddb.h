#ifndef RSCACHE_RSCACHEDAT2DISK_INETINDEXEDDB_H
#define RSCACHE_RSCACHEDAT2DISK_INETINDEXEDDB_H

#ifdef __EMSCRIPTEN__

#include <stdint.h>

// IndexedDB cache for archive data
// Returns 1 if found, 0 if not found, -1 on error
int RSCacheDat2Disk_InetIndexeddbGetArchive(int table_id, int archive_id, char** data_out, int* size_out);

// Store archive in IndexedDB
// Returns 0 on success, -1 on error
int RSCacheDat2Disk_InetIndexeddbPutArchive(int table_id, int archive_id, const char* data, int size);

// Initialize IndexedDB (called once at startup)
void RSCacheDat2Disk_InetIndexeddbInit(void);

#endif // __EMSCRIPTEN__

#endif // CACHE_INET_INDEXEDDB_H

