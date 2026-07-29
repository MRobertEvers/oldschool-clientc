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

/*
 * A namespace is two layers, not two directories. See
 * docs/CONTENT_ARCHITECTURE.md §4.1.
 *
 *   layer 0   pack/<ns>.pack    machine-owned. Regenerated wholesale by
 *                               cachepack from the cache's gameval table.
 *                               Never hand-edited; comments here get eaten.
 *   layer 1   names/<ns>.pack   human-owned. Never machine-written. Holds the
 *                               three things layer 0 structurally cannot say:
 *                                 alias     a second name for a named id
 *                                 override  replace layer 0's name for an id
 *                                 fill      name an id layer 0 leaves unnamed
 *
 * `pack/varp.pack` is a *function of the cache* — `115=bankcert` is the cache's
 * own gameval, not anybody's choice — and `LC_Pack` is `names[id]`, one name per
 * id. So `115=bank_withdrawnotes` cannot live there, and the old answer was to
 * hand-splice authored lines into a file `cachepack unpack` regenerates. This
 * makes layer 0 disposable: `rm -rf pack/ && cachepack unpack` becomes safe, and
 * every surviving name in `names/` is one a human wrote.
 */
enum
{
    PACK_LAYER_CACHE = 0,
    PACK_LAYER_AUTHORED = 1,
};

struct PackEntry
{
    char* name;
    int id;
    int layer;
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
    int id,
    int layer)
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
    pack->entries[pack->count].layer = layer;
    pack->count++;
}

static int
pack_load(
    struct Pack* pack,
    const char* path,
    int layer)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    int loaded = 0;

    /* A missing pack is not an error: layer 1 is optional for every namespace,
     * and layer 0 does not exist at all for the server-only ones (`stat` has no
     * gameval archive to be regenerated from). */
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
        pack_add(pack, name, atoi(line), layer);
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

    /*
     * Both layers resolve name -> id, authored first.
     *
     * Layer 0's names keep working on purpose: `bankcert` is what the cache
     * calls varp 115 and `bank_withdrawnotes` is what this world calls it, and
     * a config or script may reasonably say either. Searching authored first
     * only matters when the two disagree, and the loader has already refused to
     * start in that case (see validate_name_layers).
     */
    pack = &g_packs[kind];
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].layer == PACK_LAYER_AUTHORED &&
            strcmp(pack->entries[i].name, name) == 0 )
            return pack->entries[i].id;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].layer == PACK_LAYER_CACHE &&
            strcmp(pack->entries[i].name, name) == 0 )
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
    /* Layer 1 wins for id -> name: the canonical name of an id this world
     * repurposed is the one this world gave it, not the cache's. */
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].layer == PACK_LAYER_AUTHORED &&
            pack->entries[i].id == symbol_id )
            return pack->entries[i].name;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].layer == PACK_LAYER_CACHE && pack->entries[i].id == symbol_id )
            return pack->entries[i].name;
    }
    return NULL;
}

/* Forward: the diagnostic namespace name, defined just below. */
static const char*
pack_kind_name(enum Mock230PackKind kind);

/**
 * Refuse to start on a namespace whose two layers disagree.
 *
 * Two rules, both LostCity's (`packConfigs()`), and both describing hazards this
 * tree has already written down in prose rather than checked:
 *
 * 1. **An authored name may not shadow a *different* id's cache name.** If
 *    layer 0 says `115=bankcert` and layer 1 says `843=bankcert`, then
 *    `bankcert` in a script means one thing to a reader and another to the
 *    loader. Aliasing the *same* id is the entire point of layer 1 and is fine;
 *    aliasing a different one is a typo that would otherwise resolve silently.
 *
 * 2. **varp and varbit share one RuneScript name domain.** `%name` does not say
 *    which of the two it is, so a name meaning varp 115 and varbit 4 cannot
 *    coexist however separate their pack files look.
 *
 * Returns the number of collisions; each is reported through the content error
 * count, so `mock230_pack` fails on them too.
 */
