/*
 * Camera paths (editor_campath.h): the line-preserving codec of
 * editor/campaths.txt, the verbs' new text, the eye <-> keyframe maths, the
 * client's scripted camera as a stepped simulation, and the proc Copy script
 * renders.
 */

#include "editor_campath.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_header[] =
    "# ToriRSMapEd camera paths (editor/campaths.txt). The Camera path panel writes these; Copy script turns one into a "
    "proc.\n"
    "# path <name> [| coords instance <mX_Z>] [| frame]\n"
    "# key <path> | eye <x>,<z>,<level>,<height> | look <x>,<z>,<height> | cut OR glide <eye rate>,<eye rate2>,<look "
    "rate>,<look rate2> | hold <ticks>\n";

/** Angle units per radian: 2048 a turn (app_camera.c's 325.949). */
#define CAM_UNITS_PER_RADIAN 325.949
#define CAM_PI 3.14159265358979323846
/** The speed a cut keyframe takes when it is made a glide. */
#define CAM_DEFAULT_SPEED EDITOR_CAM_SPEED_MEDIUM
/** How far along the eye's ray the subject is looked for, and the step, in
 *  fine units; and how far out the subject is taken to be when the ray meets
 *  no ground (16 tiles). */
#define CAM_MARCH_MAX 16384
#define CAM_MARCH_STEP 16
#define CAM_SUBJECT_FAR 2048
/** The yaw a fitted keyframe may be off by before a nearer look tile is
 *  preferred to the one beside the subject: the tangent of three angle
 *  units, about half a degree. */
#define CAM_FIT_TOLERANCE 0.0092

static const int k_speed_rates[EDITOR_CAM_SPEED_PRESETS] = { 2, 5, 10 };
static const char* const k_speed_names[EDITOR_CAM_SPEED_PRESETS + 1] = { "Slow", "Medium", "Fast", "Custom" };

/* ---- text helpers ------------------------------------------------------------ */

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/** snprintf's contract over a running total. */
static void
put(
    char* out,
    size_t capacity,
    size_t* total,
    const char* bytes,
    size_t length)
{
    if( out && *total < capacity )
    {
        size_t const room = capacity - *total;
        memcpy(out + *total, bytes, length < room ? length : room);
    }
    *total += length;
}

static void
put_text(
    char* out,
    size_t capacity,
    size_t* total,
    const char* text)
{
    put(out, capacity, total, text, strlen(text));
}

static void
put_terminate(
    char* out,
    size_t capacity,
    size_t total)
{
    if( out && capacity > 0 )
        out[total < capacity ? total : capacity - 1] = '\0';
}

/** [*at, end) advanced past spaces. */
static void
skip_spaces(
    const char** at,
    const char* end)
{
    while( *at < end && is_space(**at) )
        (*at)++;
}

/** A decimal 0..max at *at; advances past it. 0 when there is none. */
static int
take_number(
    const char** at,
    const char* end,
    int max,
    int* out)
{
    long value = 0;
    int digits = 0;

    while( *at < end && **at >= '0' && **at <= '9' )
    {
        value = value * 10 + (**at - '0');
        if( value > max )
            return 0;
        (*at)++;
        digits++;
    }
    if( digits == 0 || digits > 9 )
        return 0;
    *out = (int)value;
    return 1;
}

/** `count` numbers separated by commas filling [*at, end) exactly. */
static int
take_numbers(
    const char* at,
    const char* end,
    const int* maxima,
    int count,
    int* out)
{
    for( int i = 0; i < count; i++ )
    {
        if( i > 0 )
        {
            if( at >= end || *at != ',' )
                return 0;
            at++;
        }
        if( !take_number(&at, end, maxima[i], &out[i]) )
            return 0;
    }
    return at == end;
}

/** 1 when [at, end) starts with `word` followed by a space or the end;
 *  `*out_rest` is then the text after it, spaces skipped. */
static int
take_word(
    const char* at,
    const char* end,
    const char* word,
    const char** out_rest)
{
    size_t const length = strlen(word);

    if( (size_t)(end - at) < length || memcmp(at, word, length) != 0 )
        return 0;
    at += length;
    if( at < end && !is_space(*at) )
        return 0;
    skip_spaces(&at, end);
    *out_rest = at;
    return 1;
}

/* ---- names and rows ------------------------------------------------------------- */

