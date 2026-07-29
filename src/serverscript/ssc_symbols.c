/*
 * Symbol tables for the compiler.
 *
 * Names come from the same two sources the reference compiler uses:
 *
 *   .pack       one `id=name` per line, one file per namespace (npc.pack,
 *               obj.pack, interface.pack, ...)
 *   .constant   one `^name value` per line, scattered through the content tree
 *
 * A constant keeps its literal text rather than a parsed number, because a
 * constant can expand to anything the grammar accepts — a string, a coord, a
 * symbol name — not just an integer.
 */

#include "ssc.h"
#include <sys/stat.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */

void
SSC_SymbolsInit(struct SSC_Symbols* symbols)
{
    memset(symbols, 0, sizeof(*symbols));
}

void
SSC_SymbolsFree(struct SSC_Symbols* symbols)
{
    int i;

    if( !symbols )
        return;

    for( i = 0; i < symbols->count; i++ )
        free(symbols->entries[i].text);
    free(symbols->entries);
    free(symbols->order);
    memset(symbols, 0, sizeof(*symbols));
}

int
SSC_SymbolsAdd(
    struct SSC_Symbols* symbols,
    const char* name,
    int32_t value,
    enum SSC_SymbolKind kind,
    const char* text)
{
    struct SSC_Symbol* entry;

    if( !name || !*name )
        return 0;

    if( symbols->count == symbols->capacity )
    {
        int capacity = symbols->capacity ? symbols->capacity * 2 : 1024;
        struct SSC_Symbol* grown =
            (struct SSC_Symbol*)realloc(symbols->entries, (size_t)capacity * sizeof(*grown));

        if( !grown )
            return 0;
        symbols->entries = grown;
        symbols->capacity = capacity;
    }

    entry = &symbols->entries[symbols->count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->value = value;
    entry->kind = kind;
    entry->text = text ? strdup(text) : NULL;

    symbols->sorted = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

static struct SSC_Symbols* g_sort_symbols;

static int
cmp_order(
    const void* a,
    const void* b)
{
    const struct SSC_Symbol* x = &g_sort_symbols->entries[*(const int32_t*)a];
    const struct SSC_Symbol* y = &g_sort_symbols->entries[*(const int32_t*)b];
    int order = strcmp(x->name, y->name);

    if( order )
        return order;
    /* Same name in two namespaces is normal (an interface and a loc can share
     * one); order by kind so a kind-qualified lookup can binary-search to the
     * right one. */
    return (int)x->kind - (int)y->kind;
}

static void
ensure_sorted(struct SSC_Symbols* symbols)
{
    int i;

    if( symbols->sorted || symbols->count == 0 )
        return;

    symbols->order = (int32_t*)realloc(symbols->order, (size_t)symbols->count * sizeof(int32_t));
    if( !symbols->order )
        return;
    for( i = 0; i < symbols->count; i++ )
        symbols->order[i] = i;

    g_sort_symbols = symbols;
    qsort(symbols->order, (size_t)symbols->count, sizeof(int32_t), cmp_order);
    g_sort_symbols = NULL;
    symbols->sorted = 1;
}

const struct SSC_Symbol*
SSC_SymbolsFind(
    struct SSC_Symbols* symbols,
    const char* name,
    enum SSC_SymbolKind kind)
{
    int lo;
    int hi;

    if( !symbols || !name )
        return NULL;
    ensure_sorted(symbols);
    if( !symbols->order )
        return NULL;

    lo = 0;
    hi = symbols->count - 1;
    while( lo <= hi )
    {
        int mid = lo + ((hi - lo) / 2);
        const struct SSC_Symbol* entry = &symbols->entries[symbols->order[mid]];
        int order = strcmp(entry->name, name);

        if( order < 0 )
        {
            lo = mid + 1;
        }
        else if( order > 0 )
        {
            hi = mid - 1;
        }
        else
        {
            int i = mid;

            /* Walk to the first entry with this name, then take the first that
             * matches the requested kind (or any, when none was requested). */
            while( i > 0 && strcmp(symbols->entries[symbols->order[i - 1]].name, name) == 0 )
                i--;
            for( ; i < symbols->count; i++ )
            {
                const struct SSC_Symbol* candidate = &symbols->entries[symbols->order[i]];

                if( strcmp(candidate->name, name) != 0 )
                    break;
                if( kind == SSC_SYM_UNKNOWN || candidate->kind == kind )
                    return candidate;
            }
            return NULL;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Loading                                                             */
/* ------------------------------------------------------------------ */

static void
strip_eol(char* line)
{
    size_t length = strlen(line);

    while( length && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                      line[length - 1] == ' ' || line[length - 1] == '\t') )
        line[--length] = '\0';
}

int
SSC_SymbolsLoadPack(
    struct SSC_Symbols* symbols,
    const char* path,
    enum SSC_SymbolKind kind)
{
    FILE* file = fopen(path, "rb");
    char line[512];
    int loaded = 0;

    if( !file )
        return -1;

    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;
        char* equals;

        /* A generated pack carries a provenance header saying where its ids
         * came from and how to add one. Without comment support that header
         * has to be kept out of the file, which is where it is most useful. */
        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        if( *cursor == '#' || (cursor[0] == '/' && cursor[1] == '/') )
            continue;

        equals = strchr(cursor, '=');
        if( !equals )
            continue;
        *equals = '\0';
        strip_eol(equals + 1);
        if( SSC_SymbolsAdd(symbols, equals + 1, (int32_t)atoi(cursor), kind, NULL) )
            loaded++;
    }
    fclose(file);
    return loaded;
}

int
SSC_SymbolsLoadConstants(
    struct SSC_Symbols* symbols,
    const char* path)
{
    FILE* file = fopen(path, "rb");
    char line[1024];
    int loaded = 0;

    if( !file )
        return -1;

    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;
        char* name;
        char* value;

        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        if( *cursor != '^' )
            continue;

        name = ++cursor;
        while( *cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '=' )
            cursor++;
        if( !*cursor )
            continue;
        *cursor++ = '\0';

        while( *cursor == ' ' || *cursor == '\t' || *cursor == '=' )
            cursor++;
        value = cursor;
        strip_eol(value);

        /* The text is kept verbatim: a constant can expand to a number, a
         * string, a coord literal or another symbol's name, and which one is
         * only decidable where it is used. */
        if( SSC_SymbolsAdd(symbols, name, 0, SSC_SYM_CONSTANT, value) )
            loaded++;
    }
    fclose(file);
    return loaded;
}

/* Map a pack filename onto the namespace it describes. Unlisted packs load as
 * SSC_SYM_UNKNOWN, which still resolves for an unqualified reference. */
static enum SSC_SymbolKind
kind_for_pack(const char* filename)
{
    static const struct
    {
        const char* file;
        enum SSC_SymbolKind kind;
    } k_map[] = {
        { "npc.pack",       SSC_SYM_NPC       },
        { "obj.pack",       SSC_SYM_OBJ       },
        { "loc.pack",       SSC_SYM_LOC       },
        { "inv.pack",       SSC_SYM_INV       },
        { "seq.pack",       SSC_SYM_SEQ       },
        { "spotanim.pack",  SSC_SYM_SPOTANIM  },
        { "interface.pack", SSC_SYM_INTERFACE },
        { "component.pack", SSC_SYM_COMPONENT },
        { "varp.pack",      SSC_SYM_VARP      },
        { "varp_mock.pack", SSC_SYM_VARP      },
        { "varbit.pack",    SSC_SYM_VARBIT    },
        { "varn.pack",      SSC_SYM_VARN      },
        { "vars.pack",      SSC_SYM_VARS      },
        { "enum.pack",      SSC_SYM_ENUM      },
        { "struct.pack",    SSC_SYM_STRUCT    },
        { "dbtable.pack",   SSC_SYM_DBTABLE   },
        { "param.pack",     SSC_SYM_PARAM     },
        { "category.pack",  SSC_SYM_CATEGORY  },
        { "synth.pack",     SSC_SYM_SYNTH     },
        { "stat.pack",      SSC_SYM_STAT      },
        { "script.pack",    SSC_SYM_SCRIPT    },
    };
    size_t i;

    for( i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++ )
    {
        if( strcmp(filename, k_map[i].file) == 0 )
            return k_map[i].kind;
    }
    return SSC_SYM_UNKNOWN;
}

static int
has_suffix(
    const char* name,
    const char* suffix)
{
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);

    return name_length >= suffix_length && strcmp(name + name_length - suffix_length, suffix) == 0;
}

int
SSC_SymbolsLoadPackDir(
    struct SSC_Symbols* symbols,
    const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    int loaded = 0;

    if( !handle )
        return -1;

    while( (entry = readdir(handle)) != NULL )
    {
        char path[1024];
        int count;

        if( !has_suffix(entry->d_name, ".pack") )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        count = SSC_SymbolsLoadPack(symbols, path, kind_for_pack(entry->d_name));
        if( count > 0 )
            loaded += count;
    }
    closedir(handle);
    return loaded;
}

int
SSC_SymbolsLoadConstantDir(
    struct SSC_Symbols* symbols,
    const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    int loaded = 0;

    if( !handle )
        return -1;

    while( (entry = readdir(handle)) != NULL )
    {
        char path[1024];
        struct stat info;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &info) != 0 )
            continue;

        if( S_ISDIR(info.st_mode) )
        {
            int count = SSC_SymbolsLoadConstantDir(symbols, path);

            if( count > 0 )
                loaded += count;
        }
        else if( has_suffix(entry->d_name, ".constant") )
        {
            int count = SSC_SymbolsLoadConstants(symbols, path);

            if( count > 0 )
                loaded += count;
        }
    }
    closedir(handle);
    return loaded;
}

