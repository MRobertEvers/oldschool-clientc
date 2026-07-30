#ifndef RSCACHE_TOOLS_CP_ASSETS_H
#define RSCACHE_TOOLS_CP_ASSETS_H

/*
 * The asset tree: every non-config table as one named file per asset, laid out
 * the way LostCity_Server's `content/` is.
 *
 * `--binary` already moves these tables, as raw container blobs under
 * `binary/idx7/1234.bin`. That is byte-exact and useless to read. This is the
 * other half of the trade: the archive's *payload*, decompressed, written to a
 * file named after the asset with an extension that says what it is —
 * `models/npc/goblin.model`, `songs/harmony.jmid`, `binary/title.jpg`.
 *
 * ## Extensions are chosen from the bytes, not from the era they came from
 *
 * LostCity stores models as `.ob2` because a rev-254 model *is* an OB2. An
 * OldSchool 239 model is not: a census over all 61,615 of them finds
 * 26,990 osrs-v2 and 34,625 osrs-v3, and **zero** ob2 or ob3. Writing them as
 * `.ob2` would name the file after a format it does not contain, and the next
 * person to open one in an OB2 tool would find out the hard way.
 *
 * So the extension is per file and comes from the magic trailer: a real ob2 gets
 * `.ob2`, a real ob3 gets `.ob3`, and the OldSchool formats get `.model`. Unpack a
 * cache that genuinely holds OB2s and you genuinely get `.ob2` files.
 *
 * The same rule pays off elsewhere. Table 10 is JPEG and table 20 is PNG — both
 * are detected and get their real extension for free, which is how LostCity's
 * `binary/title.jpg` comes out right without a converter. It also stops the
 * reverse mistake: the music tables are **not** raw MIDI (they open `17 07 f6 02`,
 * Jagex's own container), so they get `.jmid` rather than a `.mid` no player
 * would open.
 *
 * ## Nothing is transcoded
 *
 * The bytes in the file are the bytes in the archive. That keeps the round trip
 * exact at the payload level and keeps this tool out of the business of format
 * conversion, which is lossy for exactly the assets anyone would want it for
 * (EXCEPTIONS.md A5: dat2 -> dat1 models drop texture render types and animaya
 * skinning). `tools/port_lostcity` is the converter, and it is a different job.
 */

#include "cachepack.h"

/**
 * Split the archive's members into files, indexed by a `<archive>.filepack`.
 *
 * For a multi-file archive with no text form: the members are written as separate
 * files under `<dir>/<archive>/`, and `<dir>/<archive>.filepack` — beside the
 * archive, in the table's own folder — states `<file id>=<path>` for each.
 *
 * The filepack is what makes the split safe. Without it the member's id lived in
 * its *filename* and the importer recovered the member set by listing a directory,
 * so which files an archive held came from the filesystem rather than from anything
 * the tree stated. With it, the same shape is fine: names are free, ids are stated,
 * and ascending ids give the container the order it wants.
 *
 * A table whose members *do* have a text form uses a `.compack` instead — one file
 * of named blocks, plus `<file id>=<block name>`. See `member_pack_load` in
 * cp_decode.c for both.
 */
#define CP_ASSET_SPLIT 0x1

/*
 * Note on what a pack file indexes.
 *
 * A pack file indexes **archives**, full stop: `pack/interface.pack` has
 * `12=bankmain`, not a line per component. An archive's members are indexed beside
 * the archive itself, by a `.compack` or a `.filepack`, so there is one id space per
 * file and the member list lives with the member data.
 */
/**
 * Map archives are XTEA-encrypted before OldSchool 237. Their payload only
 * exists once decrypted, and re-encrypting on the way back needs the same key —
 * so the tree carries the key alongside the asset rather than silently writing
 * a cache the client cannot read.
 */
#define CP_ASSET_ENCRYPTED 0x2

/**
 * A friendly form for one asset kind: the payload as text or as an image, rather
 * than as the bytes the cache holds.
 *
 * Both halves may decline. `write` returns 0 for a record it cannot decode, and
 * `read` returns NULL when the friendly file is not on disk — in both cases the
 * caller falls back to the raw payload with its raw extension. That fallback is
 * not a nicety: 37 of osrs239's clientscripts do not decompile, and a mode that
 * dropped them would quietly ship a cache missing 37 scripts.
 *
 * `path_stem` is the output path without an extension, so a codec can write one
 * file, several, or a directory as it sees fit.
 */
struct CP_AssetCodec
{
    /** Extension this codec writes, for the report and for `--list-assets`. */
    const char* ext;
    /**
     * 1 = written, 0 = decline and let the raw payload be written instead.
     *
     * `file_ids`/`file_count` describe the archive's members, because a codec for
     * a multi-file archive (an interface's components, a map's five layers)
     * cannot recover them from the payload alone — the count lives in the
     * reference table, not in the bytes.
     */
    int (*write)(
        struct CP_Ctx* ctx,
        int record_id,
        const uint8_t* payload,
        int size,
        const int* file_ids,
        int file_count,
        const char* path_stem);
    /**
     * Payload bytes (caller frees), or NULL to fall back to the raw file.
     *
     * `*out_file_ids` (caller frees) is the rebuilt member list, so an archive
     * that gained or lost a member updates the reference table's child list too.
     * Leave it NULL for a single-file asset.
     */
    uint8_t* (*read)(
        struct CP_Ctx* ctx,
        int record_id,
        const char* path_stem,
        int** out_file_ids,
        int* out_file_count,
        int* out_size);
    /**
     * 1 when the friendly form is a *recompilation* rather than a re-encoding, so
     * the payload legitimately comes back a different length.
     *
     * Last in the struct because every codec initialises positionally; only
     * `script` sets it, and the distinction is the whole reason the field exists.
     * A sprite's friendly form is the same pixels in a different byte order, so a
     * length change there is a lost field. A clientscript's friendly form is
     * decompiled *source*, and compiling source back produces whatever the
     * compiler chooses — its own jump widths, switch-table layout and string
     * ordering. Holding that to a length bar would be holding one compiler to
     * reproducing another's output.
     *
     * What the script table is held to instead is semantic equality, measured
     * where it can be: `3rd/rscache/test/test_cs2.c` decompiles, compiles and
     * decompiles again, and 6,489 of 6,491 comparable scripts come back identical.
     */
    int semantic_only;
};