int
Editor_CamPathNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_CAMPATH_NAME_MAX )
        return 0;
    if( name[0] < 'a' || name[0] > 'z' )
        return 0;
    for( size_t i = 1; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

static int
in_range(
    int value,
    int max)
{
    return value >= 0 && value <= max;
}

int
Editor_CamKeyValid(const struct Editor_CamKey* key)
{
    assert(key);
    if( !in_range(key->eye_x, EDITOR_CAMPATH_TILE_MAX) || !in_range(key->eye_z, EDITOR_CAMPATH_TILE_MAX)
        || !in_range(key->look_x, EDITOR_CAMPATH_TILE_MAX) || !in_range(key->look_z, EDITOR_CAMPATH_TILE_MAX) )
        return 0;
    if( !in_range(key->level, 3) || !in_range(key->eye_height, EDITOR_CAMPATH_HEIGHT_MAX)
        || !in_range(key->look_height, EDITOR_CAMPATH_HEIGHT_MAX) )
        return 0;
    if( key->glide != 0 && key->glide != 1 )
        return 0;
    /* A glide whose rate2 reached the snap is a cut, and is written as one. */
    if( key->glide
        && (!in_range(key->move_rate, EDITOR_CAMPATH_RATE_MAX) || !in_range(key->look_rate, EDITOR_CAMPATH_RATE_MAX)
            || !in_range(key->move_rate2, EDITOR_CAMPATH_SNAP_RATE2 - 1)
            || !in_range(key->look_rate2, EDITOR_CAMPATH_SNAP_RATE2 - 1)) )
        return 0;
    return key->hold >= 1 && key->hold <= EDITOR_CAMPATH_HOLD_MAX;
}

int
Editor_CamPathValid(const struct Editor_CamPath* path)
{
    assert(path);
    if( !Editor_CamPathNameValid(path->name) )
        return 0;
    if( path->instance != 0 && path->instance != 1 )
        return 0;
    if( path->instance && (!in_range(path->origin_mx, 255) || !in_range(path->origin_mz, 255)) )
        return 0;
    if( path->key_count < 0 || path->key_count > EDITOR_CAMPATH_KEYS_MAX )
        return 0;
    for( int i = 0; i < path->key_count; i++ )
        if( !Editor_CamKeyValid(&path->keys[i]) )
            return 0;
    return 1;
}

void
Editor_CamPathInit(
    struct Editor_CamPath* path,
    const char* name)
{
    assert(path);
    assert(name);
    assert(Editor_CamPathNameValid(name));
    memset(path, 0, sizeof(*path));
    snprintf(path->name, sizeof(path->name), "%s", name);
}

size_t
Editor_CamPathFormatHeader(
    const struct Editor_CamPath* path,
    char* out,
    size_t out_capacity)
{
    char line[160];
    int at;

    assert(path);
    assert(out || out_capacity == 0);
    at = snprintf(line, sizeof(line), "path %s", path->name);
    if( path->instance )
        at += snprintf(line + at, sizeof(line) - (size_t)at, " | coords instance m%d_%d", path->origin_mx,
            path->origin_mz);
    if( path->frame )
        at += snprintf(line + at, sizeof(line) - (size_t)at, " | frame");
    assert(at > 0);
    assert((size_t)at < sizeof(line));
    if( out_capacity > 0 )
        snprintf(out, out_capacity, "%s", line);
    return (size_t)at;
}

size_t
Editor_CamKeyFormat(
    const char* name,
    const struct Editor_CamKey* key,
    char* out,
    size_t out_capacity)
{
    char line[256];
    int at;

    assert(name);
    assert(key);
    assert(out || out_capacity == 0);
    at = snprintf(line, sizeof(line), "key %s | eye %d,%d,%d,%d | look %d,%d,%d | ", name, key->eye_x, key->eye_z,
        key->level, key->eye_height, key->look_x, key->look_z, key->look_height);
    if( key->glide )
        at += snprintf(line + at, sizeof(line) - (size_t)at, "glide %d,%d,%d,%d", key->move_rate, key->move_rate2,
            key->look_rate, key->look_rate2);
    else
        at += snprintf(line + at, sizeof(line) - (size_t)at, "cut");
    at += snprintf(line + at, sizeof(line) - (size_t)at, " | hold %d", key->hold);
    assert(at > 0);
    assert((size_t)at < sizeof(line));
    if( out_capacity > 0 )
        snprintf(out, out_capacity, "%s", line);
    return (size_t)at;
}

/* ---- the file ----------------------------------------------------------------------- */

void
Editor_CamPathsInit(struct Editor_CamPathsFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

void
Editor_CamPathsFree(struct Editor_CamPathsFile* file)
{
    if( !file )
        return;
    for( int i = 0; i < file->line_count; i++ )
        ToriMem_Free(ToriMem_Editor(), file->lines[i].text);
    ToriMem_Free(ToriMem_Editor(), file->lines);
    memset(file, 0, sizeof(*file));
}

static struct Editor_CamPathsLine*
file_push(
    struct Editor_CamPathsFile* file,
    const char* text,
    size_t length)
{
    struct Editor_CamPathsLine* line;

    if( file->line_count == file->line_capacity )
    {
        int const next = file->line_capacity ? file->line_capacity * 2 : 16;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    line = &file->lines[file->line_count++];
    memset(line, 0, sizeof(*line));
    line->text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(line->text);
    memcpy(line->text, text, length);
    line->text[length] = '\0';
    return line;
}

/** The record's name: the bytes of [at, end) up to a space. 0 when it is not
 *  a path name or anything but spaces follows it. */
static int
take_name(
    const char* at,
    const char* end,
    char out[EDITOR_CAMPATH_NAME_MAX])
{
    const char* stop = at;

    while( stop < end && !is_space(*stop) )
        stop++;
    if( stop == at || (size_t)(stop - at) >= EDITOR_CAMPATH_NAME_MAX )
        return 0;
    memcpy(out, at, (size_t)(stop - at));
    out[stop - at] = '\0';
    skip_spaces(&stop, end);
    return stop == end && Editor_CamPathNameValid(out);
}

/** One segment of a `path` line after its name. */
static int
parse_path_segment(
    const char* at,
    const char* end,
    struct Editor_CamPathsLine* line,
    unsigned* seen)
{
    const char* rest;

    if( take_word(at, end, "frame", &rest) )
    {
        if( rest != end || (*seen & 1u) )
            return 0;
        *seen |= 1u;
        line->frame = 1;
        return 1;
    }
    if( take_word(at, end, "coords", &rest) )
    {
        const char* value;
        if( *seen & 2u )
            return 0;
        *seen |= 2u;
        if( take_word(rest, end, "world", &value) )
            return value == end;
        if( take_word(rest, end, "instance", &value) )
        {
            if( value >= end || *value != 'm' )
                return 0;
            value++;
            if( !take_number(&value, end, 255, &line->origin_mx) || value >= end || *value != '_' )
                return 0;
            value++;
            if( !take_number(&value, end, 255, &line->origin_mz) || value != end )
                return 0;
            line->instance = 1;
            return 1;
        }
    }
    return 0;
}

/** One segment of a `key` line after its name. */
static int
parse_key_segment(
    const char* at,
    const char* end,
    struct Editor_CamKey* key,
    unsigned* seen)
{
    static const int k_eye_max[4] = { EDITOR_CAMPATH_TILE_MAX, EDITOR_CAMPATH_TILE_MAX, 3, EDITOR_CAMPATH_HEIGHT_MAX };
    static const int k_look_max[3] = { EDITOR_CAMPATH_TILE_MAX, EDITOR_CAMPATH_TILE_MAX, EDITOR_CAMPATH_HEIGHT_MAX };
    static const int k_glide_max[4] = { EDITOR_CAMPATH_RATE_MAX, EDITOR_CAMPATH_SNAP_RATE2 - 1, EDITOR_CAMPATH_RATE_MAX,
                                        EDITOR_CAMPATH_SNAP_RATE2 - 1 };
    const char* rest;
    int values[4];

    if( take_word(at, end, "eye", &rest) )
    {
        if( (*seen & 1u) || !take_numbers(rest, end, k_eye_max, 4, values) )
            return 0;
        *seen |= 1u;
        key->eye_x = values[0];
        key->eye_z = values[1];
        key->level = values[2];
        key->eye_height = values[3];
        return 1;
    }
    if( take_word(at, end, "look", &rest) )
    {
        if( (*seen & 2u) || !take_numbers(rest, end, k_look_max, 3, values) )
            return 0;
        *seen |= 2u;
        key->look_x = values[0];
        key->look_z = values[1];
        key->look_height = values[2];
        return 1;
    }
    if( take_word(at, end, "cut", &rest) )
    {
        if( (*seen & 4u) || rest != end )
            return 0;
        *seen |= 4u;
        key->glide = 0;
        return 1;
    }
    if( take_word(at, end, "glide", &rest) )
    {
        if( (*seen & 4u) || !take_numbers(rest, end, k_glide_max, 4, values) )
            return 0;
        *seen |= 4u;
        key->glide = 1;
        key->move_rate = values[0];
        key->move_rate2 = values[1];
        key->look_rate = values[2];
        key->look_rate2 = values[3];
        return 1;
    }
    if( take_word(at, end, "hold", &rest) )
    {
        if( (*seen & 8u) || !take_number(&rest, end, EDITOR_CAMPATH_HOLD_MAX, &key->hold) || rest != end
            || key->hold < 1 )
            return 0;
        *seen |= 8u;
        return 1;
    }
    return 0;
}

/** Interpret `line->text`. 0 when it is a record that does not parse. */
static int
parse_line(struct Editor_CamPathsLine* line)
{
    const char* at = line->text;
    const char* end = at + strlen(at);
    const char* rest;
    const char* bar;
    unsigned seen = 0;
    int is_key;

    while( end > at && is_space(end[-1]) )
        end--;
    skip_spaces(&at, end);
    line->kind = EDITOR_CAMPATHS_LINE_TEXT;
    if( at == end || *at == '#' || (end - at >= 2 && at[0] == '/' && at[1] == '/') )
        return 1;
    if( take_word(at, end, "path", &rest) )
        is_key = 0;
    else if( take_word(at, end, "key", &rest) )
        is_key = 1;
    else
        return 0;

    /* The name runs to the first bar. */
    bar = memchr(rest, '|', (size_t)(end - rest));
    {
        const char* stop = bar ? bar : end;
        while( stop > rest && is_space(stop[-1]) )
            stop--;
        if( !take_name(rest, stop, line->name) )
            return 0;
    }
    if( is_key )
        Editor_CamSpeedApply(CAM_DEFAULT_SPEED, &line->key);
    while( bar )
    {
        const char* seg = bar + 1;
        const char* seg_end;

        bar = memchr(seg, '|', (size_t)(end - seg));
        seg_end = bar ? bar : end;
        skip_spaces(&seg, seg_end);
        while( seg_end > seg && is_space(seg_end[-1]) )
            seg_end--;
        if( is_key ? !parse_key_segment(seg, seg_end, &line->key, &seen)
                   : !parse_path_segment(seg, seg_end, line, &seen) )
            return 0;
    }
    if( is_key && seen != 15u )
        return 0;
    line->kind = is_key ? EDITOR_CAMPATHS_LINE_KEY : EDITOR_CAMPATHS_LINE_PATH;
    return 1;
}

/** Line index of the `path` line naming `name`, or -1. */
static int
find_path(
    const struct Editor_CamPathsFile* file,
    const char* name)
{
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_CAMPATHS_LINE_PATH && strcmp(file->lines[i].name, name) == 0 )
            return i;
    return -1;
}

static int
line_of(
    const struct Editor_CamPathsLine* line,
    const char* name)
{
    return line->kind != EDITOR_CAMPATHS_LINE_TEXT && strcmp(line->name, name) == 0;
}

struct Editor_CamPathsParseResult
Editor_CamPathsParse(
    struct Editor_CamPathsFile* file,
    const char* text,
    size_t length)
{
    struct Editor_CamPathsParseResult result = { 1, 0 };
    size_t at = 0;

    assert(file);
    assert(text || length == 0);
    Editor_CamPathsFree(file);
    while( at < length )
    {
        const char* start = text + at;
        const char* newline = memchr(start, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - start) : length - at;
        struct Editor_CamPathsLine* line = file_push(file, start, line_length);
        int ok = parse_line(line);

        if( ok && line->kind == EDITOR_CAMPATHS_LINE_PATH )
        {
            /* Its own line is the last one: a second opening is a failure. */
            for( int i = 0; ok && i < file->line_count - 1; i++ )
                ok = !(file->lines[i].kind == EDITOR_CAMPATHS_LINE_PATH && strcmp(file->lines[i].name, line->name) == 0);
        }
        else if( ok && line->kind == EDITOR_CAMPATHS_LINE_KEY )
        {
            int keys = 0;
            for( int i = 0; i < file->line_count; i++ )
                keys += file->lines[i].kind == EDITOR_CAMPATHS_LINE_KEY && strcmp(file->lines[i].name, line->name) == 0;
            ok = find_path(file, line->name) >= 0 && keys <= EDITOR_CAMPATH_KEYS_MAX;
        }
        if( !ok && result.ok )
        {
            result.ok = 0;
            result.line = file->line_count;
            /* Carried as text, so the file still emits byte for byte. */
            line->kind = EDITOR_CAMPATHS_LINE_TEXT;
        }
        else if( !ok )
            line->kind = EDITOR_CAMPATHS_LINE_TEXT;
        at += line_length + (newline ? 1 : 0);
        file->final_newline = newline != NULL;
    }
    return result;
}

/**
 * The file's text with every line of `skip` (may be NULL) left out and
 * `block` (`block_count` lines; may be 0) put where line `at` stood (-1: at
 * the end).
 */
static size_t
emit_edited(
    const struct Editor_CamPathsFile* file,
    const char* skip,
    int at,
    char* const* block,
    int block_count,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;
    int written = 0;
    int final_newline = file->final_newline;

    for( int i = 0; i <= file->line_count; i++ )
    {
        if( i == at || (i == file->line_count && at < 0) )
            for( int b = 0; b < block_count; b++ )
            {
                if( written++ > 0 )
                    put(out, out_capacity, &total, "\n", 1);
                put_text(out, out_capacity, &total, block[b]);
                /* Appended lines end the file with a newline, as the editor
                 * writes every line. */
                if( i == file->line_count )
                    final_newline = 1;
            }
        if( i == file->line_count )
            break;
        if( skip && line_of(&file->lines[i], skip) )
            continue;
        if( written++ > 0 )
            put(out, out_capacity, &total, "\n", 1);
        put_text(out, out_capacity, &total, file->lines[i].text);
    }
    if( written > 0 && final_newline )
        put(out, out_capacity, &total, "\n", 1);
    put_terminate(out, out_capacity, total);
    return total;
}

size_t
Editor_CamPathsEmit(
    const struct Editor_CamPathsFile* file,
    char* out,
    size_t out_capacity)
{
    assert(file);
    assert(out || out_capacity == 0);
    return emit_edited(file, NULL, -2, NULL, 0, out, out_capacity);
}

int
Editor_CamPathsCount(const struct Editor_CamPathsFile* file)
{
    int count = 0;

    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        count += file->lines[i].kind == EDITOR_CAMPATHS_LINE_PATH;
    return count;
}

const char*
Editor_CamPathsName(
    const struct Editor_CamPathsFile* file,
    int nth)
{
    assert(file);
    assert(nth >= 0);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_CAMPATHS_LINE_PATH && nth-- == 0 )
            return file->lines[i].name;
    return NULL;
}

int
Editor_CamPathsGet(
    const struct Editor_CamPathsFile* file,
    const char* name,
    struct Editor_CamPath* out)
{
    int header;

    assert(file);
    assert(name);
    assert(out);
    header = find_path(file, name);
    if( header < 0 )
        return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->instance = file->lines[header].instance;
    out->origin_mx = file->lines[header].origin_mx;
    out->origin_mz = file->lines[header].origin_mz;
    out->frame = file->lines[header].frame;
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_CAMPATHS_LINE_KEY && strcmp(file->lines[i].name, name) == 0 )
        {
            /* The parser refused a 49th. */
            assert(out->key_count < EDITOR_CAMPATH_KEYS_MAX);
            out->keys[out->key_count++] = file->lines[i].key;
        }
    return 1;
}

