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

#define FLAG_IDENTIFIERS 0x1

int
main(int argc, char** argv)
{
    if( argc != 2 )
    {
        fprintf(stderr, "Usage: %s <cache_dir>\n", argv[0]);
        return 1;
    }

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(argv[1]);
    if( !cache )
    {
        fprintf(stderr, "Failed to open cache: %s\n", argv[1]);
        return 1;
    }

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Maps];
    if( !table )
    {
        fprintf(stderr, "Failed to load map reference table\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    if( (table->flags & FLAG_IDENTIFIERS) == 0 )
    {
        fprintf(stderr, "Map reference table has no name identifiers\n");
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

    RSCacheDat2Disk_Free(cache);
    return 0;
}