struct CP_Asset
{
    /** Directory under the source tree, e.g. "models". */
    const char* dir;
    /** Pack file basename, e.g. "model" -> `pack/model.pack`. Singular, matching
     *  LostCity's, because a pack file names one thing per line. */
    const char* pack;
    /** Fallback extension, used when the payload is not a format we detect. */
    const char* ext;
    /** enum RSCache_Dat2Table — resolved to an on-disk id per cache. */
    int table;
    unsigned flags;
    /**
     * Gameval archive (table 24) naming this kind, or -1 when the cache names
     * none of them.
     *
     * Config types have had this since the start; assets did not, and the cost
     * was concrete. Table 24 archive 14 names all 969 interfaces — the cache
     * calls 161 `toplevel_osrs_stretch` and 12 `bankmain` — and because nothing
     * read it, `pack/interface.pack` was 934 lines of `interface_<id>` with 34
     * names hand-copied from a *different server's* table spliced in among them.
     * Claiming the archive replaces both halves with what the cache says.
     */
    int gameval_archive;
    /** Friendly form, or NULL to always write the raw payload. */
    const struct CP_AssetCodec* codec;
};

/**
 * `<archive>.compack` / `<archive>.filepack` — an archive's member list.
 *
 * `kind` is `"compack"` or `"filepack"`; `path_stem` is the archive's path without
 * an extension, so the index lands beside it in the table's own folder. A missing
 * file loads as an empty pack, which is how an archive gets its first index.
 *
 * Plain pack files, so they read and write through `lc_pack_*` and inherit comment
 * preservation and merge semantics — a member someone renamed keeps its name across
 * a re-export.
 */
int
cp_member_pack_load(
    struct LC_Pack* pack,
    const char* path_stem,
    const char* kind,
    const char* label);

int
cp_member_pack_save(
    const struct LC_Pack* pack,
    const char* path_stem,
    const char* kind);

const struct CP_Asset*
cp_asset(enum CP_AssetId id);

/**
 * Round-trip every asset table through the tree's form and report the fidelity.
 *
 * `tmpdir` is scratch: the friendly forms are written there and read straight
 * back, so the content tree is never touched. `assets_csv` restricts the run to
 * named kinds, as `--assets=` does.
 *
 * Returns 1 when every table met its bar. The bar differs per table and is
 * explained at the implementation — the short version is that only a *length*
 * change fails, because a same-length mismatch is byte ordering and loses nothing
 * (EXCEPTIONS.md B3).
 */
int
cp_assets_verify(
    struct CP_Ctx* ctx,
    const char* assets_csv,
    const char* tmpdir);

int
cp_asset_by_name(const char* dir);

/** Sentinel `size` meaning "this is a whole archive, do not sniff it". */
#define CP_ASSET_SIZE_ARCHIVE (-1)

/**
 * The extension `payload` should carry, without the dot.
 *
 * Detects PNG, JPEG, GIF, MIDI, Ogg and the four model formats; otherwise returns
 * the table's declared fallback. Never allocates — the result is a literal.
 *
 * Pass CP_ASSET_SIZE_ARCHIVE for `size` when `payload` is a whole multi-file
 * archive: the bytes at its head belong to the first file inside it, so sniffing
 * them would name the archive after one of its members.
 */
const char*
cp_asset_extension(
    enum CP_AssetId id,
    const uint8_t* payload,
    int size);

/* ---- drivers ------------------------------------------------------------ */

/** Turn the friendly codecs off, so every asset writes its raw payload. */
void
cp_assets_set_raw(int raw);

int
cp_assets_export(
    struct CP_Ctx* ctx,
    const char* assets_csv);

int
cp_assets_import(
    struct CP_Ctx* ctx,
    const char* out_cache_dir);

/**
 * Rename models after the configs that reference them, as LostCity's unpacker
 * does (`unpackModelNames` / `renameModel`).
 *
 * A cache names its configs (the gameval index) but never its models, so a model
 * would otherwise be `model_24458` — and the tree's whole value is being able to
 * find the thing you mean. Walking the configs turns that into
 * `models/npc/goblin.model`, which is the name LostCity would have given it.
 *
 * Called before the model table is written, so the names exist when the files are
 * placed. Ids no config references keep `model_<id>`.
 */
void
cp_assets_name_models(struct CP_Ctx* ctx);

/**
 * Name map squares `m<x>_<z>`, so the tree's `maps/` reads the way LostCity's
 * does and the server's spawn file for a square is the same file as its terrain.
 *
 * Called before the map table is written, for the same reason the model naming is.
 */
void
cp_assets_name_maps(struct CP_Ctx* ctx);

#endif
