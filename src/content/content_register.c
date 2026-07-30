/*
 * The namespace register. See content_register.h.
 */

#include "content_register.h"

#include "3rd/ini/ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The defaults.
 *
 * These are the union of what the three old tables covered, which is why some
 * namespaces here have no consumer yet (`varn`, `vars`): the compiler already
 * knew them and the runtime did not, and a register that dropped them would be a
 * regression dressed as a cleanup.
 *
 * `ids`, `names` and `gameval` are stated for every row even where nothing reads
 * them yet. They are the facts that decide whether cachepack may rewrite a file,
 * and writing them down is most of the point.
 *
 * ## The gameval column
 *
 * Archive ids in the cache's own symbol table (OSRS idx 24), correlated against
 * the config groups by record count and content and then verified per cache at
 * load — see cachepack's `cp_names_seed_from_cache`, which refuses an archive
 * whose ids are not the group's. The mapping is recorded nowhere in the cache, so
 * every number here is a checked claim rather than a documented fact:
 *
 *   0 obj   1 npc   2 inv   3 varp   4 varbit   6 loc   7 seq   8 spotanim
 *   9 dbrow   10 dbtable   12 sprite   14 interface + component   15 varc
 *
 * Archive 11 names songs *and* jingles in one id space, so neither table alone
 * can verify it and nothing claims it. 5 and 13 are absent.
 *
 * Archive 14 is the odd one: each record holds an interface's name *and* every
 * one of its components as `u16 child` + name pairs, so one archive names two
 * namespaces. Read flat it looks like one 63-character name per interface, which
 * is why it went unclaimed for so long and why `component` names were being taken
 * from another server's table — that table put `bankmain_items` on child 13, and
 * this cache says child 13 is `scrollbar`.
 *
 * A -1 means every name in that namespace is either `<type>_<id>` filler or
 * something a human wrote. Those namespaces must not claim `names = cache`;
 * `ContentRegister_Validate` is what stops them.
 */
static const struct ContentNamespace k_defaults[] = {
    /* name               ids                    names                     shared  gameval    base */
    /* ---- config types the cache names -------------------------------- */
    { "npc",            CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   1,  20000 },
    { "obj",            CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   0,  40000 },
    { "loc",            CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   6,  70000 },
    { "seq",            CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   7,  20000 },
    { "spotanim",       CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   8,   6000 },
    { "inv",            CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   2,   2000 },
    /* Both from archive 14, which carries an interface and its children in one
     * record. `component` ids are `(interface << 16) | child`. */
    { "interface",      CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,  14,   2000 },
    { "component",      CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,  14,      0 },
    /* The cache names these and fixes their ids; neither has an encoder, so
     * authored content cannot create one either way. */
    { "dbrow",          CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,   9,      0 },
    { "dbtable",        CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,  10,    259 },
    /* ---- config types the cache does not name ------------------------ */
    /* Every name here is filler or authored. Declaring any of them `cache` is
     * what licensed cachepack to rewrite the file and drop its comments. */
    { "param",          CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   2634 },
    { "hitsplat",       CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    200 },
    { "healthbar",      CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    200 },
    { "mapelement",     CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   2000 },
    { "underlay",       CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    400 },
    { "overlay",        CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    800 },
    { "idk",            CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    400 },
    /* The obj record's own `category` field states the number (opcode 94) but
     * nothing names them. */
    { "category",       CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,      0 },
    /* ---- ours to number --------------------------------------------- */
    { "enum",           CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0,  -1,   5995 },
    { "struct",         CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0,  -1,   8000 },
    /* One namespace, two destinations: server RS2 compiles to the engine's own
     * bytecode pack and never enters the cache; client CS2 goes into the script
     * asset table and does. See docs/CONTENT_PACK_PLAN.md §6.2. */
    { "script",         CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0,  -1,  12000 },
    /* The wire fixes this one — UPDATE_STAT carries the index. */
    { "stat",           CONTENT_IDS_PROTOCOL, CONTENT_NAMES_AUTHORED, 0,  -1,      0 },
    /* ---- the four that answer to `%name` ---------------------------- */
    { "varp",           CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    1,   3,   8000 },
    { "varbit",         CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    1,   4,  25000 },
    { "varn",           CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 1,  -1,      0 },
    { "vars",           CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 1,  -1,      0 },
    { "varc",           CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,  15,   2000 },
    /* ---- asset tables ------------------------------------------------ */
    /*
     * Idx-addressed rather than config records, but the same three axes apply:
     * an asset only exists once a pack line maps its name to a group id.
     *
     * Only `sprite` is named by the cache. For models, sounds and maps the pack
     * file *is* the name table rather than an overlay on one — which is why the
     * authored side is not optional, and why lines in `pack/model.pack` versus
     * models in the cache is a literal "how much of this have I named" metric.
     */
    { "sprite",         CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0,  12,  20000 },
    { "model",          CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1, 100000 },
    { "synth",          CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,  20000 },
    /* Archive 11 names songs and jingles together, so neither table can verify
     * it alone; unclaimed rather than half-trusted. */
    { "song",           CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   2000 },
    { "jingle",         CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   1000 },
    { "sample",         CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   2000 },
    { "patch",          CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   1000 },
    { "texture",        CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   1000 },
    { "animset",        CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,  20000 },
    { "base",           CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   8000 },
    { "animaya",        CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,   2000 },
    { "map",            CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,      0 },
    { "font",           CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    100 },
    { "binary",         CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    100 },
    { "dbindex",        CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,    400 },
    { "worldmapgeo",    CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,      0 },
    { "worldmaparea",   CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,      0 },
    { "worldmapground", CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0,  -1,      0 },
};

