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
#include "cp_walk.h"
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
    CP_ASSET_DEFAULTS,
    CP_ASSET_COUNT
};

/** The type carries fields the decoder consumes without storing, so a repack is a
 *  valid record with less in it than the source had. Reported, never silent. */
#define CP_TYPE_LOSSY 0x1
/** rscache has a decoder but no encoder: unpack works, pack refuses. */
#define CP_TYPE_NO_ENCODER 0x2

struct CP_Ctx;

/** The key may repeat (one line per entry) and may be `key=empty`. */
#define CP_KEY_LIST 0x1
/** The key is a numbered family (`recol1s`, `recol2d`, ...): its lines are the
 *  stem followed by a digit, its markers are written on the bare stem. */
#define CP_KEY_INDEXED 0x2

/**
 * One text key of a config type.
 *
 * Every block of the type states every key that applies, `key=default` when the
 * record does not state the field (see CP_VALUE_DEFAULT in cp_text.h). The
 * unpacker is checked against this list as it writes, and the packer refuses a
 * client record missing one, so a field can no longer fall out of the text
 * without failing the run. `applies` narrows a key to the profiles whose codec
 * can carry it (a sequence's `verticaloffset` exists only from rev 226); NULL
 * means every profile. A table ends with a NULL `key`.
 */
struct CP_KeySpec
{
    const char* key;
    unsigned flags;
    int (*applies)(const struct CP_Ctx* ctx);
    /**
     * Another key of the same opcode, or NULL. A dbtable's `column` and `default`
     * lines are one opcode: when `column` is stated and `default` is not, the
     * opcode is present with no defaults, `default=empty` — not absent. A record
     * completed with the missing key gets `empty` when its sibling is stated and
     * `default` otherwise (cp_keys_missing, `cachepack missing`, `keys`).
     */
    const char* sibling;
};

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

    /** Every key a block of this type states. See CP_KeySpec. */
    const struct CP_KeySpec* keys;
};

/* Each type's key table, defined beside its unpacker. */
extern const struct CP_KeySpec cp_underlay_keys[];
extern const struct CP_KeySpec cp_overlay_keys[];
extern const struct CP_KeySpec cp_idk_keys[];
extern const struct CP_KeySpec cp_inv_keys[];
extern const struct CP_KeySpec cp_loc_keys[];
extern const struct CP_KeySpec cp_enum_keys[];
extern const struct CP_KeySpec cp_npc_keys[];
extern const struct CP_KeySpec cp_obj_keys[];
extern const struct CP_KeySpec cp_param_keys[];
extern const struct CP_KeySpec cp_seq_keys[];
extern const struct CP_KeySpec cp_spotanim_keys[];
extern const struct CP_KeySpec cp_varbit_keys[];
extern const struct CP_KeySpec cp_varp_keys[];
extern const struct CP_KeySpec cp_varc_keys[];
extern const struct CP_KeySpec cp_hitsplat_keys[];
extern const struct CP_KeySpec cp_healthbar_keys[];
extern const struct CP_KeySpec cp_struct_keys[];
extern const struct CP_KeySpec cp_mapelement_keys[];
extern const struct CP_KeySpec cp_dbrow_keys[];
extern const struct CP_KeySpec cp_dbtable_keys[];

/** The spec `key` belongs to in `table` (an INDEXED stem matches its numbered
 *  lines), ignoring `applies`; NULL when none. For callers with no profile. */
struct RSCache_Register;
struct RSCache_RegisterField;
/** RSCache_RegisterFind / RSCache_RegisterFindLine, memoised (cp_keys.c). */
const struct RSCache_RegisterField*
cp_register_find(
    const struct RSCache_Register* reg,
    const char* name);
const struct RSCache_RegisterField*
cp_register_find_line(
    const struct RSCache_Register* reg,
    const char* key);

const struct CP_KeySpec*
cp_key_spec_in(
    const struct CP_KeySpec* table,
    const char* key);

/** The spec for `key` in the type's table, or NULL when the type has no such key
 *  (or it does not apply to this profile). */
const struct CP_KeySpec*
cp_key_spec(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const char* key);

/**
 * Check a record's unpacked lines against the type's key table: every applying
 * key stated, a marker (`default` / `empty`) the only line for its key, `empty`
 * only on a list key, and no key stated twice unless it is a list. Prints each
 * violation naming the record and returns 0 if there was one.
 */
