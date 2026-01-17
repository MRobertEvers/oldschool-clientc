#ifndef CACHE_INET_INDEXEDDB_H
#define CACHE_INET_INDEXEDDB_H

#ifdef __EMSCRIPTEN__

#include <stdint.h>

// IndexedDB cache for archive data
// Returns 1 if found, 0 if not found, -1 on error
int indexeddb_get_archive(int table_id, int archive_id, char** data_out, int* size_out);

// Store archive in IndexedDB
// Returns 0 on success, -1 on error
int indexeddb_put_archive(int table_id, int archive_id, const char* data, int size);

// Initialize IndexedDB (called once at startup)
void indexeddb_init(void);

#endif // __EMSCRIPTEN__

#endif // CACHE_INET_INDEXEDDB_H

