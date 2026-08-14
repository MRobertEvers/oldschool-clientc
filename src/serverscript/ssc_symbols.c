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

#include "content/content_register.h"

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
    {
        free(symbols->entries[i].text);
        free(symbols->entries[i].origin);
    }
    free(symbols->entries);
    free(symbols->order);
    free(symbols->carriers);
    free(symbols->exempt);
    free(symbols->read_exempt);
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

/**
 * The kinds that share one RuneScript `%name` domain, as `content.ini`'s
 * `vardomain` column declares them.
 *
 * Read from the register rather than listed here, for the reason §8.5 of
 * CONTENT_ARCHITECTURE.md gives about the allocator: a second copy of a
 * membership list is a list that silently stops agreeing. Defaults only — the
 * compiler is handed `--pack DIR` paths, not a tree root, so it has no
 * `content.ini` to overlay, exactly as `kind_for_pack` already assumes.
 */
static int
shared_var_kinds(unsigned char* out)
{
    static struct ContentRegister registry;
    static int loaded;
    int count = 0;

    if( !loaded )
    {
        ContentRegister_Defaults(&registry);
        loaded = 1;
    }
    for( int i = 0; i < registry.count; i++ )
    {
        enum SSC_SymbolKind kind;

        if( !registry.entries[i].shared_var_domain )
            continue;
        kind = SSC_SymbolKindForNamespace(registry.entries[i].name);
        if( kind == SSC_SYM_UNKNOWN )
            continue;
        out[kind] = 1;
        count++;
    }
    return count;
}

/** The namespace a kind is spelled with, for diagnostics. Never NULL. */
static const char*
kind_label(enum SSC_SymbolKind kind)
{
    static struct ContentRegister registry;
    static int loaded;

    if( !loaded )
    {
        ContentRegister_Defaults(&registry);
        loaded = 1;
    }
    for( int i = 0; i < registry.count; i++ )
    {
        if( SSC_SymbolKindForNamespace(registry.entries[i].name) == kind )
            return registry.entries[i].name;
    }
    return "?";
}

int
SSC_SymbolsValidate(struct SSC_Symbols* symbols)
{
    unsigned char in_domain[SSC_SYM_KIND_COUNT];
    int problems = 0;
    int domain_count;

    if( !symbols || symbols->count < 2 )
        return 0;
    memset(in_domain, 0, sizeof(in_domain));
    domain_count = shared_var_kinds(in_domain);

    /*
     * Rule 0 — an exemption has to exempt something.
     *
     * `wholewrite=allow` on a varp nothing is based on is an assertion that a
     * variable is shared when it is not, and it reads to the next person as
     * evidence the write was thought about. An escape hatch that can be left
     * behind after the thing it excused is gone stops meaning anything, so it is
     * checked the same way the rule it escapes is.
     */
    for( int i = 0; i < symbols->exempt_count; i++ )
    {
        if( SSC_SymbolsCarrier(symbols, symbols->exempt[i]) )
            continue;
        fprintf(stderr,
                "sscompile: varp %d declares `wholewrite=allow` but no varbit is based on "
                "it — there is nothing to exempt\n",
                symbols->exempt[i]);
        problems++;
    }
    for( int i = 0; i < symbols->read_exempt_count; i++ )
    {
        if( SSC_SymbolsCarrier(symbols, symbols->read_exempt[i]) )
            continue;
        fprintf(stderr,
                "sscompile: varp %d declares `wholeread=allow` but no varbit is based on "
                "it — there is nothing to exempt\n",
                symbols->read_exempt[i]);
        problems++;
    }

    ensure_sorted(symbols);
    if( !symbols->order )
        return 0;

    for( int i = 1; i < symbols->count; i++ )
    {
        const struct SSC_Symbol* previous = &symbols->entries[symbols->order[i - 1]];
        const struct SSC_Symbol* current = &symbols->entries[symbols->order[i]];

        if( strcmp(previous->name, current->name) != 0 )
            continue;

        /*
         * Rule 1 — a constant means one thing.
         *
         * Any second declaration, not only a disagreeing one: that is what the
         * server's loader does, and a compiler that is more permissive than the
         * runtime it feeds is a compiler whose output cannot be trusted to
         * describe the tree. The two texts are printed because the interesting
         * case is the one where they differ and nobody would have looked.
         */
        if( previous->kind == SSC_SYM_CONSTANT && current->kind == SSC_SYM_CONSTANT )
        {
            fprintf(stderr,
                    "sscompile: `^%s` is declared twice — %s says `%s`, %s says `%s`; a "
                    "constant means one thing\n",
                    current->name, previous->origin ? previous->origin : "?",
                    previous->text ? previous->text : "",
                    current->origin ? current->origin : "?", current->text ? current->text : "");
            problems++;
            continue;
        }

        /*
         * Rule 2 — `%name` says which variable, not which kind of variable.
         *
         * An ambiguity here is not a precedence question to settle in the
         * resolver; it is a name that has to be spelled differently. Both kinds
         * are named so the fix is obvious without a second lookup.
         */
        if( domain_count > 1 && previous->kind != current->kind &&
            previous->kind < SSC_SYM_KIND_COUNT && current->kind < SSC_SYM_KIND_COUNT &&
            in_domain[previous->kind] && in_domain[current->kind] )
        {
            fprintf(stderr,
                    "sscompile: `%%%s` is ambiguous — %s %d and %s %d share one RuneScript "
                    "name domain (content.ini `vardomain`), so `%%%s` cannot say which\n",
                    current->name, kind_label(previous->kind), previous->value,
                    kind_label(current->kind), current->value, current->name);
            problems++;
        }
    }
    return problems;
}

