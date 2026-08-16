#include "cs2_names.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cs2_name_file
{
    enum RSCache_CS2_NameTable table;
    const char* file_name;
};

static const struct cs2_name_file cs2_name_files[] = {
    { RSCACHE_CS2_NAMES_BOOLEAN, "boolean-names.tsv" },
    { RSCACHE_CS2_NAMES_FONTMETRICS, "fontmetrics-names.tsv" },
    { RSCACHE_CS2_NAMES_GRAPHIC, "graphic-names.tsv" },
    { RSCACHE_CS2_NAMES_INTERFACE, "interface-names.tsv" },
    { RSCACHE_CS2_NAMES_INV, "inv-names.tsv" },
    { RSCACHE_CS2_NAMES_LOC, "loc-names.tsv" },
    { RSCACHE_CS2_NAMES_MAPAREA, "maparea-names.tsv" },
    { RSCACHE_CS2_NAMES_MODEL, "model-names.tsv" },
    { RSCACHE_CS2_NAMES_NPC, "npc-names.tsv" },
    { RSCACHE_CS2_NAMES_OBJ, "obj-names.tsv" },
    { RSCACHE_CS2_NAMES_PARAM, "param-names.tsv" },
    { RSCACHE_CS2_NAMES_SEQ, "seq-names.tsv" },
    { RSCACHE_CS2_NAMES_STAT, "stat-names.tsv" },
    { RSCACHE_CS2_NAMES_STRUCT, "struct-names.tsv" },
    { RSCACHE_CS2_NAMES_SYNTH, "synth-names.tsv" },
    { RSCACHE_CS2_NAMES_CHATFILTER, "chatfilter-names.tsv" },
    { RSCACHE_CS2_NAMES_CHATTYPE, "chattype-names.tsv" },
    { RSCACHE_CS2_NAMES_CLIENTTYPE, "clienttype-names.tsv" },
    { RSCACHE_CS2_NAMES_IFTYPE, "iftype-names.tsv" },
    { RSCACHE_CS2_NAMES_KEY, "key-names.tsv" },
    { RSCACHE_CS2_NAMES_SETPOSH, "setposh-names.tsv" },
    { RSCACHE_CS2_NAMES_SETPOSV, "setposv-names.tsv" },
    { RSCACHE_CS2_NAMES_SETSIZE, "setsize-names.tsv" },
    { RSCACHE_CS2_NAMES_SETTEXTALIGNH, "settextalignh-names.tsv" },
    { RSCACHE_CS2_NAMES_SETTEXTALIGNV, "settextalignv-names.tsv" },
    { RSCACHE_CS2_NAMES_WINDOWMODE, "windowmode-names.tsv" },
};

#define CS2_NAME_FILE_COUNT ((int)(sizeof(cs2_name_files) / sizeof(cs2_name_files[0])))

void
RSCache_CS2_NamesInit(struct RSCache_CS2_Names* names)
{
    memset(names, 0, sizeof(*names));
    RSCache_CS2_ArenaInit(&names->arena);
    for( int i = 0; i < RSCACHE_CS2_NAMES_TABLE_COUNT; i++ )
        RSCache_CS2_IntMapInit(&names->tables[i]);
    RSCache_CS2_IntMapInit(&names->script_names);
    RSCache_CS2_IntMapInit(&names->param_types);

    /* `false` and `true` are seeded, not loaded.
     *
     * Every other table here is community-recovered data that ships in
     * RuneStar's `*-names.tsv` and is deliberately not vendored (EXCEPTIONS.md
     * G5). These two are not data: they are the language's own words for 0 and
     * 1, `boolean-names.tsv` is exactly two lines long, and a `boolean` has no
     * numeric spelling — so without them 2,472 of osrs239's scripts could not
     * be printed at all by a caller that had no TSVs to point at. `cachepack`
     * is such a caller, which is the whole of why this matters.
     *
     * A TSV that names them anyway overwrites these, as it should. */
    RSCache_CS2_IntMapPut(
        &names->tables[RSCACHE_CS2_NAMES_BOOLEAN], 0,
        RSCache_CS2_ArenaStrDup(&names->arena, "false"));
    RSCache_CS2_IntMapPut(
        &names->tables[RSCACHE_CS2_NAMES_BOOLEAN], 1,
        RSCache_CS2_ArenaStrDup(&names->arena, "true"));

    /* `windowmode` is seeded on the same grounds, and it is the same size of
     * table: two values, `windowmode-names.tsv` is two lines, and they are the
     * dialect's own words rather than community-recovered ids — the client
     * implements exactly `1 fixed` / `2 resizable` and nothing else is
     * spellable. Leaving it to a TSV made `[clientscript,settings_client_mode]`
     * (script 3998, the Display panel's layout dropdown) uncompilable for a
     * caller with no name directory, which is every caller that only has this
     * repo. A loaded TSV still overwrites these. */
    RSCache_CS2_IntMapPut(
        &names->tables[RSCACHE_CS2_NAMES_WINDOWMODE], 1,
        RSCache_CS2_ArenaStrDup(&names->arena, "fixed"));
    RSCache_CS2_IntMapPut(
        &names->tables[RSCACHE_CS2_NAMES_WINDOWMODE], 2,
        RSCache_CS2_ArenaStrDup(&names->arena, "resizable"));
}

