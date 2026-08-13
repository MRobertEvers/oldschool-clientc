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
#include "mock230_servercodec.h"
#include "mock230_servpack.h"

#include <rscache.h>

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* dirent's d_type is a BSD/Linux extension MinGW's dirent lacks, so classify by
 * path with stat() instead -- portable across the unix and win32 builds. */
static int
mock230_path_is_dir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

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
    struct PackEntry** by_name;
    struct PackEntry** by_id;
    int count;
    int capacity;
};

static struct Pack g_packs[MOCK230_PACK_COUNT];

static int
pack_entry_name_compare(
    const void* lhs,
    const void* rhs)
{
    const struct PackEntry* left = *(const struct PackEntry* const*)lhs;
    const struct PackEntry* right = *(const struct PackEntry* const*)rhs;
    int order = strcmp(left->name, right->name);

    if( order != 0 )
        return order;
    if( left->id != right->id )
        return left->id < right->id ? -1 : 1;
    return left < right ? -1 : left > right;
}

static int
pack_entry_id_compare(
    const void* lhs,
    const void* rhs)
{
    const struct PackEntry* left = *(const struct PackEntry* const*)lhs;
    const struct PackEntry* right = *(const struct PackEntry* const*)rhs;

    if( left->id != right->id )
        return left->id < right->id ? -1 : 1;
    return left < right ? -1 : left > right;
}

static void
pack_build_indexes(struct Pack* pack)
{
    if( pack->count <= 0 )
        return;
    pack->by_name = malloc((size_t)pack->count * sizeof(*pack->by_name));
    pack->by_id = malloc((size_t)pack->count * sizeof(*pack->by_id));
    if( !pack->by_name || !pack->by_id )
    {
        free(pack->by_name);
        free(pack->by_id);
        pack->by_name = NULL;
        pack->by_id = NULL;
        return;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        pack->by_name[i] = &pack->entries[i];
        pack->by_id[i] = &pack->entries[i];
    }
    qsort(pack->by_name,
          (size_t)pack->count,
          sizeof(*pack->by_name),
          pack_entry_name_compare);
    qsort(pack->by_id, (size_t)pack->count, sizeof(*pack->by_id), pack_entry_id_compare);
}

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
load_component_symbols_from_root(
    const char* dir,
    int additions_only)
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
            int uid;
            const char* existing;

            if( children.entries[c].id < 0 || children.entries[c].id > 0xffff )
                continue;
            snprintf(full, sizeof(full), "%s:%s", iface_name, children.entries[c].name);
            uid = (iface_id << 16) | children.entries[c].id;
            existing = mock230_content_symbol_name(MOCK230_PACK_COMPONENT, uid);
            if( additions_only && existing )
            {
                if( strcmp(existing, full) != 0 )
                    CONTENT_ERROR("%s: component %d:%d is both `%s` and `%s`\n",
                                  path, iface_id, children.entries[c].id, existing, full);
                continue;
            }
            pack_add(&g_packs[MOCK230_PACK_COMPONENT], full, uid);
            loaded++;
        }
        for( int c = 0; c < children.count; c++ )
            free(children.entries[c].name);
        free(children.entries);
    }
    return loaded;
}

/*
 * Marked client-content lanes may extend an existing interface without making
 * that extension part of a flag-off cache bake.  Their RuneScript compiler is
 * given the lane as a --component-root; the runtime needs the same name->uid
 * view so it can find name-addressed component triggers.  Component uids above
 * the script lookup-key's 21-bit subject range (including every stats child)
 * cannot be recovered from the numeric trigger index alone.
 *
 * Loading these names does not mount an interface or expose a cache record.  It
 * only lets an already-selected server script pack resolve a packet uid back to
 * the spelling under which that pack was compiled.  Existing children must be
 * restated identically; a lane can add a child, but cannot silently rename one.
 */
static int
load_ported_component_symbols(const char* dir)
{
    DIR* handle;
    struct dirent* entry;
    char ported[1024];
    char lane[1024];
    int loaded = 0;

    snprintf(ported, sizeof(ported), "%s/ported", dir);
    handle = opendir(ported);
    if( !handle )
        return 0;
    while( (entry = readdir(handle)) != NULL )
    {
        char overlays[1152];

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(lane, sizeof(lane), "%s/%s", ported, entry->d_name);
        if( mock230_path_is_dir(lane) )
        {
            loaded += load_component_symbols_from_root(lane, 1);
            /* Existing-interface additions live here so the feature-off lane
             * never replaces the base archive. The script compiler receives
             * this as an explicit component root; give runtime packet dispatch
             * the same name-to-uid view. */
            snprintf(overlays, sizeof(overlays), "%s/interface_overlays", lane);
            loaded += load_component_symbols_from_root(overlays, 1);
        }
    }
    closedir(handle);
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
    if( pack->by_name )
    {
        int low = 0;
        int high = pack->count;

        while( low < high )
        {
            int mid = low + (high - low) / 2;

            if( strcmp(pack->by_name[mid]->name, name) < 0 )
                low = mid + 1;
            else
                high = mid;
        }
        if( low < pack->count && strcmp(pack->by_name[low]->name, name) == 0 )
            return pack->by_name[low]->id;
        return -1;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        if( strcmp(pack->entries[i].name, name) == 0 )
            return pack->entries[i].id;
    }
    return -1;
}

int
mock230_content_symbol_checked(
    enum Mock230PackKind kind,
    const char* name,
    int* out_id)
{
    *out_id = -1;
    if( !name || !*name )
        return 0;
    /* The one spelling of -1 that is an answer rather than a miss. Kept here and
     * not in the caller so "how LostCity writes nothing" is stated once. */
    if( strcmp(name, "null") == 0 )
        return 1;
    *out_id = mock230_content_symbol(kind, name);
    return *out_id >= 0;
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
    if( pack->by_id )
    {
        int low = 0;
        int high = pack->count;

        while( low < high )
        {
            int mid = low + (high - low) / 2;

            if( pack->by_id[mid]->id < symbol_id )
                low = mid + 1;
            else
                high = mid;
        }
        if( low < pack->count && pack->by_id[low]->id == symbol_id )
            return pack->by_id[low]->name;
        return NULL;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].id == symbol_id )
            return pack->entries[i].name;
    }
    return NULL;
}