/*
 * The shared lookup. `allow_constant` is what separates the two entry points
 * below; everything else is one binary search over the name-sorted order.
 */
static const struct SSC_Symbol*
find_symbol(
    struct SSC_Symbols* symbols,
    const char* name,
    enum SSC_SymbolKind kind,
    int allow_constant)
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
                if( !allow_constant && candidate->kind == SSC_SYM_CONSTANT )
                    continue;
                if( kind == SSC_SYM_UNKNOWN || candidate->kind == kind )
                    return candidate;
            }
            return NULL;
        }
    }
    return NULL;
}

const struct SSC_Symbol*
SSC_SymbolsFind(
    struct SSC_Symbols* symbols,
    const char* name,
    enum SSC_SymbolKind kind)
{
    return find_symbol(symbols, name, kind, 1);
}

const struct SSC_Symbol*
SSC_SymbolsFindValue(struct SSC_Symbols* symbols, const char* name)
{
    return find_symbol(symbols, name, SSC_SYM_UNKNOWN, 0);
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
        char* trailing;

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
        /* A trailing `//` comment. Layer-1 packs use one to carry the layer-0
         * name an alias stands in for (`3254=guard  // cache: guard1`), which
         * is what makes the line readable as an alias rather than as an
         * unexamined claim about an id. Cut it before the name is taken. */
        trailing = strstr(equals + 1, "//");
        if( trailing )
            *trailing = '\0';
        strip_eol(equals + 1);
        if( !equals[1] )
            continue;
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
    int line_number = 0;

    if( !file )
        return -1;

    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;
        char* name;
        char* value;

        line_number++;
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
        {
            char origin[1200];

            /* Where it was declared, so SSC_SymbolsValidate can name both sites
             * of a duplicate. A constant is authored text; every other symbol
             * kind comes out of a generated index where "which line" answers
             * nothing. */
            snprintf(origin, sizeof(origin), "%s:%d", path, line_number);
            symbols->entries[symbols->count - 1].origin = strdup(origin);
            loaded++;
        }
    }
    fclose(file);
    return loaded;
}