void
RSCache_CS2_NamesFree(struct RSCache_CS2_Names* names)
{
    if( !names )
        return;
    for( int i = 0; i < RSCACHE_CS2_NAMES_TABLE_COUNT; i++ )
        RSCache_CS2_IntMapFree(&names->tables[i]);
    RSCache_CS2_IntMapFree(&names->script_names);
    RSCache_CS2_IntMapFree(&names->param_types);
    RSCache_CS2_ArenaFree(&names->arena);
}

/**
 * Read a two-column `id<TAB>value` file, one record per line.
 *
 * `on_row` decides what the value means, so the same reader serves the plain
 * name tables, the script-name table and the param-type table.
 */
static int
cs2_read_tsv(
    struct RSCache_CS2_Names* names,
    const char* path,
    void (*on_row)(struct RSCache_CS2_Names*, void*, int, const char*),
    void* context)
{
    FILE* file = fopen(path, "rb");
    if( !file )
        return 0;

    /* A hand-rolled line reader rather than getline: this library builds for
     * mingw, Emscripten and NXDK, and getline is POSIX-only. */
    char line[4096];
    while( fgets(line, (int)sizeof(line), file) )
    {
        int length = (int)strlen(line);
        while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r') )
            line[--length] = '\0';
        if( length == 0 )
            continue;

        char* tab = strchr(line, '\t');
        if( !tab )
            continue;
        *tab = '\0';
        char* end = NULL;
        long id = strtol(line, &end, 10);
        if( end == line || *end != '\0' )
            continue;
        on_row(names, context, (int)id, tab + 1);
    }
    fclose(file);
    return 1;
}

static void
cs2_row_name(struct RSCache_CS2_Names* names, void* context, int id, const char* value)
{
    struct RSCache_CS2_IntMap* table = (struct RSCache_CS2_IntMap*)context;
    RSCache_CS2_IntMapPut(table, id, RSCache_CS2_ArenaStrDup(&names->arena, value));
}

static void
cs2_row_param_type(struct RSCache_CS2_Names* names, void* context, int id, const char* value)
{
    (void)context;
    /* The file stores the wire descriptor byte as a decimal number; 0 means the
     * param is a plain int, matching the config default. */
    char* end = NULL;
    long desc = strtol(value, &end, 10);
    if( end == value )
        return;
    enum RSCache_CS2_Type type = RSCache_CS2_TypeOfDescAuto((uint8_t)desc);
    if( type == RSCACHE_CS2_TYPE_NONE )
        return;
    RSCache_CS2_IntMapPut(&names->param_types, id, (void*)(intptr_t)(type + 1));
}

int
RSCache_CS2_NamesLoadDirectory(struct RSCache_CS2_Names* names, const char* directory)
{
    if( !directory || !*directory )
        return 0;

    char path[1024];
    int loaded = 0;
    for( int i = 0; i < CS2_NAME_FILE_COUNT; i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", directory, cs2_name_files[i].file_name);
        FILE* probe = fopen(path, "rb");
        if( !probe )
            continue;
        fclose(probe);
        loaded += cs2_read_tsv(names, path, cs2_row_name, &names->tables[cs2_name_files[i].table]);
    }

    snprintf(path, sizeof(path), "%s/script-names.tsv", directory);
    FILE* probe = fopen(path, "rb");
    if( probe )
    {
        fclose(probe);
        loaded += cs2_read_tsv(names, path, cs2_row_name, &names->script_names);
    }

    snprintf(path, sizeof(path), "%s/param-types.tsv", directory);
    probe = fopen(path, "rb");
    if( probe )
    {
        fclose(probe);
        loaded += cs2_read_tsv(names, path, cs2_row_param_type, NULL);
    }
    return loaded;
}

