/*
 * DBTABLE / DBROW from the dat2 cache — the client's own database that CS2
 * already reads, now also available to ServerScript.
 *
 * `configs/all.dbtable` / `all.dbrow` are the machine export of these records
 * (`columndef=` / `values=`), and the text reader in mock230_db.c only accepts
 * the authored grammar (`column=` / `data=`). So the binary is the source of
 * truth for the cache half (ids 0..258); authored tables under server/scripts
 * (ids 259+) keep priority and are never overwritten.
 *
 * Same recipe as mock230_structinfo.c: profile, CONFIGS table, KIND_DBTABLE /
 * KIND_DBROW archives, decode each file. Column names are not in the binary —
 * they live in gameval archive 10 — so cache-only columns are nameless and
 * scripts address them by packed id. Tables that content also authors (e.g.
 * `quest.dbtable` with densified columns 0..N matching the sparse cache ids)
 * keep those names.
 */

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_db.h"

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
    profile.revision = MOCK230_CACHE_REVISION;

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
    struct Mock230DbTable* table;
    char fallback[64];

    symbol = mock230_content_symbol_name(MOCK230_PACK_DBTABLE, table_id);
    if( !symbol )
    {
        snprintf(fallback, sizeof(fallback), "dbtable_%d", table_id);
        symbol = fallback;
    }

    table = mock230_db_ensure_table(table_id, symbol, 0);
    if( !table )
        return;
    /* Authored schema wins — only fill columns when the table is empty. */
    if( table->column_count > 0 )
        return;

    for( int col = 0; col < record->column_count; col++ )
    {
        const struct RSCache_DbColumn* src = &record->columns[col];
        int is_string[MOCK230_DB_TUPLE_MAX];

        if( !src->present || src->type_count <= 0 )
            continue;
        if( col >= MOCK230_DB_COLUMN_MAX )
            continue;
        if( src->type_count > MOCK230_DB_TUPLE_MAX )
            continue;
        for( int i = 0; i < src->type_count; i++ )
            is_string[i] = RSCache_DbTypeIsString(src->types[i]) ? 1 : 0;
        mock230_db_column_define(table, col, NULL, src->type_count, is_string);
        if( src->tuple_count > 0 && src->values )
        {
            int total = src->tuple_count * src->type_count;
            struct Mock230DbValue* defaults = calloc((size_t)total, sizeof(*defaults));

            if( defaults )
            {
                for( int i = 0; i < total; i++ )
                {
                    defaults[i].value = src->values[i].int_value;
                    defaults[i].text = src->values[i].is_string
                        ? src->values[i].string_value : NULL;
                }
                mock230_db_column_defaults_set(table, col, defaults, total);
                free(defaults);
            }
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
    struct Mock230DbRow* row;
    const struct Mock230DbTable* table;
    char fallback[64];

    if( record->table_id < 0 )
        return;

    symbol = mock230_content_symbol_name(MOCK230_PACK_DBROW, row_id);
    if( !symbol )
    {
        snprintf(fallback, sizeof(fallback), "dbrow_%d", row_id);
        symbol = fallback;
    }

    table = mock230_db_table(record->table_id);
    if( !table || table->column_count <= 0 )
        return;

    row = mock230_db_ensure_row(row_id, symbol, record->table_id);
    if( !row )
        return;

    for( int col = 0; col < record->column_count; col++ )
    {
        const struct RSCache_DbColumn* src = &record->columns[col];
        struct Mock230DbValue* values;
        int total;

        if( !src->present || src->type_count <= 0 || src->tuple_count <= 0 )
            continue;
        if( col >= table->column_count || table->columns[col].type_count <= 0 )
            continue;
        if( col >= MOCK230_DB_COLUMN_MAX )
            continue;

        total = src->tuple_count * src->type_count;
        values = calloc((size_t)total, sizeof(*values));
        if( !values )
            continue;
        for( int i = 0; i < total; i++ )
        {
            if( src->values[i].is_string )
            {
                values[i].text = src->values[i].string_value;
                values[i].value = 0;
            }
            else
            {
                values[i].value = src->values[i].int_value;
                values[i].text = NULL;
            }
        }
        mock230_db_row_column_set(row, col, values, total);
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

int
mock230_db_load_cache(const char* cache_dir)
{
    struct RSCache_Dat2Disk* disk;
    int configs_table;
    int tables_seen;
    int rows_seen;

    g_cache_tables = 0;
    g_cache_rows = 0;

    disk = open_cache(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "mock230: no db tables (cache '%s' not found)\n", cache_dir);
        return 1;
    }

    configs_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);

    /* Tables first: import_row needs the schema (authored or just-loaded). */
    tables_seen = load_kind(
        disk, configs_table, RSCACHE_DAT2_CONFIG_KIND_DBTABLE, on_table, 1);
    rows_seen = load_kind(
        disk, configs_table, RSCACHE_DAT2_CONFIG_KIND_DBROW, on_row, 0);

    RSCache_Dat2DiskFree(disk);

    fprintf(stderr,
            "mock230: db tables loaded (%d tables, %d rows from %s; %d schema(s) "
            "installed, %d row(s) filled; %d archive table(s), %d archive row(s))\n",
            mock230_db_table_count(), mock230_db_total_row_count(), cache_dir,
            g_cache_tables, g_cache_rows, tables_seen, rows_seen);
    return 1;
}
