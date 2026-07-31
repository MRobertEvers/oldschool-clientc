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

#include "content/content_fields.h"
#include "content/content_register.h"
#include "mock230.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static int g_errors;

/* The npc field register, loaded once per content load. Read by the config parser
 * to decide what an unrecognised key means. */
static struct ContentFields g_npc_fields;
static struct ContentFields g_loc_fields;
/* How many overlay lines restated something the client's own record carries. */
static int g_client_key_overlays;

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

void
mock230_content_report_error(
    const char* fmt,
    ...)
{
    va_list args;

    /* Same prefix and the same counter as CONTENT_ERROR, so a bad line in a
     * `.dbtable` read by mock230_db.c is indistinguishable from a bad line in a
     * `.npc` read here — which is the point. A second reader with its own error
     * channel is a reader whose failures do not stop the server. */
    fprintf(stderr, "mock230: content: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    g_errors++;
}

/* ------------------------------------------------------------------ */
/* Text helpers                                                        */
/* ------------------------------------------------------------------ */

/** Strip a `//` comment and surrounding whitespace, in place. */
char*
mock230_content_clean_line(char* line)
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
char*
mock230_content_split_key_value(char* line)
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
char*
mock230_content_section_header(char* line)
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
 * One file per namespace: `pack/<ns>.pack`, `id=name` per line.
 *
 * There used to be two — `pack/` for what the cache's gameval table said and
 * `names/` for what a human wrote — and the reason was never that a namespace has
 * two name domains. It was that a pack *save* emitted nothing but `id=name` lines,
 * so any tool regenerating `configs/all.varp.compack` deleted every comment in it and every
 * line it had not generated. `configs/all.param.compack` lost all 58 of its header lines to
 * one `cachepack unpack`, and that header was the record of which param-id claims
 * had been checked against a cache and how.
 *
 * That hazard is fixed at the writer rather than worked around by the directory
 * layout (docs/CONTENT_PACK_PLAN.md §1): a pack save preserves comments and
 * *merges*, so one file can hold the cache's names and this world's. Where the two
 * disagree about an id, the authored name is the line and the cache's is a
 * trailing note:
 *
 *     <id>=<authored name>  // cache: <gameval name>
 *
 * The one thing a single file cannot hold is an *alias* — a second name for an id
 * the cache already names — because a line binds one name to one id. That is a
 * deliberate loss, decided per case during the migration; no alias in this tree
 * needed keeping, and `validate_symbols` refuses a file that tries.
 *
 * Which makes an override a *rename*, not an addition, and worth spending only
 * where the cache's name is wrong for what the id now holds. No file in this
 * tree spends it: there is not one `// cache:` note left. There were eleven, all
 * in `configs/all.varp.compack`, and none of them earned it — eight varps
 * relabelled after a varbit they merely carry (`843=varp_weapon_category`,
 * `867=bank_tab_a`, …) plus three holding this server's scratch counters. Each
 * cost the cache's own spelling: `randomhitsound` and `prayer23` resolved to
 * nothing while the rename stood. One of them cost more — varp 1 put a
 * whole-varp counter on top of twelve Dwarf Cannon varbits. `--selftest` now
 * pins the eight cache names and the three scratch ids.
 */
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

    /* A missing file is not an error: a namespace nothing has named yet has no
     * file at all, which is a different claim from an empty one. */
    if( !file )
        return 0;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = mock230_content_clean_line(raw);
        char* name;

        if( !*line )
            continue;
        name = mock230_content_split_key_value(line);
        if( !name || !*name )
            continue;
        pack_add(pack, name, atoi(line));
        loaded++;
    }
    fclose(file);
    return loaded;
}

/**
 * Build the component symbols the way the client addresses a component: an
 * interface id in the high 16 bits and a child id in the low 16.
 *
 * There is no `pack/component.pack` to read. There used to be — 26,491 lines of
 * `786432=bankmain:infinite` — and it was a second index over members that
 * `interfaces/bankmain.compack` already indexes, one named and one filler. The
 * compack carries the cache's own names now, so the id composes from the two files
 * that remain: `pack/3_interfaces.pack` says `bankmain` is 12, the compack says
 * `infinite` is child 0, and `(12 << 16) | 0` is 786432.
 *
 * Composed at load into the same flat table the other namespaces use, so a lookup
 * stays one pass and `mock230_ids.c` still asks for `"bankmain:items"`.
 */
