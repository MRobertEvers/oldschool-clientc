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
 * namespaces here have no consumer yet (`varn`, `vars`, `dbtable`): the
 * compiler already knew them and the runtime did not, and a register that
 * dropped them would be a regression dressed as a cleanup.
 *
 * `ids` and `names` are stated for every row even where nothing reads them yet.
 * They are the facts that decide whether cachepack may rewrite a file, and
 * writing them down is most of the point.
 */
static const struct ContentNamespace k_defaults[] = {
    /* name          ids                    names                     shared */
    { "npc",       CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "obj",       CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "loc",       CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "seq",       CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "spotanim",  CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "inv",       CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "interface", CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "component", CONTENT_IDS_CACHE,    CONTENT_NAMES_IMPORTED, 0 },
    { "hitsplat",  CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "synth",     CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    { "script",    CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0 },
    { "enum",      CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0 },
    { "struct",    CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0 },
    { "dbtable",   CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 0 },
    { "param",     CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    0 },
    /* No gameval archive names these, so there is no layer 0 at all. */
    { "stat",      CONTENT_IDS_PROTOCOL, CONTENT_NAMES_AUTHORED, 0 },
    { "category",  CONTENT_IDS_CACHE,    CONTENT_NAMES_AUTHORED, 0 },
    /* The four that answer to `%name`. */
    { "varp",      CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    1 },
    { "varbit",    CONTENT_IDS_CACHE,    CONTENT_NAMES_CACHE,    1 },
    { "varn",      CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 1 },
    { "vars",      CONTENT_IDS_SERVER,   CONTENT_NAMES_AUTHORED, 1 },
};

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
    }

    free(data);
    reg->from_file = 1;
    return reg->count;
}