/* ---- the verbs --------------------------------------------------------------------- */

int
Editor_CamPathsTextWith(
    const struct Editor_CamPathsFile* file,
    const char* old_name,
    const struct Editor_CamPath* path,
    char* out,
    size_t out_capacity,
    size_t* out_length)
{
    char* block[EDITOR_CAMPATH_KEYS_MAX + 1];
    int block_count = 0;
    int at = -1;
    int holder;

    assert(file);
    assert(path);
    assert(out || out_capacity == 0);
    assert(out_length);
    if( !Editor_CamPathValid(path) )
        return 0;
    if( old_name && old_name[0] )
    {
        at = find_path(file, old_name);
        if( at < 0 )
            return 0;
    }
    else
        old_name = NULL;
    holder = find_path(file, path->name);
    if( holder >= 0 && holder != at )
        return 0;

    {
        size_t const length = Editor_CamPathFormatHeader(path, NULL, 0);
        block[block_count] = ToriMem_Alloc(ToriMem_Editor(), length + 1);
        assert(block[block_count]);
        Editor_CamPathFormatHeader(path, block[block_count++], length + 1);
    }
    for( int i = 0; i < path->key_count; i++ )
    {
        size_t const length = Editor_CamKeyFormat(path->name, &path->keys[i], NULL, 0);
        block[block_count] = ToriMem_Alloc(ToriMem_Editor(), length + 1);
        assert(block[block_count]);
        Editor_CamKeyFormat(path->name, &path->keys[i], block[block_count++], length + 1);
    }
    if( at < 0 && file->line_count == 0 )
    {
        size_t total = 0;
        put(out, out_capacity, &total, k_header, sizeof(k_header) - 1);
        for( int b = 0; b < block_count; b++ )
        {
            put_text(out, out_capacity, &total, block[b]);
            put(out, out_capacity, &total, "\n", 1);
        }
        put_terminate(out, out_capacity, total);
        *out_length = total;
    }
    else
        *out_length = emit_edited(file, old_name, at, block, block_count, out, out_capacity);
    for( int b = 0; b < block_count; b++ )
        ToriMem_Free(ToriMem_Editor(), block[b]);
    return 1;
}

