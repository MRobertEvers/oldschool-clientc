#ifndef RSCACHE_DAT2DISK_H
#define RSCACHE_DAT2DISK_H

#include "rscache_profile.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct RSCache_ReferenceTable;

struct RSCache_Dat2DiskSectorHeader
{
    int part_no;
    int next_sector_no;
    int index_id;
    int archive_id;
};

struct RSCache_Dat2DiskIndexRecord
{
    int idx_file_id;
    int archive_idx;
    int sector;
    int length;
};

struct RSCache_Dat2DiskArchive
{
    char* data;
    int data_size;
    int archive_id;
    int table_id;
    int revision;
    int file_count;
    int* file_ids;
};

/*
 * ## Tables are a game's property, not the container's
 *
 * A dat2 cache addresses its contents by table id, but *what a table id means* is a
 * property of the branch the cache came from. The two branches this library reads
 * disagree over most of the range above 15:
 *
 *   id | RS2 (643)  | OSRS
 *   16 | loc        | (unused)
 *   17 | enum       | (unused)
 *   18 | npc        | worldmap geography
 *   19 | obj        | worldmap
 *   20 | seq        | worldmap ground
 *   21 | spotanim   | dbtable index
 *   22 | varbit     | animayas
 *   26 | materials  | (unused)
 *
 * OSRS keeps those record types as groups inside the config table (2); RS2 promotes
 * them to their own top-level tables.
 *
 * So there are **three** enums here, and the split is deliberate:
 *
 *   RSCache_Dat2Table          — what a caller *wants* (a logical table), game-free.
 *   RSCache_Dat2OsrsDiskTable  — the on-disk ids an OldSchool cache uses.
 *   RSCache_Dat2Rs2DiskTable   — the on-disk ids an RS2 dat2 cache uses.
 *
 * Each disk enum is **complete and self-contained**: neither borrows a constant from
 * the other, even where the two happen to agree (the 0..15 block does, everywhere).
 * That duplication is the point — an RS2 code path that reaches for an OSRS constant
 * is then a visible mistake rather than a coincidence that holds until the id it
 * borrowed is one of the ones that diverge. Table 19 read through the wrong enum does
 * not fail; it decodes objs as worldmap data.
 *
 * Callers name a logical table and let RSCache_Dat2DiskTableForGame (or
 * RSCache_Dat2DiskTableId, which takes the game from the open disk's profile) pick
 * the id. A logical table the game has no table for resolves to
 * RSCACHE_DAT2_DISK_TABLE_ABSENT, which is a load that must not be attempted rather
 * than one that fails.
 *
 * Ids per void's Index.kt, verified against cache.rs643 and the OSRS caches in-tree.
 */

/**
 * A table by role, independent of which branch's cache is open.
 *
 * This is the currency of the loading code: every call site that knows *what* it wants
 * but not *which cache* names one of these, and resolution happens in one place.
 */
enum RSCache_Dat2Table
{
    /* Present in both epochs (and, as it happens, at the same ids). */
    RSCACHE_DAT2_TABLE_ANIMATIONS = 0,
    RSCACHE_DAT2_TABLE_SKELETONS,
    RSCACHE_DAT2_TABLE_CONFIGS,
    RSCACHE_DAT2_TABLE_INTERFACES,
    RSCACHE_DAT2_TABLE_SOUND_EFFECTS,
    RSCACHE_DAT2_TABLE_MAPS,
    RSCACHE_DAT2_TABLE_MUSIC_TRACKS,
    RSCACHE_DAT2_TABLE_MODELS,
    RSCACHE_DAT2_TABLE_SPRITES,
    RSCACHE_DAT2_TABLE_TEXTURES,
    RSCACHE_DAT2_TABLE_BINARY,
    RSCACHE_DAT2_TABLE_MUSIC_JINGLES,
    RSCACHE_DAT2_TABLE_CLIENTSCRIPT,
    RSCACHE_DAT2_TABLE_FONTS,
    RSCACHE_DAT2_TABLE_MUSIC_SAMPLES,
    RSCACHE_DAT2_TABLE_MUSIC_PATCHES,

