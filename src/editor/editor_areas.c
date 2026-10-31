/**
 * The AREAS codec. See editor_areas.h for the file, its grammar and why it
 * keeps lines rather than records.
 */

#include "editor_areas.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What an empty file gains with its first record (and loses with its last). */
static const char* const k_header[] = {
    "# ToriRSMapEd named areas and square collections (editor/areas.txt).",
    "# area <name> | level <0..3|all> | rects <x0> <z0> <x1> <z1>[, ...] [| colour <0..7>] [| note <text>]",
    "# collection <name> | squares <key> ... [| note <text>]",
};
#define AREAS_HEADER_LINES ((int)(sizeof(k_header) / sizeof(k_header[0])))

/* Distinct on the terrain greens and browns, readable under a translucent
 * fill: red, yellow, cyan, magenta, orange, blue, lime, white. */
static const uint32_t k_palette[EDITOR_AREA_COLOURS] = {
    0xFF4040u, 0xFFE040u, 0x40E0FFu, 0xFF50E0u, 0xFF9020u, 0x5080FFu, 0x90FF40u, 0xF0F0F0u,
};

/* ---- lines ------------------------------------------------------------------ */

void
Editor_AreasInit(struct Editor_AreasFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

static void
line_free(struct Editor_AreasLine* line)
{
    ToriMem_Free(ToriMem_Editor(), line->text);
    ToriMem_Free(ToriMem_Editor(), line->area);
    ToriMem_Free(ToriMem_Editor(), line->collection);
    memset(line, 0, sizeof(*line));
}

void
Editor_AreasFree(struct Editor_AreasFile* file)
{
    if( !file )
        return;
    for( int i = 0; i < file->line_count; i++ )
        line_free(&file->lines[i]);
    ToriMem_Free(ToriMem_Editor(), file->lines);
    memset(file, 0, sizeof(*file));
}

static char*
copy_text(
    const char* text,
    size_t length)
{
    char* out = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(out);
    if( length > 0 )
        memcpy(out, text, length);
    out[length] = '\0';
    return out;
}

static struct Editor_AreasLine*
insert_slot(
    struct Editor_AreasFile* file,
    int index)
{
    assert(index >= 0);
    assert(index <= file->line_count);

    if( file->line_count == file->line_capacity )
    {
        int next = file->line_capacity ? file->line_capacity * 2 : 32;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    memmove(&file->lines[index + 1], &file->lines[index],
        (size_t)(file->line_count - index) * sizeof(*file->lines));
    file->line_count++;
    memset(&file->lines[index], 0, sizeof(file->lines[index]));
    return &file->lines[index];
}

static void
remove_slot(
    struct Editor_AreasFile* file,
    int index)
{
    assert(index >= 0);
    assert(index < file->line_count);
    line_free(&file->lines[index]);
    memmove(&file->lines[index], &file->lines[index + 1],
        (size_t)(file->line_count - index - 1) * sizeof(*file->lines));
    file->line_count--;
}

/* ---- one record line ----------------------------------------------------------- */

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/** [begin, end) trimmed of spaces at both ends. */
static void
trim(
    const char** begin,
    const char** end)
{
    while( *begin < *end && is_space(**begin) )
        (*begin)++;
    while( *end > *begin && is_space((*end)[-1]) )
        (*end)--;
}

static int
printable_run(
    const char* begin,
    const char* end)
{
    for( const char* at = begin; at < end; at++ )
    {
        unsigned char const c = (unsigned char)*at;
        if( c < 0x20 || c == 0x7f || c == '|' )
            return 0;
    }
    return 1;
}

int
Editor_AreaNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_AREA_NAME_MAX )
        return 0;
    if( is_space(name[0]) || is_space(name[length - 1]) )
        return 0;
    if( name[0] == '#' || (name[0] == '/' && name[1] == '/') )
        return 0;
    return printable_run(name, name + length);
}

int
Editor_AreaNoteValid(const char* note)
{
    size_t length;

    assert(note);
    length = strlen(note);
    return length < EDITOR_AREA_NOTE_MAX && printable_run(note, note + length);
}

