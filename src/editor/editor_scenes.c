/**
 * The SCENES codec. See editor_scenes.h for the file, its grammar and why it
 * keeps lines rather than records.
 */

#include "editor_scenes.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What an empty file gains with its first record (and loses with its last). */
static const char* const k_header[] = {
    "# ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.",
    "# scene <name> | squares <key> ... | camera <x>,<z>,<level>,<yaw>,<pitch>,<zoom>",
    "# scene <name> | level <0..3|auto> [above] | lane <name> | preview <var>=<value> ... | account <name> at "
    "<x>,<z>,<level>",
    "# scene <name> | note <text>",
};
#define SCENES_HEADER_LINES ((int)(sizeof(k_header) / sizeof(k_header[0])))

/* The editor's three lines: where it looks, what it pins, the note. */
static const unsigned k_format_masks[EDITOR_SCENE_FORMAT_LINES] = {
    EDITOR_SCENE_SEG_SQUARES | EDITOR_SCENE_SEG_CAMERA,
    EDITOR_SCENE_SEG_LEVEL | EDITOR_SCENE_SEG_LANE | EDITOR_SCENE_SEG_PREVIEW | EDITOR_SCENE_SEG_ACCOUNT,
    EDITOR_SCENE_SEG_NOTE,
};

/* ---- lines ------------------------------------------------------------------ */