const char*
RSCache_CS2_NamesLookup(
    const struct RSCache_CS2_Names* names,
    enum RSCache_CS2_NameTable table,
    int id)
{
    if( !names || table < 0 || table >= RSCACHE_CS2_NAMES_TABLE_COUNT )
        return NULL;
    return (const char*)RSCache_CS2_IntMapGet(&names->tables[table], id);
}

/**
 * Linear scan of a table for a name.
 *
 * The tables are read once and consulted a few times per script, so an index
 * would cost more to build than the scans it saves; the largest table is
 * ~30,000 entries and only the small constant tables are searched in practice.
 */
static bool
cs2_names_reverse(const struct RSCache_CS2_IntMap* table, const char* name, int* out_id)
{
    for( int i = 0; i < table->capacity; i++ )
    {
        if( !table->entries[i].occupied )
            continue;
        if( strcmp((const char*)table->entries[i].value, name) == 0 )
        {
            *out_id = table->entries[i].key;
            return true;
        }
    }
    return false;
}

bool
RSCache_CS2_NamesLookupId(
    const struct RSCache_CS2_Names* names,
    enum RSCache_CS2_NameTable table,
    const char* name,
    int* out_id)
{
    if( !names || !name || table < 0 || table >= RSCACHE_CS2_NAMES_TABLE_COUNT )
        return false;
    return cs2_names_reverse(&names->tables[table], name, out_id);
}

bool
RSCache_CS2_NamesScriptId(
    const struct RSCache_CS2_Names* names,
    const char* name,
    enum RSCache_CS2_Trigger required,
    int* out_id)
{
    if( !names || !name )
        return false;

    /* Scripts carrying the name under some *other* trigger. Kept rather than
     * skipped so the count is known: one is an answer, several are not. */
    int other_trigger_id = -1;
    int other_trigger_count = 0;

    for( int i = 0; i < names->script_names.capacity; i++ )
    {
        if( !names->script_names.entries[i].occupied )
            continue;
        const char* stored = (const char*)names->script_names.entries[i].value;
        /* An exact bracketed match already names its trigger. */
        if( strcmp(stored, name) == 0 )
        {
            *out_id = names->script_names.entries[i].key;
            return true;
        }

        size_t length = strlen(stored);
        const char* comma = strchr(stored, ',');
        if( !comma || length < 3 || stored[0] != '[' || stored[length - 1] != ']' )
            continue;
        size_t bare_length = (size_t)(stored + length - 1 - comma - 1);
        if( strlen(name) != bare_length || strncmp(comma + 1, name, bare_length) != 0 )
            continue;

        if( required != RSCACHE_CS2_TRIGGER_NONE )
        {
            char trigger[64];
            size_t trigger_length = (size_t)(comma - stored - 1);
            if( trigger_length >= sizeof(trigger) )
                continue;
            memcpy(trigger, stored + 1, trigger_length);
            trigger[trigger_length] = '\0';
            enum RSCache_CS2_Trigger hit = RSCache_CS2_TriggerOfName(trigger);
            if( hit != required )
            {
                /* A ~proc call must not fall back to a sole [clientscript,…]
                 * alias. Tree seed (and trampoline sources) register the
                 * clientscript wrapper under the same bare name as the real
                 * proc that lives only as `.cs2b`; binding ~name to that
                 * wrapper while compiling the wrapper is GOSUB-self. Exact
                 * [proc,…] hits still win above; RuneStar/CACHEPACK_CS2_NAMES
                 * seeds the proc id when the bytecode-only half must recompile.
                 * Other required triggers keep the one-candidate fallback. */
                if( required == RSCACHE_CS2_TRIGGER_PROC &&
                    hit == RSCACHE_CS2_TRIGGER_CLIENTSCRIPT )
                    continue;
                other_trigger_id = names->script_names.entries[i].key;
                other_trigger_count++;
                continue;
            }
        }
        *out_id = names->script_names.entries[i].key;
        return true;
    }

    /*
     * No script carries the name under the required trigger, but exactly one
     * carries it at all — so resolve to that one.
     *
     * The trigger is a fact about the *name table*, not about the bytecode:
     * `gosub_with_params` takes an id and a script record has no trigger field,
     * so a gosub to a script the table calls a clientscript is well-formed and
     * the client runs it. `cs2_write_call_target` already decides this exact
     * disagreement the same way in the other direction — it writes the bare id
     * and says the call site is evidence where the table lags — and a compiler
     * that refuses what the decompiler prints cannot round-trip.
     *
     * The count is what keeps this safe. Ambiguity is the reason the trigger is
     * required at all (`1v1arena_hud_toggle` is clientscript 2716 *and* proc
     * 2717, and binding to whichever was found first would call the wrong
     * script), and that reasoning only bites when there is more than one
     * candidate. With exactly one there is nothing to choose wrongly; with
     * several this still refuses. PROC→CLIENTSCRIPT candidates are excluded
     * above, so a trampoline's sole clientscript alias never lands here.
     */
    if( other_trigger_count == 1 )
    {
        *out_id = other_trigger_id;
        return true;
    }
    return false;
}