    /* OldSchool only. */
    RSCACHE_DAT2_TABLE_WORLDMAP_GEOGRAPHY,
    RSCACHE_DAT2_TABLE_WORLDMAP,
    RSCACHE_DAT2_TABLE_WORLDMAP_GROUND,
    RSCACHE_DAT2_TABLE_DBTABLE_INDEX,
    RSCACHE_DAT2_TABLE_ANIMAYAS,
    RSCACHE_DAT2_TABLE_GAMEVALS,

    /* RS2 only: the record types OldSchool keeps inside the config table. */
    RSCACHE_DAT2_TABLE_LOC,
    RSCACHE_DAT2_TABLE_ENUM,
    RSCACHE_DAT2_TABLE_NPC,
    RSCACHE_DAT2_TABLE_OBJ,
    RSCACHE_DAT2_TABLE_SEQ,
    RSCACHE_DAT2_TABLE_SPOTANIM,
    RSCACHE_DAT2_TABLE_VARBIT,
    RSCACHE_DAT2_TABLE_MATERIALS,
    RSCACHE_DAT2_TABLE_PARTICLES,
    RSCACHE_DAT2_TABLE_DEFAULTS,

    RSCACHE_DAT2_TABLE_COUNT,
};

/** OldSchool on-disk table ids. Complete in itself — see the note above. */
enum RSCache_Dat2OsrsDiskTable
{
    RSCACHE_DAT2_OSRS_TABLE_ANIMATIONS = 0,
    RSCACHE_DAT2_OSRS_TABLE_SKELETONS = 1,
    RSCACHE_DAT2_OSRS_TABLE_CONFIGS = 2,
    RSCACHE_DAT2_OSRS_TABLE_INTERFACES = 3,
    RSCACHE_DAT2_OSRS_TABLE_SOUND_EFFECTS = 4,
    RSCACHE_DAT2_OSRS_TABLE_MAPS = 5,
    RSCACHE_DAT2_OSRS_TABLE_MUSIC_TRACKS = 6,
    RSCACHE_DAT2_OSRS_TABLE_MODELS = 7,
    RSCACHE_DAT2_OSRS_TABLE_SPRITES = 8,
    RSCACHE_DAT2_OSRS_TABLE_TEXTURES = 9,
    RSCACHE_DAT2_OSRS_TABLE_BINARY = 10,
    RSCACHE_DAT2_OSRS_TABLE_MUSIC_JINGLES = 11,
    RSCACHE_DAT2_OSRS_TABLE_CLIENTSCRIPT = 12,
    RSCACHE_DAT2_OSRS_TABLE_FONTS = 13,
    RSCACHE_DAT2_OSRS_TABLE_MUSIC_SAMPLES = 14,
    RSCACHE_DAT2_OSRS_TABLE_MUSIC_PATCHES = 15,
    RSCACHE_DAT2_OSRS_TABLE_WORLDMAP_GEOGRAPHY = 18,
    RSCACHE_DAT2_OSRS_TABLE_WORLDMAP = 19,
    RSCACHE_DAT2_OSRS_TABLE_WORLDMAP_GROUND = 20,
    RSCACHE_DAT2_OSRS_TABLE_DBTABLE_INDEX = 21,
    RSCACHE_DAT2_OSRS_TABLE_ANIMAYAS = 22,
    // Added in ~rev230
    RSCACHE_DAT2_OSRS_TABLE_GAMEVALS = 24,
};

/**
 * RS2-branch (643-era) on-disk table ids. Complete in itself — see the note above.
 *
 * The 0..15 block is spelled out again rather than shared with the OldSchool enum. It
 * agrees id-for-id in every cache measured, and it is still written out, because the
 * agreement is a fact about these two branches and not a rule the next one has to keep.
 *
 * cache.rs643 also ships idx23..25, idx29..34 and idx36, whose contents are not
 * identified here. They are reachable — RSCache_Dat2DiskIsValidTableId is a range
 * check, not an allow-list — just unnamed.
 */
