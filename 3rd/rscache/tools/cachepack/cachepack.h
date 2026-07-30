#ifndef RSCACHE_TOOLS_CACHEPACK_H
#define RSCACHE_TOOLS_CACHEPACK_H

/*
 * cachepack — an OSRS (dat2) cache unpacker and packer.
 *
 * The architecture is LostCity_Server's, transplanted onto rscache:
 *
 *   engine/tools/unpack/config/Unpack.ts   -> cp_unpack.c  (driver)
 *   engine/tools/pack/PackAll.ts           -> cp_pack.c    (driver)
 *   engine/tools/pack/PackFileBase.ts      -> lc_pack.c    (id <-> name, reused)
 *   engine/tools/pack/config/PackShared.ts -> cp_text.c    (text format)
 *   engine/tools/unpack/config/NpcConfig.ts \
 *   engine/tools/pack/config/NpcConfig.ts   -> config/cp_npc.c (one file per type)
 *
 * Two departures from the reference, both deliberate.
 *
 * **The opcode readers and writers are rscache's, not this tool's.** LostCity
 * hand-writes three mirrors of every record layout (an unpacker, a validator and a
 * packer) and they can drift; here both directions pass through the library's
 * decoder and encoder structs, which are the ones the client uses and the ones the
 * round-trip suite already holds to exact consumption. A type this tool can express
 * is therefore a type the client can read, by construction. What is left per type is
 * only the mapping between a struct and its text — half the code and none of the
 * opportunity for the two directions to disagree about a field's width.
 *
 * **Names come from the cache.** LostCity has no name table so it invents
 * `npc_1234`; an OldSchool cache ships table 24, the gameval index, which is the
 * content team's own names for objs, npcs, locs, seqs, spotanims, invs, varps,
 * varbits and db rows/tables. `cachepack unpack` seeds the pack files from it, so a
 * record comes out as `[goblin]` rather than `[npc_3028]`. Types the gameval index
 * does not cover fall back to `<type>_<id>`, exactly as the reference does.
 *
 * The osrs230 type set is much wider than rev 254's: params, structs, enums, dbrow,
 * dbtable, healthbar, hitsplat, map elements, inv, the three var families and the
 * underlay/overlay split are all new. See cp_types.c for the register, which records
 * per type what round-trips and what does not.
 */

#include "rscache.h"

#include "asset_access.h"
#include "cp_text.h"
#include "lc_pack.h"

#include <stdbool.h>
#include <stdint.h>

/* ---- type register ------------------------------------------------------ */

/**
 * One slot per config type this tool knows, and the index into CP_Names' packs.
 *
 * Order is the order `unpack` writes and `pack` reads. It matters for packing:
 * a type whose text references another by name needs that type's pack loaded
 * first, and the register is walked in declaration order.
 */
enum CP_TypeId
{
    CP_TYPE_UNDERLAY = 0,
    CP_TYPE_OVERLAY,
    CP_TYPE_IDK,
    CP_TYPE_INV,
    CP_TYPE_LOC,
    CP_TYPE_ENUM,
    CP_TYPE_NPC,
    CP_TYPE_OBJ,
    CP_TYPE_PARAM,
    CP_TYPE_SEQ,
    CP_TYPE_SPOTANIM,
    CP_TYPE_VARBIT,
    CP_TYPE_VARP,
    CP_TYPE_VARC,
    CP_TYPE_HITSPLAT,
    CP_TYPE_HEALTHBAR,
    CP_TYPE_STRUCT,
    CP_TYPE_MAPELEMENT,
    CP_TYPE_DBROW,
    CP_TYPE_DBTABLE,
    CP_TYPE_COUNT
};

/**
 * One slot per non-config table the asset tree lays out, and the index into
 * CP_Names' asset packs. See cp_assets.h for what each one is and why its
 * extension is what it is.
 */
enum CP_AssetId
{
    CP_ASSET_FRAME = 0,
    CP_ASSET_FRAMEMAP,
    CP_ASSET_INTERFACE,
    CP_ASSET_SYNTH,
    CP_ASSET_MAP,
    CP_ASSET_SONG,
    CP_ASSET_MODEL,
    CP_ASSET_SPRITE,
    CP_ASSET_TEXTURE,
    CP_ASSET_BINARY,
    CP_ASSET_JINGLE,
    CP_ASSET_SCRIPT,
    CP_ASSET_FONT,
    CP_ASSET_SAMPLE,
    CP_ASSET_PATCH,
    CP_ASSET_WORLDMAP_GEOGRAPHY,
    CP_ASSET_WORLDMAP_AREA,
    CP_ASSET_WORLDMAP_GROUND,
    CP_ASSET_DBINDEX,
    CP_ASSET_ANIMAYA,
    CP_ASSET_COUNT
};

