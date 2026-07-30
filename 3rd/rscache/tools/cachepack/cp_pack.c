#include "cachepack.h"

#include "checksum.h"
#include "cp_walk.h"

#include "cache_edit.h"
#include "cache_write.h"
#include "dat2disk.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * The pack driver — LostCity's engine/tools/pack/config/PackShared.ts `packConfigs`.
 *
 * The reference reads every `.npc` / `.loc` / ... under the source tree, parses each
 * `[name]` block, packs it into a `.dat`/`.idx` pair, and finally writes the whole
 * config jagfile into the cache. Same shape here, with the container of the era:
 * one archive per type inside config table 2, one file per record id, written
 * through RSCache_Dat2Edit so the reference table's CRCs, sizes and child lists all
 * move with it.
 *
 * ## Why this writes into a copy of a cache rather than building one from nothing
 *
 * A cache is far more than its configs — models, sprites, maps, scripts, sounds and
 * a dozen other tables that this tool leaves alone. Emitting only the config table
 * would produce a directory the client cannot boot. So `pack` starts from a base
 * cache, exactly as the reference's `cache.write(0, 2, config)` writes one archive
 * into an existing cache file and leaves the rest untouched. Use `--base` to copy
 * first; without it the cache at `--out` is edited in place.
 *
 * ## Ordering
 *
 * Types are packed in register order, and the register puts the referenced types
 * first, because a `.loc` naming a sequence needs that sequence's pack line loaded.
 * Pack lines all load up front, so the order only matters for reporting.
 */

/** Names are the id authority: a `[name]` with no pack line has no id to write to. */
static int
resolve_id(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* debugname,
    int* out_id)
{
    int id = cp_name_find(ctx, type, debugname);
    if( id >= 0 )
    {
        *out_id = id;
        return 1;
    }
    fprintf(
        stderr,
        "cachepack: %s [%s] has no id — add a line to configs/all.%s.compack\n",
        cp_type(type)->name,
        debugname,
        cp_type(type)->name);
    return 0;
}

struct CP_PackStats
{
    int records;
    int bytes;
    int failed;
};

static int
pack_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    struct RSCache_Dat2Edit* edit,
    int config_table,
    struct CP_PackStats* stats)
{
    const struct CP_Type* type = cp_type(type_id);
    memset(stats, 0, sizeof(*stats));

    if( type->flags & CP_TYPE_NO_ENCODER )
    {
        printf("  %-11s skipped (no encoder; source records kept)\n", type->name);
        return 1;
    }

    /*
     * Found by walking, not by constructing a path.
     *
     * `configs/all.<type>` was the only place a config record could live, and the
     * packer knew that as a `snprintf`. Discovery by extension is what lets a
     * second layer exist at all — the `server/scripts` tree states the same types in
     * the same grammar — so the path stops being knowledge the packer holds and
     * becomes something the tree answers.
     *
     * Still one file per type at this phase: `configs/` is the only root, so this
     * finds exactly what the `snprintf` found and the packed bytes are unchanged.
     * That is the bar for the change.
     */
    const char* found[CP_PACK_MAX_SOURCES];
    int found_count = cp_walk_find(&ctx->walk, type->name, found, CP_PACK_MAX_SOURCES);
    struct CP_ConfigFile file;

    if( found_count <= 0 || !cp_config_file_load(&file, found[0]) )
    {
        /* A type the source tree does not carry is not an error: a partial unpack
         * is a normal thing to want, and the base cache still holds those records. */
        printf("  %-11s no source file\n", type->name);
        return 1;
    }

    /* One buffer for every record. 64 KB is comfortably past the largest config
     * record in any cache measured (the widest loc is under 2 KB); an encoder that
     * needs more returns 0 rather than overrunning, and that is reported. */
    uint8_t* buffer = malloc(64 * 1024);
    if( !buffer )
    {
        cp_config_file_free(&file);
        return 0;
    }