int
Editor_CamPathsTextWithout(
    const struct Editor_CamPathsFile* file,
    const char* name,
    char* out,
    size_t out_capacity,
    size_t* out_length)
{
    size_t length;
    char* text;

    assert(file);
    assert(name);
    assert(out || out_capacity == 0);
    assert(out_length);
    if( find_path(file, name) < 0 )
        return 0;
    /* A file that would be exactly the header goes back to absent. */
    length = emit_edited(file, name, -2, NULL, 0, NULL, 0);
    text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(text);
    emit_edited(file, name, -2, NULL, 0, text, length + 1);
    if( length == sizeof(k_header) - 1 && memcmp(text, k_header, length) == 0 )
    {
        ToriMem_Free(ToriMem_Editor(), text);
        put_terminate(out, out_capacity, 0);
        *out_length = 0;
        return 1;
    }
    ToriMem_Free(ToriMem_Editor(), text);
    *out_length = emit_edited(file, name, -2, NULL, 0, out, out_capacity);
    return 1;
}

/* ---- the maths ----------------------------------------------------------------------- */

void
Editor_CamAnglesTo(
    int eye_x,
    int eye_y,
    int eye_z,
    int target_x,
    int target_y,
    int target_z,
    int* out_pitch,
    int* out_yaw)
{
    int const dx = target_x - eye_x;
    int const dy = target_y - eye_y;
    int const dz = target_z - eye_z;
    int const distance = (int)sqrt((double)dx * dx + (double)dz * dz);

    assert(out_pitch);
    assert(out_yaw);
    /* app_camera.c app_cinema_angles_to, digit for digit: the yaw's factor is
     * negative because the scene turns the opposite way to the mathematical
     * angle. */
    *out_pitch = (int)(atan2((double)dy, (double)distance) * CAM_UNITS_PER_RADIAN) & 0x7ff;
    *out_yaw = (int)(atan2((double)dx, (double)dz) * -CAM_UNITS_PER_RADIAN) & 0x7ff;
}

int
Editor_CamPitchClamp(
    int pitch,
    int unlocked)
{
    int const low = unlocked ? EDITOR_CAMPATH_UNLOCK_PITCH_MIN : EDITOR_CAMPATH_PITCH_MIN;
    int const high = unlocked ? EDITOR_CAMPATH_UNLOCK_PITCH_MAX : EDITOR_CAMPATH_PITCH_MAX;

    return pitch < low ? low : pitch > high ? high : pitch;
}

int
Editor_CamPitchNeedsUnlock(int pitch)
{
    return pitch < EDITOR_CAMPATH_PITCH_MIN || pitch > EDITOR_CAMPATH_PITCH_MAX;
}

/** The scene point over tile (x, z)'s centre, `height` above its ground. */
static int
key_point(
    int tile_x,
    int tile_z,
    int level,
    int height,
    Editor_CamGroundFn ground,
    void* user,
    int* out_x,
    int* out_y,
    int* out_z)
{
    int ground_y = 0;

    *out_x = tile_x * 128 + 64;
    *out_z = tile_z * 128 + 64;
    if( !ground(user, *out_x, *out_z, level, &ground_y) )
        return 0;
    /* Up is -y. */
    *out_y = ground_y - height;
    return 1;
}

