/*
 * Dump decoded loc config shapes/models for given loc IDs.
 *
 * Usage:
 *   dump_loc_shapes <cache_dir> <loc_id> [loc_id...]
 */

#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
loc_file_index(
    struct RSCacheDat2Disk_ArchiveReference* ref,
    int loc_id)
{
    if( !ref )
        return -1;
    for( int i = 0; i < ref->children.count; i++ )
    {
        if( ref->children.files[i].id == loc_id )
            return i;
    }
    return -1;
}

static void
dump_loc(
    struct RSCacheShared_FileList* filelist,
    int file_index,
    int loc_id)
{
    int file_size = filelist->file_sizes[file_index];
    struct RSCacheDat2A_ConfigLocation* loc =
        config_locs_new_decode(filelist->files[file_index], file_size);
    if( !loc )
    {
        fprintf(stderr, "loc %d: decode failed\n", loc_id);
        return;
    }

    printf("loc %d name='%s' shapes=%s count=%d file_size=%d\n",
           loc_id,
           loc->name ? loc->name : "",
           loc->shapes ? "yes" : "no",
           loc->shapes_and_model_count,
           file_size);
    if( loc->shapes )
    {
        for( int i = 0; i < loc->shapes_and_model_count; i++ )
        {
            int model = loc->models && loc->models[i] ? loc->models[i][0] : -1;
            printf("  [%d] shape=%d model=%d\n", i, loc->shapes[i], model);
        }
    }
    else if( loc->models && loc->lengths )
    {
        printf("  opcode5 models (%d):", loc->lengths[0]);
        for( int i = 0; i < loc->lengths[0]; i++ )
            printf(" %d", loc->models[0][i]);
        printf("\n");
    }
    if( loc->transform_count > 0 )
    {
        printf("  transform_varbit=%d transform_varp=%d transform_count=%d\n",
               loc->transform_varbit,
               loc->transform_varp,
               loc->transform_count);
        if( loc->transforms )
        {
            int def = loc->transform_count - 1;
            printf("  default_transform_child=%d\n", loc->transforms[def]);
        }
    }

    config_locs_free(loc);
}

int
main(int argc, char** argv)
{
    if( argc < 3 )
    {
        fprintf(stderr, "Usage: %s <cache_dir> <loc_id> [loc_id...]\n", argv[0]);
        return 1;
    }

    const char* cache_dir = argv[1];
    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "Failed to open cache: %s\n", cache_dir);
        return 1;
    }

    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Locs);
    if( !archive )
    {
        fprintf(stderr, "Failed to load loc config archive\n");
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Configs];
    RSCacheDat2Disk_ArchiveInitMetadataFromTable(table, archive);
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( archive->archive_id >= 0 && archive->archive_id < table->archive_count )
        ref = &table->archives[archive->archive_id];

    struct RSCacheShared_FileList* filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !filelist || !ref )
    {
        fprintf(stderr, "Failed to decode loc file list\n");
        RSCacheShared_FileListFree(filelist);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    for( int a = 2; a < argc; a++ )
    {
        int loc_id = atoi(argv[a]);
        int idx = loc_file_index(ref, loc_id);
        if( idx < 0 )
        {
            fprintf(stderr, "loc %d: not found in config archive\n", loc_id);
            continue;
        }
        dump_loc(filelist, idx, loc_id);
    }

    RSCacheShared_FileListFree(filelist);
    RSCacheDat2Disk_Free(cache);
    return 0;
}
