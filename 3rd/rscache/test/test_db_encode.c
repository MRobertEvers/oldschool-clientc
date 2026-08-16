/*
 * dbrow / dbtable encoders, against every record in a real cache.
 *
 * These two types were decode-only: `cachepack` carried `CP_TYPE_NO_ENCODER` for
 * them, so 16,712 dbrows and 247 dbtables exported as text that could never be
 * packed back — editing `configs/all.dbrow` did nothing, silently. The decode had
 * been derived and validated; the column *packing* was left unverified, so no
 * encoder was written.
 *
 * The bar is byte-identity, not semantic round-trip. A semantic check would pass
 * on an encoder that reorders columns or drops an empty value block, and those are
 * exactly the two things the struct cannot express and the bytes can:
 *
 *   - the decoder stores a column *at its id*, so the order the source listed
 *     columns in is not recoverable;
 *   - on a DBTABLE, a column with the 0x80 flag and zero tuples decodes
 *     identically to one with the flag clear.
 *
 * Both readings are guesses until every record in the cache agrees with them.
 * That is what this measures.
 */

#include "datatypes/dat2_config_db.h"
#include "dat2disk.h"
#include "filelist.h"
#include "rscache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(int ok, const char* what)
{
    g_checks++;
    if( !ok )
        g_failures++;
    printf("db-encode: %-58s %s\n", what, ok ? "ok" : "FAILED");
}

struct Tally
{
    int records;
    int exact;
    int differ;
    int first_bad;
    int alloc_slack;
};

static void
run_group(
    struct RSCache_Dat2Disk* disk,
    int table,
    int group,
    int is_table,
    struct Tally* tally)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, group);
    struct RSCache_FileList* files;

    if( !archive )
        return;
    if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    for( int f = 0; f < files->file_count; f++ )
    {
        const uint8_t* src = (const uint8_t*)files->files[f];
        int size = files->file_sizes[f];
        uint8_t* out;
        uint32_t bound, written;

        if( size <= 0 )
            continue;
        tally->records++;

        if( is_table )
        {
            struct RSCache_Dat2ConfigDbTable rec;

            memset(&rec, 0, sizeof(rec));
            RSCache_Dat2ConfigDbTableDecodeInplace(&rec, src, size);
            bound = RSCache_Dat2ConfigDbTableEncodeBound(&rec);
            out = malloc(bound);
            written = out ? RSCache_Dat2ConfigDbTableEncode(&rec, out, bound) : 0;
            RSCache_Dat2ConfigDbTableFreeInplace(&rec);
        }
        else
        {
            struct RSCache_Dat2ConfigDbRow rec;

            memset(&rec, 0, sizeof(rec));
            RSCache_Dat2ConfigDbRowDecodeInplace(&rec, src, size);
            bound = RSCache_Dat2ConfigDbRowEncodeBound(&rec);
            out = malloc(bound);
            written = out ? RSCache_Dat2ConfigDbRowEncode(&rec, out, bound) : 0;
            RSCache_Dat2ConfigDbRowFreeInplace(&rec);
        }

        /* Is the column-array size derivable from the present columns? The text
         * form emits only present columns, so if `alloc` can exceed the highest
         * present id it has to be written down explicitly. */
        if( getenv("DB_ENCODE_ALLOC") )
        {
            struct RSCache_Dat2ConfigDbRow r;
            struct RSCache_Dat2ConfigDbTable tb;
            int count, highest = -1;
            const struct RSCache_DbColumn* cols;

            if( is_table )
            {
                memset(&tb, 0, sizeof(tb));
                RSCache_Dat2ConfigDbTableDecodeInplace(&tb, src, size);
                count = tb.column_count;
                cols = tb.columns;
            }
            else
            {
                memset(&r, 0, sizeof(r));
                RSCache_Dat2ConfigDbRowDecodeInplace(&r, src, size);
                count = r.column_count;
                cols = r.columns;
            }
            for( int c = 0; c < count; c++ )
            {
                if( cols && cols[c].present )
                    highest = c;
            }
            if( count != highest + 1 )
                tally->alloc_slack++;
            if( is_table )
                RSCache_Dat2ConfigDbTableFreeInplace(&tb);
            else
                RSCache_Dat2ConfigDbRowFreeInplace(&r);
        }

        if( out && written == (uint32_t)size && memcmp(out, src, (size_t)size) == 0 )
        {
            tally->exact++;
        }
        else
        {
            if( tally->differ == 0 )
            {
                tally->first_bad = f;
                if( getenv("DB_ENCODE_DUMP") )
                {
                    int n = size < 48 ? size : 48;

                    printf("  src(%d):", size);
                    for( int b = 0; b < n; b++ )
                        printf(" %02x", src[b]);
                    printf("\n  out(%u):", written);
                    for( int b = 0; b < (int)written && b < 48; b++ )
                        printf(" %02x", out[b]);
                    printf("\n");
                }
            }
            tally->differ++;
        }
        free(out);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache.osrs239";
    struct RSCache_Dat2Disk* disk;
    struct Tally rows = { 0, 0, 0, -1, 0 };
    struct Tally tables = { 0, 0, 0, -1, 0 };
    int config_table;

    struct RSCache profile;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Loud, and not a pass. A round-trip check that quietly succeeds on an
         * absent cache is worse than no check — the same rule
         * test_cachepack_fidelity.sh follows. */
        printf("db-encode: SKIPPED — no cache at %s\n", cache_dir);
        return 0;
    }
    /* The table lookup is per-game, so the disk has to be told which game it is;
     * without a profile every table resolves ABSENT. */
    if( !RSCache_ProfileByName("osrs239", &profile) )
    {
        printf("db-encode: SKIPPED — no osrs239 profile\n");
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    config_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( config_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        printf("db-encode: SKIPPED — %s has no config table\n", cache_dir);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    run_group(disk, config_table, RSCACHE_DAT2_CONFIG_KIND_DBROW, 0, &rows);
    run_group(disk, config_table, RSCACHE_DAT2_CONFIG_KIND_DBTABLE, 1, &tables);

    printf("db-encode: dbrow   %d records, %d exact, %d differ\n", rows.records,
           rows.exact, rows.differ);
    printf("db-encode: dbtable %d records, %d exact, %d differ\n", tables.records,
           tables.exact, tables.differ);
    if( rows.differ )
        printf("db-encode:   first differing dbrow id %d\n", rows.first_bad);
    if( tables.differ )
        printf("db-encode:   first differing dbtable id %d\n", tables.first_bad);

    if( getenv("DB_ENCODE_ALLOC") )
        printf("db-encode: alloc != highest+1 — dbrow %d, dbtable %d\n",
               rows.alloc_slack, tables.alloc_slack);
    check(rows.records > 0, "the cache has dbrow records to check");
    check(rows.differ == 0, "every dbrow re-encodes to its own bytes");
    check(tables.records > 0, "the cache has dbtable records to check");
    check(tables.differ == 0, "every dbtable re-encodes to its own bytes");

    RSCache_Dat2DiskFree(disk);

    if( g_failures )
    {
        printf("db-encode: FAILURES (%d of %d)\n", g_failures, g_checks);
        return 1;
    }
    printf("db-encode: all %d checks passed\n", g_checks);
    return 0;
}