/* Silently truncating the defaults would drop namespaces off the end of the
 * table and leave the tools disagreeing about which exist — the exact failure
 * the register was built to remove. */
_Static_assert(sizeof(k_defaults) / sizeof(k_defaults[0]) <= CONTENT_REGISTER_MAX,
               "more default namespaces than CONTENT_REGISTER_MAX holds");

int
ContentRegister_Defaults(struct ContentRegister* reg)
{
    memset(reg, 0, sizeof(*reg));
    reg->count = (int)(sizeof(k_defaults) / sizeof(k_defaults[0]));
    if( reg->count > CONTENT_REGISTER_MAX )
        reg->count = CONTENT_REGISTER_MAX;
    memcpy(reg->entries, k_defaults, (size_t)reg->count * sizeof(reg->entries[0]));
    return reg->count;
}

int
ContentRegister_Validate(const struct ContentRegister* reg)
{
    int violations = 0;

    for( int i = 0; i < reg->count; i++ )
    {
        const struct ContentNamespace* ns = &reg->entries[i];
        int claims_cache = ns->names == CONTENT_NAMES_CACHE;
        int has_archive = ns->gameval_archive >= 0;

        /*
         * A base on a namespace the *wire* fixes is a contradiction worth catching:
         * `stat` is the index UPDATE_STAT carries, so there is no id above the
         * cache's maximum to allocate — there is no cache maximum, and moving a
         * skill's number is not the content tree's to do.
         */
        if( ns->server_base != 0 && ns->ids == CONTENT_IDS_PROTOCOL )
        {
            fprintf(stderr,
                    "content.ini: namespace `%s` declares `ids = protocol` and an allocation "
                    "base of %d — the wire fixes these ids, so there is nothing to allocate\n",
                    ns->name, ns->server_base);
            violations++;
        }

        if( claims_cache == has_archive )
            continue;

        /* Name both facts and both consequences: the reader has to know which
         * side is wrong, and that is not derivable from the mismatch alone. */
        if( claims_cache )
            fprintf(stderr,
                    "content.ini: namespace `%s` declares `names = cache` but no gameval "
                    "archive names it — every name in pack/%s.pack is filler or authored, "
                    "and declaring it machine-owned lets cachepack rewrite the file\n",
                    ns->name, ns->name);
        else
            fprintf(stderr,
                    "content.ini: namespace `%s` is named by gameval archive %d but does not "
                    "declare `names = cache` — the cache's own names will never be imported "
                    "and every id stays spelled `%s_<id>`\n",
                    ns->name, ns->gameval_archive, ns->name);
        violations++;
    }
    return violations;
}

const struct ContentNamespace*
ContentRegister_Find(
    const struct ContentRegister* reg,
    const char* name)
{
    if( !name )
        return NULL;
    for( int i = 0; i < reg->count; i++ )
    {
        if( strcmp(reg->entries[i].name, name) == 0 )
            return &reg->entries[i];
    }
    return NULL;
}

