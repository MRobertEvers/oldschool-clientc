#include "cachepack.h"
#include "cp_incremental.h"
#include "rscache_valuetype.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Typed values: the one ScriptVarType table, and the one reader and writer of a
 * value by its type.
 *
 * A config value's meaning is its declared type, and the type is stated three
 * ways depending on where it is declared: a dbtable column stores the type's
 * numeric *id* (22 for a coord), a param record and an enum store its
 * *character* ('c'), and the text spells it as a *word* (`coord`). Those used to
 * be two partial tables -- the db's (id, word) and the params' (char, word) --
 * and three partial value readers, so a param could not read a coord, an enum
 * could read only numbers, and the param table called 'P' a param when the cache
 * (typeid 14) says it is a synth. This is the only table now: every reader of a
 * type id, a type character or a type word, and every reader and writer of an
 * int value by its type, goes through it -- dbtable/dbrow (cp_db.c), param
 * records and `param=` lines (cp_small.c, cp_common.c), enums (cp_idk.c) and the
 * server band writer (cp_pack.c).
 *
 * ## The words
 *
 * Jagex's ScriptVarType table as cache2 publishes it (abextm/cache2,
 * `ScriptVarType.ts`: id, char, jag name), with LostCity's word where LostCity
 * has the type -- `int` and `coord` rather than cache2's `integer` and
 * `coordgrid`, because the authored grammar wins and every authored file says
 * `int`. Rows with no id are types a param or an enum can state by character
 * that no dbtable has: `area`, `maparea` and `interface` (RuneStar's Type.kt,
 * `3rd/rscache/src/cs2/cs2_types.c`).
 *
 * `varbit` is this server's own: an authored enum (`bank_tabs`) maps to varbits,
 * and no published ScriptVarType names one. Its character is 0x81, a byte
 * windows-1252 leaves undefined -- Jagex's characters are cp1252 characters, so
 * an undefined one cannot collide with a type Jagex adds later.
 *
 * A type id or character the table does not list is spelled as its decimal
 * number and read back as one, so a newer cache's type survives the text.
 *
 * ## The values
 *
 * One spelling per type, in the authored grammar:
 *
 *     int, and the     decimal; `null` is -1 (LostCity's lookupParamValue)
 *     numeric types
 *     boolean          `yes`/`no` on a param or an enum, `true`/`false` in a
 *                      dbrow -- each what its authored files write (LostCity's
 *                      params say `yes`, its dbrows `true`); either reads back
 *     coord            level_mx_mz_lx_lz; `null` for -1
 *     a config record  its name (`null` for -1)
 *     stat, category   the name `pack/stat.pack` / `pack/category.pack` gives
 *     component        `interface:component`
 *     synth, interface the asset's name when its pack names it
 *     string           raw (see cp_value_string_*)
 *
 * A bare number is read for every int type, so a value with no name (an id past
 * the pack, a sound nobody named) is its number. A `^name` is a constant: its
 * `.constant` text, read as the type reads text.
 */

/*
 * How each type is spelled and what a name resolves in, by word. The type's id
 * and character are the library's (rscache_valuetype.h), joined in by
 * value_types() on first use, so the (id, char, word) triple has one home.
 */
static struct CP_ValueType k_value_types[] = {
    /* word            spelling            what a name resolves in */
    { -1, 0, "int", CP_VALUE_INT, -1 },
    { -1, 0, "boolean", CP_VALUE_BOOLEAN, -1 },
    { -1, 0, "seq", CP_VALUE_REF, CP_TYPE_SEQ },
    { -1, 0, "colour", CP_VALUE_INT, -1 },
    { -1, 0, "locshape", CP_VALUE_INT, -1 },
    { -1, 0, "component", CP_VALUE_COMPONENT, -1 },
    { -1, 0, "idkit", CP_VALUE_INT, -1 },
    { -1, 0, "midi", CP_VALUE_INT, -1 },
    { -1, 0, "namedobj", CP_VALUE_REF, CP_TYPE_OBJ },
    { -1, 0, "synth", CP_VALUE_ASSET, CP_ASSET_SYNTH },
    { -1, 0, "stat", CP_VALUE_STAT, -1 },
    { -1, 0, "coord", CP_VALUE_COORD, -1 },
    { -1, 0, "graphic", CP_VALUE_INT, -1 },
    { -1, 0, "fontmetrics", CP_VALUE_INT, -1 },
    { -1, 0, "enum", CP_VALUE_REF, CP_TYPE_ENUM },
    { -1, 0, "jingle", CP_VALUE_INT, -1 },
    { -1, 0, "loc", CP_VALUE_REF, CP_TYPE_LOC },
    { -1, 0, "model", CP_VALUE_INT, -1 },
    { -1, 0, "npc", CP_VALUE_REF, CP_TYPE_NPC },
    { -1, 0, "obj", CP_VALUE_REF, CP_TYPE_OBJ },
    { -1, 0, "string", CP_VALUE_STRING, -1 },
    { -1, 0, "spotanim", CP_VALUE_REF, CP_TYPE_SPOTANIM },
    { -1, 0, "inv", CP_VALUE_REF, CP_TYPE_INV },
    { -1, 0, "texture", CP_VALUE_INT, -1 },
    { -1, 0, "category", CP_VALUE_CATEGORY, -1 },
    { -1, 0, "char", CP_VALUE_INT, -1 },
    { -1, 0, "mapsceneicon", CP_VALUE_INT, -1 },
    { -1, 0, "mapelement", CP_VALUE_REF, CP_TYPE_MAPELEMENT },
    { -1, 0, "hitmark", CP_VALUE_REF, CP_TYPE_HITSPLAT },
    { -1, 0, "struct", CP_VALUE_REF, CP_TYPE_STRUCT },
    { -1, 0, "dbrow", CP_VALUE_REF, CP_TYPE_DBROW },
    { -1, 0, "dbtable", CP_VALUE_REF, CP_TYPE_DBTABLE },
    { -1, 0, "varp", CP_VALUE_REF, CP_TYPE_VARP },
    { -1, 0, "area", CP_VALUE_INT, -1 },
    { -1, 0, "maparea", CP_VALUE_INT, -1 },
    { -1, 0, "interface", CP_VALUE_ASSET, CP_ASSET_INTERFACE },
    { -1, 0, "varbit", CP_VALUE_REF, CP_TYPE_VARBIT },
};

#define VALUE_TYPE_COUNT ((int)(sizeof(k_value_types) / sizeof(k_value_types[0])))

/** The table with each row's id and character filled from the library, once.
 *  Every word here must be a library type and every library type a row here. */
static const struct CP_ValueType*
value_types(void)
{
    static int joined;

    if( !joined )
    {
        assert(VALUE_TYPE_COUNT == RSCache_ValueTypeCount());
        for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
        {
            const struct RSCache_ValueType* type = RSCache_ValueTypeNamed(k_value_types[i].name);

            assert(type);
            k_value_types[i].id = type->id;
            k_value_types[i].ch = type->ch;
        }
        joined = 1;
    }
    return k_value_types;
}

int
cp_value_type_count(void)
{
    return VALUE_TYPE_COUNT;
}

const struct CP_ValueType*
cp_value_type_at(int index)
{
    assert(index >= 0);
    assert(index < VALUE_TYPE_COUNT);
    return &value_types()[index];
}

const struct CP_ValueType*
cp_value_type_named(const char* word)
{
    assert(word);
    for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
    {
        if( strcmp(value_types()[i].name, word) == 0 )
            return &value_types()[i];
    }
    return NULL;
}

const struct CP_ValueType*
cp_value_type_of_id(int id)
{
    if( id < 0 )
        return NULL;
    for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
    {
        if( value_types()[i].id == id )
            return &value_types()[i];
    }
    return NULL;
}

const struct CP_ValueType*
cp_value_type_of_char(int ch)
{
    ch &= 0xFF;
    if( ch == 0 )
        return NULL;
    for( int i = 0; i < VALUE_TYPE_COUNT; i++ )
    {
        if( value_types()[i].ch == ch )
            return &value_types()[i];
    }
    return NULL;
}

/** Strict decimal, as the server's atoi-based readers understand numbers --
 *  `cp_parse_int` takes base prefixes, so `010` would be 8 there and 10 here. */
static int
value_parse_decimal(
    const char* text,
    int* out)
{
    char* end;
    long value;

    if( !*text )
        return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if( errno || end == text || *end || value < INT_MIN || value > INT_MAX )
        return 0;
    *out = (int)value;
    return 1;
}

const char*
cp_value_char_text(
    int ch,
    char* buf,
    size_t buf_size)
{
    const struct CP_ValueType* type = cp_value_type_of_char(ch);

    assert(buf);
    if( type )
        return type->name;
    snprintf(buf, buf_size, "%d", ch & 0xFF);
    return buf;
}

int
cp_value_char_read(const char* text)
{
    const struct CP_ValueType* type;
    int number;

    assert(text);
    type = cp_value_type_named(text);
    if( type && type->ch )
        return type->ch;
    if( value_parse_decimal(text, &number) && number > 0 && number <= 0xFF )
        return number;
    return 0;
}

const char*
cp_value_id_text(
    int id,
    char* buf,
    size_t buf_size)
{
    const struct CP_ValueType* type = cp_value_type_of_id(id);

    assert(buf);
    if( type )
        return type->name;
    snprintf(buf, buf_size, "%d", id);
    return buf;
}

int
cp_value_id_read(const char* text)
{
    const struct CP_ValueType* type;
    int number;

    assert(text);
    type = cp_value_type_named(text);
    if( type )
        return type->id;
    if( value_parse_decimal(text, &number) && number >= 0 )
        return number;
    return -1;
}

/* ---- names that are not config records ---------------------------------- */

/*
 * `stat` is a protocol-fixed table the tree states in `pack/stat.pack`, which
 * cachepack's own name loader does not read; it is loaded here, once per tree.
 * Absent is normal (a fresh unpack directory has none): stats are then numbers.
 */
static struct
{
    char srcdir[1024];
    int loaded;
    struct LC_Pack pack;
} g_stats;

static const struct LC_Pack*
value_stats(struct CP_Ctx* ctx)
{
    if( !g_stats.loaded || strcmp(g_stats.srcdir, ctx->srcdir) != 0 )
    {
        char path[1200];

        if( g_stats.loaded )
            lc_pack_free(&g_stats.pack);
        memset(&g_stats.pack, 0, sizeof(g_stats.pack));
        snprintf(g_stats.srcdir, sizeof(g_stats.srcdir), "%s", ctx->srcdir);
        snprintf(path, sizeof(path), "%s/pack/stat.pack", ctx->srcdir);
        lc_pack_load(&g_stats.pack, path, "stat", 1);
        g_stats.loaded = 1;
    }
    return &g_stats.pack;
}

static const char*
value_pack_name(
    const struct LC_Pack* pack,
    int id)
{
    if( id < 0 || id >= pack->capacity || !pack->names )
        return NULL;
    return pack->names[id];
}

/*
 * `^name` constants, as text.
 *
 * `cp_constants_load` keeps only integer constants (a param slot holds an int);
 * a value can be a coord, a name or a string, and the server expands a `^name`
 * to whatever text the `.constant` declares before reading it against the
 * type. So this keeps the text, read the way the server's `load_constant_config`
 * reads it: `^name = value`, `//` comments and edge blanks dropped.
 */
static struct
{
    const struct CP_Ctx* ctx;
    int loaded;
    char** names;
    char** texts;
    int count;
} g_constants;

/** The server's line cleaner: cut a `//` comment, trim edge blanks. */
static char*
value_clean_plain(char* line)
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

static uint32_t
value_name_hash(const char* name)
{
    uint32_t hash = 2166136261u;

    for( ; *name; name++ )
        hash = (hash ^ (uint8_t)*name) * 16777619u;
    return hash;
}

const char*
cp_value_constant_text(
    struct CP_Ctx* ctx,
    const char* name)
{
    assert(ctx);
    assert(name);
    if( g_constants.ctx != ctx || !g_constants.loaded )
    {
        const char* found[CP_PACK_MAX_SOURCES];
        int capacity = 0;
        int found_count = cp_walk_find(&ctx->walk, "constant", found, CP_PACK_MAX_SOURCES);

        for( int i = 0; i < g_constants.count; i++ )
        {
            free(g_constants.names[i]);
            free(g_constants.texts[i]);
        }
        free(g_constants.names);
        free(g_constants.texts);
        memset(&g_constants, 0, sizeof(g_constants));
        g_constants.ctx = ctx;
        g_constants.loaded = 1;

        for( int f = 0; f < found_count; f++ )
        {
            FILE* fp = fopen(found[f], "rb");
            char raw[1024];

            if( !fp )
                continue;
            while( fgets(raw, sizeof(raw), fp) )
            {
                char* line = value_clean_plain(raw);
                char* eq;
                char* key_end;
                char* value;

                if( *line != '^' || !(eq = strchr(line, '=')) )
                    continue;
                key_end = eq;
                while( key_end > line && (key_end[-1] == ' ' || key_end[-1] == '\t') )
                    key_end--;
                *key_end = '\0';
                value = eq + 1;
                while( *value == ' ' || *value == '\t' )
                    value++;
                if( g_constants.count == capacity )
                {
                    capacity = capacity ? capacity * 2 : 512;
                    g_constants.names = realloc(g_constants.names, (size_t)capacity * sizeof(char*));
                    assert(g_constants.names);
                    g_constants.texts = realloc(g_constants.texts, (size_t)capacity * sizeof(char*));
                    assert(g_constants.texts);
                }
                g_constants.names[g_constants.count] = strdup(line + 1);
                assert(g_constants.names[g_constants.count]);
                g_constants.texts[g_constants.count] = strdup(value);
                assert(g_constants.texts[g_constants.count]);
                g_constants.count++;
            }
            fclose(fp);
        }
    }
    if( g_cp_recording )
        cp_lookup_note(CP_LOOKUP_CONST_TEXT, 0, 0, name);
    /* The first declaration of the name, indexed (see cp_resolve_caret). */
    {
        static int* slots;
        static uint32_t capacity;
        static int indexed_count = -1;
        static char** indexed;
        uint32_t slot;

        if( indexed != g_constants.names || indexed_count != g_constants.count )
        {
            capacity = 1024;
            while( capacity < (uint32_t)g_constants.count * 2 + 16 )
                capacity *= 2;
            free(slots);
            slots = (int*)malloc(capacity * sizeof(int));
            assert(slots);
            memset(slots, -1, capacity * sizeof(int));
            for( int i = 0; i < g_constants.count; i++ )
            {
                slot = value_name_hash(g_constants.names[i]) & (capacity - 1);
                while( slots[slot] >= 0 && strcmp(g_constants.names[slots[slot]], g_constants.names[i]) != 0 )
                    slot = (slot + 1) & (capacity - 1);
                if( slots[slot] < 0 )
                    slots[slot] = i;
            }
            indexed = g_constants.names;
            indexed_count = g_constants.count;
        }
        slot = value_name_hash(name) & (capacity - 1);
        while( slots[slot] >= 0 )
        {
            if( strcmp(g_constants.names[slots[slot]], name) == 0 )
                return g_constants.texts[slots[slot]];
            slot = (slot + 1) & (capacity - 1);
        }
    }
    return NULL;
}

/*
 * A component named `interface:component`, to its packed `(interface << 16) |
 * child` id.
 *
 * `unpack` and `verify` have the cache's own names (gameval archive 14, in
 * `CP_Names.components`). `pack` opens no cache, so it reads the same names from
 * the tree, where `unpack` wrote them: the interface from `pack/3_interfaces.pack`,
 * the child from `interfaces/<interface>.compack`. Each `.compack` is loaded once.
 */
static struct
{
    const struct CP_Ctx* ctx;
    char** names;
    struct LC_Pack* packs;
    int count;
} g_compacks;

static int
value_component_find(
    struct CP_Ctx* ctx,
    const char* text,
    int* out)
{
    const char* colon = strchr(text, ':');
    char iface_name[256];
    const struct LC_Pack* children = NULL;
    int iface;
    int child;
    int id = lc_pack_find(&ctx->names.components, text);

    if( id >= 0 )
    {
        *out = id;
        return 1;
    }
    if( !colon || (size_t)(colon - text) >= sizeof(iface_name) )
        return 0;
    memcpy(iface_name, text, (size_t)(colon - text));
    iface_name[colon - text] = '\0';
    iface = cp_asset_name_find(ctx, CP_ASSET_INTERFACE, iface_name);
    if( iface < 0 )
        return 0;

    if( g_compacks.ctx != ctx )
    {
        for( int i = 0; i < g_compacks.count; i++ )
        {
            free(g_compacks.names[i]);
            lc_pack_free(&g_compacks.packs[i]);
        }
        free(g_compacks.names);
        free(g_compacks.packs);
        memset(&g_compacks, 0, sizeof(g_compacks));
        g_compacks.ctx = ctx;
    }
    for( int i = 0; i < g_compacks.count; i++ )
    {
        if( strcmp(g_compacks.names[i], iface_name) == 0 )
            children = &g_compacks.packs[i];
    }
    if( !children )
    {
        char path[1400];
        int n = g_compacks.count;

        g_compacks.names = realloc(g_compacks.names, (size_t)(n + 1) * sizeof(char*));
        assert(g_compacks.names);
        g_compacks.packs = realloc(g_compacks.packs, (size_t)(n + 1) * sizeof(struct LC_Pack));
        assert(g_compacks.packs);
        g_compacks.names[n] = strdup(iface_name);
        assert(g_compacks.names[n]);
        memset(&g_compacks.packs[n], 0, sizeof(struct LC_Pack));
        snprintf(path, sizeof(path), "%s/interfaces/%s.compack", ctx->srcdir, iface_name);
        lc_pack_load(&g_compacks.packs[n], path, "com", 1);
        g_compacks.count = n + 1;
        children = &g_compacks.packs[n];
    }
    child = lc_pack_find(children, colon + 1);
    /* A child the cache's gameval table leaves unnamed is named by its index
     * (`combat_interface:0`), and the tree's `.compack` may name that child
     * something else -- so a decimal the compack does not list is the index. */
    if( child < 0 && !value_parse_decimal(colon + 1, &child) )
        return 0;
    if( child < 0 || child > 0xFFFF )
        return 0;
    *out = (iface << 16) | child;
    return 1;
}

/* ---- one int value ------------------------------------------------------ */

const char*
cp_value_int_text(
    struct CP_Ctx* ctx,
    const struct CP_ValueType* type,
    int value,
    enum CP_BoolStyle bool_style,
    char* buf,
    size_t buf_size)
{
    const char* name = NULL;

    assert(ctx);
    assert(buf);
    switch( type ? type->spell : CP_VALUE_INT )
    {
    case CP_VALUE_BOOLEAN:
        /* Anything but 0/1 is not a boolean the words can say: its number. */
        if( value == 0 || value == 1 )
        {
            if( bool_style == CP_BOOL_YES_NO )
                return value ? "yes" : "no";
            return value ? "true" : "false";
        }
        break;
    case CP_VALUE_COORD:
    {
        unsigned u = (unsigned)value;
        unsigned x = (u >> 14) & 0x3FFF;
        unsigned z = u & 0x3FFF;

        if( value == -1 )
            return "null";
        snprintf(buf, buf_size, "%u_%u_%u_%u_%u", u >> 28, x >> 6, z >> 6, x & 63, z & 63);
        return buf;
    }
    case CP_VALUE_REF:
        if( value == -1 )
            return "null";
        /* A garbage id is a number, not a name invented for it: cp_name_ensure
         * would grow the pack to reach it. */
        if( value >= 0 && value < (1 << 24) )
            return cp_name_ensure(ctx, (enum CP_TypeId)type->ref, value);
        break;
    case CP_VALUE_ASSET:
        if( value == -1 )
            return "null";
        /* Named only when the asset pack already names it: an asset id is not a
         * config record, and inventing `<dir>_<id>` for a sound that does not
         * exist would grow a pack the asset export owns. */
        if( value >= 0 )
            name = cp_asset_name_get(ctx, (enum CP_AssetId)type->ref, value);
        break;
    case CP_VALUE_STAT:
        if( value == -1 )
            return "null";
        name = value_pack_name(value_stats(ctx), value);
        break;
    case CP_VALUE_CATEGORY:
        if( value == -1 )
            return "null";
        name = value_pack_name(&ctx->names.category, value);
        break;
    case CP_VALUE_COMPONENT:
        if( value == -1 )
            return "null";
        if( value >= 0 )
            name = cp_component_name(ctx, value >> 16, value & 0xFFFF);
        break;
    case CP_VALUE_STRING:
        /* A string type holding an int has no text a string reader would give
         * back. The callers route strings away before this. */
        assert(0 && "cp_value_int_text of a string type");
        break;
    case CP_VALUE_INT:
        break;
    }
    if( name )
        return name;
    snprintf(buf, buf_size, "%d", value);
    return buf;
}

/** One int value's text, after any `^constant` has been expanded. */
static int
value_int_parse(
    struct CP_Ctx* ctx,
    const struct CP_ValueType* type,
    const char* text,
    int* out)
{
    enum CP_ValueSpell spell = type ? type->spell : CP_VALUE_INT;
    int id;

    /*
     * `null` is -1 for every int-valued type, LostCity's `lookupParamValue` rule,
     * and the authored tree relies on it for plain ints too (`runesrequired`'s
     * unused rune slots are `null,null`).
     */
    if( strcmp(text, "null") == 0 )
    {
        *out = -1;
        return 1;
    }

    switch( spell )
    {
    case CP_VALUE_BOOLEAN:
        /* Either pair (LostCity's isConfigBoolean): params say `yes`, dbrows
         * `true`, and one reader serves both. */
        if( strcmp(text, "true") == 0 || strcmp(text, "yes") == 0 )
        {
            *out = 1;
            return 1;
        }
        if( strcmp(text, "false") == 0 || strcmp(text, "no") == 0 )
        {
            *out = 0;
            return 1;
        }
        return value_parse_decimal(text, out);
    case CP_VALUE_COORD:
    {
        /* As the server's db_parse_coord, then its atoi fallback. */
        unsigned parts[5];
        int count = 0;
        const char* scan = text;

        while( count < 5 )
        {
            unsigned value = 0;
            int digits = 0;

            while( *scan >= '0' && *scan <= '9' )
            {
                value = value * 10 + (unsigned)(*scan - '0');
                scan++;
                digits++;
            }
            if( !digits )
                break;
            parts[count++] = value;
            if( *scan != '_' )
                break;
            scan++;
        }
        if( count == 5 && *scan == '\0' )
        {
            *out = (int)((parts[0] << 28) | ((parts[1] * 64 + parts[3]) << 14) |
                         (parts[2] * 64 + parts[4]));
            return 1;
        }
        return value_parse_decimal(text, out);
    }
    case CP_VALUE_REF:
        return cp_resolve_ref_or_null(ctx, (enum CP_TypeId)type->ref, text, out);
    case CP_VALUE_ASSET:
        if( value_parse_decimal(text, out) )
            return 1;
        id = cp_asset_name_find(ctx, (enum CP_AssetId)type->ref, text);
        if( id < 0 )
            return 0;
        *out = id;
        return 1;
    case CP_VALUE_STAT:
    case CP_VALUE_CATEGORY:
    case CP_VALUE_COMPONENT:
        if( value_parse_decimal(text, out) )
            return 1;
        if( spell == CP_VALUE_COMPONENT )
            return value_component_find(ctx, text, out);
        id = lc_pack_find(spell == CP_VALUE_STAT ? value_stats(ctx) : &ctx->names.category,
                          text);
        if( id < 0 )
            return 0;
        *out = id;
        return 1;
    case CP_VALUE_STRING:
        assert(0 && "cp_value_int_read of a string type");
        return 0;
    case CP_VALUE_INT:
        break;
    }
    /* A number: decimal, or LostCity's `0x` hex. */
    if( value_parse_decimal(text, out) )
        return 1;
    return text[0] == '0' && text[1] == 'x' && cp_parse_int(text, out);
}

int
cp_value_int_read(
    struct CP_Ctx* ctx,
    const struct CP_ValueType* type,
    const char* text,
    int* out)
{
    assert(ctx);
    assert(text);
    assert(out);
    if( text[0] == '^' )
    {
        const char* constant = cp_value_constant_text(ctx, text + 1);

        if( constant )
            return value_int_parse(ctx, type, constant, out);
        /* `^true` / `^false`, which the language defines. */
        return cp_resolve_caret(ctx, text, out);
    }
    return value_int_parse(ctx, type, text, out);
}

/* ---- one string value --------------------------------------------------- */

void
cp_value_string_escape(
    const char* text,
    int whole,
    char* buf,
    size_t buf_size)
{
    size_t w = 0;

    assert(text);
    assert(buf);
    assert(buf_size > 2);
    /* A string that IS a marker word, as the whole value, is escaped so it is
     * never read as one (cp_text.h). */
    if( whole && (strcmp(text, CP_VALUE_DEFAULT) == 0 || strcmp(text, CP_VALUE_EMPTY) == 0) )
        buf[w++] = '\\';
    for( size_t i = 0; text[i]; i++ )
    {
        unsigned char c = (unsigned char)text[i];

        assert(w + 3 < buf_size);
        if( c == '\n' || c == '\r' )
        {
            buf[w++] = '\\';
            buf[w++] = c == '\n' ? 'n' : 'r';
            continue;
        }
        /* A backslash, and a leading caret that is not a constant. A leading
         * `[` only where the string starts the line's value, so it is never read
         * as a block header. */
        if( c == '\\' || (i == 0 && c == '^') || (i == 0 && whole && c == '[') )
            buf[w++] = '\\';
        buf[w++] = (char)c;
    }
    buf[w] = '\0';
}

int
cp_value_string_read(
    struct CP_Ctx* ctx,
    const char* raw,
    char* buf,
    size_t buf_size)
{
    assert(ctx);
    assert(raw);
    assert(buf);
    if( raw[0] == '^' )
    {
        const char* constant = cp_value_constant_text(ctx, raw + 1);

        if( !constant )
            return 0;
        snprintf(buf, buf_size, "%s", constant);
        return 1;
    }
    cp_unescape(raw, buf, (int)buf_size);
    return 1;
}
