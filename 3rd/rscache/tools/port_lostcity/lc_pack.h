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
 *
 * ## One file, hand-owned and machine-written
 *
 * There used to be two files per namespace — `pack/` for what the cache's own
 * gameval table said and `names/` for what a human wrote — because a save
 * emitted nothing but `id=name` lines and so destroyed every comment in the file
 * it rewrote. That cost was measured: `pack/param.pack` lost all 58 of its
 * header lines to one `cachepack unpack`, and that header was the record of
 * which param-id claims had been checked against a cache and how.
 *
 * The split is gone (docs/CONTENT_PACK_PLAN.md §3) because the save path grew
 * the two properties that made it unnecessary:
 *
 *   comments are data     A load captures the header, the blank lines, the
 *                         standalone comment blocks and the trailing notes, and
 *                         a save writes them back. `lc_pack_note` reads one.
 *
 *   a save is a merge     An id on disk that the in-memory pack never heard of
 *                         is *preserved*, not dropped. A tool that regenerates
 *                         one namespace from one source therefore cannot delete
 *                         a name it does not know about.
 *
 * The second rule needs the first exception stated explicitly, or retiring an id
 * would be impossible: `lc_pack_remove` records a tombstone, and a merging save
 * honours it. Deletion is something you say, never something inferred from a
 * name's absence.
 */

/**
 * One pack file in memory, comments and all.
 *
 * `names`, `trailing` and `preceding` are parallel arrays over id, all of length
 * `capacity` and all sparse. A comment array entry is meaningful only where
 * `names[id]` is non-NULL, because a comment is anchored to the id that follows
 * it and an id with no name is not in the file.
 */
struct LC_Pack
{
    /** Pack basename, e.g. "npc" for `pack/npc.pack`. Owned, so a type named on
     *  a command line does not have to outlive the pack. */
    char type[32];
    /** id -> name, sparse: NULL for ids the pack does not list. */
    char** names;
    /**
     * The `//` comment trailing `id=name`, stored raw.
     *
     * Raw means from the first `//` to end of line, *including* the whitespace
     * that separated it from the name and excluding the newline — so a save
     * reproduces the line byte for byte instead of re-deciding how much padding
     * a note gets. Use `lc_pack_note` to read the text and `lc_pack_set_note` to
     * write one; parallel to `names`, NULL where a line carries no comment.
     */
    char** trailing;
    /**
     * Whole-line comments and blank lines appearing immediately before an id.
     *
     * One string per id, holding however many lines were found, each with its
     * newline. Anchoring to *the id that follows* is what lets the file stay
     * sorted by id on write while a per-id justification stays attached to what
     * it justifies.
     */
    char** preceding;
    int capacity;
    /** One past the highest listed id, matching PackFile.max. */
    int max;
    /** Ids allocated during this run, for the report. */
    int added;
    /** Ids removed during this run, for the report. */
    int removed;
    /**
     * The comment block before the first id line — the file header.
     *
     * Separate from `preceding` because a header is about the file, not about
     * whichever id happens to be first; keeping it out of the anchoring rule is
     * what makes `param.pack`'s 58-line header survive an id being inserted
     * above everything else.
     */
    char* preamble;
    /** The comment block after the last id line, which anchors to no id. */
    char* trailer;
    /** Ids `lc_pack_remove` dropped. A merging save must not resurrect them. */
    int* tombstones;
    int tombstone_count;
    int tombstone_capacity;
    /**
     * Lines that were neither a comment nor a valid `id=name`.
     *
     * Skipped, exactly as the engine's own loader skips them, but counted and
     * reported — a malformed pack should be visible rather than silently
     * thinned.
     */
    int malformed;
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
 *
 * Comments are captured rather than discarded; see `struct LC_Pack`.
 */
int
lc_pack_load(
    struct LC_Pack* pack,
    const char* path,
    const char* type,
    int allow_missing);

/**
 * Write `pack` to `path`, one `id=name` line per listed id, ascending.
 *
 * **This is a merge.** The file already at `path` is read first: its header,
 * trailing block, comments and any id the in-memory pack does not list are all
 * carried through. Ids `lc_pack_remove` dropped are the only ones that go away.
 *
 * The write is skipped entirely when the bytes would be identical to what is
 * already there, so an unchanged pack keeps its mtime and reports nothing.
 * Returns 0 on failure.
 */
int
lc_pack_save(
    const struct LC_Pack* pack,
    const char* path);

/**
 * The same, without the per-write report line.
 *
 * For a *member* pack — `textures/texture_0.compack`, one per archive — the report
 * is noise rather than news: 969 interfaces mean 969 lines saying a file with no
 * changes in it did not change. The namespace packs still report, because there are
 * a few dozen of them and a write to one is worth seeing.
 */
int
lc_pack_save_quiet(
    const struct LC_Pack* pack,
    const char* path);

/**
 * The same, omitting every line whose name is `<type>_<id>` and which carries no
 * comment.
 *
 * Such a name carries no information: it is the id, spelled twice, and
 * `lc_pack_synthetic_id` reconstructs the mapping from the name alone. Storing
 * it cost a real tree 93,000 of its 306,818 pack lines — 20 of 39 files were
 * *entirely* this — and made every pack look authored when most held nothing.
 *
 * The comment exception is not a detail: a filler id someone wrote a note about
 * is an id someone examined, and omitting the line would throw the note away
 * along with it.
 *
 * Returns 0 on failure, and deletes `path` rather than writing an empty file
 * when nothing at all survives: a pack with no real names is not an empty
 * namespace, it is a namespace the cache does not name, and the two should not
 * look alike. A file holding only a header is kept — prose is content.
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

/** Bind `id` to `name`, replacing whatever the id held. Returns 0 on failure.
 *
 *  Comments on the id are left alone: a rename is not a retraction of what
 *  someone wrote about the id, and the two are edited independently. */
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
 *
 * Records a tombstone, so the merging save does not read the id back out of the
 * file it is overwriting.
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
 * The text of `id`'s trailing comment, or NULL.
 *
 * The `//` and the whitespace around it are stripped, so the convention
 * `3254=guard  // cache: guard1` reads back as `cache: guard1`.
 */
const char*
lc_pack_note(
    const struct LC_Pack* pack,
    int id);

/**
 * Set (or with NULL, clear) `id`'s trailing comment from its text.
 *
 * Emitted as `id=name  // text`. Returns 0 on failure.
 */
int
lc_pack_set_note(
    struct LC_Pack* pack,
    int id,
    const char* text);

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

/**
 * The same, but never below `base`.
 *
 * For a namespace whose ids the *client's cache* fixes: a new record has to take
 * a number the frozen cache does not use, and `base` is that declared floor. With
 * the cache pinned (docs/CONTENT_PACK_PLAN.md §0, decision 1) the floor is a
 * constant, so this needs no headroom check — just somewhere to start.
 */
int
lc_pack_alloc_from(
    struct LC_Pack* pack,
    const char* name,
    int base);

/** How many listed names are not `<type>_<id>` filler — i.e. how many ids
 *  someone has actually named. The numerator of a coverage report. */
int
lc_pack_named_count(const struct LC_Pack* pack);

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