/*
 * Map a pack filename onto the namespace it describes.
 *
 * *Which* namespaces exist is the register's answer (src/content, and through
 * it a tree's `content.ini`); this only says which of the compiler's own symbol
 * kinds each one becomes. Unlisted packs load as SSC_SYM_UNKNOWN, which still
 * resolves for an unqualified reference.
 *
 * The filename table this replaced had `varp_mock.pack` hardcoded — the one place
 * in the compiler that knew a *specific tree's* filename. That is exactly the
 * coupling the register removes: a namespace is one file, `pack/<ns>.pack`
 * (docs/CONTENT_PACK_PLAN.md §3), so there is nothing tree-specific left to list.
 */
enum SSC_SymbolKind
SSC_SymbolKindForNamespace(const char* ns)
{
    static const struct
    {
        const char* ns;
        enum SSC_SymbolKind kind;
    } k_map[] = {
        { "npc",       SSC_SYM_NPC       },
        { "obj",       SSC_SYM_OBJ       },
        { "loc",       SSC_SYM_LOC       },
        { "inv",       SSC_SYM_INV       },
        { "seq",       SSC_SYM_SEQ       },
        { "spotanim",  SSC_SYM_SPOTANIM  },
        /*
         * A namespace whose pack lists the archives of a cache index carries that
         * index in its name. The old spellings stay as aliases: another tree's
         * `content.ini` may still declare them, and an alias costs one line where
         * a missing one costs every symbol of that kind — which is exactly what
         * happened when these three were renamed and this table was not.
         */
        { "3_interfaces",     SSC_SYM_INTERFACE },
        { "4_soundeffects",   SSC_SYM_SYNTH     },
        { "11_musicjingles",  SSC_SYM_JINGLE    },
        { "12_clientscripts", SSC_SYM_SCRIPT    },
        { "13_fonts",         SSC_SYM_FONTMETRICS },
        { "interface", SSC_SYM_INTERFACE },
        { "component", SSC_SYM_COMPONENT },
        { "varp",      SSC_SYM_VARP      },
        { "varbit",    SSC_SYM_VARBIT    },
        { "varn",      SSC_SYM_VARN      },
        { "vars",      SSC_SYM_VARS      },
        { "enum",      SSC_SYM_ENUM      },
        { "struct",    SSC_SYM_STRUCT    },
        { "dbtable",   SSC_SYM_DBTABLE   },
        /* Listed for the shadowing, not for completeness: 102 dbrows in this
         * tree are named after the obj they describe, and an unlisted pack
         * loads as UNKNOWN, which answers a lookup of any kind. See
         * SSC_SYM_DBROW for what that cost. */
        { "dbrow",     SSC_SYM_DBROW     },
        { "param",     SSC_SYM_PARAM     },
        { "category",  SSC_SYM_CATEGORY  },
        { "synth",     SSC_SYM_SYNTH     },
        { "midi",      SSC_SYM_JINGLE    },
        { "jingle",    SSC_SYM_JINGLE    },
        { "stat",      SSC_SYM_STAT      },
        { "fontmetrics", SSC_SYM_FONTMETRICS },
        { "script",    SSC_SYM_SCRIPT    },
    };
    size_t i;

    for( i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++ )
    {
        if( strcmp(ns, k_map[i].ns) == 0 )
            return k_map[i].kind;
    }
    return SSC_SYM_UNKNOWN;
}

/**
 * 1 when this index names the *archives* of a cache index rather than records
 * inside one — `pack/7_models.pack`, `pack/2_configs.pack`. The register states it
 * as `cache_index`, so this reads the fact rather than pattern-matching the name.
 */
static int
pack_is_archive_level(const char* filename)
{
    static struct ContentRegister registry;
    static int loaded;
    const struct ContentNamespace* ns;

    if( !loaded )
    {
        ContentRegister_Defaults(&registry);
        loaded = 1;
    }
    ns = ContentRegister_ForPackFile(&registry, filename);
    return ns && ns->cache_index >= 0;
}

