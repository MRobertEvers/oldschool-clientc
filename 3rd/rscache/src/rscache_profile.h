#ifndef RSCACHE_PROFILE_H
#define RSCACHE_PROFILE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Cache identity — what a decoder or encoder needs to know about the cache it is
 * working on, in one value that callers pass down instead of a bare int.
 *
 * ## Why this exists
 *
 * Field layouts change between game revisions. Before this struct, era
 * information reached decoders only as an `int revision` lifted from the JS5
 * reference-table version of whichever archive the record came from. That is a
 * genuinely ambiguous quantity:
 *
 *  - It is a **per-archive counter**, not a per-cache one. The npc, seq and loc
 *    config groups all live in table 2 but carry independent revisions, which is
 *    why the thresholds RuneLite gates on are unrelated numbers (npc 1493,
 *    seq 1141/1268).
 *  - Its *units* changed. Old caches number archives from a small integer;
 *    modern OSRS caches store a unix timestamp. Decoders in this library
 *    disagreed about which: loc and texture treated `>= 2000` as "modern", while
 *    sequence treated `<= 1141` as "old" — two incompatible readings of one
 *    field. A modern cache took sequence's newest branch only because a
 *    timestamp happens to exceed every small-integer threshold.
 *
 * So an archive revision alone cannot answer "what layout is this". The game
 * revision can, and callers usually know it: it is in the boot manifest. This
 * struct carries it, and keeps the per-group archive revisions alongside for
 * caches that arrive without one.
 *
 * ## The four identity fields
 *
 * Every field is load-bearing. They are stated by whoever opens the cache
 * (manifest, CLI, or a named revision module) and never detected from disk:
 *
 *   game      — which revision lineage `revision` is numbered in
 *               (oldschool | rs2)
 *   epoch     — on-disk container (dat1 jagfile | dat2 js5)
 *   revision  — game revision in `game`'s lineage (230, 254, 643, ...)
 *   quirks    — client-build differences no revision comparison can imply
 *
 * The three lineages are `(game, epoch)` pairs:
 *
 *   (rs2, dat1)       — 2004-era jagfile (~200–254)
 *   (rs2, dat2)       — RS2/main continuous line (377 onward; 643 here)
 *   (oldschool, dat2) — restarted at 1 in 2013 (184–239 in this corpus)
 *
 * `revision=643` is only meaningful next to `game=rs2`. Comparing it against an
 * OldSchool threshold is the D16/D20 trap; the lineage predicates below prevent
 * that.
 *
 * ## How a datatype consults it
 *
 * Each datatype owns two functions, declared in its own header:
 *
 *   int RSCache_<Type>Flags(const struct RSCache* cache);
 *   int RSCache_<Type>CodecVersion(const struct RSCache* cache);
 *
 * `Flags` is for field-level differences — a byte that appears in one era, a
 * width that widens — which one flag-driven codec can absorb. `CodecVersion`
 * is for differences too large for that, where the era gets its own
 * `decode_<type>_vN` / `encode_<type>_vN` pair.
 *
 * Route OldSchool era questions through RSCache_RevisionAtLeastOsrs and RS2
 * questions through RSCache_RevisionAtLeastRs2 — never bare `revision >= N`.
 */

enum RSCache_Game
{
    /** Identity not stated. A memset-zeroed profile is honestly unidentified. */
    RSCACHE_GAME_UNSET = 0,
    RSCACHE_GAME_OLDSCHOOL = 1,
    RSCACHE_GAME_RS2 = 2,
};

/**
 * On-disk container family.
 *
 * Stated, never detected: a dat1 cache and a dat2 cache are different files,
 * but which lineage's field layouts they carry is not visible from the
 * container alone (OSRS and RS2 both use dat2).
 */
enum RSCache_Epoch
{
    RSCACHE_EPOCH_UNSET = 0,
    /** Jagfile era: main_file_cache.dat + .idx1..5, versionlist. */
    RSCACHE_EPOCH_DAT1 = 1,
    /** JS5: main_file_cache.dat2 + .idx0..N, reference table 255. */
    RSCACHE_EPOCH_DAT2 = 2,
};