int
Editor_CollectionKeyValid(const char* key)
{
    const char* bare;
    size_t length;
    int digits = 0;

    assert(key);
    length = strlen(key);
    if( length == 0 || length >= EDITOR_COLLECTION_KEY_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char const c = (unsigned char)key[i];
        if( c <= 0x20 || c == 0x7f || c == '|' || c == ',' || c == '\\' )
            return 0;
    }
    if( strstr(key, "..") || key[0] == '/' )
        return 0;
    bare = strrchr(key, '/');
    bare = bare ? bare + 1 : key;
    /* m<digits>_<digits>, the picker's own square key. */
    if( bare[0] != 'm' )
        return 0;
    bare++;
    while( *bare >= '0' && *bare <= '9' )
        bare++, digits++;
    if( digits == 0 || digits > 4 || *bare != '_' )
        return 0;
    bare++;
    digits = 0;
    while( *bare >= '0' && *bare <= '9' )
        bare++, digits++;
    return digits > 0 && digits <= 4 && *bare == '\0';
}

static int
rect_valid(const struct Editor_AreaRect* r)
{
    return r->x0 >= 0 && r->z0 >= 0 && r->x0 <= r->x1 && r->z0 <= r->z1 && r->x1 <= EDITOR_AREA_TILE_MAX
           && r->z1 <= EDITOR_AREA_TILE_MAX;
}

int
Editor_AreaFormat(
    const struct Editor_Area* area,
    char* out,
    size_t out_capacity)
{
    char buffer[EDITOR_CMD_SPLICE_MAX * 2];
    int at;

    assert(area);
    assert(out || out_capacity == 0);

    at = snprintf(buffer, sizeof(buffer), "area %s | level ", area->name);
    if( area->level < 0 )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, "all");
    else
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, "%d", area->level);
    at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | rects");
    for( int i = 0; i < area->rect_count && i < EDITOR_AREA_RECTS_MAX; i++ )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, "%s %d %d %d %d", i ? "," : "",
            area->rects[i].x0, area->rects[i].z0, area->rects[i].x1, area->rects[i].z1);
    if( area->colour >= 0 )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | colour %d", area->colour);
    if( area->note[0] )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | note %s", area->note);
    return snprintf(out, out_capacity, "%s", buffer);
}

int
Editor_CollectionFormat(
    const struct Editor_Collection* collection,
    char* out,
    size_t out_capacity)
{
    char buffer[EDITOR_CMD_SPLICE_MAX * 8];
    int at;

    assert(collection);
    assert(out || out_capacity == 0);

    at = snprintf(buffer, sizeof(buffer), "collection %s | squares", collection->name);
    for( int i = 0; i < collection->square_count && i < EDITOR_COLLECTION_SQUARES_MAX; i++ )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " %s", collection->squares[i]);
    if( collection->note[0] )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | note %s", collection->note);
    return snprintf(out, out_capacity, "%s", buffer);
}

int
Editor_AreaValid(const struct Editor_Area* area)
{
    assert(area);
    if( !Editor_AreaNameValid(area->name) || !Editor_AreaNoteValid(area->note) )
        return 0;
    if( area->level < -1 || area->level > 3 || area->colour < -1 || area->colour >= EDITOR_AREA_COLOURS )
        return 0;
    if( area->rect_count < 1 || area->rect_count > EDITOR_AREA_RECTS_MAX )
        return 0;
    for( int i = 0; i < area->rect_count; i++ )
        if( !rect_valid(&area->rects[i]) )
            return 0;
    return Editor_AreaFormat(area, NULL, 0) <= EDITOR_AREAS_LINE_MAX;
}

int
Editor_CollectionValid(const struct Editor_Collection* collection)
{
    assert(collection);
    if( !Editor_AreaNameValid(collection->name) || !Editor_AreaNoteValid(collection->note) )
        return 0;
    if( collection->square_count < 0 || collection->square_count > EDITOR_COLLECTION_SQUARES_MAX )
        return 0;
    for( int i = 0; i < collection->square_count; i++ )
    {
        if( !Editor_CollectionKeyValid(collection->squares[i]) )
            return 0;
        for( int k = 0; k < i; k++ )
            if( strcmp(collection->squares[k], collection->squares[i]) == 0 )
                return 0;
    }
    return Editor_CollectionFormat(collection, NULL, 0) <= EDITOR_AREAS_LINE_MAX;
}

/** A decimal integer at *at (spaces skipped first). */
static int
read_int(
    const char** at,
    const char* end,
    int* out)
{
    long value = 0;
    int digits = 0;

    while( *at < end && is_space(**at) )
        (*at)++;
    while( *at < end && **at >= '0' && **at <= '9' )
    {
        value = value * 10 + (**at - '0');
        if( value > EDITOR_AREA_TILE_MAX )
            return 0;
        (*at)++;
        digits++;
    }
    *out = (int)value;
    return digits > 0;
}