/** The type carries fields the decoder consumes without storing, so a repack is a
 *  valid record with less in it than the source had. Reported, never silent. */
#define CP_TYPE_LOSSY 0x1
/** rscache has a decoder but no encoder: unpack works, pack refuses. */
#define CP_TYPE_NO_ENCODER 0x2

struct CP_Ctx;

struct CP_Type
{
    /** Source file extension and pack basename, e.g. "npc" -> `all.npc`,
     *  `configs/all.npc.compack`. */
    const char* name;
    /** Which rscache datatype, for RSCache_RecordAddressFor. */
    enum RSCache_Type rs_type;
    /** Group id inside the config table (enum RSCache_Dat2ConfigKind). */
    int config_kind;
    /** Archive id in the gameval table, or -1 when the cache does not name it. */
    int gameval_archive;
    unsigned flags;

    /** Decode one record and push its lines. Returns 1 on success. */
    int (*unpack)(
        struct CP_Ctx* ctx,
        int id,
        const uint8_t* record,
        int record_size,
        struct CP_Lines* out);

    /**
     * Encode one record from its lines into `out` (capacity `out_capacity`).
     * Returns bytes written, or 0 on failure (message already printed).
     */
    uint32_t (*pack)(
        struct CP_Ctx* ctx,
        int id,
        const struct CP_Config* config,
        uint8_t* out,
        uint32_t out_capacity);
};

const struct CP_Type*
cp_type(enum CP_TypeId id);

/** Look a type up by its source name, or -1. */
int
cp_type_by_name(const char* name);

/* ---- names -------------------------------------------------------------- */

/*
 * One pack file per namespace.
 *
 * There used to be two arrays per kind — `pack/<ns>.pack` for what the cache's
 * gameval table said and `names/<ns>.pack` for what a human wrote — and the reason
 * was entirely the save path: `cp_names_save` wrote the first straight back out,
 * so an authored name merged into it would be spliced into a file the next unpack
 * regenerates. That hazard is gone (docs/CONTENT_PACK_PLAN.md §1): a pack save now
 * preserves comments and *merges*, so re-seeding a namespace from the cache cannot
 * take an authored line with it. Hence one array.
 *
 * Two rules keep the merged file honest, and both are already where they need to
 * be rather than here:
 *
 *   - `cp_register_may_write_pack` refuses to write a namespace the tree declares
 *     authored, so `configs/all.param.compack` is never rewritten at all.
 *   - `seed_pack_from_gameval` never renames an id the pack already lists, so an
 *     authored name that replaces a cache name survives a re-seed.
 */
struct CP_Names
{
    struct LC_Pack packs[CP_TYPE_COUNT];
    /** Whether each pack was seeded from the cache's own gameval table. Recorded
     *  so the report can distinguish real content names from `<type>_<id>`. */
    bool from_gameval[CP_TYPE_COUNT];
    /**
     * The same, for the non-config tables the asset tree lays out.
     *
     * Kept separate from `packs` rather than appended to it because the two are
     * named from different sources — a config gets its name from the cache's
     * gameval index, an asset from the config that references it (models) or from
     * its id. Merging them would put both behind one lookup that has to guess
     * which it is holding.
     */
    struct LC_Pack asset_packs[CP_ASSET_COUNT];
    /**
     * The cache's own component names, keyed by `(interface << 16) | child`.
     *
     * Decoded from gameval archive 14, the same record that names interfaces, and
     * held here so the interface codec can use them as block names — which is what
     * lets `interfaces/<name>.compack` be the only index over an interface's
     * members.
     *
     * It used to be written straight out as `pack/component.pack` and never kept,
     * on the reasoning that nothing in cachepack resolves a component. True, but it
     * meant two files indexed the same members: the compack said `0=com_0` and
     * `component.pack` said `786432=bankmain:infinite`, one filler and one named.
     */
    struct LC_Pack components;
};

/** Load `<srcdir>/pack/<type>.pack` for every type; missing files are empty. */
int
cp_names_load(
    struct CP_Names* names,
    const char* srcdir);

int
cp_names_save(
    const struct CP_Names* names,
    const char* srcdir);