void
Editor_ScenesInit(struct Editor_ScenesFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

static void
line_free(struct Editor_ScenesLine* line)
{
    ToriMem_Free(ToriMem_Editor(), line->text);
    ToriMem_Free(ToriMem_Editor(), line->row);
    memset(line, 0, sizeof(*line));
}

void
Editor_ScenesFree(struct Editor_ScenesFile* file)
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

static struct Editor_ScenesLine*
insert_slot(
    struct Editor_ScenesFile* file,
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
    struct Editor_ScenesFile* file,
    int index)
{
    assert(index >= 0);
    assert(index < file->line_count);
    line_free(&file->lines[index]);
    memmove(&file->lines[index], &file->lines[index + 1],
        (size_t)(file->line_count - index - 1) * sizeof(*file->lines));
    file->line_count--;
}

/* ---- tokens -------------------------------------------------------------------- */

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

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

static void
skip_space(
    const char** at,
    const char* end)
{
    while( *at < end && is_space(**at) )
        (*at)++;
}

/** "<word> rest": 1 when [begin, end) starts with `word` then a space or its
 *  end; *rest is past the word, spaces skipped. */
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
    skip_space(rest, end);
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

/** The next space-separated word of [*at, end) into out; 0 at the end or
 *  when it does not fit. */
static int
next_word(
    const char** at,
    const char* end,
    char* out,
    size_t out_size)
{
    const char* begin;

    skip_space(at, end);
    begin = *at;
    while( *at < end && !is_space(**at) )
        (*at)++;
    if( *at == begin )
        return 0;
    return copy_run(out, out_size, begin, *at);
}

/** A decimal integer, optionally signed, at most `max` in magnitude. */
static int
read_int(
    const char** at,
    const char* end,
    int allow_sign,
    long max,
    long* out)
{
    long value = 0;
    int digits = 0;
    int negative = 0;

    if( allow_sign && *at < end && **at == '-' )
    {
        negative = 1;
        (*at)++;
    }
    while( *at < end && **at >= '0' && **at <= '9' )
    {
        value = value * 10 + (**at - '0');
        if( value > max )
            return 0;
        (*at)++;
        digits++;
    }
    *out = negative ? -value : value;
    return digits > 0;
}

/** A tile with an optional fraction ("3222", "3222.5") as fine units. */
static int
read_fine(
    const char** at,
    const char* end,
    int32_t* out)
{
    long tile;
    long fraction = 0;
    long scale = 1;
    long fine;

    if( !read_int(at, end, 0, EDITOR_SCENE_TILE_MAX, &tile) )
        return 0;
    if( *at < end && **at == '.' )
    {
        int digits = 0;
        (*at)++;
        while( *at < end && **at >= '0' && **at <= '9' )
        {
            if( digits == 7 )
                return 0;
            fraction = fraction * 10 + (**at - '0');
            scale *= 10;
            (*at)++;
            digits++;
        }
        if( digits == 0 )
            return 0;
    }
    /* Rounded to the nearest fine unit: what Editor_SceneFormatFine writes
     * reads back exactly (1/128 has seven decimals). */
    fine = tile * 128 + (fraction * 128 + scale / 2) / scale;
    if( fine > (long)EDITOR_SCENE_TILE_MAX * 128 + 127 )
        return 0;
    *out = (int32_t)fine;
    return 1;
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

/** A word: printable, no space, no `|`. */
static int
word_valid(
    const char* text,
    size_t max)
{
    size_t const length = strlen(text);
    if( length == 0 || length >= max )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char const c = (unsigned char)text[i];
        if( c <= 0x20 || c == 0x7f || c == '|' )
            return 0;
    }
    return 1;
}

int
Editor_SceneNameValid(const char* name)
{
    assert(name);
    if( !word_valid(name, EDITOR_SCENE_NAME_MAX) )
        return 0;
    return name[0] != '#' && !(name[0] == '/' && name[1] == '/');
}

/** The squares the picker names: anything ending `m<x>_<z>`, with or
 *  without a storage before a `/`. */
static int
square_key_valid(const char* key)
{
    const char* bare;
    const char* at;
    int digits;

    if( !word_valid(key, EDITOR_SCENE_KEY_MAX) )
        return 0;
    bare = strrchr(key, '/');
    bare = bare ? bare + 1 : key;
    if( bare[0] != 'm' )
        return 0;
    at = bare + 1;
    for( digits = 0; *at >= '0' && *at <= '9'; at++ )
        digits++;
    if( digits == 0 || *at != '_' )
        return 0;
    at++;
    for( digits = 0; *at >= '0' && *at <= '9'; at++ )
        digits++;
    return digits > 0 && *at == '\0';
}

int
Editor_SceneVarValid(const char* var)
{
    const char* at;
    int digits = 0;

    assert(var);
    if( !word_valid(var, EDITOR_SCENE_VAR_MAX) || strchr(var, '=') )
        return 0;
    if( strncmp(var, "varp", 4) != 0 && strncmp(var, "varb", 4) != 0 )
        return 0;
    for( at = var + 4; *at >= '0' && *at <= '9'; at++ )
        digits++;
    return digits > 0 && (*at == '\0' || *at == '_');
}

/** A lane: torirs_maped_lanes.h's names, or "main". */
static int
lane_valid(const char* lane)
{
    size_t const length = strlen(lane);
    if( length == 0 || length > 40 || length >= EDITOR_SCENE_LANE_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = lane[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

static int
account_valid(const char* account)
{
    return word_valid(account, EDITOR_SCENE_ACCOUNT_MAX) && strchr(account, ',') == NULL;
}

static int
note_valid(const char* note)
{
    size_t const length = strlen(note);
    return length > 0 && length < EDITOR_SCENE_NOTE_MAX && printable_run(note, note + length) && !is_space(note[0])
           && !is_space(note[length - 1]);
}

void
Editor_SceneInitRow(
    struct Editor_Scene* out,
    const char* name)
{
    assert(out);
    assert(name);
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->level = -1;
}

int
Editor_SceneValid(const struct Editor_Scene* scene)
{
    assert(scene);
    if( !Editor_SceneNameValid(scene->name) || (scene->segments & ~(unsigned)EDITOR_SCENE_SEG_ALL) != 0 )
        return 0;
    if( scene->segments & EDITOR_SCENE_SEG_SQUARES )
    {
        if( scene->square_count < 1 || scene->square_count > EDITOR_SCENE_SQUARES_MAX )
            return 0;
        for( int i = 0; i < scene->square_count; i++ )
            if( !square_key_valid(scene->squares[i]) )
                return 0;
    }
    if( scene->segments & EDITOR_SCENE_SEG_CAMERA )
    {
        const struct Editor_SceneCamera* c = &scene->camera;
        long const fine_max = (long)EDITOR_SCENE_TILE_MAX * 128 + 127;
        if( c->x < 0 || c->x > fine_max || c->z < 0 || c->z > fine_max || c->level < 0 || c->level > 3 )
            return 0;
        if( c->yaw < 0 || c->yaw > 2047 || c->pitch < 0 || c->pitch > 2047 || c->zoom < 0
            || c->zoom > EDITOR_SCENE_ZOOM_MAX )
            return 0;
    }
    if( scene->segments & EDITOR_SCENE_SEG_LEVEL )
    {
        if( scene->level < -1 || scene->level > 3 || (scene->above && scene->level < 0) )
            return 0;
    }
    if( (scene->segments & EDITOR_SCENE_SEG_LANE) && !lane_valid(scene->lane) )
        return 0;
    if( scene->segments & EDITOR_SCENE_SEG_PREVIEW )
    {
        if( scene->preview_count < 1 || scene->preview_count > EDITOR_SCENE_PREVIEW_MAX )
            return 0;
        for( int i = 0; i < scene->preview_count; i++ )
            if( !Editor_SceneVarValid(scene->preview[i].var) )
                return 0;
    }
    if( scene->segments & EDITOR_SCENE_SEG_ACCOUNT )
    {
        if( !account_valid(scene->account) || scene->account_x < 0 || scene->account_x > EDITOR_SCENE_TILE_MAX
            || scene->account_z < 0 || scene->account_z > EDITOR_SCENE_TILE_MAX || scene->account_level < 0
            || scene->account_level > 3 )
            return 0;
    }
    if( (scene->segments & EDITOR_SCENE_SEG_NOTE) && !note_valid(scene->note) )
        return 0;
    return 1;
}

void
Editor_SceneFormatFine(
    int32_t fine,
    char* out,
    size_t out_size)
{
    int32_t const tile = fine / 128;
    int32_t const rest = fine % 128;
    char digits[8];
    int n = 7;

    assert(out);
    assert(fine >= 0);
    if( rest == 0 )
    {
        snprintf(out, out_size, "%d", (int)tile);
        return;
    }
    /* rest / 128 = rest * 78125 / 10^7, exactly. */
    snprintf(digits, sizeof(digits), "%07ld", (long)rest * 78125L);
    while( n > 1 && digits[n - 1] == '0' )
        n--;
    digits[n] = '\0';
    snprintf(out, out_size, "%d.%s", (int)tile, digits);
}

/* ---- formatting -------------------------------------------------------------- */

struct Pen
{
    char* out;
    size_t size;
    size_t at;
};

static void
pen_put(
    struct Pen* pen,
    const char* format,
    ...)
{
    va_list args;
    int written;
    char scratch[1];

    va_start(args, format);
    if( pen->at < pen->size )
        written = vsnprintf(pen->out + pen->at, pen->size - pen->at, format, args);
    else
        written = vsnprintf(scratch, 0, format, args);
    va_end(args);
    if( written > 0 )
        pen->at += (size_t)written;
}

int
Editor_SceneFormatLine(
    const struct Editor_Scene* scene,
    unsigned mask,
    char* out,
    size_t out_capacity)
{
    char buffer[2 * EDITOR_CMD_SPLICE_MAX];
    struct Pen pen = { buffer, sizeof(buffer), 0 };
    unsigned const segments = scene->segments & mask;

    assert(scene);
    assert(out || out_capacity == 0);

    pen_put(&pen, "scene %s", scene->name);
    if( segments & EDITOR_SCENE_SEG_SQUARES )
    {
        pen_put(&pen, " | squares");
        for( int i = 0; i < scene->square_count; i++ )
            pen_put(&pen, " %s", scene->squares[i]);
    }
    if( segments & EDITOR_SCENE_SEG_CAMERA )
    {
        char x[24];
        char z[24];
        Editor_SceneFormatFine(scene->camera.x, x, sizeof(x));
        Editor_SceneFormatFine(scene->camera.z, z, sizeof(z));
        pen_put(&pen, " | camera %s,%s,%d,%d,%d,%d", x, z, (int)scene->camera.level, (int)scene->camera.yaw,
            (int)scene->camera.pitch, (int)scene->camera.zoom);
    }
    if( segments & EDITOR_SCENE_SEG_LEVEL )
    {
        if( scene->level < 0 )
            pen_put(&pen, " | level auto");
        else
            pen_put(&pen, " | level %d%s", scene->level, scene->above ? " above" : "");
    }
    if( segments & EDITOR_SCENE_SEG_LANE )
        pen_put(&pen, " | lane %s", scene->lane);
    if( segments & EDITOR_SCENE_SEG_PREVIEW )
    {
        pen_put(&pen, " | preview");
        for( int i = 0; i < scene->preview_count; i++ )
            pen_put(&pen, " %s=%d", scene->preview[i].var, (int)scene->preview[i].value);
    }
    if( segments & EDITOR_SCENE_SEG_ACCOUNT )
        pen_put(&pen, " | account %s at %d,%d,%d", scene->account, scene->account_x, scene->account_z,
            scene->account_level);
    if( segments & EDITOR_SCENE_SEG_NOTE )
        pen_put(&pen, " | note %s", scene->note);
    if( pen.at >= sizeof(buffer) )
    {
        /* Longer than any line can be: say so by the length alone. */
        if( out_capacity > 0 )
            out[0] = '\0';
        return (int)pen.at;
    }
    return snprintf(out, out_capacity, "%s", buffer);
}

int
Editor_ScenesFormat(
    const struct Editor_Scene* scene,
    char lines[EDITOR_SCENE_FORMAT_LINES][EDITOR_SCENES_LINE_MAX + 1],
    unsigned masks[EDITOR_SCENE_FORMAT_LINES])
{
    int count = 0;

    assert(scene);
    assert(lines);
    assert(masks);
    for( int i = 0; i < EDITOR_SCENE_FORMAT_LINES; i++ )
    {
        if( !(scene->segments & k_format_masks[i]) )
            continue;
        if( Editor_SceneFormatLine(scene, k_format_masks[i], NULL, 0) > EDITOR_SCENES_LINE_MAX )
            return 0;
        Editor_SceneFormatLine(scene, k_format_masks[i], lines[count], EDITOR_SCENES_LINE_MAX + 1);
        masks[count] = scene->segments & k_format_masks[i];
        count++;
    }
    if( count == 0 )
    {
        /* Nothing but the name: the header. */
        if( Editor_SceneFormatLine(scene, 0, NULL, 0) > EDITOR_SCENES_LINE_MAX )
            return 0;
        Editor_SceneFormatLine(scene, 0, lines[0], EDITOR_SCENES_LINE_MAX + 1);
        masks[0] = 0;
        count = 1;
    }
    return count;
}

void
Editor_SceneSummary(
    const struct Editor_Scene* scene,
    char* out,
    size_t out_size)
{
    struct Pen pen = { out, out_size, 0 };
    int parts = 0;

    assert(scene);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( scene->segments & EDITOR_SCENE_SEG_SQUARES )
    {
        int const shown = scene->square_count < 3 ? scene->square_count : 3;
        for( int i = 0; i < shown; i++ )
            pen_put(&pen, "%s%s", i ? " " : "", scene->squares[i]);
        if( scene->square_count > shown )
            pen_put(&pen, " +%d", scene->square_count - shown);
        parts++;
    }
    if( scene->segments & EDITOR_SCENE_SEG_LEVEL )
    {
        if( scene->level < 0 )
            pen_put(&pen, "%sAuto", parts ? " | " : "");
        else
            pen_put(&pen, "%sL%d%s", parts ? " | " : "", scene->level, scene->above ? " above" : "");
        parts++;
    }
    if( scene->segments & EDITOR_SCENE_SEG_LANE )
    {
        pen_put(&pen, "%s%s", parts ? " | " : "", scene->lane);
        parts++;
    }
    if( scene->segments & EDITOR_SCENE_SEG_PREVIEW )
    {
        pen_put(&pen, "%spreview", parts ? " | " : "");
        for( int i = 0; i < scene->preview_count; i++ )
            pen_put(&pen, " %s=%d", scene->preview[i].var, (int)scene->preview[i].value);
        parts++;
    }
    if( scene->segments & EDITOR_SCENE_SEG_ACCOUNT )
    {
        pen_put(&pen, "%s%s", parts ? " | " : "", scene->account);
        parts++;
    }
    if( parts == 0 )
        pen_put(&pen, "%s", (scene->segments & EDITOR_SCENE_SEG_NOTE) ? scene->note : "(empty)");
}

/* ---- parsing ------------------------------------------------------------------ */

static int
parse_segment(
    struct Editor_Scene* row,
    const char* begin,
    const char* end)
{
    const char* rest;
    unsigned bit;

    if( segment_is(begin, end, "squares", &rest) )
    {
        bit = EDITOR_SCENE_SEG_SQUARES;
        if( row->segments & bit )
            return 0;
        while( rest < end )
        {
            if( row->square_count == EDITOR_SCENE_SQUARES_MAX )
                return 0;
            if( !next_word(&rest, end, row->squares[row->square_count], EDITOR_SCENE_KEY_MAX) )
                break;
            if( !square_key_valid(row->squares[row->square_count]) )
                return 0;
            row->square_count++;
            skip_space(&rest, end);
        }
        if( row->square_count == 0 )
            return 0;
    }
    else if( segment_is(begin, end, "camera", &rest) )
    {
        long level;
        long yaw;
        long pitch;
        long zoom;
        bit = EDITOR_SCENE_SEG_CAMERA;
        if( row->segments & bit )
            return 0;
        if( !read_fine(&rest, end, &row->camera.x) || rest >= end || *rest++ != ',' )
            return 0;
        if( !read_fine(&rest, end, &row->camera.z) || rest >= end || *rest++ != ',' )
            return 0;
        if( !read_int(&rest, end, 0, 3, &level) || rest >= end || *rest++ != ',' )
            return 0;
        if( !read_int(&rest, end, 0, 2047, &yaw) || rest >= end || *rest++ != ',' )
            return 0;
        if( !read_int(&rest, end, 0, 2047, &pitch) || rest >= end || *rest++ != ',' )
            return 0;
        if( !read_int(&rest, end, 0, EDITOR_SCENE_ZOOM_MAX, &zoom) || rest != end )
            return 0;
        row->camera.level = (int32_t)level;
        row->camera.yaw = (int32_t)yaw;
        row->camera.pitch = (int32_t)pitch;
        row->camera.zoom = (int32_t)zoom;
    }
    else if( segment_is(begin, end, "level", &rest) )
    {
        const char* above;
        long level;
        bit = EDITOR_SCENE_SEG_LEVEL;
        if( row->segments & bit )
            return 0;
        if( segment_is(rest, end, "auto", &above) )
        {
            if( above != end )
                return 0;
            row->level = -1;
        }
        else
        {
            if( !read_int(&rest, end, 0, 3, &level) )
                return 0;
            row->level = (int)level;
            skip_space(&rest, end);
            if( rest < end )
            {
                if( !segment_is(rest, end, "above", &above) || above != end )
                    return 0;
                row->above = 1;
            }
        }
    }
    else if( segment_is(begin, end, "lane", &rest) )
    {
        bit = EDITOR_SCENE_SEG_LANE;
        if( row->segments & bit )
            return 0;
        if( !copy_run(row->lane, sizeof(row->lane), rest, end) || !lane_valid(row->lane) )
            return 0;
    }
    else if( segment_is(begin, end, "preview", &rest) )
    {
        bit = EDITOR_SCENE_SEG_PREVIEW;
        if( row->segments & bit )
            return 0;
        while( rest < end )
        {
            char pair[EDITOR_SCENE_VAR_MAX + 16];
            const char* at;
            const char* pair_end;
            char* equals;
            long value;
            struct Editor_ScenePreview* entry;
            if( !next_word(&rest, end, pair, sizeof(pair)) )
                break;
            if( row->preview_count == EDITOR_SCENE_PREVIEW_MAX )
                return 0;
            equals = strchr(pair, '=');
            if( !equals )
                return 0;
            *equals = '\0';
            entry = &row->preview[row->preview_count];
            if( !copy_run(entry->var, sizeof(entry->var), pair, equals) || !Editor_SceneVarValid(entry->var) )
                return 0;
            at = equals + 1;
            pair_end = at + strlen(at);
            if( !read_int(&at, pair_end, 1, 2147483647L, &value) || at != pair_end )
                return 0;
            entry->value = (int32_t)value;
            row->preview_count++;
            skip_space(&rest, end);
        }
        if( row->preview_count == 0 )
            return 0;
    }
    else if( segment_is(begin, end, "account", &rest) )
    {
        const char* coords;
        long x;
        long z;
        long level;
        bit = EDITOR_SCENE_SEG_ACCOUNT;
        if( row->segments & bit )
            return 0;
        if( !next_word(&rest, end, row->account, sizeof(row->account)) || !account_valid(row->account) )
            return 0;
        skip_space(&rest, end);
        if( !segment_is(rest, end, "at", &coords) )
            return 0;
        if( !read_int(&coords, end, 0, EDITOR_SCENE_TILE_MAX, &x) || coords >= end || *coords++ != ',' )
            return 0;
        if( !read_int(&coords, end, 0, EDITOR_SCENE_TILE_MAX, &z) || coords >= end || *coords++ != ',' )
            return 0;
        if( !read_int(&coords, end, 0, 3, &level) || coords != end )
            return 0;
        row->account_x = (int)x;
        row->account_z = (int)z;
        row->account_level = (int)level;
    }
    else if( segment_is(begin, end, "note", &rest) )
    {
        bit = EDITOR_SCENE_SEG_NOTE;
        if( row->segments & bit )
            return 0;
        if( !copy_run(row->note, sizeof(row->note), rest, end) || !note_valid(row->note) )
            return 0;
    }
    else
        return 0;
    row->segments |= bit;
    return 1;
}

int
Editor_ScenesParseLine(
    const char* text,
    struct Editor_Scene* out_row)
{
    struct Editor_Scene row;
    const char* begin = text;
    const char* end;
    const char* rest;
    const char* bar;

    assert(text);

    end = text + strlen(text);
    trim(&begin, &end);
    if( begin == end || begin[0] == '#' || (end - begin >= 2 && begin[0] == '/' && begin[1] == '/') )
        return EDITOR_SCENES_LINE_TEXT;
    if( !segment_is(begin, end, "scene", &rest) )
        return -1;

    memset(&row, 0, sizeof(row));
    row.level = -1;

    /* The name runs to the first bar. */
    bar = memchr(rest, '|', (size_t)(end - rest));
    {
        const char* name_end = bar ? bar : end;
        const char* name_begin = rest;
        trim(&name_begin, &name_end);
        if( !copy_run(row.name, sizeof(row.name), name_begin, name_end) || !Editor_SceneNameValid(row.name) )
            return -1;
    }
    while( bar )
    {
        const char* seg = bar + 1;
        const char* seg_end;
        bar = memchr(seg, '|', (size_t)(end - seg));
        seg_end = bar ? bar : end;
        trim(&seg, &seg_end);
        if( !parse_segment(&row, seg, seg_end) )
            return -1;
    }
    if( out_row )
        *out_row = row;
    return EDITOR_SCENES_LINE_RECORD;
}

/** Classify `line->text` and attach its row. 0 when it does not parse. */
static int
classify(struct Editor_ScenesLine* line)
{
    struct Editor_Scene row;
    int const kind = Editor_ScenesParseLine(line->text, &row);

    line->kind = EDITOR_SCENES_LINE_TEXT;
    if( kind == EDITOR_SCENES_LINE_RECORD )
    {
        line->kind = EDITOR_SCENES_LINE_RECORD;
        line->row = ToriMem_Alloc(ToriMem_Editor(), sizeof(*line->row));
        assert(line->row);
        *line->row = row;
    }
    return kind >= 0;
}

/** A line of scene `name` other than `skip` that a line holding `mask` would
 *  collide with -- one holding any of its segment kinds, or, for a header
 *  (mask 0), another header -- or -1. */
static int
find_clash(
    const struct Editor_ScenesFile* file,
    const char* name,
    unsigned mask,
    int skip)
{
    for( int i = 0; i < file->line_count; i++ )
    {
        const struct Editor_Scene* row = file->lines[i].row;
        if( i == skip || !row || strcmp(row->name, name) != 0 )
            continue;
        if( mask == 0 ? row->segments == 0 : (row->segments & mask) != 0 )
            return i;
    }
    return -1;
}

/** The line of scene `name` holding exactly `mask`, or -1. */
static int
find_key(
    const struct Editor_ScenesFile* file,
    const char* name,
    unsigned mask)
{
    for( int i = 0; i < file->line_count; i++ )
    {
        const struct Editor_Scene* row = file->lines[i].row;
        if( row && row->segments == mask && strcmp(row->name, name) == 0 )
            return i;
    }
    return -1;
}

struct Editor_ScenesParseResult
Editor_ScenesParse(
    struct Editor_ScenesFile* file,
    const char* text,
    size_t length)
{
    struct Editor_ScenesParseResult result = { 1, 0 };
    size_t at = 0;
    int line_number = 0;

    assert(file);
    assert(text || length == 0);

    Editor_ScenesFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_ScenesLine* line = insert_slot(file, file->line_count);
        int ok;

        line_number++;
        line->text = copy_text(begin, line_length);
        ok = classify(line);
        /* A segment kind belongs to one line of its scene: two cameras would
         * make every by-line command, and Enter, ambiguous. */
        if( ok && line->row && find_clash(file, line->row->name, line->row->segments, file->line_count - 1) >= 0 )
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
Editor_ScenesEmit(
    const struct Editor_ScenesFile* file,
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

void
Editor_ScenesCopy(
    struct Editor_ScenesFile* dst,
    const struct Editor_ScenesFile* src)
{
    assert(dst);
    assert(src);
    assert(dst != src);
    Editor_ScenesFree(dst);
    for( int i = 0; i < src->line_count; i++ )
    {
        struct Editor_ScenesLine* line = insert_slot(dst, dst->line_count);
        line->kind = src->lines[i].kind;
        line->text = copy_text(src->lines[i].text, strlen(src->lines[i].text));
        if( src->lines[i].row )
        {
            line->row = ToriMem_Alloc(ToriMem_Editor(), sizeof(*line->row));
            assert(line->row);
            *line->row = *src->lines[i].row;
        }
    }
    dst->final_newline = src->final_newline;
}

/** 1 when line `index` is the first line naming its scene. */
static int
first_of_scene(
    const struct Editor_ScenesFile* file,
    int index)
{
    const struct Editor_Scene* row = file->lines[index].row;
    if( !row )
        return 0;
    for( int i = 0; i < index; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->name, row->name) == 0 )
            return 0;
    return 1;
}

int
Editor_ScenesCount(const struct Editor_ScenesFile* file)
{
    int n = 0;
    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        n += first_of_scene(file, i);
    return n;
}

const char*
Editor_ScenesName(
    const struct Editor_ScenesFile* file,
    int nth)
{
    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        if( first_of_scene(file, i) && nth-- == 0 )
            return file->lines[i].row->name;
    return NULL;
}

int
Editor_ScenesHas(
    const struct Editor_ScenesFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->name, name) == 0 )
            return 1;
    return 0;
}

/** Copy the segments `mask` of `from` into `to`. */
static void
take_segments(
    struct Editor_Scene* to,
    const struct Editor_Scene* from,
    unsigned mask)
{
    if( mask & EDITOR_SCENE_SEG_SQUARES )
    {
        to->square_count = from->square_count;
        memcpy(to->squares, from->squares, sizeof(to->squares));
    }
    if( mask & EDITOR_SCENE_SEG_CAMERA )
        to->camera = from->camera;
    if( mask & EDITOR_SCENE_SEG_LEVEL )
    {
        to->level = from->level;
        to->above = from->above;
    }
    if( mask & EDITOR_SCENE_SEG_LANE )
        memcpy(to->lane, from->lane, sizeof(to->lane));
    if( mask & EDITOR_SCENE_SEG_PREVIEW )
    {
        to->preview_count = from->preview_count;
        memcpy(to->preview, from->preview, sizeof(to->preview));
    }
    if( mask & EDITOR_SCENE_SEG_ACCOUNT )
    {
        memcpy(to->account, from->account, sizeof(to->account));
        to->account_x = from->account_x;
        to->account_z = from->account_z;
        to->account_level = from->account_level;
    }
    if( mask & EDITOR_SCENE_SEG_NOTE )
        memcpy(to->note, from->note, sizeof(to->note));
    to->segments |= mask;
}

void
Editor_SceneMerge(
    struct Editor_Scene* into,
    const struct Editor_Scene* from)
{
    assert(into);
    assert(from);
    take_segments(into, from, from->segments);
}

void
Editor_SceneClear(
    struct Editor_Scene* scene,
    unsigned mask)
{
    struct Editor_Scene empty;

    assert(scene);
    Editor_SceneInitRow(&empty, scene->name);
    empty.segments = mask;
    take_segments(scene, &empty, mask);
    scene->segments &= ~mask;
}

int
Editor_ScenesGet(
    const struct Editor_ScenesFile* file,
    const char* name,
    struct Editor_Scene* out)
{
    int found = 0;

    assert(file);
    assert(name);
    assert(out);
    Editor_SceneInitRow(out, name);
    for( int i = 0; i < file->line_count; i++ )
    {
        const struct Editor_Scene* row = file->lines[i].row;
        if( !row || strcmp(row->name, name) != 0 )
            continue;
        take_segments(out, row, row->segments);
        found = 1;
    }
    return found;
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

    assert(before_len <= EDITOR_SCENES_LINE_MAX);
    assert(after_len <= EDITOR_SCENES_LINE_MAX);

    Editor_CmdInit(out, EDITOR_DOC_SCENES, EDITOR_SCENES_DOC_KEY, kind);
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

/** SCENE_SET: record line `text` becomes line `old_line` (-1: a new line).
 *  0 when it does not parse, `old_line` is not a record, a segment kind it
 *  holds is another line's, or nothing would change. */
static int
set_command(
    struct Editor_Cmd* out,
    const struct Editor_ScenesFile* file,
    int old_line,
    const char* text)
{
    struct Editor_Scene row;

    if( strlen(text) > EDITOR_SCENES_LINE_MAX || Editor_ScenesParseLine(text, &row) != EDITOR_SCENES_LINE_RECORD )
        return 0;
    if( old_line >= 0
        && (!file->lines[old_line].row || strlen(file->lines[old_line].text) > EDITOR_SCENES_LINE_MAX) )
        return 0;
    if( old_line >= 0 && strcmp(file->lines[old_line].text, text) == 0 )
        return 0;
    if( find_clash(file, row.name, row.segments, old_line) >= 0 )
        return 0;
    fill_command(out, EDITOR_CMD_SCENE_SET, old_line >= 0 ? file->lines[old_line].text : NULL, text, old_line);
    return 1;
}

static int
remove_command(
    struct Editor_Cmd* out,
    const struct Editor_ScenesFile* file,
    int line)
{
    if( !file->lines[line].row || strlen(file->lines[line].text) > EDITOR_SCENES_LINE_MAX )
        return 0;
    fill_command(out, EDITOR_CMD_SCENE_REMOVE, file->lines[line].text, NULL, line);
    return 1;
}

/* ---- plans --------------------------------------------------------------------- */

struct Plan
{
    struct Editor_Cmd* out;
    int capacity;
    int count;
    struct Editor_ScenesFile work;
    int failed;
};

static void
plan_begin(
    struct Plan* plan,
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file)
{
    memset(plan, 0, sizeof(*plan));
    plan->out = out;
    plan->capacity = capacity;
    Editor_ScenesInit(&plan->work);
    Editor_ScenesCopy(&plan->work, file);
}

static int
plan_room(struct Plan* plan)
{
    if( plan->failed || plan->count >= plan->capacity )
    {
        plan->failed = 1;
        return 0;
    }
    return 1;
}

/** Take the command just written at out[count] onto the working copy. */
static void
plan_take(
    struct Plan* plan,
    int built)
{
    int applied;

    if( !built || plan->failed )
    {
        plan->failed = 1;
        return;
    }
    applied = Editor_ScenesApply(&plan->work, &plan->out[plan->count], 0);
    assert(applied);
    (void)applied;
    plan->count++;
}

static int
plan_end(struct Plan* plan)
{
    int const count = plan->failed ? 0 : plan->count;
    Editor_ScenesFree(&plan->work);
    return count;
}

/** Line indices of scene `name` in `file`, in order. */
static int
scene_lines(
    const struct Editor_ScenesFile* file,
    const char* name,
    int* out,
    int max)
{
    int n = 0;
    for( int i = 0; i < file->line_count && n < max; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->name, name) == 0 )
            out[n++] = i;
    return n;
}

int
Editor_ScenePlanSave(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file,
    const struct Editor_Scene* scene)
{
    struct Plan plan;
    char lines[EDITOR_SCENE_FORMAT_LINES][EDITOR_SCENES_LINE_MAX + 1];
    unsigned masks[EDITOR_SCENE_FORMAT_LINES];
    int existing[EDITOR_SCENE_PLAN_MAX];
    int keep[EDITOR_SCENE_PLAN_MAX];
    int n;
    int m;
    int same;

    assert(out);
    assert(file);
    assert(scene);
    assert(capacity >= 0);

    plan_begin(&plan, out, capacity, file);
    n = Editor_SceneValid(scene) ? Editor_ScenesFormat(scene, lines, masks) : 0;
    if( n == 0 )
    {
        plan.failed = 1;
        return plan_end(&plan);
    }
    m = scene_lines(&plan.work, scene->name, existing, EDITOR_SCENE_PLAN_MAX);
    if( m == 0 )
    {
        for( int i = 0; i < n; i++ )
            if( plan_room(&plan) )
                plan_take(&plan, set_command(&out[plan.count], &plan.work, -1, lines[i]));
        return plan_end(&plan);
    }
    same = m == n;
    for( int i = 0; i < m && i < EDITOR_SCENE_FORMAT_LINES && same; i++ )
        same = strcmp(plan.work.lines[existing[i]].text, lines[i]) == 0;
    if( same )
    {
        plan.failed = 1; /* nothing would change */
        return plan_end(&plan);
    }
    /* Lines after the first that already read as the editor would write
     * them stay; the others go, last first so the earlier indices hold. */
    for( int i = 1; i < m; i++ )
    {
        keep[i] = 0;
        if( i < n && i < EDITOR_SCENE_FORMAT_LINES )
            keep[i] = strcmp(plan.work.lines[existing[i]].text, lines[i]) == 0;
    }
    for( int i = m - 1; i >= 1; i-- )
        if( !keep[i] && plan_room(&plan) )
            plan_take(&plan, remove_command(&out[plan.count], &plan.work, existing[i]));
    /* The first keeps its place. */
    if( strcmp(plan.work.lines[existing[0]].text, lines[0]) != 0 && plan_room(&plan) )
        plan_take(&plan, set_command(&out[plan.count], &plan.work, existing[0], lines[0]));
    for( int i = 1; i < n; i++ )
        if( !(i < m && keep[i]) && plan_room(&plan) )
            plan_take(&plan, set_command(&out[plan.count], &plan.work, -1, lines[i]));
    return plan_end(&plan);
}

int
Editor_ScenePlanRename(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file,
    const char* old_name,
    const char* new_name)
{
    struct Plan plan;

    assert(out);
    assert(file);
    assert(old_name);
    assert(new_name);
    plan_begin(&plan, out, capacity, file);
    if( !Editor_SceneNameValid(new_name) || strcmp(old_name, new_name) == 0 || !Editor_ScenesHas(file, old_name)
        || Editor_ScenesHas(file, new_name) )
        plan.failed = 1;
    for( int i = 0; i < plan.work.line_count && !plan.failed; i++ )
    {
        struct Editor_Scene row;
        char text[EDITOR_SCENES_LINE_MAX + 1];
        if( !plan.work.lines[i].row || strcmp(plan.work.lines[i].row->name, old_name) != 0 )
            continue;
        row = *plan.work.lines[i].row;
        snprintf(row.name, sizeof(row.name), "%s", new_name);
        if( Editor_SceneFormatLine(&row, row.segments, NULL, 0) > EDITOR_SCENES_LINE_MAX )
        {
            plan.failed = 1;
            break;
        }
        Editor_SceneFormatLine(&row, row.segments, text, sizeof(text));
        if( plan_room(&plan) )
            plan_take(&plan, set_command(&out[plan.count], &plan.work, i, text));
    }
    return plan_end(&plan);
}

int
Editor_ScenePlanDelete(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file,
    const char* name)
{
    struct Plan plan;

    assert(out);
    assert(file);
    assert(name);
    plan_begin(&plan, out, capacity, file);
    if( !Editor_ScenesHas(file, name) )
        plan.failed = 1;
    /* Each remove is taken before the next is found, so every one carries
     * the index it stood at when it went (the undo walks them back in
     * reverse); the emptied file may lose its header lines too, so the scan
     * starts over rather than reason about which indices moved. */
    for( int i = 0; i < plan.work.line_count && !plan.failed; )
    {
        if( plan.work.lines[i].row && strcmp(plan.work.lines[i].row->name, name) == 0 )
        {
            if( plan_room(&plan) )
                plan_take(&plan, remove_command(&out[plan.count], &plan.work, i));
            i = 0;
            continue;
        }
        i++;
    }
    return plan_end(&plan);
}

void
Editor_ScenesCmdLine(
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

/** The file holds nothing but the header lines. */
static int
only_header(const struct Editor_ScenesFile* file)
{
    if( file->line_count != SCENES_HEADER_LINES )
        return 0;
    for( int i = 0; i < SCENES_HEADER_LINES; i++ )
        if( strcmp(file->lines[i].text, k_header[i]) != 0 )
            return 0;
    return 1;
}

/** Where a new line of scene `name` goes: after its last line, else the
 *  end. */
static int
insert_index(
    const struct Editor_ScenesFile* file,
    const char* name)
{
    int last = -1;

    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->name, name) == 0 )
            last = i;
    return last < 0 ? file->line_count : last + 1;
}

/** Insert record line `text` at `index` (-1: where insert_index puts it). An
 *  empty file gets the header first. */
static void
insert_record(
    struct Editor_ScenesFile* file,
    int index,
    const char* text)
{
    struct Editor_ScenesLine* line;
    struct Editor_Scene row;
    int ok;

    if( file->line_count == 0 )
    {
        for( int i = 0; i < SCENES_HEADER_LINES; i++ )
        {
            struct Editor_ScenesLine* header = insert_slot(file, file->line_count);
            header->text = copy_text(k_header[i], strlen(k_header[i]));
            header->kind = EDITOR_SCENES_LINE_TEXT;
        }
        file->final_newline = 1;
    }
    if( index < 0 || index > file->line_count )
    {
        ok = Editor_ScenesParseLine(text, &row) == EDITOR_SCENES_LINE_RECORD;
        assert(ok);
        index = insert_index(file, row.name);
    }
    line = insert_slot(file, index);
    line->text = copy_text(text, strlen(text));
    ok = classify(line);
    assert(ok);
    (void)ok;
}

static void
remove_record(
    struct Editor_ScenesFile* file,
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
    struct Editor_ScenesFile* file,
    int index,
    const char* text)
{
    struct Editor_ScenesLine* line = &file->lines[index];
    int ok;

    line_free(line);
    line->text = copy_text(text, strlen(text));
    ok = classify(line);
    assert(ok);
    (void)ok;
}

int
Editor_ScenesApply(
    struct Editor_ScenesFile* file,
    const struct Editor_Cmd* command,
    int inverse)
{
    char before[EDITOR_CMD_SPLICE_MAX + 1];
    char after[EDITOR_CMD_SPLICE_MAX + 1];
    struct Editor_Scene expect_row;
    struct Editor_Scene want_row;
    int has_expect;
    int has_want;
    const char* expect;
    const char* want;
    int at = -1;
    int is_set;

    assert(file);
    assert(command);

    if( command->kind != EDITOR_CMD_SCENE_SET && command->kind != EDITOR_CMD_SCENE_REMOVE )
        return 0;
    if( command->u.area.before_len > EDITOR_SCENES_LINE_MAX || command->u.area.after_len > EDITOR_SCENES_LINE_MAX )
        return 0;
    is_set = command->kind == EDITOR_CMD_SCENE_SET;
    /* The shape a command may have: SET has an after (and a before when it
     * rewrites a line), REMOVE only a before. */
    if( is_set ? !command->has_after : (!command->has_before || command->has_after) )
        return 0;
    Editor_ScenesCmdLine(command, 0, before, sizeof(before));
    Editor_ScenesCmdLine(command, 1, after, sizeof(after));

    has_expect = inverse ? command->has_after : command->has_before;
    has_want = inverse ? command->has_before : command->has_after;
    expect = inverse ? after : before;
    want = inverse ? before : after;

    /* Both lines must be records; their keys (name, segment kinds) find the
     * line, and its bytes must be the command's. */
    if( has_expect )
    {
        if( Editor_ScenesParseLine(expect, &expect_row) != EDITOR_SCENES_LINE_RECORD )
            return 0;
        at = find_key(file, expect_row.name, expect_row.segments);
        if( at < 0 || strcmp(file->lines[at].text, expect) != 0 )
            return 0;
    }
    if( has_want )
    {
        if( Editor_ScenesParseLine(want, &want_row) != EDITOR_SCENES_LINE_RECORD )
            return 0;
        if( find_clash(file, want_row.name, want_row.segments, at) >= 0 )
            return 0;
    }

    if( has_expect && has_want )
        replace_record(file, at, want);
    else if( has_expect )
        remove_record(file, at);
    else
        /* An undone remove goes back where it stood; a new line where its
         * scene's lines are. */
        insert_record(file, is_set ? -1 : command->u.area.line, want);
    return 1;
}

void
Editor_ScenesCmdLabel(
    const struct Editor_Cmd* command,
    char* out,
    size_t out_size)
{
    char before[EDITOR_CMD_SPLICE_MAX + 1];
    char after[EDITOR_CMD_SPLICE_MAX + 1];
    struct Editor_Scene a;
    struct Editor_Scene b;

    assert(command);
    assert(out);
    assert(out_size > 0);

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    Editor_ScenesCmdLine(command, 0, before, sizeof(before));
    Editor_ScenesCmdLine(command, 1, after, sizeof(after));
    if( command->has_before )
        (void)Editor_ScenesParseLine(before, &a);
    if( command->has_after )
        (void)Editor_ScenesParseLine(after, &b);

    if( command->has_before && !command->has_after )
        snprintf(out, out_size, "Delete scene %s", a.name);
    else if( !command->has_before )
        snprintf(out, out_size, "Save scene %s", b.name);
    else if( strcmp(a.name, b.name) != 0 )
        snprintf(out, out_size, "Rename scene %s to %s", a.name, b.name);
    else
        snprintf(out, out_size, "Update scene %s", b.name);
}
