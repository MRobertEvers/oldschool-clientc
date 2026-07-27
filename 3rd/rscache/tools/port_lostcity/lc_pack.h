#ifndef RSCACHE_TOOLS_LC_PACK_H
#define RSCACHE_TOOLS_LC_PACK_H

/*
 * LostCity `content/pack/*.pack` files: `id=name` per line, sparse, sorted by id
 * on write.
 *
 * These are the id authority for a LostCity build. Nothing in the content tree
 * carries a numeric id — configs, models and maps are all named — so an asset
 * only exists once a pack line maps its name to an id. The engine's own
 * PackFile.save() writes them sorted, so this does too; a diff against the
 * original then shows only the appended lines.
 */

struct LC_Pack
{
    /** Pack basename, e.g. "npc" for `pack/npc.pack`. */
    const char* type;
    /** id -> name, sparse: NULL for ids the pack does not list. */
    char** names;
    int capacity;
    /** One past the highest listed id, matching PackFile.max. */
    int max;
    /** Ids allocated during this run, for the report. */
    int added;
};

enum LC_PackKind
{
    LC_PACK_NPC,
    LC_PACK_SEQ,
    LC_PACK_SPOTANIM,
    LC_PACK_LOC,
    LC_PACK_MODEL,
    LC_PACK_ANIM,
    LC_PACK_ANIMSET,
    LC_PACK_MAP,
    LC_PACK_COUNT
};

struct LC_Packs
{
    struct LC_Pack packs[LC_PACK_COUNT];
};

/** Load every pack this tool touches from `<content_dir>/pack`. */
int
lc_packs_load(
    struct LC_Packs* packs,
    const char* content_dir);

void
lc_packs_free(struct LC_Packs* packs);

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
 */
int
lc_pack_alloc(
    struct LC_Pack* pack,
    const char* name);

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