static enum SSC_SymbolKind
kind_for_pack(const char* filename)
{
    static struct ContentRegister registry;
    static int loaded;
    const struct ContentNamespace* ns;

    /* Defaults only: the compiler is handed `--pack DIR` paths rather than a
     * tree root, so it has no `content.ini` to read. The defaults are the union
     * of what every consumer knows, which is what makes them safe here — a
     * namespace a tree adds without telling the compiler simply resolves
     * unqualified, exactly as an unlisted pack always did. */
    if( !loaded )
    {
        ContentRegister_Defaults(&registry);
        loaded = 1;
    }

    ns = ContentRegister_ForPackFile(&registry, filename);
    if( !ns )
        return SSC_SYM_UNKNOWN;
    return SSC_SymbolKindForNamespace(ns->name);
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

/* dirent's d_type is a BSD/Linux extension MinGW's dirent lacks, so classify by
 * path with stat() instead -- portable across the unix and win32 builds. Same
 * shape as mock230_path_is_dir() in src/net/mock/mock230_content.c. */
static int
ssc_path_is_dir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
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
        enum SSC_SymbolKind kind;

        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        /*
         * Recurse. `SSC_SymbolsLoadConstantDir` below already does, and this one
         * not doing so is why nothing under a subdirectory was ever reachable.
         */
        if( ssc_path_is_dir(path) )
        {
            if( entry->d_name[0] != '.' )
            {
                int nested = SSC_SymbolsLoadPackDir(symbols, path);

                if( nested > 0 )
                    loaded += nested;
            }
            continue;
        }
        /*
         * Three extensions, because there are two levels of index plus a ledger:
         * `pack/7_models.pack` names the archives of a cache index,
         * `configs/all.npc.compack` names the records inside one archive, and
         * `pack/<ns>.alloc` is the server's allocation ledger — the ids
         * `ss_allocate.py` handed out past the cache's high-water mark. All are
         * `id=name` and all are ours to read. Filtering on `.pack` alone
         * silently dropped every config type; dropping `.alloc` would load a
         * server dbtable's columns typed and lose the table's own name.
         */
        if( !has_suffix(entry->d_name, ".pack") && !has_suffix(entry->d_name, ".compack") &&
            !has_suffix(entry->d_name, ".alloc") )
            continue;
        kind = kind_for_pack(entry->d_name);
        /*
         * An *archive-level* index whose names the language cannot refer to is not
         * a symbol source.
         *
         * `pack/` holds one index per cache index. Some of those do name symbols —
         * `3_interfaces.pack` is archive-level and interfaces are addressable — but
         * most name things RuneScript has no way to mention: 61,615 models, 8,534
         * sprites, and `2_configs.pack`, whose twenty entries are config *group*
         * names. Loading those files every name under SSC_SYM_UNKNOWN, where an
         * unqualified lookup can pick one up: `2_configs.pack` says `5=inv` while
         * `configs/all.inv.compack` says `93=inv`, and the wrong one won — silently,
         * in ten places across the compiled scripts.
         *
         * The test is both halves. A member-level index with no kind (`hitsplat`)
         * still loads, because its names *are* referenced and resolve unqualified.
         */
        if( kind == SSC_SYM_UNKNOWN && pack_is_archive_level(entry->d_name) )
            continue;
        count = SSC_SymbolsLoadPack(symbols, path, kind);
        if( count > 0 )
            loaded += count;
    }
    closedir(handle);
    return loaded;
}

/**
 * Component symbols, composed the way the client addresses a component.
 *
 * There is no `pack/component.pack` to read — it was removed once the cache's own
 * component names went into `interfaces/<name>.compack`, which is the member index
 * over exactly those children. A compack binds a *local* child id, so loading one
 * directly would bind `items` to 12 rather than to bankmain's twelfth child. The id
 * is `(interface << 16) | child`, and composing it needs both files:
 * `pack/3_interfaces.pack` says `bankmain` is 12, the compack says `items` is child
 * 12, and the symbol is 786444.
 *
 * Call after the pack directories, since it reads the interface symbols they load.
 * Mirrors `load_component_symbols` in `src/net/mock/mock230_content.c` — the server
 * was taught this and the compiler was not, which is why every component reference
 * in RuneScript stopped resolving.
 */