int
mock230_content_symbol_walk(
    enum Mock230PackKind kind,
    int index,
    int* out_id,
    const char** out_name)
{
    const struct Pack* pack;

    if( kind < 0 || kind >= MOCK230_PACK_COUNT )
        return 0;
    pack = &g_packs[kind];
    if( index < 0 || index >= pack->count )
        return pack->count;
    if( out_id )
        *out_id = pack->entries[index].id;
    if( out_name )
        *out_name = pack->entries[index].name;
    return pack->count;
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
        [MOCK230_PACK_HEALTHBAR] = "healthbar",
        /* Cache index 4's archive index, same shape as 3_interfaces above. */
        [MOCK230_PACK_SYNTH] = "4_soundeffects",
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

/*
 * Imported client lanes own minted ids outside the rank-0 cache's member
 * compacks.  The ServerScript compiler layers each lane's pack directory over
 * the ordinary symbols; the runtime must resolve server overlay headers through
 * the same view (`[rs2012_tormented_demon_melee]`, for example), or the script
 * compiles while its npc definition silently fails to load.
 *
 * Keep this generic over marked directories below `ported/`: Summoning and the
 * RS2012 QBD/TD lane use the same `.alloc` contract, and future lanes should not
 * require another hard-coded root in C.  `validate_symbols` below still rejects
 * either a duplicate id with a different name or a duplicate name with a
 * different id after all layers have been read.
 */
static int
load_ported_pack_symbols(const char* dir)
{
    DIR* handle;
    struct dirent* entry;
    char ported[1024];
    char lane[1024];
    char path[1280];
    int loaded = 0;

    snprintf(ported, sizeof(ported), "%s/ported", dir);
    handle = opendir(ported);
    if( !handle )
        return 0;
    while( (entry = readdir(handle)) != NULL )
    {
        if( entry->d_name[0] == '.' )
            continue;
        snprintf(lane, sizeof(lane), "%s/%s", ported, entry->d_name);
        if( !mock230_path_is_dir(lane) )
            continue;
        for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
        {
            const char* name = pack_kind_name((enum Mock230PackKind)kind);

            if( kind == MOCK230_PACK_COMPONENT )
                continue; /* composed from lane interface compacks below */
            snprintf(path, sizeof(path), "%s/pack/%s.pack", lane, name);
            loaded += pack_load(&g_packs[kind], path);
            snprintf(path, sizeof(path), "%s/pack/%s.alloc", lane, name);
            loaded += pack_load(&g_packs[kind], path);
        }
    }
    closedir(handle);
    return loaded;
}

/**
 * 1 when this kind's records are files of a config archive, so its index is a
 * `.compack` beside `configs/all.<type>` rather than a pack in `pack/`.
 *
 * The five that are not: interfaces and sound effects are whole cache archives
 * (index 3 and 4) and get an archive-level pack; components are named across
 * every interface archive at once; `stat` is fixed by the wire; `category` is an
 * obj field the cache numbers and nothing names.
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
    /* Sound effects are cache index 4 and get an archive-level pack, the same
     * shape as interfaces above: there is no `configs/all.synth`, because a
     * sound effect is a whole archive rather than a file inside one. */
    case MOCK230_PACK_SYNTH:
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

int
mock230_content_npc_param(
    const struct Mock230NpcDef* def,
    int param_id,
    int32_t* out)
{
    if( !def )
        return 0;
    for( int i = 0; i < def->param_count; i++ )
    {
        if( def->params[i].key != param_id )
            continue;
        if( out )
            *out = def->params[i].value;
        return 1;
    }
    return 0;
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

const struct Mock230LocDef*
mock230_content_loc_at(int index)
{
    if( index < 0 || index >= g_loc_def_count )
        return NULL;
    return &g_loc_defs[index];
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

/**
 * One symbolic param value, resolved or refused.
 *
 * Split out so the four call sites cannot each forget the check independently,
 * which is exactly how they came to share a bug.
 */
static int
param_symbol(
    int* out,
    enum Mock230PackKind kind,
    const char* param_name,
    const char* value,
    const char* where)
{
    int id;

    if( !mock230_content_symbol_checked(kind, value, &id) )
    {
        CONTENT_ERROR("%s: param `%s` names `%s`, which is not in %s — write `null` for "
                      "\"nothing\"\n",
                      where, param_name, value, pack_kind_name(kind));
        return 0;
    }
    *out = id;
    return 1;
}

/*
 * Also file the param under its *id*, so `npc_param` can find it.
 *
 * Every branch below writes a named C field, which is what the engine reads.
 * That is fine for the engine and useless to a script: `npc_param` is handed a
 * param *id* and has no way back to a field name. Until this list existed the
 * only opcode-visible authored param was `death_drop`, and it was visible
 * because `mock230_scripts.c` compared the popped id against
 * `mock230_content_symbol(MOCK230_PACK_PARAM, "death_drop")` — one game-facing
 * name in C, answering one param, pushing 0 for the rest.
 *
 * These rows are rank 1 in the sense CONTENT_ARCHITECTURE.md §3.1 uses: an
 * authored overlay value that overrides the cache record's own param table.
 * `npc_param` reads them first and the cache row second, which is the same
 * precedence `npc_def_seed_from_cache` already gives the bonuses.
 *
 * A name the param pack does not know is filed under no id and still writes its
 * field — `attack_anim`, `defend_anim` and the rest are engine spellings that
 * predate the pack, and refusing them here would break loading rather than
 * teach anyone anything.
 */
static void
record_authored_param(
    struct Mock230NpcDef* def,
    const char* name,
    int32_t resolved,
    const char* where)
{
    int param_id = mock230_content_symbol(MOCK230_PACK_PARAM, name);

    if( param_id < 0 )
        return;
    for( int i = 0; i < def->param_count; i++ )
    {
        if( def->params[i].key == param_id )
        {
            def->params[i].value = resolved;
            return;
        }
    }
    if( def->param_count >= MOCK230_NPCDEF_PARAM_MAX )
    {
        CONTENT_ERROR("%s: more than %d authored params on one npc\n", where,
                      MOCK230_NPCDEF_PARAM_MAX);
        return;
    }
    def->params[def->param_count].key = param_id;
    def->params[def->param_count].value = resolved;
    def->param_count++;
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
    int32_t resolved;

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
            record_authored_param(def, text, def->bonus[i], where);
            return 1;
        }
    }
    if( strcmp(text, "attackrate") == 0 )
        resolved = def->attackrate = atoi(value);
    else if( strcmp(text, "attackrange") == 0 )
        resolved = def->attackrange = atoi(value);
    /* NPC source profiles also carry the two offensive scalars that are not
     * among the legacy twelve bonus slots.  They are script-visible params,
     * not engine fields: ranged and magic swing code reads them through
     * npc_param, and keeping them here preserves the normal overlay precedence
     * over cache defaults. */
    else if( strcmp(text, "rangebonus") == 0 || strcmp(text, "magicdamage") == 0 ||
             strcmp(text, "magic_maxhit") == 0 )
        resolved = atoi(value);
    else if( strcmp(text, "damagetype") == 0 )
        resolved = def->damagetype = atoi(value);
    else if( strcmp(text, "huntrange") == 0 )
        resolved = def->huntrange = atoi(value);
    else if( strcmp(text, "undead") == 0 )
    {
        /* Crumble Undead gate — combat.param int; overlays use 0/1 or ^true/^false. */
        if( value[0] == '^' )
            resolved = mock230_content_constant_int(value, 0);
        else
            resolved = atoi(value);
    }
    /*
     * The four symbolic params, and the one place the two halves of this merge
     * had to be combined rather than chosen between.
     *
     * The gate: every one of these used to take `mock230_content_symbol`'s -1
     * for an answer, so a misspelled seq or obj name loaded silently and the npc
     * simply had no anim and dropped nothing — `param=death_drop,bones_TYPO` at
     * 0 errors. `null` still means "nothing" (see
     * `mock230_content_symbol_checked`); a name is now required to exist.
     *
     * And the id: the branch cannot `return` on success the way the gate first
     * wrote it, because `record_authored_param` below is what files the value
     * under its param *id* so `npc_param` can find it. Returning early here
     * would leave the four symbolic params the only ones a script could not
     * read — which is the bug that motivated the id list in the first place.
     */
    else if( strcmp(text, "attack_anim") == 0 || strcmp(text, "slashattack_anim") == 0 )
    {
        if( !param_symbol(&def->attack_anim, MOCK230_PACK_SEQ, text, value, where) )
            return 0;
        resolved = def->attack_anim;
    }
    else if( strcmp(text, "defend_anim") == 0 )
    {
        if( !param_symbol(&def->defend_anim, MOCK230_PACK_SEQ, text, value, where) )
            return 0;
        resolved = def->defend_anim;
    }
    else if( strcmp(text, "death_anim") == 0 )
    {
        if( !param_symbol(&def->death_anim, MOCK230_PACK_SEQ, text, value, where) )
            return 0;
        resolved = def->death_anim;
    }
    else if( strcmp(text, "death_drop") == 0 )
    {
        if( !param_symbol(&def->death_drop, MOCK230_PACK_OBJ, text, value, where) )
            return 0;
        resolved = def->death_drop;
    }
    /*
     * The three combat sounds, and the one place in this function that takes a
     * bare integer where its neighbours take a name.
     *
     * Not an inconsistency to tidy: `param_symbol` resolves through a pack
     * namespace and there is no `synth` param *type* for cachepack to declare
     * (`cp_common.c`'s `k_param_types` — the letter a sound would want, 'P', is
     * already `param`). So a `.npc` block cannot spell a sound by name at all.
     * `tools/gen_npc_combat.py` does the name resolution, where it can be
     * checked against `pack/4_soundeffects.pack`, and keeps the name in
     * `npc_combat/<npc>.combat` beside the id. The number that arrives here has
     * already been through a name.
     */
    else if( strcmp(text, "attack_sound") == 0 )
    {
        def->attack_sound = atoi(value);
        resolved = def->attack_sound;
    }
    else if( strcmp(text, "defend_sound") == 0 )
    {
        def->defend_sound = atoi(value);
        resolved = def->defend_sound;
    }
    else if( strcmp(text, "death_sound") == 0 )
    {
        def->death_sound = atoi(value);
        resolved = def->death_sound;
    }
    else
    {
        CONTENT_ERROR("%s: unknown param `%s`\n", where, text);
        return 0;
    }
    record_authored_param(def, text, resolved, where);
    return 1;
}

/*
 * `<level>_<mapx>_<mapz>_<localx>_<localz>` — the reference's coordinate
 * literal, and the form every `patrol<N>` waypoint is written in.
 *
 * A map square is 64 tiles, so the absolute tile is `mapx * 64 + localx`. The
 * five fields are all required: a four-field spelling is a typo that would
 * otherwise place the waypoint at a plausible-looking wrong tile, which for a
 * patrol reads as the npc walking somewhere odd rather than as a bad config.
 */
static int
parse_coord_literal(
    const char* text,
    int* out_level,
    int* out_x,
    int* out_z)
{
    int level;
    int map_x;
    int map_z;
    int local_x;
    int local_z;

    if( sscanf(text, "%d_%d_%d_%d_%d", &level, &map_x, &map_z, &local_x, &local_z) != 5 )
        return 0;
    if( local_x < 0 || local_x > 63 || local_z < 0 || local_z > 63 )
        return 0;
    *out_level = level;
    *out_x = map_x * 64 + local_x;
    *out_z = map_z * 64 + local_z;
    return 1;
}

/*
 * `patrol<N>=<coord>,<pause>`.
 *
 * N is 1-based and the reference writes them in order, but nothing enforces
 * that — so the index in the key is what decides the slot rather than the order
 * the lines happen to be read in. A route with a gap in it (patrol1, patrol3)
 * would otherwise silently compact into a shorter, different route.
 */
static int
apply_patrol(
    struct Mock230NpcDef* def,
    int index,
    char* text,
    const char* where)
{
    char* comma = strchr(text, ',');
    int level;
    int x;
    int z;

    if( index < 1 || index > MOCK230_NPC_PATROL_MAX )
    {
        CONTENT_ERROR("%s: patrol%d is outside 1..%d\n", where, index, MOCK230_NPC_PATROL_MAX);
        return 0;
    }
    if( comma )
        *comma = '\0';
    if( !parse_coord_literal(text, &level, &x, &z) )
    {
        CONTENT_ERROR("%s: patrol%d wants `<level>_<mapx>_<mapz>_<localx>_<localz>,<pause>`, "
                      "got `%s`\n",
                      where, index, text);
        return 0;
    }
    def->patrol[index - 1].level = level;
    def->patrol[index - 1].x = x;
    def->patrol[index - 1].z = z;
    def->patrol[index - 1].pause = comma ? atoi(comma + 1) : 0;
    if( index > def->patrol_count )
        def->patrol_count = index;
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
    else if( strcmp(key, "death_delay") == 0 )
        def->death_delay = atoi(value);
    else if( strcmp(key, "wanderrange") == 0 )
        def->wanderrange = atoi(value);
    else if( strcmp(key, "turnspeed") == 0 )
        def->turnspeed = atoi(value);
    else if( strcmp(key, "maxrange") == 0 )
        def->maxrange = atoi(value);
    /* `givechase=no` is the only value the reference writes — its packer emits
     * the opcode for the negative case and nothing for the positive — so
     * anything else means yes. */
    else if( strcmp(key, "givechase") == 0 )
        def->givechase = strcmp(value, "no") != 0;
    /* Same shape as givechase above, and for the same reason: the negative is
     * the only case worth stating, so anything but `no` means yes. */
    else if( strcmp(key, "retaliate") == 0 )
        def->retaliate = strcmp(value, "no") != 0;
    else if( strcmp(key, "blockwalk") == 0 )
    {
        /* LostCity BlockWalk: none / npc / all / player (NpcConfig also accepts
         * `NPC`). Numeric literals are allowed so a band dump round-trips. */
        if( strcmp(value, "none") == 0 || strcmp(value, "0") == 0 )
            def->blockwalk = 0;
        else if( strcmp(value, "npc") == 0 || strcmp(value, "NPC") == 0 ||
                 strcmp(value, "1") == 0 )
            def->blockwalk = 1;
        else if( strcmp(value, "all") == 0 || strcmp(value, "2") == 0 )
            def->blockwalk = 2;
        else if( strcmp(value, "player") == 0 || strcmp(value, "3") == 0 )
            def->blockwalk = 3;
        else
            CONTENT_ERROR("%s: blockwalk `%s` is not none/npc/all/player\n", where, value);
    }
    else if( strcmp(key, "blocksight") == 0 )
    {
        if( strcmp(value, "yes") == 0 || strcmp(value, "true") == 0 ||
            strcmp(value, "1") == 0 )
            def->blocksight = 1;
        else if( strcmp(value, "no") == 0 || strcmp(value, "false") == 0 ||
                 strcmp(value, "0") == 0 )
            def->blocksight = 0;
        else
            CONTENT_ERROR("%s: blocksight `%s` is not 0/1\n", where, value);
    }
    else if( strcmp(key, "moverestrict") == 0 )
    {
        /* LostCity MoveRestrict. Only nomove is enforced by the mover; the rest
         * are stored so content can state them. Always keep `nomove` in sync. */
        if( strcmp(value, "normal") == 0 )
            def->moverestrict = 0;
        else if( strcmp(value, "blocked") == 0 )
            def->moverestrict = 1;
        else if( strcmp(value, "blocked+normal") == 0 || strcmp(value, "los") == 0 )
            def->moverestrict = 2;
        else if( strcmp(value, "indoors") == 0 )
            def->moverestrict = 3;
        else if( strcmp(value, "outdoors") == 0 )
            def->moverestrict = 4;
        else if( strcmp(value, "nomove") == 0 )
            def->moverestrict = 5;
        else if( strcmp(value, "passthru") == 0 )
            def->moverestrict = 6;
        else
            CONTENT_ERROR("%s: moverestrict `%s` is not a known MoveRestrict\n", where,
                          value);
        def->nomove = def->moverestrict == 5;
    }
    else if( strcmp(key, "huntmode") == 0 )
        def->huntmode = strcmp(value, "aggressive") == 0 ? MOCK230_HUNT_AGGRESSIVE
                                                         : MOCK230_HUNT_NONE;
    else if( strcmp(key, "param") == 0 )
        (void)apply_param(def, value, where);
    else if( strcmp(key, "defaultmode") == 0 )
    {
        /*
         * Only `patrol` and `none`/`wander` are accepted, and an unknown mode is
         * an error rather than a fallback. A `defaultmode` the engine silently
         * ignored would give an npc the *wander* behaviour under a config line
         * saying it does something else — which is a config that reads correct
         * and behaves wrong, the worst outcome available here.
         */
        if( strcmp(value, "patrol") == 0 )
            def->defaultmode = MOCK230_NPCMODE_PATROL;
        else if( strcmp(value, "none") == 0 || strcmp(value, "null") == 0 )
            def->defaultmode = MOCK230_NPCMODE_NONE;
        else if( strcmp(value, "wander") == 0 )
            def->defaultmode = MOCK230_NPCMODE_WANDER;
        else
            CONTENT_ERROR("%s: defaultmode `%s` is not implemented\n", where, value);
    }
    else if( strncmp(key, "patrol", 6) == 0 && key[6] >= '0' && key[6] <= '9' )
        (void)apply_patrol(def, atoi(key + 6), value, where);
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

/*
 * `.npc` files are read twice, and the two passes are not an optimisation.
 *
 * `[default]` states what every npc block starts from, and `npc_def_seed_from_cache`
 * copies it at the moment a block's header is read. So a `[default]` in a file
 * the directory walk reaches *after* the roster would apply to nothing — and
 * `areas/` sorts before `general/`, which is precisely the order that happens.
 * Nothing would report it: every npc would simply carry the built-in numbers.
 *
 * One pass for `[default]` and one for everything else makes the answer
 * independent of where an author puts the file.
 */
static int g_npc_default_pass;

static void load_npc_config(const char* path);

static void
load_npc_default_config(const char* path)
{
    g_npc_default_pass = 1;
    load_npc_config(path);
    g_npc_default_pass = 0;
}

static void
load_npc_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230NpcDef* def = NULL;
    int line_number = 0;
    int skipping = 0;
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
            int npc_id;

            /*
             * `[default]` edits what every npc starts from.
             *
             * The alternative was a table of ids in C (it was one, until this),
             * and the alternative to *that* was the engine deriving an
             * animation name from the npc's display name — `"Goblin"` ->
             * `goblin_attack` — which guessed right often enough to look like a
             * feature and silently produced -1 for every npc whose sequences
             * the cache spells differently ("Man" has no `man_attack`).
             *
             * `default` is not an npc name in any gameval table, so it cannot
             * collide with a real block; the symbol lookup below would reject
             * it, which is why this branch comes first.
             */
            if( strcmp(header, "default") == 0 )
            {
                def = &g_npc_default;
                skipping = !g_npc_default_pass;
                continue;
            }

            /* Every other block belongs to the second pass. `skipping` rather
             * than a null def, so its keys are passed over in silence instead of
             * each reporting "before any [section]". */
            skipping = g_npc_default_pass;
            if( skipping )
            {
                def = NULL;
                continue;
            }

            npc_id = mock230_content_symbol(MOCK230_PACK_NPC, header);

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

        if( skipping )
            continue;

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
 * Equipment requirements and authored obj params (BAS, attack anims, …).
 *
 * The same overlay contract the `.npc` / `.loc` grammars have: a block starts
 * from what `cache.osrs239` already says and states only what a cache cannot.
 * Combat bonuses and attack rate are already in the record — restating them
 * here is how a config comes to disagree with the client.
 *
 * Two `param=` shapes:
 *
 *     param=levelrequire,attack,20          # repeatable (stat, level) pair
 *     param=ready_baseanim,human_staffready # ordinary typed param → oc_param
 *
 * `levelrequire` stays special: a param maps one id to one scalar, and this is
 * a repeating pair (fields/obj.ini). Everything else goes through
 * `mock230_objinfo_param_overlay` the way `.loc` uses
 * `mock230_locinfo_param_overlay`.
 */
static int
obj_resolve_param_value(
    int param_id,
    const char* value,
    int* out,
    const char* where)
{
    char declared = mock230_content_param_type(param_id);

    if( strcmp(value, "null") == 0 )
    {
        *out = -1;
        return 1;
    }
    /* Server overlays declare type=seq/obj/…; cache all.param uses single
     * letters. Either way an int-shaped value may be a symbol or a number. */
    if( declared == 's' )
    {
        CONTENT_ERROR("%s: string obj params are not overlaid yet (got `%s`)\n", where, value);
        return 0;
    }
    if( (value[0] >= '0' && value[0] <= '9') || (value[0] == '-' && value[1] >= '0') )
    {
        *out = atoi(value);
        return 1;
    }
    /* Symbolic: try seq first (anims / baseanim), then obj / spotanim, then
     * synth. Synth is last because its names are `synth_<id>` and carry no
     * information — a lookup there can only succeed on a name shaped for it, so
     * it can never shadow one of the others. */
    if( mock230_content_symbol_checked(MOCK230_PACK_SEQ, value, out) )
        return 1;
    if( mock230_content_symbol_checked(MOCK230_PACK_OBJ, value, out) )
        return 1;
    if( mock230_content_symbol_checked(MOCK230_PACK_SPOTANIM, value, out) )
        return 1;
    if( mock230_content_symbol_checked(MOCK230_PACK_SYNTH, value, out) )
        return 1;
    CONTENT_ERROR("%s: cannot resolve param value `%s`\n", where, value);
    return 0;
}

static void
obj_config_key(
    int obj_id,
    const char* key,
    const char* value,
    int* stats,
    int* levels,
    int* count,
    const char* where)
{
    char param_name[64] = { 0 };
    char skill_name[64] = { 0 };
    char value_name[128] = { 0 };
    int level = 0;
    int stat;
    int param_id;
    int resolved;

    if( strcmp(key, "param") != 0 )
    {
        /* Every other LostCity obj key states something the cache already does.
         * Accepted and ignored, like the `.npc` grammar's `model=` — but never
         * silently, so a paste from a LostCity tree reports what it dropped. */
        CONTENT_ERROR("%s: obj key `%s` is the cache's to state, ignored\n", where, key);
        return;
    }

    /* Ordinary `param=<name>,<value>` (two fields). */
    if( sscanf(value, "%63[^,],%127s", param_name, value_name) == 2 &&
        strchr(value_name, ',') == NULL )
    {
        if( strcmp(param_name, "levelrequire") == 0 )
        {
            CONTENT_ERROR("%s: `param=levelrequire,<skill>,<level>` needs three fields\n",
                          where);
            return;
        }
        if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, param_name, &param_id) )
        {
            CONTENT_ERROR("%s: obj param `%s` is not in configs/all.param.compack\n", where,
                          param_name);
            return;
        }
        if( !obj_resolve_param_value(param_id, value_name, &resolved, where) )
            return;
        mock230_objinfo_param_overlay(obj_id, param_id, resolved);
        return;
    }

    /* `param=levelrequire,<skill>,<level>` (three fields). */
    if( sscanf(value, "%63[^,],%63[^,],%d", param_name, skill_name, &level) != 3 )
    {
        CONTENT_ERROR("%s: `param=<name>,<value>` or `param=levelrequire,<skill>,<level>`, "
                      "got `%s`\n",
                      where, value);
        return;
    }
    if( strcmp(param_name, "levelrequire") != 0 )
    {
        CONTENT_ERROR("%s: three-field obj param must be levelrequire, got `%s`\n", where,
                      param_name);
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
        obj_config_key(obj_id, line, value, stats, levels, &count, where);
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
        { "4_soundeffects", MOCK230_PACK_SYNTH },
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
            if( def->count >= def->capacity )
            {
                int next = def->capacity ? def->capacity * 2 : 32;
                struct Mock230EnumValue* grown =
                    realloc(def->values, (size_t)next * sizeof(*grown));

                if( !grown )
                {
                    CONTENT_ERROR("%s:%d: out of memory growing enum values\n", path,
                                  line_number);
                    continue;
                }
                def->values = grown;
                def->capacity = next;
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

/*
 * Rank-0 enums — the cache export at `configs/all.enum`.
 *
 * ServerScript's `enum` / `enum_getoutputcount` used to answer only for the
 * handful of authored `.enum` files under `server/scripts`. Everything the
 * client's own CS2 reads — Combat Achievement tier lists, the collection-log
 * catalog, the popout strip's panel list (`enum_4067`) — lives here, and
 * content that needed a count hardcoded it. Loading this file is what retires
 * that limitation.
 *
 * The grammar is a subset of the authored one plus three keys the unpacker
 * emits for string enums (`outputstring`, `defaultstr`, `valstr`). Types are
 * not declared (`inputtype`/`outputtype` are absent); int enums are the
 * default and `outputstring=yes` flips the output to text. Values are raw
 * integers — no symbol resolution, because this file is a machine dump.
 */
static int
load_rank0_enums(const char* content_dir)
{
    char path[1024];
    FILE* file;
    char raw[8192];
    struct Mock230EnumDef* def = NULL;
    int line_number = 0;
    int loaded = 0;

    snprintf(path, sizeof(path), "%s/configs/all.enum", content_dir);
    file = fopen(path, "rb");
    if( !file )
    {
        fprintf(stderr,
                "mock230: no configs/all.enum — enum()/enum_getoutputcount cannot "
                "answer for cache tables\n");
        return 0;
    }

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
            /* Authored overlays loaded first win: skip a cache dump that
             * restates a name already present. */
            if( mock230_content_enum(header) )
            {
                def = NULL;
                continue;
            }
            g_enum_defs = grow(g_enum_defs, &g_enum_def_capacity, g_enum_def_count,
                               sizeof(*g_enum_defs));
            def = &g_enum_defs[g_enum_def_count++];
            memset(def, 0, sizeof(*def));
            def->symbol = strdup(header);
            def->input_kind = MOCK230_PACK_COUNT;
            def->output_kind = MOCK230_PACK_COUNT;
            def->default_int = 0;
            def->default_text = "null";
            loaded++;
            continue;
        }

        value = mock230_content_split_key_value(line);
        if( !value || !def )
            continue;

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
        else if( strcmp(line, "outputstring") == 0 )
        {
            def->output_is_string = strcmp(value, "yes") == 0;
        }
        else if( strcmp(line, "default") == 0 )
        {
            if( def->output_is_string )
                def->default_text = strdup(value);
            else
                def->default_int = atoi(value);
        }
        else if( strcmp(line, "defaultstr") == 0 )
        {
            def->output_is_string = 1;
            def->default_text = strdup(value);
        }
        else if( strcmp(line, "val") == 0 || strcmp(line, "valstr") == 0 )
        {
            char* comma = strchr(value, ',');
            int key;
            int mapped = 0;
            const char* text = NULL;
            int as_string = def->output_is_string || strcmp(line, "valstr") == 0;

            if( !comma )
            {
                CONTENT_ERROR("%s:%d: %s needs `key,value`\n", path, line_number, line);
                continue;
            }
            *comma = '\0';
            key = atoi(value);
            if( as_string )
            {
                def->output_is_string = 1;
                text = strdup(comma + 1);
            }
            else
            {
                mapped = atoi(comma + 1);
            }
            if( def->count >= def->capacity )
            {
                int next = def->capacity ? def->capacity * 2 : 32;
                struct Mock230EnumValue* grown =
                    realloc(def->values, (size_t)next * sizeof(*grown));

                if( !grown )
                {
                    CONTENT_ERROR("%s:%d: out of memory growing enum values\n", path,
                                  line_number);
                    continue;
                }
                def->values = grown;
                def->capacity = next;
            }
            def->values[def->count].key = key;
            def->values[def->count].text = text;
            def->values[def->count].value = mapped;
            def->count++;
        }
        /* Unknown keys are ignored here: the unpacker emits fields the
         * decoder does not store (`EXCEPTIONS.md`), and a silent skip is the
         * same trade the rest of the rank-0 loaders make. */
    }
    fclose(file);
    return loaded;
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
        else if( strcmp(line, "wholewrite") == 0 )
        {
            /* Only `allow` means anything. Spelling it any other way is a
             * declaration that reads as a decision and is not one, which is
             * exactly what an unknown-key error exists to catch. */
            if( strcmp(value, "allow") != 0 )
                CONTENT_ERROR("%s:%d: `wholewrite` takes `allow`, not `%s`\n", path,
                              line_number, value);
            else
                def->wholewrite_allowed = 1;
        }
        else if( strcmp(line, "wholeread") == 0 )
        {
            /*
             * The read half of the same licence, and it is the compiler's
             * business rather than this loader's.
             *
             * `wholeread=allow` says a whole-varp READ of a carrier is meant —
             * content naming a bit no cache varbit claims, which is the only way
             * to reach one (varbit ids are cache-only, so content cannot declare
             * the varbit it would rather use). `ssc_symbols.c` is what enforces
             * it, and it also refuses the declaration when no varbit is based on
             * the varp at all, so the claim is checked where it is made.
             *
             * Nothing at runtime needs it: reading a carrier whole destroys
             * nothing, which is exactly why it is a separate word from
             * `wholewrite`. What this arm buys is that the key is *accepted* —
             * it was rejected as unknown, and one rejected line is one content
             * error, which is a red `mock230 --selftest` for a file that is
             * correct.
             */
            if( strcmp(value, "allow") != 0 )
                CONTENT_ERROR("%s:%d: `wholeread` takes `allow`, not `%s`\n", path,
                              line_number, value);
        }
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
 *
 * Resolution publishes to **two** places and that is the point (2026-08-02):
 * `Mock230LocDef.next_loc_stage`, which the engine's own door swap reads, and
 * the loc param table, which is where `loc_param(next_loc_stage)` — the
 * reference's line in `doors/scripts/doors.rs2` — looks. Only the first existed
 * before, so a `.loc` block could state a param that no script could read; the
 * script got the declared default and a door opened into loc 0. One value, two
 * readers, resolved once.
 *
 * The value is resolved through `pack/loc.pack`, i.e. this grammar assumes a
 * loc-typed param. `fields/loc.ini` says so (`ref = loc`) and cachepack honours
 * it on the bake side, but `struct ContentField` carries no `ref`, so C cannot
 * ask. Only `next_loc_stage` is declared today; the second loc param whose type
 * is not `loc` is what makes carrying `ref` into C worth doing, and it will fail
 * loudly here ("is not in configs/all.loc.compack") rather than quietly.
 */

struct PendingStage
{
    int def_index;
    /** The param the overlay line named, resolved through pack/param.pack. */
    int param_id;
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
            def->category = -1;
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
            /*
             * Through the pack, not through a `strcmp` ladder.
             *
             * The ladder that was here accepted exactly two spellings and filed
             * them under a private enum, so `category` on a loc meant something
             * different from `category` on an npc or an obj while being spelled
             * the same and living in the same field register. Resolving it here
             * is what makes `[oploc1,_door_closed]` a category subject the trigger
             * lookup can answer — see `Mock230LocDef.category`.
             */
            int id = mock230_content_symbol(MOCK230_PACK_CATEGORY, value);

            if( id < 0 )
                CONTENT_ERROR("%s:%d: `%s` is not in pack/category.pack — a category is "
                              "named there or it is not a category\n",
                              path, line_number, value);
            else if( id == 0 )
                CONTENT_ERROR("%s:%d: `%s` resolves to category 0, which is the decoder's "
                              "\"unstated\" and must never be a name\n",
                              path, line_number, value);
            else
                def->category = id;
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
                 * name spelled twice. The binding is `param = <name>` (or the retired
                 * projection spelling `client = param:<name>`) — either fills
                 * `param_name`, and that is what this parser accepts. */
                const struct ContentField* field = ContentFields_Find(&g_loc_fields, value);
                int param_id;

                if( !field || !field->param_name[0] )
                {
                    CONTENT_ERROR("%s:%d: `%s` is not a loc param this build knows — "
                                  "declare it in fields/loc.ini with `param = <name>`\n",
                                  path, line_number, value);
                    continue;
                }
                /* The register names the param; the pack numbers it. Which number
                 * `next_loc_stage` is is the pack file's business, and a script
                 * asking for it by name has to reach the same row this line
                 * writes. */
                param_id = mock230_content_symbol(MOCK230_PACK_PARAM, field->param_name);
                if( param_id < 0 )
                {
                    CONTENT_ERROR("%s:%d: fields/loc.ini maps `%s` to param `%s`, which "
                                  "pack/param.pack does not name\n",
                                  path, line_number, value, field->param_name);
                    continue;
                }
                g_pending = grow(g_pending, &g_pending_capacity, g_pending_count,
                                 sizeof(*g_pending));
                g_pending[g_pending_count].def_index = def_index;
                g_pending[g_pending_count].param_id = param_id;
                g_pending[g_pending_count].symbol = strdup(comma + 1);
                g_pending_count++;
            }
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
        {
            g_loc_defs[index].next_loc_stage = target;
            /* …and into the param table, so `loc_param(next_loc_stage)` answers
             * the same number the field does. See this section's header. */
            mock230_locinfo_param_overlay(g_loc_defs[index].loc_id, g_pending[i].param_id,
                                          target);
        }
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

/**
 * Almost every .jm2 contains only MAP and LOC data, which this server does not
 * consume. Check cheaply for the two tokens that can make a file relevant
 * before paying for millions of fgets/trim/section operations. Keeping two
 * bytes of overlap makes a token split across fread blocks visible.
 */
static int
jm2_may_have_spawns(FILE* file)
{
    unsigned char bytes[65536 + 2];
    size_t carry = 0;
    size_t got;

    while( (got = fread(bytes + carry, 1, sizeof(bytes) - carry, file)) > 0 )
    {
        size_t total = carry + got;

        for( size_t i = 0; i + 2 < total; i++ )
        {
            if( (bytes[i] == 'N' && bytes[i + 1] == 'P' && bytes[i + 2] == 'C') ||
                (bytes[i] == 'O' && bytes[i + 1] == 'B' && bytes[i + 2] == 'J') )
            {
                rewind(file);
                return 1;
            }
        }
        carry = total < 2 ? total : 2;
        if( carry )
            memmove(bytes, bytes + total - carry, carry);
    }
    return 0;
}

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
    if( !jm2_may_have_spawns(file) )
    {
        fclose(file);
        return;
    }
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
/* The `default=` of each param. The reference relies on these: an obj that
 * does not carry the row answers with the param's declared default
 * (LostCity's ObjConfigOps pushes `paramType.defaultInt`), and 365 of the 469
 * declared defaults are -1 — "no id", which no zero can spell because 0 is a
 * real obj. A param that declares no default is 0, matching the cache decode:
 * `RSCache_Dat2ConfigParamInit` leaves `default_int` on the zeroed record.
 * There is deliberately no "undeclared" sentinel — -1 is the most-declared
 * default of all, so any in-band marker would collide with real data. */
static int* g_param_defaults;

int
mock230_content_param_default(int param_id)
{
    if( param_id < 0 || param_id >= g_param_type_count || !g_param_defaults )
        return 0;
    return g_param_defaults[param_id];
}

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
    int line_number = 0;

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
    g_param_defaults = (int*)malloc((size_t)g_param_type_count * sizeof(int));
    if( !g_param_defaults )
    {
        free(g_param_types);
        g_param_types = NULL;
        g_param_type_count = 0;
        fclose(file);
        return 0;
    }
    for( int i = 0; i < g_param_type_count; i++ )
        g_param_defaults[i] = 0;

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
            /*
             * A block naming a param `configs/all.param.compack` does not list.
             *
             * Silent before, and the shape of the silence is what makes it worth
             * an error: the block is skipped, so the param keeps `type` 0 and
             * `default` 0, and `push_typed_param` then answers every absent row
             * with 0 instead of the declared default. This file and its index
             * are both machine exports of the same archive, so a name in one and
             * not the other means the two have drifted.
             */
            if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, header, &current) )
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.param.compack\n", path,
                              line_number, header);
            continue;
        }
        if( current < 0 || current >= g_param_type_count )
            continue;
        value = mock230_content_split_key_value(line);
        if( !value )
            continue;
        if( strcmp(line, "type") == 0 )
        {
            g_param_types[current] = value[0];
            loaded++;
            continue;
        }
        if( strcmp(line, "default") == 0 )
        {
            /* Always a plain integer. This file is a cache dump, so the value
             * is the raw g4 the record carried — every one of the 469 is
             * numeric, and a *string* param (type 's' — the char here is the
             * cache's own code, not the overlay grammar's word) declares
             * `defaultstr=`, which nothing reads yet. Symbolic defaults
             * (`default=human_death`) live in the server overlay `.param`
             * files, which this loader does not walk — see the caller. */
            g_param_defaults[current] = atoi(value);
        }
    }
    fclose(file);
    return loaded;
}

