/*
 * The LostCity content tree, read at boot.
 *
 * Three grammars, all of them LostCity's:
 *
 *   .pack   `id=name`, one line each, one file per namespace
 *   configs `[symbol]` sections of `key=value`, with `param=<name>,<value>`
 *   .jm2    `==== SECTION ====` banners over `level x z: fields...` lines
 *
 * Nothing here is clever. The value of matching the reference's syntax exactly
 * is that a LostCity config can be pasted in and a config written here can be
 * pasted back, so the two content trees stay one skill rather than two.
 *
 * Load order matters and is documented on mock230_content_load: bonuses are
 * seeded from the cache params that mock230_objinfo / mock230_npcinfo decoded,
 * and a config block overlays that.
 */

#include "mock230_content.h"

#include "mock230.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static int g_errors;

/*
 * Every rejection prints and counts. A content tree that half-loads is the
 * worst outcome available: the server runs, the fight is subtly wrong, and
 * nothing in the log says which line stopped meaning anything. `mock230_pack`
 * turns a non-zero count into a non-zero exit status.
 */
#define CONTENT_ERROR(...)                                                                         \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, "mock230: content: " __VA_ARGS__);                                         \
        g_errors++;                                                                                \
    } while( 0 )

int
mock230_content_error_count(void)
{
    return g_errors;
}

/* ------------------------------------------------------------------ */
/* Text helpers                                                        */
/* ------------------------------------------------------------------ */

/** Strip a `//` comment and surrounding whitespace, in place. */
static char*
clean_line(char* line)
{
    char* comment = strstr(line, "//");
    size_t length;

    if( comment )
        *comment = '\0';
    while( *line == ' ' || *line == '\t' )
        line++;
    length = strlen(line);
    while( length && (unsigned char)line[length - 1] <= ' ' )
        line[--length] = '\0';
    return line;
}

/** Split `key=value` in place. Returns the value, or NULL when there is no `=`. */
static char*
split_key_value(char* line)
{
    char* equals = strchr(line, '=');

    if( !equals )
        return NULL;
    *equals = '\0';
    /* Trim the key's trailing space so `hitpoints = 5` reads the same as
     * `hitpoints=5`. LostCity's own files never put a space there, but a config
     * that silently ignores the key it cannot parse is a bad neighbour. */
    for( size_t i = strlen(line); i > 0 && (unsigned char)line[i - 1] <= ' '; i-- )
        line[i - 1] = '\0';
    equals++;
    while( *equals == ' ' || *equals == '\t' )
        equals++;
    return equals;
}

/** `[name]` section header; returns the name in place, or NULL. */
static char*
section_header(char* line)
{
    size_t length = strlen(line);

    if( length < 2 || line[0] != '[' || line[length - 1] != ']' )
        return NULL;
    line[length - 1] = '\0';
    return line + 1;
}

/* ------------------------------------------------------------------ */
/* Packs                                                               */
/* ------------------------------------------------------------------ */

struct PackEntry
{
    char* name;
    int id;
};

struct Pack
{
    struct PackEntry* entries;
    int count;
    int capacity;
};

static struct Pack g_packs[MOCK230_PACK_COUNT];

static void
pack_add(
    struct Pack* pack,
    const char* name,
    int id)
{
    if( pack->count == pack->capacity )
    {
        int capacity = pack->capacity ? pack->capacity * 2 : 64;
        struct PackEntry* grown = realloc(pack->entries, (size_t)capacity * sizeof(*grown));

        if( !grown )
            return;
        pack->entries = grown;
        pack->capacity = capacity;
    }
    pack->entries[pack->count].name = strdup(name);
    pack->entries[pack->count].id = id;
    pack->count++;
}

static int
pack_load(
    struct Pack* pack,
    const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    int loaded = 0;

    if( !file )
        return 0;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = clean_line(raw);
        char* name;

        if( !*line )
            continue;
        name = split_key_value(line);
        if( !name || !*name )
            continue;
        pack_add(pack, name, atoi(line));
        loaded++;
    }
    fclose(file);
    return loaded;
}

int
mock230_content_symbol(
    enum Mock230PackKind kind,
    const char* name)
{
    const struct Pack* pack;

    if( !name || !*name )
        return -1;
    /* LostCity spells "no value" as the literal `null`, in configs and as a
     * param default. Resolving it to -1 without complaint is what lets
     * `param=death_drop,null` mean "drops nothing". */
    if( strcmp(name, "null") == 0 )
        return -1;
    if( kind < 0 || kind >= MOCK230_PACK_COUNT )
        return -1;

    pack = &g_packs[kind];
    for( int i = 0; i < pack->count; i++ )
    {
        if( strcmp(pack->entries[i].name, name) == 0 )
            return pack->entries[i].id;
    }
    return -1;
}