/* ------------------------------------------------------------------ */
/* Database columns                                                    */
/* ------------------------------------------------------------------ */

/*
 * `.dbtable` configs, which are the only source of column indices.
 *
 *   [magic_spell_table]
 *   column=spell,int,INDEXED,REQUIRED
 *   column=spellcom,component
 *
 * A `table:column` reference compiles to (table << 12) | (column << 4), matching
 * how DbOps.ts unpacks it; the low nibble is a tuple index the corpus does not
 * use. The table id comes from dbtable.pack, so that has to be loaded first.
 */
static int
load_dbtable_file(
    struct SSC_Symbols* symbols,
    const char* path)
{
    FILE* file = fopen(path, "rb");
    char line[512];
    char table_name[SSC_MAX_NAME] = "";
    int32_t table_id = -1;
    int column_index = 0;
    int loaded = 0;

    if( !file )
        return 0;

    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;

        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        strip_eol(cursor);

        if( *cursor == '[' )
        {
            char* close = strchr(cursor, ']');
            const struct SSC_Symbol* table;

            if( !close )
                continue;
            *close = '\0';
            snprintf(table_name, sizeof(table_name), "%s", cursor + 1);
            table = SSC_SymbolsFind(symbols, table_name, SSC_SYM_DBTABLE);
            table_id = table ? table->value : -1;
            column_index = 0;
            continue;
        }

        if( strncmp(cursor, "column=", 7) != 0 )
            continue;

        {
            char* name = cursor + 7;
            char* comma = strchr(name, ',');
            char qualified[SSC_MAX_NAME];

            if( comma )
                *comma = '\0';
            if( table_id >= 0 && *name )
            {
                snprintf(qualified, sizeof(qualified), "%s:%s", table_name, name);
                if( SSC_SymbolsAdd(
                        symbols,
                        qualified,
                        (table_id << 12) | (column_index << 4),
                        SSC_SYM_DBCOLUMN,
                        NULL) )
                    loaded++;
            }
            column_index++;
        }
    }
    fclose(file);
    return loaded;
}

