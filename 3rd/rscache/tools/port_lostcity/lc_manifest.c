#include "lc_manifest.h"

#include "ini.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
lc_request_add(
    struct LC_RequestList* list,
    const char* arg)
{
    assert(list && arg);
    if( list->count >= list->cap )
    {
        int cap = list->cap == 0 ? 16 : list->cap * 2;
        struct LC_Request* grown = realloc(list->items, (size_t)cap * sizeof(*grown));
        if( !grown )
            return 0;
        list->items = grown;
        list->cap = cap;
    }
    struct LC_Request* req = &list->items[list->count];
    memset(req, 0, sizeof(*req));
    snprintf(req->raw, sizeof(req->raw), "%s", arg);

    const char* eq = strchr(arg, '=');
    req->id = atoi(arg);
    if( eq && eq[1] )
    {
        snprintf(req->name, sizeof(req->name), "%s", eq + 1);
        req->has_name = 1;
    }
    list->count++;
    return 1;
}

void
lc_request_list_free(struct LC_RequestList* list)
{
    if( !list )
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void
lc_manifest_init(struct LC_Manifest* manifest)
{
    assert(manifest);
    memset(manifest, 0, sizeof(*manifest));
    manifest->max_textures = -1;
}

/*
 * Strip the blanks the reader leaves around a key or value, and cut a trailing
 * comment.
 *
 * The vendored reader hands back everything between `=` and the newline, so
 * `7706=inferno_zuk  ; the boss` arrives with both. Cutting at `;`/`#` is safe
 * for every value this manifest carries: LostCity names are `[a-z0-9_]`, and a
 * path holding a comment character would be pathological.
 */
static void
trim(char* text)
{
    assert(text);
    char* cut = strpbrk(text, ";#");
    if( cut )
        *cut = '\0';
    char* start = text;
    while( *start == ' ' || *start == '\t' )
        start++;
    size_t len = strlen(start);
    while( len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                       start[len - 1] == '\r' || start[len - 1] == '\n') )
        start[--len] = '\0';
    if( start != text )
        memmove(text, start, len + 1);
}

/* Join a manifest-relative value onto the manifest's directory. Absolute values
 * and an empty base copy through unchanged — same rule boot manifests use. */
static void
join_path(
    char* dst,
    size_t cap,
    const char* manifest_dir,
    const char* value)
{
    if( value[0] == '/' || manifest_dir[0] == '\0' )
        snprintf(dst, cap, "%s", value);
    else
        snprintf(dst, cap, "%s/%s", manifest_dir, value);
}

enum manifest_section
{
    SECTION_NONE = 0,
    SECTION_PORT,
    SECTION_NPC,
    SECTION_SEQ,
    SECTION_SPOTANIM,
    SECTION_LOC,
    SECTION_MAP,
    SECTION_TEXTURE,
};

static enum manifest_section
section_of(const char* header)
{
    if( strcmp(header, "port:lostcity") == 0 )
        return SECTION_PORT;
    if( strcmp(header, "export:npc") == 0 )
        return SECTION_NPC;
    if( strcmp(header, "export:seq") == 0 )
        return SECTION_SEQ;
    if( strcmp(header, "export:spotanim") == 0 )
        return SECTION_SPOTANIM;
    if( strcmp(header, "export:loc") == 0 )
        return SECTION_LOC;
    if( strcmp(header, "export:map") == 0 )
        return SECTION_MAP;
    if( strcmp(header, "export:texture") == 0 )
        return SECTION_TEXTURE;
    return SECTION_NONE;
}

static struct LC_RequestList*
list_for_section(
    struct LC_Manifest* manifest,
    enum manifest_section section)
{
    switch( section )
    {
    case SECTION_NPC:
        return &manifest->npcs;
    case SECTION_SEQ:
        return &manifest->seqs;
    case SECTION_SPOTANIM:
        return &manifest->spotanims;
    case SECTION_LOC:
        return &manifest->locs;
    case SECTION_MAP:
        return &manifest->maps;
    case SECTION_TEXTURE:
        return &manifest->textures;
    default:
        return NULL;
    }
}

/* An `[export:*]` line is `<id>=<name>`, name optional. Rebuilt into the text
 * the CLI flags take so both paths share one parser. */
static int
add_export(
    struct LC_RequestList* list,
    const char* section,
    const char* key,
    const char* value)
{
    if( key[0] == '\0' )
    {
        fprintf(stderr, "manifest: [%s] has a line with no id\n", section);
        return 0;
    }
    char arg[192];
    if( value[0] == '\0' )
        snprintf(arg, sizeof(arg), "%s", key);
    else
        snprintf(arg, sizeof(arg), "%s=%s", key, value);
    if( !lc_request_add(list, arg) )
    {
        fprintf(stderr, "manifest: out of memory adding [%s] %s\n", section, arg);
        return 0;
    }
    return 1;
}