const char*
mock230_content_symbol_name(
    enum Mock230PackKind kind,
    int symbol_id)
{
    const struct Pack* pack;

    if( kind < 0 || kind >= MOCK230_PACK_COUNT )
        return NULL;
    pack = &g_packs[kind];
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].id == symbol_id )
            return pack->entries[i].name;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Definition tables                                                   */
/* ------------------------------------------------------------------ */

/*
 * Sparse ids over a dense array would be 15,000 entries of mostly nothing, so
 * definitions live in a growable list and lookup is a scan. The list is the
 * size of the content tree — a few dozen — not the size of the cache.
 */

static struct Mock230NpcDef* g_npc_defs;
static int g_npc_def_count;
static int g_npc_def_capacity;

static struct Mock230LocDef* g_loc_defs;
static int g_loc_def_count;
static int g_loc_def_capacity;

static struct Mock230MapNpcSpawn* g_npc_spawns;
static int g_npc_spawn_count;
static int g_npc_spawn_capacity;

static struct Mock230MapObjSpawn* g_obj_spawns;
static int g_obj_spawn_count;
static int g_obj_spawn_capacity;

/** Engine defaults. OpenRune's NpcCombatDef.DEFAULT, with LostCity's animation
 *  names — the two agree on all of it. */
static struct Mock230NpcDef g_npc_default;

static void*
grow(
    void* array,
    int* capacity,
    int count,
    size_t element)
{
    void* grown;

    if( count < *capacity )
        return array;
    *capacity = *capacity ? *capacity * 2 : 32;
    grown = realloc(array, (size_t)*capacity * element);
    return grown ? grown : array;
}

const struct Mock230NpcDef*
mock230_content_npc(int npc_id)
{
    for( int i = 0; i < g_npc_def_count; i++ )
    {
        if( g_npc_defs[i].npc_id == npc_id )
            return &g_npc_defs[i];
    }
    return NULL;
}

const struct Mock230NpcDef*
mock230_content_npc_default(void)
{
    return &g_npc_default;
}

const struct Mock230LocDef*
mock230_content_loc(int loc_id)
{
    for( int i = 0; i < g_loc_def_count; i++ )
    {
        if( g_loc_defs[i].loc_id == loc_id )
            return &g_loc_defs[i];
    }
    return NULL;
}

const struct Mock230MapNpcSpawn*
mock230_content_npc_spawns(int* count)
{
    *count = g_npc_spawn_count;
    return g_npc_spawns;
}

const struct Mock230MapObjSpawn*
mock230_content_obj_spawns(int* count)
{
    *count = g_obj_spawn_count;
    return g_obj_spawns;
}

/* ------------------------------------------------------------------ */
/* .npc configs                                                        */
/* ------------------------------------------------------------------ */

/* LostCity's combat.param / npc_combat.param names. The index into this table
 * is the cache param id for the twelve bonuses, which is why the order is not
 * negotiable — see Mock230CombatParam. */
static const char* const k_bonus_param_names[MOCK230_PARAM_BONUS_COUNT] = {
    "stabattack",   "slashattack",   "crushattack",    "magicattack",
    "rangeattack",  "stabdefence",   "slashdefence",   "crushdefence",
    "magicdefence", "rangedefence",  "strengthbonus",  "prayerbonus",
};

static void
npc_def_seed_from_cache(
    struct Mock230NpcDef* def,
    int npc_id)
{
    const struct Mock230NpcInfo* info = mock230_npcinfo(npc_id);

    *def = g_npc_default;
    def->npc_id = npc_id;
    if( info->has_params )
    {
        for( int i = 0; i < MOCK230_PARAM_BONUS_COUNT; i++ )
            def->bonus[i] = info->bonus[i];
        def->attackrate = info->attackrate;
    }
}

/** `param=<name>,<value>`. Returns 0 for a name nothing here knows, which is an
 *  error rather than a shrug: a typo'd param is a stat that silently stays at
 *  its default. */
