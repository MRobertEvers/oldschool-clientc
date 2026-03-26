#ifndef LUAJS_ARCHIVES_H
#define LUAJS_ARCHIVES_H

#include "osrs/rscache/cache_dat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Deserialize a CacheDatArchive from a byte buffer produced by
 * luajs_CacheDatArchive_serialize_to_buffer.
 * Caller must free the result with luajs_CacheDatArchive_free().
 *
 * @param buffer  Serialized bytes (e.g. HEAPU8 view in JS)
 * @param size    Size of buffer in bytes
 * @return  Newly allocated archive, or NULL on error
 */
struct CacheDatArchive*
luajs_CacheDatArchive_deserialize(
    const void* buffer,
    int size);

/**
 * Compute the size in bytes needed to serialize the archive.
 */
int
luajs_CacheDatArchive_serialized_size(const struct CacheDatArchive* archive);

/**
 * Serialize archive into a pre-allocated buffer.
 *
 * @param archive  Archive to serialize
 * @param buffer   Output buffer (must have at least luajs_CacheDatArchive_serialized_size(archive)
 * bytes)
 * @param size    Size of buffer
 * @return  Number of bytes written, or -1 on error
 */
int
luajs_CacheDatArchive_serialize_to_buffer(
    const struct CacheDatArchive* archive,
    void* buffer,
    int size);

/**
 * Free a CacheDatArchive allocated by luajs_CacheDatArchive_deserialize.
 * Safe to call with NULL.
 */
void
luajs_CacheDatArchive_free(struct CacheDatArchive* archive);

#ifdef __cplusplus
}
#endif

#endif /* LUAJS_ARCHIVES_H */