const char*
RSCache_CS2_NamesScript(const struct RSCache_CS2_Names* names, int script_id)
{
    if( !names )
        return NULL;
    return (const char*)RSCache_CS2_IntMapGet(&names->script_names, script_id);
}

enum RSCache_CS2_Type
RSCache_CS2_NamesParamType(const struct RSCache_CS2_Names* names, int param_id)
{
    if( !names )
        return RSCACHE_CS2_TYPE_NONE;
    intptr_t stored = (intptr_t)RSCache_CS2_IntMapGet(&names->param_types, param_id);
    if( stored == 0 )
        return RSCACHE_CS2_TYPE_NONE;
    return (enum RSCache_CS2_Type)(stored - 1);
}

void
RSCache_CS2_NamesSetParamType(
    struct RSCache_CS2_Names* names,
    int param_id,
    enum RSCache_CS2_Type type)
{
    if( !names || type == RSCACHE_CS2_TYPE_NONE )
        return;
    RSCache_CS2_IntMapPut(&names->param_types, param_id, (void*)(intptr_t)(type + 1));
}

void
RSCache_CS2_NamesSetScript(
    struct RSCache_CS2_Names* names,
    int script_id,
    const char* name)
{
    assert(names && name);
    assert(script_id >= 0);
    RSCache_CS2_IntMapPut(
        &names->script_names, script_id, RSCache_CS2_ArenaStrDup(&names->arena, name));
}

/* -------------------------------------------------------------------------
 * Constant formatting
 * ---------------------------------------------------------------------- */

struct cs2_colour_constant
{
    int value;
    const char* name;
};

static const struct cs2_colour_constant cs2_colour_constants[] = {
    { 0xFF0000, "^red" },   { 0x00FF00, "^green" },   { 0x0000FF, "^blue" },
    { 0xFFFF00, "^yellow" }, { 0xFF00FF, "^magenta" }, { 0x00FFFF, "^cyan" },
    { 0xFFFFFF, "^white" }, { 0x000000, "^black" },
};

/** `name_<id>` — the fallback spelling for a type whose ids are not unique. */
static void
cs2_write_id_suffixed(const char* name, int id, char* out, int capacity)
{
    snprintf(out, (size_t)capacity, "%s_%d", name, id);
}

/** A constant prefixed with `^`, as the language writes its named constants. */
static bool
cs2_write_prefixed_constant(
    const struct RSCache_CS2_Names* names,
    enum RSCache_CS2_NameTable table,
    const char* prefix,
    int value,
    char* out,
    int capacity,
    bool fall_back_to_number)
{
    const char* name = RSCache_CS2_NamesLookup(names, table, value);
    if( name )
    {
        snprintf(out, (size_t)capacity, "^%s_%s", prefix, name);
        return true;
    }
    if( value == -1 )
    {
        snprintf(out, (size_t)capacity, "null");
        return true;
    }
    if( !fall_back_to_number )
        return false;
    snprintf(out, (size_t)capacity, "%d", value);
    return true;
}