static int
apply_param(
    struct Mock230NpcDef* def,
    char* text,
    const char* where)
{
    char* comma = strchr(text, ',');
    const char* value;

    if( !comma )
    {
        CONTENT_ERROR("%s: param needs `name,value`, got `%s`\n", where, text);
        return 0;
    }
    *comma = '\0';
    value = comma + 1;

    for( int i = 0; i < MOCK230_PARAM_BONUS_COUNT; i++ )
    {
        if( strcmp(text, k_bonus_param_names[i]) == 0 )
        {
            def->bonus[i] = atoi(value);
            return 1;
        }
    }
    if( strcmp(text, "attackrate") == 0 )
        def->attackrate = atoi(value);
    else if( strcmp(text, "attackrange") == 0 )
        def->attackrange = atoi(value);
    else if( strcmp(text, "damagetype") == 0 )
        def->damagetype = atoi(value);
    else if( strcmp(text, "huntrange") == 0 )
        def->huntrange = atoi(value);
    else if( strcmp(text, "attack_anim") == 0 || strcmp(text, "slashattack_anim") == 0 )
        def->attack_anim = mock230_content_symbol(MOCK230_PACK_SEQ, value);
    else if( strcmp(text, "defend_anim") == 0 )
        def->defend_anim = mock230_content_symbol(MOCK230_PACK_SEQ, value);
    else if( strcmp(text, "death_anim") == 0 )
        def->death_anim = mock230_content_symbol(MOCK230_PACK_SEQ, value);
    else if( strcmp(text, "death_drop") == 0 )
        def->death_drop = mock230_content_symbol(MOCK230_PACK_OBJ, value);
    else
    {
        CONTENT_ERROR("%s: unknown param `%s`\n", where, text);
        return 0;
    }
    return 1;
}

static void
npc_config_key(
    struct Mock230NpcDef* def,
    const char* key,
    char* value,
    const char* where)
{
    if( strcmp(key, "hitpoints") == 0 )
    {
        def->hitpoints = atoi(value);
        def->authored_combat = 1;
    }
    else if( strcmp(key, "attack") == 0 )
        def->attack = atoi(value);
    else if( strcmp(key, "strength") == 0 )
        def->strength = atoi(value);
    else if( strcmp(key, "defence") == 0 )
        def->defence = atoi(value);
    else if( strcmp(key, "magic") == 0 )
        def->magic = atoi(value);
    else if( strcmp(key, "ranged") == 0 )
        def->ranged = atoi(value);
    else if( strcmp(key, "respawnrate") == 0 )
        def->respawnrate = atoi(value);
    else if( strcmp(key, "wanderrange") == 0 )
        def->wanderrange = atoi(value);
    else if( strcmp(key, "moverestrict") == 0 )
        def->nomove = strcmp(value, "nomove") == 0;
    else if( strcmp(key, "huntmode") == 0 )
        def->huntmode = strcmp(value, "aggressive") == 0 ? MOCK230_HUNT_AGGRESSIVE
                                                         : MOCK230_HUNT_NONE;
    else if( strcmp(key, "param") == 0 )
        (void)apply_param(def, value, where);
    else
    {
        /* Keys LostCity authors because it *builds* the npc record. Ours comes
         * from the cache, so these are inert here — accepted so a config can be
         * shared with a LostCity tree unchanged, but never silently: a `name=`
         * that does nothing is worth knowing about. */
        static const char* const k_from_cache[] = { "name",   "desc",     "vislevel",
                                                    "op1",    "op2",      "op3",
                                                    "op4",    "op5",      "walkanim",
                                                    "readyanim", "category", "size",
                                                    NULL };
        for( int i = 0; k_from_cache[i]; i++ )
        {
            if( strcmp(key, k_from_cache[i]) == 0 )
                return;
        }
        CONTENT_ERROR("%s: unknown key `%s`\n", where, key);
    }
}

static void
load_npc_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230NpcDef* def = NULL;
    int line_number = 0;
    char where[600];

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = section_header(line);
        if( header )
        {
            int npc_id = mock230_content_symbol(MOCK230_PACK_NPC, header);

            def = NULL;
            if( npc_id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in pack/npc.pack\n", path, line_number,
                              header);
                continue;
            }
            g_npc_defs = grow(g_npc_defs, &g_npc_def_capacity, g_npc_def_count,
                              sizeof(*g_npc_defs));
            def = &g_npc_defs[g_npc_def_count++];
            npc_def_seed_from_cache(def, npc_id);
            def->symbol = mock230_content_symbol_name(MOCK230_PACK_NPC, npc_id);
            continue;
        }

        value = split_key_value(line);
        if( !value )
        {
            CONTENT_ERROR("%s:%d: expected `key=value`\n", path, line_number);
            continue;
        }
        if( !def )
        {
            CONTENT_ERROR("%s:%d: `%s` before any [section]\n", path, line_number, line);
            continue;
        }
        snprintf(where, sizeof(where), "%s:%d", path, line_number);
        npc_config_key(def, line, value, where);
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .loc configs                                                        */
/* ------------------------------------------------------------------ */