int
Editor_CamKeyLookPoint(
    const struct Editor_CamKey* key,
    Editor_CamGroundFn ground,
    void* user,
    int* out_x,
    int* out_y,
    int* out_z)
{
    assert(key);
    assert(ground);
    assert(out_x);
    assert(out_y);
    assert(out_z);
    return key_point(key->look_x, key->look_z, key->level, key->look_height, ground, user, out_x, out_y, out_z);
}

int
Editor_CamKeyEye(
    const struct Editor_CamKey* key,
    Editor_CamGroundFn ground,
    void* user,
    struct Editor_CamEye* out_eye)
{
    int look_x;
    int look_y;
    int look_z;

    assert(key);
    assert(ground);
    assert(out_eye);
    if( !key_point(key->eye_x, key->eye_z, key->level, key->eye_height, ground, user, &out_eye->x, &out_eye->y,
            &out_eye->z) )
        return 0;
    if( !Editor_CamKeyLookPoint(key, ground, user, &look_x, &look_y, &look_z) )
        return 0;
    Editor_CamAnglesTo(out_eye->x, out_eye->y, out_eye->z, look_x, look_y, look_z, &out_eye->pitch, &out_eye->yaw);
    return 1;
}

/** The short way from angle `from` to `to`, -1024..1023. */
static int
angle_delta(
    int from,
    int to)
{
    int delta = (to - from) & 0x7ff;
    return delta >= 1024 ? delta - 2048 : delta;
}

/** The line a fitted camera looks along: from the snapped eye towards the
 *  subject, falling `slope` per unit of ground covered. */
struct FitLine
{
    int centre_x;
    int centre_z;
    int eye_y;
    double ux;
    double uz;
    double slope;
    int level;
    Editor_CamGroundFn ground;
    void* user;
};

/** How tile (tx, tz) would do as the look tile: 1 with its centre's angle
 *  off the line (a tangent), how far along the line it stands and the
 *  height that keeps the pitch; 0 when it cannot be one -- behind or beside
 *  the eye, off the terrain, or the line is under its ground there (the game
 *  takes no negative height). */
static int
fit_candidate(
    const struct FitLine* line,
    int tx,
    int tz,
    double* out_error,
    double* out_along,
    int* out_height)
{
    int const tcx = tx * 128 + 64;
    int const tcz = tz * 128 + 64;
    double const ox = tcx - line->centre_x;
    double const oz = tcz - line->centre_z;
    double const along = ox * line->ux + oz * line->uz;
    double want;
    int g = 0;

    if( tx < 0 || tz < 0 || tx > EDITOR_CAMPATH_TILE_MAX || tz > EDITOR_CAMPATH_TILE_MAX || along < 64.0 )
        return 0;
    if( !line->ground(line->user, tcx, tcz, line->level, &g) )
        return 0;
    want = (double)g - (line->eye_y + line->slope * sqrt(ox * ox + oz * oz));
    if( want < -0.5 || want > EDITOR_CAMPATH_HEIGHT_MAX )
        return 0;
    *out_error = fabs(ox * line->uz - oz * line->ux) / along;
    *out_along = along;
    *out_height = want < 0.0 ? 0 : (int)(want + 0.5);
    return 1;
}

enum Editor_CamFit
Editor_CamKeyFromEye(
    const struct Editor_CamEye* eye,
    int level,
    Editor_CamGroundFn ground,
    void* user,
    struct Editor_CamKey* out_key,
    struct Editor_CamFitNote* note)
{
    struct Editor_CamFitNote local;
    int pitch;
    int yaw;
    int tile_x;
    int tile_z;
    int centre_x;
    int centre_z;
    int ground_y = 0;
    int height;
    int eye_y;
    double fx;
    double fy;
    double fz;
    double subject_x;
    double subject_y;
    double subject_z;
    int best = 0;
    int best_x = 0;
    int best_z = 0;
    int best_height = 0;
    double best_error = 0.0;
    double best_along = 0.0;

    assert(eye);
    assert(level >= 0);
    assert(level <= 3);
    assert(ground);
    assert(out_key);
    if( !note )
        note = &local;
    memset(note, 0, sizeof(*note));

    pitch = eye->pitch & 0x7ff;
    yaw = eye->yaw & 0x7ff;
    /* Past straight down the camera is on its back, past 1024 it looks up. */
    if( pitch > 1024 )
        return EDITOR_CAM_FIT_LOOKS_UP;
    if( pitch > 512 )
        pitch = 512;
    if( eye->x < 0 || eye->z < 0 || eye->x / 128 > EDITOR_CAMPATH_TILE_MAX || eye->z / 128 > EDITOR_CAMPATH_TILE_MAX )
        return EDITOR_CAM_FIT_EYE_OFF_WORLD;
    tile_x = eye->x / 128;
    tile_z = eye->z / 128;
    centre_x = tile_x * 128 + 64;
    centre_z = tile_z * 128 + 64;
    if( !ground(user, centre_x, centre_z, level, &ground_y) )
        return EDITOR_CAM_FIT_EYE_OFF_TERRAIN;
    height = ground_y - eye->y;
    if( height < 0 )
    {
        height = 0;
        note->eye_raised = 1;
    }
    if( height > EDITOR_CAMPATH_HEIGHT_MAX )
        height = EDITOR_CAMPATH_HEIGHT_MAX;
    eye_y = ground_y - height;

    /* The eye's view ray (y down): yaw 0 looks north (+z), a growing yaw
     * turns towards -x; the pitch tips it down. */
    {
        double const a = yaw * (2.0 * CAM_PI / 2048.0);
        double const p = pitch * (2.0 * CAM_PI / 2048.0);
        fx = -cos(p) * sin(a);
        fy = sin(p);
        fz = cos(p) * cos(a);
    }

    /* The subject: where that ray meets the ground. A ray that leaves the
     * terrain or never comes down is taken to look CAM_SUBJECT_FAR out. */
    {
        double const flat = sqrt(fx * fx + fz * fz);
        double far_t = flat > 0.01 ? CAM_SUBJECT_FAR / flat : CAM_SUBJECT_FAR;
        double reached = 0.0;
        double before = 0.0; /* how far above the ground the ray was a step ago */
        int hit = 0;

        for( int t = CAM_MARCH_STEP; t <= CAM_MARCH_MAX && !hit; t += CAM_MARCH_STEP )
        {
            double const px = eye->x + fx * t;
            double const py = eye->y + fy * t;
            double const pz = eye->z + fz * t;
            int g = 0;
            double above;

            if( px < 0.0 || pz < 0.0 || !ground(user, (int)px, (int)pz, level, &g) )
                break;
            above = (double)g - py;
            if( above <= 0.0 )
            {
                /* Between the last step and this one, where the two meet. */
                double const back = before > 0.0 ? CAM_MARCH_STEP * (-above) / (before - above) : 0.0;
                far_t = t - back;
                hit = 1;
            }
            before = above;
            reached = t;
        }
        if( !hit && reached > 0.0 && far_t > reached )
            far_t = reached;
        subject_x = eye->x + fx * far_t;
        subject_y = eye->y + fy * far_t;
        subject_z = eye->z + fz * far_t;
    }