int
cp_keys_check_lines(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const char* record_name,
    const struct CP_Lines* lines);

struct RSCache_Register;
struct RSCache_RegisterField;

/**
 * The whole-record checks: the client table AND every server key of the type —
 * a `fields/<type>.ini` field spelled `text = key` that is not a client key
 * (`hitpoints`, `respawnrate`, a varp's `scope`). Client and server keys are
 * one key set of one record, spelled and stated the same way; the client-only
 * forms above are for the client view, after the server keys are routed away.
 */
int
cp_keys_check_lines_full(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const char* record_name,
    const struct CP_Lines* lines);

int
cp_keys_check_config_full(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const struct CP_Config* config,
    const char* where);

/** Append `key=default` for every server key of the type: what a record that
 *  comes from the client cache states for fields only the server carries. */
void
cp_keys_add_server_defaults(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    struct CP_Lines* lines);

/** The type's field register (`fields/<type>.ini` under `--src`), loaded once. */
const struct RSCache_Register*
cp_ctx_fields(
    struct CP_Ctx* ctx,
    enum CP_TypeId type);

/** Release what cp_ctx_fields loaded. Accepts a context that loaded none. */
void
cp_ctx_fields_free(struct CP_Ctx* ctx);

/** Call `missing` for every key (client and server) `config` does not state,
 *  with the marker completing it takes (`default`, or `empty` for a key whose
 *  sibling is stated). Returns how many. The enumeration `cachepack missing`
 *  reports. */
int
cp_keys_missing(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const struct CP_Config* config,
    void (*missing)(void* user, const char* key, const char* marker),
    void* user);

/** True if `field` is one of the type's server keys (see the checks above). */
int
cp_keys_is_server_key(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_RegisterField* field);

/** The same check over a parsed (or merged) block. `where` names its origin. */
int
cp_keys_check_config(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct CP_Config* config,
    const char* where);

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
    /**
     * The server's allocation ledger: `pack/<ns>.alloc`, ids `ss_allocate.py`
     * handed out for records the cache does not hold.
     *
     * A second array and not more lines in `packs` — the separation is the
     * point, on both sides of the tool:
     *
     *   - `cp_names_save` writes `packs` back to `configs/all.<ns>.compack`, so a
     *     server name merged into it would be spliced into the machine-owned
     *     member index — the exact mixing this layer exists to end.
     *   - `--gamevals` emits `packs` into the cache's own symbol table, and a
     *     server id in there is a name the client cache carries for a record it
     *     does not hold.
     *
     * Both lookups (`cp_name_find`, `cp_name_get`) search `packs` first and this
     * second; nothing writes it — `ss_allocate.py` is the one writer.
     *
     * Being listed here is also a *routing* statement: an id the server
     * allocated names a record the cache cannot hold, so the packer routes it
     * server-side without a membership line (`routing_client_member`). The
     * allocation is the membership.
     */
    struct LC_Pack alloc[CP_TYPE_COUNT];
    /**
     * The subset of `alloc` an imported lane minted (`ported/<lane>/pack/<ns>.alloc`).
     * A record with one of these names is defined whole in its lane; a
     * `server/scripts` block for it is an overlay of that record, partial like any
     * overlay, and the lane's own bake checks the full record.
     */
    struct LC_Pack lane[CP_TYPE_COUNT];
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
    /**
     * The cache's own dbtable *column* names, from gameval archive 10.
     *
     * The same record that names a table names every one of its columns — key 1
     * is the table, key n >= 2 is column n-2 (see `keyed_gameval_name`). They are
     * held here rather than written to a file of their own because a column is not
     * separately addressable: it is a position in a table, so its name belongs
     * beside the types it names, in `configs/all.dbtable`'s `column=` lines and in
     * every `all.dbrow` that fills it.
     *
     * Not an `LC_Pack` keyed on `(table << 12) | (column << 4)`, which is how the
     * client addresses one and how `components` handles the analogous case: that
     * key reaches 1,057,536 for this cache and `LC_Pack.names` is a flat array, so
     * it would cost ~25 MB of mostly-NULL pointers to hold 3,000 strings. Indexed
     * by table, then by column, which is the shape the data actually has.
     */
    char*** dbtable_columns;   /* [dbtable_count][dbtable_column_count[t]] */
    int* dbtable_column_count; /* [dbtable_count] */
    int dbtable_count;
    /**
     * `pack/category.pack` — the one namespace a *config field* refers to that is
     * not a config type.
     *
     * A category is not a record: the cache has no category group and no gameval
     * table for it (`cp_fields.c`'s `k_server_groups`), so it cannot be a
     * `CP_TypeId` and `packs[]` cannot hold it. But `loc.category` (config opcode
     * 61) and `npc.category` (18) are *fields whose value is a category id*, and
     * an authored overlay spells that value as a name — `category=door_closed`,
     * which is how LostCity's own `.loc` blocks state it. Resolving it needs the
     * table, so the table is loaded here.
     *
     * Sparse and usually small (55 rows in this tree). Absent is normal: a tree
     * that has named no category resolves nothing and says so per call site.
     */
    struct LC_Pack category;
};

