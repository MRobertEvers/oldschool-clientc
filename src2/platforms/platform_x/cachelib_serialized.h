#ifndef PLATFORM_X_CACHELIB_SERIALIZED_H
#define PLATFORM_X_CACHELIB_SERIALIZED_H

#ifdef __cplusplus
extern "C" {
#endif

struct RSCacheDat2Disk_Archive;
struct RSCacheDat1Disk_Archive;

/**
 * Compute the size in bytes needed to serialize a CacheArchive.
 * @return Size in bytes, or -1 on error.
 */
int
cachelib_cache_archive_serialized_size(const struct RSCacheDat2Disk_Archive* archive);

/**
 * Serialize a CacheArchive into a pre-allocated buffer.
 * @param archive  Archive to serialize
 * @param buffer   Output buffer (must have at least cachelib_cache_archive_serialized_size(archive) bytes)
 * @param size     Size of buffer in bytes
 * @return Number of bytes written, or -1 on error
 */
int
cachelib_cache_archive_serialize_to_buffer(
    const struct RSCacheDat2Disk_Archive* archive,
    void* buffer,
    int size);

/**
 * Deserialize a CacheArchive from a byte buffer.
 * Caller must free the result with cachelib_RSCacheDat2Disk_ArchiveFree().
 * @param buffer  Serialized bytes
 * @param size    Size of buffer in bytes
 * @return Newly allocated archive, or NULL on error
 */
struct RSCacheDat2Disk_Archive*
cachelib_cache_archive_deserialize(
    const void* buffer,
    int size);

/**
 * Free a CacheArchive allocated by cachelib_cache_archive_deserialize.
 * Safe to call with NULL.
 */
void
cachelib_RSCacheDat2Disk_ArchiveFree(struct RSCacheDat2Disk_Archive* archive);

/**
 * Compute the size in bytes needed to serialize a CacheDatArchive.
 * @return Size in bytes, or -1 on error.
 */
int
cachelib_cache_dat_archive_serialized_size(const struct RSCacheDat1Disk_Archive* archive);

/**
 * Serialize a CacheDatArchive into a pre-allocated buffer.
 * @param archive  Archive to serialize
 * @param buffer   Output buffer (must have at least cachelib_cache_dat_archive_serialized_size(archive) bytes)
 * @param size     Size of buffer in bytes
 * @return Number of bytes written, or -1 on error
 */
int
cachelib_cache_dat_archive_serialize_to_buffer(
    const struct RSCacheDat1Disk_Archive* archive,
    void* buffer,
    int size);

/**
 * Deserialize a CacheDatArchive from a byte buffer.
 * Caller must free the result with cachelib_RSCacheDat1Disk_ArchiveFree().
 * @param buffer  Serialized bytes
 * @param size    Size of buffer in bytes
 * @return Newly allocated archive, or NULL on error
 */
struct RSCacheDat1Disk_Archive*
cachelib_cache_dat_archive_deserialize(
    const void* buffer,
    int size);

/**
 * Free a CacheDatArchive allocated by cachelib_cache_dat_archive_deserialize.
 * Safe to call with NULL.
 */
void
cachelib_RSCacheDat1Disk_ArchiveFree(struct RSCacheDat1Disk_Archive* archive);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_X_CACHELIB_SERIALIZED_H */