/** "<word> rest": 1 when [begin, end) starts with `word` then a space or its
 *  end; *rest is past the word, trimmed. */
static int
segment_is(
    const char* begin,
    const char* end,
    const char* word,
    const char** rest)
{
    size_t const length = strlen(word);
    if( (size_t)(end - begin) < length || memcmp(begin, word, length) != 0 )
        return 0;
    if( begin + length < end && !is_space(begin[length]) )
        return 0;
    *rest = begin + length;
    while( *rest < end && is_space(**rest) )
        (*rest)++;
    return 1;
}

static int
copy_run(
    char* out,
    size_t out_size,
    const char* begin,
    const char* end)
{
    if( (size_t)(end - begin) >= out_size )
        return 0;
    memcpy(out, begin, (size_t)(end - begin));
    out[end - begin] = '\0';
    return 1;
}

static int
parse_area_segment(
    struct Editor_Area* area,
    const char* begin,
    const char* end,
    unsigned* seen)
{
    const char* rest;

    if( segment_is(begin, end, "level", &rest) )
    {
        int level;
        if( *seen & 1 )
            return 0;
        *seen |= 1;
        if( end - rest == 3 && memcmp(rest, "all", 3) == 0 )
        {
            area->level = -1;
            return 1;
        }
        if( !read_int(&rest, end, &level) || rest != end || level > 3 )
            return 0;
        area->level = level;
        return 1;
    }
    if( segment_is(begin, end, "rects", &rest) )
    {
        if( *seen & 2 )
            return 0;
        *seen |= 2;
        while( rest < end )
        {
            int v[4];
            for( int k = 0; k < 4; k++ )
                if( !read_int(&rest, end, &v[k]) )
                    return 0;
            if( !Editor_AreaAddRect(area, v[0], v[1], v[2], v[3]) )
                return 0;
            while( rest < end && is_space(*rest) )
                rest++;
            if( rest < end )
            {
                if( *rest != ',' )
                    return 0;
                rest++;
            }
        }
        return area->rect_count > 0;
    }
    if( segment_is(begin, end, "colour", &rest) || segment_is(begin, end, "color", &rest) )
    {
        int colour;
        if( *seen & 4 )
            return 0;
        *seen |= 4;
        if( !read_int(&rest, end, &colour) || rest != end || colour >= EDITOR_AREA_COLOURS )
            return 0;
        area->colour = colour;
        return 1;
    }
    if( segment_is(begin, end, "note", &rest) )
    {
        if( *seen & 8 )
            return 0;
        *seen |= 8;
        return copy_run(area->note, sizeof(area->note), rest, end);
    }
    return 0;
}

static int
parse_collection_segment(
    struct Editor_Collection* collection,
    const char* begin,
    const char* end,
    unsigned* seen)
{
    const char* rest;

    if( segment_is(begin, end, "squares", &rest) )
    {
        if( *seen & 1 )
            return 0;
        *seen |= 1;
        while( rest < end )
        {
            const char* word = rest;
            char key[EDITOR_COLLECTION_KEY_MAX];
            while( rest < end && !is_space(*rest) )
                rest++;
            if( !copy_run(key, sizeof(key), word, rest) || !Editor_CollectionKeyValid(key)
                || Editor_CollectionFind(collection, key) >= 0
                || collection->square_count >= EDITOR_COLLECTION_SQUARES_MAX )
                return 0;
            snprintf(collection->squares[collection->square_count++], EDITOR_COLLECTION_KEY_MAX, "%s", key);
            while( rest < end && is_space(*rest) )
                rest++;
        }
        return 1;
    }
    if( segment_is(begin, end, "note", &rest) )
    {
        if( *seen & 2 )
            return 0;
        *seen |= 2;
        return copy_run(collection->note, sizeof(collection->note), rest, end);
    }
    return 0;
}

int
Editor_AreasParseLine(
    const char* text,
    struct Editor_Area* out_area,
    struct Editor_Collection* out_collection)
{
    struct Editor_Area area;
    struct Editor_Collection collection;
    const char* begin = text;
    const char* end;
    const char* rest;
    const char* bar;
    int is_area;
    unsigned seen = 0;

    assert(text);

    end = text + strlen(text);
    trim(&begin, &end);
    if( begin == end || begin[0] == '#' || (end - begin >= 2 && begin[0] == '/' && begin[1] == '/') )
        return EDITOR_AREAS_LINE_TEXT;