int
SSC_SymbolsLoadComponentDir(
    struct SSC_Symbols* symbols,
    const char* content_dir)
{
    int loaded = 0;
    int scanned = 0;

    /* Snapshot the interface ids first: loading children appends to `entries`,
     * which may realloc it out from under a live pointer. */
    for( int i = 0; i < symbols->count; i++ )
    {
        if( symbols->entries[i].kind == SSC_SYM_INTERFACE )
            scanned++;
    }
    if( scanned == 0 )
        return 0;

    struct
    {
        char name[SSC_MAX_NAME];
        int id;
    }* ifaces = calloc((size_t)scanned, sizeof(*ifaces));
    int count = 0;

    if( !ifaces )
        return 0;
    for( int i = 0; i < symbols->count && count < scanned; i++ )
    {
        if( symbols->entries[i].kind != SSC_SYM_INTERFACE )
            continue;
        snprintf(ifaces[count].name, sizeof(ifaces[count].name), "%s",
                 symbols->entries[i].name);
        ifaces[count].id = symbols->entries[i].value;
        count++;
    }

    for( int i = 0; i < count; i++ )
    {
        struct SSC_Symbols children;
        char path[1024];

        if( ifaces[i].id < 0 )
            continue;
        snprintf(path, sizeof(path), "%s/interfaces/%s.compack", content_dir,
                 ifaces[i].name);
        SSC_SymbolsInit(&children);
        if( SSC_SymbolsLoadPack(&children, path, SSC_SYM_COMPONENT) > 0 )
        {
            for( int c = 0; c < children.count; c++ )
            {
                char full[SSC_MAX_NAME];

                if( children.entries[c].value < 0 || children.entries[c].value > 0xffff )
                    continue;
                snprintf(full, sizeof(full), "%s:%s", ifaces[i].name,
                         children.entries[c].name);
                if( SSC_SymbolsAdd(symbols, full,
                                   (ifaces[i].id << 16) | children.entries[c].value,
                                   SSC_SYM_COMPONENT, NULL) )
                    loaded++;
            }
        }
        SSC_SymbolsFree(&children);
    }
    free(ifaces);
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
/* Carrier varps                                                       */
/* ------------------------------------------------------------------ */

/*
 * Which varps have varbits packed into them.
 *
 * The relation is one key — `basevar=` on a varbit record — and it exists in
 * exactly one place, `configs/all.<varbit>`, written by `cachepack unpack` out of
 * config group 14. Nothing here re-derives it, guesses it, or keeps a list; the
 * compiler reads the same file `mock230_varbit.c`'s header calls the authority.
 *
 * The filename is composed from the register rather than typed, so a tree that
 * spells the namespace differently is still read: `ContentRegister` says which
 * namespace becomes SSC_SYM_VARBIT and the record file is `all.<that>`, the same
 * convention `all.<ns>.compack` already follows one level down.
 */
static const char*
varbit_namespace(void)
{
    static struct ContentRegister registry;
    static int loaded;

    if( !loaded )
    {
        ContentRegister_Defaults(&registry);
        loaded = 1;
    }
    for( int i = 0; i < registry.count; i++ )
    {
        if( SSC_SymbolKindForNamespace(registry.entries[i].name) == SSC_SYM_VARBIT )
            return registry.entries[i].name;
    }
    return NULL;
}

static struct SSC_VarpCarrier*
carrier_slot(
    struct SSC_Symbols* symbols,
    int32_t varp)
{
    for( int i = 0; i < symbols->carrier_count; i++ )
    {
        if( symbols->carriers[i].varp == varp )
            return &symbols->carriers[i];
    }
    if( symbols->carrier_count == symbols->carrier_capacity )
    {
        int next = symbols->carrier_capacity ? symbols->carrier_capacity * 2 : 256;
        struct SSC_VarpCarrier* grown =
            (struct SSC_VarpCarrier*)realloc(symbols->carriers, (size_t)next * sizeof(*grown));

        if( !grown )
            return NULL;
        symbols->carriers = grown;
        symbols->carrier_capacity = next;
    }
    {
        struct SSC_VarpCarrier* slot = &symbols->carriers[symbols->carrier_count++];

        memset(slot, 0, sizeof(*slot));
        slot->varp = varp;
        return slot;
    }
}

const struct SSC_VarpCarrier*
SSC_SymbolsCarrier(
    const struct SSC_Symbols* symbols,
    int32_t varp)
{
    if( !symbols )
        return NULL;
    for( int i = 0; i < symbols->carrier_count; i++ )
    {
        if( symbols->carriers[i].varp == varp )
            return &symbols->carriers[i];
    }
    return NULL;
}

int
SSC_SymbolsLoadVarbitBases(
    struct SSC_Symbols* symbols,
    const char* dir)
{
    const char* ns = varbit_namespace();
    char path[1024];
    FILE* file;
    char line[512];
    char varbit_name[SSC_MAX_NAME] = "";
    int start_bit = -1;
    int end_bit = -1;
    int32_t base = -1;

    if( !symbols || !dir || !ns )
        return -1;
    snprintf(path, sizeof(path), "%s/all.%s", dir, ns);
    file = fopen(path, "rb");
    if( !file )
        return -1;

    /*
     * The keys arrive in whichever order the record states them, so a block is
     * only committed when the next header (or EOF) proves it complete. Committing
     * on `basevar=` alone would drop the bit range from the diagnostic for every
     * record that states the range afterwards.
     */
#define COMMIT_VARBIT()                                                                            \
    do                                                                                             \
    {                                                                                              \
        if( base >= 0 && *varbit_name )                                                            \
        {                                                                                          \
            struct SSC_VarpCarrier* slot = carrier_slot(symbols, base);                            \
                                                                                                   \
            if( slot )                                                                             \
            {                                                                                      \
                if( slot->sample_count < SSC_CARRIER_SAMPLES )                                     \
                {                                                                                  \
                    int s = slot->sample_count++;                                                  \
                                                                                                   \
                    snprintf(slot->sample[s], sizeof(slot->sample[s]), "%s", varbit_name);          \
                    slot->sample_start[s] = start_bit;                                             \
                    slot->sample_end[s] = end_bit;                                                 \
                }                                                                                  \
                slot->bits++;                                                                      \
            }                                                                                      \
        }                                                                                          \
        base = -1;                                                                                 \
        start_bit = -1;                                                                            \
        end_bit = -1;                                                                              \
    } while( 0 )

    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;

        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        strip_eol(cursor);

        if( *cursor == '[' )
        {
            char* close = strchr(cursor, ']');

            COMMIT_VARBIT();
            if( !close )
                continue;
            *close = '\0';
            snprintf(varbit_name, sizeof(varbit_name), "%s", cursor + 1);
            continue;
        }
        if( strncmp(cursor, "basevar=", 8) == 0 )
        {
            const struct SSC_Symbol* varp =
                SSC_SymbolsFind(symbols, cursor + 8, SSC_SYM_VARP);

            base = varp ? varp->value : -1;
        }
        else if( strncmp(cursor, "startbit=", 9) == 0 )
            start_bit = atoi(cursor + 9);
        else if( strncmp(cursor, "endbit=", 7) == 0 )
            end_bit = atoi(cursor + 7);
    }
    COMMIT_VARBIT();
#undef COMMIT_VARBIT
    fclose(file);
    return symbols->carrier_count;
}