void
cp_names_free(struct CP_Names* names);

/**
 * Seed every pack that has a gameval archive from the open cache.
 *
 * A gameval archive is a file-per-id group of name strings. The mapping from
 * archive id to config type is not stated anywhere in the cache, so it is
 * **verified rather than trusted**: an archive is accepted only when every file id
 * it carries is also a record id in the config group it claims to name. A
 * mismatched archive is refused with a message and that type keeps synthetic
 * names, which is a legible fallback; accepting it would silently label every npc
 * with some other type's name.
 *
 * Ids the gameval archive does not cover get `<type>_<id>`.
 */
void
cp_names_seed_from_cache(
    struct CP_Ctx* ctx);

/**
 * Print, per namespace, how many ids carry a name someone chose.
 *
 * `<ns>_<id>` filler does not count — it is the id spelled twice, and counting it
 * would report full coverage of a table nobody has examined. Needs an open cache
 * for the denominator; a no-op without one.
 */
void
cp_names_report_coverage(
    struct CP_Ctx* ctx);

/** Name for `id`, or NULL when the pack does not list it. */
const char*
cp_name_get(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id);

/**
 * Name for `id`, registering `<type>_<id>` when absent.
 *
 * Every reachable id must end up named: an unnamed id has no way to be spelled in
 * the text, and a reference to it would have to fall back to a number, which the
 * reader cannot tell from a name.
 */
const char*
cp_name_ensure(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int id);

/** Id for `name`, or -1. */
int
cp_name_find(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* name);

/* The same three, over the asset packs. */

const char*
cp_asset_name_get(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id);

/** Name for `id`, registering `<dir>_<id>` when absent. */
const char*
cp_asset_name_ensure(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id);

int
cp_asset_name_find(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    const char* name);

/**
 * Bind `id` to `name`, uniquified against what the pack already holds.
 *
 * Used by the model renamer, where several configs can want the same name for
 * different models — `goblin` is an npc *and* the six models it is built from.
 */
void
cp_asset_name_set(
    struct CP_Ctx* ctx,
    enum CP_AssetId asset,
    int id,
    const char* name);

/* ---- context ------------------------------------------------------------ */

struct CP_Ctx
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache; /* open only for unpack/verify */
    bool cache_open;

    /**
     * Previous revision's cache, opened for LC-style unpack diffs.
     *
     * When set, unpack still rewrites `configs/all.<type>` from the *new* cache
     * (so the tree matches what the client ships) and additionally writes a
     * review queue under `configs/_unpack/<rev_name>/`: new ids and `.merge`
     * files for records whose text changed. See cp_unpack.c.
     */
    struct Tool_Dat2Cache compare;
    bool compare_open;
    struct RSCache compare_profile;

    /** `--rev` string, used as the `_unpack/<rev_name>/` directory name. */
    char rev_name[64];

    /**
     * `--cache`, so `unpack` can record *which* cache the tree was derived from.
     *
     * The tree is the source of truth and the cache it came from is archived
     * unchanged (docs/CONTENT_PACK_PLAN.md §0), which only works if "derived
     * from which one" is answerable later without guessing. Empty for `pack`.
     */
    char cache_dir[1024];

    struct CP_Names names;
    char srcdir[1024];

    /** Counted, not fatal: a record the decoder did not consume to the byte has
     *  fields this tool cannot see, and the count is the headline of the report. */
    int warn_short_decode;
    int warn_unknown_key;
    int warn_unresolved_name;
    /** Quiet down the per-record chatter after this many of each kind. */
    int warn_limit;
};

/** Warn at most `warn_limit` times per counter. Returns 1 when it printed. */
int
cp_warn(
    struct CP_Ctx* ctx,
    int* counter,
    const char* fmt,
    ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* ---- record access ------------------------------------------------------ */

/**
 * One config group, opened once and walked.
 *
 * A config type is a single archive holding one file per record, so the whole
 * type is one decompression. Loading per record instead would re-inflate a
 * 56,000-file archive once per loc.
 *
 * Only the unsharded (OldSchool config-group) layout is handled — the RS2 branch
 * puts these types in their own tables addressed by `id >> shift`, and this tool
 * is for OldSchool caches. A sharded profile is refused rather than mis-addressed.
 */
struct CP_Group
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    /** Record id per index, ascending as the container stores them. */
    const int* ids;
    int count;
};

int
cp_group_open(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    struct CP_Group* group);

