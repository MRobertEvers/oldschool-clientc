/*
 * Dump dat2 map index (table 5) archive group -> name-hash mapping.
 *
 * Usage:
 *   dump_map_index <cache_dir>
 *
 * Prints TSV lines: group\tidentifier
 */

#include "osrs/rscache/dat2disk/dat2disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IDENTIFIERS 0x1

int
main(int argc, char** argv)
{
    if( argc != 2 )
    {
        fprintf(stderr, "Usage: %s <cache_dir>\n", argv[0]);
        return 1;
    }

    char dat2_path[1024];
    snprintf(dat2_path, sizeof(dat2_path), "%s/main_file_cache.dat2", argv[1]);

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewUninitialized();
    cache->directory = strdup(argv[1]);
    cache->_dat2_file = fopen(dat2_path, "rb");
    if( !cache->_dat2_file )
    {
        fprintf(stderr, "Failed to open cache: %s\n", dat2_path);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct RSCacheDat2Disk_Archive* table_archive =
        RSCacheDat2Disk_ArchiveNewReferenceTableLoad(cache, RSCacheDat2Disk_Table_Maps);
    if( !table_archive )
    {
        fprintf(stderr, "Failed to load map reference table\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct RSCacheDat2Disk_ReferenceTable* table =
        RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCacheDat2Disk_ArchiveFree(table_archive);
    if( !table )
    {
        fprintf(stderr, "Failed to decode map reference table\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    if( (table->flags & FLAG_IDENTIFIERS) == 0 )
    {
        fprintf(stderr, "Map reference table has no name identifiers\n");
        RSCacheDat2Disk_ReferenceTableFree(table);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    for( int i = 0; i < table->id_count; i++ )
    {
        int id = table->ids[i];
        struct RSCacheDat2Disk_ArchiveReference* archive = &table->archives[id];
        if( archive->index < 0 )
            continue;
        printf("%d\t%d\n", archive->index, archive->identifier);
    }

    RSCacheDat2Disk_ReferenceTableFree(table);
    RSCacheDat2Disk_Free(cache);
    return 0;
}