static int
validate_name_layers(void)
{
    /* varp and varbit only. `varn`/`vars` would join this list when they exist;
     * the other namespaces each have their own domain. */
    static const enum Mock230PackKind k_shared_domain[] = {
        MOCK230_PACK_VARP,
        MOCK230_PACK_VARBIT,
    };
    int collisions = 0;

    for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
    {
        const struct Pack* pack = &g_packs[kind];

        for( int i = 0; i < pack->count; i++ )
        {
            if( pack->entries[i].layer != PACK_LAYER_AUTHORED )
                continue;
            for( int j = 0; j < pack->count; j++ )
            {
                if( pack->entries[j].layer != PACK_LAYER_CACHE )
                    continue;
                if( pack->entries[j].id == pack->entries[i].id )
                    continue; /* an alias for the same id — the point of layer 1 */
                if( strcmp(pack->entries[j].name, pack->entries[i].name) != 0 )
                    continue;
                CONTENT_ERROR(
                    "names/%s.pack: `%s` = %d shadows pack/%s.pack's `%s` = %d\n",
                    pack_kind_name((enum Mock230PackKind)kind), pack->entries[i].name,
                    pack->entries[i].id, pack_kind_name((enum Mock230PackKind)kind),
                    pack->entries[j].name, pack->entries[j].id);
                collisions++;
            }
        }
    }

    for( size_t a = 0; a < sizeof(k_shared_domain) / sizeof(k_shared_domain[0]); a++ )
    {
        for( size_t b = a + 1; b < sizeof(k_shared_domain) / sizeof(k_shared_domain[0]); b++ )
        {
            const struct Pack* left = &g_packs[k_shared_domain[a]];
            const struct Pack* right = &g_packs[k_shared_domain[b]];

            for( int i = 0; i < left->count; i++ )
            {
                for( int j = 0; j < right->count; j++ )
                {
                    if( strcmp(left->entries[i].name, right->entries[j].name) != 0 )
                        continue;
                    CONTENT_ERROR("`%s` is both %s %d and %s %d — one RuneScript name "
                                  "domain covers both\n",
                                  left->entries[i].name,
                                  pack_kind_name(k_shared_domain[a]), left->entries[i].id,
                                  pack_kind_name(k_shared_domain[b]), right->entries[j].id);
                    collisions++;
                }
            }
        }
    }

    return collisions;
}

/** The pack file a kind is spelled with, for diagnostics. */
static const char*
pack_kind_name(enum Mock230PackKind kind)
{
    static const char* k_names[MOCK230_PACK_COUNT] = {
        "npc",  "obj",  "loc",       "seq",   "spotanim", "inv",   "varp",
        "varbit", "interface", "component", "stat", "param", "hitsplat",
    };

    if( kind < 0 || kind >= MOCK230_PACK_COUNT )
        return "?";
    return k_names[kind];
}

int
mock230_content_resolve(
    const char* what,
    const struct Mock230SymbolRef* refs,
    int count)
{
    int failed = 0;

    for( int i = 0; i < count; i++ )
    {
        *refs[i].out = mock230_content_symbol(refs[i].kind, refs[i].name);
        if( *refs[i].out >= 0 )
            continue;
        CONTENT_ERROR("%s: no `%s` in %s.pack\n", what, refs[i].name,
                      pack_kind_name(refs[i].kind));
        failed++;
    }
    return failed;
}

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

struct Constant
{
    char* name; /* without the caret */
    char* text;
};

static struct Constant* g_constants;
static int g_constant_count;
static int g_constant_capacity;

const char*
mock230_content_constant(const char* name)
{
    if( !name )
        return NULL;
    if( *name == '^' )
        name++;
    for( int i = 0; i < g_constant_count; i++ )
    {
        if( strcmp(g_constants[i].name, name) == 0 )
            return g_constants[i].text;
    }
    return NULL;
}