enum RSCache_Dat2Rs2DiskTable
{
    RSCACHE_DAT2_RS2_TABLE_ANIMATIONS = 0,
    RSCACHE_DAT2_RS2_TABLE_SKELETONS = 1,
    RSCACHE_DAT2_RS2_TABLE_CONFIGS = 2,
    RSCACHE_DAT2_RS2_TABLE_INTERFACES = 3,
    RSCACHE_DAT2_RS2_TABLE_SOUND_EFFECTS = 4,
    RSCACHE_DAT2_RS2_TABLE_MAPS = 5,
    RSCACHE_DAT2_RS2_TABLE_MUSIC_TRACKS = 6,
    RSCACHE_DAT2_RS2_TABLE_MODELS = 7,
    RSCACHE_DAT2_RS2_TABLE_SPRITES = 8,
    RSCACHE_DAT2_RS2_TABLE_TEXTURES = 9,
    RSCACHE_DAT2_RS2_TABLE_BINARY = 10,
    RSCACHE_DAT2_RS2_TABLE_MUSIC_JINGLES = 11,
    RSCACHE_DAT2_RS2_TABLE_CLIENTSCRIPT = 12,
    RSCACHE_DAT2_RS2_TABLE_FONTS = 13,
    RSCACHE_DAT2_RS2_TABLE_MUSIC_SAMPLES = 14,
    RSCACHE_DAT2_RS2_TABLE_MUSIC_PATCHES = 15,
    RSCACHE_DAT2_RS2_TABLE_LOC = 16,
    RSCACHE_DAT2_RS2_TABLE_ENUM = 17,
    RSCACHE_DAT2_RS2_TABLE_NPC = 18,
    RSCACHE_DAT2_RS2_TABLE_OBJ = 19,
    RSCACHE_DAT2_RS2_TABLE_SEQ = 20,
    RSCACHE_DAT2_RS2_TABLE_SPOTANIM = 21,
    RSCACHE_DAT2_RS2_TABLE_VARBIT = 22,
    /** Procedural texture materials. Its presence is what selects the procedural texture
     *  system over the sprite-backed one — see RSCache_Dat2UsesProcTextures. */
    RSCACHE_DAT2_RS2_TABLE_MATERIALS = 26,
    RSCACHE_DAT2_RS2_TABLE_PARTICLES = 27,
    RSCACHE_DAT2_RS2_TABLE_DEFAULTS = 28,
};

/** Resolution result for a logical table this epoch has no table for. Not an error:
 *  an OldSchool cache has no materials table and a 643 cache has no world map, so the
 *  right response is to skip the load, not to attempt and report it. */
#define RSCACHE_DAT2_DISK_TABLE_ABSENT (-1)

/**
 * One past the highest addressable table id, across every game — the bound for
 * anything indexed by table id, and for RSCache_Dat2DiskIsValidTableId.
 *
 * Deliberately not a per-game count: it sizes arrays and validates ids, neither of
 * which should change with the cache that happens to be open. 36 is the highest idx
 * file any cache in-tree ships (cache.rs643's idx36).
 */
#define RSCACHE_DAT2_DISK_TABLE_CAPACITY 37

/**
 * The on-disk table id `table` has for `game` (an enum RSCache_Game), or
 * RSCACHE_DAT2_DISK_TABLE_ABSENT when that game has no such table.
 *
 * RSCACHE_GAME_UNSET / anything other than OLDSCHOOL or RS2 resolves to ABSENT:
 * a dat1 cache is a different container, read through dat1disk.h.
 */
int
RSCache_Dat2DiskTableForGame(
    int game,
    enum RSCache_Dat2Table table);

/**
 * There are many tables in the Dat2 cache format.
 *
 * Table 255 serves as a special table.
 * Table 255 contains archive metadata for the archives stored in
 * the other tables.
 * The metadata includes, file names, file ids, and CRC information.
 * The archive ids in table 255 correspond to the table ids of the other tables.
 * For example, table 2 corresponds to archive 2 of table 255.
 * The archives in table 255 are a binary blob that is parsed into a list of archive metadata.
 * So the metadata for the archives of table 2 are stored in a list
 * of archive metadata in archive 2 of table 255.
 * You can find archive metadata by searching through the list of archive metadata and looking for
 * the archive id that matches the archive id of the archive in table 2.
 *
 * Otherwise, Table 255 behaves like the other tables.
 *
 * Each table stores Archives. Archives can be a single blob,
 * or a multi-file archive. Archives can be compressed or uncompressed.
 *
 * The compression mode of the archive is stored WITH the archive.
 * All other information about the archive is stored in the table 255.
 *
 * Archives that contain multiple files are stored in a multi-file format, called a "FileList" here.
 * Note: For "dat", LostCity calls this a "JagFile", we call it a "FileListDat", which is slightly
 * different from "FileList".
 */

