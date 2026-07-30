#include "cp_register.h"

#include "cachepack.h"
#include "cp_assets.h"
#include "ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    CP_REGISTER_MAX = 64,
    CP_REGISTER_NAME_MAX = 48,
};

struct CP_RegisterEntry
{
    char name[CP_REGISTER_NAME_MAX];
    bool machine_owned; /* names = cache */
};

static struct CP_RegisterEntry g_entries[CP_REGISTER_MAX];
static int g_count;

/**
 * The gameval archive naming `ns`, or -1 — from the codec tables themselves.
 *
 * The fourth column of `cp_types.c` and `cp_assets.c` is the only statement of
 * this fact cachepack has, so reading it here rather than restating it is what
 * keeps the register's default from being a third table that can drift.
 *
 * Returns -2 for a namespace neither table knows, which is a different answer
 * from "known, and unnamed": `stat` is not cachepack's to have an opinion about,
 * and treating it as unnamed would silently pass a check it was never in scope
 * for.
 */
static int
gameval_archive_for(const char* ns)
{
    /*
     * Named by archive 14 alongside the interfaces, but not a table of its own.
     *
     * One archive-14 record holds an interface's name *and* every one of its
     * components as `u16 child` + name pairs, so components are as
     * machine-generated as interfaces are — they simply have no reference table
     * to appear in. Without this row the derived default would call
     * `component.pack` authored and stop regenerating it.
     */
    if( strcmp(ns, "component") == 0 )
        return 14;

    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( strcmp(cp_type(i)->name, ns) == 0 )
            return cp_type(i)->gameval_archive;
    }
    for( int i = 0; i < CP_ASSET_COUNT; i++ )
    {
        if( strcmp(cp_asset(i)->pack, ns) == 0 )
            return cp_asset(i)->gameval_archive;
    }
    return -2;
}

static const struct CP_RegisterEntry*
find_entry(const char* ns)
{
    for( int i = 0; i < g_count; i++ )
    {
        if( strcmp(g_entries[i].name, ns) == 0 )
            return &g_entries[i];
    }
    return NULL;
}

/*
 * Trim in place and drop an inline comment.
 *
 * 3rd/ini skips whitespace *before* an element, then takes the key as
 * everything up to `=` and the value as everything to end of line — so
 * `names = cache` arrives as key `"names "` and value `" cache"`. Every
 * consumer has to do this; forgetting it parses a valid file into no settings
 * at all and reports success.
 */
static void
trim(char* text)
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

void
cp_register_load(const char* srcdir)
{
    char path[1200];
    FILE* file;
    long size;
    uint8_t* data;
    struct INIReader reader;
    struct INIElement element;
    struct CP_RegisterEntry* current = NULL;

    g_count = 0;

    snprintf(path, sizeof(path), "%s/content.ini", srcdir);
    file = fopen(path, "rb");
    if( !file )
        return;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return;
    }
    data = (uint8_t*)malloc((size_t)size);
    if( !data )
    {
        fclose(file);
        return;
    }
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return;
    }
    fclose(file);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            trim(element._section.name);
            current = NULL;
            if( strncmp(element._section.name, "namespace:", 10) != 0 )
                continue;
            if( g_count >= CP_REGISTER_MAX )
                continue;
            current = &g_entries[g_count++];
            snprintf(current->name, sizeof(current->name), "%s", element._section.name + 10);
            current->machine_owned = true; /* until the file says otherwise */
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL || !current )
            continue;
        trim(element._keyval.name);
        trim(element._keyval.value);
        if( strcmp(element._keyval.name, "names") == 0 )
            current->machine_owned = strcmp(element._keyval.value, "cache") == 0;
    }

    free(data);
}

bool
cp_register_may_write_pack(const char* ns)
{
    const struct CP_RegisterEntry* entry = find_entry(ns);

    if( entry )
        return entry->machine_owned;
    /*
     * Undeclared falls back to the fact, not to yes.
     *
     * A namespace is machine-owned exactly when there is a gameval archive to
     * generate it from. `-2` (neither table knows the namespace) means cachepack
     * does not generate it either, so it is not cachepack's to rewrite.
     */
    return gameval_archive_for(ns) >= 0;
}

int
cp_register_check(void)
{
    int violations = 0;

    for( int i = 0; i < g_count; i++ )
    {
        const char* ns = g_entries[i].name;
        int archive = gameval_archive_for(ns);

        if( archive == -2 )
            continue; /* not a table cachepack has */
        if( g_entries[i].machine_owned == (archive >= 0) )
            continue;

        if( g_entries[i].machine_owned )
            fprintf(stderr,
                    "cachepack: content.ini declares `%s` as `names = cache`, but no gameval "
                    "archive names it — regenerating pack/%s.pack would delete names nothing "
                    "can put back\n",
                    ns, ns);
        else
            fprintf(stderr,
                    "cachepack: content.ini declares `%s` as authored, but gameval archive %d "
                    "names it — the cache's own names will never be imported\n",
                    ns, archive);
        violations++;
    }
    return violations;
}
