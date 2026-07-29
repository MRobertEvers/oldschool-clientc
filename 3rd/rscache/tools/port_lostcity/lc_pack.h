#ifndef RSCACHE_TOOLS_LC_PACK_H
#define RSCACHE_TOOLS_LC_PACK_H

/*
 * LostCity `content/pack/<type>.pack` files: `id=name` per line, sparse, sorted by id
 * on write.
 *
 * These are the id authority for a LostCity build. Nothing in the content tree
 * carries a numeric id — configs, models and maps are all named — so an asset
 * only exists once a pack line maps its name to an id. The engine's own
 * PackFile.save() writes them sorted, so this does too; a diff against the
 * original then shows only the appended lines.
 *
 * Two layers: `lc_pack_*` operates on a single pack file by path and knows
 * nothing about which types exist, which is what lets the `packfile` tool patch
 * `obj.pack` or `varp.pack` — types `port_lostcity` never writes. `lc_packs_*`
 * is the fixed set of eleven packs the exporter touches.
 */

struct LC_Pack
{
    /** Pack basename, e.g. "npc" for `pack/npc.pack`. Owned, so a type named on
     *  a command line does not have to outlive the pack. */
    char type[32];
    /** id -> name, sparse: NULL for ids the pack does not list. */
    char** names;
    int capacity;
    /** One past the highest listed id, matching PackFile.max. */
    int max;
    /** Ids allocated during this run, for the report. */
    int added;
    /** Ids removed during this run, for the report. */
    int removed;
};

enum LC_PackKind
{
    LC_PACK_NPC,
    LC_PACK_OBJ,
    LC_PACK_SEQ,
    LC_PACK_SPOTANIM,
    LC_PACK_LOC,
    LC_PACK_MODEL,
    LC_PACK_ANIM,
    LC_PACK_ANIMSET,
    LC_PACK_BASE,
    LC_PACK_MAP,
    LC_PACK_FLO,
    LC_PACK_TEXTURE,
    LC_PACK_COUNT
};

struct LC_Packs
{
    struct LC_Pack packs[LC_PACK_COUNT];
};

/* ---- one pack file ------------------------------------------------------- */

/**
 * Load `path` into `pack`, labelling it `type`.
 *
 * `allow_missing` treats a nonexistent file as an empty pack, which is how a
 * type that has no pack file yet gets its first line. A file that exists but
 * cannot be read is always a failure.
 */
int
lc_pack_load(
    struct LC_Pack* pack,
    const char* path,
    const char* type,
    int allow_missing);

/** Write `pack` to `path`, one `id=name` line per listed id, ascending. */
int
lc_pack_save(
    const struct LC_Pack* pack,
    const char* path);

/**
 * The same, omitting every line whose name is `<type>_<id>`.
 *
 * Such a name carries no information: it is the id, spelled twice, and
 * `lc_pack_synthetic_id` reconstructs the mapping from the name alone. Storing
 * it cost a real tree 93,000 of its 306,818 pack lines — 20 of 39 files were
 * *entirely* this — and made every pack look authored when most held nothing.
 *
 * Returns 0 on failure, and deletes `path` rather than writing an empty file
 * when nothing survives: a pack with no real names is not an empty namespace,
 * it is a namespace the cache does not name, and the two should not look alike.
 */
int
lc_pack_save_sparse(
    const struct LC_Pack* pack,
    const char* path);

/**
 * Id encoded in a synthetic `<type>_<id>` name, or -1.
 *
 * The inverse of what `cp_name_ensure` writes, so a record whose name was never
 * anything but its id resolves with no pack line to back it.
 */
int
lc_pack_synthetic_id(
    const char* type,
    const char* name);

void
lc_pack_free(struct LC_Pack* pack);

/** Bind `id` to `name`, replacing whatever the id held. Returns 0 on failure. */
int
lc_pack_set(
    struct LC_Pack* pack,
    int id,
    const char* name);

/**
 * Drop `id` from the pack. Returns 0 when the id was not listed.
 *
 * `max` is recomputed, because the engine derives it from the file it reads
 * back — leaving it high would hand the next allocation an id past the end of
 * the written file, which is a hole no line accounts for.
 */
int
lc_pack_remove(
    struct LC_Pack* pack,
    int id);

/** Id for `name`, or -1 when the pack does not list it. */
int
lc_pack_find(
    const struct LC_Pack* pack,
    const char* name);

/**
 * Id for `name`, appending it at the next free id when absent.
 *
 * Returns the existing id when the name is already listed, which is what makes
 * a re-run idempotent and lets several exporters ask for the same shared asset.
 * Returns -1 only on allocation failure.
 *
 * Appends at `max` rather than filling the first hole: a gap in a pack is
 * usually a deliberately retired id, and re-using it would silently rebind
 * every reference an old build still holds.
 */
int
lc_pack_alloc(
    struct LC_Pack* pack,
    const char* name);

/* ---- the exporter's fixed set -------------------------------------------- */

/** Load every pack this tool touches from `<content_dir>/pack`. */
int
lc_packs_load(
    struct LC_Packs* packs,
    const char* content_dir);

void
lc_packs_free(struct LC_Packs* packs);

/**
 * Write every pack back to `<content_dir>/pack`, sorted by id.
 *
 * Writes in place, over the files `lc_packs_load` read. Staging them elsewhere
 * for a later copy would let the ids the exported configs were written against
 * drift from the ids the build actually reads.
 */
int
lc_packs_write(
    const struct LC_Packs* packs,
    const char* content_dir);

#endif
