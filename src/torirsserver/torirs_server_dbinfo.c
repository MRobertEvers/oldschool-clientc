/*
 * DBTABLE / DBROW from the dat2 cache — the client's own database that CS2
 * already reads, now also available to ServerScript.
 *
 * `configs/all.dbtable` / `all.dbrow` are the machine export of these records
 * (`columndef=` / `values=`), and the text reader in torirs_server_db.c only accepts
 * the authored grammar (`column=` / `data=`). So the binary is the source of
 * truth for the cache half (ids 0..258); authored tables under server/scripts
 * (ids 259+) keep priority and are never overwritten.
 *
 * Same recipe as torirs_server_structinfo.c: profile, CONFIGS table, KIND_DBTABLE /
 * KIND_DBROW archives, decode each file. Column names are not in the binary —
 * they live in gameval archive 10 — so cache-only columns are nameless and
 * scripts address them by packed id. Tables that content also authors (e.g.
 * `quest.dbtable` with densified columns 0..N matching the sparse cache ids)
 * keep those names.
 */

#include "torirs_server.h"
#include <assert.h>
#include "torirs_server_content.h"
#include "torirs_server_db.h"

#include <rscache.h>

#include <datatypes/dat2_config_db.h>
#include <datatypes/dat2_configs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_cache_tables;
static int g_cache_rows;

static struct RSCache_Dat2Disk*
open_cache(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( disk )
        RSCache_Dat2DiskSetProfile(disk, &profile);
    return disk;
}

static void
import_table(
    int table_id,
    const struct RSCache_Dat2ConfigDbTable* record)
{
    const char* symbol;
    struct ToriRSServerDbTable* table;
    char fallback[64];

    symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_DBTABLE, table_id);
    if( !symbol )
    {
        snprintf(fallback, sizeof(fallback), "dbtable_%d", table_id);
        symbol = fallback;
    }

    table = ToriRSServer_DbEnsureTable(table_id, symbol, 0);
    if( !table )
        return;
    /* Authored schema wins — only fill columns when the table is empty. */
    if( table->column_count > 0 )
        return;

    for( int col = 0; col < record->column_count; col++ )
    {
        const struct RSCache_DbColumn* src = &record->columns[col];
        int is_string[TORIRSSERVER_DB_TUPLE_MAX];

        if( !src->present || src->type_count <= 0 )
            continue;
        if( col >= TORIRSSERVER_DB_COLUMN_MAX )
            continue;
        if( src->type_count > TORIRSSERVER_DB_TUPLE_MAX )
            continue;
        for( int i = 0; i < src->type_count; i++ )
            is_string[i] = RSCache_DbTypeIsString(src->types[i]) ? 1 : 0;
        ToriRSServer_DbColumnDefine(table, col, NULL, src->type_count, is_string);
        if( src->tuple_count > 0 && src->values )
        {
            int total = src->tuple_count * src->type_count;
            struct ToriRSServerDbValue* defaults = calloc((size_t)total, sizeof(*defaults));

            assert(defaults);
            for( int i = 0; i < total; i++ )
            {
                /* One member, never both — the value is a union now, and the
                 * setter re-reads it by the schema position. */
                if( src->values[i].is_string )
                    defaults[i].text = src->values[i].string_value;
                else
                    defaults[i].value = src->values[i].int_value;
            }
            ToriRSServer_DbColumnDefaultsSet(table, col, defaults, total);
            free(defaults);
        }
    }
    g_cache_tables++;
}

static void
import_row(
    int row_id,
    const struct RSCache_Dat2ConfigDbRow* record)
{
    const char* symbol;
    struct ToriRSServerDbRow* row;
    const struct ToriRSServerDbTable* table;
    char fallback[64];

    if( record->table_id < 0 )
        return;

    symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_DBROW, row_id);
    if( !symbol )
    {
        snprintf(fallback, sizeof(fallback), "dbrow_%d", row_id);
        symbol = fallback;
    }

    table = ToriRSServer_DbTable(record->table_id);
    if( !table || table->column_count <= 0 )
        return;

    row = ToriRSServer_DbEnsureRow(row_id, symbol, record->table_id);
    if( !row )
        return;

    for( int col = 0; col < record->column_count; col++ )
    {
        const struct RSCache_DbColumn* src = &record->columns[col];
        struct ToriRSServerDbValue* values;
        int total;

        if( !src->present || src->type_count <= 0 || src->tuple_count <= 0 )
            continue;
        if( col >= table->column_count || table->columns[col].type_count <= 0 )
            continue;
        if( col >= TORIRSSERVER_DB_COLUMN_MAX )
            continue;

        total = src->tuple_count * src->type_count;
        values = calloc((size_t)total, sizeof(*values));
        assert(values);
        for( int i = 0; i < total; i++ )
        {
            /* One member, never both — the value is a union now, and the
             * setter re-reads it by the schema position. */
            if( src->values[i].is_string )
                values[i].text = src->values[i].string_value;
            else
                values[i].value = src->values[i].int_value;
        }
        ToriRSServer_DbRowColumnSet(row, col, values, total);
        free(values);
    }
    g_cache_rows++;
}