/*
 * Server overlay `.param` files (`bas.param`, `combat.param`, …) declare
 * `type=` / `default=` with symbolic defaults (`default=human_ready`). The
 * cache dump in configs/all.param never names these server-allocated params,
 * so without this walk `oc_param` answers 0 for every absent row.
 */
static int
load_server_param_defaults_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    int current = -1;
    int loaded = 0;
    int line_number = 0;

    if( !file )
        return 0;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;
        int resolved;

        line_number++;
        if( !*line )
            continue;
        header = mock230_content_section_header(line);
        if( header )
        {
            if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, header, &current) )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.param.compack\n", path,
                              line_number, header);
                current = -1;
            }
            continue;
        }
        if( current < 0 || current >= g_param_type_count )
            continue;
        value = mock230_content_split_key_value(line);
        if( !value )
            continue;
        if( strcmp(line, "type") == 0 )
        {
            /* Overlay words → cache letter. Everything but string is int-shaped. */
            if( strcmp(value, "string") == 0 || strcmp(value, "s") == 0 )
                g_param_types[current] = 's';
            else
                g_param_types[current] = 'i';
            loaded++;
            continue;
        }
        if( strcmp(line, "default") == 0 )
        {
            if( g_param_types[current] == 's' )
                continue; /* defaultstr= not read yet; leave 0 */
            if( strcmp(value, "null") == 0 )
                g_param_defaults[current] = -1;
            else if( strcmp(value, "yes") == 0 || strcmp(value, "true") == 0 )
                g_param_defaults[current] = 1;
            else if( strcmp(value, "no") == 0 || strcmp(value, "false") == 0 )
                g_param_defaults[current] = 0;
            else if( (value[0] >= '0' && value[0] <= '9') ||
                     (value[0] == '-' && value[1] >= '0') )
                g_param_defaults[current] = atoi(value);
            else if( strchr(value, ' ') != NULL || value[0] == '^' )
                continue; /* prose / constant — not a seq/obj id */
            else if( mock230_content_symbol_checked(MOCK230_PACK_SEQ, value, &resolved) ||
                     mock230_content_symbol_checked(MOCK230_PACK_OBJ, value, &resolved) ||
                     mock230_content_symbol_checked(MOCK230_PACK_SPOTANIM, value, &resolved) ||
                     mock230_content_symbol_checked(MOCK230_PACK_NPC, value, &resolved) )
                g_param_defaults[current] = resolved;
            /* else leave 0 — unknown symbolic defaults are not pack failures */
        }
    }
    fclose(file);
    return loaded;
}