    if( segment_is(begin, end, "area", &rest) )
        is_area = 1;
    else if( segment_is(begin, end, "collection", &rest) )
        is_area = 0;
    else
        return -1;

    memset(&area, 0, sizeof(area));
    memset(&collection, 0, sizeof(collection));
    area.level = -1;
    area.colour = -1;

    /* The name runs to the first bar. */
    bar = memchr(rest, '|', (size_t)(end - rest));
    {
        const char* name_end = bar ? bar : end;
        const char* name_begin = rest;
        trim(&name_begin, &name_end);
        if( !copy_run(is_area ? area.name : collection.name, EDITOR_AREA_NAME_MAX, name_begin, name_end)
            || !Editor_AreaNameValid(is_area ? area.name : collection.name) )
            return -1;
    }
    while( bar )
    {
        const char* seg = bar + 1;
        const char* seg_end;
        bar = memchr(seg, '|', (size_t)(end - seg));
        seg_end = bar ? bar : end;
        trim(&seg, &seg_end);
        if( is_area ? !parse_area_segment(&area, seg, seg_end, &seen)
                    : !parse_collection_segment(&collection, seg, seg_end, &seen) )
            return -1;
    }
    if( is_area && area.rect_count == 0 )
        return -1;
    if( is_area && out_area )
        *out_area = area;
    if( !is_area && out_collection )
        *out_collection = collection;
    return is_area ? EDITOR_AREAS_LINE_AREA : EDITOR_AREAS_LINE_COLLECTION;
}

/** Classify `line->text` and attach its record. 0 when it does not parse. */
static int
classify(struct Editor_AreasLine* line)
{
    struct Editor_Area area;
    struct Editor_Collection* collection = ToriMem_Alloc(ToriMem_Editor(), sizeof(*collection));
    int kind;

    assert(collection);
    kind = Editor_AreasParseLine(line->text, &area, collection);
    line->kind = EDITOR_AREAS_LINE_TEXT;
    if( kind == EDITOR_AREAS_LINE_AREA )
    {
        line->kind = EDITOR_AREAS_LINE_AREA;
        line->area = ToriMem_Alloc(ToriMem_Editor(), sizeof(*line->area));
        assert(line->area);
        *line->area = area;
    }
    else if( kind == EDITOR_AREAS_LINE_COLLECTION )
    {
        line->kind = EDITOR_AREAS_LINE_COLLECTION;
        line->collection = collection;
        collection = NULL;
    }
    ToriMem_Free(ToriMem_Editor(), collection);
    return kind >= 0;
}

static const char*
line_name(const struct Editor_AreasLine* line)
{
    if( line->area )
        return line->area->name;
    if( line->collection )
        return line->collection->name;
    return NULL;
}

static int
find_named(
    const struct Editor_AreasFile* file,
    enum Editor_AreasLineKind kind,
    const char* name,
    int skip)
{
    for( int i = 0; i < file->line_count; i++ )
        if( i != skip && file->lines[i].kind == kind && strcmp(line_name(&file->lines[i]), name) == 0 )
            return i;
    return -1;
}

struct Editor_AreasParseResult
Editor_AreasParse(
    struct Editor_AreasFile* file,
    const char* text,
    size_t length)
{
    struct Editor_AreasParseResult result = { 1, 0 };
    size_t at = 0;
    int line_number = 0;

    assert(file);
    assert(text || length == 0);

    Editor_AreasFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_AreasLine* line = insert_slot(file, file->line_count);
        int ok;

        line_number++;
        line->text = copy_text(begin, line_length);
        ok = classify(line);
        /* Names are unique per kind: a second row of the same name would make
         * every by-name command ambiguous. */
        if( ok && line->kind != EDITOR_AREAS_LINE_TEXT
            && find_named(file, line->kind, line_name(line), file->line_count - 1) >= 0 )
            ok = 0;
        if( !ok && result.ok )
        {
            result.ok = 0;
            result.line = line_number;
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
Editor_AreasEmit(
    const struct Editor_AreasFile* file,
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
            size_t const room = out_capacity - total;
            memcpy(out + total, text, length < room ? length : room);
        }
        total += length;
        if( newline )
        {
            if( out && total < out_capacity )
                out[total] = '\n';
            total++;
        }
    }
    if( out_capacity > 0 )
        out[total < out_capacity ? total : out_capacity - 1] = '\0';
    return total;
}