/**
 * The cache's name for one dbtable column, or NULL.
 *
 * NULL for a table the gameval archive does not carry, a column past the end of
 * the ones it names, or a cache with no symbol table at all — every caller has to
 * cope, because none of those is an error.
 */
const char*
cp_db_column_name(
    const struct CP_Names* names,
    int table_id,
    int column);

/**
 * How many files of one extension a pack run will consider.
 *
 * `cp_walk_find` stops at this many and says nothing, so it has to stay ahead of
 * the tree rather than merely near it: at 256 the 265 `.constant` files silently
 * lost nine, which costs a `^name` that resolves to nothing at a use site far
 * away. Raised to 1024 with that margin in mind; the array is
 * `sizeof(char*) * this` on one stack frame, so the cost is 8KB, not a table.
 */
#define CP_PACK_MAX_SOURCES 1024

/** Load `<srcdir>/pack/<type>.pack` for every type; missing files are empty. */
int
cp_names_load(
    struct CP_Names* names,
    const char* srcdir);

/**
 * Layer `<srcdir>/ported/<lane>/pack/<type>.alloc` over the ordinary namespace
 * view. Pack and membership call this because server overlays may name imported
 * records. Import deliberately does not: saving one lane must not absorb every
 * other lane's allocations.
 *
 * Conflicting ids or names are fatal; an identical repeated binding is merely
 * redundant, matching the runtime symbol loader.
 */
int
cp_names_load_ported_allocs(
    struct CP_Names* names,
    const char* srcdir,
    const char* const* included_lanes,
    int included_lane_count);

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

/**
 * Write the pack files back into the cache's own symbol table (`--gamevals`).
 *
 * Makes the cache self-describing: anything pointed at it alone recovers your
 * names without the content tree. Not a faithful round trip — names go out
 * normalised — and archive 14 is refused because it is nested. See the block
 * comment in cp_names.c, and docs/CONTENT_PACK_PLAN.md §5.5.
 *
 * Off by default. Nothing outside cachepack reads this table, so emitting it can
 * neither help nor break a boot; it is for the tools and for anyone who is handed
 * the cache on its own.
 */
int
cp_names_emit_gamevals(
    struct CP_Ctx* ctx,
    const char* out_cache_dir);

/**
 * The gameval archives the flat emit cannot regenerate, as raw container bytes.
 *
 * Archive 14 is nested (interface + component names), 10 is keyed (dbtable +
 * column names), and 11 names songs and jingles in one id space no pack file
 * mirrors. Regenerating any of them flat destroys names nothing else carries,
 * so they ride as bytes — `gamevals/<name>.bin`, indexed by
 * `gamevals/gamevals.filepack` — and `cp_names_emit_gamevals` imports them
 * after the flat archives, which is what makes idx24 complete on a pack with
 * no --base.
 */
int
cp_names_export_raw_gamevals(struct CP_Ctx* ctx);

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

/**
 * Id for `name` in the server's allocation ledger alone, or -1.
 *
 * The routing gate's question — `cp_name_find` answers "what id", this answers
 * "whose id". A name here is a record the server allocated past the cache's
 * high-water mark, which is what routes it server-side with no membership line.
 */