int
SSC_SymbolsLoadDbTableDir(
    struct SSC_Symbols* symbols,
    const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    int loaded = 0;

    if( !handle )
        return -1;

    while( (entry = readdir(handle)) != NULL )
    {
        char path[1024];
        struct stat info;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &info) != 0 )
            continue;

        if( S_ISDIR(info.st_mode) )
        {
            int count = SSC_SymbolsLoadDbTableDir(symbols, path);

            if( count > 0 )
                loaded += count;
        }
        else if( has_suffix(entry->d_name, ".dbtable") )
        {
            loaded += load_dbtable_file(symbols, path);
        }
    }
    closedir(handle);
    return loaded;
}

/* ------------------------------------------------------------------ */
/* Built-in enumerations                                               */
/* ------------------------------------------------------------------ */

/*
 * A few argument types are language-level enumerations with no `.pack` file:
 * content writes `npc_setmode(wander)` with no sigil and no symbol table entry.
 * Those names have to come from somewhere, so they are seeded here.
 *
 * Only npc_mode is seeded, because it is the only one whose values are
 * recoverable from the reference tree (engine/src/engine/entity/NpcMode.ts).
 * `stat`, `npc_stat` and `locshape` are also bare-name enumerations, and their
 * value lists are not in the checkout — deliberately NOT guessed. A wrong stat
 * id compiles cleanly and silently reads the wrong skill, which is worse than
 * the compile error content gets today.
 */
