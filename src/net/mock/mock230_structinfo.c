/*
 * `struct` config records from the cache — which are nothing but a param map.
 *
 * `struct RSCache_Dat2ConfigStruct` is `{ int id; struct RSCache_Params params; }`
 * and that is the whole record, so this file is the param table and nothing
 * else. `struct_param` is the only reader.
 *
 * Same recipe as mock230_npcinfo.c: profile, CONFIGS table, the KIND_STRUCT
 * archive, the file list, decode each. The decoder itself
 * (`RSCache_Dat2ConfigStructDecodeInplace`) has been linked into this binary
 * since rscache landed and had no caller anywhere in src/ — PORTING_GUIDE §1's
 * rule that a linked decoder is reused rather than reimplemented is literal
 * here, there was nothing to write.
 *
 * ── Why the whole group is decoded at boot ───────────────────────────
 *
 * "Lazy" is not available as a design. Struct records are individually-
 * addressed files inside *one* dat2 group and the group is the unit of
 * compression, so reaching one record means decompressing all 3,988. The only
 * real choice is what to retain afterwards, and the answer is the params.
 *
 * Measured on cache.osrs239: 3,988 records, 3,278 of them carrying params,
 * 20,751 rows of which 6,115 are strings and none are RSCACHE_PARAM_LONG.
 * Retained is ~486 KB flat plus the strings — the same order as the obj table's
 * 1.26 MB, which this project already accepted. The transient
 * `Dat2DiskNewFromDirectory` is the same cost class boot already pays four
 * times over for objinfo / npcinfo / seqinfo / varbit.
 */

#include "mock230.h"
#include "mock230_paramtable.h"

#include <rscache.h>

#include <datatypes/dat2_config_struct.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Mock230ParamTable g_struct_params;
static int g_struct_records;

const struct Mock230ParamRow*
mock230_struct_param(
    int struct_id,
    int param_id)
{
    return mock230_paramtable_find(&g_struct_params, struct_id, param_id);
}

int
mock230_structinfo_count(void)
{
    return g_struct_records;
}

int
mock230_structinfo_param_count(void)
{
    return g_struct_params.count;
}

int
mock230_structinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;

    mock230_structinfo_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = MOCK230_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Run from src/ as well as from the repo root, like objinfo. */
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( !disk )
    {
        fprintf(stderr, "mock230: no struct params (cache '%s' not found)\n", cache_dir);
        return 0;
    }

    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_STRUCT);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        fprintf(stderr, "mock230: no struct config archive in '%s'\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigStruct record;

        if( files->file_sizes[i] <= 0 )
            continue;
        memset(&record, 0, sizeof(record));
        record.id = archive->file_ids[i];
        RSCache_Dat2ConfigStructDecodeInplace(&record, files->files[i], files->file_sizes[i]);
        mock230_paramtable_read(&g_struct_params, archive->file_ids[i], &record.params);
        RSCache_Dat2ConfigStructFreeInplace(&record);
    }

    /* Binary-searched, so it has to actually be ordered — and a record's own
     * params do not arrive ordered. 1,847 of the 2,833 struct records carrying
     * two or more params are out of key order in cache.osrs239. */
    mock230_paramtable_sort(&g_struct_params);

    /* Read the count before the free, not after: the archive owns it. */
    g_struct_records = archive->file_count;

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);

    fprintf(stderr, "mock230: struct params loaded (%d records from %s, %d rows in %zu KB)\n",
            g_struct_records, cache_dir, g_struct_params.count,
            mock230_paramtable_bytes(&g_struct_params) / 1024);
    return 1;
}

void
mock230_structinfo_free(void)
{
    mock230_paramtable_free(&g_struct_params);
    g_struct_records = 0;
}
