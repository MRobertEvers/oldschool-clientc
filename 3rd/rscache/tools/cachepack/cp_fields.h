#ifndef RSCACHE_TOOLS_CACHEPACK_CP_FIELDS_H
#define RSCACHE_TOOLS_CACHEPACK_CP_FIELDS_H

/*
 * The server pack's container — what cachepack wraps a server band in.
 *
 * ## The register and the band are the library's now
 *
 * `fields/<type>.ini` is read by `rscache_register.h` (`struct RSCache_Register`)
 * and the band is written by `rscache_band.h` (`RSCache_BandEncode`). The game
 * server reads both back through the same two files, so there is one parser of the
 * register and one codec of the band — this file used to hold a second of each,
 * and the two halves agreed only through tests. What cachepack keeps is what only
 * a writer needs:
 *
 *   - `ref` validated against `cp_type_by_name` (`pack_server_type`): the library
 *     knows no config type, so whether `ref = obj` names one is cachepack's
 *     question, and a misspelling is fatal there.
 *   - symbolic values resolved to ids (`resolve_field_value` in cp_pack.c), and
 *     a value too wide for its wire refused with `RSCache_BandFits` and tallied
 *     per field — never handed to the encoder, which asserts on one.
 *   - the archive header, the server-only namespaces and their name tables,
 *     below.
 *
 * **There are no built-in defaults, deliberately.** A writer that invents an
 * opcode the tree did not declare writes bytes nothing agreed to read. A tree with
 * no `fields/<type>.ini` therefore emits no server band at all, which is the safe
 * answer rather than a silently different one.
 *
 * ## `ref` — the namespace a value is spelled in
 *
 * The band is integers. Content is not: `param=death_drop,bones` names an obj and
 * `param=attack_anim,cow_attack` names a sequence, and turning either into an id
 * needs to know *which* pack file to look in:
 *
 *     [npc.death_drop]  server = opcode:151:u4   ref = obj
 *
 * A field with no `ref` accepts only a decimal literal. That is the right default:
 * `huntmode = aggressive` is an engine enum with no cache namespace at all, and
 * guessing one would bake a number nobody agreed on. The unresolved value is
 * reported per field and not written, which is a visible gap rather than a wrong
 * record.
 */

#include "rscache_band.h"
#include "rscache_register.h"
#include "rscache_serverpack.h"

#include <stdint.h>

enum
{
    /** Every field at once — opcode byte plus widest payload — plus the
     *  terminator: `RSCache_BandEncodeBound` of the largest register there can be,
     *  so a stack buffer of this size satisfies the encoder for any register. */
    CP_SERVER_BAND_MAX = (RSCACHE_REGISTER_MAX * 5) + 1,
};

/** The byte count of a band wire width (`u2` -> 2), for diagnostics that spell
 *  the width the way the register does. 0 for a field with no band home. */
int
cp_register_wire_bytes(enum RSCache_RegisterWire wire);

/* ---- the archive -------------------------------------------------------- */

/*
 * `server/pack/` is a dat2 with no reference table.
 *
 * `RSCache_Dat2DiskWriteArchive` will create `main_file_cache.dat2` and an
 * `idx<n>` from nothing, which is what makes the server pack a real cache
 * directory rather than a bespoke file format — but it writes no `idx255`, so
 * there is no per-archive CRC, version or child list the way a client cache has.
 * Nothing would notice an archive left over from a tree two edits ago.
 *
 * Hence a header on every archive, carrying the three facts the missing reference
 * table would have carried:
 *
 *     u8  'S'
 *     u8  'P'
 *     u8  version        — the payload grammar's revision, so a reader can refuse
 *                          a stream it predates instead of decoding it as if it
 *                          understood
 *     u8  kind           — which grammar: an opcode band, or a name table
 *     u32 crc32(payload) — over the bytes that follow, so truncation and a stale
 *                          sector chain are detectable
 *     ..  payload
 *
 * Eight bytes on a payload that is typically under forty. The magic is not
 * paranoia: these archives sit in a directory that looks exactly like a client
 * cache, and reading one as the other should fail loudly on the first byte. The
 * `kind` byte is what keeps the container self-describing now that two different
 * payloads live in it — inferring the grammar from the group id would work today
 * and stop working the moment a server-only type has both.
 */
/* The framing is the library's (rscache_serverpack.h); these name it here. */
#define CP_SERVER_PACK_VERSION RSCACHE_SERVERPACK_VERSION

/** What an archive's payload is. */
enum CP_ServerPayload
{
    /** `<opcode:u8> <payload>` until a zero opcode — one record's server band. */
    CP_SERVER_PAYLOAD_BAND = RSCACHE_SERVERPACK_KIND_BAND,
    /** `u2 count`, then `u4 id` + NUL-terminated name per entry. */
    CP_SERVER_PAYLOAD_NAMES = RSCACHE_SERVERPACK_KIND_NAMES,
    /** Up to 256 records' client encodings. */
    CP_SERVER_PAYLOAD_RECORDS = RSCACHE_SERVERPACK_KIND_RECORDS,
};