/*
 * `wholewrite=allow` / `wholeread=allow` on a `[varp]` block. Returns 1 when it
 * was recorded.
 *
 * `reads_only` picks which of the two lists it lands in. They are separate all
 * the way down rather than one flag with a level, because the question each
 * answers is different: a whole write destroys a neighbour's variable, a whole
 * read merely gives back the packed word. A single key would have meant buying
 * silence on the second by opening the first.
 */
static int
exempt_varp(
    struct SSC_Symbols* symbols,
    int32_t varp,
    int reads_only)
{
    int32_t** list = reads_only ? &symbols->read_exempt : &symbols->exempt;
    int* count = reads_only ? &symbols->read_exempt_count : &symbols->exempt_count;
    int* capacity = reads_only ? &symbols->read_exempt_capacity : &symbols->exempt_capacity;

    for( int i = 0; i < *count; i++ )
    {
        if( (*list)[i] == varp )
            return 0;
    }
    if( *count == *capacity )
    {
        int next = *capacity ? *capacity * 2 : 16;
        int32_t* grown = (int32_t*)realloc(*list, (size_t)next * sizeof(*grown));

        if( !grown )
            return 0;
        *list = grown;
        *capacity = next;
    }
    (*list)[(*count)++] = varp;
    /* Only a varp that *is* a carrier gets the flag — an exemption on a varp
     * nothing is based on has nothing to exempt, and the list above keeps it so
     * that can be said out loud rather than accepted in silence. */
    for( int i = 0; i < symbols->carrier_count; i++ )
    {
        if( symbols->carriers[i].varp != varp )
            continue;
        if( reads_only )
            symbols->carriers[i].read_exempt = 1;
        else
            symbols->carriers[i].exempt = 1;
    }
    return 1;
}