int
cp_name_find_alloc(
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

/** One `^name = <int>` from a `.constant` file. See CP_Ctx.constants. */
struct CP_Constant
{
    char* name; /**< without the leading caret */
    int value;
};

enum
{
    CP_MAX_LANES = 8,
};

struct CP_Ctx
{
    /** `--lane <name>`: lanes whose `ported/<name>/configs` are walked as
     *  definitions (below the authored overlays), so their records are whole
     *  records here rather than overlays of a record defined elsewhere. */
    const char* lanes[CP_MAX_LANES];
    int lane_count;
    /** `--server-out <dir>`: where the server pack goes; "" = <src>/server/pack. */
    char server_out[1024];
    /** This cachepack's identity (its executable's size and mtime), stamped into
     *  the server pack so a rebuilt cachepack rewrites a pack its tree has not
     *  changed. */
    uint64_t writer;
    /** `--force`: write the server pack even when its stamp says it is fresh. */
    int force_server;
    struct RSCache profile;
    struct Tool_Dat2Cache cache; /* open only for unpack/verify */
    bool cache_open;
    /*
     * The config commit reached disk.
     *
     * `cache_open` is set before the config pass, so it cannot answer this — and
     * the asset and binary imports were gated on it alone. A pack that aborted
     * before committing therefore went on to write assets into a cache with no
     * config table, producing a directory that looks like a cache, is the size
     * of one, and cannot be booted. Failing is fine; leaving that behind is not.
     *
     * It is deliberately not "cp_pack_run returned true": a run with membership
     * errors returns false *after* a successful commit, and those exist in the
     * tree today. Aborting the asset import for them would be a regression.
     */
    bool cache_committed;

    /** Source files found by extension, built once per pack run (cp_walk.h). */
    struct CP_Walk walk;

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
    /** Each type's `fields/<type>.ini`, loaded on first use from `srcdir`
     *  (cp_ctx_fields). A type with no register file loads as an empty table. */
    struct RSCache_Register* register_fields[CP_TYPE_COUNT];

    /**
     * Param id -> declared `type=` character, or 0. Built by
     * `cp_param_types_load` before the type loop; see it for why not lazily.
     */
    struct CP_ParamType
    {
        char code;
    } *param_types;
    int param_types_count;

    /**
     * The tree's `^constants`, as a param value can name one.
     *
     * `param=undead,^true` and `param=damagetype,^crush_style` are in this tree's
     * `.npc` overlays, and a `^name` is declared in a `.constant` file under
     * `server/scripts` (4,878 of them across 265 files). Loaded lazily — most packs never
     * meet a caret, and the walk is only worth doing for one that does.
     *
     * Integer-valued constants only. A constant whose value is a name is not a
     * thing a param value can hold, so it is skipped rather than half-resolved.
     */
    struct CP_Constant* constants;
    int constants_count;
    int constants_capacity;
    int constants_loaded;

    /** Counted, not fatal: a record the decoder did not consume to the byte has
     *  fields this tool cannot see, and the count is the headline of the report. */
    int warn_short_decode;
    int warn_unknown_key;
    int warn_unresolved_name;
    /* Archives added to a reference table that did not list them. Rare when
     * packing onto a base cache — and every single archive when packing the
     * tree alone, where the tables start empty. One line each meant six figures
     * of stderr writes, which is both unreadable and slow, so it goes through
     * cp_warn's suppression like every other repeated note. */
    int warn_reference_added;
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
 * One config type, opened once and walked.
 *
 * A config type is a single archive holding one file per record, so the whole
 * type is one decompression. Loading per record instead would re-inflate a
 * 56,000-file archive once per loc.
 *
 * OldSchool types are normally one config archive. RS2 types may span a table of
 * groups addressed by `id >> shift`; those groups are flattened here into the
 * same ascending record view so unpack/decode/name discovery need no era branch.
 */
struct CP_Group
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    /** Record id per index, ascending as the container stores them. */
    const int* ids;
    /** Non-NULL when `ids` is an aggregate allocated for a sharded table. */
    int* owned_ids;
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

/**
 * `pack --server-only`: rebuild `<src>/server/pack` and nothing else.
 *
 * No cache is opened and no `--out` is needed, which is what makes it cheap
 * enough to run as a build step before every server boot. The bands it writes
 * are byte-identical to the ones a full `pack` writes — same walk, same merge,
 * same encoder.
 */
int
cp_pack_server_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel);

/**
 * `cachepack missing --src DIR`: merge every type exactly as `pack` does and
 * print, for every record that must state every key and does not, one line per
 * missing key: `<type>\t<record>\t<file>\t<key>`, `<file>` being the first file
 * that declares the record (where a completion belongs). Writes nothing.
 */
int
cp_missing_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel);


