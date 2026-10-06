/*
 * DBTABLE / DBROW from the dat2 cache — the client's own database that CS2
 * already reads, now also available to ServerScript.
 *
 * `configs/all.dbtable` / `all.dbrow` are these records as text, in the same
 * authored grammar (`column=` / `data=`) as server/scripts, but the binary is
 * the source of truth for the cache half (ids 0..258): the text is read only
 * for the column names the binary lacks (name_cache_table_columns in
 * torirs_server_db.c). Authored tables under server/scripts (ids 259+) keep
 * priority and are never overwritten.
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
#include "torirs_server_servpack.h"

#include <rscache.h>

#include <datatypes/dat2_config_db.h>
#include <datatypes/dat2_configs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_cache_tables;
static int g_cache_rows;

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
        for( int i = 0; i < src->type_count; i++ )
            ToriRSServer_DbColumnTypeCode(&table->columns[col], i, src->types[i]);
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

/*
 * The db, whole, from the server pack: every table (the cache's and the tree's,
 * merged by cachepack into one record each) and then every row. Column names
 * are not in a client record; they are the dbtable's `column` band
 * (ToriRSServer_ContentLoadPack applies it after this).
 */
int
ToriRSServer_DbLoadPack(struct RSCache_ServerPack* pack)
{
    struct ToriRSServerKindRecords tables;
    struct ToriRSServerKindRecords rows;

    assert(pack);
    ToriRSServer_DbFree();
    g_cache_tables = 0;
    g_cache_rows = 0;
    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_DBTABLE, &tables) )
        return -1;
    for( int i = 0; i < tables.count; i++ )
    {
        struct RSCache_Dat2ConfigDbTable record;

        memset(&record, 0, sizeof(record));
        record.id = tables.ids[i];
        RSCache_Dat2ConfigDbTableDecodeInplace(&record, (char*)tables.files[i], (int)tables.sizes[i]);
        import_table(tables.ids[i], &record);
        RSCache_Dat2ConfigDbTableFreeInplace(&record);
    }
    ToriRSServer_ServPackKindFree(&tables);

    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_DBROW, &rows) )
        return -1;
    for( int i = 0; i < rows.count; i++ )
    {
        struct RSCache_Dat2ConfigDbRow record;

        memset(&record, 0, sizeof(record));
        record.id = rows.ids[i];
        RSCache_Dat2ConfigDbRowDecodeInplace(&record, (char*)rows.files[i], (int)rows.sizes[i]);
        import_row(rows.ids[i], &record);
        RSCache_Dat2ConfigDbRowFreeInplace(&record);
    }
    ToriRSServer_ServPackKindFree(&rows);

    fprintf(stderr, "torirsserver: db loaded from %s (%d tables, %d rows)\n", pack->dir,
            g_cache_tables, g_cache_rows);
    return 0;
}