    /* The look tile. The game aims from one tile centre at another, so the
     * yaws it can hold from here are the directions of those centres: the
     * candidates are the tiles along the line from the SNAPPED eye to the
     * subject and their neighbours, each with the height that keeps the
     * pitch exactly -- which leaves its centre's offset from the line, as an
     * angle from the eye, as the only error. Two passes: the least such
     * angle any of them has, then the FARTHEST tile within
     * CAM_FIT_TOLERANCE of it, because a look tile beside the subject is
     * the one a person reading the script expects. */
    {
        struct FitLine line;
        double const dx = subject_x - centre_x;
        double const dz = subject_z - centre_z;
        double const length = sqrt(dx * dx + dz * dz);

        memset(&line, 0, sizeof(line));
        line.centre_x = centre_x;
        line.centre_z = centre_z;
        line.eye_y = eye_y;
        line.level = level;
        line.ground = ground;
        line.user = user;
        if( length >= 1.0 )
        {
            line.ux = dx / length;
            line.uz = dz / length;
            line.slope = (subject_y - eye_y) / length;
        }
        for( int pass = 0; pass < 2 && length >= 1.0 && (pass == 0 || best); pass++ )
        {
            double const allowed = best_error > CAM_FIT_TOLERANCE ? best_error : CAM_FIT_TOLERANCE;
            int last_x = tile_x;
            int last_z = tile_z;

            for( double d = 32.0; d <= length + 64.0; d += 32.0 )
            {
                double const qx = centre_x + line.ux * d;
                double const qz = centre_z + line.uz * d;
                int base_x;
                int base_z;

                if( qx < 0.0 || qz < 0.0 )
                    break;
                base_x = (int)qx / 128;
                base_z = (int)qz / 128;
                if( base_x == last_x && base_z == last_z )
                    continue;
                last_x = base_x;
                last_z = base_z;
                for( int n = 0; n < 9; n++ )
                {
                    int const tx = base_x + n % 3 - 1;
                    int const tz = base_z + n / 3 - 1;
                    double error;
                    double along;
                    int want;

                    if( (tx == tile_x && tz == tile_z) || !fit_candidate(&line, tx, tz, &error, &along, &want) )
                        continue;
                    if( pass == 0 )
                    {
                        if( !best || error < best_error )
                            best_error = error;
                        best = 1;
                    }
                    else if( error <= allowed + 1e-9 && along > best_along )
                    {
                        best_along = along;
                        best_x = tx;
                        best_z = tz;
                        best_height = want;
                    }
                }
            }
        }
    }
    if( !best )
    {
        /* Straight down, or so nearly that the ray never leaves the eye's
         * tile: the neighbour the yaw points at, as steeply as its ground
         * allows. */
        double const a = yaw * (2.0 * CAM_PI / 2048.0);
        int const step_x = (int)lround(-sin(a));
        int const step_z = (int)lround(cos(a));
        int g = 0;

        note->steep = 1;
        best_x = tile_x + step_x;
        best_z = tile_z + step_z;
        best_height = 0;
        if( best_x < 0 || best_z < 0 || best_x > EDITOR_CAMPATH_TILE_MAX || best_z > EDITOR_CAMPATH_TILE_MAX
            || !ground(user, best_x * 128 + 64, best_z * 128 + 64, level, &g) )
        {
            best_x = tile_x;
            best_z = tile_z;
        }
    }

    out_key->eye_x = tile_x;
    out_key->eye_z = tile_z;
    out_key->level = level;
    out_key->eye_height = height;
    out_key->look_x = best_x;
    out_key->look_z = best_z;
    out_key->look_height = best_height;
    {
        struct Editor_CamEye fitted;
        if( Editor_CamKeyEye(out_key, ground, user, &fitted) )
        {
            int const dyaw = angle_delta(yaw, fitted.yaw);
            int const dpitch = angle_delta(pitch, fitted.pitch);
            note->yaw_error = dyaw < 0 ? -dyaw : dyaw;
            note->pitch_error = dpitch < 0 ? -dpitch : dpitch;
        }
    }
    return EDITOR_CAM_FIT_OK;
}

/* ---- the scripted camera, stepped ------------------------------------------------------ */

int
Editor_CamEase(
    int current,
    int target,
    int rate,
    int rate2)
{
    if( current < target )
    {
        current += rate + (target - current) * rate2 / 1000;
        if( current > target )
            current = target;
    }
    else if( current > target )
    {
        current -= rate + (current - target) * rate2 / 1000;
        if( current < target )
            current = target;
    }
    return current;
}

int
Editor_CamEaseYaw(
    int current,
    int target,
    int rate,
    int rate2)
{
    int delta = target - current;

    if( delta > 1024 )
        delta -= 2048;
    else if( delta < -1024 )
        delta += 2048;

    if( delta > 0 )
        current = (current + rate + delta * rate2 / 1000) & 0x7ff;
    else if( delta < 0 )
        current = (current - rate - -delta * rate2 / 1000) & 0x7ff;

    /* Stopping when the sign of what is left flips: the linear ease cannot
     * see past the seam. */
    if( delta != 0 )
    {
        int remaining = target - current;
        if( remaining > 1024 )
            remaining -= 2048;
        else if( remaining < -1024 )
            remaining += 2048;
        if( (remaining < 0 && delta > 0) || (remaining > 0 && delta < 0) )
            current = target;
    }
    return current;
}

void
Editor_CamSimInit(
    struct Editor_CamSim* sim,
    const struct Editor_CamEye* eye,
    int unlocked)
{
    assert(sim);
    assert(eye);
    memset(sim, 0, sizeof(*sim));
    sim->eye = *eye;
    sim->eye.yaw &= 0x7ff;
    sim->unlocked = unlocked ? 1 : 0;
}

/** The angles the camera turns towards: the look point's, clamped. */
static void
sim_aim(
    const struct Editor_CamSim* sim,
    int* out_pitch,
    int* out_yaw)
{
    Editor_CamAnglesTo(sim->eye.x, sim->eye.y, sim->eye.z, sim->look_x, sim->look_y, sim->look_z, out_pitch, out_yaw);
    *out_pitch = Editor_CamPitchClamp(*out_pitch, sim->unlocked);
}