int
Editor_AreasFindArea(
    const struct Editor_AreasFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    return find_named(file, EDITOR_AREAS_LINE_AREA, name, -1);
}

int
Editor_AreasFindCollection(
    const struct Editor_AreasFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    return find_named(file, EDITOR_AREAS_LINE_COLLECTION, name, -1);
}

static int
count_kind(
    const struct Editor_AreasFile* file,
    enum Editor_AreasLineKind kind)
{
    int n = 0;
    for( int i = 0; i < file->line_count; i++ )
        n += file->lines[i].kind == kind;
    return n;
}

static int
nth_kind(
    const struct Editor_AreasFile* file,
    enum Editor_AreasLineKind kind,
    int nth)
{
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == kind && nth-- == 0 )
            return i;
    return -1;
}

int
Editor_AreasAreaCount(const struct Editor_AreasFile* file)
{
    assert(file);
    return count_kind(file, EDITOR_AREAS_LINE_AREA);
}

int
Editor_AreasCollectionCount(const struct Editor_AreasFile* file)
{
    assert(file);
    return count_kind(file, EDITOR_AREAS_LINE_COLLECTION);
}

int
Editor_AreasNthArea(
    const struct Editor_AreasFile* file,
    int nth)
{
    assert(file);
    return nth_kind(file, EDITOR_AREAS_LINE_AREA, nth);
}

int
Editor_AreasNthCollection(
    const struct Editor_AreasFile* file,
    int nth)
{
    assert(file);
    return nth_kind(file, EDITOR_AREAS_LINE_COLLECTION, nth);
}

/* ---- record helpers ---------------------------------------------------------- */

int
Editor_AreaAddRect(
    struct Editor_Area* area,
    int x0,
    int z0,
    int x1,
    int z1)
{
    struct Editor_AreaRect r;

    assert(area);
    if( area->rect_count >= EDITOR_AREA_RECTS_MAX )
        return 0;
    r.x0 = x0 < x1 ? x0 : x1;
    r.x1 = x0 < x1 ? x1 : x0;
    r.z0 = z0 < z1 ? z0 : z1;
    r.z1 = z0 < z1 ? z1 : z0;
    if( !rect_valid(&r) )
        return 0;
    area->rects[area->rect_count++] = r;
    return 1;
}

void
Editor_AreaCentre(
    const struct Editor_Area* area,
    int* out_x,
    int* out_z)
{
    int x0;
    int z0;
    int x1;
    int z1;

    assert(area);
    assert(out_x);
    assert(out_z);
    assert(area->rect_count > 0);

    x0 = area->rects[0].x0;
    z0 = area->rects[0].z0;
    x1 = area->rects[0].x1;
    z1 = area->rects[0].z1;
    for( int i = 1; i < area->rect_count; i++ )
    {
        x0 = area->rects[i].x0 < x0 ? area->rects[i].x0 : x0;
        z0 = area->rects[i].z0 < z0 ? area->rects[i].z0 : z0;
        x1 = area->rects[i].x1 > x1 ? area->rects[i].x1 : x1;
        z1 = area->rects[i].z1 > z1 ? area->rects[i].z1 : z1;
    }
    *out_x = (x0 + x1) / 2;
    *out_z = (z0 + z1) / 2;
}

int
Editor_CollectionFind(
    const struct Editor_Collection* collection,
    const char* key)
{
    assert(collection);
    assert(key);
    for( int i = 0; i < collection->square_count; i++ )
        if( strcmp(collection->squares[i], key) == 0 )
            return i;
    return -1;
}

int
Editor_CollectionAdd(
    struct Editor_Collection* collection,
    const char* key)
{
    assert(collection);
    assert(key);
    if( !Editor_CollectionKeyValid(key) || Editor_CollectionFind(collection, key) >= 0
        || collection->square_count >= EDITOR_COLLECTION_SQUARES_MAX )
        return 0;
    snprintf(collection->squares[collection->square_count++], EDITOR_COLLECTION_KEY_MAX, "%s", key);
    if( Editor_CollectionFormat(collection, NULL, 0) > EDITOR_AREAS_LINE_MAX )
    {
        collection->square_count--;
        return 0;
    }
    return 1;
}

int
Editor_CollectionRemove(
    struct Editor_Collection* collection,
    const char* key)
{
    int index;

    assert(collection);
    assert(key);
    index = Editor_CollectionFind(collection, key);
    if( index < 0 )
        return 0;
    memmove(collection->squares[index], collection->squares[index + 1],
        (size_t)(collection->square_count - index - 1) * sizeof(collection->squares[0]));
    collection->square_count--;
    return 1;
}

