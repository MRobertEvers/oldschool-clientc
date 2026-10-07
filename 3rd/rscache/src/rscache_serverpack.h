#ifndef RSCACHE_SERVERPACK_H
#define RSCACHE_SERVERPACK_H

/*
 * The server pack: `<content>/server/pack`, the one input the game server reads
 * its config records from.
 *
 * ## What it holds
 *
 * A dat2 directory (main_file_cache.dat2 + idx<n>) with no reference table. Three
 * kinds of archive live in it, each wrapped in the same header:
 *
 *   group = config kind          archive = record id        one record's server
 *                                                           band (rscache_band.h)
 *   group = config kind + 64     archive = record id >> 8   up to 256 records'
 *                                                           CLIENT encodings, the
 *                                                           bytes the client codec
 *                                                           of that type decodes
 *   group = 128 + n              archive = 0                a server-only name
 *                                                           table (stat, category)
 *   group = 193                  archive = config kind      the ids of the records
 *                                                           that are ALSO in the
 *                                                           client cache (a server-
 *                                                           only enum is not): the
 *                                                           client's array bounds
 *   group = 192                  archive = config kind      the type's `[default]`
 *                                                           band: what a record is
 *                                                           before any block states
 *                                                           it (an empty band when
 *                                                           the tree states none)
 *
 * The client encodings are here, rather than read out of a client cache, because
 * the server's records are not the client's: a server-only enum or dbrow has a
 * client-format body and no place in any client cache, and a cache baked from an
 * older tree would hand the server last week's npc. The pack is written from the
 * same merge of the same tree as the band beside it, so the two halves of a
 * record always come from one source.
 *
 * ## The header
 *
 * There is no idx255, so no per-archive CRC or version the way a client cache
 * has. Every archive carries its own:
 *
 *     u8  'S'  u8  'P'
 *     u8  version        RSCACHE_SERVERPACK_VERSION; a reader refuses any other
 *     u8  kind           enum RSCache_ServerPackKind
 *     u32 crc32(payload)
 *     ..  payload
 *
 * One implementation of it, here, for both ends: cachepack writes through it and
 * the game server reads through it. The two used to restate the constants on
 * each side and agree only through tests.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RSCACHE_SERVERPACK_VERSION 2

enum RSCache_ServerPackKind
{
    /** `<opcode:u8> <payload>` until a zero opcode — one record's server band. */
    RSCACHE_SERVERPACK_KIND_BAND = 1,
    /** `u2 count`, then `u4 id` + NUL-terminated name per entry. */
    RSCACHE_SERVERPACK_KIND_NAMES = 2,
    /** `u2 count`, then per record `u4 id`, `u4 size`, `size` bytes, ascending by
     *  id — the client codec's encoding of each. */
    RSCACHE_SERVERPACK_KIND_RECORDS = 3,
    /** `u4 count`, then `u4 id` each, ascending. */
    RSCACHE_SERVERPACK_KIND_IDS = 4,
};

enum
{
    RSCACHE_SERVERPACK_HEADER = 8,
    /** Client-record groups sit this far above their config kind. */
    RSCACHE_SERVERPACK_RECORDS_GROUP_OFFSET = 64,
    /** log2 of the records one client-record archive holds. */
    RSCACHE_SERVERPACK_RECORDS_SHIFT = 8,
    /** Server-only name tables start here. */
    RSCACHE_SERVERPACK_NAMES_GROUP_BASE = 128,
    /** Each type's `[default]` band, at archive = config kind. */
    RSCACHE_SERVERPACK_DEFAULTS_GROUP = 192,
    /** Each type's client-routed record ids, at archive = config kind. */
    RSCACHE_SERVERPACK_CLIENT_IDS_GROUP = 193,
};

/** The group holding config kind `config_kind`'s client records. */
int
RSCache_ServerPackRecordsGroup(int config_kind);