/* walk_configs callback: server overlay `.param` type/default declarations. */
static void
load_server_param_file(const char* path)
{
    int n = load_server_param_defaults_file(path);

    if( n > 0 )
        fprintf(stderr, "mock230: %d server param type(s) from %s\n", n, path);
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

/**
 * Recursively load every `.npc` then every `.loc`; the two passes keep a door
 * config free to name a loc declared in another file.
 *
 * `scandir` + `alphasort`, not `readdir` walked as it comes: `readdir` makes no
 * ordering promise at all, it hands back entries in whatever order the
 * filesystem's directory storage happens to hold them, and two files that
 * declare a block for the same npc are not merged (`mock230_content_npc`
 * returns the *first* match in load order — see the comment above
 * `g_npc_defs`). "areas/ sorts before general/, which is precisely the order
 * that happens" a few hundred lines up is an assumption this function was not
 * actually keeping: it read correctly on whatever filesystem order that
 * comment was measured against, and broke the moment a second generated
 * `.npc` file landed beside an existing one in the same directory and readdir
 * happened to hand it back first — the npcs both blocks name then silently
 * lost every field only the *other* block set. Sorting makes the order the
 * comment already promises actually true, rather than true by filesystem
 * coincidence.
 */
static void
walk_configs(
    const char* dir,
    const char* suffix,
    void (*load)(const char*))
{
    struct dirent** entries;
    char path[1024];
    int count = scandir(dir, &entries, NULL, alphasort);

    if( count < 0 )
        return;
    for( int i = 0; i < count; i++ )
    {
        if( entries[i]->d_name[0] != '.' )
        {
            snprintf(path, sizeof(path), "%s/%s", dir, entries[i]->d_name);
            if( mock230_path_is_dir(path) )
                walk_configs(path, suffix, load);
            else if( has_suffix(entries[i]->d_name, suffix) )
                load(path);
        }
        free(entries[i]);
    }
    free(entries);
}

/*
 * The multi-combat zone set, from `maps/multiway.csv`.
 *
 * Where LostCity puts it: `content/maps/multiway.csv`, read by `GameMap.ts`
 * into a `Set<number>` of zone indices and answered by `GameMap.isMulti`. Engine
 * code, content data — so the file ports across and only the reader is written
 * here.
 *
 * A *set of zones*, not a list of rectangles, because that is the shape of the
 * fact: a zone is either multi or it is not, the wilderness edge is ragged, and
 * 4,697 zones do not reduce to a handful of boxes. Kept sorted so a query is a
 * binary search — `player_combat.rs2` asks once per swing, per fighter.
 */
static int* g_multiway;
static int g_multiway_count;
static int g_multiway_capacity;

/*
 * Same packing as `mock230_zone_index` / the reference's `ZoneMap.zoneIndex`.
 * Kept local so `mock230_pack` need not link the zone module.
 */
static int
content_zone_index(
    int x,
    int z,
    int level)
{
    return ((x >> 3) & 0x7ff) | (((z >> 3) & 0x7ff) << 11) | ((level & 3) << 22);
}

static int
compare_zone_index(
    const void* a,
    const void* b)
{
    int left = *(const int*)a;
    int right = *(const int*)b;

    return left < right ? -1 : (left > right ? 1 : 0);
}

static void
load_multiway(const char* path)
{
    FILE* file = fopen(path, "rb");
    char line[256];

    if( !file )
        return;
    while( fgets(line, sizeof(line), file) )
    {
        int level, map_x, map_z, local_x, local_z;

        if( sscanf(line, "%d_%d_%d_%d_%d", &level, &map_x, &map_z, &local_x, &local_z) != 5 )
            continue; /* a comment or a blank line — the reference skips both. */
        /*
         * The reference warns on a line that is not zone-aligned and then files
         * it anyway, which files the *containing* zone. Reproduced by rounding
         * rather than by warning: the zone index shifts the tile down to its
         * zone, so an unaligned line lands where the reference puts it, and a
         * warning nobody can act on in a ported data file is noise.
         */
        if( g_multiway_count == g_multiway_capacity )
        {
            int capacity = g_multiway_capacity ? g_multiway_capacity * 2 : 1024;
            int* grown = realloc(g_multiway, (size_t)capacity * sizeof(*grown));

            if( !grown )
            {
                CONTENT_ERROR("maps/multiway.csv: out of memory at %d zones\n",
                              g_multiway_count);
                break;
            }
            g_multiway = grown;
            g_multiway_capacity = capacity;
        }
        g_multiway[g_multiway_count++] =
            content_zone_index((map_x << 6) + local_x, (map_z << 6) + local_z, level);
    }
    fclose(file);
    qsort(g_multiway, (size_t)g_multiway_count, sizeof(*g_multiway), compare_zone_index);
}

int
mock230_content_multiway(
    int x,
    int z,
    int level)
{
    int key = content_zone_index(x, z, level);
    int low = 0;
    int high = g_multiway_count - 1;

    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_multiway[mid] == key )
            return 1;
        if( g_multiway[mid] < key )
            low = mid + 1;
        else
            high = mid - 1;
    }
    return 0;
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

