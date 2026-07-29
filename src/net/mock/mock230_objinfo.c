/*
 * Obj metadata for the mock server, straight out of the same cache the client
 * boots from.
 *
 * Only four fields matter here, and three of them are the equipment story:
 * `wearpos_1` is the slot an item goes in, and `wearpos_2` / `wearpos_3` are
 * the extra slots it claims — which is how the cache encodes both "this bow is
 * two-handed" (weapon + shield) and "this full helm covers your hair and jaw"
 * (head + hair + jaw). Deriving equipment from the cache instead of a
 * hand-written table means every wearable item in the game works, and the
 * appearance hiding rules come out right for free.
 *
 * The whole obj table decodes in about 0.1 s, so it is read once at startup
 * rather than lazily per id.
 */
#include "mock230.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Mock230ObjInfo* g_objs;
static int g_obj_count;

static const struct Mock230ObjInfo k_unknown = {
    .name = "item",
    .wearpos = -1,
    .wearpos_2 = -1,
    .wearpos_3 = -1,
    .stackable = 0,
};

/*
 * Copy the combat bonuses out of the record's param table.
 *
 * Param ids 0..11 are the twelve equipment bonuses (see Mock230CombatParam) and
 * 14 is the attack rate in ticks — an OldSchool convention, documented by
 * OpenRune's ParamMapper and confirmed against cache.osrs230 before anything
 * was built on it. Anything else in the table is left alone: `params` also
 * carries skill requirements, slayer categories and quest flags.
 *
 * `kinds[i] != 0` means a string param, whose `values[i]` is a `char*` — reading
 * it as an int is a wild pointer dereference, not a wrong number.
 */
static void
read_combat_params(
    const struct RSCache_Params* params,
    int* bonus,
    int* attackrate,
    int* has_params)
{
    for( int i = 0; i < params->count; i++ )
    {
        int key = params->keys[i];
        int value;

        if( params->kinds[i] != 0 || !params->values[i] )
            continue;
        value = *(const int*)params->values[i];

        if( key >= 0 && key < 12 )
        {
            bonus[key] = value;
            *has_params = 1;
        }
        else if( key == 14 )
        {
            *attackrate = value;
            *has_params = 1;
        }
    }
}

/* Which of the three melee bonuses a weapon swings with: whichever attack bonus
 * is largest. A scimitar's slash beats its stab, a mace's crush beats both.
 * Ties go to stab, matching the enum order, and a weapon with no attack bonuses
 * at all lands on crush, which is what an unarmed punch is. */
static int
derive_damage_type(const int* bonus)
{
    int best = 2; /* crush */
    int best_value = bonus[2];

    if( bonus[0] > best_value )
    {
        best = 0;
        best_value = bonus[0];
    }
    if( bonus[1] > best_value )
        best = 1;
    return best;
}

static void
record(
    int obj_id,
    const struct RSCache_Dat2ConfigObj* obj)
{
    struct Mock230ObjInfo* entry;

    if( obj_id < 0 || obj_id >= g_obj_count )
        return;
    entry = &g_objs[obj_id];
    entry->name = (obj->name && strcmp(obj->name, "null") != 0) ? strdup(obj->name) : NULL;
    entry->wearpos = obj->wearpos_1;
    entry->wearpos_2 = obj->wearpos_2;
    entry->wearpos_3 = obj->wearpos_3;
    entry->stackable = obj->stacking_behaviour == 1;
    for( int i = 0; i < 5; i++ )
        entry->if_ops[i] = obj->if_actions[i] ? strdup(obj->if_actions[i]) : NULL;

    entry->attackrate = 4;
    read_combat_params(&obj->params, entry->bonus, &entry->attackrate, &entry->has_params);
    entry->damagetype = derive_damage_type(entry->bonus);
}

int
mock230_objinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int loaded = 0;

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = 230;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Also try one directory up, so running the binary from src/ (where
         * make leaves it) finds the repo-root cache. Without the cache every
         * item reports wearpos -1 and nothing can be equipped, which reads as
         * a logic bug rather than a missing file. */
        char parent[512];
        snprintf(parent, sizeof(parent), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(parent);
        if( disk )
            cache_dir = parent;
    }
    if( !disk )
    {
        fprintf(
            stderr,
            "mock230: no cache at %s — equipment slots unavailable (items stay unwearable)\n",
            cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) ||
        archive->file_count <= 0 )
    {
        fprintf(stderr, "mock230: obj config archive missing from %s\n", cache_dir);
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_OBJ, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    /* file_ids are sparse, so size the table from the largest one rather than
     * from the file count. */
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        if( file_id + 1 > g_obj_count )
            g_obj_count = file_id + 1;
    }
    g_objs = calloc((size_t)(g_obj_count > 0 ? g_obj_count : 1), sizeof(*g_objs));
    if( !g_objs )
    {
        g_obj_count = 0;
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    for( int i = 0; i < g_obj_count; i++ )
    {
        g_objs[i].wearpos = -1;
        g_objs[i].wearpos_2 = -1;
        g_objs[i].wearpos_3 = -1;
    }

    for( int i = 0; i < files->file_count; i++ )
    {
        struct RSCache_Dat2ConfigObj* obj;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        if( files->file_sizes[i] <= 0 )
            continue;
        obj = RSCache_Dat2ConfigObjNewDecodeProfile(
            &profile, files->files[i], files->file_sizes[i]);
        if( !obj )
            continue;
        record(file_id, obj);
        RSCache_Dat2ConfigObjFree(obj);
        loaded++;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    fprintf(stderr, "mock230: obj metadata loaded (%d records from %s)\n", loaded, cache_dir);
    return loaded;
}

void
mock230_objinfo_free(void)
{
    for( int i = 0; i < g_obj_count; i++ )
    {
        free((void*)g_objs[i].name);
        for( int op = 0; op < 5; op++ )
            free((void*)g_objs[i].if_ops[op]);
    }
    free(g_objs);
    g_objs = NULL;
    g_obj_count = 0;
}

const struct Mock230ObjInfo*
mock230_objinfo(int obj_id)
{
    if( !g_objs || obj_id < 0 || obj_id >= g_obj_count || !g_objs[obj_id].name )
        return &k_unknown;
    return &g_objs[obj_id];
}