int
Editor_AreaColourIndex(
    const struct Editor_Area* area,
    int nth)
{
    assert(area);
    if( area->colour >= 0 )
        return area->colour;
    return (nth < 0 ? 0 : nth) % EDITOR_AREA_COLOURS;
}

uint32_t
Editor_AreaPaletteRgb(int index)
{
    int const wrapped = ((index % EDITOR_AREA_COLOURS) + EDITOR_AREA_COLOURS) % EDITOR_AREA_COLOURS;
    return k_palette[wrapped];
}

/* ---- commands ------------------------------------------------------------------ */

static void
fill_command(
    struct Editor_Cmd* out,
    enum Editor_CmdKind kind,
    const char* before,
    const char* after,
    int line)
{
    size_t const before_len = before ? strlen(before) : 0;
    size_t const after_len = after ? strlen(after) : 0;

    assert(before_len <= EDITOR_AREAS_LINE_MAX);
    assert(after_len <= EDITOR_AREAS_LINE_MAX);

    Editor_CmdInit(out, EDITOR_DOC_AREAS, EDITOR_AREAS_DOC_KEY, kind);
    out->has_before = before != NULL;
    out->has_after = after != NULL;
    out->u.area.line = line;
    out->u.area.before_len = (uint32_t)before_len;
    out->u.area.after_len = (uint32_t)after_len;
    if( before_len > 0 )
        memcpy(out->u.area.text, before, before_len);
    if( after_len > 0 )
        memcpy(out->u.area.text + before_len, after, after_len);
}

/** The SET for a record whose formatted row is `row`, named `new_name`, of
 *  `kind`, replacing the record `old_name` (NULL/"" = new). */
static int
set_command(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    enum Editor_AreasLineKind kind,
    enum Editor_CmdKind cmd_kind,
    const char* old_name,
    const char* new_name,
    const char* row)
{
    int old_line = -1;
    int taken;

    if( old_name && old_name[0] )
    {
        old_line = find_named(file, kind, old_name, -1);
        if( old_line < 0 )
            return 0;
        if( strcmp(file->lines[old_line].text, row) == 0 )
            return 0; /* nothing would change */
    }
    taken = find_named(file, kind, new_name, old_line);
    if( taken >= 0 )
        return 0;
    fill_command(out, cmd_kind, old_line >= 0 ? file->lines[old_line].text : NULL, row, old_line);
    return 1;
}

int
Editor_AreaSetCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* old_name,
    const struct Editor_Area* area)
{
    char row[EDITOR_AREAS_LINE_MAX + 1];

    assert(out);
    assert(file);
    assert(area);
    if( !Editor_AreaValid(area) )
        return 0;
    Editor_AreaFormat(area, row, sizeof(row));
    return set_command(out, file, EDITOR_AREAS_LINE_AREA, EDITOR_CMD_AREA_SET, old_name, area->name, row);
}

int
Editor_CollectionSetCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* old_name,
    const struct Editor_Collection* collection)
{
    char row[EDITOR_AREAS_LINE_MAX + 1];

    assert(out);
    assert(file);
    assert(collection);
    if( !Editor_CollectionValid(collection) )
        return 0;
    Editor_CollectionFormat(collection, row, sizeof(row));
    return set_command(
        out, file, EDITOR_AREAS_LINE_COLLECTION, EDITOR_CMD_COLLECTION_SET, old_name, collection->name, row);
}

static int
remove_command(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    enum Editor_AreasLineKind kind,
    enum Editor_CmdKind cmd_kind,
    const char* name)
{
    int const line = find_named(file, kind, name, -1);
    if( line < 0 )
        return 0;
    /* A row hand-written past the command's room cannot ride in one; it is
     * removed by editing the file. */
    if( strlen(file->lines[line].text) > EDITOR_AREAS_LINE_MAX )
        return 0;
    fill_command(out, cmd_kind, file->lines[line].text, NULL, line);
    return 1;
}

int
Editor_AreaRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* name)
{
    assert(out);
    assert(file);
    assert(name);
    return remove_command(out, file, EDITOR_AREAS_LINE_AREA, EDITOR_CMD_AREA_REMOVE, name);
}

int
Editor_CollectionRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_AreasFile* file,
    const char* name)
{
    assert(out);
    assert(file);
    assert(name);
    return remove_command(out, file, EDITOR_AREAS_LINE_COLLECTION, EDITOR_CMD_COLLECTION_REMOVE, name);
}