/*
 * What an npc is before any content describes it.
 *
 * Numbers only. The four *ids* that used to be here — `human_unarmedpunch`,
 * `human_unarmedblock`, `human_death`, `bones` — were the engine naming content
 * it does not own, and they are now the `[default]` block of a `.npc` config
 * (`server/scripts/general/configs/npc_default.npc`). An engine holding no ids
 * is the whole architecture; four of them hiding in an initialiser is exactly
 * how that erodes.
 *
 * The animation fields start at -1, which is "play nothing", so a tree that
 * declares no `[default]` block degrades to silent npcs rather than to npcs
 * animating with sequence 0.
 */
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
    /*
     * How long the corpse lies there once the death animation has *started* —
     * `npc_delay(1)` in `[proc,npc_death]`, which is `tick + 1 + 1` and so two
     * ticks. It used to be 3 and to be measured from the killing blow, which is
     * a different thing: the animation now starts a tick later (and later still
     * if the npc was mid-step), so the same 3 ticks would have been a different
     * death from the reference's.
     *
     * Not 0, which would despawn the corpse on the tick the animation started
     * and eat it entirely. A tree that states `death_delay` overrides it; this
     * is only what a tree stating nothing degrades to.
     */
    g_npc_default.death_delay = 2;
    g_npc_default.attackrate = 4;
    g_npc_default.attackrange = 1;
    /*
     * The reference's NpcType defaults, and the pair that decides how far a
     * monster will roam and follow: 5 tiles of wander from where it spawned,
     * and 7 of leash — `maxrange` defaults to `wanderrange + 2` in LostCity's
     * `NpcType`, which is where both numbers come from.
     *
     * `wanderrange` was missing while `maxrange` was here, and the two are a
     * pair. The consequence was invisible for as long as the roster was the 63
     * hand-authored Lumbridge npcs, because their `.npc` blocks state it: an
     * npc nothing describes got `wanderrange = 0`, which
     * `mock230_world_npc_default_mode` reads as MODE_NONE and the roam block
     * skips outright. On a world roster of 23,139 npcs, of which 22 files state
     * a wanderrange, that is "most npcs never move".
     */
    g_npc_default.wanderrange = 5;
    g_npc_default.maxrange = 7;
    g_npc_default.givechase = 1;
    /* Everything fights back unless it says otherwise. */
    g_npc_default.retaliate = 1;
    /* LostCity NpcType defaults: blockwalk=NPC, no sight block, normal move. */
    g_npc_default.blockwalk = 1;
    g_npc_default.blocksight = 0;
    g_npc_default.moverestrict = 0;
    g_npc_default.nomove = 0;
    g_npc_default.turnspeed = -1; /* unstated: defer to the cache record */
    g_npc_default.damagetype = MOCK230_DAMAGE_CRUSH;
    g_npc_default.attack_anim = -1;
    g_npc_default.defend_anim = -1;
    g_npc_default.death_anim = -1;
    g_npc_default.death_drop = -1;
    /* Silent unless something states otherwise. Sound effect 0 is a real clip,
     * so this cannot be 0 — see the field comment in mock230_content.h. */
    g_npc_default.attack_sound = -1;
    g_npc_default.defend_sound = -1;
    g_npc_default.death_sound = -1;
    g_npc_default.defaultmode = MOCK230_NPCMODE_NONE;
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
        /* The server's allocation ledger, layered into the same namespace. The
         * compack is the cache's member index and `pack/<ns>.alloc` holds the
         * ids ss_allocate.py handed out past its high-water mark — one
         * namespace, two files, so a rename in either is caught by
         * `validate_symbols` exactly as a duplicate inside one file is. Absent
         * for most kinds, and `pack_load` reads absence as zero symbols. */
        snprintf(path, sizeof(path), "%s/pack/%s.alloc", dir, name);
        symbols += pack_load(&g_packs[kind], path);
    }
    /* Same imported namespace layers passed to sscompile in the build. */
    symbols += load_ported_pack_symbols(dir);
    /* After the loop: it needs the interface pack to already be loaded. */
    symbols += load_component_symbols_from_root(dir, 0);
    symbols += load_ported_component_symbols(dir);

    /* Packs are immutable from here through boot and runtime. Keep their
     * insertion-order arrays for walking and diagnostics, and build sorted
     * pointer views for the far more frequent name/id lookups. */
    for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
        pack_build_indexes(&g_packs[kind]);

    /* A symbol table that answers a name two ways is refused here rather than
     * resolved silently later. */
    validate_symbols(&reg);

    /*
     * A varp the per-player array cannot hold, refused at boot.
     *
     * `Mock230Player.varps` is a flat array and its size is a constant, so the
     * tree can outgrow it — and it has: the `%com_*` combat block reached
     * 6223 against an array of 6216, and the seven ids over the end were not
     * dropped quietly, they aborted `[proc,player_combat_stat]` on every npc
     * swing. From the outside that is "npcs do not fight back", with the real
     * message buried in a script trace nobody reads during a fight.
     *
     * Allocating a varp is content's to do, so this does not cap it — it says
     * exactly which line of the engine has to move, and it says so once at
     * boot instead of once per swing.
     */
    {
        int highest = -1;
        int count = mock230_content_symbol_walk(MOCK230_PACK_VARP, -1, NULL, NULL);

        for( int i = 0; i < count; i++ )
        {
            int id = -1;

            mock230_content_symbol_walk(MOCK230_PACK_VARP, i, &id, NULL);
            if( id > highest )
                highest = id;
        }
        if( highest >= MOCK230_VARP_COUNT )
            CONTENT_ERROR(
                "the tree declares varp %d and Mock230Player.varps holds %d — raise "
                "MOCK230_VARP_SERVER_HEADROOM by at least %d (mock230.h)\n",
                highest,
                MOCK230_VARP_COUNT,
                highest - MOCK230_VARP_COUNT + 1);
    }

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
    /* Server `.param` overlays before configs that write `param=` rows, so
     * defaults for ready_baseanim / slashattack_anim / … answer correctly. */
    walk_configs(path, ".param", load_server_param_file);
    /* Constants first: every other grammar may write `^name` where a number
     * goes, and an unexpanded caret is a load error rather than a zero. */
    walk_configs(path, ".constant", load_constant_config);
    walk_configs(path, ".enum", load_enum_config);
    /* Rank-0 enums after the server walk so an authored `.enum` of the same
     * name keeps winning (mock230_content_enum returns the first match).
     * Structs need no text loader: mock230_structinfo_load already feeds
     * SS_OP_STRUCT_PARAM from the binary cache. */
    {
        int enums = load_rank0_enums(dir);

        if( enums )
            fprintf(stderr, "mock230: %d enums from configs/all.enum\n", enums);
    }
    walk_configs(path, ".varp", load_varp_config);
    /* `[default]` first, then the roster — see load_npc_default_config. */
    walk_configs(path, ".npc", load_npc_default_config);
    walk_configs(path, ".npc", load_npc_config);
    walk_configs(path, ".obj", load_obj_config);
    walk_configs(path, ".loc", load_loc_config);
    /* After the configs: a spawn names an npc or an obj, and the name has to
     * resolve against the packs the loader has already read. */
    walk_configs(path, ".spawn", load_spawn_config);
    resolve_loc_stages();

    snprintf(path, sizeof(path), "%s/maps", dir);
    load_maps(path);
    snprintf(path, sizeof(path), "%s/maps/multiway.csv", dir);
    load_multiway(path);

    {
        int requires_total = 0;
        int requires_from_cache = 0;

        mock230_obj_require_counts(&requires_total, &requires_from_cache);
        fprintf(stderr,
                "mock230: content loaded (%d symbols, %d constants, %d npc defs, %d loc defs, "
                "%d varp defs, %d equip reqs (%d from the cache), %d npc spawns, "
                "%d obj spawns%s)\n",
                symbols, g_constant_count, g_npc_def_count, g_loc_def_count, g_varp_def_count,
                requires_total, requires_from_cache, g_npc_spawn_count, g_obj_spawn_count,
                g_errors ? ", WITH ERRORS" : "");
        if( g_client_key_overlays )
            fprintf(stderr,
                    "mock230: %d config line(s) restate a field the client's own record "
                    "carries — the cache already says it, so the overlay is inert\n",
                    g_client_key_overlays);
    }
    return g_npc_def_count;
}