/** The `interface:component` spelling, resolving the interface half by name. */
static bool
cs2_write_component(
    const struct RSCache_CS2_Names* names,
    int value,
    char* out,
    int capacity)
{
    int interface_id = value >> 16;
    const char* name = RSCache_CS2_NamesLookup(names, RSCACHE_CS2_NAMES_INTERFACE, interface_id);
    char interface_text[256];
    if( name )
    {
        snprintf(interface_text, sizeof(interface_text), "%s", name);
    }
    else if( interface_id == -1 )
    {
        snprintf(interface_text, sizeof(interface_text), "null");
    }
    else
    {
        cs2_write_id_suffixed("interface", interface_id, interface_text, sizeof(interface_text));
    }
    snprintf(out, (size_t)capacity, "%s:%d", interface_text, value & 0xFFFF);
    return true;
}

bool
RSCache_CS2_NamesFormatInt(
    const struct RSCache_CS2_Names* names,
    int value,
    enum RSCache_CS2_Type type,
    const char* identifier,
    char* out,
    int out_capacity)
{
    const char* literal = RSCache_CS2_TypeLiteral(type);
    bool named = identifier && strcmp(identifier, literal) != 0;

    /* Identifier-specific spellings come first: these are ints whose *name*,
     * not their type, says how to print them. */
    if( named )
    {
        struct
        {
            const char* identifier;
            enum RSCache_CS2_NameTable table;
            const char* prefix;
        } static const constants[] = {
            { "key", RSCACHE_CS2_NAMES_KEY, "key" },
            { "iftype", RSCACHE_CS2_NAMES_IFTYPE, "iftype" },
            { "setsize", RSCACHE_CS2_NAMES_SETSIZE, "setsize" },
            { "setposh", RSCACHE_CS2_NAMES_SETPOSH, "setpos" },
            { "setposv", RSCACHE_CS2_NAMES_SETPOSV, "setpos" },
            { "settextalignh", RSCACHE_CS2_NAMES_SETTEXTALIGNH, "settextalign" },
            { "settextalignv", RSCACHE_CS2_NAMES_SETTEXTALIGNV, "settextalign" },
            { "chattype", RSCACHE_CS2_NAMES_CHATTYPE, "chattype" },
            { "windowmode", RSCACHE_CS2_NAMES_WINDOWMODE, "windowmode" },
            { "clienttype", RSCACHE_CS2_NAMES_CLIENTTYPE, "clienttype" },
            { "chatfilter", RSCACHE_CS2_NAMES_CHATFILTER, "chatfilter" },
        };
        for( size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); i++ )
        {
            if( strcmp(identifier, constants[i].identifier) == 0 )
                return cs2_write_prefixed_constant(
                    names, constants[i].table, constants[i].prefix, value, out, out_capacity,
                    true);
        }

        if( strcmp(identifier, "colour") == 0 )
        {
            if( value == -1 )
            {
                snprintf(out, (size_t)out_capacity, "null");
                return true;
            }
            for( size_t i = 0;
                 i < sizeof(cs2_colour_constants) / sizeof(cs2_colour_constants[0]);
                 i++ )
            {
                if( cs2_colour_constants[i].value == value )
                {
                    snprintf(out, (size_t)out_capacity, "%s", cs2_colour_constants[i].name);
                    return true;
                }
            }
            /* A colour is 24 bits; anything above them is not a colour, and
             * printing it as one would silently lose the high byte. */
            if( (value >> 24) != 0 )
                return false;
            snprintf(out, (size_t)out_capacity, "0x%06x", value);
            return true;
        }

        if( strcmp(identifier, "bool") == 0 )
        {
            const char* name = RSCache_CS2_NamesLookup(names, RSCACHE_CS2_NAMES_BOOLEAN, value);
            if( name )
            {
                snprintf(out, (size_t)out_capacity, "^%s", name);
                return true;
            }
            if( value == -1 )
            {
                snprintf(out, (size_t)out_capacity, "null");
                return true;
            }
            return false;
        }
    }

    switch( type )
    {
    case RSCACHE_CS2_TYPE_INT:
        if( value == 2147483647 )
            snprintf(out, (size_t)out_capacity, "^max_32bit_int");
        else if( value == (-2147483647 - 1) )
            snprintf(out, (size_t)out_capacity, "^min_32bit_int");
        else
            snprintf(out, (size_t)out_capacity, "%d", value);
        return true;

    case RSCACHE_CS2_TYPE_NPC_UID:
        snprintf(out, (size_t)out_capacity, "%d", value);
        return true;

    case RSCACHE_CS2_TYPE_COORD:
        if( value == -1 )
        {
            snprintf(out, (size_t)out_capacity, "null");
            return true;
        }
        else
        {
            int plane = (int)((unsigned)value >> 28);
            int x = (int)(((unsigned)value >> 14) & 0x3FFF);
            int z = value & 0x3FFF;
            snprintf(
                out, (size_t)out_capacity, "%d_%d_%d_%d_%d", plane, x / 64, z / 64, x & 0x3F,
                z & 0x3F);
            return true;
        }

    case RSCACHE_CS2_TYPE_COMPONENT:
        if( value == -1 )
        {
            snprintf(out, (size_t)out_capacity, "null");
            return true;
        }
        return cs2_write_component(names, value, out, out_capacity);

    case RSCACHE_CS2_TYPE_TYPE:
    {
        enum RSCache_CS2_Type referenced = RSCache_CS2_TypeOfDesc((uint8_t)value);
        if( referenced == RSCACHE_CS2_TYPE_NONE )
            return false;
        snprintf(out, (size_t)out_capacity, "%s", RSCache_CS2_TypeLiteral(referenced));
        return true;
    }

    case RSCACHE_CS2_TYPE_GRAPHIC:
        if( value == -1 )
        {
            snprintf(out, (size_t)out_capacity, "null");
            return true;
        }
        else
        {
            const char* name =
                RSCache_CS2_NamesLookup(names, RSCACHE_CS2_NAMES_GRAPHIC, value);
            if( name )
                snprintf(out, (size_t)out_capacity, "\"%s\"", name);
            else
                snprintf(out, (size_t)out_capacity, "\"graphic_%d\"", value);
            return true;
        }

    /* Ids that are unique but unnamed still have a spelling. */
    case RSCACHE_CS2_TYPE_ENUM:
    case RSCACHE_CS2_TYPE_CATEGORY:
        if( value == -1 )
            snprintf(out, (size_t)out_capacity, "null");
        else
            cs2_write_id_suffixed(literal, value, out, out_capacity);
        return true;

    /* The language has no literal for these, so the number stands — the same
     * departure from upstream, and for the same reason, as `newvar` below.
     * `char` 46 is one script; `area` and `mapelement` join it because the
     * argument is identical and a bare int is what the compiler reads back. */
    case RSCACHE_CS2_TYPE_CHAR:
    case RSCACHE_CS2_TYPE_AREA:
    case RSCACHE_CS2_TYPE_MAPELEMENT:
        if( value == -1 )
            snprintf(out, (size_t)out_capacity, "null");
        else
            snprintf(out, (size_t)out_capacity, "%d", value);
        return true;

    /* Named where the table has one, `<type>_<id>` where it does not.
     *
     * These four used to refuse outright, on the reasoning that they have no
     * numeric spelling in the language. True of `boolean`, whose two values are
     * now seeded (RSCache_CS2_NamesInit) so the table is never short. Not true
     * of the other three: `<literal>_<id>` is a spelling, the compiler already
     * reads it back for every unique-id type, and it round-trips exactly.
     *
     * Refusing cost more than it protected. Without a TSV to hand it took 1,756
     * of osrs239's scripts on `fontmetrics` and `stat` alone; with one it still
     * took 12, because a live cache outruns a community table — `stat_23` is
     * Sailing and `maparea_42` is a map region added after RuneStar's tables
     * were last written. A decompile that says `stat_23` is exact; one that
     * refuses the script says nothing at all. */
    case RSCACHE_CS2_TYPE_BOOLEAN:
    case RSCACHE_CS2_TYPE_STAT:
    case RSCACHE_CS2_TYPE_MAPAREA:
    case RSCACHE_CS2_TYPE_FONTMETRICS:
    {
        enum RSCache_CS2_NameTable table = type == RSCACHE_CS2_TYPE_BOOLEAN
                                               ? RSCACHE_CS2_NAMES_BOOLEAN
                                               : type == RSCACHE_CS2_TYPE_STAT
                                                     ? RSCACHE_CS2_NAMES_STAT
                                                     : type == RSCACHE_CS2_TYPE_MAPAREA
                                                           ? RSCACHE_CS2_NAMES_MAPAREA
                                                           : RSCACHE_CS2_NAMES_FONTMETRICS;
        const char* name = RSCache_CS2_NamesLookup(names, table, value);
        if( name )
        {
            snprintf(out, (size_t)out_capacity, "%s", name);
            return true;
        }
        if( value == -1 )
        {
            snprintf(out, (size_t)out_capacity, "null");
            return true;
        }
        /* `boolean` keeps refusing. Its values are 0 and 1 and both are always
         * named, so a third one is not a missing name — it is a slot the type
         * solver put a non-boolean in, and `boolean_7` would hide that. */
        if( type == RSCACHE_CS2_TYPE_BOOLEAN )
            return false;
        cs2_write_id_suffixed(literal, value, out, out_capacity);
        return true;
    }

    /* Unique ids: named if known, otherwise `<type>_<id>`. */
    case RSCACHE_CS2_TYPE_INV:
    case RSCACHE_CS2_TYPE_SYNTH:
    case RSCACHE_CS2_TYPE_PARAM:
    case RSCACHE_CS2_TYPE_INTERFACE:
    {
        enum RSCache_CS2_NameTable table = type == RSCACHE_CS2_TYPE_INV
                                               ? RSCACHE_CS2_NAMES_INV
                                               : type == RSCACHE_CS2_TYPE_SYNTH
                                                     ? RSCACHE_CS2_NAMES_SYNTH
                                                     : type == RSCACHE_CS2_TYPE_PARAM
                                                           ? RSCACHE_CS2_NAMES_PARAM
                                                           : RSCACHE_CS2_NAMES_INTERFACE;
        const char* name = RSCache_CS2_NamesLookup(names, table, value);
        if( name )
            snprintf(out, (size_t)out_capacity, "%s", name);
        else if( value == -1 )
            snprintf(out, (size_t)out_capacity, "null");
        else
            cs2_write_id_suffixed(literal, value, out, out_capacity);
        return true;
    }

    /* Non-unique names: the id is always appended, so `coins_995`. */
    case RSCACHE_CS2_TYPE_OBJ:
    case RSCACHE_CS2_TYPE_NAMEDOBJ:
    case RSCACHE_CS2_TYPE_LOC:
    case RSCACHE_CS2_TYPE_MODEL:
    case RSCACHE_CS2_TYPE_STRUCT:
    case RSCACHE_CS2_TYPE_NPC:
    case RSCACHE_CS2_TYPE_SEQ:
    {
        if( value == -1 )
        {
            snprintf(out, (size_t)out_capacity, "null");
            return true;
        }
        enum RSCache_CS2_NameTable table;
        const char* fallback;
        switch( type )
        {
        case RSCACHE_CS2_TYPE_OBJ:
        case RSCACHE_CS2_TYPE_NAMEDOBJ:
            table = RSCACHE_CS2_NAMES_OBJ;
            /* namedobj shares obj's table *and* its fallback spelling. */
            fallback = "obj";
            break;
        case RSCACHE_CS2_TYPE_LOC:
            table = RSCACHE_CS2_NAMES_LOC;
            fallback = "loc";
            break;
        case RSCACHE_CS2_TYPE_MODEL:
            table = RSCACHE_CS2_NAMES_MODEL;
            fallback = "model";
            break;
        case RSCACHE_CS2_TYPE_STRUCT:
            table = RSCACHE_CS2_NAMES_STRUCT;
            fallback = "struct";
            break;
        case RSCACHE_CS2_TYPE_NPC:
            table = RSCACHE_CS2_NAMES_NPC;
            fallback = "npc";
            break;
        default:
            table = RSCACHE_CS2_NAMES_SEQ;
            fallback = "seq";
            break;
        }
        const char* name = RSCache_CS2_NamesLookup(names, table, value);
        cs2_write_id_suffixed(name ? name : fallback, value, out, out_capacity);
        return true;
    }

    case RSCACHE_CS2_TYPE_NEWVAR:
    case RSCACHE_CS2_TYPE_DBROW:
    case RSCACHE_CS2_TYPE_SPOTANIM:
    case RSCACHE_CS2_TYPE_PLAYER_UID:
        /* Upstream has no spelling for these and throws. They are plain ints on
         * the wire with no name table, so the number is written — which is what
         * the compiler reads back, and is strictly better than refusing the
         * whole script. */
        if( value == -1 )
            snprintf(out, (size_t)out_capacity, "null");
        else
            snprintf(out, (size_t)out_capacity, "%d", value);
        return true;

    default:
        return false;
    }
}