int
mock230_content_constant_int(
    const char* name,
    int fallback)
{
    const char* text = mock230_content_constant(name);
    char* end;
    long value;

    if( !text )
    {
        CONTENT_ERROR("no `^%s` in any .constant\n", *name == '^' ? name + 1 : name);
        return fallback;
    }
    value = strtol(text, &end, 10);
    while( *end == ' ' || *end == '\t' )
        end++;
    if( end == text || *end )
    {
        CONTENT_ERROR("`^%s` is `%s`, which is not a number\n",
                      *name == '^' ? name + 1 : name, text);
        return fallback;
    }
    return (int)value;
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

static struct Mock230EnumDef* g_enum_defs;
static int g_enum_def_count;
static int g_enum_def_capacity;

static struct Mock230VarpDef* g_varp_defs;
static int g_varp_def_count;
static int g_varp_def_capacity;

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

const struct Mock230EnumDef*
mock230_content_enum(const char* symbol)
{
    for( int i = 0; i < g_enum_def_count; i++ )
    {
        if( g_enum_defs[i].symbol && strcmp(g_enum_defs[i].symbol, symbol) == 0 )
            return &g_enum_defs[i];
    }
    return NULL;
}

const struct Mock230VarpDef*
mock230_content_varp(int varp_id)
{
    for( int i = 0; i < g_varp_def_count; i++ )
    {
        if( g_varp_defs[i].varp_id == varp_id )
            return &g_varp_defs[i];
    }
    return NULL;
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
/* .obj configs                                                        */
/* ------------------------------------------------------------------ */

/*
 * Equipment requirements, and nothing else yet.
 *
 * The same overlay contract the `.npc` grammar has: a block starts from what
 * `cache.osrs239` already says about the record and states only what a cache
 * cannot. For an obj that is a short list, because the cache is unusually
 * generous here — name, model, cost, weight, stackability, stack variants, the
 * wear positions, the category, the twelve combat bonuses and the attack rate
 * are all in the record already, and restating any of them in a config is how a
 * config comes to disagree with the client.
 *
 * What it cannot state is the level you need to *wear* the thing, except for a
 * subset and never more than two of them. See docs/mock230_content.md §5 and
 * tools/kronos_item_import.py.
 *
 *     [mithril_scimitar]
 *     param=levelrequire,attack,20
 *
 *     [pest_void_knight_top]
 *     param=levelrequire,attack,42
 *     param=levelrequire,defence,42
 *     ...
 *
 * `levelrequire` is repeatable and its value is `<skill>,<level>`, which is a
 * deliberate departure from the cache's fixed 434/436 + 435/437 pair: the pair
 * is why an OldSchool cache cannot express Void knight gear at all, and there is
 * no reason for a config this server owns to inherit the limit. LostCity spells
 * its own single-requirement form `param=levelrequire,N`; this is that with the
 * skill named, because rev 230 has items requiring seven.
 */
static void
obj_config_key(
    const char* key,
    const char* value,
    int* stats,
    int* levels,
    int* count,
    const char* where)
{
    char param_name[64] = { 0 };
    char skill_name[64] = { 0 };
    int level = 0;
    int stat;

    if( strcmp(key, "param") != 0 )
    {
        /* Every other LostCity obj key states something the cache already does.
         * Accepted and ignored, like the `.npc` grammar's `model=` — but never
         * silently, so a paste from a LostCity tree reports what it dropped. */
        CONTENT_ERROR("%s: obj key `%s` is the cache's to state, ignored\n", where, key);
        return;
    }
    if( sscanf(value, "%63[^,],%63[^,],%d", param_name, skill_name, &level) != 3 )
    {
        CONTENT_ERROR("%s: `param=levelrequire,<skill>,<level>` is the shape, got `%s`\n",
                      where, value);
        return;
    }
    if( strcmp(param_name, "levelrequire") != 0 )
    {
        CONTENT_ERROR("%s: obj param `%s` is not one this server reads\n", where, param_name);
        return;
    }
    stat = mock230_content_symbol(MOCK230_PACK_STAT, skill_name);
    if( stat < 0 )
    {
        CONTENT_ERROR("%s: `%s` is not in server/pack/stat.pack\n", where, skill_name);
        return;
    }
    if( level <= 0 || level > 99 )
    {
        CONTENT_ERROR("%s: level %d is outside 1..99\n", where, level);
        return;
    }
    if( *count >= MOCK230_OBJ_REQUIRE_MAX )
    {
        CONTENT_ERROR("%s: more than %d requirements on one obj\n", where,
                      MOCK230_OBJ_REQUIRE_MAX);
        return;
    }
    stats[*count] = stat;
    levels[*count] = level;
    (*count)++;
}

static void
load_obj_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    char where[600];
    int obj_id = -1;
    int stats[MOCK230_OBJ_REQUIRE_MAX];
    int levels[MOCK230_OBJ_REQUIRE_MAX];
    int count = 0;
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
            /* Flush the block that just ended before starting the next one. The
             * requirements are a set, so they are applied once per block rather
             * than accumulated into the table a line at a time. */
            if( obj_id >= 0 && count )
                mock230_obj_require_set(obj_id, stats, levels, count);
            count = 0;
            obj_id = mock230_content_symbol(MOCK230_PACK_OBJ, header);
            if( obj_id < 0 )
                CONTENT_ERROR("%s:%d: `%s` is not in pack/obj.pack\n", path, line_number,
                              header);
            continue;
        }

        value = split_key_value(line);
        if( !value )
        {
            CONTENT_ERROR("%s:%d: expected `key=value`\n", path, line_number);
            continue;
        }
        if( obj_id < 0 )
        {
            CONTENT_ERROR("%s:%d: `%s` before any [section]\n", path, line_number, line);
            continue;
        }
        snprintf(where, sizeof(where), "%s:%d", path, line_number);
        obj_config_key(line, value, stats, levels, &count, where);
    }
    if( obj_id >= 0 && count )
        mock230_obj_require_set(obj_id, stats, levels, count);
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .constant configs                                                   */
/* ------------------------------------------------------------------ */