/*
 * Doors are the reason this exists. Two loc ids look identical to a cache
 * reader — one closed, one open — and nothing in the cache says which pairs
 * with which. LostCity records the pairing as `category=door_closed` plus
 * `param=next_loc_stage,<symbol>`, and so does this.
 *
 * `next_loc_stage` is resolved lazily, after every .loc file has been read: a
 * door names its open half and the open half names it back, so whichever is
 * read first refers forward.
 */

struct PendingStage
{
    int def_index;
    char* symbol;
};

static struct PendingStage* g_pending;
static int g_pending_count;
static int g_pending_capacity;

static void
load_loc_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230LocDef* def = NULL;
    int def_index = -1;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = section_header(line);
        if( header )
        {
            int loc_id = mock230_content_symbol(MOCK230_PACK_LOC, header);

            def = NULL;
            if( loc_id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in pack/loc.pack\n", path, line_number,
                              header);
                continue;
            }
            g_loc_defs = grow(g_loc_defs, &g_loc_def_capacity, g_loc_def_count,
                              sizeof(*g_loc_defs));
            def_index = g_loc_def_count++;
            def = &g_loc_defs[def_index];
            memset(def, 0, sizeof(*def));
            def->loc_id = loc_id;
            def->symbol = mock230_content_symbol_name(MOCK230_PACK_LOC, loc_id);
            def->next_loc_stage = -1;
            continue;
        }

        value = split_key_value(line);
        if( !value || !def )
        {
            CONTENT_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                          line_number);
            continue;
        }

        if( strcmp(line, "category") == 0 )
        {
            if( strcmp(value, "door_closed") == 0 )
                def->category = MOCK230_LOC_CATEGORY_DOOR_CLOSED;
            else if( strcmp(value, "door_opened") == 0 )
                def->category = MOCK230_LOC_CATEGORY_DOOR_OPENED;
            else
                CONTENT_ERROR("%s:%d: unknown category `%s`\n", path, line_number, value);
        }
        else if( strcmp(line, "param") == 0 )
        {
            char* comma = strchr(value, ',');

            if( !comma )
            {
                CONTENT_ERROR("%s:%d: param needs `name,value`\n", path, line_number);
                continue;
            }
            *comma = '\0';
            if( strcmp(value, "next_loc_stage") != 0 )
            {
                CONTENT_ERROR("%s:%d: unknown loc param `%s`\n", path, line_number, value);
                continue;
            }
            g_pending = grow(g_pending, &g_pending_capacity, g_pending_count,
                             sizeof(*g_pending));
            g_pending[g_pending_count].def_index = def_index;
            g_pending[g_pending_count].symbol = strdup(comma + 1);
            g_pending_count++;
        }
        else if( strcmp(line, "name") != 0 && strcmp(line, "desc") != 0 )
        {
            CONTENT_ERROR("%s:%d: unknown key `%s`\n", path, line_number, line);
        }
    }
    fclose(file);
}

static void
resolve_loc_stages(void)
{
    for( int i = 0; i < g_pending_count; i++ )
    {
        int index = g_pending[i].def_index;
        int target = mock230_content_symbol(MOCK230_PACK_LOC, g_pending[i].symbol);

        if( target < 0 )
            CONTENT_ERROR("next_loc_stage `%s` is not in pack/loc.pack\n",
                          g_pending[i].symbol);
        else if( index >= 0 && index < g_loc_def_count )
            g_loc_defs[index].next_loc_stage = target;
        free(g_pending[i].symbol);
    }
    free(g_pending);
    g_pending = NULL;
    g_pending_count = 0;
    g_pending_capacity = 0;
}

/* ------------------------------------------------------------------ */
/* .jm2 maps                                                           */
/* ------------------------------------------------------------------ */