/**
 * `membership`: seed `pack/<ns>.client` and `pack/<ns>.server` from the routing
 * the packer already performs, and write nothing else.
 *
 * A separate command rather than a flag on `pack`, because it is not part of
 * packing: it opens no cache, writes no archive, and creates only files that do
 * not already exist, so running it twice is a no-op. See the section comment in
 * `cp_pack.c` and docs/PACK_ENTITY_SPLIT_PLAN.md §4 step 1.
 */
int
cp_membership_emit(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel);

/**
 * `membership --check-only`: hold the seeded files against provenance and write
 * nothing.
 *
 * The first of the two agreement checks docs/PACK_ENTITY_SPLIT_PLAN.md §3.3
 * names. The second is against the id range and is *not* here: `server_base`
 * belongs to `src/content/content_register.c` and cachepack links nothing from
 * `src/`, so `ToriRSServer_Pack` runs that half against the register it already holds.
 *
 * Returns 0 when a disagreement no gate can explain was found. The large,
 * legitimate populations §8.5 records are counted and printed rather than failed
 * — the routing being checked is the one the files were seeded from, so what a
 * failure here means is that the tree changed under them.
 */
int
cp_membership_check(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel);

int
cp_verify_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    /** Where to write `<type>.digests` (`<id>=<crc32>` per record), or NULL. */
    const char* digest_dir);

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

/**
 * True when `base_dir`'s main_file_cache.dat2 is the one `<srcdir>/meta.ini`
 * records under [source] (size and crc32) — the build this tree was unpacked
 * from. Two builds of one revision differ in records the tree may not restate,
 * and `pack --base` keeps the base's bytes for those. A tree with no [source]
 * passes; a mismatch prints both identities and fails.
 */
int
cp_check_base_identity(const char* srcdir, const char* base_dir);

/**
 * Interpret every script in the packed cache against that cache's own callees
 * and fail on any that does not hold together (wrong gosub stack types, an
 * underflow, a mistyped return). Returns 1 when all link.
 */
int
cp_scripts_link_check(struct CP_Ctx* ctx);

int
cp_binary_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir);

/* ---- raw passthrough ---------------------------------------------------- */

/**
 * The config groups no CP_Type decodes, as raw container bytes.
 *
 * osrs239's idx2 holds 41 groups and the type table claims 20; the other 21 are
 * a few hundred bytes of near-empty records the client can still ask for. A
 * pack with no --base used to drop them, which is the one way a tree-only
 * cache differed from the original at the index level. They ride as bytes —
 * `configs/<name>.bin`, indexed by `pack/2_configs.pack` like every other
 * archive of index 2 — because a group with no decoder has no text form to
 * take, and raw is byte-exact by construction.
 *
 * `cp_raw_groups_import` with a NULL `out_cache_dir` checks that every indexed
 * raw group has a file, writing nothing (the `--check-only` contract).
 */
int
cp_raw_groups_export(struct CP_Ctx* ctx);

int
cp_raw_groups_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir);

/** One archive's raw container bytes, straight off the sector chain —
 *  compression byte, lengths and payload as stored, never decompressed. */
uint8_t*
cp_binary_read_raw(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    int* out_size);

/** Record an archive's name (djb2) so the client can resolve it by name.
 *  No-op for a NULL/empty name or an id nothing was written for. */
int
cp_reference_set_name(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    const char* name,
    int* out_dirty);

/** Write a raw identifier, when the pack line carried `hashcode(N)`. */
int
cp_reference_set_identifier(
    struct CP_Ctx* ctx,
    int table_id,
    int archive_id,
    int identifier,
    int* out_dirty);

/**
 * Identifier a pack line wants written: `hashcode`, else djb2(`hashname`),
 * else djb2(`fallback_name`). `fallback_name` is the pack filename.
 */
int
cp_pack_archive_identifier(
    const struct LC_Pack* pack,
    int id,
    const char* fallback_name);


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

/** Walk the content roots every command merges: `configs` (rank 0), each
 *  `--lane`'s `ported/<lane>/configs`, then `server/scripts`. */
void
cp_walk_content(struct CP_Ctx* ctx);

/** Stamp the server pack at `server_dir` as written from this tree, now. */
int
cp_server_stamp_write(
    struct CP_Ctx* ctx,
    const char* server_dir);