/** The archive in that group holding record `id`. */
int
RSCache_ServerPackRecordsArchive(int id);

/* ---- framing ------------------------------------------------------------- */

/** Wrap `payload` in the header. Returns bytes written; `out_capacity` must be at
 *  least `payload_size + RSCACHE_SERVERPACK_HEADER` (asserted). */
uint32_t
RSCache_ServerPackFrame(
    enum RSCache_ServerPackKind kind,
    const uint8_t* payload,
    uint32_t payload_size,
    uint8_t* out,
    uint32_t out_capacity);

/**
 * Validate a header and point at the payload inside `data`. Returns 1 when the
 * magic, the version and the CRC hold and the kind is `kind`; 0 otherwise — a
 * stale or truncated pack, or an archive of another kind.
 */
int
RSCache_ServerPackUnframe(
    const uint8_t* data,
    uint32_t size,
    enum RSCache_ServerPackKind kind,
    const uint8_t** out_payload,
    uint32_t* out_payload_size);

/* ---- name tables --------------------------------------------------------- */

/** Encode an `id -> name` table. Sparse: the id is per entry. Returns bytes
 *  written, or 0 when `out_capacity` is too small. */
uint32_t
RSCache_ServerPackNamesEncode(
    const int* ids,
    const char* const* names,
    int count,
    uint8_t* out,
    uint32_t out_capacity);

/** Read one back; names point into `data`. Fills up to `max`. Returns the entry
 *  count, or -1 when the table is malformed. */
int
RSCache_ServerPackNamesDecode(
    const uint8_t* data,
    uint32_t size,
    int* out_ids,
    const char** out_names,
    int max);

/* ---- client records ------------------------------------------------------ */

/** Bytes RSCache_ServerPackRecordsEncode needs for `count` records totalling
 *  `body_bytes`. */
uint32_t
RSCache_ServerPackRecordsBound(
    int count,
    uint32_t body_bytes);

/** Encode `count` records (ids ascending, all in one archive). Returns bytes
 *  written; capacity must be RSCache_ServerPackRecordsBound (asserted). */
uint32_t
RSCache_ServerPackRecordsEncode(
    const int* ids,
    const uint8_t* const* bodies,
    const uint32_t* sizes,
    int count,
    uint8_t* out,
    uint32_t out_capacity);

/** A cursor over a records payload. */
struct RSCache_ServerPackRecords
{
    const uint8_t* data;
    uint32_t size;
    uint32_t position;
    int remaining;
    /** The id last returned, -1 before the first. */
    int last_id;
};

/** Start reading a records payload. Returns 0 when it is too short to hold its
 *  count. */
int
RSCache_ServerPackRecordsBegin(
    struct RSCache_ServerPackRecords* cursor,
    const uint8_t* payload,
    uint32_t payload_size);

/** The next record: 1 with `*out_id`, `*out_body`, `*out_size` set (the body
 *  points into the payload), 0 at the end, -1 when the payload is malformed
 *  (a record running past the end, or ids not ascending). */
int
RSCache_ServerPackRecordsNext(
    struct RSCache_ServerPackRecords* cursor,
    int* out_id,
    const uint8_t** out_body,
    uint32_t* out_size);

/* ---- id lists ------------------------------------------------------------ */

/** Encode `count` ascending ids. Returns bytes written (4 + 4 * count); the
 *  capacity must hold that (asserted). */
uint32_t
RSCache_ServerPackIdsEncode(
    const int* ids,
    int count,
    uint8_t* out,
    uint32_t out_capacity);

/** The count an ids payload states, or -1 when it is malformed (its length is
 *  not 4 + 4 * count, or the ids do not ascend). `out_ids` (may be NULL) gets up
 *  to `max` of them. */
int
RSCache_ServerPackIdsDecode(
    const uint8_t* payload,
    uint32_t size,
    int* out_ids,
    int max);

/* ---- freshness --------------------------------------------------------------- */

