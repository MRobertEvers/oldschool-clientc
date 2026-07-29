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
    .noted_id = -1,
    .noted_template = -1,
    .cert_id = -1,
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

/* ------------------------------------------------------------------ */
/* Equipment requirements                                              */
/* ------------------------------------------------------------------ */

static struct Mock230ObjRequire* g_requires;
static int g_require_count;
static int g_require_capacity;
static int g_require_from_cache;

/** Which skills can gate *wearing* something.
 *
 * The cache's 434/436 pair states a requirement to *use* a record, and for a
 * wearable obj "use" sometimes means "make": a fire battlestaff reads Crafting
 * 62, which is what it takes to build one rather than to hold it. Nineteen
 * records in cache.osrs239 are like that, so being wearable is not the test —
 * the skill is, because no combat skill is ever a creation requirement. A
 * non-combat requirement that really does gate wearing (a skill cape needs 99,
 * a larupia hat needs 28 Hunter) comes in through the `.obj` overlay instead,
 * where it is stated deliberately rather than inferred. */
static int
gates_wearing(int stat)
{
    switch( stat )
    {
    case MOCK230_STAT_ATTACK:
    case MOCK230_STAT_DEFENCE:
    case MOCK230_STAT_STRENGTH:
    case MOCK230_STAT_HITPOINTS:
    case MOCK230_STAT_RANGED:
    case MOCK230_STAT_PRAYER:
    case MOCK230_STAT_MAGIC:
        return 1;
    default:
        return 0;
    }
}

/** The table is kept sorted by obj id so a lookup is a binary search; ids arrive
 *  in ascending order from the decode pass, and the overlay's inserts are rare
 *  enough that shifting is cheaper than a second index. */
static int
require_index(int obj_id)
{
    int low = 0;
    int high = g_require_count - 1;

    while( low <= high )
    {
        int mid = low + ((high - low) / 2);

        if( g_requires[mid].obj_id < obj_id )
            low = mid + 1;
        else if( g_requires[mid].obj_id > obj_id )
            high = mid - 1;
        else
            return mid;
    }
    return -(low + 1); /* -(insertion point) - 1, like bsearch's cousins */
}

static struct Mock230ObjRequire*
require_slot(int obj_id)
{
    int index = require_index(obj_id);

    if( index >= 0 )
        return &g_requires[index];

    index = -(index + 1);
    if( g_require_count == g_require_capacity )
    {
        int capacity = g_require_capacity ? g_require_capacity * 2 : 256;
        struct Mock230ObjRequire* grown =
            (struct Mock230ObjRequire*)realloc(g_requires, (size_t)capacity * sizeof(*grown));

        if( !grown )
            return NULL;
        g_requires = grown;
        g_require_capacity = capacity;
    }
    memmove(&g_requires[index + 1], &g_requires[index],
            (size_t)(g_require_count - index) * sizeof(*g_requires));
    memset(&g_requires[index], 0, sizeof(g_requires[index]));
    g_requires[index].obj_id = obj_id;
    g_require_count++;
    return &g_requires[index];
}

const struct Mock230ObjRequire*
mock230_obj_require(int obj_id)
{
    int index;

    if( obj_id < 0 || !g_requires )
        return NULL;
    index = require_index(obj_id);
    return index >= 0 ? &g_requires[index] : NULL;
}

int
mock230_obj_require_set(
    int obj_id,
    const int* stats,
    const int* levels,
    int count)
{
    struct Mock230ObjRequire* entry;

    if( obj_id < 0 || count < 0 || count > MOCK230_OBJ_REQUIRE_MAX )
        return 0;
    entry = require_slot(obj_id);
    if( !entry )
        return 0;
    entry->count = 0;
    for( int i = 0; i < count; i++ )
    {
        entry->req[entry->count].stat = stats[i];
        entry->req[entry->count].level = levels[i];
        entry->count++;
    }
    return 1;
}

void
mock230_obj_require_counts(
    int* total,
    int* from_cache)
{
    if( total )
        *total = g_require_count;
    if( from_cache )
        *from_cache = g_require_from_cache;
}

/*
 * Read the cache's own requirement pairs off one record.
 *
 * 434/436 and 435/437 are (skill, level), and that is all the room a cache
 * record has — which is why Void knight gear, with seven, cannot be stated in
 * one at all. A pair with only half of it present is ignored rather than
 * half-applied.
 */
