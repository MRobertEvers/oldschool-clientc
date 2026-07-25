#ifndef RSCACHE_PROFILE_H
#define RSCACHE_PROFILE_H

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
 * revision can, and callers usually know it: it is in the boot manifest and in
 * the login handshake. This struct carries it, and keeps the per-group archive
 * revisions alongside for caches that arrive without one.
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
 * Both should route era questions through RSCache_RevisionAtLeastOsrs rather than
 * comparing raw revisions, so the timestamp ambiguity is resolved in exactly one
 * place.
 */

enum RSCache_Game
{
    RSCACHE_GAME_OLDSCHOOL = 0,
    RSCACHE_GAME_RS2 = 1,
};

/** On-disk container family. */
enum RSCache_Container
{
    /** JS5: main_file_cache.dat2 + .idx0..N, reference table 255. */
    RSCACHE_CONTAINER_DAT2 = 0,
    /** Jagfile era: main_file_cache.dat + .idx1..5, versionlist. */
    RSCACHE_CONTAINER_DAT1 = 1,
};

/**
 * Field-layout family.
 *
 * Distinct from the revision because it is *not* derivable from one: 643-era
 * caches number their reference tables in the same small-integer range OSRS used
 * before it switched to timestamps, so the two families are indistinguishable by
 * revision alone. Whoever opens the cache has to say which it is.
 */
enum RSCache_Epoch
{
    /** Jagfile-era layouts (newline-terminated strings, narrow ids). */
    RSCACHE_EPOCH_DAT1_CLASSIC = 0,
    /** OldSchool RuneScape, 2013 onwards. */
    RSCACHE_EPOCH_OSRS = 1,
    /** The 643 / RuneScape-2 branch. */
    RSCACHE_EPOCH_643 = 2,
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
/** Kronos client: loc opcodes 78/79 omit the ambient_sound_retain byte that
 *  stock clients of the same revision write. */
#define RSCACHE_QUIRK_KRONOS 0x1u

/** No game revision known. Datatype flag functions then fall back to their
 *  per-group archive-revision thresholds. */
#define RSCACHE_REVISION_UNKNOWN 0

/** No archive revision recorded for a group. */
#define RSCACHE_GROUP_REVISION_UNKNOWN (-1)

/** Codec version "derive it from the profile" — the default for every slot. */
#define RSCACHE_CODEC_AUTO 0

struct RSCache
{
    /** enum RSCache_Game */
    int game;
    /** enum RSCache_Container */
    int container;
    /** enum RSCache_Epoch */
    int epoch;
    /** Game revision (230, 233, 254, ...), or RSCACHE_REVISION_UNKNOWN.
     *  Authoritative when set: it comes from the boot manifest or the login
     *  handshake, not from guessing at a counter. */
    int version;
    /** RSCACHE_QUIRK_* bitmask. */
    uint32_t quirks;
    /** JS5 archive revision per datatype group, or
     *  RSCACHE_GROUP_REVISION_UNKNOWN. Only consulted when `version` is unknown.
     *  Filled in by the caller as archives are loaded, since each group's
     *  revision only becomes known when its reference table is read. */
    int32_t group_revision[RSCACHE_TYPE_COUNT];
    /** Explicit per-datatype codec version, or RSCACHE_CODEC_AUTO. A revision
     *  module sets only the slots where the derivation rule would be wrong. */
    int16_t codec[RSCACHE_TYPE_COUNT];
};

/** A profile with nothing known: OSRS dat2, unknown revision, no quirks, every
 *  group revision unknown and every codec on AUTO. Every revision module starts
 *  from this so a new field defaults sensibly everywhere at once. */
struct RSCache
RSCache_ProfileZero(void);

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
 * resolved, and the only era predicate datatypes should use:
 *
 *  1. If the cache is OSRS-epoch and carries a game revision, compare against it.
 *     Unambiguous.
 *  2. Otherwise compare the group's JS5 archive revision against
 *     `archive_rev_threshold` — the value the reference client gates the same
 *     field on. Pass RSCACHE_GROUP_REVISION_UNKNOWN when no such constant is
 *     known for the group.
 *  3. With neither available, answer `default_when_unknown`. Callers pick the
 *     branch that has been observed to decode the local caches, so behaviour on
 *     an unidentified cache is a deliberate choice rather than an accident.
 *
 * ## Why the name says Osrs
 *
 * Revision numbers are **not one sequence**. dat1 caches are numbered in a 2004-era
 * lineage that reaches 254; OldSchool restarted from 1 in 2013 and is now in the 230s.
 * The two ranges overlap and mean completely different things, so a bare
 * `version >= 220` is only meaningful once you know which lineage `version` is from.
 *
 * Every threshold in this library is an OldSchool revision, so a **dat1 profile must
 * not satisfy any of them** — rev 254 is older than OSRS 220, not newer. This function
 * enforces that by skipping step 1 for a non-OSRS epoch, and the name carries the
 * invariant so a future dat1-era gate does not reach for the wrong predicate. If one
 * is ever needed, add a sibling rather than widening this.
 */
bool
RSCache_RevisionAtLeastOsrs(
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
    if( !cache )
        return derived;
    int16_t explicit_version = cache->codec[type];
    return explicit_version == RSCACHE_CODEC_AUTO ? derived : (int)explicit_version;
}

/**
 * Where a record type lives, and how a record id splits into an archive and a file.
 *
 * OSRS keeps most types as one config group holding every record as a file, so the id *is*
 * the file id. RS2 promotes several types to their own table and shards them into groups, so
 * an id has to be split — and the shard width is per type, not uniform:
 *
 *   loc  table 16, 256 files per group -> archive = id >> 8, file = id & 0xFF
 *   npc  table 18, 128 files per group -> archive = id >> 7, file = id & 0x7F
 *   obj  table 19, 256 files per group -> archive = id >> 8, file = id & 0xFF
 *
 * Measured against cache.rs643 and matching void's DefinitionDecoder subclasses, which
 * declare exactly these overrides (`getArchive`/`getFile`). A single hardcoded `>> 8` would
 * silently mis-address every npc.
 *
 * `group_shift == 0` means "not sharded": the config-group layout, where `group` is the
 * config kind and the id indexes files directly.
 */
struct RSCache_RecordAddress
{
    /** Disk table the records live in. */
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
    return cache && cache->container == RSCACHE_CONTAINER_DAT1;
}

static inline bool
RSCache_HasQuirk(
    const struct RSCache* cache,
    uint32_t quirk)
{
    return cache && (cache->quirks & quirk) != 0u;
}

#endif
