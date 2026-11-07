/*
 * The ENCOUNTERS document (editor_encounters.h): the line-preserving codec,
 * one set in memory, the verbs' new text; and the encounter builder's two
 * readers, the death-drop script and the ai templates.
 */

#include "editor_encounters.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_header[] = "# ToriRSMapEd encounter sets (editor/encounters.txt).\n"
                               "# encounter <name> [| colour <0..7>] [| spawn <npc|obj>:<name> <x> <z> <level> [<count>]]... "
                               "[| note <text>]\n";

/* ---- names ------------------------------------------------------------------ */

int
Editor_EncounterNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_ENCOUNTER_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

int
Editor_EncounterNoteValid(const char* note)
{
    size_t length;

    assert(note);
    length = strlen(note);
    if( length >= EDITOR_ENCOUNTER_NOTE_MAX )
        return 0;
    if( length > 0 && (note[0] == ' ' || note[length - 1] == ' ') )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char const c = (unsigned char)note[i];
        if( c < 0x20 || c == 0x7f || c == '|' )
            return 0;
    }
    return 1;
}

int
Editor_EncounterSpawnNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_CMD_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

/* ---- lines ---------------------------------------------------------------------- */

void
Editor_EncountersInit(struct Editor_EncountersFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

static void
line_free(struct Editor_EncountersLine* line)
{
    ToriMem_Free(ToriMem_Editor(), line->text);
    ToriMem_Free(ToriMem_Editor(), line->encounter);
    memset(line, 0, sizeof(*line));
}

void
Editor_EncountersFree(struct Editor_EncountersFile* file)
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

static struct Editor_EncountersLine*
append_slot(struct Editor_EncountersFile* file)
{
    if( file->line_count == file->line_capacity )
    {
        int const next = file->line_capacity ? file->line_capacity * 2 : 32;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    memset(&file->lines[file->line_count], 0, sizeof(file->lines[0]));
    return &file->lines[file->line_count++];
}

/* ---- one record line ---------------------------------------------------------- */

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

/** [begin, end) into `out` (NUL-terminated); 0 when it does not fit. */
static int
copy_run(
    char* out,
    size_t size,
    const char* begin,
    const char* end)
{
    if( (size_t)(end - begin) >= size )
        return 0;
    memcpy(out, begin, (size_t)(end - begin));
    out[end - begin] = '\0';
    return 1;
}

/** The next space-separated word of [*at, end). */
static int
next_word(
    const char** at,
    const char* end,
    char* out,
    size_t size)
{
    const char* p = *at;
    const char* start;

    while( p < end && (*p == ' ' || *p == '\t') )
        p++;
    if( p >= end )
        return 0;
    start = p;
    while( p < end && *p != ' ' && *p != '\t' )
        p++;
    if( !copy_run(out, size, start, p) )
        return 0;
    *at = p;
    return 1;
}

static int
parse_int(
    const char* word,
    int lo,
    int hi,
    int* out)
{
    long value = 0;

    if( *word == '\0' )
        return 0;
    for( const char* p = word; *p; p++ )
    {
        if( *p < '0' || *p > '9' )
            return 0;
        value = value * 10 + (*p - '0');
        if( value > hi )
            return 0;
    }
    if( value < lo )
        return 0;
    *out = (int)value;
    return 1;
}

/** "<npc|obj>:<name> <x> <z> <level> [<count>]" */
static int
parse_spawn(
    const char* begin,
    const char* end,
    struct Editor_EncounterSpawn* out)
{
    char word[EDITOR_CMD_NAME_MAX + 8];
    const char* at = begin;
    const char* colon;

    memset(out, 0, sizeof(*out));
    out->count = 1;
    if( !next_word(&at, end, word, sizeof(word)) )
        return 0;
    colon = strchr(word, ':');
    if( !colon )
        return 0;
    if( colon - word == 3 && memcmp(word, "npc", 3) == 0 )
        out->is_obj = 0;
    else if( colon - word == 3 && memcmp(word, "obj", 3) == 0 )
        out->is_obj = 1;
    else
        return 0;
    if( strlen(colon + 1) >= sizeof(out->name) )
        return 0;
    snprintf(out->name, sizeof(out->name), "%s", colon + 1);
    if( !Editor_EncounterSpawnNameValid(out->name) )
        return 0;
    if( !next_word(&at, end, word, sizeof(word)) || !parse_int(word, 0, EDITOR_ENCOUNTER_TILE_MAX, &out->x) )
        return 0;
    if( !next_word(&at, end, word, sizeof(word)) || !parse_int(word, 0, EDITOR_ENCOUNTER_TILE_MAX, &out->z) )
        return 0;
    if( !next_word(&at, end, word, sizeof(word)) || !parse_int(word, 0, 3, &out->level) )
        return 0;
    if( next_word(&at, end, word, sizeof(word)) )
    {
        if( !parse_int(word, 1, 1 << 30, &out->count) )
            return 0;
        if( next_word(&at, end, word, sizeof(word)) )
            return 0;
    }
    return 1;
}

/** `seg` starts with the keyword `word` followed by a space (or is it). */
static int
segment_is(
    const char* seg,
    const char* seg_end,
    const char* word,
    const char** out_rest)
{
    size_t const n = strlen(word);
    if( (size_t)(seg_end - seg) < n || memcmp(seg, word, n) != 0 )
        return 0;
    if( seg + n < seg_end && seg[n] != ' ' && seg[n] != '\t' )
        return 0;
    *out_rest = seg + n;
    return 1;
}

int
Editor_EncountersParseLine(
    const char* text,
    struct Editor_Encounter* out)
{
    struct Editor_Encounter scratch;
    struct Editor_Encounter* encounter;
    const char* begin = text;
    const char* end;
    const char* rest;
    const char* bar;
    int seen_colour = 0;
    int seen_note = 0;

    assert(text);
    end = text + strlen(text);
    trim(&begin, &end);
    if( begin == end || begin[0] == '#' || (end - begin >= 2 && begin[0] == '/' && begin[1] == '/') )
        return EDITOR_ENCOUNTERS_LINE_TEXT;
    if( !segment_is(begin, end, "encounter", &rest) )
        return -1;
    encounter = out ? out : &scratch;
    memset(encounter, 0, sizeof(*encounter));
    encounter->colour = -1;

    bar = memchr(rest, '|', (size_t)(end - rest));
    {
        const char* name_begin = rest;
        const char* name_end = bar ? bar : end;
        trim(&name_begin, &name_end);
        if( !copy_run(encounter->name, sizeof(encounter->name), name_begin, name_end)
            || !Editor_EncounterNameValid(encounter->name) )
            return -1;
    }
    while( bar )
    {
        const char* seg = bar + 1;
        const char* seg_end;
        const char* value;

        bar = memchr(seg, '|', (size_t)(end - seg));
        seg_end = bar ? bar : end;
        trim(&seg, &seg_end);
        if( segment_is(seg, seg_end, "colour", &value) )
        {
            char word[16];
            const char* at = value;
            if( seen_colour++ || !next_word(&at, seg_end, word, sizeof(word))
                || !parse_int(word, 0, EDITOR_ENCOUNTER_COLOURS - 1, &encounter->colour)
                || next_word(&at, seg_end, word, sizeof(word)) )
                return -1;
        }
        else if( segment_is(seg, seg_end, "note", &value) )
        {
            const char* note_begin = value;
            const char* note_end = seg_end;
            trim(&note_begin, &note_end);
            if( seen_note++ || !copy_run(encounter->note, sizeof(encounter->note), note_begin, note_end)
                || !Editor_EncounterNoteValid(encounter->note) )
                return -1;
        }
        else if( segment_is(seg, seg_end, "spawn", &value) )
        {
            struct Editor_EncounterSpawn spawn;
            if( !parse_spawn(value, seg_end, &spawn) || !Editor_EncounterAddSpawn(encounter, &spawn) )
                return -1;
        }
        else
            return -1;
    }
    return EDITOR_ENCOUNTERS_LINE_RECORD;
}

/** Appends with snprintf's contract over (out, capacity, *total). */
static void
put(
    char* out,
    size_t capacity,
    size_t* total,
    const char* text,
    size_t length)
{
    if( out && *total < capacity )
    {
        size_t const room = capacity - *total;
        memcpy(out + *total, text, length < room ? length : room);
    }
    *total += length;
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

size_t
Editor_EncounterFormat(
    const struct Editor_Encounter* encounter,
    char* out,
    size_t out_capacity)
{
    char piece[EDITOR_CMD_NAME_MAX + 96];
    size_t total = 0;
    int n;

    assert(encounter);
    assert(out || out_capacity == 0);

    n = snprintf(piece, sizeof(piece), "encounter %s", encounter->name);
    put(out, out_capacity, &total, piece, (size_t)n);
    if( encounter->colour >= 0 )
    {
        n = snprintf(piece, sizeof(piece), " | colour %d", encounter->colour);
        put(out, out_capacity, &total, piece, (size_t)n);
    }
    for( int i = 0; i < encounter->spawn_count; i++ )
    {
        const struct Editor_EncounterSpawn* s = &encounter->spawns[i];
        if( s->count != 1 )
            n = snprintf(piece, sizeof(piece), " | spawn %s:%s %d %d %d %d", s->is_obj ? "obj" : "npc", s->name, s->x,
                s->z, s->level, s->count);
        else
            n = snprintf(piece, sizeof(piece), " | spawn %s:%s %d %d %d", s->is_obj ? "obj" : "npc", s->name, s->x,
                s->z, s->level);
        put(out, out_capacity, &total, piece, (size_t)n);
    }
    if( encounter->note[0] )
    {
        put(out, out_capacity, &total, " | note ", 8);
        put(out, out_capacity, &total, encounter->note, strlen(encounter->note));
    }
    put_terminate(out, out_capacity, total);
    return total;
}

/* ---- the file ----------------------------------------------------------------- */

static int
find_named(
    const struct Editor_EncountersFile* file,
    const char* name,
    int skip)
{
    for( int i = 0; i < file->line_count; i++ )
        if( i != skip && file->lines[i].kind == EDITOR_ENCOUNTERS_LINE_RECORD
            && strcmp(file->lines[i].encounter->name, name) == 0 )
            return i;
    return -1;
}

struct Editor_EncountersParseResult
Editor_EncountersParse(
    struct Editor_EncountersFile* file,
    const char* text,
    size_t length)
{
    struct Editor_EncountersParseResult result = { 1, 0 };
    size_t at = 0;
    int line_number = 0;

    assert(file);
    assert(text || length == 0);

    Editor_EncountersFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_EncountersLine* line = append_slot(file);
        struct Editor_Encounter* encounter = ToriMem_Alloc(ToriMem_Editor(), sizeof(*encounter));
        int kind;
        int ok;

        assert(encounter);
        line_number++;
        line->text = copy_text(begin, line_length);
        kind = Editor_EncountersParseLine(line->text, encounter);
        ok = kind >= 0;
        if( kind == EDITOR_ENCOUNTERS_LINE_RECORD )
        {
            /* Names are unique: a second record of a name would make every
             * by-name verb ambiguous. */
            if( find_named(file, encounter->name, file->line_count - 1) >= 0 )
                ok = 0;
            line->kind = EDITOR_ENCOUNTERS_LINE_RECORD;
            line->encounter = encounter;
            if( !ok )
            {
                line->kind = EDITOR_ENCOUNTERS_LINE_TEXT;
                line->encounter = NULL;
                ToriMem_Free(ToriMem_Editor(), encounter);
            }
        }
        else
        {
            line->kind = EDITOR_ENCOUNTERS_LINE_TEXT;
            ToriMem_Free(ToriMem_Editor(), encounter);
        }
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
Editor_EncountersEmit(
    const struct Editor_EncountersFile* file,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;

    assert(file);
    assert(out || out_capacity == 0);

    for( int i = 0; i < file->line_count; i++ )
    {
        put(out, out_capacity, &total, file->lines[i].text, strlen(file->lines[i].text));
        if( i + 1 < file->line_count || file->final_newline )
            put(out, out_capacity, &total, "\n", 1);
    }
    put_terminate(out, out_capacity, total);
    return total;
}

int
Editor_EncountersCount(const struct Editor_EncountersFile* file)
{
    int count = 0;

    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        count += file->lines[i].kind == EDITOR_ENCOUNTERS_LINE_RECORD;
    return count;
}

const struct Editor_Encounter*
Editor_EncountersNth(
    const struct Editor_EncountersFile* file,
    int nth)
{
    assert(file);
    for( int i = 0; i < file->line_count; i++ )
    {
        if( file->lines[i].kind != EDITOR_ENCOUNTERS_LINE_RECORD )
            continue;
        if( nth-- == 0 )
            return file->lines[i].encounter;
    }
    return NULL;
}

int
Editor_EncountersFind(
    const struct Editor_EncountersFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    return find_named(file, name, -1);
}

int
Editor_EncountersColour(
    const struct Editor_EncountersFile* file,
    int nth)
{
    const struct Editor_Encounter* encounter;

    assert(file);
    encounter = Editor_EncountersNth(file, nth);
    assert(encounter);
    return encounter->colour >= 0 ? encounter->colour : nth % EDITOR_ENCOUNTER_COLOURS;
}

/* ---- one set ------------------------------------------------------------------- */

void
Editor_EncounterInit(
    struct Editor_Encounter* encounter,
    const char* name)
{
    assert(encounter);
    assert(name);
    assert(Editor_EncounterNameValid(name));
    memset(encounter, 0, sizeof(*encounter));
    encounter->colour = -1;
    snprintf(encounter->name, sizeof(encounter->name), "%s", name);
}

static int
spawn_valid(const struct Editor_EncounterSpawn* spawn)
{
    return Editor_EncounterSpawnNameValid(spawn->name) && spawn->x >= 0 && spawn->x <= EDITOR_ENCOUNTER_TILE_MAX
           && spawn->z >= 0 && spawn->z <= EDITOR_ENCOUNTER_TILE_MAX && spawn->level >= 0 && spawn->level <= 3
           && spawn->count >= 1;
}

static int
spawn_same(
    const struct Editor_EncounterSpawn* a,
    const struct Editor_EncounterSpawn* b)
{
    return a->is_obj == b->is_obj && strcmp(a->name, b->name) == 0 && a->x == b->x && a->z == b->z
           && a->level == b->level && a->count == b->count;
}

int
Editor_EncounterValid(const struct Editor_Encounter* encounter)
{
    assert(encounter);
    if( !Editor_EncounterNameValid(encounter->name) || !Editor_EncounterNoteValid(encounter->note) )
        return 0;
    if( encounter->colour < -1 || encounter->colour >= EDITOR_ENCOUNTER_COLOURS )
        return 0;
    if( encounter->spawn_count < 0 || encounter->spawn_count > EDITOR_ENCOUNTER_SPAWNS_MAX )
        return 0;
    for( int i = 0; i < encounter->spawn_count; i++ )
    {
        if( !spawn_valid(&encounter->spawns[i]) )
            return 0;
        for( int j = 0; j < i; j++ )
            if( spawn_same(&encounter->spawns[i], &encounter->spawns[j]) )
                return 0;
    }
    return 1;
}

int
Editor_EncounterFindSpawn(
    const struct Editor_Encounter* encounter,
    const struct Editor_EncounterSpawn* spawn)
{
    assert(encounter);
    assert(spawn);
    for( int i = 0; i < encounter->spawn_count; i++ )
        if( spawn_same(&encounter->spawns[i], spawn) )
            return i;
    return -1;
}

int
Editor_EncounterAddSpawn(
    struct Editor_Encounter* encounter,
    const struct Editor_EncounterSpawn* spawn)
{
    assert(encounter);
    assert(spawn);
    if( encounter->spawn_count >= EDITOR_ENCOUNTER_SPAWNS_MAX || !spawn_valid(spawn)
        || Editor_EncounterFindSpawn(encounter, spawn) >= 0 )
        return 0;
    encounter->spawns[encounter->spawn_count++] = *spawn;
    return 1;
}

void
Editor_EncounterRemoveSpawn(
    struct Editor_Encounter* encounter,
    int index)
{
    assert(encounter);
    assert(index >= 0);
    assert(index < encounter->spawn_count);
    memmove(&encounter->spawns[index], &encounter->spawns[index + 1],
        (size_t)(encounter->spawn_count - index - 1) * sizeof(encounter->spawns[0]));
    encounter->spawn_count--;
}

int
Editor_EncounterCentre(
    const struct Editor_Encounter* encounter,
    int* out_x,
    int* out_z,
    int* out_level)
{
    long sum_x = 0;
    long sum_z = 0;
    int per_level[4] = { 0, 0, 0, 0 };
    int level = 0;
    int n;

    assert(encounter);
    assert(out_x);
    assert(out_z);
    assert(out_level);
    n = encounter->spawn_count;
    if( n == 0 )
        return 0;
    for( int i = 0; i < n; i++ )
    {
        sum_x += encounter->spawns[i].x;
        sum_z += encounter->spawns[i].z;
        per_level[encounter->spawns[i].level]++;
    }
    for( int l = 1; l < 4; l++ )
        if( per_level[l] > per_level[level] )
            level = l;
    *out_x = (int)((sum_x * 2 + n) / (2 * n));
    *out_z = (int)((sum_z * 2 + n) / (2 * n));
    *out_level = level;
    return 1;
}

int
Editor_EncounterNpcNames(
    const struct Editor_Encounter* encounter,
    char out[][EDITOR_CMD_NAME_MAX],
    int max)
{
    int count = 0;

    assert(encounter);
    assert(out || max == 0);
    for( int i = 0; i < encounter->spawn_count && count < max; i++ )
    {
        int seen = 0;
        if( encounter->spawns[i].is_obj )
            continue;
        for( int j = 0; j < count && !seen; j++ )
            seen = strcmp(out[j], encounter->spawns[i].name) == 0;
        if( !seen )
            snprintf(out[count++], EDITOR_CMD_NAME_MAX, "%s", encounter->spawns[i].name);
    }
    return count;
}

/* ---- the verbs ----------------------------------------------------------------- */

/** Every line of `file`, line `replace` swapped for `with` (NULL: dropped),
 *  `append` (NULL: none) added as a last line. */
static size_t
emit_edited(
    const struct Editor_EncountersFile* file,
    int replace,
    const char* with,
    const char* append,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;
    int last = -1;

    for( int i = 0; i < file->line_count; i++ )
        if( i != replace || with )
            last = i;
    for( int i = 0; i < file->line_count; i++ )
    {
        const char* text = i == replace ? with : file->lines[i].text;
        if( !text )
            continue;
        put(out, out_capacity, &total, text, strlen(text));
        if( i != last || file->final_newline || append )
            put(out, out_capacity, &total, "\n", 1);
    }
    if( append )
    {
        put(out, out_capacity, &total, append, strlen(append));
        put(out, out_capacity, &total, "\n", 1);
    }
    put_terminate(out, out_capacity, total);
    return total;
}

int
Editor_EncountersTextWith(
    const struct Editor_EncountersFile* file,
    const char* old_name,
    const struct Editor_Encounter* encounter,
    char* out,
    size_t out_capacity,
    size_t* out_length)
{
    size_t line_length;
    char* line;
    int replace = -1;
    int holder;

    assert(file);
    assert(encounter);
    assert(out || out_capacity == 0);
    assert(out_length);
    line_length = Editor_EncounterFormat(encounter, NULL, 0);

    if( !Editor_EncounterValid(encounter) )
        return 0;
    if( old_name && old_name[0] )
    {
        replace = find_named(file, old_name, -1);
        if( replace < 0 )
            return 0;
    }
    holder = find_named(file, encounter->name, replace);
    if( holder >= 0 )
        return 0;

    line = ToriMem_Alloc(ToriMem_Editor(), line_length + 1);
    assert(line);
    Editor_EncounterFormat(encounter, line, line_length + 1);
    if( replace >= 0 )
        *out_length = emit_edited(file, replace, line, NULL, out, out_capacity);
    else if( file->line_count == 0 )
    {
        size_t total = 0;
        put(out, out_capacity, &total, k_header, sizeof(k_header) - 1);
        put(out, out_capacity, &total, line, line_length);
        put(out, out_capacity, &total, "\n", 1);
        put_terminate(out, out_capacity, total);
        *out_length = total;
    }
    else
        *out_length = emit_edited(file, -1, NULL, line, out, out_capacity);
    ToriMem_Free(ToriMem_Editor(), line);
    return 1;
}

int
Editor_EncountersTextWithout(
    const struct Editor_EncountersFile* file,
    const char* name,
    char* out,
    size_t out_capacity,
    size_t* out_length)
{
    int index;
    size_t length;
    char* text;

    assert(file);
    assert(name);
    assert(out || out_capacity == 0);
    assert(out_length);
    index = find_named(file, name, -1);
    if( index < 0 )
        return 0;
    /* A file that would be exactly the header goes back to absent. */
    length = emit_edited(file, index, NULL, NULL, NULL, 0);
    text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(text);
    emit_edited(file, index, NULL, NULL, text, length + 1);
    if( length == sizeof(k_header) - 1 && memcmp(text, k_header, length) == 0 )
    {
        ToriMem_Free(ToriMem_Editor(), text);
        put_terminate(out, out_capacity, 0);
        *out_length = 0;
        return 1;
    }
    ToriMem_Free(ToriMem_Editor(), text);
    *out_length = emit_edited(file, index, NULL, NULL, out, out_capacity);
    return 1;
}

/* ---- the Drops tab ---------------------------------------------------------------- */

/** [begin, end) of the block headed `[<header>]`: from the line after its
 *  header to the next line starting '['. */
static int
block_find(
    const char* text,
    size_t length,
    const char* header,
    const char** out_begin,
    const char** out_end)
{
    size_t const header_length = strlen(header);
    size_t at = 0;

    while( at < length )
    {
        const char* line = text + at;
        const char* newline = memchr(line, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - line) : length - at;
        size_t content = line_length;

        while( content > 0 && is_space(line[content - 1]) )
            content--;
        if( content == header_length + 2 && line[0] == '[' && line[content - 1] == ']'
            && memcmp(line + 1, header, header_length) == 0 )
        {
            size_t next = newline ? (size_t)(newline - text) + 1 : length;
            *out_begin = text + next;
            while( next < length && text[next] != '[' )
            {
                const char* nl = memchr(text + next, '\n', length - next);
                next = nl ? (size_t)(nl - text) + 1 : length;
            }
            *out_end = text + next;
            return 1;
        }
        at += line_length + (newline ? 1 : 0);
    }
    return 0;
}

/** The arguments of `obj_add(npc_coord, <obj>, <count>, ...)` in [begin, end). */
static int
parse_obj_add(
    const char* begin,
    const char* end,
    struct Editor_DropRow* out)
{
    static const char k_call[] = "obj_add(npc_coord,";
    const char* at;
    const char* comma;
    const char* stop;
    int depth = 0;

    if( (size_t)(end - begin) < sizeof(k_call) - 1 || memcmp(begin, k_call, sizeof(k_call) - 1) != 0 )
        return 0;
    at = begin + sizeof(k_call) - 1;
    while( at < end && *at == ' ' )
        at++;
    /* The obj: up to the comma outside parentheses. */
    comma = at;
    while( comma < end && (depth > 0 || *comma != ',') )
    {
        depth += *comma == '(' ? 1 : *comma == ')' ? -1 : 0;
        comma++;
    }
    if( comma >= end || !copy_run(out->obj, sizeof(out->obj), at, comma) )
        return 0;
    at = comma + 1;
    while( at < end && *at == ' ' )
        at++;
    stop = at;
    depth = 0;
    while( stop < end && (depth > 0 || *stop != ',') )
    {
        depth += *stop == '(' ? 1 : *stop == ')' ? -1 : 0;
        stop++;
    }
    if( stop >= end || !copy_run(out->count, sizeof(out->count), at, stop) )
        return 0;
    return out->obj[0] != '\0' && out->count[0] != '\0';
}

/** A line is `<prefix><int><suffix>` exactly; the int into *out. */
static int
line_int_between(
    const char* begin,
    const char* end,
    const char* prefix,
    const char* suffix,
    int* out)
{
    size_t const p = strlen(prefix);
    size_t const s = strlen(suffix);
    char digits[16];

    if( (size_t)(end - begin) <= p + s || memcmp(begin, prefix, p) != 0 || memcmp(end - s, suffix, s) != 0 )
        return 0;
    if( !copy_run(digits, sizeof(digits), begin + p, end - s) )
        return 0;
    return parse_int(digits, 0, 1 << 30, out);
}

int
Editor_DropsRead(
    const char* text,
    size_t length,
    const char* header,
    struct Editor_DropTable* out)
{
    const char* begin;
    const char* end;
    const char* at;
    int denominator = 0;
    int running = 0;
    /* What the next obj_add is: 0 always, 1 a walk arm, 2 the remains. */
    int pending = 0;
    int pending_share = 0;

    assert(text || length == 0);
    assert(header);
    assert(out);

    if( !out->found && out->row_count == 0 )
        memset(out, 0, sizeof(*out));
    if( !block_find(text, length, header, &begin, &end) )
        return 0;
    out->found = 1;
    for( at = begin; at < end; )
    {
        const char* newline = memchr(at, '\n', (size_t)(end - at));
        const char* line_end = newline ? newline : end;
        const char* b = at;
        const char* e = line_end;
        int k;

        at = newline ? newline + 1 : end;
        trim(&b, &e);
        if( b == e || (e - b >= 2 && b[0] == '/' && b[1] == '/') )
            continue;
        if( line_int_between(b, e, "def_int $dropint = random(", ");", &k) && k > 0 )
        {
            denominator = k;
            running = 0;
            continue;
        }
        if( (line_int_between(b, e, "if ($dropint < ", ") {", &k)
                || line_int_between(b, e, "} else if ($dropint < ", ") {", &k))
            && denominator > 0 && k > running )
        {
            pending = 1;
            pending_share = k - running;
            running = k;
            continue;
        }
        if( (size_t)(e - b) == strlen("if (npc_param(death_drop) ! null) {")
            && memcmp(b, "if (npc_param(death_drop) ! null) {", (size_t)(e - b)) == 0 )
        {
            pending = 2;
            continue;
        }
        if( (e - b == 1 && b[0] == '}') )
        {
            pending = 0;
            continue;
        }
        if( out->row_count < EDITOR_DROPS_MAX )
        {
            struct Editor_DropRow* row = &out->rows[out->row_count];
            memset(row, 0, sizeof(*row));
            if( parse_obj_add(b, e, row) )
            {
                if( pending == 1 )
                {
                    row->numerator = pending_share;
                    row->denominator = denominator;
                }
                else
                {
                    row->always = 1;
                    row->numerator = 1;
                    row->denominator = 1;
                }
                out->row_count++;
                continue;
            }
        }
        /* A call: where the rows are when the block states none. */
        if( (b[0] == '~' || b[0] == '@') && e[-1] == ';' && !out->call[0] )
        {
            const char* name_end = b + 1;
            while( name_end < e - 1 && *name_end != '(' )
                name_end++;
            if( copy_run(out->call, sizeof(out->call), b + 1, name_end) && name_end == e - 1 )
                continue;
            out->call[0] = '\0';
        }
        out->unread++;
    }
    return 1;
}

void
Editor_DropChance(
    const struct Editor_DropRow* row,
    char* out,
    size_t out_size)
{
    assert(row);
    assert(out);
    if( row->always || row->denominator <= 0 )
        snprintf(out, out_size, "always");
    else
        snprintf(out, out_size, "%d/%d (%.2f%%)", row->numerator, row->denominator,
            100.0 * (double)row->numerator / (double)row->denominator);
}

/* ---- the Combat tab ---------------------------------------------------------------- */

const char*
Editor_CombatTemplateName(enum Editor_CombatTemplate template_kind)
{
    switch( template_kind )
    {
    case EDITOR_COMBAT_MELEE:
        return "melee";
    case EDITOR_COMBAT_CASTER:
        return "caster";
    case EDITOR_COMBAT_RANGER:
        return "ranger";
    case EDITOR_COMBAT_HEALER:
        return "healer";
    case EDITOR_COMBAT_TEMPLATE_COUNT:
        break;
    }
    assert(0 && "Editor_CombatTemplateName: no such template");
    return "";
}

int
Editor_CombatTemplateBody(
    enum Editor_CombatTemplate template_kind,
    const char* caster_source,
    size_t caster_length,
    char out_trigger[32],
    char* out,
    size_t out_capacity)
{
    const char* body = NULL;
    size_t body_length = 0;

    assert(out_trigger);
    assert(out || out_capacity == 0);
    assert(caster_source || caster_length == 0);
    assert(template_kind >= 0 && template_kind < EDITOR_COMBAT_TEMPLATE_COUNT);

    snprintf(out_trigger, 32, "ai_opplayer2");
    if( template_kind == EDITOR_COMBAT_MELEE )
        body = "~npc_meleeattack;\n";
    else if( template_kind == EDITOR_COMBAT_RANGER )
        body = "~npc_rangeattack;\n";
    else if( template_kind == EDITOR_COMBAT_HEALER )
        body = "~npc_meleeattack;\nnpc_statheal(hitpoints, 2, 0);\n";
    if( body )
        body_length = strlen(body);
    else
    {
        /* The caster: the first ai swing the dark wizard's file states, its
         * body verbatim (comments and blank lines dropped). */
        size_t at = 0;
        const char* begin = NULL;
        const char* end = NULL;
        size_t total = 0;

        while( at < caster_length && !begin )
        {
            const char* line = caster_source + at;
            const char* newline = memchr(line, '\n', caster_length - at);
            size_t const line_length = newline ? (size_t)(newline - line) : caster_length - at;
            const char* comma;
            if( line_length > 15 && (memcmp(line, "[ai_opplayer2,", 14) == 0 || memcmp(line, "[ai_applayer2,", 14) == 0) )
            {
                char header[EDITOR_CMD_NAME_MAX * 2];
                const char* close = memchr(line, ']', line_length);
                comma = line + 13;
                if( close && copy_run(header, sizeof(header), line + 1, close) )
                {
                    snprintf(out_trigger, 32, "%.*s", (int)(comma - line - 1), line + 1);
                    if( block_find(caster_source, caster_length, header, &begin, &end) )
                        break;
                }
            }
            at += line_length + (newline ? 1 : 0);
        }
        if( !begin )
        {
            snprintf(out_trigger, 32, "ai_opplayer2");
            put_terminate(out, out_capacity, 0);
            return -1;
        }
        for( const char* p = begin; p < end; )
        {
            const char* newline = memchr(p, '\n', (size_t)(end - p));
            const char* e = newline ? newline : end;
            const char* b = p;
            const char* content_end = e;
            p = newline ? newline + 1 : end;
            {
                const char* lead = b;
                const char* tail = content_end;
                trim(&lead, &tail);
                if( lead == tail || (tail - lead >= 2 && lead[0] == '/' && lead[1] == '/') )
                    continue;
            }
            while( content_end > b && is_space(content_end[-1]) )
                content_end--;
            put(out, out_capacity, &total, b, (size_t)(content_end - b));
            put(out, out_capacity, &total, "\n", 1);
        }
        put_terminate(out, out_capacity, total);
        return (int)total;
    }
    {
        size_t total = 0;
        put(out, out_capacity, &total, body, body_length);
        put_terminate(out, out_capacity, total);
        return (int)total;
    }
}