/** Per-datatype slot for `codec[]` and `group_revision[]`. */
enum RSCache_Type
{
    RSCACHE_TYPE_OBJ = 0,
    RSCACHE_TYPE_NPC,
    RSCACHE_TYPE_LOC,
    RSCACHE_TYPE_SEQUENCE,
    RSCACHE_TYPE_SPOTANIM,
    RSCACHE_TYPE_IDK,
    RSCACHE_TYPE_UNDERLAY,
    RSCACHE_TYPE_OVERLAY,
    RSCACHE_TYPE_ENUM,
    RSCACHE_TYPE_STRUCT,
    RSCACHE_TYPE_PARAM,
    RSCACHE_TYPE_MAPELEMENT,
    RSCACHE_TYPE_DBROW,
    RSCACHE_TYPE_DBTABLE,
    RSCACHE_TYPE_VARBIT,
    RSCACHE_TYPE_VARPLAYER,
    RSCACHE_TYPE_VARCLIENT,
    RSCACHE_TYPE_VARCLIENT_STRING,
    RSCACHE_TYPE_INV,
    RSCACHE_TYPE_HITSPLAT,
    RSCACHE_TYPE_HEALTHBAR,
    RSCACHE_TYPE_COMPONENT,
    RSCACHE_TYPE_MODEL,
    RSCACHE_TYPE_FRAME,
    RSCACHE_TYPE_FRAMEMAP,
    RSCACHE_TYPE_ANIMAYA,
    RSCACHE_TYPE_SPRITE,
    RSCACHE_TYPE_FONT,
    RSCACHE_TYPE_TEXTURE,
    RSCACHE_TYPE_MAP_TERRAIN,
    RSCACHE_TYPE_MAP_LOCS,
    RSCACHE_TYPE_MAPSQUARES,
    RSCACHE_TYPE_CLIENTSCRIPT,
    RSCACHE_TYPE_WORLDMAP,
    RSCACHE_TYPE_COUNT,
};

/** Quirks that are client-build specific rather than revision ordered, so no
 *  revision comparison can imply them. */
#define RSCACHE_QUIRK_NONE 0u
/** Kronos client: loc opcodes 78/79 omit the ambient_sound_retain byte that
 *  stock clients of the same revision write. */
#define RSCACHE_QUIRK_KRONOS 0x1u
/** Void RS 634: map locs were pre-decrypted (RemoveXteas); no xteas.json. */
#define RSCACHE_QUIRK_VOID_RS634_NO_XTEAS 0x2u

/** No game revision known. Datatype flag functions then fall back to their
 *  per-group archive-revision thresholds. No shipped manifest reaches this:
 *  identity is stated in full. */
#define RSCACHE_REVISION_UNKNOWN 0

/** No archive revision recorded for a group. */
#define RSCACHE_GROUP_REVISION_UNKNOWN (-1)

/** Codec version "derive it from the profile" — the default for every slot. */
#define RSCACHE_CODEC_AUTO 0

struct RSCache
{
    /** enum RSCache_Game — which lineage `revision` is numbered in. */
    int game;
    /** enum RSCache_Epoch — on-disk container (dat1 | dat2). */
    int epoch;
    /** Game revision in `game`'s lineage (230, 254, 643, ...), or
     *  RSCACHE_REVISION_UNKNOWN. Authoritative when set: it comes from the
     *  boot manifest, not from guessing at a counter. */
    int revision;
    /** RSCACHE_QUIRK_* bitmask. */
    uint32_t quirks;
    /** JS5 archive revision per datatype group, or
     *  RSCACHE_GROUP_REVISION_UNKNOWN. Only consulted when `revision` is
     *  unknown. Filled in by the caller as archives are loaded. */
    int32_t group_revision[RSCACHE_TYPE_COUNT];
    /** Explicit per-datatype codec version, or RSCACHE_CODEC_AUTO. A revision
     *  module sets only the slots where the derivation rule would be wrong. */
    int16_t codec[RSCACHE_TYPE_COUNT];
};

/** A profile with nothing known: every identity field UNSET, no quirks, every
 *  group revision unknown and every codec on AUTO. Every revision module starts
 *  from this so a new field defaults sensibly everywhere at once. Unlike the
 *  previous "OSRS dat2 by default", an unset profile is honestly unidentified. */
struct RSCache
RSCache_ProfileZero(void);

/** True when game and epoch are both stated (revision may still be unknown). */
static inline bool
RSCache_ProfileIsIdentified(const struct RSCache* cache)
{
    return cache && cache->game != RSCACHE_GAME_UNSET && cache->epoch != RSCACHE_EPOCH_UNSET;
}

/** Record the JS5 archive revision for one datatype group. */
void
RSCache_ProfileSetGroupRevision(
    struct RSCache* cache,
    enum RSCache_Type type,
    int32_t archive_revision);

/** The archive revision recorded for `type`, or RSCACHE_GROUP_REVISION_UNKNOWN. */
int32_t
RSCache_GroupRevision(
    const struct RSCache* cache,
    enum RSCache_Type type);

/**
 * Does this cache's `type` records use the layout introduced at **OldSchool** game
 * revision `game_rev` or later?
 *
 * This is the single place the "what does a revision number mean" ambiguity is
 * resolved for the OldSchool lineage:
 *
 *  1. If the cache is OldSchool and carries a game revision, compare against it.
 *  2. Otherwise compare the group's JS5 archive revision against
 *     `archive_rev_threshold`. Pass RSCACHE_GROUP_REVISION_UNKNOWN when no such
 *     constant is known for the group.
 *  3. With neither available, answer `default_when_unknown`.
 *
 * ## Why the name says Osrs
 *
 * Revision numbers are **not one sequence**. `(rs2, dat1)`, `(rs2, dat2)` and
 * `(oldschool, dat2)` are independent lineages — OSRS rev N ≠ RS2 rev N — so a
 * bare `revision >= 220` is only meaningful once you know which lineage
 * `revision` is from. Every threshold routed through this function is an
 * OldSchool revision; an RS2 profile must not satisfy any of them. The name
 * carries the invariant so a future non-OSRS gate reaches for
 * RSCache_RevisionAtLeastRs2 instead of widening this.
 */