static int
load_kind(
    struct RSCache_Dat2Disk* disk,
    int configs_table,
    int kind,
    void (*on_record)(int id, const void* record),
    int is_table)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int loaded = 0;

    archive = RSCache_Dat2DiskArchiveNewLoad(disk, configs_table, kind);
    if( !archive )
        return 0;
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        if( is_table )
        {
            struct RSCache_Dat2ConfigDbTable record;

            memset(&record, 0, sizeof(record));
            record.id = archive->file_ids[i];
            RSCache_Dat2ConfigDbTableDecodeInplace(
                &record, files->files[i], files->file_sizes[i]);
            on_record(archive->file_ids[i], &record);
            RSCache_Dat2ConfigDbTableFreeInplace(&record);
        }
        else
        {
            struct RSCache_Dat2ConfigDbRow record;

            memset(&record, 0, sizeof(record));
            record.id = archive->file_ids[i];
            RSCache_Dat2ConfigDbRowDecodeInplace(
                &record, files->files[i], files->file_sizes[i]);
            on_record(archive->file_ids[i], &record);
            RSCache_Dat2ConfigDbRowFreeInplace(&record);
        }
        loaded++;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return loaded;
}

static void
on_table(int id, const void* record)
{
    import_table(id, (const struct RSCache_Dat2ConfigDbTable*)record);
}

static void
on_row(int id, const void* record)
{
    import_row(id, (const struct RSCache_Dat2ConfigDbRow*)record);
}

/*
 * The cache's DBTABLE and DBROW halves load at two different points in boot,
 * and the gap between them is where the content tree goes.
 *
 * A `.dbrow` in `server/scripts` names its table by symbol, and the symbol
 * namespace is shared: `poh_hotspot` is a *cache* table (id 112) that authored
 * rows legitimately extend, exactly as `poh_workshop_functions.rs2` reads it
 * back through `poh_hotspot:builddata`. Loading every cache table only after
 * the authored rows meant that name resolved against nothing, and the 82
 * flatpack group rows reported `names unknown table` and then 82 more
 * `data= before table=` — the schema had not arrived yet, not the file being
 * wrong.
 *
 * Rows still load *after* the tree, because that half is a merge:
 * `ToriRSServer_DbEnsureRow` fills a cache id onto whatever the tree already
 * stated, so an authored value wins over the cache's.
 */
int
ToriRSServer_DbLoadCacheTables(const char* cache_dir)
{
    struct RSCache_Dat2Disk* disk;
    int tables_seen;

    g_cache_tables = 0;

    disk = open_cache(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "torirsserver: no db tables (cache '%s' not found)\n", cache_dir);
        return 1;
    }

    tables_seen = load_kind(disk,
                            RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
                            RSCACHE_DAT2_CONFIG_KIND_DBTABLE, on_table, 1);
    RSCache_Dat2DiskFree(disk);

    fprintf(stderr, "torirsserver: db schemas loaded (%d schema(s) installed from %s; "
                    "%d archive table(s))\n",
            g_cache_tables, cache_dir, tables_seen);
    return 1;
}

int
ToriRSServer_DbLoadCacheRows(const char* cache_dir)
{
    struct RSCache_Dat2Disk* disk;
    int rows_seen;

    g_cache_rows = 0;

    disk = open_cache(cache_dir);
    if( !disk )
        return 1; /* ToriRSServer_DbLoadCacheTables already said so */

    rows_seen = load_kind(disk,
                          RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
                          RSCACHE_DAT2_CONFIG_KIND_DBROW, on_row, 0);
    RSCache_Dat2DiskFree(disk);

    fprintf(stderr,
            "torirsserver: db tables loaded (%d tables, %d rows from %s; %d row(s) "
            "filled; %d archive row(s))\n",
            ToriRSServer_DbTableCount(), ToriRSServer_DbTotalRowCount(), cache_dir,
            g_cache_rows, rows_seen);
    return 1;
}
