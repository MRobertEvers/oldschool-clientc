#include "editor_wma.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int
Editor_WmaNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_WMA_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

void
Editor_WmaDocKey(
    char* out,
    size_t out_size,
    const char* name)
{
    assert(out);
    assert(out_size > 0);
    assert(name);
    snprintf(out, out_size, "%s%s", EDITOR_WMA_KEY_PREFIX, name);
}

void
Editor_WmaInit(struct Editor_WmaFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

void
Editor_WmaFree(struct Editor_WmaFile* file)
{
    if( !file )
        return;
    for( int i = 0; i < file->line_count; i++ )
        ToriMem_Free(ToriMem_Editor(), file->lines[i].text);
    ToriMem_Free(ToriMem_Editor(), file->lines);
    memset(file, 0, sizeof(*file));
}

static struct Editor_WmaLine*
wma_append(struct Editor_WmaFile* file)
{
    if( file->line_count == file->line_capacity )
    {
        int const capacity = file->line_capacity ? file->line_capacity * 2 : 16;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, sizeof(file->lines[0]) * (size_t)capacity);
        assert(file->lines);
        file->line_capacity = capacity;
    }
    memset(&file->lines[file->line_count], 0, sizeof(file->lines[0]));
    file->lines[file->line_count].area = -1;
    return &file->lines[file->line_count++];
}

/** One decimal integer filling [s, e), as the reader's cat_parse_int. */
static int
wma_int(
    const char* s,
    const char* e,
    int* out)
{
    int sign = 1;
    int value = 0;
    int digits = 0;

    if( s < e && *s == '-' )
    {
        sign = -1;
        s++;
    }
    while( s < e && *s >= '0' && *s <= '9' )
    {
        value = value * 10 + (*s - '0');
        s++;
        digits++;
    }
    if( !digits || s != e )
        return 0;
    *out = sign * value;
    return 1;
}

/** Exactly EDITOR_WMA_SECTION_INTS comma-separated integers. */
static int
wma_section(
    const char* s,
    const char* e,
    struct Editor_WmaSection* out)
{
    int n = 0;

    for( ;; )
    {
        const char* comma = memchr(s, ',', (size_t)(e - s));
        const char* te = comma ? comma : e;
        if( n >= EDITOR_WMA_SECTION_INTS || !wma_int(s, te, &out->v[n]) )
            return 0;
        n++;
        if( !comma )
            break;
        s = comma + 1;
    }
    return n == EDITOR_WMA_SECTION_INTS;
}

/** `section<digits>`: the reader's family test (cat_family_len == 7). */
static int
wma_is_section_key(
    const char* key,
    size_t length)
{
    if( length <= 7 || memcmp(key, "section", 7) != 0 )
        return 0;
    for( size_t i = 7; i < length; i++ )
        if( key[i] < '0' || key[i] > '9' )
            return 0;
    return 1;
}

struct Editor_WmaParseResult
Editor_WmaParse(
    struct Editor_WmaFile* file,
    const char* text,
    size_t length)
{
    struct Editor_WmaParseResult result = { 1, 0 };
    size_t at = 0;
    int line_number = 0;
    int area = -1;

    assert(file);
    assert(text || length == 0);

    Editor_WmaFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_WmaLine* line = wma_append(file);
        const char* s;
        const char* e;
        int bad = 0;

        line_number++;
        line->text = ToriMem_Alloc(ToriMem_Editor(), line_length + 1);
        assert(line->text);
        memcpy(line->text, begin, line_length);
        line->text[line_length] = '\0';
        s = line->text;
        e = line->text + line_length;
        if( e > s && e[-1] == '\r' )
            e--;

        if( e > s && s[0] == '[' )
        {
            const char* rb = memchr(s, ']', (size_t)(e - s));
            size_t const name_length = rb ? (size_t)(rb - (s + 1)) : 0;
            if( !rb || name_length == 0 || name_length >= EDITOR_WMA_NAME_MAX || rb + 1 != e
                || file->area_count >= EDITOR_WMA_AREAS_MAX )
                bad = 1;
            else
            {
                char name[EDITOR_WMA_NAME_MAX];
                memcpy(name, s + 1, name_length);
                name[name_length] = '\0';
                if( Editor_WmaFindArea(file, name) >= 0 )
                    bad = 1;
                else
                {
                    struct Editor_WmaArea* a = &file->areas[file->area_count];
                    memset(a, 0, sizeof(*a));
                    snprintf(a->name, sizeof(a->name), "%s", name);
                    a->line = file->line_count - 1;
                    area = file->area_count++;
                    line->kind = EDITOR_WMA_LINE_HEADER;
                    line->area = area;
                }
            }
            if( bad )
                area = -1;
        }
        else if( area >= 0 && e > s && !(e - s >= 2 && s[0] == '/' && s[1] == '/') )
        {
            const char* eq = memchr(s, '=', (size_t)(e - s));
            struct Editor_WmaArea* a = &file->areas[area];
            if( eq )
            {
                size_t const key_length = (size_t)(eq - s);
                line->kind = EDITOR_WMA_LINE_FIELD;
                line->area = area;
                if( key_length == 8 && memcmp(s, "external", 8) == 0 )
                {
                    size_t const n = (size_t)(e - (eq + 1));
                    if( n >= sizeof(a->external) )
                        bad = 1;
                    else
                    {
                        memcpy(a->external, eq + 1, n);
                        a->external[n] = '\0';
                    }
                }
                else if( key_length == 4 && memcmp(s, "main", 4) == 0 )
                {
                    if( e - eq == 4 && memcmp(eq + 1, "yes", 3) == 0 )
                        a->is_main = 1;
                    else if( e - eq == 3 && memcmp(eq + 1, "no", 2) == 0 )
                        a->is_main = 0;
                    else
                        bad = 1;
                }
                else if( wma_is_section_key(s, key_length) )
                {
                    if( a->section_count >= EDITOR_WMA_SECTIONS_MAX
                        || !wma_section(eq + 1, e, &a->sections[a->section_count]) )
                        bad = 1;
                    else
                        a->section_count++;
                }
            }
        }
        if( bad )
        {
            line->kind = EDITOR_WMA_LINE_TEXT;
            line->area = -1;
            if( result.ok )
            {
                result.ok = 0;
                result.line = line_number;
            }
        }
        at += line_length;
        if( newline )
        {
            at++;
            file->final_newline = at == length;
        }
        else
            file->final_newline = 0;
    }
    return result;
}