void
Editor_AreasCmdLine(
    const struct Editor_Cmd* command,
    int after,
    char* out,
    size_t out_size)
{
    uint32_t length;
    const char* at;

    assert(command);
    assert(out);
    assert(out_size > 0);

    length = after ? command->u.area.after_len : command->u.area.before_len;
    at = command->u.area.text + (after ? command->u.area.before_len : 0);
    if( command->u.area.before_len > EDITOR_CMD_SPLICE_MAX
        || command->u.area.after_len > EDITOR_CMD_SPLICE_MAX - command->u.area.before_len )
        length = 0;
    if( length >= out_size )
        length = (uint32_t)out_size - 1;
    memcpy(out, at, length);
    out[length] = '\0';
}

static int
cmd_line_kind(enum Editor_CmdKind kind)
{
    return kind == EDITOR_CMD_AREA_SET || kind == EDITOR_CMD_AREA_REMOVE ? EDITOR_AREAS_LINE_AREA
                                                                          : EDITOR_AREAS_LINE_COLLECTION;
}

/** The file holds nothing but the header lines. */
static int
only_header(const struct Editor_AreasFile* file)
{
    if( file->line_count != AREAS_HEADER_LINES )
        return 0;
    for( int i = 0; i < AREAS_HEADER_LINES; i++ )
        if( strcmp(file->lines[i].text, k_header[i]) != 0 )
            return 0;
    return 1;
}

/** Insert record line `text` at `index` (-1 or past the end: append). An
 *  empty file gets the header first. */
static void
insert_record(
    struct Editor_AreasFile* file,
    int index,
    const char* text)
{
    struct Editor_AreasLine* line;
    int ok;

    if( file->line_count == 0 )
    {
        for( int i = 0; i < AREAS_HEADER_LINES; i++ )
        {
            struct Editor_AreasLine* header = insert_slot(file, file->line_count);
            header->text = copy_text(k_header[i], strlen(k_header[i]));
            header->kind = EDITOR_AREAS_LINE_TEXT;
        }
        file->final_newline = 1;
    }
    if( index < 0 || index > file->line_count )
        index = file->line_count;
    line = insert_slot(file, index);
    line->text = copy_text(text, strlen(text));
    ok = classify(line);
    assert(ok);
    (void)ok;
}

static void
remove_record(
    struct Editor_AreasFile* file,
    int index)
{
    remove_slot(file, index);
    if( only_header(file) )
    {
        while( file->line_count > 0 )
            remove_slot(file, file->line_count - 1);
    }
}

static void
replace_record(
    struct Editor_AreasFile* file,
    int index,
    const char* text)
{
    struct Editor_AreasLine* line = &file->lines[index];
    int ok;

    line_free(line);
    line->text = copy_text(text, strlen(text));
    ok = classify(line);
    assert(ok);
    (void)ok;
}