/*
 * `^name = value`, one per line, no sections. LostCity writes the caret on the
 * declaration as well as at every use, so the file is greppable for either.
 *
 * The text is kept verbatim rather than parsed: a constant expands to whatever
 * the grammar accepts — a number here, but a coord or a string in a `.rs2` — and
 * the serverscript compiler reads the same files through ssc_symbols.
 */
static void
load_constant_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = clean_line(raw);
        char* value;

        line_number++;
        if( !*line )
            continue;
        if( *line != '^' )
        {
            CONTENT_ERROR("%s:%d: a .constant holds `^name = value` lines only\n", path,
                          line_number);
            continue;
        }
        value = split_key_value(line);
        if( !value )
        {
            CONTENT_ERROR("%s:%d: `%s` has no `=`\n", path, line_number, line);
            continue;
        }
        if( mock230_content_constant(line + 1) )
        {
            CONTENT_ERROR("%s:%d: `%s` is declared twice\n", path, line_number, line);
            continue;
        }
        g_constants =
            grow(g_constants, &g_constant_capacity, g_constant_count, sizeof(*g_constants));
        g_constants[g_constant_count].name = strdup(line + 1);
        g_constants[g_constant_count].text = strdup(value);
        g_constant_count++;
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .enum configs                                                       */
/* ------------------------------------------------------------------ */