static int
parse_int(
    const char* section,
    const char* key,
    const char* value,
    int* out)
{
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if( end == value || *end != '\0' || parsed < 0 )
    {
        fprintf(
            stderr,
            "manifest: [%s] %s must be a non-negative int, got '%s'\n",
            section,
            key,
            value);
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int
set_port_key(
    struct LC_Manifest* manifest,
    const char* manifest_dir,
    const char* key,
    const char* value)
{
    if( strcmp(key, "rev") == 0 )
        snprintf(manifest->rev, sizeof(manifest->rev), "%s", value);
    else if( strcmp(key, "cache") == 0 )
        join_path(manifest->cache_dir, sizeof(manifest->cache_dir), manifest_dir, value);
    else if( strcmp(key, "content") == 0 )
        join_path(manifest->content_dir, sizeof(manifest->content_dir), manifest_dir, value);
    else if( strcmp(key, "area") == 0 )
        snprintf(manifest->area, sizeof(manifest->area), "%s", value);
    else if( strcmp(key, "prefix") == 0 )
        snprintf(manifest->prefix, sizeof(manifest->prefix), "%s", value);
    else if( strcmp(key, "max_textures") == 0 )
        return parse_int("port:lostcity", key, value, &manifest->max_textures);
    else
    {
        fprintf(stderr, "manifest: [port:lostcity] unknown key '%s'\n", key);
        return 0;
    }
    return 1;
}

/** Where the reader is, between elements. */
struct manifest_parse
{
    struct LC_Manifest* manifest;
    const char* manifest_dir;
    enum manifest_section section;
    char section_name[64];
};

/** Fold one INI element into the manifest. Returns 0 to stop the load. */
static int
handle_element(
    struct manifest_parse* parse,
    struct INIElement* element)
{
    if( element->kind == INI_ELEMENT_SECTION )
    {
        trim(element->_section.name);
        parse->section = section_of(element->_section.name);
        snprintf(parse->section_name, sizeof(parse->section_name), "%s", element->_section.name);
        if( parse->section == SECTION_NONE )
        {
            fprintf(stderr, "manifest: unknown section [%s]\n", parse->section_name);
            return 0;
        }
        return 1;
    }
    if( element->kind != INI_ELEMENT_KEYVAL )
        return 1;

    trim(element->_keyval.name);
    trim(element->_keyval.value);
    if( element->_keyval.name[0] == '\0' && element->_keyval.value[0] == '\0' )
        return 1;

    if( parse->section == SECTION_PORT )
        return set_port_key(
            parse->manifest, parse->manifest_dir, element->_keyval.name, element->_keyval.value);

    struct LC_RequestList* list = list_for_section(parse->manifest, parse->section);
    if( !list )
    {
        fprintf(stderr, "manifest: key '%s' outside a section\n", element->_keyval.name);
        return 0;
    }
    return add_export(
        list, parse->section_name, element->_keyval.name, element->_keyval.value);
}

static char*
read_whole_file(
    const char* path,
    long* out_size)
{
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        fprintf(stderr, "manifest: cannot open %s\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        fprintf(stderr, "manifest: cannot size %s\n", path);
        return NULL;
    }
    char* data = malloc((size_t)size + 1);
    if( !data )
    {
        fclose(file);
        return NULL;
    }
    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        fclose(file);
        free(data);
        fprintf(stderr, "manifest: short read %s\n", path);
        return NULL;
    }
    data[size] = '\0';
    fclose(file);
    *out_size = size;
    return data;
}

int
lc_manifest_load(
    struct LC_Manifest* manifest,
    const char* path)
{
    assert(manifest && path);

    long size = 0;
    char* data = read_whole_file(path, &size);
    if( !data )
        return 0;

    char manifest_dir[1024];
    snprintf(manifest_dir, sizeof(manifest_dir), "%s", path);
    char* slash = strrchr(manifest_dir, '/');
    if( slash )
        *slash = '\0';
    else
        manifest_dir[0] = '\0';

    struct INIReader reader;
    ini_reader_init(&reader);

    struct manifest_parse parse;
    memset(&parse, 0, sizeof(parse));
    parse.manifest = manifest;
    parse.manifest_dir = manifest_dir;
    parse.section = SECTION_NONE;

    int ok = 1;
    int status = TORI_INI_ERR_NONE;
    struct INIElement element;
    while( (status = ini_reader_next(&reader, (uint8_t*)data, (uint32_t)size, &element)) ==
           TORI_INI_ERR_OK )
    {
        if( !handle_element(&parse, &element) )
        {
            ok = 0;
            break;
        }
    }

    free(data);
    if( !ok )
        return 0;

    /* Only end-of-file returns NONE; every other non-OK code is the reader
     * refusing the text, including the length limits it reports without
     * marking the reader itself as failed. */
    if( status != TORI_INI_ERR_NONE )
    {
        fprintf(
            stderr,
            "manifest: parse error %d in %s near byte %u\n",
            status,
            path,
            reader.offset);
        return 0;
    }
    return 1;
}

void
lc_manifest_free(struct LC_Manifest* manifest)
{
    if( !manifest )
        return;
    lc_request_list_free(&manifest->npcs);
    lc_request_list_free(&manifest->seqs);
    lc_request_list_free(&manifest->spotanims);
    lc_request_list_free(&manifest->locs);
    lc_request_list_free(&manifest->maps);
    lc_request_list_free(&manifest->textures);
}