void
SSC_SymbolsSeedBuiltins(struct SSC_Symbols* symbols)
{
    static const struct
    {
        const char* name;
        int32_t value;
    } k_npc_mode[] = {
        { "null",            -1 },
        { "none",            0  },
        { "wander",          1  },
        { "patrol",          2  },
        { "playerescape",    3  },
        { "playerfollow",    4  },
        { "playerface",      5  },
        { "playerfaceclose", 6  },
        { "opplayer1",       7  },
        { "opplayer2",       8  },
        { "opplayer3",       9  },
        { "opplayer4",       10 },
        { "opplayer5",       11 },
        { "applayer1",       12 },
        { "applayer2",       13 },
        { "applayer3",       14 },
        { "applayer4",       15 },
        { "applayer5",       16 },
        { "oploc1",          17 },
        { "oploc2",          18 },
        { "oploc3",          19 },
        { "oploc4",          20 },
        { "oploc5",          21 },
        { "aploc1",          22 },
        { "aploc2",          23 },
        { "aploc3",          24 },
        { "aploc4",          25 },
        { "aploc5",          26 },
        { "opobj1",          27 },
        { "opobj2",          28 },
        { "opobj3",          29 },
        { "opobj4",          30 },
        { "opobj5",          31 },
        { "apobj1",          32 },
        { "apobj2",          33 },
        { "apobj3",          34 },
        { "apobj4",          35 },
        { "apobj5",          36 },
        { "opnpc1",          37 },
        { "opnpc2",          38 },
        { "opnpc3",          39 },
        { "opnpc4",          40 },
        { "opnpc5",          41 },
        { "apnpc1",          42 },
        { "apnpc2",          43 },
        { "apnpc3",          44 },
        { "apnpc4",          45 },
        { "apnpc5",          46 },
    };
    size_t i;

    /* RuneScript's locshape names, in the order the cache's loc `shape` field
     * uses. Cross-checked against RSCACHE_LOC_SHAPE_* in
     * 3rd/rscache/src/datatypes/dat2_config_loc.h — the same 0..22 numbering
     * the collision map already relies on. */
    static const char* const k_locshape[] = {
        "wall_straight",
        "wall_diagonalcorner",
        "wall_l",
        "wall_squarecorner",
        "walldecor_straight_nooffset",
        "walldecor_straight_offset",
        "walldecor_diagonal_offset",
        "walldecor_diagonal_nooffset",
        "walldecor_diagonal_both",
        "wall_diagonal",
        "centrepiece_straight",
        "centrepiece_diagonal",
        "roof_straight",
        "roof_diagonal_with_roofedge",
        "roof_diagonal",
        "roof_l_concave",
        "roof_l_convex",
        "roof_flat",
        "roofedge_straight",
        "roofedge_diagonalcorner",
        "roofedge_l",
        "roofedge_squarecorner",
        "grounddecor",
    };

    /* ScriptVarType char codes. A `type`-typed argument is the type itself:
     * `enum(int, string, my_enum, $key)` passes 105 and 115. Values are the
     * char codes from ScriptVarType.getTypeChar in the reference. */
    static const struct
    {
        const char* name;
        int32_t value;
    } k_types[] = {
        { "int",        105 },
        { "string",     115 },
        { "enum",       103 },
        { "obj",        111 },
        { "loc",        108 },
        { "component",  73  },
        { "namedobj",   79  },
        { "struct",     74  },
        { "boolean",    49  },
        { "coord",      99  },
        { "category",   121 },
        { "spotanim",   116 },
        { "npc",        110 },
        { "inv",        118 },
        { "synth",      80  },
        { "seq",        65  },
        { "stat",       83  },
        { "varp",       86  },
        { "player_uid", 112 },
        { "npc_uid",    78  },
        { "interface",  97  },
        { "npc_stat",   254 },
        { "idkit",      75  },
        { "dbrow",      208 },
        { "midi",       77  },
        { "autoint",    255 },
    };

    /* `null` already means -1 in the expression parser, so seeding it here is
     * harmless duplication rather than a conflict. */
    for( i = 0; i < sizeof(k_npc_mode) / sizeof(k_npc_mode[0]); i++ )
    {
        if( !SSC_SymbolsFind(symbols, k_npc_mode[i].name, SSC_SYM_UNKNOWN) )
            SSC_SymbolsAdd(
                symbols, k_npc_mode[i].name, k_npc_mode[i].value, SSC_SYM_NPC_MODE, NULL);
    }
    for( i = 0; i < sizeof(k_locshape) / sizeof(k_locshape[0]); i++ )
    {
        if( !SSC_SymbolsFind(symbols, k_locshape[i], SSC_SYM_UNKNOWN) )
            SSC_SymbolsAdd(symbols, k_locshape[i], (int32_t)i, SSC_SYM_LOCSHAPE, NULL);
    }
    for( i = 0; i < sizeof(k_types) / sizeof(k_types[0]); i++ )
    {
        if( !SSC_SymbolsFind(symbols, k_types[i].name, SSC_SYM_UNKNOWN) )
            SSC_SymbolsAdd(symbols, k_types[i].name, k_types[i].value, SSC_SYM_TYPE, NULL);
    }
}