    for( int i = 0; i < file.count; i++ )
    {
        const struct CP_Config* config = &file.configs[i];
        int id = 0;
        if( !resolve_id(ctx, type_id, config->debugname, &id) )
        {
            stats->failed++;
            continue;
        }
        uint32_t size = type->pack(ctx, id, config, buffer, 64 * 1024);
        if( size == 0 )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to encode\n", type->name,
                    config->debugname);
            stats->failed++;
            continue;
        }
        if( !RSCache_Dat2EditPutFile(edit, config_table, type->config_kind, id, buffer, size) )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to stage\n", type->name,
                    config->debugname);
            stats->failed++;
            continue;
        }
        stats->records++;
        stats->bytes += (int)size;
    }

    free(buffer);
    cp_config_file_free(&file);

    printf("  %-11s %6d records, %d bytes%s\n", type->name, stats->records, stats->bytes,
           stats->failed ? " (with failures)" : "");
    return 1;
}

int
cp_pack_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* base_cache_dir,
    const char* out_cache_dir)
{
    if( base_cache_dir )
    {
        printf("Copying %s -> %s\n", base_cache_dir, out_cache_dir);
        if( tool_copy_cache_dir(base_cache_dir, out_cache_dir) != 0 )
        {
            fprintf(stderr, "cachepack: failed to copy the base cache\n");
            return 0;
        }
    }

    if( !tool_dat2_open(out_cache_dir, &ctx->profile, &ctx->cache) )
        return 0;
    ctx->cache_open = true;

    int config_table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( config_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        fprintf(stderr, "cachepack: the target cache has no config table\n");
        return 0;
    }

    struct RSCache_Dat2Edit* edit = RSCache_Dat2EditNew(ctx->cache.disk);
    if( !edit )
        return 0;

    /*
     * One walk for the whole run. `configs/` alone for now — `server/scripts/`
     * joins it as rank 1 when the merge lands, and the roots being a list here is
     * what makes that a one-line change rather than a rewrite.
     */
    {
        static const char* const ROOTS[] = { "configs" };
        static const int RANKS[] = { 0 };

        cp_walk_tree(&ctx->walk, ctx->srcdir, ROOTS, RANKS, 1);
    }

    printf("Packing configs from %s\n", ctx->srcdir);
    int total = 0, failed = 0;
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;
        struct CP_PackStats stats;
        if( !pack_type(ctx, i, edit, config_table, &stats) )
        {
            RSCache_Dat2EditFree(edit);
            return 0;
        }
        total += stats.records;
        failed += stats.failed;
    }

    if( total == 0 )
    {
        fprintf(stderr, "cachepack: nothing to write\n");
        RSCache_Dat2EditFree(edit);
        return 0;
    }

    printf("Committing %d records into %s\n", total, out_cache_dir);
    bool ok = RSCache_Dat2EditCommit(edit, out_cache_dir);
    RSCache_Dat2EditFree(edit);
    if( !ok )
    {
        fprintf(stderr, "cachepack: commit failed\n");
        return 0;
    }

    printf("Done. %d records written, %d failed, %d unknown keys, %d unresolved names.\n", total,
           failed, ctx->warn_unknown_key, ctx->warn_unresolved_name);
    /* A failure here is a record the target cache still holds in its old form, not
     * a corrupt one — but the caller should know the pack was partial. */
    return failed == 0;
}

/* ---- verify -------------------------------------------------------------- */

/*
 * The standing bar for every codec in this library: decode a record, write it back,
 * and compare. Here the trip is longer — record -> text -> record — so it measures
 * the text layer as well as the codecs.
 *
 * Two columns, for the same reason the library's own round-trip suite reports two:
 * a mismatch at *identical length* is a re-encoding (opcode order, or an aliased
 * opcode collapsed to its lowest spelling), while a mismatch at a different length
 * is a field that did not survive. High same-length with low exact is expected and
 * harmless; low both is a loss worth chasing.
 */