void
Editor_CamSimMoveTo(
    struct Editor_CamSim* sim,
    int x,
    int y,
    int z,
    int rate,
    int rate2)
{
    assert(sim);
    sim->move_set = 1;
    sim->move_x = x;
    sim->move_y = y;
    sim->move_z = z;
    sim->move_rate = rate;
    sim->move_rate2 = rate2;
    /* rs_gameproto_exec.c CAM_MOVETO: a rate2 of 100 or more is there now. */
    if( rate2 >= EDITOR_CAMPATH_SNAP_RATE2 )
    {
        sim->eye.x = x;
        sim->eye.y = y;
        sim->eye.z = z;
    }
}

void
Editor_CamSimLookAt(
    struct Editor_CamSim* sim,
    int x,
    int y,
    int z,
    int rate,
    int rate2)
{
    assert(sim);
    sim->look_set = 1;
    sim->look_x = x;
    sim->look_y = y;
    sim->look_z = z;
    sim->look_rate = rate;
    sim->look_rate2 = rate2;
    if( rate2 >= EDITOR_CAMPATH_SNAP_RATE2 )
        sim_aim(sim, &sim->eye.pitch, &sim->eye.yaw);
}

void
Editor_CamSimStep(struct Editor_CamSim* sim)
{
    assert(sim);
    if( sim->move_set )
    {
        sim->eye.x = Editor_CamEase(sim->eye.x, sim->move_x, sim->move_rate, sim->move_rate2);
        sim->eye.y = Editor_CamEase(sim->eye.y, sim->move_y, sim->move_rate, sim->move_rate2);
        sim->eye.z = Editor_CamEase(sim->eye.z, sim->move_z, sim->move_rate, sim->move_rate2);
    }
    if( sim->look_set )
    {
        int pitch;
        int yaw;
        sim_aim(sim, &pitch, &yaw);
        sim->eye.pitch = Editor_CamEase(sim->eye.pitch, pitch, sim->look_rate, sim->look_rate2);
        sim->eye.yaw = Editor_CamEaseYaw(sim->eye.yaw, yaw, sim->look_rate, sim->look_rate2);
    }
}

int
Editor_CamSimArrived(const struct Editor_CamSim* sim)
{
    assert(sim);
    if( sim->move_set && (sim->eye.x != sim->move_x || sim->eye.y != sim->move_y || sim->eye.z != sim->move_z) )
        return 0;
    if( sim->look_set )
    {
        int pitch;
        int yaw;
        sim_aim(sim, &pitch, &yaw);
        if( sim->eye.pitch != pitch || sim->eye.yaw != yaw )
            return 0;
    }
    return 1;
}

int
Editor_CamGlideCycles(
    const struct Editor_CamEye* from,
    const struct Editor_CamKey* key,
    Editor_CamGroundFn ground,
    void* user,
    int unlocked,
    int max_cycles)
{
    struct Editor_CamSim sim;
    struct Editor_CamEye target;
    int look_x;
    int look_y;
    int look_z;
    int cut;

    assert(from);
    assert(key);
    assert(ground);
    cut = !key->glide;
    if( !Editor_CamKeyEye(key, ground, user, &target)
        || !Editor_CamKeyLookPoint(key, ground, user, &look_x, &look_y, &look_z) )
        return -1;
    Editor_CamSimInit(&sim, from, unlocked);
    Editor_CamSimMoveTo(&sim, target.x, target.y, target.z, cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate2);
    Editor_CamSimLookAt(&sim, look_x, look_y, look_z, cut ? EDITOR_CAMPATH_CUT_RATE : key->look_rate,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->look_rate2);
    for( int cycle = 0; cycle <= max_cycles; cycle++ )
    {
        if( Editor_CamSimArrived(&sim) )
            return cycle;
        Editor_CamSimStep(&sim);
    }
    return -1;
}

int
Editor_CamCyclesToTicks(int cycles)
{
    assert(cycles >= 0);
    return (cycles + EDITOR_CAMPATH_CYCLES_PER_TICK - 1) / EDITOR_CAMPATH_CYCLES_PER_TICK;
}

/* ---- the speed presets ---------------------------------------------------------------- */

const char*
Editor_CamSpeedName(int speed)
{
    assert(speed >= 0);
    assert(speed <= EDITOR_CAM_SPEED_CUSTOM);
    return k_speed_names[speed];
}

void
Editor_CamSpeedApply(
    int preset,
    struct Editor_CamKey* key)
{
    assert(preset >= 0);
    assert(preset < EDITOR_CAM_SPEED_PRESETS);
    assert(key);
    key->glide = 1;
    key->move_rate = key->move_rate2 = key->look_rate = key->look_rate2 = k_speed_rates[preset];
}

int
Editor_CamSpeedOf(const struct Editor_CamKey* key)
{
    assert(key);
    for( int i = 0; i < EDITOR_CAM_SPEED_PRESETS; i++ )
        if( key->move_rate == k_speed_rates[i] && key->move_rate2 == k_speed_rates[i]
            && key->look_rate == k_speed_rates[i] && key->look_rate2 == k_speed_rates[i] )
            return i;
    return EDITOR_CAM_SPEED_CUSTOM;
}

/* ---- the script ---------------------------------------------------------------------- */

void
Editor_CamCoordText(
    int x,
    int z,
    int level,
    char* out,
    size_t out_size)
{
    assert(out);
    assert(out_size > 0);
    snprintf(out, out_size, "%d_%d_%d_%d_%d", level, x / 64, z / 64, x % 64, z % 64);
}

void
Editor_CamPathCoordText(
    const struct Editor_CamPath* path,
    int x,
    int z,
    int level,
    char* out,
    size_t out_size)
{
    assert(path);
    assert(out);
    assert(out_size > 0);
    if( path->instance )
        snprintf(out, out_size, "map_instance_coord($handle, %d, %d, %d)", x - path->origin_mx * 64,
            z - path->origin_mz * 64, level);
    else
        Editor_CamCoordText(x, z, level, out, out_size);
}