/**
 * Table id of the special reference table archives (see comment above).
 * Reference table N (archive_id=N, table_id=RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID)
 * holds the RSCache_ReferenceTable metadata for table N. It is fetched
 * through the exact same archive-load path as any other table/archive pair, so
 * callers without a live RSCache_Dat2Disk handle (e.g. an IO queue consumer) can
 * request it like any other cache archive.
 */
#define RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID 255

/**
 * Where a disk's archive containers actually live.
 *
 * Every RSCache_Dat2Disk has one. The dat2 sector file is not the interface, it
 * is an *implementation* of the interface — the one this library ships for
 * hosts that have a filesystem (RSCache_Dat2DiskFileStore, installed by the
 * NewFromDirectory constructors). A host with different storage supplies its
 * own and gets everything else unchanged: reference tables, table id
 * resolution, XTEA, archive metadata and the whole decode path are the same
 * code, because they were never about the container layout.
 *
 * Making it required rather than optional is the point. An "if there is a
 * store, else read the file" disk has two backings and two sets of bugs, and
 * the file path stays the one that is really tested; one mandatory vtable means
 * the browser exercises the same call sites the desktop does.
 *
 * ## Why anyone would want a different one
 *
 * The dat2 layout solves a problem a filesystem has and a key-value store does
 * not: packing variable-length archives into one file without a per-archive
 * inode. A host whose durable storage is already keyed — IndexedDB in a
 * browser, which is what forced this — gains nothing from sector chains and
 * pays for them twice, in the 520-byte sector headers and in the orphaned
 * sectors every rewrite leaves behind (RSCache_Dat2DiskWriteArchive appends and
 * re-points, exactly as the real client does).
 *
 * ## The unit
 *
 * A record is the exact idx-record payload: the JS5 container, plus whatever
 * local version trailer the writer appended. That is deliberately the same blob
 * RSCache_Dat2DiskArchiveNewLoadRaw returns and RSCache_Dat2DiskWriteArchiveTo
 * accepts, so a store transports bytes this library already round-trips and is
 * never a second encoding to keep in step.
 */
struct RSCache_Dat2Store
{
    /** Passed to every callback below. The file store points it at the disk. */
    void* user;

    /**
     * Fetch one record. Returns 1 and hands over a malloc'd buffer the caller
     * frees, 0 when the record does not exist, -1 on error.
     *
     * "Does not exist" and "cannot be produced right now" must not be confused:
     * a store that cannot answer synchronously has to report -1, because 0 is
     * what tells JS5 the group is missing and worth downloading again.
     */
    int (*get)(void* user, int table_id, int archive_id, uint8_t** data, int* size);

    /** Create or replace one record. Returns 0 on success, -1 on failure.
     *  NULL for a store that cannot be written (a read-only serving cache). */
    int (*put)(void* user, int table_id, int archive_id, const uint8_t* data, int size);

    /**
     * Non-zero when table_id holds at least one record. This is the store's
     * answer to "is there a main_file_cache.idxN": it decides which tables the
     * open scans for a reference table, so a store that answered optimistically
     * would report a failed decode for every table the cache does not have.
     */
    int (*has_table)(void* user, int table_id);

    /**
     * Optional. Called after a reference table container has been put, so a
     * backing with side structures can bring them up to date.
     *
     * It exists for exactly one thing: the file store's zero-byte idxN presence
     * sentinel, and reopening the dat2 reader whose buffered state predates the
     * write. A keyed store has neither — has_table answers from the records
     * themselves — and leaves this NULL. Returns 0 on success.
     */
    int (*commit_table)(void* user, int table_id);

    /** Optional. Frees `user` when the disk closes. NULL when the store's state
     *  is owned elsewhere (the file store's is the disk itself). */
    void (*destroy)(void* user);
};