/* ------------------------------------------------------------------ */
/* The server band (server/pack)                                       */
/* ------------------------------------------------------------------ */

/*
 * The band load path: verify everything, then apply, or apply nothing.
 *
 * The text pass above and `cachepack pack` read the *same* tree, so during
 * migration each is a full check on the other: every value a band archive
 * states must be the value the text parse loaded, and every band-registered
 * field the text moved off its seed must either be in the band or be a field
 * the band has no wire for (`huntmode=aggressive` is an engine enum name and
 * the band carries integers — `fields/npc.ini` documents which). The compare is
 * three-way against the *seed* — engine defaults, the `[default]` block, and
 * the cache params — because "the band omitted it" and "the band contradicts
 * it" are different failures: the first falls back a field, the second means
 * the pack on disk predates the tree and none of it can be trusted.
 *
 * Only after every archive of every type passes is the band decoded over the
 * live records, at which point the band — not the text — is what the engine
 * runs on. `mock230_boot_load` logs which of the two happened.
 */

/** Per-type glue the generic codec cannot carry: how to seed a record and how
 *  to reach the text pass's defs. One row per `Mock230_ServerTypes()` entry —
 *  a registered type with no row here is reported at load, not skipped. */
struct BandGlue
{
    const char* name;
    /** idx number inside server/pack — the type's own config kind, the same
     *  two coordinates the client cache uses. */
    int group;
    enum Mock230PackKind pack_kind;
    void (*seed)(void* record, int id);
    /** 1 when `seed` actually sees the cache record for `id`. The npc seed
     *  reads through `mock230_npcinfo`, which hides nameless records behind a
     *  placeholder — a band over one of those has nothing here to compare
     *  against, and no def ever seeds from it either. */
    int (*seed_sees_cache)(int id);
    int (*def_count)(void);
    void* (*def_at)(int index, int* out_id);
};