/** The server pack directory: `--server-out`, else <src>/server/pack. */
void
cp_server_dir(
    const struct CP_Ctx* ctx,
    char* out,
    size_t out_size);

/** True if `name` is a record an imported lane minted (CP_Names.lane). */
int
cp_name_is_lane(
    const struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* name);

/** Type every param from the open cache's own param records (unpack, verify),
 *  instead of from the tree's text, which an unpack is about to rewrite. */
int
cp_param_types_from_cache(struct CP_Ctx* ctx);

/** Write `key=<name of id>`, naming it if the pack has no name yet. The caller
 *  has already decided the field is present (see CP_KeySpec); there is no
 *  "absent" value here to compare against. */
void
cp_emit_name(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id);

/** Resolve a name (or a bare number, for ids the packs do not list) to an id.
 *  Returns 0 and warns when the name is unknown. */
int
cp_resolve_ref(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* text,
    int* out_id);

/**
 * Resolve a `^name` to its integer value. Returns 0 when `text` is not a caret
 * or names no integer constant.
 *
 * `^true`/`^false` are the language's; everything else comes from the tree's
 * `.constant` files, loaded on first use.
 */
int
cp_resolve_caret(
    struct CP_Ctx* ctx,
    const char* text,
    int* out_value);

/* ---- typed values: the one ScriptVarType table (cp_value.c) ------------- */

/*
 * A value's type is a ScriptVarType, stated as its numeric id by a dbtable
 * column, as its character by a param record or an enum, and as its word in the
 * text. One table answers all three, and one reader and one writer spell an int
 * value by its type -- dbrow, param records, `param=` lines, enums and the
 * server band writer all go through these. See cp_value.c for the table and the
 * spellings.
 */

/** How a value of a type is spelled. */
enum CP_ValueSpell
{
    CP_VALUE_INT = 0,   /* decimal (also every numeric type: graphic, model, ...) */
    CP_VALUE_STRING,    /* text; never an int */
    CP_VALUE_BOOLEAN,   /* yes/no or true/false (CP_BoolStyle) */
    CP_VALUE_COORD,     /* level_mx_mz_lx_lz */
    CP_VALUE_REF,       /* a config record's name; `ref` is its CP_TypeId */
    CP_VALUE_ASSET,     /* an asset's name; `ref` is its CP_AssetId */
    CP_VALUE_STAT,      /* pack/stat.pack */
    CP_VALUE_CATEGORY,  /* pack/category.pack */
    CP_VALUE_COMPONENT, /* interface:component */
};

struct CP_ValueType
{
    /** The ScriptVarType id a dbtable column stores, or -1 when none is published. */
    int id;
    /** The type character (a windows-1252 byte) a param or an enum stores, or 0. */
    int ch;
    /** The word the text spells it with. */
    const char* name;
    enum CP_ValueSpell spell;
    /** CP_TypeId for CP_VALUE_REF, CP_AssetId for CP_VALUE_ASSET, else -1. */
    int ref;
};

/** A boolean's words: a dbrow says `true`/`false`, a param and an enum `yes`/`no`. */
enum CP_BoolStyle
{
    CP_BOOL_TRUE_FALSE = 0,
    CP_BOOL_YES_NO,
};

int
cp_value_type_count(void);

const struct CP_ValueType*
cp_value_type_at(int index);

/** The type a word names, or NULL. */
const struct CP_ValueType*
cp_value_type_named(const char* word);

/** The type with ScriptVarType id `id`, or NULL. */
const struct CP_ValueType*
cp_value_type_of_id(int id);

/** The type with character `ch` (a byte), or NULL. */
const struct CP_ValueType*
cp_value_type_of_char(int ch);

/** The text of a type character: its word, or its byte in decimal when the
 *  table does not list it (so it still round-trips). `buf` is scratch. */
const char*
cp_value_char_text(
    int ch,
    char* buf,
    size_t buf_size);

/** Inverse of cp_value_char_text: the character, or 0 for text that is neither
 *  a listed word nor a decimal byte. */
int
cp_value_char_read(const char* text);

/** The text of a type id: its word, or the id in decimal. */
const char*
cp_value_id_text(
    int id,
    char* buf,
    size_t buf_size);

/** Inverse of cp_value_id_text: the id, or -1. A listed word with no id is -1. */
int
cp_value_id_read(const char* text);

