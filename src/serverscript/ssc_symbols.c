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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
cmp_order(const void* a, const void* b)
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

    symbols->order =
        (int32_t*)realloc(symbols->order, (size_t)symbols->count * sizeof(int32_t));
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
        char* equals = strchr(line, '=');

        if( !equals )
            continue;
        *equals = '\0';
        strip_eol(equals + 1);
        if( SSC_SymbolsAdd(symbols, equals + 1, (int32_t)atoi(line), kind, NULL) )
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
        { "npc.pack", SSC_SYM_NPC },
        { "obj.pack", SSC_SYM_OBJ },
        { "loc.pack", SSC_SYM_LOC },
        { "inv.pack", SSC_SYM_INV },
        { "seq.pack", SSC_SYM_SEQ },
        { "spotanim.pack", SSC_SYM_SPOTANIM },
        { "interface.pack", SSC_SYM_INTERFACE },
        { "component.pack", SSC_SYM_COMPONENT },
        { "varp.pack", SSC_SYM_VARP },
        { "varbit.pack", SSC_SYM_VARBIT },
        { "varn.pack", SSC_SYM_VARN },
        { "enum.pack", SSC_SYM_ENUM },
        { "struct.pack", SSC_SYM_STRUCT },
        { "param.pack", SSC_SYM_PARAM },
        { "category.pack", SSC_SYM_CATEGORY },
        { "synth.pack", SSC_SYM_SYNTH },
        { "stat.pack", SSC_SYM_STAT },
        { "script.pack", SSC_SYM_SCRIPT },
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
has_suffix(const char* name, const char* suffix)
{
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);

    return name_length >= suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
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