/** Map a `.enum` type name onto the pack it resolves against. */
static enum Mock230PackKind
pack_kind_for_type(const char* name)
{
    static const struct
    {
        const char* name;
        enum Mock230PackKind kind;
    } k_map[] = {
        { "npc", MOCK230_PACK_NPC },         { "namedobj", MOCK230_PACK_OBJ },
        { "obj", MOCK230_PACK_OBJ },         { "loc", MOCK230_PACK_LOC },
        { "seq", MOCK230_PACK_SEQ },         { "spotanim", MOCK230_PACK_SPOTANIM },
        { "inv", MOCK230_PACK_INV },         { "varp", MOCK230_PACK_VARP },
        { "varbit", MOCK230_PACK_VARBIT },
        { "interface", MOCK230_PACK_INTERFACE },
        { "component", MOCK230_PACK_COMPONENT },
        { "stat", MOCK230_PACK_STAT },       { "param", MOCK230_PACK_PARAM },
        { "hitsplat", MOCK230_PACK_HITSPLAT },
    };

    for( size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++ )
    {
        if( strcmp(name, k_map[i].name) == 0 )
            return k_map[i].kind;
    }
    /* `int` and anything else unlisted: the operand is a literal, not a
     * symbol. MOCK230_PACK_COUNT is the sentinel for that. */
    return MOCK230_PACK_COUNT;
}

/** Resolve one side of a `val=` line: a symbol when the declared type names a
 *  pack, a literal when it does not. `^name` expands first, either way — the
 *  reference writes `val=^prayer_thickskin,3`. */
static int
enum_operand(
    enum Mock230PackKind kind,
    const char* text,
    int* out_ok)
{
    *out_ok = 1;
    if( *text == '^' )
    {
        const char* expanded = mock230_content_constant(text);

        if( !expanded )
        {
            *out_ok = 0;
            return -1;
        }
        text = expanded;
    }
    if( kind == MOCK230_PACK_COUNT )
        return atoi(text);
    {
        int id = mock230_content_symbol(kind, text);

        if( id < 0 )
            *out_ok = 0;
        return id;
    }
}

static void
load_enum_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230EnumDef* def = NULL;
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
            g_enum_defs = grow(g_enum_defs, &g_enum_def_capacity, g_enum_def_count,
                               sizeof(*g_enum_defs));
            def = &g_enum_defs[g_enum_def_count++];
            memset(def, 0, sizeof(*def));
            def->symbol = strdup(header);
            def->input_kind = MOCK230_PACK_COUNT;
            def->output_kind = MOCK230_PACK_COUNT;
            continue;
        }

        value = split_key_value(line);
        if( !value || !def )
        {
            CONTENT_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                          line_number);
            continue;
        }

        if( strcmp(line, "inputtype") == 0 )
            def->input_kind = pack_kind_for_type(value);
        else if( strcmp(line, "outputtype") == 0 )
            def->output_kind = pack_kind_for_type(value);
        else if( strcmp(line, "default") == 0 )
            continue; /* carried by the reference; nothing here reads it */
        else if( strcmp(line, "val") == 0 )
        {
            char* comma = strchr(value, ',');
            int key_ok = 0;
            int value_ok = 0;
            int key;
            int mapped;

            if( !comma )
            {
                CONTENT_ERROR("%s:%d: val needs `key,value`\n", path, line_number);
                continue;
            }
            *comma = '\0';
            key = enum_operand(def->input_kind, value, &key_ok);
            mapped = enum_operand(def->output_kind, comma + 1, &value_ok);
            if( !key_ok )
                CONTENT_ERROR("%s:%d: `%s` does not resolve\n", path, line_number, value);
            if( !value_ok )
                CONTENT_ERROR("%s:%d: `%s` does not resolve\n", path, line_number,
                              comma + 1);
            if( !key_ok || !value_ok )
                continue;
            if( def->count >= MOCK230_ENUM_VALUE_MAX )
            {
                CONTENT_ERROR("%s:%d: more than %d values in one enum\n", path,
                              line_number, MOCK230_ENUM_VALUE_MAX);
                continue;
            }
            def->values[def->count].key = key;
            def->values[def->count].value = mapped;
            def->count++;
        }
        else
        {
            CONTENT_ERROR("%s:%d: unknown enum key `%s`\n", path, line_number, line);
        }
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .varp configs                                                       */
/* ------------------------------------------------------------------ */

