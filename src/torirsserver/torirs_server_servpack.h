#ifndef TORIRSSERVER_SERVPACK_H
#define TORIRSSERVER_SERVPACK_H

/*
 * The game server's door into `<content>/server/pack`.
 *
 * The container is the rscache library's (`rscache_serverpack.h`): framing,
 * version, CRC, the client-record archives and their cursor all live there, and
 * cachepack writes through the same code. This file used to restate the header
 * constants (at version 1) and parse the container itself, and the two ends
 * agreed only through tests. What is left here is the server's side of the
 * contract:
 *
 *  - where the pack is (`<content>/server/pack`),
 *  - that it is REQUIRED: the server reads its npc and loc records from nothing
 *    else, so a missing pack or an archive that does not validate is a boot
 *    failure naming the fix, never a fallback to config text,
 *  - and the walks the loaders share: every client record of a config kind,
 *    every band archive of one, and a type's `[default]` band.
 */

#include "rscache_serverpack.h"

#include <stddef.h>
#include <stdint.h>

/** What a boot failure tells the operator to run. */
#define TORIRSSERVER_SERVPACK_FIX "make -C src torirsserver-servpack (cachepack pack --src <tree> --server-only)"

/* A type's `[default]` band: RSCACHE_SERVERPACK_DEFAULTS_GROUP, archive = the
 * config kind (rscache_serverpack.h). Every npc def is seeded from it, and an
 * npc with no def reads it directly (`ToriRSServer_ContentNpcDefault`). */
enum
{
    TORIRSSERVER_SERVPACK_DEFAULTS_GROUP = RSCACHE_SERVERPACK_DEFAULTS_GROUP,
};

/**
 * Where the pack is, in order: `TORIRSSERVER_PACK_DIR` when set; else
 * `<cache_dir>.serverpack` when it exists -- a lane composition's own pack, written
 * beside the cache it pairs with (`make torirsserver-servpack SERVPACK_LANES=...
 * SERVPACK_OUT=<cache>.serverpack`, which run-live.sh does); else
 * `<content_dir>/server/pack`. `cache_dir` may be NULL.
 */
void
ToriRSServer_ServPackDir(
    const char* content_dir,
    const char* cache_dir,
    char* out,
    size_t out_size);

/**
 * Open the pack at `pack_dir`. Returns 1, or 0 after printing which directory
 * has no pack and the command that builds one.
 */
int
ToriRSServer_ServPackOpen(
    struct RSCache_ServerPack* pack,
    const char* pack_dir);

/**
 * Is the pack at `pack_dir` the one its tree would write now? Its stamp's
 * fingerprint (rscache_serverpack.h) is recomputed over `content_dir` with the
 * lanes it names. 1 when it matches; 0 after naming the fix when it does not or
 * there is no stamp -- unless TORIRSSERVER_ALLOW_STALE_PACK=1, the caller saying
 * in as many words that stale is what it wants (run-live.sh --skip-checks).
 */
int
ToriRSServer_ServPackFresh(
    const char* pack_dir,
    const char* content_dir);

/** Every client record of one config kind, held in memory: `ids[i]` ascending,
 *  `files[i]` / `sizes[i]` its client-codec bytes. The shape a cache config
 *  archive's file list had, so a loader swaps one for the other. */
struct ToriRSServerKindRecords
{
    int count;
    int capacity;
    int* ids;
    uint8_t** files;
    uint32_t* sizes;
};

/**
 * Load every client record of `config_kind`. Returns 1, or 0 after a report when
 * an archive does not validate (the caller must refuse to boot). A kind with no
 * records loads as count 0.
 */
int
ToriRSServer_ServPackKindLoad(
    struct RSCache_ServerPack* pack,
    int config_kind,
    struct ToriRSServerKindRecords* out);

/** The id of the highest record plus one (0 when empty). */
int
ToriRSServer_ServPackKindIdBound(const struct ToriRSServerKindRecords* records);

void
ToriRSServer_ServPackKindFree(struct ToriRSServerKindRecords* records);

/**
 * One past the highest id of `config_kind` that the CLIENT cache also holds
 * (RSCACHE_SERVERPACK_CLIENT_IDS_GROUP): the size of the client's own array for
 * that type, which a server-only record (an allocated varp) is past. Returns -1
 * after a report when the pack has no such archive or it does not validate.
 */
int
ToriRSServer_ServPackClientIdBound(
    struct RSCache_ServerPack* pack,
    int config_kind);

/** Called once per record by `ToriRSServer_ServPackEachRecord`, ids ascending.
 *  `body` points into a buffer freed after the call. */
typedef void (*ToriRSServerServPackRecordFn)(
    void* context,
    int id,
    const uint8_t* body,
    uint32_t size);

/**
 * Every client record of config kind `config_kind` (group `config_kind + 64`).
 *
 * Returns how many records were visited, or -1 after a report when an archive
 * is indexed but does not validate or its payload is malformed: a stale or
 * truncated pack, which the caller must refuse rather than load half of.
 */
int
ToriRSServer_ServPackEachRecord(
    struct RSCache_ServerPack* pack,
    int config_kind,
    ToriRSServerServPackRecordFn visit,
    void* context);

/** Called once per band archive by `ToriRSServer_ServPackEachBand`. */
typedef void (*ToriRSServerServPackBandFn)(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size);

/**
 * Every band archive of config kind `config_kind` (group `config_kind`, archive
 * = record id), ids ascending. Returns how many were visited, or -1 after a
 * report when one does not validate.
 */
int
ToriRSServer_ServPackEachBand(
    struct RSCache_ServerPack* pack,
    int config_kind,
    ToriRSServerServPackBandFn visit,
    void* context);

/**
 * The type's `[default]` band. Returns 1 with a heap buffer in `*out_owned`
 * (free it) and the band at `*out_band`; 0 after a report when the pack holds
 * none or it does not validate.
 */
int
ToriRSServer_ServPackDefaults(
    struct RSCache_ServerPack* pack,
    int config_kind,
    void** out_owned,
    const uint8_t** out_band,
    uint32_t* out_size);

#endif