size_t
Editor_WmaEmit(
    const struct Editor_WmaFile* file,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;

    assert(file);
    assert(out || out_capacity == 0);
    for( int i = 0; i < file->line_count; i++ )
    {
        const char* text = file->lines[i].text;
        size_t const length = strlen(text);
        int const newline = i + 1 < file->line_count || file->final_newline;
        if( out && total < out_capacity )
        {
            size_t const room = out_capacity - total - 1;
            memcpy(out + total, text, length < room ? length : room);
            if( newline && length < room )
                out[total + length] = '\n';
        }
        total += length + (size_t)newline;
    }
    if( out && out_capacity > 0 )
        out[total < out_capacity ? total : out_capacity - 1] = '\0';
    return total;
}

int
Editor_WmaFindArea(
    const struct Editor_WmaFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->area_count; i++ )
        if( strcmp(file->areas[i].name, name) == 0 )
            return i;
    return -1;
}

int
Editor_WmaAreaClaims(
    const struct Editor_WmaArea* area,
    int map_x,
    int map_z)
{
    assert(area);
    for( int i = 0; i < area->section_count; i++ )
    {
        const int* v = area->sections[i].v;
        if( v[0] == 0 )
        {
            int const x0 = v[3] < v[5] ? v[3] : v[5];
            int const x1 = v[3] < v[5] ? v[5] : v[3];
            int const z0 = v[4] < v[6] ? v[4] : v[6];
            int const z1 = v[4] < v[6] ? v[6] : v[4];
            if( map_x >= x0 && map_x <= x1 && map_z >= z0 && map_z <= z1 )
                return 1;
        }
        else if( v[3] == map_x && v[4] == map_z )
            return 1;
    }
    return 0;
}

int32_t
Editor_WmaPackCoord(
    int level,
    int x,
    int z)
{
    assert(level >= 0);
    assert(level < 4);
    assert(x >= 0);
    assert(x < 16384);
    assert(z >= 0);
    assert(z < 16384);
    return (int32_t)(((uint32_t)level << 28) | ((uint32_t)x << 14) | (uint32_t)z);
}

size_t
Editor_WmaNewAreaText(
    const char* name,
    const char* external,
    int x0,
    int z0,
    int x1,
    int z1,
    int min_plane,
    int planes,
    char* out,
    size_t out_capacity)
{
    int written;
    int const mid_x = ((x0 + x1 + 1) * 64) / 2;
    int const mid_z = ((z0 + z1 + 1) * 64) / 2;

    assert(name);
    assert(external);
    assert(out || out_capacity == 0);
    assert(Editor_WmaNameValid(name));
    assert(x0 >= 0);
    assert(x0 <= x1);
    assert(x1 < 256);
    assert(z0 >= 0);
    assert(z0 <= z1);
    assert(z1 < 256);
    assert(min_plane >= 0);
    assert(planes >= 1);
    assert(min_plane + planes <= 4);

    /* The field order and spelling are cachepack's worldmap_emit_area, so a
     * block moved into details.wma packs as written. */
    written = snprintf(out, out_capacity,
        "// worldmap/areas/%s.wma: written by the ToriRSEd instance helper "
        "(src/editor/editor_instance.c); the catalog's atlas reads it.\n"
        "[%s]\n"
        "internal=%s\n"
        "external=%s\n"
        "origin=%d\n"
        "background=0xFF000000\n"
        "main=no\n"
        "zoom=100\n"
        "unknown_int=-1\n"
        "unknown_byte=0\n"
        "section1=0,%d,%d,%d,%d,%d,%d,0,0,0,0,%d,%d,%d,%d,0,0,0\n"
        "section1_dst_chunk_y_high=0\n",
        name, name, name, external, (int)Editor_WmaPackCoord(0, mid_x, mid_z), min_plane, planes, x0, z0, x1,
        z1, x0, z0, x1, z1);
    assert(written > 0);
    return (size_t)written;
}
