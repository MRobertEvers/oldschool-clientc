#ifndef TORIRSSERVER_SERVPACK_H
#define TORIRSSERVER_SERVPACK_H

/*
 * The reader of `<content>/server/pack` — the dat2 cachepack's `pack` writes.
 *
 * This is the container half of the pair whose codec half is
 * `torirs_server_servercodec.h`: that file turns a band of `<opcode> <payload>` bytes
 * into record fields, this one gets those bytes off the disk. The split matters
 * because the two fail differently — a band that decodes wrong is a register
 * drift, a band that will not *open* is a stale or truncated pack — and the
 * caller has to tell them apart to fall back sensibly.
 *
 * ## The pack is a cache directory with no reference table
 *
 * One archive per record at `(config kind, record id)` — `npc` in idx9, `loc`
 * in idx6, the same coordinates the client cache uses. Nothing here needs a
 * reference table to *find* an archive (the idx file is the index), but a
 * reference table is also what a cache uses to notice staleness, and this pack
 * has none. Its replacement is a header on every archive:
 *
 *     u8  'S'  u8  'P'
 *     u8  version   — the payload grammar's revision
 *     u8  kind      — 1 = an opcode band, 2 = a name table
 *     u32 crc32(payload)
 *
 * written by `cp_server_archive_build_payload` (cachepack's `cp_fields.c`) and
 * validated here, byte for byte. The two files are the two ends of one wire:
 * neither may change the framing without the other, which is why the constants
 * below restate `cp_fields.h`'s and say so.
 */

#include <stdint.h>
#include <stdio.h>

/** More than `ToriRSServer_ServerEncodeBound` for every registered type, with room
 *  for fields a newer register declares that this build does not know yet —
 *  those must reach `ToriRSServer_ServerDecode` so the short read is *its* report. */
enum
{
    TORIRSSERVER_SERVPACK_BAND_MAX = 512,
};

/** `ToriRSServer_ServPackReadBand` results at or below zero. */
enum
{
    /** No archive at this (group, id) — an ordinary sparse gap. */
    TORIRSSERVER_SERVPACK_ABSENT = 0,
    /** An archive is indexed but does not validate: bad magic, a version this
     *  build predates, the wrong payload kind, or a CRC mismatch. A stale or
     *  truncated pack, never a decode problem. */
    TORIRSSERVER_SERVPACK_INVALID = -1,
};

struct ToriRSServerServPackIdx
{
    int group;
    FILE* file;
    int entries;
};

struct ToriRSServerServPack
{
    char dir[560];
    FILE* dat2;
    /** idx files, opened lazily per group. Two config kinds and two name-table
     *  groups exist today; eight is headroom, not a format limit. */
    struct ToriRSServerServPackIdx idx[8];
    int idx_count;
};

/** Open `<content_dir>/server/pack`. Returns 0, or -1 when there is no pack —
 *  which is not an error: a fresh checkout has none until `cachepack pack`
 *  runs, exactly as it has no script pack until `torirsserver-scripts` does. */
int
ToriRSServer_ServPackOpen(
    struct ToriRSServerServPack* pack,
    const char* content_dir);

void
ToriRSServer_ServPackClose(struct ToriRSServerServPack* pack);

/** Entries in `group`'s idx file — an exclusive upper bound on archive ids, so
 *  a caller can scan the pack without a reference table. 0 when the group has
 *  no idx at all. */
int
ToriRSServer_ServPackEntryCount(
    struct ToriRSServerServPack* pack,
    int group);

/**
 * Read archive `(group, id)` and validate it down to the band bytes.
 *
 * Validation is the whole point of the call: the container framing, then the
 * `'S' 'P'` header — magic, version, the band payload kind, and the CRC over
 * the payload. What lands in `out` is only the band itself, ready for
 * `ToriRSServer_ServerDecode`.
 *
 * Returns the band's size in bytes, `TORIRSSERVER_SERVPACK_ABSENT`, or
 * `TORIRSSERVER_SERVPACK_INVALID`.
 */
int
ToriRSServer_ServPackReadBand(
    struct ToriRSServerServPack* pack,
    int group,
    int id,
    uint8_t* out,
    int out_capacity);

#endif