/**
 * Open a config group from an arbitrary dat2 cache (main or compare).
 *
 * `disk_cache` must already be open and profiled. Used by unpack's diff path so
 * the previous revision can be walked without swapping `ctx->cache`.
 */
int
cp_group_open_disk(
    struct CP_Ctx* ctx,
    struct Tool_Dat2Cache* disk_cache,
    enum CP_TypeId type,
    struct CP_Group* group);

void
cp_group_free(struct CP_Group* group);

/** Bytes of the record at `index`, and its size. */
const uint8_t*
cp_group_record(
    const struct CP_Group* group,
    int index,
    int* out_size);

/**
 * Index of `id` in `group`, or -1. Ids are ascending as the container stores
 * them, so this is a binary search.
 */
int
cp_group_find_id(
    const struct CP_Group* group,
    int id);

/** Every record id present for `type`, ascending. Caller frees `*out_ids`. */
int
cp_record_ids(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int** out_ids,
    int* out_count);

/* ---- drivers ------------------------------------------------------------ */

struct CP_Selection
{
    /** NULL = every type. Otherwise a bitmask over enum CP_TypeId. */
    uint32_t mask;
    bool all;
};

int
cp_unpack_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel);

int
cp_pack_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* base_cache_dir,
    const char* out_cache_dir);

int
cp_verify_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel);

/**
 * Decode a record and encode it straight back, no text in between.
 *
 * `verify` runs this alongside the full trip so a loss can be attributed: the
 * library's codecs lose a documented amount (EXCEPTIONS.md B2/B3) and this tool
 * must not lose any more. NULL for the two decode-only types.
 */
typedef uint32_t (*cp_codec_fn)(
    struct CP_Ctx* ctx,
    const uint8_t* data,
    int size,
    uint8_t* out,
    uint32_t out_capacity);

cp_codec_fn
cp_codec_roundtrip(enum CP_TypeId type);

/* ---- binary tables ------------------------------------------------------ */

/**
 * Export whole tables as raw container payloads, and import them back.
 *
 * Configs are text because a config is a record with named fields. Models,
 * sprites, maps, scripts and the rest are not, and inventing a text form for them
 * would mean a decoder, an encoder and a fidelity claim per table. These go out as
 * the bytes the container holds, which is byte-exact by construction and is what
 * makes a full unpack/pack cycle reproduce a working cache.
 */
int
cp_binary_export(
    struct CP_Ctx* ctx,
    const char* tables_csv);

int
cp_binary_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir);

/**
 * Point one reference-table entry at bytes now stored, and write the table back.
 *
 * Shared by the raw-container importer and the asset-tree importer: both place an
 * archive and then have to leave the table agreeing with it, and a stale CRC is
 * how the client comes to reject an archive that is perfectly well formed.
 *
 * `file_ids` (may be NULL) is the archive's new child list, for a caller that
 * rebuilt it from a directory and may have added or removed files.
 *
 * Sets `*out_dirty` when something changed; the table is only rewritten — and its
 * version only bumped — once, by `cp_reference_write`, after the last archive.
 */
int
cp_reference_sync(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    const uint8_t* container,
    int container_size,
    const int* file_ids,
    int file_count,
    int* out_dirty);

int
cp_reference_write(
    struct CP_Ctx* ctx,
    const char* out_dir,
    int table_id);

/* ---- shared config helpers, used by the per-type modules ---------------- */

/**
 * A record holding nothing but the terminator.
 *
 * Every pack function starts by decoding this, so the struct it then fills carries
 * exactly the decoder's own defaults. That matters more than it looks: a field the
 * text does not mention has to end up where an *absent opcode* would leave it, and
 * several of these types default to -1 rather than 0. Copying the default list into
 * this tool would be a second copy to keep in step with the library; running the
 * decoder makes the two agree by construction.
 *
 * **Zero the struct first.** Only some of the library's decoders initialise the
 * record themselves (`init_loc`, `init_overlay`, the `New*` entry points); the
 * smaller `...DecodeInplace` functions leave that to the caller and only write the
 * fields their opcodes carry. Skipping the memset leaves whatever was on the stack
 * in every field the empty record does not touch — which surfaces as a params count
 * of a few million, not as a wrong value.
 */
extern const uint8_t cp_empty_record[1];

/** Free-standing growable int list, for the array-valued keys. */
struct CP_IntList
{
    int* items;
    int count;
    int capacity;
};

void
cp_intlist_set(
    struct CP_IntList* list,
    int index,
    int value);