struct RSCache_Dat2Disk
{
    /** The cache directory, or — for a store the caller supplied — a label
     *  naming it. Only the file store opens it as a path; it is kept for every
     *  backing because diagnostics and callers such as the JS5 storage adapter
     *  identify a cache by it. */
    char* directory;
    struct RSCache_ReferenceTable* tables[RSCACHE_DAT2_DISK_TABLE_CAPACITY];
    /** The file store's handle on main_file_cache.dat2, and NULL under any
     *  other backing. Nothing outside the file store may read through it. */
    FILE* dat2_file;
    /** Stated identity. game/revision drive table ids and the map XTEA gate.
     *  Unset (ProfileZero) until RSCache_Dat2DiskSetProfile. */
    struct RSCache profile;
    int profile_set;
    /** Non-zero when opened through NewReadOnlyFromDirectory. */
    int read_only;
    /** The backing. Never empty: store.get is non-NULL on every open disk. */
    struct RSCache_Dat2Store store;
};

bool
RSCache_Dat2DiskIsValidTableId(int table_id);

/**
 * Declare which cache this disk is reading, so table lookups through
 * RSCache_Dat2DiskTableId resolve to that branch's ids and map loaders know
 * whether lX_Z archives are XTEA-encrypted.
 *
 * Stated, never detected — the same rule as RSCache identity, and for the same
 * reason: RS2 and early-OldSchool caches are indistinguishable by their
 * reference-table revisions. A disk opened without this call has no table ids
 * to offer (ABSENT) and MapLocsEncrypted must not be asked.
 *
 * Only affects id *resolution* and XTEA. Opening a cache reads whichever idx
 * files are present regardless of game, so this may be called at any point after
 * the open.
 */
void
RSCache_Dat2DiskSetProfile(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile);

/** The profile identity this disk was told, or NULL if SetProfile was never called. */
const struct RSCache*
RSCache_Dat2DiskProfile(const struct RSCache_Dat2Disk* disk);

/** The on-disk id `table` has in this cache, or RSCACHE_DAT2_DISK_TABLE_ABSENT.
 *  A NULL / unset disk answers ABSENT. */
int
RSCache_Dat2DiskTableId(
    const struct RSCache_Dat2Disk* disk,
    enum RSCache_Dat2Table table);

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromDirectory(char const* directory);

/**
 * Open an existing cache using read-only file handles. This is intended for
 * cache-serving processes and guarantees that mutating disk helpers reject
 * the handle.
 */
struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewReadOnlyFromDirectory(const char* directory);

/**
 * Open a cache directory for incremental population, creating an empty dat2
 * file when needed. Existing dat2 bytes are never truncated. The directory
 * itself must already exist.
 */
struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewSparseFromDirectory(const char* directory);

/**
 * Open a cache whose archives live in `store` rather than in dat2/idx files.
 *
 * `label` names the cache for diagnostics and for callers that identify one by
 * `disk->directory`; nothing opens it as a path. `store` is required and must
 * supply at least `get`. Unless it sets `destroy`, `store->user` stays the
 * caller's and must outlive the disk.
 *
 * Reference tables are decoded at open exactly as for a directory, so an empty
 * store yields a disk with no tables, which is the correct starting point for
 * an incremental (JS5-populated) cache.
 */
struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromStore(
    const char* label,
    const struct RSCache_Dat2Store* store);

/**
 * The dat2-file backing, bound to `disk`.
 *
 * This is what the NewFromDirectory constructors install, exposed so that the
 * shipped backing is a peer of any other rather than a hidden default. It reads
 * `disk->directory`, `disk->dat2_file` and `disk->read_only`, so it is only
 * meaningful for a disk those describe.
 */
struct RSCache_Dat2Store
RSCache_Dat2DiskFileStore(struct RSCache_Dat2Disk* disk);

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewUninitialized(void);
void
RSCache_Dat2DiskFree(struct RSCache_Dat2Disk* disk);

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewReferenceTableLoad(
    struct RSCache_Dat2Disk* disk,
    int table_id);