/*
 * LostCity's map format, of which only two sections are read here.
 *
 *   ==== NPC ====
 *   <level> <x> <z>: <npc id>
 *   ==== OBJ ====
 *   <level> <x> <z>: <obj id> <count>
 *
 * x and z are local to the 64x64 map square named by the filename, so a spawn
 * is stable no matter where the mock's scene happens to be centred.
 *
 * MAP and LOC are deliberately not read: terrain and scenery come from
 * cache.osrs230, which is the same data the client draws, and a second copy
 * that could disagree with it is a bug waiting to be written.
 */

enum Jm2Section
{
    JM2_NONE = 0,
    JM2_MAP,
    JM2_LOC,
    JM2_NPC,
    JM2_OBJ,
};

static void
load_jm2(
    const char* path,
    int map_x,
    int map_z)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    enum Jm2Section section = JM2_NONE;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = clean_line(raw);
        int level, local_x, local_z, id, count;

        line_number++;
        if( !*line )
            continue;

        if( strncmp(line, "====", 4) == 0 )
        {
            if( strstr(line, "NPC") )
                section = JM2_NPC;
            else if( strstr(line, "OBJ") )
                section = JM2_OBJ;
            else if( strstr(line, "LOC") )
                section = JM2_LOC;
            else
                section = JM2_MAP;
            continue;
        }
        if( section != JM2_NPC && section != JM2_OBJ )
            continue;

        count = 1;
        if( sscanf(line, "%d %d %d: %d %d", &level, &local_x, &local_z, &id, &count) < 4 )
        {
            CONTENT_ERROR("%s:%d: expected `level x z: id [count]`\n", path, line_number);
            continue;
        }
        if( local_x < 0 || local_x > 63 || local_z < 0 || local_z > 63 || level < 0 ||
            level > 3 )
        {
            CONTENT_ERROR("%s:%d: %d %d %d is outside the map square\n", path, line_number,
                          level, local_x, local_z);
            continue;
        }

        if( section == JM2_NPC )
        {
            g_npc_spawns = grow(g_npc_spawns, &g_npc_spawn_capacity, g_npc_spawn_count,
                                sizeof(*g_npc_spawns));
            g_npc_spawns[g_npc_spawn_count].npc_id = id;
            g_npc_spawns[g_npc_spawn_count].x = map_x * 64 + local_x;
            g_npc_spawns[g_npc_spawn_count].z = map_z * 64 + local_z;
            g_npc_spawns[g_npc_spawn_count].level = level;
            g_npc_spawn_count++;
        }
        else
        {
            g_obj_spawns = grow(g_obj_spawns, &g_obj_spawn_capacity, g_obj_spawn_count,
                                sizeof(*g_obj_spawns));
            g_obj_spawns[g_obj_spawn_count].obj_id = id;
            g_obj_spawns[g_obj_spawn_count].count = count;
            g_obj_spawns[g_obj_spawn_count].x = map_x * 64 + local_x;
            g_obj_spawns[g_obj_spawn_count].z = map_z * 64 + local_z;
            g_obj_spawns[g_obj_spawn_count].level = level;
            g_obj_spawn_count++;
        }
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* Walking the tree                                                    */
/* ------------------------------------------------------------------ */

static int
has_suffix(
    const char* name,
    const char* suffix)
{
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);

    return name_length >= suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
}

/** Recursively load every `.npc` then every `.loc`; the two passes keep a door
 *  config free to name a loc declared in another file. */
static void
walk_configs(
    const char* dir,
    const char* suffix,
    void (*load)(const char*))
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    char path[1024];

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( entry->d_type == DT_DIR )
            walk_configs(path, suffix, load);
        else if( has_suffix(entry->d_name, suffix) )
            load(path);
    }
    closedir(handle);
}

static void
load_maps(const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    char path[1024];

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        int map_x, map_z;

        if( !has_suffix(entry->d_name, ".jm2") )
            continue;
        if( sscanf(entry->d_name, "m%d_%d.jm2", &map_x, &map_z) != 2 )
        {
            CONTENT_ERROR("maps/%s: expected m<x>_<z>.jm2\n", entry->d_name);
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        load_jm2(path, map_x, map_z);
    }
    closedir(handle);
}

/* ------------------------------------------------------------------ */
/* Load / free                                                         */
/* ------------------------------------------------------------------ */