void
cp_intlist_push(
    struct CP_IntList* list,
    int value);

void
cp_intlist_free(struct CP_IntList* list);

/** Emit `key=<name of id in type>` unless `id` is `absent`. */
void
cp_emit_ref(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id,
    int absent);

/** Resolve a name (or a bare number, for ids the packs do not list) to an id.
 *  Returns 0 and warns when the name is unknown. */
int
cp_resolve_ref(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* text,
    int* out_id);

/** `recolNs`/`recolNd` + `retexNs`/`retexNd` line pairs. */
void
cp_emit_recols(
    struct CP_Lines* out,
    const int* from,
    const int* to,
    int count,
    const char* prefix);

/**
 * Collect `prefix<N>s` / `prefix<N>d` line pairs into two parallel lists.
 *
 * The count is taken from the highest index named, not from the number of lines,
 * so a hand-edit that leaves a hole does not silently shift every later pair down
 * a slot. Returns 0 when the two halves end up different lengths — a source with
 * no destination is not a recolour, and letting it through would write a
 * count-prefixed list whose tail is uninitialised.
 */
int
cp_collect_pairs(
    const struct CP_Config* config,
    const char* prefix,
    struct CP_IntList* from,
    struct CP_IntList* to);

/** `param=<name>,<value>` lines for a param map. */
void
cp_emit_params(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const struct RSCache_Params* params);

/** Parse one `param=` line into `params`. Returns 1 on success. */
int
cp_parse_param(
    struct CP_Ctx* ctx,
    struct RSCache_Params* params,
    const char* value);

/**
 * `op1..opN`, plus the rev-237 sub-ops and conditional ops.
 *
 * Plain ops and the conditional forms live in different places on the struct, and
 * the split is load-bearing rather than cosmetic: every decoder in the library
 * writes opcodes 30..34 into `actions[]` and leaves `RSCache_EntityOps.ops[]`
 * NULL, while the encoders emit plain ops from `actions[]` *and* would emit them
 * again from `ops[]` if anything ever filled it. So this reads `actions` and
 * writes `actions`, and `ops` carries only the sub/conditional lists.
 */
void
cp_emit_entity_ops(
    struct CP_Lines* out,
    char* const* actions,
    int slots,
    const struct RSCache_EntityOps* ops);

/** Returns 1 when `key` was an op line and was consumed. */
int
cp_parse_entity_op(
    char** actions,
    int slots,
    struct RSCache_EntityOps* ops,
    const char* key,
    const char* value);

/** Parse `keyN` (1-based) and return N-1, or -1 when `key` is not `prefix` + digits. */
int
cp_indexed_key(
    const char* key,
    const char* prefix);

/* Per-type entry points, declared here so the register in cp_types.c can name
 * them without a header per type. */
#define CP_DECLARE_TYPE(n)                                                                    \
    int cp_unpack_##n(                                                                        \
        struct CP_Ctx*, int, const uint8_t*, int, struct CP_Lines*);                          \
    uint32_t cp_pack_##n(                                                                     \
        struct CP_Ctx*, int, const struct CP_Config*, uint8_t*, uint32_t);

CP_DECLARE_TYPE(underlay)
CP_DECLARE_TYPE(overlay)
CP_DECLARE_TYPE(idk)
CP_DECLARE_TYPE(inv)
CP_DECLARE_TYPE(loc)
CP_DECLARE_TYPE(enum)
CP_DECLARE_TYPE(npc)
CP_DECLARE_TYPE(obj)
CP_DECLARE_TYPE(param)
CP_DECLARE_TYPE(seq)
CP_DECLARE_TYPE(spotanim)
CP_DECLARE_TYPE(varbit)
CP_DECLARE_TYPE(varp)
CP_DECLARE_TYPE(varc)
CP_DECLARE_TYPE(hitsplat)
CP_DECLARE_TYPE(healthbar)
CP_DECLARE_TYPE(struct)
CP_DECLARE_TYPE(mapelement)
CP_DECLARE_TYPE(dbrow)
CP_DECLARE_TYPE(dbtable)

#undef CP_DECLARE_TYPE


/** Path of a config type's member index, `configs/all.<type>.compack`. */
void
cp_config_member_index(
    char* out,
    size_t out_size,
    const char* srcdir,
    const char* type);

/** The cache's name for one interface child, `bankmain:items`, or NULL. */
const char*
cp_component_name(
    struct CP_Ctx* ctx,
    int interface_id,
    int child_id);

#endif
