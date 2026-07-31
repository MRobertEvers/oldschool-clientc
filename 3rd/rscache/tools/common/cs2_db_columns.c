#include "cs2_db_columns.h"

#include "datatypes/dat2_config_db.h"
#include "datatypes/dat2_configs.h"
#include "filelist.h"
#include "reference_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ToolDbColumns
{
    /* Indexed by table id, sparse: a cache's dbtable ids are not consecutive. */
    struct RSCache_Dat2ConfigDbTable* tables;
    int table_count;
};

struct ToolDbColumns*
tool_db_columns_load(struct RSCache_Dat2Disk* disk)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return NULL;

    /* Asked for, not assumed. The DBTABLE group arrived with OldSchool's client
     * database and no earlier cache has one, so loading it blind made every
     * pre-2022 cache print the library's "failed to read dat2 index entry" on
     * stdout — into the middle of the decompiled source, since that is where a
     * single-script decompile goes. */
    struct RSCache_ReferenceTable* reference = disk->tables[table];
    if( !reference || RSCACHE_DAT2_CONFIG_KIND_DBTABLE >= reference->archive_count ||
        reference->archives[RSCACHE_DAT2_CONFIG_KIND_DBTABLE].index < 0 )
        return NULL;

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_DBTABLE);
    if( !archive )
        return NULL;
    /* A group loaded by id carries no file list until the reference table is
     * consulted; without this it decodes as zero records. */
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    int highest = -1;
    for( int i = 0; i < files->file_count; i++ )
    {
        int id = archive->file_ids ? archive->file_ids[i] : i;
        if( id > highest )
            highest = id;
    }

    struct ToolDbColumns* columns = calloc(1, sizeof(*columns));
    if( columns && highest >= 0 )
    {
        columns->table_count = highest + 1;
        columns->tables = calloc((size_t)columns->table_count, sizeof(*columns->tables));
        if( !columns->tables )
        {
            free(columns);
            columns = NULL;
        }
    }
    if( columns && columns->tables )
    {
        for( int i = 0; i < files->file_count; i++ )
        {
            int id = archive->file_ids ? archive->file_ids[i] : i;
            if( id < 0 || id >= columns->table_count )
                continue;
            RSCache_Dat2ConfigDbTableDecodeInplace(
                &columns->tables[id], files->files[i], files->file_sizes[i]);
        }
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return columns;
}

void
tool_db_columns_free(struct ToolDbColumns* columns)
{
    if( !columns )
        return;
    for( int i = 0; i < columns->table_count; i++ )
        RSCache_Dat2ConfigDbTableFreeInplace(&columns->tables[i]);
    free(columns->tables);
    free(columns);
}

int
tool_db_columns_lookup(
    void* user,
    int table_id,
    int column_id,
    enum RSCache_CS2_Type* out,
    int capacity)
{
    /* CS2_DB_TRACE=1 says which lookup failed. A miss is not an error here — it
     * makes db_getfield fall back to one int — so without this the only symptom
     * is a stack underflow several opcodes later. */
    static int trace = -1;
    if( trace < 0 )
        trace = getenv("CS2_DB_TRACE") != NULL;

    struct ToolDbColumns* columns = (struct ToolDbColumns*)user;
    if( !columns || table_id < 0 || table_id >= columns->table_count )
    {
        if( trace )
            fprintf(stderr, "DB table %d: not in this cache (have %d)\n", table_id,
                    columns ? columns->table_count : 0);
        return -1;
    }

    struct RSCache_Dat2ConfigDbTable* entry = &columns->tables[table_id];
    if( column_id < 0 || column_id >= entry->column_count )
    {
        if( trace )
            fprintf(stderr, "DB table %d column %d: out of range (%d columns)\n", table_id,
                    column_id, entry->column_count);
        return -1;
    }
    struct RSCache_DbColumn* column = &entry->columns[column_id];
    if( !column->present || column->type_count <= 0 || !column->types )
    {
        if( trace )
            fprintf(stderr, "DB table %d column %d: not present\n", table_id, column_id);
        return -1;
    }
    if( column->type_count > capacity )
        return -1;

    for( int i = 0; i < column->type_count; i++ )
    {
        /* `int` or `string`, and no finer.
         *
         * A dbtable's field types are ScriptVarType *ordinals*, not the type
         * descriptor characters CS2 uses elsewhere — table 78's second column
         * is `0 36 36 23`, which as descriptors would be NUL, '$', '$' and end
         * of medium. Only the string ordinal is established (36, exhaustively,
         * across osrs230/239/jan2026), so that is all this claims. It is also
         * all the decompiler needs: which *stack* a field occupies decides the
         * shape, and a narrower type would be a guess dressed as a fact.
         *
         * Running the ordinals through RSCache_CS2_TypeOfDescAuto instead
         * returned "no such type" for every one of them, which the decompiler
         * reads as "column unknown" and falls back to a single int — the exact
         * symptom this function exists to remove. */
        out[i] = RSCache_DbTypeIsString(column->types[i]) ? RSCACHE_CS2_TYPE_STRING
                                                          : RSCACHE_CS2_TYPE_INT;
    }
    return column->type_count;
}