/*
 * The pack is a derived file, and a server that boots on one older than its tree
 * is running content nobody can see in the text. So the pack carries a fingerprint
 * of everything it was written from, and both ends compute it with this one
 * function: cachepack writes it (and skips the rebuild when it already matches),
 * the server recomputes it at boot and refuses a pack that does not match.
 *
 * The inputs are every file's path and contents (not its mtime: a pack shipped in
 * a zip or a fresh checkout has new mtimes and the same bytes) under:
 *   configs/  fields/  pack/  interfaces/ (its .compack files)
 *   server/scripts/  -- the config files only (a config type's extension,
 *                       .constant, .compack), so a script edit does not stale it
 *   ported/<lane>/configs/  for each included lane
 *   ported/<any>/pack/      every lane's ledger names records
 * hashed in sorted path order with the pack format version, so the answer does
 * not depend on directory order.
 */
uint64_t
RSCache_ServerPackFingerprint(
    const char* srcdir,
    const char* const* lanes,
    int lane_count);

enum
{
    RSCACHE_SERVERPACK_STAMP_LANES = 8,
};

/** What a pack was written from: its tree's fingerprint, the included lanes, and
 *  the writer's own identity (cachepack's, so a new cachepack rewrites). */
struct RSCache_ServerPackStamp
{
    uint64_t fingerprint;
    uint64_t writer;
    char lanes[RSCACHE_SERVERPACK_STAMP_LANES][64];
    int lane_count;
};

/** Write `<dir>/stamp.txt`. Returns 1, or 0 when the file cannot be written. */
int
RSCache_ServerPackStampWrite(
    const char* dir,
    const struct RSCache_ServerPackStamp* stamp);

/** Read `<dir>/stamp.txt`. Returns 1, or 0 when it is missing or malformed. */
int
RSCache_ServerPackStampRead(
    const char* dir,
    struct RSCache_ServerPackStamp* stamp);

/* ---- reading the directory ------------------------------------------------ */

struct RSCache_ServerPackIdx
{
    int group;
    FILE* file;
    int entries;
};

struct RSCache_ServerPack
{
    char dir[1024];
    FILE* dat2;
    /** idx files, opened on first use per group. */
    struct RSCache_ServerPackIdx idx[64];
    int idx_count;
};

/** Open the pack at `dir` (the `server/pack` directory itself). Returns 1, or 0
 *  when it has no main_file_cache.dat2. */
int
RSCache_ServerPackOpen(
    struct RSCache_ServerPack* pack,
    const char* dir);

/** Close it. Accepts a pack that never opened. */
void
RSCache_ServerPackClose(struct RSCache_ServerPack* pack);

/** Entries in `group`'s idx: an exclusive bound on its archive ids. 0 when the
 *  group has no idx. */
int
RSCache_ServerPackEntryCount(
    struct RSCache_ServerPack* pack,
    int group);

/** RSCache_ServerPackRead results at or below zero. */
enum
{
    /** No archive at this (group, archive): an ordinary sparse gap. */
    RSCACHE_SERVERPACK_ABSENT = 0,
    /** Indexed but does not validate: bad container, magic, version, kind or
     *  CRC. A stale or truncated pack. */
    RSCACHE_SERVERPACK_INVALID = -1,
};

/**
 * Read archive `(group, archive)` and validate it down to its payload, which
 * must be of `kind`. On success returns 1 and hands back a heap buffer in
 * `*out_owned` (free it) with the payload at `*out_payload`, `*out_size` bytes.
 * Otherwise returns RSCACHE_SERVERPACK_ABSENT or RSCACHE_SERVERPACK_INVALID and
 * `*out_owned` is NULL.
 */
int
RSCache_ServerPackRead(
    struct RSCache_ServerPack* pack,
    int group,
    int archive,
    enum RSCache_ServerPackKind kind,
    void** out_owned,
    const uint8_t** out_payload,
    uint32_t* out_size);

#endif