static void
band_seed_npc(
    void* record,
    int id)
{
    npc_def_seed_from_cache((struct Mock230NpcDef*)record, id);
}

/** What load_loc_config starts a block from, exactly. */
static void
band_seed_loc(
    void* record,
    int id)
{
    struct Mock230LocDef* def = (struct Mock230LocDef*)record;

    memset(def, 0, sizeof(*def));
    def->loc_id = id;
    def->symbol = mock230_content_symbol_name(MOCK230_PACK_LOC, id);
    def->next_loc_stage = -1;
}

static int
band_npc_def_count(void)
{
    return g_npc_def_count;
}

static void*
band_npc_def_at(
    int index,
    int* out_id)
{
    *out_id = g_npc_defs[index].npc_id;
    return &g_npc_defs[index];
}

static int
band_loc_def_count(void)
{
    return g_loc_def_count;
}

static void*
band_loc_def_at(
    int index,
    int* out_id)
{
    *out_id = g_loc_defs[index].loc_id;
    return &g_loc_defs[index];
}

/** The loc seed reads nothing from the cache, so nothing is hidden from it. */
static int
band_loc_seed_sees_cache(int id)
{
    (void)id;
    return 1;
}

static const struct BandGlue k_band_glue[] = {
    { "npc", RSCACHE_DAT2_CONFIG_KIND_NPC, MOCK230_PACK_NPC, band_seed_npc,
      mock230_npcinfo_known, band_npc_def_count, band_npc_def_at },
    { "loc", RSCACHE_DAT2_CONFIG_KIND_LOCS, MOCK230_PACK_LOC, band_seed_loc,
      band_loc_seed_sees_cache, band_loc_def_count, band_loc_def_at },
};

#define BAND_GLUE_COUNT ((int)(sizeof(k_band_glue) / sizeof(k_band_glue[0])))

/** Big enough for any registered record; checked against `record_size` before
 *  use so a new, larger type fails here rather than overruns. */
union BandRecord
{
    struct Mock230NpcDef npc;
    struct Mock230LocDef loc;
};

enum
{
    BAND_FIELD_MAX = 64,
};

/** The same offset read `mock230_servercodec.c`'s field_get does, restated
 *  because that one is rightly private: memcpy, not a cast, for the alignment
 *  reason documented there. */
static int
band_field(
    const void* record,
    const struct ServerField* field)
{
    int value;

    memcpy(&value, (const char*)record + field->offset, sizeof(value));
    return value;
}