static int
load_component_symbols(const char* dir)
{
    const struct Pack* interfaces = &g_packs[MOCK230_PACK_INTERFACE];
    int loaded = 0;

    for( int i = 0; i < interfaces->count; i++ )
    {
        const char* iface_name = interfaces->entries[i].name;
        int iface_id = interfaces->entries[i].id;
        struct Pack children;
        char path[1024];

        if( iface_id < 0 || !iface_name )
            continue;
        memset(&children, 0, sizeof(children));
        snprintf(path, sizeof(path), "%s/interfaces/%s.compack", dir, iface_name);
        if( pack_load(&children, path) == 0 )
        {
            free(children.entries);
            continue;
        }
        for( int c = 0; c < children.count; c++ )
        {
            char full[512];

            if( children.entries[c].id < 0 || children.entries[c].id > 0xffff )
                continue;
            snprintf(full, sizeof(full), "%s:%s", iface_name, children.entries[c].name);
            pack_add(&g_packs[MOCK230_PACK_COMPONENT], full,
                     (iface_id << 16) | children.entries[c].id);
            loaded++;
        }
        for( int c = 0; c < children.count; c++ )
            free(children.entries[c].name);
        free(children.entries);
    }
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

    /* One file, one pass. There is no precedence question left: a name means one
     * id or the loader refused to start (see validate_symbols). */
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

/* Forward: the diagnostic namespace name, defined just below. */
static const char*
pack_kind_name(enum Mock230PackKind kind);

/**
 * Refuse to start on a symbol table that answers a name two ways.
 *
 * Two rules, both LostCity's (`packConfigs()`), and neither of them about how many
 * *files* a namespace has — which is why both survived the collapse to one file
 * per namespace while the third rule (an authored name shadowing a cache name in a
 * different file) simply stopped being expressible:
 *
 * 1. **A name means one id within a namespace.** Two lines binding `bankcert` to
 *    115 and to 843 make it mean one thing to a reader and another to the loader,
 *    and last-one-wins is not a resolution. This is also the check that catches
 *    the one thing the single-file format cannot hold: an alias for an id the
 *    cache already names, which a migration might try to reintroduce.
 *
 * 2. **varp, varbit, varn and vars share one RuneScript name domain.** `%name`
 *    does not say which of the four it is, so a name meaning varp 115 and varbit 4
 *    cannot coexist however separate their pack files look. The membership comes
 *    from the register's `vardomain` column rather than a list here, so adding a
 *    namespace to the domain is one declaration.
 *
 * Returns the number of collisions; each is reported through the content error
 * count, so `mock230_pack` fails on them too.
 */
/**
 * One symbol, tagged with the namespace it came from, for the collision sort.
 *
 * Sorted rather than compared pairwise, and that is not a micro-optimisation: the
 * merged tables are large — 62,194 locs, 26,491 components — and the pairwise form
 * of this check is billions of `strcmp`s before the server prints its first line.
 */
struct SymbolRow
{
    const char* name;
    int id;
    int kind;
};

static int
compare_symbol_row(
    const void* lhs,
    const void* rhs)
{
    const struct SymbolRow* left = lhs;
    const struct SymbolRow* right = rhs;
    int order = strcmp(left->name, right->name);

    if( order != 0 )
        return order;
    /* Ties by id so the report is stable, and so a name listed twice for the same
     * id lands adjacent and reads as redundant rather than ambiguous. */
    return left->id - right->id;
}

/** Sorted copy of one or more namespaces' rows. Caller frees. */
static struct SymbolRow*
collect_rows(
    const int* kinds,
    int kind_count,
    int* out_count)
{
    int total = 0;

    for( int k = 0; k < kind_count; k++ )
        total += g_packs[kinds[k]].count;
    *out_count = total;
    if( total == 0 )
        return NULL;

    struct SymbolRow* rows = malloc((size_t)total * sizeof(*rows));
    if( !rows )
        return NULL;
    int at = 0;
    for( int k = 0; k < kind_count; k++ )
    {
        const struct Pack* pack = &g_packs[kinds[k]];
        for( int i = 0; i < pack->count; i++ )
        {
            rows[at].name = pack->entries[i].name;
            rows[at].id = pack->entries[i].id;
            rows[at].kind = kinds[k];
            at++;
        }
    }
    qsort(rows, (size_t)total, sizeof(*rows), compare_symbol_row);
    return rows;
}

static int
validate_symbols(const struct ContentRegister* reg)
{
    int collisions = 0;
    int shared[MOCK230_PACK_COUNT];
    int shared_count = 0;

    for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
    {
        int only[1] = { kind };
        int count = 0;
        struct SymbolRow* rows = collect_rows(only, 1, &count);
        const struct ContentNamespace* ns =
            ContentRegister_Find(reg, pack_kind_name((enum Mock230PackKind)kind));

        if( ns && ns->shared_var_domain )
            shared[shared_count++] = kind;

        for( int i = 1; i < count; i++ )
        {
            if( strcmp(rows[i].name, rows[i - 1].name) != 0 )
                continue;
            if( rows[i].id == rows[i - 1].id )
                continue; /* the same line twice is redundant, not ambiguous */
            CONTENT_ERROR("pack/%s.pack: `%s` is both %d and %d — a name means one id\n",
                          pack_kind_name((enum Mock230PackKind)kind), rows[i].name,
                          rows[i - 1].id, rows[i].id);
            collisions++;
        }
        free(rows);
    }

    /* The `%name` domain, as the register declares it: all its namespaces sorted
     * together, so a name appearing under two of them lands adjacent. */
    if( shared_count > 1 )
    {
        int count = 0;
        struct SymbolRow* rows = collect_rows(shared, shared_count, &count);

        for( int i = 1; i < count; i++ )
        {
            if( strcmp(rows[i].name, rows[i - 1].name) != 0 )
                continue;
            if( rows[i].kind == rows[i - 1].kind )
                continue; /* already reported by the per-namespace pass */
            CONTENT_ERROR("`%s` is both %s %d and %s %d — one RuneScript name domain "
                          "covers both\n",
                          rows[i].name, pack_kind_name((enum Mock230PackKind)rows[i - 1].kind),
                          rows[i - 1].id, pack_kind_name((enum Mock230PackKind)rows[i].kind),
                          rows[i].id);
            collisions++;
        }
        free(rows);
    }

    return collisions;
}

/**
 * The namespace a kind is spelled with: `pack/<name>.pack`, and the register row.
 *
 * One table rather than two. This used to name only the thirteen kinds a
 * diagnostic had ever mentioned, with the other eight left NULL, while a second
 * list inside `mock230_content_load` said which files to read — so a kind could be
 * loadable and unnameable, or named and never loaded, and nothing compared them.
 * Every kind is spelled here and the loader walks this.
 */
const char*
mock230_content_pack_name(enum Mock230PackKind kind)
{
    return pack_kind_name(kind);
}

static const char*
pack_kind_name(enum Mock230PackKind kind)
{
    static const char* k_names[MOCK230_PACK_COUNT] = {
        [MOCK230_PACK_NPC] = "npc",
        [MOCK230_PACK_OBJ] = "obj",
        [MOCK230_PACK_LOC] = "loc",
        [MOCK230_PACK_SEQ] = "seq",
        [MOCK230_PACK_SPOTANIM] = "spotanim",
        [MOCK230_PACK_INV] = "inv",
        [MOCK230_PACK_VARP] = "varp",
        [MOCK230_PACK_VARBIT] = "varbit",
        /* The archive index of cache index 3; see content_register.h `cache_index`. */
        [MOCK230_PACK_INTERFACE] = "3_interfaces",
        [MOCK230_PACK_COMPONENT] = "component",
        [MOCK230_PACK_STAT] = "stat",
        [MOCK230_PACK_PARAM] = "param",
        [MOCK230_PACK_HITSPLAT] = "hitsplat",
        [MOCK230_PACK_ENUM] = "enum",
        [MOCK230_PACK_STRUCT] = "struct",
        [MOCK230_PACK_DBTABLE] = "dbtable",
        [MOCK230_PACK_DBROW] = "dbrow",
        [MOCK230_PACK_CATEGORY] = "category",
    };

    if( kind < 0 || kind >= MOCK230_PACK_COUNT )
        return "?";
    return k_names[kind];
}

/**
 * 1 when this kind's records are files of a config archive, so its index is a
 * `.compack` beside `configs/all.<type>` rather than a pack in `pack/`.
 *
 * The four that are not: interfaces are cache index 3 and get an archive-level
 * pack; components are named across every one of those archives at once; `stat` is
 * fixed by the wire; `category` is an obj field the cache numbers and nothing names.
 */
static int
pack_kind_is_config(enum Mock230PackKind kind)
{
    switch( kind )
    {
    case MOCK230_PACK_INTERFACE:
    case MOCK230_PACK_COMPONENT:
    case MOCK230_PACK_STAT:
    case MOCK230_PACK_CATEGORY:
        return 0;
    default:
        return 1;
    }
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

const struct Mock230EnumDef*
mock230_content_enum_by_id(int enum_id)
{
    const char* symbol;

    if( enum_id < 0 )
        return NULL;
    /*
     * Id -> name -> def, rather than storing the id on the def.
     *
     * The name is the only thing a `.enum` block states; the id comes from
     * `configs/all.enum.compack`, which is loaded by the time any config is read. Going
     * through the pack keeps one answer to "what number is this enum" — the
     * alternative, resolving it while parsing and caching it on the def, is a
     * second answer that silently disagrees when a pack is regenerated.
     */
    symbol = mock230_content_symbol_name(MOCK230_PACK_ENUM, enum_id);
    if( !symbol )
        return NULL;
    return mock230_content_enum(symbol);
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
        /*
         * A key the ladder above does not handle. The field register decides what
         * that means, rather than a list in this file.
         *
         * LostCity authors `name=` and `op1=` because it *builds* the npc record;
         * ours comes from the cache, so those keys are inert — accepted so a config
         * can be shared with a LostCity tree unchanged. That used to be a
         * `k_from_cache[]` array right here, which put "the client already states
         * this" in C where a content author could neither see it nor add to it.
         *
         * Declared `scope = client` now says something more useful than "ignored":
         * the overlay is *patching the cache*, and that is worth a line rather than
         * silence. Anything the register does not declare at all is still an error.
         */
        const struct ContentField* field = ContentFields_Find(&g_npc_fields, key);

        if( field && field->scope == CONTENT_SCOPE_CLIENT )
        {
            g_client_key_overlays++;
            return;
        }
        if( field )
        {
            /* Declared, but this parser has no field for it — a register row that
             * has run ahead of the struct. Worth saying, because the value is being
             * dropped. */
            CONTENT_ERROR("%s: `%s` is declared in fields/npc.ini but this build has "
                          "nowhere to put it\n",
                          where, key);
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
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            int npc_id = mock230_content_symbol(MOCK230_PACK_NPC, header);

            def = NULL;
            if( npc_id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.npc.compack\n", path, line_number,
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

        value = mock230_content_split_key_value(line);
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
 * subset and never more than two of them. See docs/mock230_content.md §5.
 *
 * The values were transcribed from a Kronos server dump by an importer that no
 * longer exists; they are authored content now and this tree is their home.
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
        CONTENT_ERROR("%s: `%s` is not in pack/stat.pack\n", where, skill_name);
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
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
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
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.obj.compack\n", path, line_number,
                              header);
            continue;
        }

        value = mock230_content_split_key_value(line);
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
        char* line = mock230_content_clean_line(raw);
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
        value = mock230_content_split_key_value(line);
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
        { "enum", MOCK230_PACK_ENUM },       { "struct", MOCK230_PACK_STRUCT },
        { "dbtable", MOCK230_PACK_DBTABLE }, { "dbrow", MOCK230_PACK_DBROW },
        { "category", MOCK230_PACK_CATEGORY },
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

/*
 * Whether a declared `.enum` side holds text.
 *
 * Separate from pack_kind_for_type because that answers "which pack does this
 * resolve against", and `int` and `string` both answer "none". The RuneScript
 * `enum` opcode needs the other question answered, because the output type picks
 * which stack it pushes onto.
 */
static int
type_is_string(const char* name)
{
    return strcmp(name, "string") == 0;
}

/** Expand a leading `^constant`, or return the text unchanged. NULL when the
 *  constant does not resolve. */
static const char*
enum_text(const char* text)
{
    if( *text != '^' )
        return text;
    return mock230_content_constant(text);
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
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            g_enum_defs = grow(g_enum_defs, &g_enum_def_capacity, g_enum_def_count,
                               sizeof(*g_enum_defs));
            def = &g_enum_defs[g_enum_def_count++];
            memset(def, 0, sizeof(*def));
            def->symbol = strdup(header);
            def->input_kind = MOCK230_PACK_COUNT;
            def->output_kind = MOCK230_PACK_COUNT;
            /* The reference's own defaults, so a key with no entry yields what
             * its content expects rather than a zero that means "found". */
            def->default_int = 0;
            def->default_text = "null";
            continue;
        }

        value = mock230_content_split_key_value(line);
        if( !value || !def )
        {
            CONTENT_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                          line_number);
            continue;
        }

        if( strcmp(line, "inputtype") == 0 )
        {
            def->input_kind = pack_kind_for_type(value);
            def->input_is_string = type_is_string(value);
        }
        else if( strcmp(line, "outputtype") == 0 )
        {
            def->output_kind = pack_kind_for_type(value);
            def->output_is_string = type_is_string(value);
        }
        else if( strcmp(line, "default") == 0 )
        {
            const char* expanded = enum_text(value);

            if( !expanded )
            {
                CONTENT_ERROR("%s:%d: `%s` does not resolve\n", path, line_number, value);
                continue;
            }
            if( def->output_is_string )
                def->default_text = strdup(expanded);
            else
                def->default_int = atoi(expanded);
        }
        else if( strcmp(line, "val") == 0 )
        {
            char* comma = strchr(value, ',');
            int key_ok = 0;
            int value_ok = 0;
            int key;
            int mapped = 0;
            const char* text = NULL;

            if( !comma )
            {
                CONTENT_ERROR("%s:%d: val needs `key,value`\n", path, line_number);
                continue;
            }
            *comma = '\0';
            key = enum_operand(def->input_kind, value, &key_ok);
            /*
             * A string-valued enum keeps the text. It cannot go through
             * enum_operand, which resolves or atoi()s — and atoi() of
             * "Nothing interesting happens." is 0, so every message in
             * displaymessage.enum would silently become the same one.
             *
             * Commas are not escaped in the reference's grammar, so the value is
             * everything after the *first* comma; a message containing one still
             * arrives whole.
             */
            if( def->output_is_string )
            {
                text = enum_text(comma + 1);
                value_ok = text != NULL;
                if( value_ok )
                    text = strdup(text);
            }
            else
            {
                mapped = enum_operand(def->output_kind, comma + 1, &value_ok);
            }
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
            def->values[def->count].text = text;
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
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            int varp_id = mock230_content_symbol(MOCK230_PACK_VARP, header);

            def = NULL;
            if( varp_id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.varp.compack\n", path, line_number,
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

        value = mock230_content_split_key_value(line);
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
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            int loc_id = mock230_content_symbol(MOCK230_PACK_LOC, header);

            def = NULL;
            if( loc_id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.loc.compack\n", path, line_number,
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

        value = mock230_content_split_key_value(line);
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
            {
                /* Which params a loc may carry is the field register's to say, not a
                 * name spelled twice. A row declared `client = param:<name>` is one
                 * the encoder knows how to bake, so it is one this parser accepts. */
                const struct ContentField* field = ContentFields_Find(&g_loc_fields, value);

                if( !field || field->client != CONTENT_CLIENT_PARAM )
                {
                    CONTENT_ERROR("%s:%d: `%s` is not a loc param this build bakes — "
                                  "declare it in fields/loc.ini as `client = param:<name>`\n",
                                  path, line_number, value);
                    continue;
                }
            }
            g_pending = grow(g_pending, &g_pending_capacity, g_pending_count,
                             sizeof(*g_pending));
            g_pending[g_pending_count].def_index = def_index;
            g_pending[g_pending_count].symbol = strdup(comma + 1);
            g_pending_count++;
        }
        else
        {
            /* Same rule as the npc ladder above: the register decides what a key the
             * parser does not handle means. */
            const struct ContentField* field = ContentFields_Find(&g_loc_fields, line);

            if( field && field->scope == CONTENT_SCOPE_CLIENT )
                g_client_key_overlays++;
            else if( field )
                CONTENT_ERROR("%s:%d: `%s` is declared in fields/loc.ini but this build "
                              "has nowhere to put it\n",
                              path, line_number, line);
            else
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
            CONTENT_ERROR("next_loc_stage `%s` is not in configs/all.loc.compack\n",
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
    else if( strcmp(key, "varbit") == 0 )
        def->varbit = mock230_content_symbol(MOCK230_PACK_VARBIT, value);
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
    def->varbit = -1;
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
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            def = prayer_begin(header);
            if( !def )
                CONTENT_ERROR("%s:%d: more than %d prayers; `prayer_active` is a "
                              "32-bit mask\n",
                              path, line_number, MOCK230_PRAYER_MAX);
            continue;
        }

        value = mock230_content_split_key_value(line);
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
        else if( strcmp(line, "varbit") == 0 && def->varbit < 0 )
            CONTENT_ERROR("%s:%d: `%s` is not in configs/all.varbit.compack\n", path,
                          line_number, value);
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
        char* line = mock230_content_clean_line(raw);
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
        /*
         * Spawns used to live here and no longer do — see
         * `server/scripts/areas/lumbridge/configs/lumbridge.spawn` for the whole
         * argument. What is left is the refusal, and it has to be loud.
         *
         * If this simply stopped reading them, a tree that had not migrated
         * would load with **zero spawns and no message**: an empty world that
         * looks like a scene bug rather than a content one. So an old-format
         * section is an error naming the file and the fix, which fails
         * `mock230_pack` instead of booting an empty Lumbridge.
         */
        if( section == JM2_NPC || section == JM2_OBJ )
        {
            CONTENT_ERROR("%s:%d: `%s` is a spawn, and spawns are server content — "
                          "move this square's ==== NPC ==== / ==== OBJ ==== sections "
                          "into a .spawn file under server/scripts/\n",
                          path, line_number, line);
            continue;
        }
        (void)map_x;
        (void)map_z;
        (void)level;
        (void)local_x;
        (void)local_z;
        (void)id;
        (void)count;
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .spawn configs                                                      */
/* ------------------------------------------------------------------ */

/*
 * Where the world's npcs and ground objs stand.
 *
 *     ==== NPC ====
 *     cook                         3208 3213 0
 *
 *     ==== OBJ ====
 *     bones                        3210 3215 0 28
 *
 * Names rather than ids, and absolute world tiles rather than a square-local
 * pair, which is the whole reason this is not a `.jm2` section any more. The
 * `====` markers are kept from that format so the file reads the same way.
 *
 * A name that does not resolve is an error. That is not politeness: a spawn was
 * the last place in this tree carrying a bare id, so it was the last place a
 * cache bump could silently repoint at a different creature — and it already
 * had (`sos_pest_giantspider1`, level 50, standing in the Lumbridge Swamp).
 */
static void
load_spawn_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    enum Jm2Section section = JM2_NONE;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = mock230_content_clean_line(raw);
        char name[128];
        int x;
        int z;
        int level;
        int count;
        int id;
        int fields;

        line_number++;
        if( !*line )
            continue;

        if( strncmp(line, "====", 4) == 0 )
        {
            if( strstr(line, "NPC") )
                section = JM2_NPC;
            else if( strstr(line, "OBJ") )
                section = JM2_OBJ;
            else
            {
                CONTENT_ERROR("%s:%d: `%s` — a .spawn file has ==== NPC ==== and "
                              "==== OBJ ==== sections and nothing else\n",
                              path, line_number, line);
                section = JM2_NONE;
            }
            continue;
        }
        if( section == JM2_NONE )
        {
            CONTENT_ERROR("%s:%d: `%s` before any ==== NPC ==== / ==== OBJ ==== header\n",
                          path, line_number, line);
            continue;
        }

        count = 1;
        fields = sscanf(line, "%127s %d %d %d %d", name, &x, &z, &level, &count);
        if( fields < 4 )
        {
            CONTENT_ERROR("%s:%d: expected `<name> <x> <z> <level>%s`, got `%s`\n",
                          path, line_number,
                          section == JM2_OBJ ? " [count]" : "", line);
            continue;
        }
        if( section == JM2_NPC && fields > 4 )
        {
            CONTENT_ERROR("%s:%d: an npc spawn has no count\n", path, line_number);
            continue;
        }
        if( level < 0 || level > 3 )
        {
            CONTENT_ERROR("%s:%d: level %d is not 0..3\n", path, line_number, level);
            continue;
        }
        if( count < 1 )
        {
            CONTENT_ERROR("%s:%d: count %d is not at least 1\n", path, line_number, count);
            continue;
        }

        id = mock230_content_symbol(section == JM2_NPC ? MOCK230_PACK_NPC : MOCK230_PACK_OBJ,
                                    name);
        if( id < 0 )
        {
            CONTENT_ERROR("%s:%d: `%s` is not in configs/all.%s.compack\n", path, line_number,
                          name, section == JM2_NPC ? "npc" : "obj");
            continue;
        }

        if( section == JM2_NPC )
        {
            g_npc_spawns = grow(g_npc_spawns, &g_npc_spawn_capacity, g_npc_spawn_count,
                                sizeof(*g_npc_spawns));
            g_npc_spawns[g_npc_spawn_count].npc_id = id;
            g_npc_spawns[g_npc_spawn_count].x = x;
            g_npc_spawns[g_npc_spawn_count].z = z;
            g_npc_spawns[g_npc_spawn_count].level = level;
            g_npc_spawn_count++;
        }
        else
        {
            g_obj_spawns = grow(g_obj_spawns, &g_obj_spawn_capacity, g_obj_spawn_count,
                                sizeof(*g_obj_spawns));
            g_obj_spawns[g_obj_spawn_count].obj_id = id;
            g_obj_spawns[g_obj_spawn_count].count = count;
            g_obj_spawns[g_obj_spawn_count].x = x;
            g_obj_spawns[g_obj_spawn_count].z = z;
            g_obj_spawns[g_obj_spawn_count].level = level;
            g_obj_spawn_count++;
        }
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* Param types                                                         */
/* ------------------------------------------------------------------ */

/*
 * What each param id's value *is*, from `configs/all.param`.
 *
 * The reason this exists is `oc_param` and its three siblings, which the
 * opcode meta marks `runtime_typed`: the declared type of the param decides
 * whether the result lands on the VM's int stack or its string stack. Without
 * it the opcode cannot be implemented at all, only guessed at — which is why
 * `docs/osrs230_mockserver.md` §3.13d listed the whole family as blocked on
 * data rather than on effort.
 *
 * It also makes `configs/` stop being write-only. `CONTENT_ARCHITECTURE.md`
 * §3.5's complaint was that `configs/all.<type>` is 8.6 MB of text whose only
 * consumer is `cachepack pack`; this is the first runtime reader of any of it.
 *
 * The type is one character, the cache's own code. Only `s` is a string; every
 * other letter is some flavour of integer as far as the stacks are concerned
 * (`i` plain, `d` a coordinate, `o` an obj, `1` a boolean, and so on), and the
 * VM has one int stack for all of them.
 */
static char* g_param_types;
static int g_param_type_count;

char
mock230_content_param_type(int param_id)
{
    if( param_id < 0 || param_id >= g_param_type_count )
        return 0;
    return g_param_types[param_id];
}

static int
load_param_types(const char* content_dir)
{
    char path[1024];
    FILE* file;
    char raw[512];
    int current = -1;
    int loaded = 0;
    int highest = -1;

    snprintf(path, sizeof(path), "%s/configs/all.param", content_dir);
    file = fopen(path, "rb");
    if( !file )
    {
        /*
         * Not fatal, and deliberately so: a tree without the unpacked config
         * text still boots, and every `oc_param` call then reports an unknown
         * type rather than inventing one. Silence here would be the wrong
         * trade, so it says what it lost.
         */
        fprintf(stderr,
                "mock230: no configs/all.param — oc_param cannot type its result\n");
        return 0;
    }

    /* Indexed by param id directly, sized off the highest the pack names. The
     * pack is already loaded at this point — it is layer 0 of the `param`
     * namespace and comes off `configs/all.param.compack`. */
    {
        const struct Pack* pack = &g_packs[MOCK230_PACK_PARAM];

        for( int i = 0; i < pack->count; i++ )
            if( pack->entries[i].id > highest )
                highest = pack->entries[i].id;
    }
    if( highest < 0 )
    {
        fclose(file);
        return 0;
    }
    g_param_type_count = highest + 1;
    g_param_types = (char*)calloc((size_t)g_param_type_count, 1);
    if( !g_param_types )
    {
        g_param_type_count = 0;
        fclose(file);
        return 0;
    }

    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        if( !*line )
            continue;
        header = mock230_content_section_header(line);
        if( header )
        {
            current = mock230_content_symbol(MOCK230_PACK_PARAM, header);
            continue;
        }
        if( current < 0 || current >= g_param_type_count )
            continue;
        value = mock230_content_split_key_value(line);
        if( !value || strcmp(line, "type") != 0 )
            continue;
        g_param_types[current] = value[0];
        loaded++;
    }
    fclose(file);
    return loaded;
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
    struct ContentRegister reg;
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
     * The register, then one file per namespace.
     *
     * `ContentRegister_Load` overlays the tree's `content.ini` onto the built-in
     * defaults and never fails, so a tree without one still boots. What *does*
     * refuse to boot is a tree whose declaration contradicts itself — a namespace
     * claiming `names = cache` with no gameval archive behind it tells cachepack it
     * may rewrite a hand-written file, and that has already cost this tree
     * `configs/all.param.compack`'s 58-line header once.
     */
    ContentRegister_Load(&reg, dir);
    ContentFields_Load(&g_npc_fields, dir, "npc");
    ContentFields_Load(&g_loc_fields, dir, "loc");
    g_client_key_overlays = 0;
    if( ContentRegister_Validate(&reg) != 0 )
        CONTENT_ERROR("content.ini contradicts the gameval evidence; see above\n");

    for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
    {
        /*
         * Two levels of index, two places.
         *
         * A config record is a *file* of a config archive — `[swarm_walk]` is file 0
         * of archive 12 — so what binds `0=swarm_walk` is a member index and lives
         * beside the archive it indexes, as `configs/all.seq.compack`. `pack/` holds
         * the other level: one file per cache index, naming that index's archives.
         *
         * Both are `id=name` and used to share a name and a directory, which is why
         * the distinction had to be known rather than read.
         */
        const char* name = pack_kind_name((enum Mock230PackKind)kind);

        if( kind == MOCK230_PACK_COMPONENT )
            continue; /* composed below, from the interfaces and their compacks */
        if( pack_kind_is_config((enum Mock230PackKind)kind) )
            snprintf(path, sizeof(path), "%s/configs/all.%s.compack", dir, name);
        else
            snprintf(path, sizeof(path), "%s/pack/%s.pack", dir, name);
        symbols += pack_load(&g_packs[kind], path);
    }
    /* After the loop: it needs the interface pack to already be loaded. */
    symbols += load_component_symbols(dir);

    /* A symbol table that answers a name two ways is refused here rather than
     * resolved silently later. */
    validate_symbols(&reg);

    /* After the packs (a default names its animations by symbol) and before the
     * configs (each block starts from a copy of it). */
    init_defaults();

    /* Before the configs: nothing under server/ reads a param type yet, but the
     * ordering is the one that stays right when something does. */
    {
        int param_types = load_param_types(dir);

        if( param_types )
            fprintf(stderr, "mock230: %d param types from configs/all.param\n", param_types);
    }

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
    /* After the configs: a spawn names an npc or an obj, and the name has to
     * resolve against the packs the loader has already read. */
    walk_configs(path, ".spawn", load_spawn_config);
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
        if( g_client_key_overlays )
            fprintf(stderr,
                    "mock230: %d config line(s) restate a field the client's own record "
                    "carries — the cache already says it, so the overlay is inert\n",
                    g_client_key_overlays);
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
    free(g_param_types);
    g_param_types = NULL;
    g_param_type_count = 0;

    free(g_npc_spawns);
    g_npc_spawns = NULL;
    g_npc_spawn_count = g_npc_spawn_capacity = 0;
    free(g_obj_spawns);
    g_obj_spawns = NULL;
    g_obj_spawn_count = g_obj_spawn_capacity = 0;
    g_errors = 0;
}