static void
load_varp_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230VarpDef* def = NULL;
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
            int varp_id = mock230_content_symbol(MOCK230_PACK_VARP, header);

            def = NULL;
            if( varp_id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in pack/varp.pack\n", path, line_number,
                              header);
                continue;
            }
            g_varp_defs = grow(g_varp_defs, &g_varp_def_capacity, g_varp_def_count,
                               sizeof(*g_varp_defs));
            def = &g_varp_defs[g_varp_def_count++];
            memset(def, 0, sizeof(*def));
            def->varp_id = varp_id;
            def->symbol = mock230_content_symbol_name(MOCK230_PACK_VARP, varp_id);
            def->clientcode = -1;
            continue;
        }

        value = split_key_value(line);
        if( !value || !def )
        {
            CONTENT_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                          line_number);
            continue;
        }

        if( strcmp(line, "transmit") == 0 )
            def->transmit = strcmp(value, "yes") == 0;
        else if( strcmp(line, "protect") == 0 )
            def->protect = strcmp(value, "yes") == 0;
        else if( strcmp(line, "scope") == 0 )
            def->scope_perm = strcmp(value, "perm") == 0;
        else if( strcmp(line, "clientcode") == 0 )
            def->clientcode = atoi(value);
        else
            CONTENT_ERROR("%s:%d: unknown varp key `%s`\n", path, line_number, line);
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
/* .prayer configs                                                     */
/* ------------------------------------------------------------------ */

static struct Mock230PrayerDef* g_prayer_defs;
static int g_prayer_def_count;
static int g_prayer_def_capacity;

const struct Mock230PrayerDef*
mock230_content_prayer(int index)
{
    if( index < 0 || index >= g_prayer_def_count )
        return NULL;
    return &g_prayer_defs[index];
}

int
mock230_content_prayer_count(void)
{
    return g_prayer_def_count;
}

/** `group=` takes the reference's stat names, one per line so a prayer can
 *  claim several — which Piety, Chivalry, Rigour and Augury all do. */
static int
prayer_group(const char* name)
{
    static const struct
    {
        const char* name;
        int mask;
    } k_groups[] = {
        { "defence", MOCK230_PRAYER_GROUP_DEFENCE },
        { "strength", MOCK230_PRAYER_GROUP_STRENGTH },
        { "attack", MOCK230_PRAYER_GROUP_ATTACK },
        { "ranged", MOCK230_PRAYER_GROUP_RANGED },
        { "magic", MOCK230_PRAYER_GROUP_MAGIC },
        { "overhead", MOCK230_PRAYER_GROUP_OVERHEAD },
    };

    for( size_t i = 0; i < sizeof(k_groups) / sizeof(k_groups[0]); i++ )
        if( strcmp(name, k_groups[i].name) == 0 )
            return k_groups[i].mask;
    return 0;
}

/** A `key=value` operand that may be a `^constant`. */
static int
config_int(const char* text)
{
    const char* expanded = *text == '^' ? mock230_content_constant(text) : NULL;

    return atoi(expanded ? expanded : text);
}

/** One `key=value` inside a prayer block. Returns 0 for a key it does not
 *  know, so the caller can report it with the line number. */
static int
prayer_field(
    struct Mock230PrayerDef* def,
    const char* key,
    const char* value)
{
    if( strcmp(key, "name") == 0 )
    {
        free(def->name);
        def->name = strdup(value);
    }
    else if( strcmp(key, "level") == 0 )
        def->level = config_int(value);
    else if( strcmp(key, "drain") == 0 )
        def->drain = config_int(value);
    else if( strcmp(key, "headicon") == 0 )
        def->headicon = config_int(value);
    else if( strcmp(key, "group") == 0 )
        def->groups |= prayer_group(value);
    else if( strcmp(key, "button") == 0 )
        def->button = mock230_content_symbol(MOCK230_PACK_COMPONENT, value);
    else
        return 0;
    return 1;
}