/** One keyframe's cam_moveto and cam_lookat. */
static void
script_key(
    const struct Editor_CamPath* path,
    const struct Editor_CamKey* key,
    char* out,
    size_t capacity,
    size_t* total)
{
    char coord[96];
    char line[192];
    int const cut = !key->glide;

    Editor_CamPathCoordText(path, key->eye_x, key->eye_z, key->level, coord, sizeof(coord));
    snprintf(line, sizeof(line), "cam_moveto(%s, %d, %d, %d);\n", coord, key->eye_height,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate, cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate2);
    put_text(out, capacity, total, line);
    Editor_CamPathCoordText(path, key->look_x, key->look_z, key->level, coord, sizeof(coord));
    snprintf(line, sizeof(line), "cam_lookat(%s, %d, %d, %d);\n", coord, key->look_height,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->look_rate, cut ? EDITOR_CAMPATH_CUT_RATE : key->look_rate2);
    put_text(out, capacity, total, line);
}

static void
script_delay(
    int ticks,
    char* out,
    size_t capacity,
    size_t* total)
{
    char line[32];

    /* p_delay(n) resumes n + 1 ticks on. */
    assert(ticks >= 1);
    snprintf(line, sizeof(line), "p_delay(%d);\n", ticks - 1);
    put_text(out, capacity, total, line);
}

size_t
Editor_CamPathScript(
    const struct Editor_CamPath* path,
    int unlock,
    char* out,
    size_t out_capacity)
{
    char line[256];
    size_t total = 0;

    assert(path);
    assert(out || out_capacity == 0);
    assert(Editor_CamPathValid(path));

    snprintf(line, sizeof(line), "[proc,%s_camera]%s\n", path->name, path->instance ? "(int $handle)" : "");
    put_text(out, out_capacity, &total, line);
    snprintf(line, sizeof(line), "// generated by the map editor's Camera path panel from %s: %s\n",
        EDITOR_CAMPATHS_FILE_PATH, path->name);
    put_text(out, out_capacity, &total, line);
    if( path->instance )
        snprintf(line, sizeof(line),
            "// ~%s_camera($handle); plays it in the map instance $handle, whose south-west square is a copy of m%d_%d\n",
            path->name, path->origin_mx, path->origin_mz);
    else
        snprintf(line, sizeof(line), "// ~%s_camera; plays it for a player standing in the scene these tiles are in\n",
            path->name);
    put_text(out, out_capacity, &total, line);

    if( path->frame )
    {
        /* general/scripts/cutscene_frame.rs2's header: T the black, T+2 the
         * lock and the HUD, then the camera and the black lifted. */
        put_text(out, out_capacity, &total,
            "~fade_screen(0, 255, 0, 0);\n"
            "p_delay(1);\n"
            "~cutscene_lock(3, 0);\n"
            "~cutscene_hud_lock;\n"
            "p_delay(0);\n");
    }
    if( unlock )
        /* A framing steeper or flatter than the player's own camera may tip:
         * without this the client clamps the scripted pitch to 128..383. */
        put_text(out, out_capacity, &total, "cam_unlock(true);\n");
    for( int i = 0; i < path->key_count; i++ )
    {
        const struct Editor_CamKey* key = &path->keys[i];

        script_key(path, key, out, out_capacity, &total);
        if( path->frame && i == 0 )
        {
            /* The first framing is in place a tick before the black lifts;
             * that tick is the first of its hold. */
            put_text(out, out_capacity, &total, "p_delay(0);\n~fade_screen(0, 0, 0, 255);\n");
            if( key->hold >= 2 )
                script_delay(key->hold - 1, out, out_capacity, &total);
        }
        else
            script_delay(key->hold, out, out_capacity, &total);
    }
    if( path->frame )
    {
        /* E the black, E+2 the unlock -- ~cutscene_hud_unlock sends
         * cam_unlock(false) and cam_reset itself -- E+4 the black lifted,
         * E+6 the overlay closed. */
        put_text(out, out_capacity, &total,
            "~fade_screen(0, 255, 0, 0);\n"
            "p_delay(1);\n"
            "~cutscene_unlock;\n"
            "~cutscene_hud_unlock;\n"
            "p_delay(1);\n"
            "~fade_screen(0, 0, 0, 255);\n"
            "p_delay(1);\n"
            "if_closesub(toplevel_osrs_stretch:overlay_atmosphere);\n");
    }
    else
    {
        if( unlock )
            put_text(out, out_capacity, &total, "cam_unlock(false);\n");
        put_text(out, out_capacity, &total, "cam_reset;\n");
    }
    put_terminate(out, out_capacity, total);
    return total;
}

int
Editor_CamPathTicks(const struct Editor_CamPath* path)
{
    int ticks = 0;

    assert(path);
    for( int i = 0; i < path->key_count; i++ )
        ticks += path->keys[i].hold;
    /* The frame: 2 + 1 ticks before the first keyframe, 2 + 2 + 2 after the
     * last (the p_delays above). */
    if( path->frame )
        ticks += 9;
    return ticks;
}

void
Editor_CamKeyCheat(
    const struct Editor_CamKey* key,
    char* out,
    size_t out_size)
{
    char eye[48];
    char look[48];
    int cut;

    assert(key);
    assert(out);
    assert(out_size > 0);
    cut = !key->glide;
    Editor_CamCoordText(key->eye_x, key->eye_z, key->level, eye, sizeof(eye));
    Editor_CamCoordText(key->look_x, key->look_z, key->level, look, sizeof(look));
    snprintf(out, out_size, "cam %s %d %s %d %d %d", eye, key->eye_height, look, key->look_height,
        cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate, cut ? EDITOR_CAMPATH_CUT_RATE : key->move_rate2);
}

void
Editor_CamKeyRowText(
    const struct Editor_CamKey* key,
    int index,
    char* out,
    size_t out_size)
{
    char move[48];
    char level[8] = "";

    assert(key);
    assert(out);
    assert(out_size > 0);
    /* The level only when it is not the ground's: most paths never leave it. */
    if( key->level != 0 )
        snprintf(level, sizeof(level), " L%d", key->level);
    /* A preset is named alone ("Medium"): only a glide has a speed, and the
     * row must keep both heights inside a 420 px window. */
    if( !key->glide )
        snprintf(move, sizeof(move), "cut");
    else if( Editor_CamSpeedOf(key) != EDITOR_CAM_SPEED_CUSTOM )
        snprintf(move, sizeof(move), "%s", k_speed_names[Editor_CamSpeedOf(key)]);
    else
        snprintf(move, sizeof(move), "glide %d,%d/%d,%d", key->move_rate, key->move_rate2, key->look_rate,
            key->look_rate2);
    /* How the leg moves and how long it holds first: in a docked column the
     * row's tail is what the panel's edge cuts. */
    snprintf(out, out_size, "%d  %s, hold %d  eye %d,%d%s +%d  look %d,%d +%d", index + 1, move, key->hold, key->eye_x,
        key->eye_z, level, key->eye_height, key->look_x, key->look_z, key->look_height);
}