int
cp_verify_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* digest_dir)
{
    printf("Seeding names from the cache's gameval table...\n");
    cp_names_seed_from_cache(ctx);
    cp_names_report_coverage(ctx);

    printf("%-11s %8s   %8s %8s %8s   %8s %8s\n", "type", "records", "exact", "same-len",
           "differ", "codec-ex", "lost-here");

    uint8_t* buffer = malloc(64 * 1024);
    if( !buffer )
        return 0;
    uint8_t* codec_buffer = malloc(64 * 1024);
    if( !codec_buffer )
    {
        free(buffer);
        return 0;
    }
    int any_loss = 0;
    int text_regression = 0;

    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        if( !sel->all && !(sel->mask & (1u << t)) )
            continue;
        const struct CP_Type* type = cp_type(t);
        if( type->flags & CP_TYPE_NO_ENCODER )
        {
            printf("%-11s %8s   (decode only)\n", type->name, "-");
            continue;
        }
        cp_codec_fn codec = cp_codec_roundtrip(t);

        /*
         * A per-record digest of the bytes `pack` would write.
         *
         * Captured now, while the packer's output is still a pure function of
         * `configs/all.<type>`. Once a server overlay is merged in before encoding,
         * the only way to show that an *unoverlaid* record did not move is to
         * compare against a snapshot taken before the merge existed — a golden
         * captured afterwards proves nothing.
         *
         * Per record, not a whole-cache hash: a whole-cache hash says a byte moved
         * and not which record moved it, and the assertion this feeds is set
         * containment over record ids.
         */
        FILE* digests = NULL;
        if( digest_dir )
        {
            char path[1200];

            snprintf(path, sizeof(path), "%s/%s.digests", digest_dir, type->name);
            digests = fopen(path, "wb");
            if( !digests )
                fprintf(stderr, "cachepack: cannot write %s\n", path);
        }

        struct CP_Group group;
        if( !cp_group_open(ctx, t, &group) )
            continue;

        int records = 0, exact = 0, same_len = 0, differ = 0;
        int codec_exact = 0, lost_here = 0;
        struct CP_Lines lines;
        cp_lines_init(&lines);

        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;

            /* The baseline: what the library's own codec manages on this record. */
            int codec_ok = 0;
            if( codec )
            {
                uint32_t cw = codec(ctx, record, size, codec_buffer, 64 * 1024);
                codec_ok = cw == (uint32_t)size && memcmp(codec_buffer, record, (size_t)size) == 0;
                codec_exact += codec_ok;
            }

            cp_lines_clear(&lines);
            if( !type->unpack(ctx, id, record, size, &lines) )
                continue;

            /*
             * Round-trip through the *text*, not through the struct: serialising
             * the lines and parsing them back is what makes this a test of the
             * escaping and the key names rather than only of the codecs.
             */
            size_t text_size = 0;
            char* text = cp_lines_to_string(&lines, cp_name_ensure(ctx, t, id), &text_size);
            if( !text )
                break;

            struct CP_ConfigFile file;
            int parsed = cp_config_file_load_memory(&file, text, text_size, type->name);
            free(text);
            if( !parsed || file.count != 1 )
            {
                if( parsed )
                    cp_config_file_free(&file);
                differ++;
                records++;
                continue;
            }

            uint32_t written = type->pack(ctx, id, &file.configs[0], buffer, 64 * 1024);
            records++;
            if( digests && written )
                fprintf(digests, "%d=%08x\n", id,
                        RSCache_Crc32Buffer(buffer, (size_t)written));
            int text_ok = written == (uint32_t)size &&
                          memcmp(buffer, record, (size_t)size) == 0;
            if( text_ok )
                exact++;
            else if( written == (uint32_t)size )
                same_len++;
            else
                differ++;
            /* The number that matters: a record the codec reproduced and the text
             * did not is this tool's fault, and nothing else in the report
             * distinguishes it from the library's own documented losses. */
            if( codec_ok && !text_ok )
                lost_here++;
            cp_config_file_free(&file);
        }

        if( digests )
            fclose(digests);
        cp_lines_free(&lines);
        cp_group_free(&group);

        printf("%-11s %8d   %8d %8d %8d   %8d %8d%s\n", type->name, records, exact, same_len,
               differ, codec_exact, lost_here, lost_here ? "  <-- text layer" : "");
        if( differ > 0 )
            any_loss = 1;
        if( lost_here > 0 )
            text_regression = 1;
    }

    free(codec_buffer);
    free(buffer);

    if( any_loss )
        printf("\nRecords in the `differ` column changed length: a field did not survive the\n"
               "trip. `same-len` is a re-encoding (opcode order or an aliased opcode) and\n"
               "costs nothing.\n");
    printf("\n`codec-ex` is what the library's own decode->encode manages on the same\n"
           "records, with no text in between; `lost-here` counts records it reproduced\n"
           "byte-exactly and the text did not. That column is the one this tool owns,\n"
           "and it should be zero — everything else is the library's, and measured.\n");

    /* A non-zero lost-here column is a failure of this tool, so it fails the run.
     * The other columns are the library's fidelity and are reported, not judged. */
    return !text_regression;
}