/** A fresh block, or NULL when the tree declares more prayers than the mask
 *  can hold. */
static struct Mock230PrayerDef*
prayer_begin(const char* symbol)
{
    struct Mock230PrayerDef* def;

    if( g_prayer_def_count >= MOCK230_PRAYER_MAX )
        return NULL;
    g_prayer_defs =
        grow(g_prayer_defs, &g_prayer_def_capacity, g_prayer_def_count, sizeof(*g_prayer_defs));
    def = &g_prayer_defs[g_prayer_def_count++];
    memset(def, 0, sizeof(*def));
    def->symbol = strdup(symbol);
    def->name = strdup(symbol);
    def->level = 1;
    def->drain = 1;
    def->headicon = -1;
    def->button = -1;
    return def;
}

static void
load_prayer_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230PrayerDef* def = NULL;
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
            def = prayer_begin(header);
            if( !def )
                CONTENT_ERROR("%s:%d: more than %d prayers; `prayer_active` is a "
                              "32-bit mask\n",
                              path, line_number, MOCK230_PRAYER_MAX);
            continue;
        }

        value = split_key_value(line);
        if( !value || !def )
            CONTENT_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                          line_number);
        else if( !prayer_field(def, line, value) )
            CONTENT_ERROR("%s:%d: unknown prayer key `%s`\n", path, line_number, line);
        else if( strcmp(line, "group") == 0 && !prayer_group(value) )
            CONTENT_ERROR("%s:%d: unknown prayer group `%s`\n", path, line_number, value);
        else if( strcmp(line, "button") == 0 && def->button < 0 )
            CONTENT_ERROR("%s:%d: `%s` is not in pack/component.pack\n", path, line_number,
                          value);
    }
    fclose(file);
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
    /*
     * The namespace register.
     *
     * One row per namespace rather than one per *file*, because a namespace is
     * two layers of one name domain (see the Packs section above): layer 0 in
     * `pack/<ns>.pack`, layer 1 in `names/<ns>.pack`. A namespace with no cache
     * table — `stat`, which no gameval archive names — simply has no layer 0,
     * and a missing pack loads as zero symbols rather than as an error.
     *
     * This is also step 2 of docs/CONTENT_ARCHITECTURE.md in embryo: when the
     * `content.ini` register lands, this table is what it replaces, and the same
     * rows are what sscompile and cachepack should be reading instead of their
     * own filename tables.
     */
    static const struct
    {
        const char* ns;
        enum Mock230PackKind kind;
    } k_namespaces[] = {
        { "npc",       MOCK230_PACK_NPC       },
        { "obj",       MOCK230_PACK_OBJ       },
        { "loc",       MOCK230_PACK_LOC       },
        { "seq",       MOCK230_PACK_SEQ       },
        { "spotanim",  MOCK230_PACK_SPOTANIM  },
        { "inv",       MOCK230_PACK_INV       },
        { "varp",      MOCK230_PACK_VARP      },
        { "varbit",    MOCK230_PACK_VARBIT    },
        { "interface", MOCK230_PACK_INTERFACE },
        { "component", MOCK230_PACK_COMPONENT },
        { "param",     MOCK230_PACK_PARAM     },
        { "hitsplat",  MOCK230_PACK_HITSPLAT  },
        { "stat",      MOCK230_PACK_STAT      },
    };
    /*
     * Where layer 1 used to live, before it was a layer.
     *
     * Kept so a content tree that has not moved its files yet still loads —
     * these are read at layer 1, which is what they always were in spirit. They
     * go away once the tree ships `names/`.
     */
    static const struct
    {
        const char* file;
        enum Mock230PackKind kind;
    } k_legacy_authored[] = {
        { "server/pack/stat.pack",      MOCK230_PACK_STAT },
        { "server/pack/varp_mock.pack", MOCK230_PACK_VARP },
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

    /*
     * Layer 0 first, then layer 1, then the pre-`names/` locations.
     *
     * Order is not resolution — the lookups pick by layer, not by position — but
     * loading in this order keeps the diagnostics readable: an authored name is
     * reported against the cache name it collides with, and the cache name has
     * to be present for that to say anything useful.
     */
    for( size_t i = 0; i < sizeof(k_namespaces) / sizeof(k_namespaces[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/pack/%s.pack", dir, k_namespaces[i].ns);
        symbols += pack_load(&g_packs[k_namespaces[i].kind], path, PACK_LAYER_CACHE);
    }
    for( size_t i = 0; i < sizeof(k_namespaces) / sizeof(k_namespaces[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/names/%s.pack", dir, k_namespaces[i].ns);
        symbols += pack_load(&g_packs[k_namespaces[i].kind], path, PACK_LAYER_AUTHORED);
    }
    for( size_t i = 0; i < sizeof(k_legacy_authored) / sizeof(k_legacy_authored[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", dir, k_legacy_authored[i].file);
        symbols += pack_load(&g_packs[k_legacy_authored[i].kind], path, PACK_LAYER_AUTHORED);
    }

    /* A namespace whose two layers disagree is refused here rather than
     * resolved silently later. */
    validate_name_layers();

    /* After the packs (a default names its animations by symbol) and before the
     * configs (each block starts from a copy of it). */
    init_defaults();

    snprintf(path, sizeof(path), "%s/server/scripts", dir);
    /* Constants first: every other grammar may write `^name` where a number
     * goes, and an unexpanded caret is a load error rather than a zero. */
    walk_configs(path, ".constant", load_constant_config);
    walk_configs(path, ".enum", load_enum_config);
    walk_configs(path, ".varp", load_varp_config);
    walk_configs(path, ".npc", load_npc_config);
    walk_configs(path, ".obj", load_obj_config);
    walk_configs(path, ".loc", load_loc_config);
    walk_configs(path, ".prayer", load_prayer_config);
    resolve_loc_stages();

    snprintf(path, sizeof(path), "%s/maps", dir);
    load_maps(path);

    {
        int requires_total = 0;
        int requires_from_cache = 0;

        mock230_obj_require_counts(&requires_total, &requires_from_cache);
        fprintf(stderr,
                "mock230: content loaded (%d symbols, %d constants, %d npc defs, %d loc defs, "
                "%d varp defs, %d prayers, %d equip reqs (%d from the cache), %d npc spawns, "
                "%d obj spawns%s)\n",
                symbols, g_constant_count, g_npc_def_count, g_loc_def_count, g_varp_def_count,
                g_prayer_def_count, requires_total, requires_from_cache, g_npc_spawn_count,
                g_obj_spawn_count, g_errors ? ", WITH ERRORS" : "");
    }
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
    free(g_varp_defs);
    g_varp_defs = NULL;
    g_varp_def_count = g_varp_def_capacity = 0;
    for( int i = 0; i < g_enum_def_count; i++ )
        free((void*)g_enum_defs[i].symbol);
    free(g_enum_defs);
    g_enum_defs = NULL;
    g_enum_def_count = g_enum_def_capacity = 0;
    for( int i = 0; i < g_prayer_def_count; i++ )
    {
        free((void*)g_prayer_defs[i].symbol);
        free(g_prayer_defs[i].name);
    }
    free(g_prayer_defs);
    g_prayer_defs = NULL;
    g_prayer_def_count = g_prayer_def_capacity = 0;
    for( int i = 0; i < g_constant_count; i++ )
    {
        free(g_constants[i].name);
        free(g_constants[i].text);
    }
    free(g_constants);
    g_constants = NULL;
    g_constant_count = g_constant_capacity = 0;
    free(g_npc_spawns);
    g_npc_spawns = NULL;
    g_npc_spawn_count = g_npc_spawn_capacity = 0;
    free(g_obj_spawns);
    g_obj_spawns = NULL;
    g_obj_spawn_count = g_obj_spawn_capacity = 0;
    g_errors = 0;
}