enum
{
    CP_SERVER_PACK_HEADER = RSCACHE_SERVERPACK_HEADER,
};

/** Wrap an encoded band — `RSCache_BandEncode`'s output, terminator included —
 *  in the header. Returns bytes written, or 0. */
uint32_t
cp_server_archive_build(
    const uint8_t* band,
    uint32_t band_size,
    uint8_t* out,
    uint32_t out_capacity);

/** The same framing over any payload — one entry point, so the magic, version
 *  and CRC cannot be written two slightly different ways. */
uint32_t
cp_server_archive_build_payload(
    int kind,
    const uint8_t* payload,
    uint32_t payload_size,
    uint8_t* out,
    uint32_t out_capacity);

/**
 * Validate a header and point at the payload inside it.
 *
 * Returns 1 when the magic, the version and the CRC all hold, 0 otherwise. The
 * writer's own round-trip check runs through this, so the header is exercised by
 * the thing that writes it rather than only by a reader nobody has written yet.
 */
int
cp_server_archive_open(
    const uint8_t* data,
    int size,
    int* out_version,
    int* out_kind,
    const uint8_t** out_payload,
    int* out_payload_size);

/* ---- server-only namespaces ---------------------------------------------- */

/*
 * `stat` and `category` are not records the client ignores. They are types the
 * client has no concept of: no config kind, no gameval archive, nothing in the
 * cache to overlay. Today they exist only as `pack/<ns>.pack` text re-read at
 * every boot, which is the same "re-parsed out of text" problem the server band
 * removed for npc.
 *
 * ## The group-id space, and why 128
 *
 * The server pack addresses an archive by `(group, id)`, borrowing the cache's
 * config-kind numbering — `npc` is group 9 because that is its kind. A
 * server-only type has no kind, so it needs a number from a space that provably
 * cannot collide with one.
 *
 * **The whole space is 0..255, and that is a format limit rather than a
 * convention.** Every sector in a dat2 carries its table id in a single byte
 * (`dat2disk.c`, `data[7] = header->index_id & 0xFF` in both the small and large
 * sector headers), so a group id of 256 is not merely unwise — it is written as
 * 0 and silently aliases another table. Anything "well above the cache's range"
 * has to fit in a byte.
 *
 * Inside that byte: `dat2_configs.h` runs 1..39, with 39 (`dbtable`) the largest
 * kind this revision defines. **128 is the top half of the space**, leaving
 * 40..127 — more than double the current maximum — for OldSchool to grow into
 * before it reaches ours. That is the same argument the 64..255 opcode band
 * makes in `torirs_server_servercodec.h`, and it is the strongest one available: the
 * numbers below are Jagex's and may grow, the numbers above are ours alone, and
 * the gap between them is the margin.
 *
 * One group per namespace, with its name table at archive 0. A server-only type
 * that later needs per-record bands — `prayer` will — takes its own group and
 * says so here, rather than sharing one and reserving archive ids inside it.
 */
#define CP_SERVER_GROUP_BASE RSCACHE_SERVERPACK_NAMES_GROUP_BASE

struct CP_ServerGroup
{
    /** Namespace as `pack/<name>.pack` spells it. */
    const char* name;
    /** idx number inside `server/pack`, 128..255. */
    int group;
};

/** The server-only namespaces, for the packer and for the test that checks the
 *  space is respected. */
const struct CP_ServerGroup*
cp_server_groups(int* out_count);

/** `ns`'s group, or -1 when it is not a server-only namespace. */
int
cp_server_group_for(const char* ns);

/**
 * Encode an `id -> name` table.
 *
 * Sparse by construction: the id is written per entry rather than implied by
 * position. A dense form would be wrong for `category`, whose ids are the
 * cache's own and run to 131 across 24 names — and it is also why this is not a
 * `RSCache_FileList`, which indexes members 0..n-1 and takes their real ids from
 * a reference table this pack does not have.
 *
 * Returns bytes written, or 0 if `out_capacity` is too small.
 */
uint32_t
cp_server_names_encode(
    const int* ids,
    const char* const* names,
    int count,
    uint8_t* out,
    uint32_t out_capacity);

/**
 * Read one back.
 *
 * `out_ids` and `out_names` are filled up to `max`; the names point into `data`
 * and are NUL-terminated there. Returns the entry count, or -1 when the table is
 * malformed.
 */
int
cp_server_names_decode(
    const uint8_t* data,
    int size,
    int* out_ids,
    const char** out_names,
    int max);

#endif
