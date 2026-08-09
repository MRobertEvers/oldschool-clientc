/*
 * Dump dat2 interfaces reference table metadata.
 *
 * Usage:
 *   dump_interface_index <cache_dir>
 *
 * Without archive identifiers (flags&1==0), prints:
 *   archive_index\t-1
 *
 * With identifiers, prints:
 *   archive_index\tidentifier
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
        RSCacheDat2Disk_ArchiveNewReferenceTableLoad(cache, RSCacheDat2Disk_Table_Interfaces);
    if( !table_archive )
    {
        fprintf(stderr, "Failed to load interfaces reference table\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct RSCacheDat2Disk_ReferenceTable* table =
        RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCacheDat2Disk_ArchiveFree(table_archive);
    if( !table )
    {
        fprintf(stderr, "Failed to decode interfaces reference table\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    fprintf(stderr, "interfaces ref: format=%d flags=%d id_count=%d archive_count=%d\n",
            table->format, table->flags, table->id_count, table->archive_count);

    if( (table->flags & FLAG_IDENTIFIERS) == 0 )
    {
        for( int i = 0; i < table->id_count; i++ )
        {
            int id = table->ids[i];
            struct RSCacheDat2Disk_ArchiveReference* archive = &table->archives[id];
            if( archive->index < 0 )
                continue;
            printf("%d\t-1\n", archive->index);
        }
        RSCacheDat2Disk_ReferenceTableFree(table);
        RSCacheDat2Disk_Free(cache);
        return 0;
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
