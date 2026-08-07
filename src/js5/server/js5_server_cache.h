#ifndef SRC_JS5_SERVER_JS5_SERVER_CACHE_H
#define SRC_JS5_SERVER_JS5_SERVER_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Js5ServerCache;

enum Js5ServerCacheResult
{
    JS5_SERVER_CACHE_ERROR = -1,
    JS5_SERVER_CACHE_NOT_FOUND = 0,
    JS5_SERVER_CACHE_OK = 1,
};

struct Js5ServerBlob
{
    uint8_t* data;
    size_t size;
};

/*
 * Open and validate a complete cache for read-only serving. Startup fails when
 * DAT2/idx255 is absent, no reference tables exist, or any physical idxN has
 * no decodable reference table. The generated 255/255 master is retained in
 * memory for the lifetime of the store.
 */
struct Js5ServerCache*
Js5ServerCacheOpen(const char* cache_dir);

void
Js5ServerCacheFree(struct Js5ServerCache* cache);

/* Load an owned exact container. Local u16/u32 version trailers are removed. */
enum Js5ServerCacheResult
Js5ServerCacheLoad(
    struct Js5ServerCache* cache,
    uint8_t archive,
    uint16_t group,
    struct Js5ServerBlob* blob);

void
Js5ServerBlobFree(struct Js5ServerBlob* blob);

/*
 * Convenience helper used by tests and the legacy mock-game integration.
 * It allocates the complete 512-byte-block-framed wire response. The caller
 * owns `*out` on success.
 */
enum Js5ServerCacheResult
Js5ServerCacheBuildResponse(
    struct Js5ServerCache* cache,
    uint8_t archive,
    uint16_t group,
    uint8_t** out,
    size_t* out_size);

size_t
Js5ServerCacheMasterSize(const struct Js5ServerCache* cache);

#ifdef __cplusplus
}
#endif

#endif
