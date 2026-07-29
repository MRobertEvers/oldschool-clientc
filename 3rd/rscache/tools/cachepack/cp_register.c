#include "cp_register.h"

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
    for( int i = 0; i < g_count; i++ )
    {
        if( strcmp(g_entries[i].name, ns) == 0 )
            return g_entries[i].machine_owned;
    }
    /* Undeclared means machine-owned, which is what cachepack assumed before the
     * register existed. */
    return true;
}