int
Editor_AreasApply(
    struct Editor_AreasFile* file,
    const struct Editor_Cmd* command,
    int inverse)
{
    char before[EDITOR_CMD_SPLICE_MAX + 1];
    char after[EDITOR_CMD_SPLICE_MAX + 1];
    struct Editor_Area area_before;
    struct Editor_Area area_after;
    struct Editor_Collection* coll = NULL;
    int kind;
    int has_expect;
    int has_want;
    const char* expect;
    const char* want;
    int at = -1;
    int result = 0;

    assert(file);
    assert(command);

    if( command->kind != EDITOR_CMD_AREA_SET && command->kind != EDITOR_CMD_AREA_REMOVE
        && command->kind != EDITOR_CMD_COLLECTION_SET && command->kind != EDITOR_CMD_COLLECTION_REMOVE )
        return 0;
    if( command->u.area.before_len > EDITOR_AREAS_LINE_MAX || command->u.area.after_len > EDITOR_AREAS_LINE_MAX )
        return 0;
    kind = cmd_line_kind(command->kind);
    Editor_AreasCmdLine(command, 0, before, sizeof(before));
    Editor_AreasCmdLine(command, 1, after, sizeof(after));

    /* The shape a command may have: SET has an after (and a before when it
     * replaces), REMOVE only a before. */
    if( command->kind == EDITOR_CMD_AREA_SET || command->kind == EDITOR_CMD_COLLECTION_SET )
    {
        if( !command->has_after )
            return 0;
    }
    else if( !command->has_before || command->has_after )
        return 0;

    has_expect = inverse ? command->has_after : command->has_before;
    has_want = inverse ? command->has_before : command->has_after;
    expect = inverse ? after : before;
    want = inverse ? before : after;

    /* Both lines must be records of the command's kind; their names key the
     * search (unique per kind). */
    coll = ToriMem_Alloc(ToriMem_Editor(), sizeof(*coll));
    assert(coll);
    if( has_expect && Editor_AreasParseLine(expect, &area_before, coll) != kind )
        goto done;
    if( has_expect )
    {
        const char* name = kind == EDITOR_AREAS_LINE_AREA ? area_before.name : coll->name;
        at = find_named(file, (enum Editor_AreasLineKind)kind, name, -1);
        if( at < 0 || strcmp(file->lines[at].text, expect) != 0 )
            goto done;
    }
    if( has_want )
    {
        const char* name;
        if( Editor_AreasParseLine(want, &area_after, coll) != kind )
            goto done;
        name = kind == EDITOR_AREAS_LINE_AREA ? area_after.name : coll->name;
        if( find_named(file, (enum Editor_AreasLineKind)kind, name, at) >= 0 )
            goto done;
    }

    if( has_expect && has_want )
        replace_record(file, at, want);
    else if( has_expect )
        remove_record(file, at);
    else
        insert_record(file, command->kind == EDITOR_CMD_AREA_REMOVE || command->kind == EDITOR_CMD_COLLECTION_REMOVE
                                ? command->u.area.line
                                : -1,
            want);
    result = 1;
done:
    ToriMem_Free(ToriMem_Editor(), coll);
    return result;
}

void
Editor_AreasCmdLabel(
    const struct Editor_Cmd* command,
    char* out,
    size_t out_size)
{
    char before[EDITOR_CMD_SPLICE_MAX + 1];
    char after[EDITOR_CMD_SPLICE_MAX + 1];
    struct Editor_Area* a;
    struct Editor_Collection* c;
    int is_area;
    const char* what;
    const char* old_name;
    const char* new_name;

    assert(command);
    assert(out);
    assert(out_size > 0);

    a = ToriMem_Calloc(ToriMem_Editor(), 2, sizeof(*a));
    c = ToriMem_Calloc(ToriMem_Editor(), 2, sizeof(*c));
    assert(a);
    assert(c);
    is_area = command->kind == EDITOR_CMD_AREA_SET || command->kind == EDITOR_CMD_AREA_REMOVE;
    what = is_area ? "area" : "collection";

    Editor_AreasCmdLine(command, 0, before, sizeof(before));
    Editor_AreasCmdLine(command, 1, after, sizeof(after));
    if( command->has_before )
        (void)Editor_AreasParseLine(before, &a[0], &c[0]);
    if( command->has_after )
        (void)Editor_AreasParseLine(after, &a[1], &c[1]);
    old_name = is_area ? a[0].name : c[0].name;
    new_name = is_area ? a[1].name : c[1].name;

    if( command->has_before && !command->has_after )
        snprintf(out, out_size, "Delete %s %s", what, old_name);
    else if( !command->has_before )
        snprintf(out, out_size, "%s %s %s", is_area ? "Mark" : "New", what, new_name);
    else if( strcmp(old_name, new_name) != 0 )
        snprintf(out, out_size, "Rename %s %s to %s", what, old_name, new_name);
    else if( is_area && a[0].level != a[1].level )
    {
        if( a[1].level < 0 )
            snprintf(out, out_size, "Set level of area %s to all", new_name);
        else
            snprintf(out, out_size, "Set level of area %s to %d", new_name, a[1].level);
    }
    else if( is_area && a[1].rect_count > a[0].rect_count )
        snprintf(out, out_size, "Add rect to area %s", new_name);
    else if( !is_area && c[1].square_count > c[0].square_count )
        snprintf(out, out_size, "Add %d square%s to collection %s", c[1].square_count - c[0].square_count,
            c[1].square_count - c[0].square_count == 1 ? "" : "s", new_name);
    else if( !is_area && c[1].square_count < c[0].square_count )
        snprintf(out, out_size, "Remove %d square%s from collection %s", c[0].square_count - c[1].square_count,
            c[0].square_count - c[1].square_count == 1 ? "" : "s", new_name);
    else
        snprintf(out, out_size, "Edit %s %s", what, new_name);
    ToriMem_Free(ToriMem_Editor(), a);
    ToriMem_Free(ToriMem_Editor(), c);
}