const struct ContentNamespace*
ContentRegister_ForPackFile(
    const struct ContentRegister* reg,
    const char* filename)
{
    char stem[64];
    const char* dot;
    size_t length;

    if( !filename )
        return NULL;
    dot = strrchr(filename, '.');
    if( !dot || strcmp(dot, ".pack") != 0 )
        return NULL;
    length = (size_t)(dot - filename);
    if( length == 0 || length >= sizeof(stem) )
        return NULL;
    memcpy(stem, filename, length);
    stem[length] = '\0';
    return ContentRegister_Find(reg, stem);
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/*
 * Trim, in place, and drop an inline comment.
 *
 * 3rd/ini does not do this: it skips whitespace *before* an element, then takes
 * the key as everything up to `=` and the value as everything to end of line.
 * So `ids       = cache` arrives as key `"ids       "` and value `" cache"`, and
 * a naive strcmp silently matches nothing — which is how the first version of
 * this file parsed a valid content.ini into no overrides at all while reporting
 * success. Every consumer of that parser has to do this or write its ini files
 * without spaces.
 *
 * Inline comments are dropped too, so `names = cache ; the gameval table` works.
 * Safe because every value in this grammar is a single bare word.
 */
void
ContentIni_Trim(char* text)
{
    char* start = text;
    size_t length;

    for( char* scan = text; *scan; scan++ )
    {
        if( *scan == ';' || *scan == '#' )
        {
            *scan = '\0';
            break;
        }
    }

    while( *start == ' ' || *start == '\t' )
        start++;
    if( start != text )
        memmove(text, start, strlen(start) + 1);

    length = strlen(text);
    while( length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\r' || text[length - 1] == '\n') )
        text[--length] = '\0';
}

static enum ContentIdAuthority
parse_ids(
    const char* value,
    enum ContentIdAuthority fallback)
{
    if( strcmp(value, "cache") == 0 )
        return CONTENT_IDS_CACHE;
    if( strcmp(value, "server") == 0 )
        return CONTENT_IDS_SERVER;
    if( strcmp(value, "protocol") == 0 )
        return CONTENT_IDS_PROTOCOL;
    return fallback;
}

static enum ContentNameAuthority
parse_names(
    const char* value,
    enum ContentNameAuthority fallback)
{
    if( strcmp(value, "cache") == 0 )
        return CONTENT_NAMES_CACHE;
    if( strcmp(value, "authored") == 0 )
        return CONTENT_NAMES_AUTHORED;
    if( strcmp(value, "derived") == 0 )
        return CONTENT_NAMES_DERIVED;
    if( strncmp(value, "imported", 8) == 0 )
        return CONTENT_NAMES_IMPORTED;
    return fallback;
}

int
ContentRegister_Load(
    struct ContentRegister* reg,
    const char* dir)
{
    char path[1024];
    FILE* file;
    long size;
    uint8_t* data;
    struct INIReader reader;
    struct INIElement element;
    struct ContentNamespace* current = NULL;

    /* Defaults first, then the file *overlays* them. A tree that declares only
     * the one namespace it invented still gets the other twenty — which is what
     * makes adopting the register a one-line change rather than a transcription
     * of everything that already worked. */
    ContentRegister_Defaults(reg);

    snprintf(path, sizeof(path), "%s/content.ini", dir);
    file = fopen(path, "rb");
    if( !file )
        return reg->count;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return reg->count;
    }
    data = (uint8_t*)malloc((size_t)size);
    if( !data )
    {
        fclose(file);
        return reg->count;
    }
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return reg->count;
    }
    fclose(file);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            /* `[namespace:varp]`. Anything else in the file is not ours. */
            const char* name = element._section.name;

            ContentIni_Trim(element._section.name);
            current = NULL;
            if( strncmp(name, "namespace:", 10) != 0 )
                continue;
            name += 10;

            for( int i = 0; i < reg->count; i++ )
            {
                if( strcmp(reg->entries[i].name, name) == 0 )
                {
                    current = &reg->entries[i];
                    break;
                }
            }
            if( !current && reg->count < CONTENT_REGISTER_MAX )
            {
                current = &reg->entries[reg->count++];
                memset(current, 0, sizeof(*current));
                snprintf(current->name, sizeof(current->name), "%s", name);
                /* Zero would mean "gameval archive 0", which is obj. A namespace
                 * the defaults never had is unnamed by the cache until it says
                 * otherwise. */
                current->gameval_archive = -1;
            }
            continue;
        }

        if( element.kind != INI_ELEMENT_KEYVAL || !current )
            continue;

        ContentIni_Trim(element._keyval.name);
        ContentIni_Trim(element._keyval.value);

        if( strcmp(element._keyval.name, "ids") == 0 )
            current->ids = parse_ids(element._keyval.value, current->ids);
        else if( strcmp(element._keyval.name, "names") == 0 )
            current->names = parse_names(element._keyval.value, current->names);
        else if( strcmp(element._keyval.name, "vardomain") == 0 )
            current->shared_var_domain = atoi(element._keyval.value) != 0 ||
                                         strcmp(element._keyval.value, "yes") == 0;
        /* A tree may state the archive for a namespace the defaults do not carry.
         * `none` is how it says there is no archive without relying on -1 being
         * spellable. */
        else if( strcmp(element._keyval.name, "gameval") == 0 )
            current->gameval_archive =
                strcmp(element._keyval.value, "none") == 0 ? -1 : atoi(element._keyval.value);
        /* `base = none` is the same as 0 and reads better where the point is that
         * the namespace cannot be allocated into. */
        else if( strcmp(element._keyval.name, "base") == 0 )
            current->server_base =
                strcmp(element._keyval.value, "none") == 0 ? 0 : atoi(element._keyval.value);
    }

    free(data);
    reg->from_file = 1;
    return reg->count;
}