static int
load_varp_decl_file(
    struct SSC_Symbols* symbols,
    const char* path)
{
    FILE* file = fopen(path, "rb");
    char line[512];
    int32_t varp = -1;
    int loaded = 0;

    if( !file )
        return 0;
    while( fgets(line, sizeof(line), file) )
    {
        char* cursor = line;
        char* comment;

        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        comment = strstr(cursor, "//");
        if( comment )
            *comment = '\0';
        strip_eol(cursor);

        if( *cursor == '[' )
        {
            char* close = strchr(cursor, ']');
            const struct SSC_Symbol* symbol;

            varp = -1;
            if( !close )
                continue;
            *close = '\0';
            symbol = SSC_SymbolsFind(symbols, cursor + 1, SSC_SYM_VARP);
            varp = symbol ? symbol->value : -1;
            continue;
        }
        if( varp >= 0 && strncmp(cursor, "wholewrite=", 11) == 0 &&
            strcmp(cursor + 11, "allow") == 0 )
            loaded += exempt_varp(symbols, varp, 0);
        else if( varp >= 0 && strncmp(cursor, "wholeread=", 10) == 0 &&
                 strcmp(cursor + 10, "allow") == 0 )
            loaded += exempt_varp(symbols, varp, 1);
    }
    fclose(file);
    return loaded;
}

int
SSC_SymbolsLoadVarpDecls(
    struct SSC_Symbols* symbols,
    const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    int loaded = 0;

    if( !handle )
        return 0;
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
            loaded += SSC_SymbolsLoadVarpDecls(symbols, path);
        else if( has_suffix(entry->d_name, ".varp") )
            loaded += load_varp_decl_file(symbols, path);
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
    /*
     * Type names are seeded per KIND, not per name.
     *
     * The `SSC_SYM_UNKNOWN` guard the other seed loops use matches any kind, and
     * for a type name that is also a pack symbol that means the type is never
     * registered at all. `inv` is exactly that collision: the backpack container
     * is *named* `inv` (pack/inv.pack), so `SSC_SYM_INV` claimed the name and
     * the `inv` TYPE never existed — which made `(inv $x)` in a header record
     * its param type as `int` (parse_header_lists' "a type the symbol table does
     * not know is int" fallback). Nothing in a compiled script noticed, because
     * an inv rides the int stack either way; what it broke is the one consumer
     * that reads `param_types` back, `mock230_scripts_run_debugproc`, which then
     * ran `strtol("collection_transmit")` and passed container 0.
     *
     * Lookups are kind-specific (SSC_SymbolsFind walks every entry with the
     * name and takes the first of the requested kind), so both entries can
     * coexist: `inv` as a value still resolves to the container.
     */
    for( i = 0; i < sizeof(k_types) / sizeof(k_types[0]); i++ )
    {
        if( !SSC_SymbolsFind(symbols, k_types[i].name, SSC_SYM_TYPE) )
            SSC_SymbolsAdd(symbols, k_types[i].name, k_types[i].value, SSC_SYM_TYPE, NULL);
    }
}
