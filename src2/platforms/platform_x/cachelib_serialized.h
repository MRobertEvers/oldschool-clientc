#ifndef PLATFORM_X_CACHELIB_SERIALIZED_H
#define PLATFORM_X_CACHELIB_SERIALIZED_H

#ifdef __cplusplus
extern "C" {
#endif

struct CacheArchive;
struct CacheDatArchive;

/**
 * Compute the size in bytes needed to serialize a CacheArchive.
 * @return Size in bytes, or -1 on error.
 */
int
cachelib_cache_archive_serialized_size(const struct CacheArchive* archive);

/**
 * Serialize a CacheArchive into a pre-allocated buffer.
 * @param archive  Archive to serialize
 * @param buffer   Output buffer (must have at least cachelib_cache_archive_serialized_size(archive) bytes)
 * @param size     Size of buffer in bytes
 * @return Number of bytes written, or -1 on error
 */
int
cachelib_cache_archive_serialize_to_buffer(
    const struct CacheArchive* archive,
    void* buffer,
    int size);

/**
 * Deserialize a CacheArchive from a byte buffer.
 * Caller must free the result with cachelib_cache_archive_free().
 * @param buffer  Serialized bytes
 * @param size    Size of buffer in bytes
 * @return Newly allocated archive, or NULL on error
 */
struct CacheArchive*
cachelib_cache_archive_deserialize(
    const void* buffer,
    int size);

/**
 * Free a CacheArchive allocated by cachelib_cache_archive_deserialize.
 * Safe to call with NULL.
 */
void
cachelib_cache_archive_free(struct CacheArchive* archive);

/**
 * Compute the size in bytes needed to serialize a CacheDatArchive.
 * @return Size in bytes, or -1 on error.
 */
int
cachelib_cache_dat_archive_serialized_size(const struct CacheDatArchive* archive);

/**
 * Serialize a CacheDatArchive into a pre-allocated buffer.
 * @param archive  Archive to serialize
 * @param buffer   Output buffer (must have at least cachelib_cache_dat_archive_serialized_size(archive) bytes)
 * @param size     Size of buffer in bytes
 * @return Number of bytes written, or -1 on error
 */
int
cachelib_cache_dat_archive_serialize_to_buffer(
    const struct CacheDatArchive* archive,
    void* buffer,
    int size);

/**
 * Deserialize a CacheDatArchive from a byte buffer.
 * Caller must free the result with cachelib_cache_dat_archive_free().
 * @param buffer  Serialized bytes
 * @param size    Size of buffer in bytes
 * @return Newly allocated archive, or NULL on error
 */
struct CacheDatArchive*
cachelib_cache_dat_archive_deserialize(
    const void* buffer,
    int size);

/**
 * Free a CacheDatArchive allocated by cachelib_cache_dat_archive_deserialize.
 * Safe to call with NULL.
 */
void
cachelib_cache_dat_archive_free(struct CacheDatArchive* archive);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_X_CACHELIB_SERIALIZED_H */