/**
 * Hold one decoded record to the text parse, field by registered field.
 *
 * Returns the mismatches — band values that contradict the text. A field the
 * band merely *lacks* (decoded value still at the seed) goes into the per-field
 * `text_only` tally instead: the text value stands for it, and the tally is
 * what keeps that gap visible in the boot log rather than silent.
 */
static int
band_compare(
    const struct ServerType* type,
    const void* merged,
    const void* text,
    const void* seed,
    int id,
    enum Mock230PackKind pack_kind,
    int* text_only)
{
    int mismatched = 0;
    const char* symbol = NULL;

    for( int i = 0; i < type->count; i++ )
    {
        const struct ServerField* field = &type->fields[i];
        int band_value = band_field(merged, field);
        int text_value = band_field(text, field);

        if( band_value == text_value )
            continue;
        if( band_value == band_field(seed, field) )
        {
            text_only[i]++;
            continue;
        }
        /* Name lookup is a linear scan of a large pack namespace. Most band
         * records match, so pay for the diagnostic spelling only when this
         * record will actually print a diagnostic. */
        if( !symbol )
            symbol = mock230_content_symbol_name(pack_kind, id);
        fprintf(stderr,
                "mock230: server band: %s [%s] %d: `%s` is %d in the band but %d from the text "
                "overlays\n",
                type->name, symbol ? symbol : "?", id, field->name, band_value, text_value);
        mismatched++;
    }
    return mismatched;
}

static void
band_verify_type(
    struct Mock230ServPack* pack,
    const struct BandGlue* glue,
    const struct ServerType* type,
    struct Mock230BandReport* report,
    int* text_only)
{
    uint8_t band[MOCK230_SERVPACK_BAND_MAX];
    union BandRecord seed;
    union BandRecord merged;
    int entries = Mock230_ServPackEntryCount(pack, glue->group);

    /* Every archive the pack holds, whether or not the text authored a block
     * for the record: a band over a record with no block must decode to
     * exactly its seed, which is how a stale export shows up. */
    for( int id = 0; id < entries; id++ )
    {
        int size = Mock230_ServPackReadBand(pack, glue->group, id, band, sizeof(band));
        const void* text;
        int consumed;

        if( size == MOCK230_SERVPACK_ABSENT )
            continue;
        if( size == MOCK230_SERVPACK_INVALID )
        {
            if( report->invalid++ < 8 )
            {
                const char* symbol = mock230_content_symbol_name(glue->pack_kind, id);

                fprintf(stderr,
                        "mock230: server band: %s [%s] %d refuses to open — bad magic, "
                        "version, kind or CRC\n",
                        type->name, symbol ? symbol : "?", id);
            }
            continue;
        }

        glue->seed(&seed, id);
        memcpy(&merged, &seed, type->record_size);
        consumed = Mock230_ServerDecode(type, &merged, band, size);
        if( consumed != size )
        {
            /* The codec's short-read signal: an opcode this build does not
             * know. A register that ran ahead of the C table, or a pack from a
             * newer build — either way not this build's to decode. */
            if( report->invalid++ < 8 )
            {
                const char* symbol = mock230_content_symbol_name(glue->pack_kind, id);

                fprintf(stderr,
                        "mock230: server band: %s [%s] %d stops at byte %d of %d — an opcode "
                        "this build does not know\n",
                        type->name, symbol ? symbol : "?", id, consumed, size);
            }
            continue;
        }

        report->archives++;
        text = NULL;
        for( int i = 0; i < glue->def_count(); i++ )
        {
            int def_id;
            void* def = glue->def_at(i, &def_id);

            if( def_id == id )
            {
                text = def;
                break;
            }
        }
        if( text )
            report->overlaid++;
        else if( !glue->seed_sees_cache(id) )
        {
            /* No def, and the seed is blind to this record (a nameless multinpc
             * instance, hidden behind the npcinfo placeholder). The band states
             * what the tree states, but nothing on this side loads the record
             * at all, so there is no value to hold it to — and never a def to
             * apply it over. Counted, not compared. */
            report->unseeded++;
            continue;
        }
        report->mismatched += band_compare(type, &merged, text ? text : (const void*)&seed,
                                           &seed, id, glue->pack_kind, text_only);
    }

    /* And the other direction: text defs the pack holds no archive for. Their
     * band-registered fields must all still sit at the seed — or be fields the
     * band has no wire for, which is the text_only tally again. */
    for( int i = 0; i < glue->def_count(); i++ )
    {
        int id;
        void* def = glue->def_at(i, &id);
        int size;

        size = Mock230_ServPackReadBand(pack, glue->group, id, band, sizeof(band));
        if( size != MOCK230_SERVPACK_ABSENT )
            continue;
        glue->seed(&seed, id);
        report->mismatched +=
            band_compare(type, &seed, def, &seed, id, glue->pack_kind, text_only);
    }
}

/** Decode every archive over its live def. Runs only after every type
 *  verified, so this is the preference switch and never a change of value. */
static void
band_apply_type(
    struct Mock230ServPack* pack,
    const struct BandGlue* glue,
    const struct ServerType* type)
{
    uint8_t band[MOCK230_SERVPACK_BAND_MAX];

    for( int i = 0; i < glue->def_count(); i++ )
    {
        int id;
        void* def = glue->def_at(i, &id);
        int size = Mock230_ServPackReadBand(pack, glue->group, id, band, sizeof(band));

        if( size > 0 )
        {
            Mock230_ServerDecode(type, def, band, size);
            /* Packs that emit moverestrict=nomove without the collapsed nomove
             * opcode still pin the mover. */
            if( strcmp(type->name, "npc") == 0 )
            {
                struct Mock230NpcDef* npc = (struct Mock230NpcDef*)def;

                if( npc->moverestrict == 5 )
                    npc->nomove = 1;
            }
        }
    }
}

enum Mock230BandStatus
mock230_content_load_server_band(
    const char* dir,
    struct Mock230BandReport* report)
{
    struct Mock230ServPack pack;
    int type_count = 0;
    const struct ServerType* types = Mock230_ServerTypes(&type_count);

    memset(report, 0, sizeof(*report));
    if( Mock230_ServPackOpen(&pack, dir) != 0 )
        return MOCK230_BAND_MISSING;

    for( int t = 0; t < type_count; t++ )
    {
        const struct ServerType* type = &types[t];
        const struct BandGlue* glue = NULL;
        int text_only[BAND_FIELD_MAX] = { 0 };

        for( int g = 0; g < BAND_GLUE_COUNT; g++ )
        {
            if( strcmp(k_band_glue[g].name, type->name) == 0 )
                glue = &k_band_glue[g];
        }
        if( !glue || type->count > BAND_FIELD_MAX || type->record_size > sizeof(union BandRecord) )
        {
            /* A type the codec registers that this loader cannot reach is a
             * gap someone has to see, not a skip. */
            CONTENT_ERROR("server band: type `%s` is registered but the boot has no glue "
                          "for it\n",
                          type->name);
            continue;
        }

        band_verify_type(&pack, glue, type, report, text_only);

        for( int i = 0; i < type->count; i++ )
        {
            if( !text_only[i] )
                continue;
            report->text_only += text_only[i];
            fprintf(stderr,
                    "mock230: server band: %s.%s stays text-loaded on %d record(s) — the band "
                    "has no wire for it\n",
                    type->name, type->fields[i].name, text_only[i]);
        }
    }

    if( report->invalid || report->mismatched )
    {
        Mock230_ServPackClose(&pack);
        return MOCK230_BAND_STALE;
    }

    for( int t = 0; t < type_count; t++ )
    {
        for( int g = 0; g < BAND_GLUE_COUNT; g++ )
        {
            if( strcmp(k_band_glue[g].name, types[t].name) == 0 )
                band_apply_type(&pack, &k_band_glue[g], &types[t]);
        }
    }
    Mock230_ServPackClose(&pack);
    return MOCK230_BAND_LOADED;
}

void
mock230_content_free(void)
{
    for( int kind = 0; kind < MOCK230_PACK_COUNT; kind++ )
    {
        for( int i = 0; i < g_packs[kind].count; i++ )
            free(g_packs[kind].entries[i].name);
        free(g_packs[kind].entries);
        free(g_packs[kind].by_name);
        free(g_packs[kind].by_id);
        g_packs[kind].entries = NULL;
        g_packs[kind].by_name = NULL;
        g_packs[kind].by_id = NULL;
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
    free(g_multiway);
    g_multiway = NULL;
    g_multiway_count = g_multiway_capacity = 0;
    for( int i = 0; i < g_enum_def_count; i++ )
    {
        free((void*)g_enum_defs[i].symbol);
        if( g_enum_defs[i].default_text &&
            strcmp(g_enum_defs[i].default_text, "null") != 0 )
            free((void*)g_enum_defs[i].default_text);
        for( int j = 0; j < g_enum_defs[i].count; j++ )
            free((void*)g_enum_defs[i].values[j].text);
        free(g_enum_defs[i].values);
    }
    free(g_enum_defs);
    g_enum_defs = NULL;
    g_enum_def_count = g_enum_def_capacity = 0;
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