/**
 * Read the exact idx-record bytes without decrypting or decompressing them.
 * The returned archive owns its data and must be freed with
 * RSCache_Dat2DiskArchiveFree.
 */
struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoadRaw(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id);

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoad(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id);
struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoadDecrypted(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id,
    uint32_t* xtea_key_nullable);
/** Fill revision/file_count/file_ids from the table's reference entry.
 *  Returns false (archive metadata untouched) when the reference table does
 *  not list the archive id — possible in hand-patched caches whose idx files
 *  carry records the master index never learned about. */
bool
RSCache_Dat2DiskArchiveInitMetadata(
    struct RSCache_Dat2Disk* disk,
    struct RSCache_Dat2DiskArchive* archive);
bool
RSCache_Dat2DiskArchiveInitMetadataFromTable(
    struct RSCache_ReferenceTable* table,
    struct RSCache_Dat2DiskArchive* archive);
void
RSCache_Dat2DiskArchiveFree(struct RSCache_Dat2DiskArchive* archive);

uint32_t*
RSCache_Dat2DiskArchiveXteaKey(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id);

int
RSCache_Dat2DiskDat2FileReadArchive(
    FILE* dat2_file,
    int idx_file_id,
    int archive_id,
    int sector,
    int length,
    struct RSCache_Dat2DiskArchive* archive);

int
RSCache_Dat2DiskDatFileReadArchive(
    FILE* dat_file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct RSCache_Dat2DiskArchive* archive);

int
RSCache_Dat2DiskDat2FileAppendArchive(
    FILE* file,
    int index_id,
    int archive_id,
    uint8_t* data,
    int data_size);

int
RSCache_Dat2DiskIndexFileReadRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_Dat2DiskIndexRecord* record);

int
RSCache_Dat2DiskIndexFileWriteRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_Dat2DiskIndexRecord* record);

/*
 * Close the cached write handles.
 *
 * RSCache_Dat2DiskWriteArchive keeps the .dat2 and the current .idxN open
 * between calls (an open per archive was a measurable share of a pack on
 * Windows). Every write is flushed, so readers always see the bytes — but a
 * caller that DELETES or MOVES a cache file must call this first, and so must
 * anything finishing up before exit.
 */
void
RSCache_Dat2DiskWriteFlush(void);


/**
 * Write one already-framed archive container into a cache directory.
 *
 * `data` must be the output of RSCache_ArchiveEncode — compression byte, lengths
 * and payload — not raw decompressed bytes. This layer only places those bytes in
 * the sector chain and records the idx entry.
 *
 * Creates `main_file_cache.dat2` and `main_file_cache.idx<table_id>` if absent,
 * and appends; an archive id that already has an entry is re-pointed at the new
 * copy, leaving the old sectors orphaned. That is the same
 * append-and-orphan behaviour the real client's cache writer has, so repacking a
 * cache in place grows the file.
 *
 * One subtlety worth knowing: RSCache_Dat2DiskIndexFileReadRecord treats
 * `sector <= 0` as "no such archive", so nothing may live at sector 0. A fresh
 * `.dat2` therefore gets one reserved zero sector before the first archive.
 *
 * Returns 0 on success, -1 on failure.
 */
int
RSCache_Dat2DiskWriteArchive(
    const char* directory,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size);

/**
 * Write one archive into whichever backing `disk` has.
 *
 * The directory-keyed form above predates stores and cannot reach one, because
 * a path is not enough to name a store. Callers that hold a disk should use
 * this: it routes to the store when there is one and to the disk's directory
 * otherwise, so the same call site serves both backings.
 *
 * Returns 0 on success, -1 on failure (including a read-only disk).
 */
int
RSCache_Dat2DiskWriteArchiveTo(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size);

/**
 * Persist and install one raw reference-table container for table N.
 *
 * The input is borrowed and written byte-for-byte as archive 255/N. The
 * function also creates the non-truncating idxN sentinel used during cache
 * discovery, decodes a private copy, and replaces disk->tables[N] only after
 * every step succeeds. Existing table metadata is freed after the swap.
 */
bool
RSCache_Dat2DiskInstallReferenceTableRaw(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    const uint8_t* data,
    int data_size);

#endif