bool
RSCache_RevisionAtLeastOsrs(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int game_rev,
    int32_t archive_rev_threshold,
    bool default_when_unknown);

/**
 * Does this cache's `type` records use the layout introduced at **RS2** game
 * revision `game_rev` or later?
 *
 * Same three-step ladder as RSCache_RevisionAtLeastOsrs, but step 1 requires
 * `game == RSCACHE_GAME_RS2`. Use for thresholds like 474/537/555/582/629 that
 * are RuneScape numbers and mean nothing on the OldSchool line.
 */
bool
RSCache_RevisionAtLeastRs2(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int game_rev,
    int32_t archive_rev_threshold,
    bool default_when_unknown);

/** Explicit codec version for `type`, or `derived` when the slot is on AUTO. */
static inline int
RSCache_CodecVersionOr(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int derived)
{
    assert(cache);
    int16_t explicit_version = cache->codec[type];
    return explicit_version == RSCACHE_CODEC_AUTO ? derived : (int)explicit_version;
}

/**
 * Where a record type lives, and how a record id splits into an archive and a file.
 *
 * OSRS keeps most types as one config group holding every record as a file, so the id *is*
 * the file id. RS2 dat2 promotes several types to their own table and shards them into
 * groups, so an id has to be split — and the shard width is per type, not uniform:
 *
 *   loc  table 16, 256 files per group -> archive = id >> 8, file = id & 0xFF
 *   npc  table 18, 128 files per group -> archive = id >> 7, file = id & 0x7F
 *   obj  table 19, 256 files per group -> archive = id >> 8, file = id & 0xFF
 *
 * Measured against cache.rs643 and matching void's DefinitionDecoder subclasses.
 * A single hardcoded `>> 8` would silently mis-address every npc.
 *
 * `group_shift == 0` means "not sharded": the config-group layout, where `group` is the
 * config kind and the id indexes files directly.
 */
struct RSCache_RecordAddress
{
    /** enum RSCache_Dat2Table — the table the records live in, by role. Logical, not an
     *  on-disk id: the id differs between branches (locs are table 16 in RS2 and have no
     *  table at all in OldSchool), so it is settled by whoever holds the open cache, via
     *  RSCache_Dat2DiskTableId. */
    int table;
    /** Config-kind / group id when not sharded; ignored when `group_shift` is non-zero. */
    int group;
    /** Bits to shift an id right to get its group. 0 = not sharded. */
    int group_shift;
    /** Mask for the file index within a group. */
    int file_mask;
};

/**
 * How to address `type` in this cache. Falls back to the OSRS config-group layout for any
 * type or era without a sharded mapping, which is what every existing caller already does.
 */
struct RSCache_RecordAddress
RSCache_RecordAddressFor(
    const struct RSCache* cache,
    enum RSCache_Type type);

static inline bool
RSCache_IsDat1(const struct RSCache* cache)
{
    return cache && cache->epoch == RSCACHE_EPOCH_DAT1;
}

static inline bool
RSCache_IsOsrs(const struct RSCache* cache)
{
    return cache && cache->game == RSCACHE_GAME_OLDSCHOOL;
}

/** RS2 on the dat2 container — the layout family formerly called "epoch 643".
 *  The `epoch == DAT2` conjunct is load-bearing: dat1 profiles are also
 *  `game == RS2` and must not take the dat2 RS2 codecs. */
static inline bool
RSCache_IsRs2Dat2(const struct RSCache* cache)
{
    return cache && cache->game == RSCACHE_GAME_RS2 && cache->epoch == RSCACHE_EPOCH_DAT2;
}

static inline bool
RSCache_HasQuirk(
    const struct RSCache* cache,
    uint32_t quirk)
{
    return cache && (cache->quirks & quirk) != 0u;
}

/* --- identity name parsing (shared by manifest, CLI, boot log, harnesses) --- */

/** "oldschool" | "rs2" → enum, or RSCACHE_GAME_UNSET on unknown. */
int
RSCache_GameFromName(const char* name);

/** enum → "oldschool" | "rs2" | "unset". */
const char*
RSCache_GameName(int game);

/** "dat1" | "dat2" → enum, or RSCACHE_EPOCH_UNSET on unknown. */
int
RSCache_EpochFromName(const char* name);

/** enum → "dat1" | "dat2" | "unset". */
const char*
RSCache_EpochName(int epoch);

/** "none" | "kronos" | "void_rs634_no_xteas" (comma-separated) → quirks bitmask.
 *  Returns false on unknown token. */
bool
RSCache_QuirksFromList(
    const char* list,
    uint32_t* out_quirks);

/** quirks bitmask → "none" | "kronos" | "void_rs634_no_xteas" (or comma-joined).
 *  Writes into `buf`. */
void
RSCache_QuirksName(
    uint32_t quirks,
    char* buf,
    int buf_size);

#endif