static void
read_requirements(
    int obj_id,
    const struct RSCache_Params* params,
    int wearable)
{
    int skill[2] = { -1, -1 };
    int level[2] = { -1, -1 };
    int stats[2];
    int levels[2];
    int count = 0;

    if( !wearable )
        return;
    for( int i = 0; i < params->count; i++ )
    {
        int key = params->keys[i];
        int value;

        if( params->kinds[i] != 0 || !params->values[i] )
            continue;
        value = *(const int*)params->values[i];
        switch( key )
        {
        case 434: skill[0] = value; break;
        case 436: level[0] = value; break;
        case 435: skill[1] = value; break;
        case 437: level[1] = value; break;
        default: break;
        }
    }
    for( int i = 0; i < 2; i++ )
    {
        if( skill[i] < 0 || level[i] <= 0 || !gates_wearing(skill[i]) )
            continue;
        stats[count] = skill[i];
        levels[count] = level[i];
        count++;
    }
    if( count && mock230_obj_require_set(obj_id, stats, levels, count) )
        g_require_from_cache++;
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
    entry->known = 1;
    /* A note is stackable whatever its own `stacking_behaviour` says — every
     * one of them records 0, and the reference client tests
     * `stackable == 1 || notedTemplate != -1`. Without the second half a stack
     * of 20 noted swordfish occupies 20 backpack slots. */
    entry->stackable = obj->stacking_behaviour == 1 || obj->noted_template >= 0;
    entry->weight = obj->weight;
    /*
     * Notes, both directions, out of the same two fields.
     *
     * Opcode 97 (`noted_id`) is "the record on the other side of the note
     * link", and opcode 98 (`noted_template`) is what says which side this
     * record is on: a plain item leaves it -1 and points *forward* at its note,
     * a note sets it to the shared template (799) and points *back* at the
     * item. So both directions read straight out of the record and neither
     * needs an index — which is worth stating, because the pair does look like
     * one field and a flag until you print a few.
     *
     *   1511 Logs          noted 1512 / template   -1
     *   1512 "null"        noted 1511 / template  799
     */
    entry->noted_id = obj->noted_id;
    entry->noted_template = obj->noted_template;
    entry->cert_id = obj->noted_template < 0 ? obj->noted_id : -1;
    for( int i = 0; i < 5; i++ )
        entry->if_ops[i] = obj->if_actions[i] ? strdup(obj->if_actions[i]) : NULL;

    /* Opcode 94. Two things read it: the combat interface's weapon_category
     * varbit, which picks one of the ten attack-style layouts, and the
     * `[opheld<n>,_<category>]` trigger, which is how content addresses "every
     * bone in the cache" without listing all 38. Zero is the decoder's default
     * *and* a legal id, so callers that need to tell "unset" from "category 0"
     * treat 0 as unset — the same thing the text config does by omitting the
     * key. */
    entry->category = obj->category;

    entry->attackrate = 4;
    read_combat_params(&obj->params, entry->bonus, &entry->attackrate, &entry->has_params);
    read_requirements(obj_id, &obj->params, entry->wearpos >= 0);
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
    profile.revision = MOCK230_CACHE_REVISION;

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
        g_objs[i].noted_id = -1;
        g_objs[i].noted_template = -1;
        g_objs[i].cert_id = -1;
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

    /* A note takes its name from the item it stands for — its own record says
     * `null`. Borrowing it here rather than at every call site means a message
     * about a note reads as "Swordfish" instead of "item". */
    for( int id = 0; id < g_obj_count; id++ )
    {
        int real = g_objs[id].noted_id;

        if( g_objs[id].name || g_objs[id].noted_template < 0 )
            continue;
        if( real < 0 || real >= g_obj_count || !g_objs[real].name )
            continue;
        g_objs[id].name = strdup(g_objs[real].name);
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

    free(g_requires);
    g_requires = NULL;
    g_require_count = 0;
    g_require_capacity = 0;
    g_require_from_cache = 0;
}

const struct Mock230ObjInfo*
mock230_objinfo(int obj_id)
{
    /* Keyed on `known`, not on the name: a record with no name is still a
     * record, and every one of the game's ~8,000 notes is one. */
    if( !g_objs || obj_id < 0 || obj_id >= g_obj_count || !g_objs[obj_id].known )
        return &k_unknown;
    return &g_objs[obj_id];
}
