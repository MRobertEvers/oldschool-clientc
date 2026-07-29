/*
 * Varbits: named bit ranges inside player variables.
 *
 * The ranges come from the cache (config group 14) and nowhere else. That is
 * deliberate — the *client* reads the same records to unpack what the server
 * packs, so authoring them again in a config would be a second source of truth
 * with nothing keeping the two in step.
 *
 * What content owns is the *names*: content/pack/varbit.pack maps
 * `weapon_category` to 357 and `combat_level` to 13027, imported from
 * OpenRune's gameval table like every other id.
 *
 * Same one-pass decode as mock230_objinfo: the group is 13,876 records at rev
 * 230 and decodes in a few milliseconds, so it is read once at startup rather
 * than lazily per id.
 */
#include "mock230.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VarbitRange
{
    int basevar;
    uint8_t startbit;
    uint8_t endbit;
};

static struct VarbitRange* g_varbits;
static int g_varbit_count;

int
mock230_varbit_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int highest = -1;
    int loaded = 0;

    mock230_varbit_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = 230;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        char parent[512];

        snprintf(parent, sizeof(parent), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(parent);
    }
    if( !disk )
    {
        fprintf(stderr, "mock230: no varbit table (cache '%s' not found)\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_VARBIT);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        fprintf(stderr, "mock230: no varbit config group in '%s'\n", cache_dir);
        return 0;
    }

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files || !archive->file_ids )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    if( highest < 0 )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    g_varbit_count = highest + 1;
    g_varbits = calloc((size_t)g_varbit_count, sizeof(*g_varbits));
    if( !g_varbits )
    {
        g_varbit_count = 0;
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    /* -1 is "absent". A zeroed slot would read as varp 0, which is a real varp,
     * so every unknown varbit would silently alias it. */
    for( int i = 0; i < g_varbit_count; i++ )
        g_varbits[i].basevar = -1;

    for( int i = 0; i < files->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigVarbit entry;

        if( id < 0 || id >= g_varbit_count || files->file_sizes[i] <= 0 )
            continue;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigVarbitDecodeInplace(&entry, files->files[i], files->file_sizes[i]);
        g_varbits[id].basevar = entry.basevar;
        g_varbits[id].startbit = (uint8_t)entry.startbit;
        g_varbits[id].endbit = (uint8_t)entry.endbit;
        loaded++;
        RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    fprintf(stderr, "mock230: varbit table loaded (%d records from %s)\n", loaded, cache_dir);
    return loaded;
}

void
mock230_varbit_free(void)
{
    free(g_varbits);
    g_varbits = NULL;
    g_varbit_count = 0;
}

static const struct VarbitRange*
varbit_range(int varbit_id)
{
    if( !g_varbits || varbit_id < 0 || varbit_id >= g_varbit_count )
        return NULL;
    if( g_varbits[varbit_id].basevar < 0 )
        return NULL;
    return &g_varbits[varbit_id];
}

int
mock230_varbit_get(
    const struct Mock230Player* player,
    int varbit_id)
{
    const struct VarbitRange* range = varbit_range(varbit_id);
    int width;
    uint32_t mask;

    if( !range || range->basevar >= MOCK230_VARP_COUNT )
        return 0;
    width = range->endbit - range->startbit + 1;
    if( width <= 0 || width > 32 )
        return 0;
    mask = width >= 32 ? 0xffffffffu : ((1u << width) - 1u);
    return (int)(((uint32_t)player->varps[range->basevar] >> range->startbit) & mask);
}

int
mock230_varbit_set(
    struct Mock230Server* srv,
    int varbit_id,
    int value)
{
    const struct VarbitRange* range = varbit_range(varbit_id);
    int width;
    uint32_t mask;
    uint32_t current;

    if( !range || range->basevar >= MOCK230_VARP_COUNT )
        return -1;
    width = range->endbit - range->startbit + 1;
    if( width <= 0 || width > 32 )
        return -1;
    mask = width >= 32 ? 0xffffffffu : ((1u << width) - 1u);

    current = (uint32_t)srv->player.varps[range->basevar];
    current &= ~(mask << range->startbit);
    current |= ((uint32_t)value & mask) << range->startbit;
    /* Through the ordinary varp write, so the transmit gate and the
     * small/large encoder choice apply exactly as they would to a plain varp —
     * a varbit is a *view* of a varp, not a second kind of variable. */
    mock230_world_set_varp(srv, range->basevar, (int)current);
    return range->basevar;
}