static void
init_defaults(void)
{
    memset(&g_npc_default, 0, sizeof(g_npc_default));
    g_npc_default.npc_id = -1;
    g_npc_default.hitpoints = 10;
    g_npc_default.attack = 1;
    g_npc_default.strength = 1;
    g_npc_default.defence = 1;
    g_npc_default.respawnrate = 25;
    g_npc_default.attackrate = 4;
    g_npc_default.attackrange = 1;
    g_npc_default.damagetype = MOCK230_DAMAGE_CRUSH;
    g_npc_default.attack_anim = mock230_content_symbol(MOCK230_PACK_SEQ, "human_unarmedpunch");
    g_npc_default.defend_anim = mock230_content_symbol(MOCK230_PACK_SEQ, "human_unarmedblock");
    g_npc_default.death_anim = mock230_content_symbol(MOCK230_PACK_SEQ, "human_death");
    g_npc_default.death_drop = mock230_content_symbol(MOCK230_PACK_OBJ, "bones");
}

int
mock230_content_load(const char* dir)
{
    static const struct
    {
        const char* file;
        enum Mock230PackKind kind;
    } k_packs[] = {
        { "npc.pack",       MOCK230_PACK_NPC       },
        { "obj.pack",       MOCK230_PACK_OBJ       },
        { "loc.pack",       MOCK230_PACK_LOC       },
        /* Two files, one namespace. door.pack is generated by a different tool
         * from a different source, and merging them would mean one generator
         * overwriting the other's work every time it ran. */
        { "door.pack",      MOCK230_PACK_LOC       },
        { "seq.pack",       MOCK230_PACK_SEQ       },
        { "spotanim.pack",  MOCK230_PACK_SPOTANIM  },
        { "inv.pack",       MOCK230_PACK_INV       },
        { "varp.pack",      MOCK230_PACK_VARP      },
        { "interface.pack", MOCK230_PACK_INTERFACE },
        { "component.pack", MOCK230_PACK_COMPONENT },
        { "stat.pack",      MOCK230_PACK_STAT      },
        { "param.pack",     MOCK230_PACK_PARAM     },
    };
    char path[1024];
    DIR* probe;
    int symbols = 0;

    mock230_content_free();

    probe = opendir(dir);
    if( !probe )
    {
        /* Same fallback as the cache loaders: `make` leaves the binary in src/
         * but the tree is addressed from the repo root. Not finding it is not
         * an error — the engine defaults keep the mock running. */
        static char parent[1024];

        snprintf(parent, sizeof(parent), "../%s", dir);
        probe = opendir(parent);
        if( !probe )
        {
            fprintf(stderr, "mock230: no content tree at %s — engine defaults only\n", dir);
            init_defaults();
            return 0;
        }
        dir = parent;
    }
    closedir(probe);

    for( size_t i = 0; i < sizeof(k_packs) / sizeof(k_packs[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/pack/%s", dir, k_packs[i].file);
        symbols += pack_load(&g_packs[k_packs[i].kind], path);
    }

    /* After the packs (a default names its animations by symbol) and before the
     * configs (each block starts from a copy of it). */
    init_defaults();

    snprintf(path, sizeof(path), "%s/scripts", dir);
    walk_configs(path, ".npc", load_npc_config);
    walk_configs(path, ".loc", load_loc_config);
    resolve_loc_stages();

    snprintf(path, sizeof(path), "%s/maps", dir);
    load_maps(path);

    fprintf(stderr,
            "mock230: content loaded (%d symbols, %d npc defs, %d loc defs, "
            "%d npc spawns, %d obj spawns%s)\n",
            symbols, g_npc_def_count, g_loc_def_count, g_npc_spawn_count, g_obj_spawn_count,
            g_errors ? ", WITH ERRORS" : "");
    return g_npc_def_count;
}

void
mock230_content_free(void)
{
    for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
    {
        for( int i = 0; i < g_packs[kind].count; i++ )
            free(g_packs[kind].entries[i].name);
        free(g_packs[kind].entries);
        g_packs[kind].entries = NULL;
        g_packs[kind].count = 0;
        g_packs[kind].capacity = 0;
    }
    free(g_npc_defs);
    g_npc_defs = NULL;
    g_npc_def_count = g_npc_def_capacity = 0;
    free(g_loc_defs);
    g_loc_defs = NULL;
    g_loc_def_count = g_loc_def_capacity = 0;
    free(g_npc_spawns);
    g_npc_spawns = NULL;
    g_npc_spawn_count = g_npc_spawn_capacity = 0;
    free(g_obj_spawns);
    g_obj_spawns = NULL;
    g_obj_spawn_count = g_obj_spawn_capacity = 0;
    g_errors = 0;
}