/**
 * One int value of `type` as text (`type` NULL: a plain int). The result is a
 * name the context owns, a literal, or `buf`. A string type asserts.
 */
const char*
cp_value_int_text(
    struct CP_Ctx* ctx,
    const struct CP_ValueType* type,
    int value,
    enum CP_BoolStyle bool_style,
    char* buf,
    size_t buf_size);

/**
 * Read one int value of `type` (NULL: a plain int): `null` (-1), a `^constant`
 * (its text, read as the type reads it), either boolean pair, a coord, a name in
 * the type's namespace, or a number. Returns 1 and sets `*out`, or 0 when the
 * text is not a value of that type. `text` is already unescaped.
 */
int
cp_value_int_read(
    struct CP_Ctx* ctx,
    const struct CP_ValueType* type,
    const char* text,
    int* out);

/** A `^name`'s `.constant` text, or NULL. */
const char*
cp_value_constant_text(
    struct CP_Ctx* ctx,
    const char* name);

/**
 * Escape a string value for the text: `\`, `\n`, `\r`, a leading `^` (which
 * would read as a constant) and, when `whole` (the string is the line's whole
 * value), a leading `[` and a marker word. `//` and trailing blanks are the line
 * writer's (cp_line_write).
 */
void
cp_value_string_escape(
    const char* text,
    int whole,
    char* buf,
    size_t buf_size);

/** Read a string value from its raw (escaped) text: an unescaped leading `^` is
 *  a constant's text, anything else is unescaped. Returns 0 for an unknown
 *  constant. */
int
cp_value_string_read(
    struct CP_Ctx* ctx,
    const char* raw,
    char* buf,
    size_t buf_size);

/** Cut a value at its first unescaped `//` and trim unescaped blanks, in place:
 *  the server's line cleaner, so a trailing comment is not part of the value. */
void
cp_value_clean(char* text);

/**
 * Split `text` (a value with db escapes: `\,` is a comma inside a field) into
 * exactly `n` fields, in place: the first n-1 end at an unescaped comma and the
 * last takes the rest. Each field is unescaped, except that an unescaped
 * leading `^` is kept so cp_value_int_read sees the constant. Returns the
 * number of fields found, which is short of `n` when the text is.
 */
int
cp_value_split(
    char* text,
    int n,
    char** fields);

/** `cp_resolve_ref`, plus the literal `null` as -1 — the reference's spelling of
 *  "no value". Use this wherever the value is allowed to name nothing. */
int
cp_resolve_ref_or_null(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* text,
    int* out_id);

/**
 * Resolve a category name (or a bare number) to a category id.
 *
 * The `cp_resolve_ref` of the one namespace that is not a config type — see
 * `CP_Names.category`. Same contract: a bare number always wins, a name is looked
 * up in `pack/category.pack`, and an unknown name warns once and returns 0 so the
 * record is written without the field rather than with a wrong one.
 */
int
cp_resolve_category(
    struct CP_Ctx* ctx,
    const char* text,
    int* out_id);

/*
 * There is deliberately no `cp_emit_category`. The unpack writes the id, exactly
 * as `cp_npc.c` has always written `category=249`: `configs/all.<type>` is the
 * machine export of the cache, and naming its values would make 8,407 lines of it
 * depend on a file the cache does not contain. Names are for what a human writes.
 */

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

/* ---- param types -------------------------------------------------------- */

/*
 * A param record's `type=` is its ScriptVarType word (cp_value.c); the record
 * stores the character. The type matters beyond cosmetics: it is what a value
 * is read through -- `default=bones` is obj 526 only because `death_drop` is a
 * `namedobj`, and nothing else in the record says so.
 */

/**
 * Build `ctx->param_types` from every `.param` source in the tree.
 *
 * Run once before the type loop, not lazily, because the register packs `loc` and
 * `npc` *before* `param` — a record that states `param=death_drop,bones` is
 * encoded while the param table would still be empty. Returns the number of
 * params typed.
 */
int
cp_param_types_load(struct CP_Ctx* ctx);

/** The declared `type=` character for a param id, or 0. */
char
cp_param_type_of(
    struct CP_Ctx* ctx,
    int param_id);

/** Load the tree's integer-valued `^constants` once. `cp_resolve_caret` calls
 *  this lazily; pack passes may call it after walking the tree to prime it. */
void
cp_constants_load(struct CP_Ctx* ctx);

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
